// dm_system_interface.hpp —— ros2_control 的 `SystemInterface` 插件（把 C++ 核接到真机）。
//
// 三层坐标系（**这是本文件最容易看错的地方**）：
//     URDF / 模型坐标  ←sign/zero_shift→  我们的关节坐标 q_ours  ←direction/offset→  电机 p_m
//   - ros2_control 的**命令与状态接口都是模型坐标**（和 URDF、JTC 一致）；
//   - `dm_joint` 的"关节侧"是 **q_ours**（本项目标定的那一套，软限位也在这一套里）；
//   - 换算常量：`sign`/`zero_shift` 来自 arm_description/config/align.yaml，
//     `direction`/`offset` 来自 motor_driver/config/joint.yaml —— 由 xacro 变成 <param> 传进来。
//
// 安全设计（都是真机踩出来的，别删）：
//   1. **命令是 NaN 就发"保持"**（目标是**使能那一刻锁定**的电机侧位置）：ros2_control 里没被写过的
//      命令接口就是 NaN，当 0 处理 = 一使能就朝零位冲。锁定而不是"每圈重读实测值"，是因为后者没有
//      回复力 —— 重力能把关节慢慢压走（陷阱 #38，2026-10-05 真机实测 20s 掉 0.0027 rad）；
//   2. `write()` 每圈给**所有**关节发帧：不发帧的会被电机侧 500ms 看门狗打成锁存 ERR=13（只能断电清）；
//   3. `enable_on_activate=false`（默认）时**不发任何控制帧**，只发 0x7FF 刷新帧读状态 ⇒ 可做"只读"验收；
//   4. `on_deactivate()` **全部失能**；`read()` 里 ERR 不是 0/1 ⇒ 返回 ERROR 让 CM 停控制器。
#ifndef MOTOR_DRIVER_HARDWARE__DM_SYSTEM_INTERFACE_HPP_
#define MOTOR_DRIVER_HARDWARE__DM_SYSTEM_INTERFACE_HPP_

#include <hardware_interface/system_interface.hpp>
#include <hardware_interface/types/hardware_component_interface_params.hpp>
#include <hardware_interface/types/hardware_interface_return_values.hpp>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp_lifecycle/state.hpp>

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "motor_driver_hardware/dm_bus.hpp"
#include "motor_driver_hardware/dm_gravity.hpp"
#include "motor_driver_hardware/dm_joint.hpp"
#include "motor_driver_hardware/dm_serial.hpp"

namespace motor_driver_hardware
{

class DmSystemInterface : public hardware_interface::SystemInterface
{
public:
  DmSystemInterface() = default;
  // 测试用：注入假串口。生产（pluginlib 构造）走默认构造 + on_configure 里自建 SerialPort
  explicit DmSystemInterface(SerialIo * injected_io)
  : injected_io_(injected_io) {}
  ~DmSystemInterface() override;

  // 用**新版**签名（jazzy）：老版 `on_init(const HardwareInfo&)` 已被标记 deprecated
  hardware_interface::CallbackReturn on_init(
    const hardware_interface::HardwareComponentInterfaceParams & params) override;
  hardware_interface::CallbackReturn on_configure(
    const rclcpp_lifecycle::State & previous_state) override;
  hardware_interface::CallbackReturn on_activate(
    const rclcpp_lifecycle::State & previous_state) override;
  hardware_interface::CallbackReturn on_deactivate(
    const rclcpp_lifecycle::State & previous_state) override;
  hardware_interface::CallbackReturn on_cleanup(
    const rclcpp_lifecycle::State & previous_state) override;
  hardware_interface::CallbackReturn on_error(
    const rclcpp_lifecycle::State & previous_state) override;

  std::vector<hardware_interface::StateInterface> export_state_interfaces() override;
  std::vector<hardware_interface::CommandInterface> export_command_interfaces() override;

  hardware_interface::return_type read(
    const rclcpp::Time & time, const rclcpp::Duration & period) override;
  hardware_interface::return_type write(
    const rclcpp::Time & time, const rclcpp::Duration & period) override;

private:
  struct JointParams
  {
    std::string name;
    JointConfig cfg;                                  // direction/offset/档位/软限位（q_ours 那一套）
    int sign = 1;                                     // 模型对齐：q_urdf = sign·q_ours + zero_shift
    double zero_shift = 0.0;
    std::optional<std::array<double, 4>> pid;         // KP_ASR/KI_ASR/KP_APR/KI_APR；空 = 不写 PID
    std::unique_ptr<Joint> joint;
    int missing_streak = 0;
    uint8_t err_warned = 0;                           // 只读模式下"这个故障码已提示过"
    std::optional<double> hold_pos;                   // 使能那一刻锁定的保持目标（电机侧）；空 = 还没锁定
    // MIT（gravity_ff=true）才用得上。torque_max 存进 cfg.torque_max（关节侧额定值）。
    double kp_hold = 0.0;
    double kd_hold = 0.0;
    // ki_hold：**宿主侧积分增益**（默认 0 = 不生效）。MIT 模式下固件没有积分，稳态误差被
    // "不可重复扰动/kp" 卡住（真机 j3 在 kp=7 下差 0.134 rad，而 POS_VEL 是 0.0016 rad）
    // ⇒ τ_i 加进 MIT 帧的 t_ff，就变成"宿主侧 PI + 重力前馈"。
    double ki_hold = 0.0;
    double tau_i = 0.0;                               // 积分器输出（**关节侧** N·m）
    std::optional<double> prev_q_ref_ours;            // 上一周期的参考（判断命令是否跳变 ⇒ 复位积分）
    std::optional<double> hold_ours;                  // 同一个保持目标的**关节侧**值（MIT 的保持帧要它）
    double last_tau_ours = 0.0;                       // 上一次算出来的重力项；算不出来时拿它顶（必须继续喂狗）
  };

