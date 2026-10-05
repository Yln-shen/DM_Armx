// dm_joint.hpp —— 单个关节（语义对齐 Python 的 joint.py）。
//
// 边界：
//   - 不拥有控制循环（`set_*` 只发一帧）、不 poll、不写寄存器；
//   - 不知道 ROS、不读 yaml —— 参数由调用方（M5 的插件）填进 `JointConfig`；
//   - 支持 **mode 1(MIT) 与 mode 2(位置速度)**；力位混控(4)与速度(3)没移植。
//     ros2_control 这条链的**轨迹执行仍走 POS_VEL**（固件闭环稳态误差能到 ~0）；
//     MIT 是唯一能**直接给力矩**的模式，留给重力前馈这类需要 `tau_ff` 的场合。
//
// 三条真机教训内建在这里：
//   - 位置类模式下**没有实测位置就拒绝使能**（切模式时电机内部指令被清零，使能会朝零位冲）；
//   - 使能后**立刻补"保持帧"**，且保持帧用**电机侧实测值**、不经过软限位/换算（钳了就不是保持）；
//     （mode 1 的保持帧是"零增益零前馈"，出力恒为 0，与 Python 侧一致）
//   - `prepare_frame()` 的顺序固定为 NaN 拦 → 软限位 → 换算 → PMAX 钳位。
#ifndef MOTOR_DRIVER_HARDWARE__DM_JOINT_HPP_
#define MOTOR_DRIVER_HARDWARE__DM_JOINT_HPP_

#include <cstdint>
#include <optional>
#include <string>

#include "motor_driver_hardware/dm_bus.hpp"
#include "motor_driver_hardware/dm_frames.hpp"

namespace motor_driver_hardware
{

constexpr double kHoldVlim = 0.1;   // 保持帧的速度幅值（与 Python 侧 HOLD_VLIM 一致）
// MIT 钳位日志的限流间隔（同 Python 的 CLAMP_WARN_INTERVAL）
constexpr double kClampWarnInterval = 5.0;

// 模型坐标（URDF / ros2_control 接口用的那一套） ⇄ 我们的关节坐标 q_ours。
//   q_urdf = sign · q_ours + zero_shift        （sign/zero_shift 来自 arm_description/config/align.yaml）
// 放在这里是为了让**插件与测试用同一份公式**，而不是各写一遍。
inline double model_to_ours(double q_urdf, int sign, double zero_shift)
{
  return static_cast<double>(sign) * (q_urdf - zero_shift);
}

inline double ours_to_model(double q_ours, int sign, double zero_shift)
{
  return static_cast<double>(sign) * q_ours + zero_shift;
}

struct JointConfig
{
  std::string name;
  uint8_t motor_id = 1;
  std::string motor_type;                       // "4340P" / "4310"（只用于报错信息）
  int direction = 1;                            // ±1：电机侧 = direction × 关节侧 + offset
  double offset = 0.0;
  Limit limit{12.5, 10.0, 28.0};                // 档位（必须来自 0x15/0x16/0x17 回读）
  std::optional<double> position_min;           // 关节侧软限位（我们的标定）；空 = 不钳
  std::optional<double> position_max;
  // MIT 的力矩上限（关节侧 N·m，DESIGN §4.3 的额定值：j1~j3 12.0 / j4~j6 3.5）。
  // 空 = 不钳（单关节直用时）；注意它**不是**档位里的 `limit.t_max`（那是电机峰值 28/10）。
  std::optional<double> torque_max;
  int mode = 2;                                 // 1 = MIT / 2 = POS_VEL（别的模式没移植）
};

struct JointState
{
  std::string name;
  uint8_t motor_id = 0;
  uint8_t err = 0;
  double position = 0.0;      // 关节侧（已换算）
  double velocity = 0.0;      // 关节侧（只乘 direction）
  double torque = 0.0;        // 关节侧（只乘 direction）
  uint8_t t_mos_raw = 0;
  uint8_t t_rotor_raw = 0;
  bool enabled = false;
  double stamp = 0.0;
};

class Joint
{
public:
  // 构造时会**校验/注册**到 bus（与 Python 侧一致）：总线上一台电机只能有一个映射范围，
  // 否则解出来的力矩差数倍且不报错。
  Joint(MotorBus & bus, const JointConfig & cfg);
  ~Joint() = default;
  Joint(const Joint &) = delete;
  Joint & operator=(const Joint &) = delete;

