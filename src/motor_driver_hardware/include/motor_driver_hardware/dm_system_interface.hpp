// dm_system_interface.hpp —— ros2_control 的 `SystemInterface` 插件（把 C++ 核接到真机）。
//
// 三层坐标系（**这是本文件最容易看错的地方**）：
//     URDF / 模型坐标  ←sign/zero_shift→  我们的关节坐标 q_ours  ←direction/offset→  电机 p_m
//   - ros2_control 的**命令与状态接口都是模型坐标**（和 URDF、JTC 一致）；
//   - `dm_joint` 的"关节侧"是 **q_ours**（本项目标定的那一套，软限位也在这一套里）；
//   - 换算常量：`sign`/`zero_shift` 来自 arm_description/config/align.yaml，
//     `direction`/`offset` 来自 motor_driver/config/joint.yaml —— 由 xacro 变成 <param> 传进来。
//
// 两条控制路径（由 `gravity_ff` 选，见 write()）：
//   - `gravity_ff=false`（默认）：整链 POS_VEL，固件闭环，静态精度最好；`write_pos_vel()`。
//   - `gravity_ff=true`：整链 MIT（只有它能直接给力矩）+ 重力前馈；`write_gravity_ff()`。
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

  // ── 生命周期（jazzy 新版签名；老版 on_init(const HardwareInfo&) 已 deprecated）──
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

  // ── 接口导出 ──
  std::vector<hardware_interface::StateInterface> export_state_interfaces() override;
  std::vector<hardware_interface::CommandInterface> export_command_interfaces() override;

  // ── 周期读写 ──
  hardware_interface::return_type read(
    const rclcpp::Time & time, const rclcpp::Duration & period) override;
  hardware_interface::return_type write(
    const rclcpp::Time & time, const rclcpp::Duration & period) override;