  bool parse_params(const hardware_interface::HardwareInfo & info);   // 失败返回 false（已打日志）
  void build_hardware();                                             // on_configure：串口/总线/关节对象
  void teardown();                                                   // on_cleanup / 出错收尾
  void disable_all_quietly();                                        // 失能，绝不抛

  // 模型坐标 ⇄ 我们的关节坐标
  double model_to_ours(std::size_t i, double q_urdf) const;
  double ours_to_model(std::size_t i, double q_ours) const;

  // gravity_ff=true 时 write() 走这条：实测姿态 → 重力项 +（可选）宿主侧积分 → 每关节 MIT 帧
  // （含残差守卫）。`period` 是积分步长 —— 用控制循环给的真实周期，别写死 1/100。
  hardware_interface::return_type write_gravity_ff(
    const rclcpp::Time & time, const rclcpp::Duration & period);

  std::vector<JointParams> joints_;
  std::vector<double> hw_positions_;
  std::vector<double> hw_velocities_;
  // effort（模型坐标 N·m）：来自电机反馈的**电流估计**，不是力矩传感器。只当量级参考。
  std::vector<double> hw_efforts_;
  std::vector<double> hw_commands_;
  // velocity 命令接口（模型坐标 rad/s）：只有 gravity_ff=true 走 MIT 时才用得上（当 dq_des）。
  // 这两条链都导出它 —— ros2_controllers.yaml 是 mock/真机共用的，少一个接口 JTC 会配置失败。
  std::vector<double> hw_vel_commands_;

  SerialIo * injected_io_ = nullptr;                  // 测试注入（不拥有）
  std::unique_ptr<SerialPort> owned_io_;              // 生产自持
  SerialIo * io_ = nullptr;
  std::unique_ptr<MotorBus> bus_;

  std::string device_{"/dev/ttyACM1"};
  int baud_ = 921600;
  bool enable_on_activate_ = false;                   // 默认**只读**
  double vlim_ = 1.0;                                 // POS_VEL 速度上限（rad/s）
  int fail_streak_limit_ = 10;                        // 连续多少圈收不到反馈就报错
  // 重力前馈总开关（默认 false ⇒ 一切照旧走 POS_VEL）。
  bool gravity_ff_ = false;
  std::string urdf_path_;                             // 动力学模型路径（gravity_ff 时才要求非空）
  // 重力前馈缩放 0~1：**分级上电用**（第一次给真机发力矩先 0.2，确认方向对再往上加）
  double gravity_ff_scale_ = 1.0;
  // 重力模型：gravity_ff=true 时在 on_configure 建好（构造即抛 ⇒ FATAL，不静默降级）
  std::unique_ptr<GravityModel> gravity_;
  // 残差守卫（|q−q_hold| 或 |dq| 越界）⇒ 锁存：**把 tau_ff 全部置 0**（仍留 MIT+kp_hold），
  // 并只报一次 ERROR。比"运行期写寄存器切回 POS_VEL"简单也安全得多（切模式要走陷阱 #2 的流程）。
  bool gravity_guard_tripped_ = false;
  // ⚠️ 速度那一路必须**宽限 + 防抖**：使能到第一次 write 之间 MIT 是零增益的，机械臂真的会掉，
  //    速度能到 0.7~1.5 rad/s。2026-10-05 真机就因为这个把前馈**永久锁死**了。
  rclcpp::Time gravity_guard_since_;                  // 第一次判守卫的时刻（宽限从这里算）
  bool gravity_guard_since_set_ = false;
  rclcpp::Time gravity_vel_over_since_;               // 速度首次越限的时刻
  bool gravity_vel_over_ = false;                     // 当前是否处于"速度越限"状态
  // q 缓冲：tau_ours() 要按关节顺序读，不每帧分配
  std::vector<double> q_urdf_buf_;
  std::vector<double> tau_ours_buf_;
};

}  // namespace motor_driver_hardware

#endif  // MOTOR_DRIVER_HARDWARE__DM_SYSTEM_INTERFACE_HPP_
