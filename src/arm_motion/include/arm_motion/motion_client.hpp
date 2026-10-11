// motion_client.hpp —— 给 JointTrajectoryController 发**单点轨迹**的非阻塞客户端。
//
// 2026-10-07 从 `arm_application/src/safe_park_node.cpp` 抽出（M9.0），**行为逐条等价**：
// 不拥有线程、不 sleep、不碰协议 / 串口 / 寄存器；由调用方在定时器里 `tick()`。
// 将来 `arm_motion` 的 `ExecuteMotion` action server 会用同一个件做"执行"那一阶段。
//
// 为什么它**自己不打任何日志**：这套状态机要被 `safe_park` / `arm_manage` / `arm_motion`
// 三处用，而日志措辞是**业务**的（"折叠位"这类词只属于调用方）。所以这里只产出**事件队列**
// （带严重级），由调用方决定前缀、决定打哪一条 —— 调用方因此能**逐字复现**迁移前的日志。
//
// 护栏（与迁移前同一份实现）：
//   · 关节名必须与 `arm_limits::kJointNames` **逐个一致**（顺序也一致）；
//   · 目标是**模型坐标**，逐关节比 `arm_limits::kModelLimits`（JTC **不校验限位**，
//     越界目标它照执行 ⇒ 臂顶在机械硬限位上堵转，见陷阱 #59）。
#ifndef ARM_MOTION__MOTION_CLIENT_HPP_
#define ARM_MOTION__MOTION_CLIENT_HPP_

#include <arm_description/joint_limits.hpp>

#include <rclcpp/rclcpp.hpp>
#include <rclcpp_action/rclcpp_action.hpp>

