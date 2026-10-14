// dm_system_interface.hpp —— ros2_control 的 `SystemInterface` 插件（把 C++ 核接到真机）。
//
// **这一层的职责**（LAYERS §2）：串口与帧 I/O、把上层给的目标**装配成 CAN 帧**、使能/失能（电气动作）、
// 反馈 → 状态接口、**物理**边界（PMAX / `torque_max`）、喂 500 ms 看门狗、ERR 上报。
// **不做**：控制律（τ_g / 摩擦 / 积分 / 守卫策略）、轨迹、模型软限位、业务流程 —— 那些属于控制层/运动层。
//
// 三层坐标系（**这是本文件最容易看错的地方**）：
//     URDF / 模型坐标  ←sign/zero_shift→  我们的关节坐标 q_ours  ←direction/offset→  电机 p_m
//   - ros2_control 的**命令与状态接口都是模型坐标**（和 URDF、JTC 一致）；
//   - `dm_joint` 的"关节侧"是 **q_ours**（本项目标定的那一套，软限位也在这一套里）；
//   - 换算常量：`sign`/`zero_shift` 来自 arm_description/config/align.yaml，
//     `direction`/`offset` 来自 motor_driver/config/joint.yaml —— 由 xacro 变成 <param> 传进来。
//
// 两种控制模式由 **`effort` 命令接口的 NaN 语义**选（D-L1，见 docs/LAYERS.md §4 与 dm_frame_plan.hpp）：
//   - `effort` 是 NaN（没有控制器声索力矩）⇒ **POS_VEL**：位置给固件闭环，静态精度最好；
//   - `effort` 是有限值（控制器在写力矩）  ⇒ **MIT**：硬件的 PD（`kp_hold`/`kd_hold`）**加上**
//     `effort` 当 `tau_ff`（D-L2：控制器只写前馈量，PD 留在硬件层）。
//   模式可以**运行期切换**（控制器起停 ⇒ 模式必然变），由 `advance_mode_fsm()` 一次切一台。
//   ⚠️ `gravity_ff` 参数只决定"**没有控制器时**预置哪一种模式"（见 parse_params），不再是整链开关。
//
// 安全设计（都是真机踩出来的，别删）：
//   1. **命令是 NaN 就发"保持"**（目标是**使能那一刻锁定**的位置）：ros2_control 里没被写过的
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
#include "motor_driver_hardware/dm_frame_plan.hpp"
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
  // 只有控制器读得到，外部节点拿不到 ERR/温度（见 MANAGE_ARCH M-5）。
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
    // MIT 的保持增益（D-L2：**留在硬件层** —— "这台电机怎么调"）。控制器只写 `effort` 前馈量，
    // PD 由硬件在自己的 100 Hz 里叠上去。
    double kp_hold = 0.0;
    double kd_hold = 0.0;
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
  };

  // ── 运行期使能（M9.2，设计见 docs/MANAGE_ARCH.md §5）────────────────────────
  // **为什么需要**：`enable_on_activate` 是硬件组件的 <param>（来自 robot_description，构建/launch 期
  //   定死），CM 的 `set_hardware_component_state` **改不了已加载的参数** ⇒ 启动为只读之后想使能，
  //   只能走插件自己提供的服务（决策 M-1）。
  // **怎么做**：服务回调只做"前提检查 + 置请求"；真正的寄存器 I/O 与使能帧由 `write()` 里的状态机
  //   **逐周期**推进（一个周期最多一次 `0x0A` 写 ≈12 ms，期间**不发控制帧**）；回调随后**阻塞等结果**
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

  // ── 运行期模式切换（D-L1 的必然结果：控制器起停 ⇒ 模式变）────────────────────
  // 每个控制器周期重新算一次**期望模式**（`pick_mode(effort)`）；与**已提交**的模式不同就切，
  // 一次切一台（一次 `0x0A` 写 ≈12 ms）。
  // ⚠️ 切换**必须补一帧原模式的保持帧**：`0x0A` 是运行期写，若不补，那一刻"在飞的目标"会被
  //    电机侧清零（陷阱 #2 同源）⇒ 先从"保持当前位置"起步，两种模式的参考点都是当前实测位置，
  //    切过去时 PD 误差 ≈ 0 ⇒ **无跳变**。
  // ⚠️ **没有 `enable_req_` 时也允许切**（激活时就是靠它写 `0x0A`），这与运行期使能状态机不冲突：
  //    它只在 `enable_req_ != 0` 时跑，且那时 `advance_mode_fsm()` 不会被执行。
  void advance_mode_fsm();
  // 写入期望模式：一次一台（一次寄存器往返）= 运行期路径。
  void write_mode_one(std::size_t i, ControlMode mode);
  // 逐台写一遍（on_activate 用）。**不是 RT 路径**：那里本来就在阻塞地等电机上线，
  // 一次写完整臂是原行为（6 台 × ~12 ms）。
  void write_mode_all();

  // ── 使能序列的两块积木（on_activate 与运行期状态机**共用同一份**，别写两遍）──
  void enable_all_and_latch();              // 全部使能 + 锁保持目标
  // 记结果：先写原因（加锁）再置原子量 ⇒ 读到 result≠0 时原因一定已就绪。
  void set_enable_result(int v, const std::string & why);

  // ── 当前控制模式（`committed_mode_` 是"真的写进 0x0A 的那个"；空 = 本轮还没提交过）──
  // 单独一个数组：模式是**每条关节各自**的事（一台不同不影响别的 5 台）。
  std::vector<ControlMode> committed_mode_;
  std::vector<ControlMode> pending_mode_;
  std::vector<char> mode_done_;
  std::size_t mode_step_ = 0;

  // ── 模型坐标 ⇄ 我们的关节坐标（换算公式的真源在 dm_joint.hpp，这里只套每关节的 sign/δ）──
  double model_to_ours(std::size_t i, double q_urdf) const;
  double ours_to_model(std::size_t i, double q_ours) const;

  // ── 两条装配路径（`write()` 按模式分派；只负责装配帧，不做决策、不算控制律）──
  hardware_interface::return_type send_pos_vel_frames();
  hardware_interface::return_type send_mit_frames();
  // 所有关节补一帧**各自当前模式**的保持帧。返回是否**每台都发成功**
  // （模式切换前用它可以 best effort，使能后用它的返回值判成败）。
  bool send_hold_frames_all();

  // ── 参数解析 ──
  bool parse_params(const hardware_interface::HardwareInfo & info);   // 失败返回 false（已打日志）
  bool parse_one_joint(const hardware_interface::ComponentInfo & j);  // 同上，逐关节
  void log_params() const;

  // ── 骨架 ──
  void build_hardware();                                             // on_configure：串口/总线/关节对象
  void teardown();                                                   // on_cleanup / 出错收尾
  void disable_all_quietly();                                        // 失能，绝不抛

  // ── ROS 侧接口（on_configure 里建；**没有节点时不建** = 单测）──
  rclcpp::Service<arm_msgs::srv::SetEnabled>::SharedPtr set_enabled_srv_;
  rclcpp::Service<arm_msgs::srv::HardwareEmergencyStop>::SharedPtr estop_srv_;
  rclcpp::Publisher<arm_msgs::msg::HardwareStatus>::SharedPtr status_pub_;
  int status_tick_ = 0;                    // 每 kStatusEvery 圈发一次（10 Hz @ 100 Hz）

  std::vector<JointParams> joints_;
  std::vector<JointRuntime> rt_;
  std::vector<double> hw_positions_;
  std::vector<double> hw_velocities_;
  // effort（模型坐标 N·m）：来自电机反馈的**电流估计**，不是力矩传感器。只当量级参考。
  std::vector<double> hw_efforts_;
  std::vector<double> hw_commands_;
  // velocity 命令接口（模型坐标 rad/s）：MIT 下当 `dq_des` 用，POS_VEL 路径不看它。
  // 两条链都导出它 —— ros2_controllers.yaml 是 mock/真机共用的，少一个接口 JTC 会配置失败。
  std::vector<double> hw_vel_commands_;
  // effort 命令接口（模型坐标 N·m）—— **本层选模式的唯一依据**（D-L1）：
  //   NaN = 没有控制器声索力矩 ⇒ 发 POS_VEL；有限 = 有控制器在写 ⇒ 发 MIT 且把它当 `tau_ff`。
  //   默认 NaN；`gravity_ff=true` 时预置 0.0（使能即进 MIT，且前馈为 0 —— 控制律不在本层）。
  std::vector<double> hw_effort_commands_;

  SerialIo * injected_io_ = nullptr;                  // 测试注入（不拥有）
  std::unique_ptr<SerialPort> owned_io_;              // 生产自持
  SerialIo * io_ = nullptr;
  std::unique_ptr<MotorBus> bus_;

  std::string device_{"/dev/ttyACM0"};
  int baud_ = 921600;
  bool enable_on_activate_ = false;                   // 默认**只读**
  double vlim_ = 1.0;                                 // POS_VEL 速度上限（rad/s）
  int fail_streak_limit_ = 10;                        // 连续多少圈收不到反馈就报错
  // ⚠️ 这只是"没有控制器时**预置**哪一种模式"（false⇒POS_VEL / true⇒MIT+`effort`=0），
  //    **不是**整链开关：真正的模式由 `effort` 接口每周期决定（D-L1）。控制律不在这条路径上。
  bool gravity_ff_ = false;
};

}  // namespace motor_driver_hardware

#endif  // MOTOR_DRIVER_HARDWARE__DM_SYSTEM_INTERFACE_HPP_
