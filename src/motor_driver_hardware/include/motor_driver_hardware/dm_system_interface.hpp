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

#include <arm_msgs/msg/hardware_status.hpp>
#include <arm_msgs/srv/hardware_emergency_stop.hpp>
#include <arm_msgs/srv/set_enabled.hpp>

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
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

  // ── 运行期使能（服务回调与单测**共用**这两个入口）──────────────────────────
  // 公开的理由：这是服务回调的实现体，而且**单测没有节点**（服务建不起来）⇒ 直接调它们
  // 就能把"前提检查 → 状态机 → 结果"整条路测出来（与本文件"注入假串口"的先例一致）。
  // 前提检查 + 置请求：**不阻塞、不发帧**（真正的活由 write() 的状态机干）；why 里写拒绝原因。
  bool request_enable(bool enable, std::string * why);
  // 轮询等状态机完成（服务回调用）。超时返回 false（说明 CM 的 update 没在跑）。
  bool wait_enable_done(double timeout_s, std::string * why);
  bool armed() const {return armed_;}
  // write() 里逐周期推进的状态机（失能请求优先、可打断使能序列）。
  void advance_enable_fsm();
  // read() 里每 kStatusEvery 圈调一次（10 Hz）：**只有插件能发** —— ros2_control 的状态接口
  // 只有控制器读得到，外部节点拿不到 ERR/温度/守卫（见 MANAGE_ARCH M-5）。
  void publish_status();
  // 服务处理（在 on_configure 里注册）
  void handle_set_enabled(
    const std::shared_ptr<arm_msgs::srv::SetEnabled::Request> req,
    std::shared_ptr<arm_msgs::srv::SetEnabled::Response> res);
  void handle_emergency_stop(std::shared_ptr<arm_msgs::srv::HardwareEmergencyStop::Response> res);

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

  // ── 运行期使能（M9.2，设计见 docs/MANAGE_ARCH.md §5）────────────────────────
  // **为什么需要**：`enable_on_activate` 是硬件组件的 <param>（来自 robot_description，构建/launch 期
  //   定死），CM 的 `set_hardware_component_state` **改不了已加载的参数** ⇒ 启动为只读之后想使能，
  //   只能走插件自己提供的服务（决策 M-1）。
  // **怎么做**：服务回调只做"前提检查 + 置请求"；真正的寄存器 I/O 与使能帧由 `write()` 里的状态机
  //   **逐周期**推进（一个周期最多一次 `0x0A` 写 ≈150 ms，期间**不发控制帧**）；回调随后**阻塞等结果**
  //   （约 1 s）。使能序列与 `on_activate` 共用同一份积木（`write_mode_one` / `enable_all_and_latch`）。
  // ⚠️ 回调能安全阻塞的前提：CM 的 `read/update/write` **不跑在 executor 上**
  //   （`controller_manager.hpp:212` 的注释 + `ros2_control_node` 的 "Spawning RT thread"）
  //   ⇒ 阻塞的回调**不会**卡住状态机（否则就是陷阱 #57 那种死锁）。代价：这 ~1 s 内 CM 的其它服务排队。
  // ⚠️ 状态机必须在 `write()` 的"只读早返回"**之前**（否则只读启动后永远使能不了）。
  // ⚠️ 失能请求**随时可以打断使能序列**（急停要能立刻生效，不用等 6 次寄存器写完）。
  enum class EnablePhase {Idle, WriteMode, EnableAll};
  bool armed_ = false;                     // **真实**使能态（on_activate 里初值 = enable_on_activate_）
  bool active_ = false;                    // 硬件组件是否 ACTIVE（服务的前提）
  EnablePhase phase_ = EnablePhase::Idle;
  std::size_t phase_step_ = 0;             // WriteMode 推进到第几台
  std::atomic<int> enable_req_{0};         // 0=无请求 / 1=请求使能 / -1=请求失能（失能优先）
  std::atomic<int> enable_result_{0};      // 0=未决 / 1=成功 / -1=失败
  std::string enable_fail_why_;            // 失败原因（先写它、再置 enable_result_）
  std::mutex enable_mu_;                   // 只保护 enable_fail_why_
  double enable_timeout_s_ = 3.0;          // 等状态机完成的上限。⚠️ 真机实测整个使能序列只要
                                           // **~70 ms**（6 台 × 寄存器往返 ~12 ms，2026-10-11），
                                           // 给 3 s 只是"CM 的 update 停了"时别把服务挂死。
  // ⚠️ 上面那个"~12 ms"是**实测**：早先文档里的"每台 150 ms"是 Python 寄存器工具自己轮询节拍的
  //    开销，不是这条链的往返延迟。`sync_states` 的 quiet 窗口才是这里真正的阻塞源（~30~50 ms）。

  // ── ROS 侧接口（on_configure 里建；**没有节点时不建** = 单测）──
  rclcpp::Service<arm_msgs::srv::SetEnabled>::SharedPtr set_enabled_srv_;
  rclcpp::Service<arm_msgs::srv::HardwareEmergencyStop>::SharedPtr estop_srv_;
  rclcpp::Publisher<arm_msgs::msg::HardwareStatus>::SharedPtr status_pub_;
  int status_tick_ = 0;                    // 每 kStatusEvery 圈发一次（10 Hz @ 100 Hz）

  // ── 参数解析 ──
  bool parse_params(const hardware_interface::HardwareInfo & info);   // 失败返回 false（已打日志）
  bool parse_one_joint(const hardware_interface::ComponentInfo & j);  // 同上，逐关节
  void log_params() const;

  // ── 骨架 ──
  void build_hardware();                                             // on_configure：串口/总线/关节对象
  void teardown();                                                   // on_cleanup / 出错收尾
  void disable_all_quietly();                                        // 失能，绝不抛

  // ── 使能序列的两块积木（on_activate 与运行期状态机**共用同一份**，别写两遍）──
  void write_mode_one(std::size_t i);       // ① 单台写 0x0A（+ 可选写 PID）
  void enable_all_and_latch();              // ② 全部使能 + 锁保持目标 + MIT 下补真实保持帧
  // 记结果：先写原因（加锁）再置原子量 ⇒ 读到 result≠0 时原因一定已就绪。
  void set_enable_result(int v, const std::string & why);

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
