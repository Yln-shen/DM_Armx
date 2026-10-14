// dm_system_interface.hpp —— ros2_control 的 `SystemInterface` 插件（把 C++ 核接到真机）。
//
// **这一层的职责**（LAYERS §2）：串口与帧 I/O、把上层给的目标**装配成 CAN 帧**、使能/失能（电气动作）、
// 反馈 → 状态接口、**物理**边界（PMAX）、喂 500 ms 看门狗、ERR 上报。
// **不做**：控制律（τ_g / 摩擦 / 积分 / 守卫策略）、轨迹、模型软限位、业务流程 —— 那些属于控制层/运动层。
//
// **只有一种控制模式：POS_VEL（`0x0A = 2`）**（2026-10-14 决定，砍掉整个 MIT 路径）：
//   - 位置给固件闭环（固件自带积分，静态精度最好）；
//   - MIT 曾经用来做重力前馈，但：① 折叠位的重力由**机械结构**承载（实测：失能 5 s 零漂移、
//     torque 全在 ±0.02 N·m 量化噪声内）⇒ 那条前馈在折叠位是空转；② 本链不需要关节力矩。
//   - ⚠️ `dm_frames::mit_frame` 与 Python 侧 `joint.py` 的 MIT **保留不动**（协议只有一个真源，
//     两份实现要继续逐字节咬住），砍掉的只是 **C++ 的控制路径**。
//
// `effort` 命令接口**保留为空插槽**：本层永远不读它（现在没有力矩路径），这样将来若要加力矩
// 控制器，不必改 URDF/xacro。导出的 effort **状态**接口仍报电机反馈里的**电流估计**。
//
// 安全设计（都是真机踩出来的，别删）：
//   1. **命令是 NaN 就发"保持"**（目标是**使能那一刻锁定**的电机侧位置）：ros2_control 里没被写过的
//      命令接口就是 NaN，当 0 处理 = 一使能就朝零位冲。锁定而不是"每圈重读实测值"，是因为后者没有
//      回复力 —— 重力能把关节慢慢压走（陷阱 #38）；
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
#include "motor_driver_hardware/dm_joint.hpp"
#include "motor_driver_hardware/dm_serial.hpp"

namespace motor_driver_hardware
{

// CTRL_MODE 寄存器（`0x0A`，RAM）的唯一取值。要改回 MIT 就得在这里加一个枚举值，
// 并把 `dm_joint` 的 `set_mit` 与 `dm_gravity` 一起恢复 —— 不是只改这一个数。
constexpr uint32_t kCtrlModePosVel = 2;   // 1 = MIT / 2 = 位置速度 / 3 = 速度 / 4 = 力位混控

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
  };

  // ── 运行期状态：每周期都会变的东西（与"静态参数"分开，便于一眼看出谁是状态）──
  struct JointRuntime
  {
    int missing_streak = 0;                           // 连续多少圈没反馈
    uint8_t err_warned = 0;                           // 只读模式下"这个故障码已提示过"
    // 使能那一刻锁定的保持目标（**电机侧**）。POS_VEL 的保持帧直接用它发，
    // 不换算、不钳位 —— 钳了就不是"保持"（陷阱 #21）。
    std::optional<double> hold_pos;
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

  // ── 使能序列的两块积木（on_activate 与运行期状态机**共用同一份**，别写两遍）──
  void write_mode_one(std::size_t i);       // ① 单台写 0x0A=2（+ 可选写 PID）
  void enable_all_and_latch();              // ② 全部使能 + 锁保持目标 + 补保持帧
  // 记结果：先写原因（加锁）再置原子量 ⇒ 读到 result≠0 时原因一定已就绪。
  void set_enable_result(int v, const std::string & why);

  // ── 模型坐标 ⇄ 我们的关节坐标（换算公式的真源在 dm_joint.hpp，这里只套每关节的 sign/δ）──
  double model_to_ours(std::size_t i, double q_urdf) const;
  double ours_to_model(std::size_t i, double q_ours) const;

  // ── POS_VEL 装配路径 ──
  hardware_interface::return_type write_pos_vel();

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
  // **空插槽**：`effort` 命令接口。本层永远不读它（没有力矩路径），导出只是为了让将来加力矩
  // 控制器时不必改 URDF/xacro。永远是 NaN。
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
};

}  // namespace motor_driver_hardware

#endif  // MOTOR_DRIVER_HARDWARE__DM_SYSTEM_INTERFACE_HPP_