  const JointConfig & config() const {return cfg_;}
  const std::string & name() const {return cfg_.name;}
  uint8_t motor_id() const {return cfg_.motor_id;}
  int mode() const {return cfg_.mode;}

  // ── 换算与钳位（纯函数，和 Python 的公式逐条对应）──
  double joint_to_motor(double joint_value) const;   // direction × v + offset
  double motor_to_joint(double motor_value) const;   // direction × (v − offset)
  double clamp(double joint_value) const;            // 软限位（NaN 不在这里拦，见 prepare_frame）
  double clamp_pmax(double motor_value) const;       // 电机侧 ±PMAX

  // 发送前的统一处理：NaN 拦 → 软限位 → 换算 → PMAX。返回**电机侧**位置。
  double prepare_frame(double joint_pos) const;
  // 同 prepare_frame，但**跳过软限位钳位**（NaN 拦与 PMAX 钳位保留）。
  // 给"保持当前位置"这类帧用：它的语义是"待在你现在的位置"，**任何钳位都会凭空造出一个
  // PD 项**。真机踩过（2026-10-05）：激活时 j4 被软限位钳掉 0.176 rad ⇒ kp=25 下
  // PD=4.4 > torque_max=3.5 ⇒ **拒发 ⇒ FATAL ⇒ 整条链起不来**。POS_VEL 的保持帧
  // 早就故意绕过钳位（陷阱 #21），MIT 的保持帧也要同样处理。
  double prepare_frame_raw(double joint_pos) const;

  // ── 发送（每条只发一帧）──
  void set_pos_vel(double joint_pos, double vlim);   // 仅 mode 2
  // 仅 mode 1。`tau` 是**关节侧** N·m（内部只乘 direction，不加 offset）。
  // `torque_max` 不是空时先按**预测总力矩**钳 `tau_ff`（见 clamp_mit_torque）。
  // bypass_soft_limits=true ⇒ 目标**不做软限位钳位**（见 prepare_frame_raw）。
  // 只给"保持当前位置"这种帧用；轨迹/点动一律保持默认 false。
  void set_mit(double kp, double kd, double joint_pos, double dq = 0.0, double tau = 0.0,
    bool bypass_soft_limits = false);
  void enable();     // 命令帧 + 保持帧（用电机侧实测位置，绝不朝零位冲）
  void disable();

  // ── 状态 ──
  bool has_state() const;
  JointState get_state() const;      // 没有反馈 ⇒ 抛（"从没收到过"不该被当成 0）
  void assert_healthy() const;       // 只查 ERR；故障 ⇒ 先失能再抛

private:
  // 按 `torque_max` 钳 MIT 的 `tau_ff`。入参与返回都是**电机侧**量（与 Python 的
  // `_clamp_mit_torque` 逐条对应）：PD 项自己超 ⇒ 抛（钳 tau_ff 救不回来）；
  // 否则把 tau_ff 钳到"总力矩 = ±torque_max"。
  double clamp_mit_torque(double kp, double kd, double q_des_motor, double dq_des_motor,
    double tau_ff_motor) const;

  MotorBus & bus_;
  JointConfig cfg_;
  mutable double clamp_warn_at_ = -kClampWarnInterval;   // 首次钳位一定打日志
};

}  // namespace motor_driver_hardware

#endif  // MOTOR_DRIVER_HARDWARE__DM_JOINT_HPP_
