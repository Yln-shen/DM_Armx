// motion_server.hpp —— 运动接口层的 action server（`ExecuteMotion`）。
//
// **职责**（LAYERS §2、MOTION_ARCH §3）：把"去哪"编排成"规划 → 校验 → 执行 → 验证"，
// 是**唯一**的 JTC 目标发布者（D-M2）。不做控制律、不碰串口/使能。
//
// 三条硬约束（都是踩过的）：
//   ① **非阻塞**：单线程 executor + 定时器 tick。规划/执行全是 `wait_for(0)` 轮询 ——
//      绝不在回调里等 future（陷阱 #57：`safe_park` 第一版在回调里 sleep ⇒ 订阅回调永远进不来）。
//   ② **取消必须真取消**（陷阱 #60）：真机踩过 `timeout_sec=0.3` 时节点退出后臂仍走完
//      0.5566 rad，而日志写着"臂停在当前位置"。所以每条放弃路径都要 `async_cancel_goal`
//      并如实报"取消已确认 / 未确认"。
//   ③ **逐点查限位**：JTC 不校验限位（陷阱 #59）。见 `trajectory_utils.hpp`。
//
// 本轮范围：`TARGET_JOINTS` + `TARGET_NAMED`。⚠️ `TARGET_POSE` **未实现**（error 1）——
// 它的消费者（视觉/抓放）还没影，先做等于没有真机需求可验收。
#ifndef ARM_MOTION__MOTION_SERVER_HPP_
#define ARM_MOTION__MOTION_SERVER_HPP_

#include <arm_msgs/action/execute_motion.hpp>

#include <rclcpp/rclcpp.hpp>
#include <rclcpp_action/rclcpp_action.hpp>

#include <control_msgs/action/follow_joint_trajectory.hpp>
#include <moveit_msgs/action/move_group.hpp>
#include <sensor_msgs/msg/joint_state.hpp>

#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "arm_motion/named_targets.hpp"
#include "arm_motion/trajectory_utils.hpp"

namespace arm_motion
{

class MotionServer
{
public:
  using ExecuteMotion = arm_msgs::action::ExecuteMotion;
  using GoalHandle = rclcpp_action::ServerGoalHandle<ExecuteMotion>;
  using MoveGroup = moveit_msgs::action::MoveGroup;
  using MoveGroupGoalHandle = rclcpp_action::ClientGoalHandle<MoveGroup>;
  using FollowJointTrajectory = control_msgs::action::FollowJointTrajectory;
  using JtcGoalHandle = rclcpp_action::ClientGoalHandle<FollowJointTrajectory>;

  // 错误码：与 `MOTION_ARCH.md` §4.3 的错误码表一一对应（**不要**改数值，它是对外契约）
  enum ErrorCode : uint16_t
  {
    OK = 0,
    INVALID_GOAL = 1,
    JOINT_LIMIT = 2,
    IK_FAILED = 3,
    PLANNING_FAILED = 4,
    CARTESIAN_INCOMPLETE = 5,
    EXEC_TIMEOUT = 6,
    PREEMPTED = 7,
    RESIDUAL_TOO_LARGE = 8,
    NAMED_TARGET_MISSING = 9,
    BUSY = 10,
    HARDWARE_ERROR = 11,
    START_STATE_INVALID = 12,
    CANCELLED = 13,
  };

  MotionServer(rclcpp::Node * node, NamedTargets targets, const std::string & planning_group);

  // 可配项（`start()` 之前设；默认值来自 motion_params.yaml 的调用方）
  void set_scalings(double vel, double acc);
  void set_timeouts(double planning_sec, double exec_sec, double state_wait_sec, double settle_sec);
  void set_tolerances(double max_residual_rad, double cancel_confirm_sec);
  void set_topic_names(const std::string & jtc_action, const std::string & joint_states_topic);
  void set_move_group_names(const std::string & action, const std::string & cartesian_srv);

  // 建 action server / 客户端 / 订阅。返回后即可被外部 tick 驱动。
  void start();

private:
  using Clock = std::chrono::steady_clock;

  enum class Phase
  {
    Idle,
    Validating,
    Planning,
    Executing,
    Verifying,
    Cancelling,
  };

  // ── 一个目标的全过程状态（一次只有一个 active）──
  struct Job
  {
    std::shared_ptr<GoalHandle> handle;
    ExecuteMotion::Goal goal;
    Trajectory planned;              // 规划出来的轨迹（已过限位校验）
    std::vector<std::string> jtc_joint_names;
    Clock::time_point t0;            // 收到 goal 的时刻（算 elapsed_sec）
    Clock::time_point phase_since;   // 进入当前阶段的时刻
    Clock::time_point exec_started;  // 进入 EXECUTING 的时刻（执行超时从这儿算）
    double exec_timeout_sec = 0.0;   // 本目标的执行超时（Goal 给了就用 Goal 的）
    bool cancel_requested = false;
    bool cancelled_confirmed = false;
    uint16_t cancel_error = CANCELLED;
  };