#include <action_msgs/srv/cancel_goal.hpp>
#include <control_msgs/action/follow_joint_trajectory.hpp>
#include <sensor_msgs/msg/joint_state.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace arm_motion
{

// 一条给调用方打印的事件（**不含前缀**，前缀由调用方加）。
struct MotionEvent
{
  enum Level { kInfo, kWarn, kError };
  Level level;
  std::string text;
};

class MotionClient
{
public:
  using FollowJointTrajectory = control_msgs::action::FollowJointTrajectory;
  using GoalHandle = rclcpp_action::ClientGoalHandle<FollowJointTrajectory>;

  // `node` 只用来建订阅 / action client（`rclcpp_lifecycle::LifecycleNode` 也能传，它派生自 Node）。
  explicit MotionClient(
    rclcpp::Node * node,
    const std::vector<std::string> & joint_names,
    const std::string & action_name = "/arm_controller/follow_joint_trajectory",
    const std::string & state_topic = "joint_states");

  // ───────────────────────── 配置（`start()` 之前设）─────────────────────────
  void set_vlim(double rad_s);              // 速度幅值：duration = max(1.0, travel/vlim)
  void set_tolerance(double tol_rad);       // 残差判据
  void set_timeout(double sec);             // 执行阶段超时（到点取消）
  void set_state_wait(double sec);          // 等第一帧 /joint_states 的超时
  void set_action_wait(double sec);         // 等 action server 的超时（默认 10）
  void set_goal_reply_wait(double sec);     // 等 goal 被接受的超时（默认 5）
  void set_verify_delay(double sec);        // 到位后等状态刷新的时间（默认 0.3）
  void set_cancel_wait(double sec);         // 等取消确认的超时（默认 1.0）

  // ───────────────────────── 护栏 ─────────────────────────
  // 关节名与 `arm_limits::kJointNames` 逐个一致（顺序也一致）；不一致时填 `why`（已解析的绝对判据）。
  bool validate_joint_names(std::string * why = nullptr) const;
  // 目标（**模型坐标** rad）长度 / 有限性 / 逐关节软限位。越界填 `why`（含哪一关节、数值、允许区间）。
  // `label` 只影响报错文案里的名字（调用方传自己的参数名，例如 "park_pose"），便于日志逐字复现。
  bool validate_target(
    const std::vector<double> & target, std::string * why = nullptr,
    const std::string & label = "目标") const;

  // ───────────────────────── 一次运动 ─────────────────────────
  // 发起"从当前位置到 `target` 的单点轨迹"。返回 false ⇒ **当场拒绝、一帧没发**（`why` 说明原因）。
  bool start(const std::vector<double> & target, std::string * why = nullptr);
  // 每 tick 推进一步。返回 true = 收工（成功与否看 `ok()`）。**不许阻塞**（future 只做 wait_for(0)）。
  bool tick();
  bool ok() const {return ok_;}
  const char * phase_name() const;
  bool running() const;

  // ───────────────────────── 诊断（给调用方打日志用）─────────────────────────
  const std::vector<std::string> & joint_names() const {return joint_names_;}
  const std::vector<double> & target() const {return target_;}
  // 发起时读到的实测位置；还没读到完整状态时为空
  const std::vector<double> & start_positions() const {return start_positions_;}
  // 验证时读到的实测位置（只有走到 VERIFY 才有）
  const std::vector<double> & final_positions() const {return final_positions_;}
  double travel() const {return travel_;}                 // 最大单关节行程
  double planned_duration() const {return duration_;}     // max(1.0, travel/vlim)
  double max_residual() const {return max_residual_;}
  int32_t goal_error_code() const {return goal_error_code_;}
  const std::string & goal_error_string() const {return goal_error_string_;}
  bool cancel_confirmed() const {return cancel_confirmed_;}

  // ───────────────────────── 事件队列 ─────────────────────────
  // 累积的事件；**由调用方决定何时清空**（迁移前的 safe_park 是即时打印，调用方 tick 后即可清）。
  const std::vector<MotionEvent> & events() const {return events_;}
  void clear_events() {events_.clear();}

private:
  enum class Phase { WaitState, WaitServer, WaitGoalReply, WaitResult, Verify, Cancelling, Done };

  void push(MotionEvent::Level level, const std::string & text);
  bool have_full_state() const;
  std::vector<double> current_positions() const;      // 按 joint_names_ 顺序；未知的留 NaN
  bool send_goal();
  void cancel_goal(const std::string & why);
  // ⚠️ 每个 tick_* 返回的是"**整个运动**是否收工"（与迁移前 safe_park 的语义一致），
  //    不是"阶段是否推进" —— 搞混会让第一圈就误判完成。
  bool tick_wait_state();
  bool tick_wait_server();
  bool tick_wait_goal_reply();
  bool tick_wait_result();
  bool tick_verify();
  bool tick_cancelling();

  rclcpp::Node * node_;
  std::vector<std::string> joint_names_;
  std::vector<double> target_;

  double vlim_ = 0.15;
  double tol_rad_ = 0.05;
  double timeout_sec_ = 60.0;
  double state_wait_sec_ = 10.0;
  double action_wait_sec_ = 10.0;
  double goal_reply_wait_sec_ = 5.0;
  double verify_delay_sec_ = 0.3;
  double cancel_wait_sec_ = 1.0;

  bool started_ = false;
  bool ok_ = false;
  Phase phase_ = Phase::WaitState;
  rclcpp::Time started_at_;
  rclcpp::Time phase_since_;

  std::vector<double> start_positions_;
  std::vector<double> final_positions_;
  double travel_ = 0.0;
  double duration_ = 0.0;
  double max_residual_ = 0.0;
  int32_t goal_error_code_ = 0;
  std::string goal_error_string_;
  bool cancel_confirmed_ = false;

  sensor_msgs::msg::JointState::SharedPtr last_state_;
  rclcpp::Subscription<sensor_msgs::msg::JointState>::SharedPtr state_sub_;
  rclcpp_action::Client<FollowJointTrajectory>::SharedPtr action_client_;
  std::shared_future<GoalHandle::SharedPtr> goal_future_;
  GoalHandle::SharedPtr goal_handle_;
  std::shared_future<GoalHandle::WrappedResult> result_future_;
  std::shared_future<action_msgs::srv::CancelGoal::Response::SharedPtr> cancel_future_;
  bool cancel_sent_ = false;

  std::vector<MotionEvent> events_;
};

}  // namespace arm_motion

#endif  // ARM_MOTION__MOTION_CLIENT_HPP_