private:
  // ── 静态参数：一条关节的 <param>（构造后不再变）──
  struct JointParams
  {
    std::string name;
    JointConfig cfg;                                  // direction/offset/档位/软限位（q_ours 那一套）
    int sign = 1;                                     // 模型对齐：q_urdf = sign·q_ours + zero_shift
    double zero_shift = 0.0;
    std::optional<std::array<double, 4>> pid;         // KP_ASR/KI_ASR/KP_APR/KI_APR；空 = 不写 PID
    std::unique_ptr<Joint> joint;
    // MIT（gravity_ff=true）才用得上：
    double kp_hold = 0.0;
    double kd_hold = 0.0;
    // ki_hold：**宿主侧积分增益**（默认 0 = 不生效）。MIT 固件没有积分，稳态误差被
    // "不可重复扰动/kp" 卡住（真机 j3 在 kp=7 下差 0.134 rad，而 POS_VEL 是 0.0016 rad）
    // ⇒ τ_i 加进 MIT 帧的 t_ff，就变成"宿主侧 PI + 重力前馈"。
    double ki_hold = 0.0;
    // 库仑摩擦前馈（**只在有命令/跑轨迹时生效**）：τ_ff += +friction_c·tanh(v_cmd/friction_v_eps)。
    // 符号取**正**：摩擦力矩反对速度（v>0 时是 −F_c）⇒ 抵消它要给 +F_c·sign(v)。
    // 符号取自**命令速度**（实测速度 12 bit，低速下只有几个 LSB、符号会抖）。
    double friction_c = 0.0;
    double friction_v_eps = 0.0;
  };

  // ── 运行期状态：每周期都会变的东西（与"静态参数"分开，便于一眼看出谁是状态）──
  struct JointRuntime
  {
    int missing_streak = 0;                           // 连续多少圈没反馈
    uint8_t err_warned = 0;                           // 只读模式下"这个故障码已提示过"
    // 使能那一刻锁定的保持目标。`hold_pos` 是**电机侧**（POS_VEL 保持帧要它），
    // `hold_ours` 是**关节侧**同一点（MIT 保持帧要它）。两者别混用。
    std::optional<double> hold_pos;
    std::optional<double> hold_ours;
    // 重力项（**关节侧** N·m，已套 sign；还没乘 gravity_ff_scale）。
    // 算不出来时**保留上一次的值**（照样得发帧喂狗），所以别把它当"这一帧算成功了"的证据。
    double tau_gravity = 0.0;
    // 宿主侧积分器（只有 ki_hold>0 才动）。`tau_i` 是**关节侧** N·m。
    double tau_i = 0.0;
    // 上一周期的参考（关节侧）：用来判"命令跳变 ⇒ 复位积分"。
    std::optional<double> prev_q_ref_ours;
  };

  // ── 重力前馈的残差守卫（锁存：一旦触发就把 τ_ff 全部置 0，见 check_guard）──
  struct GravityGuard
  {
    // 保持（命令 NaN）：偏离**保持点**超过 kRad ⇒ 触发；有命令（跑轨迹）：只看**跟踪误差** > kTrack。
    // 速度那一路只在保持时查（跑轨迹时机械臂本来就以 ~1 rad/s 在动 ⇒ 会误触发）。
    bool tripped = false;
    bool since_set = false;                           // 还没判过守卫（宽限从第一次 check 算起）
    bool vel_over = false;                            // 当前是否处于"速度越限"
    // ⚠️ 两个时刻用**显式构造**给初值：只写 `rclcpp::Time x;` 会让 `GravityGuard{}` 触发
    //    -Wconversion（从 {} 到 explicit 构造），而写 `x{}` 又必须知道时钟类型 ⇒ 干脆写全。
    rclcpp::Time since{0, 0, RCL_ROS_TIME};            // 首次判守卫的时刻
    rclcpp::Time vel_over_since{0, 0, RCL_ROS_TIME};   // 速度首次越限的时刻（防抖从这里算）
  };

  // ── 参数解析 ──
  bool parse_params(const hardware_interface::HardwareInfo & info);   // 失败返回 false（已打日志）
  bool parse_one_joint(const hardware_interface::ComponentInfo & j);  // 同上，逐关节
  void log_params() const;

  // ── 骨架 ──
  void build_hardware();                                             // on_configure：串口/总线/关节对象
  void teardown();                                                   // on_cleanup / 出错收尾
  void disable_all_quietly();                                        // 失能，绝不抛

  // ── 模型坐标 ⇄ 我们的关节坐标（换算公式的真源在 dm_joint.hpp，这里只套每关节的 sign/δ）──
  double model_to_ours(std::size_t i, double q_urdf) const;
  double ours_to_model(std::size_t i, double q_ours) const;

  // ── 重力前馈路径（gravity_ff=true 时 write() 走这条）──
  // `write_gravity_ff()` 只负责"按顺序调用下面四个"；每个函数管一件事。
  hardware_interface::return_type write_gravity_ff(
    const rclcpp::Time & time, const rclcpp::Duration & period);
  // ① 实测姿态 → 每关节的 tau_gravity（关节侧）。算不出来沿用上一次的值（仍发帧喂狗）。
  void refresh_gravity_torque();
  // ② 残差守卫：越界就锁存（之后 τ_ff 恒 0，但仍发 MIT + kp_hold，不失阻尼）。
  void check_guard(const rclcpp::Time & time);
  // ③ 宿主侧积分：只有 ki_hold>0 才动。
  void update_integrators(const rclcpp::Duration & period);
  // ④ 逐关节发 MIT 帧（含摩擦前馈与保持帧的绕限位）。
  hardware_interface::return_type send_mit_frames();

  // ── POS_VEL 路径（gravity_ff=false 时 write() 走这条）──
  hardware_interface::return_type write_pos_vel();

  std::vector<JointParams> joints_;
  std::vector<JointRuntime> rt_;
  std::vector<double> hw_positions_;
  std::vector<double> hw_velocities_;
  // effort（模型坐标 N·m）：来自电机反馈的**电流估计**，不是力矩传感器。只当量级参考。
  std::vector<double> hw_efforts_;
  std::vector<double> hw_commands_;
  // velocity 命令接口（模型坐标 rad/s）：只有 gravity_ff=true 走 MIT 时才用得上（当 dq_des）。
  // 两条链都导出它 —— ros2_controllers.yaml 是 mock/真机共用的，少一个接口 JTC 会配置失败。
  std::vector<double> hw_vel_commands_;

  SerialIo * injected_io_ = nullptr;                  // 测试注入（不拥有）
  std::unique_ptr<SerialPort> owned_io_;              // 生产自持
  SerialIo * io_ = nullptr;
  std::unique_ptr<MotorBus> bus_;

  std::string device_{"/dev/ttyACM0"};
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
  GravityGuard guard_;

  // q 缓冲：tau_ours() 要按关节顺序读，不每帧分配
  std::vector<double> q_urdf_buf_;
  std::vector<double> tau_ours_buf_;
};

}  // namespace motor_driver_hardware

#endif  // MOTOR_DRIVER_HARDWARE__DM_SYSTEM_INTERFACE_HPP_