  // ── action server 回调（**只做常量时间的事**）──
  rclcpp_action::GoalResponse on_goal(const rclcpp_action::GoalUUID & uuid,
    std::shared_ptr<const ExecuteMotion::Goal> goal);
  rclcpp_action::CancelResponse on_cancel(const std::shared_ptr<GoalHandle> handle);
  void on_accepted(const std::shared_ptr<GoalHandle> handle);

  // ── 每 tick 推进一步 ──
  void tick();

  // 阶段实现。每个都**立刻返回**（future 只做 wait_for(0)）。
  void advance_validating();
  void advance_planning();
  void advance_executing();
  void advance_verifying();
  void advance_cancelling();

  // ── 收尾 ──
  void finish(uint16_t code, const std::string & message);
  void fail(uint16_t code, const std::string & message);       // finish + 结果
  void publish_feedback(const std::string & phase, double progress);

  // ── 校验（VALIDATING 用）──
  bool resolve_goal(ExecuteMotion::Goal * goal, std::string * why, uint16_t * code);
  bool check_start_state(std::string * why);

  // ── 规划（PLANNING 用）──
  void send_move_group_goal();
  void on_move_group_result(const MoveGroupGoalHandle::WrappedResult & result);
  // 把 moveit 的轨迹转成本层朴素结构 + 逐点校验（失败 ⇒ 填 why/code）
  bool adopt_planned_trajectory(
    const trajectory_msgs::msg::JointTrajectory & msg, std::string * why, uint16_t * code);

  // ── 执行（EXECUTING 用）──
  void send_jtc_goal();
  void cancel_jtc_goal(const std::string & why);

  // ── 取消（任何阶段被请求时）──
  void begin_cancel(uint16_t error_code);

  // ── 状态 ──
  rclcpp::Node * node_;
  NamedTargets targets_;
  std::string planning_group_;

  double vel_scaling_ = 0.1;
  double acc_scaling_ = 0.1;
  double planning_timeout_sec_ = 5.0;
  double exec_timeout_sec_ = 30.0;
  double state_wait_sec_ = 2.0;
  double settle_sec_ = 0.3;
  double max_residual_rad_ = 0.05;
  double cancel_confirm_sec_ = 1.0;

  std::string jtc_action_ = "/arm_controller/follow_joint_trajectory";
  std::string joint_states_topic_ = "/joint_states";
  std::string move_group_action_ = "/move_action";
  std::string cartesian_srv_ = "/compute_cartesian_path";

  // 单线程 executor ⇒ 这些只需要"跨回调不重入"的保护，不需要真并发锁；
  // 但 action 回调与定时器回调都在同一线程，仍用 mutex 是为了**将来换 executor 时不静默出错**。
  std::mutex mu_;
  Phase phase_ = Phase::Idle;
  std::optional<Job> job_;
  bool cancel_pending_ = false;    // 收到 cancel 但还没进 CANCELLING

  sensor_msgs::msg::JointState::SharedPtr last_state_;
  rclcpp::Subscription<sensor_msgs::msg::JointState>::SharedPtr state_sub_;

  rclcpp_action::Server<ExecuteMotion>::SharedPtr server_;
  rclcpp_action::Client<MoveGroup>::SharedPtr move_group_client_;
  rclcpp_action::Client<FollowJointTrajectory>::SharedPtr jtc_client_;

  // ⚠️ rclcpp_action 的**同步**取结果路径是私有的（`ClientGoalHandle::async_get_result()` 只对
  //    `Client` 开放）⇒ 正确用法是 `SendGoalOptions` 的 `result_callback`：结果**推**过来，
  //    我们只置标志，`tick()` 非阻塞地看标志。**不要**试图去轮询一个 result future。
  //    这些标志会被 executor 回调写、被 tick 读 ⇒ 用 `res_mu_` 保护（见下方注释）。
  std::mutex res_mu_;
  MoveGroupGoalHandle::SharedPtr mg_goal_handle_;
  bool mg_done_ = false;
  bool mg_ok_ = false;
  // ⚠️ rclcpp_action 给的是 `Result::SharedPtr`（不是值）—— 别想当然按值存
  MoveGroup::Result::SharedPtr mg_result_;
  bool mg_cancel_sent_ = false;

  JtcGoalHandle::SharedPtr jtc_goal_handle_;
  bool jtc_done_ = false;
  bool jtc_ok_ = false;
  FollowJointTrajectory::Result::SharedPtr jtc_result_;
  bool jtc_cancel_sent_ = false;
  Clock::time_point jtc_cancel_sent_at_{};

  rclcpp::TimerBase::SharedPtr tick_timer_;
};

}  // namespace arm_motion

#endif  // ARM_MOTION__MOTION_SERVER_HPP_
