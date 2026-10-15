// motion_server.cpp —— `ExecuteMotion` action server 的实现。
//
// 本轮范围：`TARGET_JOINTS` + `TARGET_NAMED`（`TARGET_POSE` 未实现，见头文件）。
//
// ⚠️ 三条 ros2 action API 教训（都是写这个文件时踩的，别再犯）：
//   ① `ClientGoalHandle::async_get_result()` 是**私有**的（只对 `Client` 开放）
//      ⇒ 取结果只能靠 `SendGoalOptions::result_callback`（结果**推**过来），
//        然后在本层置标志、由 `tick()` 非阻塞地看标志。**没有**可轮询的 result future。
//   ② `async_send_goal()` 返回的是"解出 goal handle"的 future（goal 被拒时解出 nullptr），
//      **不是**结果 future。
//   ③ `WrappedResult::result` 是 `Result::SharedPtr`（不是值）。
#include "arm_motion/motion_server.hpp"

#include <moveit_msgs/msg/motion_plan_request.hpp>
#include <moveit_msgs/msg/motion_plan_response.hpp>
#include <moveit_msgs/msg/planning_options.hpp>

#include <algorithm>
#include <cmath>
#include <sstream>

namespace arm_motion
{

namespace
{

Sample to_sample(const trajectory_msgs::msg::JointTrajectoryPoint & p)
{
  Sample s;
  s.positions = p.positions;
  s.time_from_start = static_cast<double>(p.time_from_start.sec) +
    static_cast<double>(p.time_from_start.nanosec) * 1e-9;
  return s;
}

std::string fmt(double v)
{
  std::ostringstream ss;
  ss.precision(6);
  ss << v;
  return ss.str();
}

}  // namespace

// ══════════════════════════ 构造与配置 ══════════════════════════

MotionServer::MotionServer(
  rclcpp::Node * node, NamedTargets targets, const std::string & planning_group)
: node_(node), targets_(std::move(targets)), planning_group_(planning_group)
{
}

void MotionServer::set_scalings(double vel, double acc)
{
  vel_scaling_ = vel;
  acc_scaling_ = acc;
}

void MotionServer::set_timeouts(double planning_sec, double exec_sec, double state_wait_sec,
  double settle_sec)
{
  planning_timeout_sec_ = planning_sec;
  exec_timeout_sec_ = exec_sec;
  state_wait_sec_ = state_wait_sec;
  settle_sec_ = settle_sec;
}

void MotionServer::set_tolerances(double max_residual_rad, double cancel_confirm_sec)
{
  max_residual_rad_ = max_residual_rad;
  cancel_confirm_sec_ = cancel_confirm_sec;
}

void MotionServer::set_topic_names(const std::string & jtc_action, const std::string & state_topic)
{
  jtc_action_ = jtc_action;
  joint_states_topic_ = state_topic;
}

void MotionServer::set_move_group_names(
  const std::string & action, const std::string & cartesian_srv)
{
  move_group_action_ = action;
  cartesian_srv_ = cartesian_srv;
}

// ══════════════════════════ 启动 ══════════════════════════

void MotionServer::start()
{
  // `/joint_states`：QoS 必须 RELIABLE + TRANSIENT_LOCAL，否则**一帧都收不到且不报错**（陷阱 #58）
  state_sub_ = node_->create_subscription<sensor_msgs::msg::JointState>(
    joint_states_topic_, rclcpp::QoS(1).reliable().transient_local(),
    [this](sensor_msgs::msg::JointState::SharedPtr msg) {last_state_ = msg;});

  server_ = rclcpp_action::create_server<ExecuteMotion>(
    node_, "execute_motion",
    [this](const rclcpp_action::GoalUUID & uuid, std::shared_ptr<const ExecuteMotion::Goal> goal) {
      return on_goal(uuid, goal);
    },
    [this](const std::shared_ptr<GoalHandle> handle) {return on_cancel(handle);},
    [this](const std::shared_ptr<GoalHandle> handle) {on_accepted(handle);});

  // ⚠️ **不用** `MoveGroupInterface`：它的 plan()/move() 都是阻塞调用，放进回调就重演陷阱 #57。
  //    用原生 action ⇒ 依赖里也不出现 moveit_ros_planning_interface（D-M3）。
  move_group_client_ = rclcpp_action::create_client<MoveGroup>(node_, move_group_action_);
  jtc_client_ = rclcpp_action::create_client<FollowJointTrajectory>(node_, jtc_action_);

  tick_timer_ = node_->create_wall_timer(std::chrono::milliseconds(50), [this]() {tick();});
}

// ══════════════════════════ action server 回调 ══════════════════════════
// ⚠️ 这三个回调**只做常量时间的事**（判断 + 入队）；重活全在 `tick()` 里（MOTION_ARCH §3.3）。

rclcpp_action::GoalResponse MotionServer::on_goal(
  const rclcpp_action::GoalUUID &, std::shared_ptr<const ExecuteMotion::Goal> goal)
{
  std::lock_guard<std::mutex> lk(mu_);
  // 忙 + 没带 preempt ⇒ 直接拒。带 preempt ⇒ 接受，由状态机去取消前一个目标。
  if (phase_ != Phase::Idle && !goal->preempt) {
    return rclcpp_action::GoalResponse::REJECT;
  }
  return rclcpp_action::GoalResponse::ACCEPT_AND_EXECUTE;
}

rclcpp_action::CancelResponse MotionServer::on_cancel(const std::shared_ptr<GoalHandle>)
{
  std::lock_guard<std::mutex> lk(mu_);
  // 立刻接受取消请求；真正的取消动作交给状态机下一 tick（**不许在这里阻塞**）
  cancel_pending_ = true;
  return rclcpp_action::CancelResponse::ACCEPT;
}

void MotionServer::on_accepted(const std::shared_ptr<GoalHandle> handle)
{
  std::lock_guard<std::mutex> lk(mu_);

  // REJECT 不带错误码 ⇒ 业务层只看到"goal 被拒"。这条是兜底：给一个**明确的 error 10**
  if (phase_ != Phase::Idle && !handle->get_goal()->preempt) {
    auto result = std::make_shared<ExecuteMotion::Result>();
    result->success = false;
    result->error_code = BUSY;
    result->message = "已有目标在执行（未带 preempt）";
    result->elapsed_sec = 0.0;
    result->max_residual_rad = std::nan("");
    handle->abort(result);
    return;
  }

  // 带 preempt 且当前有目标 ⇒ 先把前一个取消掉（它拿到 error 7 PREEMPTED）
  if (phase_ != Phase::Idle) {begin_cancel(PREEMPTED);}

  Job j;
  j.handle = handle;
  j.goal = *handle->get_goal();
  j.t0 = Clock::now();
  j.phase_since = j.t0;
  j.exec_timeout_sec = j.goal.timeout_sec > 0.0 ? j.goal.timeout_sec : exec_timeout_sec_;
  job_ = std::move(j);
  phase_ = Phase::Validating;
  cancel_pending_ = false;
}

// ══════════════════════════ 状态机 ══════════════════════════

void MotionServer::tick()
{
  std::lock_guard<std::mutex> lk(mu_);
  if (phase_ == Phase::Idle || !job_.has_value()) {return;}

  // 取消优先：任何阶段被请求取消 ⇒ 进入 CANCELLING（急停要能立刻生效）
  if (cancel_pending_ && phase_ != Phase::Cancelling) {
    cancel_pending_ = false;
    begin_cancel(CANCELLED);
  }

  switch (phase_) {
    case Phase::Validating: advance_validating(); break;
    case Phase::Planning: advance_planning(); break;
    case Phase::Executing: advance_executing(); break;
    case Phase::Verifying: advance_verifying(); break;
    case Phase::Cancelling: advance_cancelling(); break;
    case Phase::Idle: break;
  }
}

// ── VALIDATING：字段 / 类型 / 目标解析 / 起始状态 ──
void MotionServer::advance_validating()
{
  std::string why;
  uint16_t code = INVALID_GOAL;
  if (!resolve_goal(&job_->goal, &why, &code)) {
    fail(code, why);
    return;
  }
  publish_feedback("VALIDATING", 0.0);
  phase_ = Phase::Planning;
  job_->phase_since = Clock::now();
  send_move_group_goal();
}

// ── PLANNING：等 `result_callback` 置标志（**非阻塞**）──
void MotionServer::advance_planning()
{
  {
    std::unique_lock<std::mutex> lk(res_mu_, std::try_to_lock);
    if (lk.owns_lock() && mg_done_) {
      mg_done_ = false;                    // 只消费一次
      if (!mg_ok_) {
        fail(PLANNING_FAILED, "规划没有正常返回（goal 被拒，或 result code 不是 SUCCEEDED）");
        return;
      }
      if (!mg_result_) {
        fail(PLANNING_FAILED, "规划返回了空结果");
        return;
      }
      if (mg_result_->error_code.val != moveit_msgs::msg::MoveItErrorCodes::SUCCESS) {
        // 自建错误码：IK 无解单独一类（§8.3）；**原始数值保留在 message 里**（排查的唯一线索）
        const int32_t v = mg_result_->error_code.val;
        const uint16_t code = (v == moveit_msgs::msg::MoveItErrorCodes::NO_IK_SOLUTION) ?
          IK_FAILED : PLANNING_FAILED;
        fail(code, "规划失败：MoveItErrorCodes=" + std::to_string(v) + "（原始值保留，便于排查）");
        return;
      }
      if (mg_result_->planned_trajectory.joint_trajectory.points.empty()) {
        fail(PLANNING_FAILED, "规划成功但轨迹为空");
        return;
      }
      // 采纳 + **逐点查限位**（护栏；JTC 自己不查，陷阱 #59）
      std::string why;
      uint16_t code2 = JOINT_LIMIT;
      if (!adopt_planned_trajectory(
          mg_result_->planned_trajectory.joint_trajectory, &why, &code2))
      {
        fail(code2, why);
        return;
      }
      publish_feedback("PLANNING", 1.0);
      phase_ = Phase::Executing;
      job_->phase_since = Clock::now();
      job_->exec_started = Clock::now();
      send_jtc_goal();
      return;
    }
  }

  // 规划超时保护（move_group 自己的 allowed_planning_time 之外再套一层）
  const double waited = std::chrono::duration<double>(Clock::now() - job_->phase_since).count();
  if (waited > planning_timeout_sec_) {
    // ⚠️ 规划超时也要**显式取消**（否则 move_group 还在算，臂可能随后动起来 —— 陷阱 #60 的同类）
    if (mg_goal_handle_) {move_group_client_->async_cancel_goal(mg_goal_handle_);}
    fail(PLANNING_FAILED, "规划超时（" + fmt(waited) + " s），已请求取消");
  }
}

// ── EXECUTING：等 JTC 结果（非阻塞）+ 进度反馈 + 执行超时 ──
void MotionServer::advance_executing()
{
  // ① 进度：按"已过时间 / 轨迹总时长"估算（MOTION_ARCH §4.2 的 progress 定义）
  if (!job_->planned.points.empty()) {
    const double total = job_->planned.points.back().time_from_start;
    const double done =
      std::chrono::duration<double>(Clock::now() - job_->exec_started).count();
    publish_feedback("EXECUTING", total > 0.0 ? std::min(1.0, done / total) : 0.0);
  }

  // ② 执行超时 ⇒ 取消 + error 6
  const double ran = std::chrono::duration<double>(Clock::now() - job_->exec_started).count();
  if (ran > job_->exec_timeout_sec) {
    begin_cancel(EXEC_TIMEOUT);
    return;
  }

  // ③ 看结果标志
  std::unique_lock<std::mutex> lk(res_mu_, std::try_to_lock);
  if (!lk.owns_lock() || !jtc_done_) {return;}
  jtc_done_ = false;                       // 只消费一次
  if (!jtc_ok_) {
    fail(HARDWARE_ERROR, "JTC 执行失败（goal 被拒，或 result code 不是 SUCCEEDED）");
    return;
  }
  if (jtc_result_ &&
    jtc_result_->error_code != control_msgs::action::FollowJointTrajectory::Result::SUCCESSFUL)
  {
    fail(HARDWARE_ERROR, "JTC 执行失败：error_code=" +
      std::to_string(jtc_result_->error_code) +
      (jtc_result_->error_string.empty() ? "" : ("、\"" + jtc_result_->error_string + "\"")));
    return;
  }
  publish_feedback("EXECUTING", 1.0);
  phase_ = Phase::Verifying;
  job_->phase_since = Clock::now();
}

// ── VERIFYING：等状态刷新再比残差 ──
void MotionServer::advance_verifying()
{
  const double since_phase =
    std::chrono::duration<double>(Clock::now() - job_->phase_since).count();
  if (since_phase < settle_sec_) {return;}

  // 残差：实测（按名字重排到真源顺序）vs 轨迹**末点**
  std::vector<double> measured;
  double residual = std::nan("");
  if (last_state_ && reorder_to(last_state_->name, last_state_->position,
      job_->planned.joint_names, &measured))
  {
    residual = max_abs_diff(measured, job_->planned.points.back().positions);
  }

  auto r = std::make_shared<ExecuteMotion::Result>();
  r->elapsed_sec = std::chrono::duration<double>(Clock::now() - job_->t0).count();
  r->max_residual_rad = residual;

  if (!std::isfinite(residual)) {
    // 拿不到状态 ⇒ 不能谎报"到位了"，但执行本身是成功的。
    // ⚠️ 选择：仍算成功，但 message 里说清"残差算不出来" —— 别把这条当"已到位"的证明。
    r->success = true;
    r->error_code = OK;
    r->message = "执行成功；⚠️ 残差无法验证（/joint_states 缺关节或没有状态）——"
      "别把这条当'已到位'的证明";
    job_->handle->succeed(r);
  } else if (residual > max_residual_rad_) {
    r->success = false;
    r->error_code = RESIDUAL_TOO_LARGE;
    r->message = "执行完成但残差 " + fmt(residual) + " rad 超过阈值 " + fmt(max_residual_rad_) +
      " rad —— ⚠️ **不失能、不挪动**，臂就在那里保持";
    job_->handle->abort(r);
  } else {
    r->success = true;
    r->error_code = OK;
    r->message = "到位（残差 " + fmt(residual) + " rad）";
    job_->handle->succeed(r);
  }
  phase_ = Phase::Idle;
  job_.reset();
}

// ── CANCELLING：取消两个下游，等确认（超时不再等）──
void MotionServer::advance_cancelling()
{
  // 规划阶段取消 move_group；执行阶段取消 JTC。都发（不知道下游走到哪一步了，幂等）
  if (mg_goal_handle_ && !mg_done_ && !mg_cancel_sent_) {
    move_group_client_->async_cancel_goal(mg_goal_handle_);
    mg_cancel_sent_ = true;
  }
  if (jtc_goal_handle_ && !jtc_done_ && !jtc_cancel_sent_) {
    jtc_client_->async_cancel_goal(jtc_goal_handle_);
    jtc_cancel_sent_ = true;
    jtc_cancel_sent_at_ = Clock::now();
  }

  // ① JTC 的取消确认（这是"臂真的停了吗"的唯一凭据，陷阱 #60）
  bool jtc_finished = false;
  {
    std::unique_lock<std::mutex> lk(res_mu_, std::try_to_lock);
    if (lk.owns_lock()) {jtc_finished = jtc_done_; jtc_done_ = false;}
  }
  if (jtc_cancel_sent_ && !jtc_finished) {
    const double cw = std::chrono::duration<double>(Clock::now() - jtc_cancel_sent_at_).count();
    if (cw <= cancel_confirm_sec_) {return;}   // 再等等
    job_->cancelled_confirmed = false;         // 超时：**如实报"未确认"**，别写"已停"
  } else {
    job_->cancelled_confirmed = true;
  }

  // ② 收尾：用对应错误码 abort / canceled
  const uint16_t code = job_->cancel_error;
  std::string msg;
  switch (code) {
    case PREEMPTED: msg = "被新的目标抢占"; break;
    case EXEC_TIMEOUT: msg = "执行超时，已请求取消"; break;
    default: msg = "被取消请求终止"; break;
  }
  msg += job_->cancelled_confirmed ?
    "（下游已确认取消）" : "（⚠️ **未收到下游取消确认** —— 臂可能还在动，别靠近）";

  auto r = std::make_shared<ExecuteMotion::Result>();
  r->success = false;
  r->error_code = code;
  r->message = msg;
  r->elapsed_sec = std::chrono::duration<double>(Clock::now() - job_->t0).count();
  r->max_residual_rad = std::nan("");
  if (job_->handle->is_canceling()) {
    job_->handle->canceled(r);
  } else {
    job_->handle->abort(r);
  }
  phase_ = Phase::Idle;
  job_.reset();
}

// ══════════════════════════ 收尾工具 ══════════════════════════

void MotionServer::fail(uint16_t code, const std::string & message)
{
  publish_feedback("FAILED", 0.0);
  auto r = std::make_shared<ExecuteMotion::Result>();
  r->success = false;
  r->error_code = code;
  r->message = message;
  r->elapsed_sec = job_.has_value() ?
    std::chrono::duration<double>(Clock::now() - job_->t0).count() : 0.0;
  r->max_residual_rad = std::nan("");
  if (job_.has_value()) {job_->handle->abort(r);}
  phase_ = Phase::Idle;
  job_.reset();
}

void MotionServer::publish_feedback(const std::string & phase, double progress)
{
  if (!job_.has_value()) {return;}
  auto fb = std::make_shared<ExecuteMotion::Feedback>();
  fb->phase = phase;
  fb->progress = progress;
  job_->handle->publish_feedback(fb);
}

void MotionServer::begin_cancel(uint16_t error_code)
{
  if (!job_.has_value()) {return;}
  job_->cancel_error = error_code;
  job_->cancel_requested = true;
  job_->cancelled_confirmed = false;
  phase_ = Phase::Cancelling;
  job_->phase_since = Clock::now();
  jtc_cancel_sent_ = false;
  mg_cancel_sent_ = false;
  publish_feedback("CANCELLING", 0.0);
}

// ══════════════════════════ 目标解析与校验 ══════════════════════════

bool MotionServer::resolve_goal(ExecuteMotion::Goal * goal, std::string * why, uint16_t * code)
{
  auto failc = [why, code](uint16_t c, const std::string & s) {
      if (why) {*why = s;} if (code) {*code = c;} return false;
    };

  // 缩放：>1 直接拒（真机安全上"放大"没有正当用途）；<=0 用服务端默认
  if (goal->max_velocity_scaling > 1.0 || goal->max_acceleration_scaling > 1.0) {
    return failc(INVALID_GOAL, "速度/加速度缩放必须在 (0,1] 内，收到 vel=" +
      fmt(goal->max_velocity_scaling) + " acc=" + fmt(goal->max_acceleration_scaling));
  }
  if (goal->max_velocity_scaling > 0.0) {vel_scaling_ = goal->max_velocity_scaling;}
  if (goal->max_acceleration_scaling > 0.0) {acc_scaling_ = goal->max_acceleration_scaling;}

  switch (goal->target_type) {
    case ExecuteMotion::Goal::TARGET_JOINTS: {
      std::string w;
      if (!trajectory_joint_names_match(goal->joint_names, &w)) {
        return failc(INVALID_GOAL, "TARGET_JOINTS 的 " + w);
      }
      // 逐关节查限位（护栏第一道；别等规划完了才发现目标本身越界）
      if (!point_within_limits(goal->joint_positions, &w)) {
        return failc(JOINT_LIMIT, "TARGET_JOINTS 越界：" + w);
      }
      break;
    }
    case ExecuteMotion::Goal::TARGET_NAMED: {
      Target t;
      if (!targets_.find(goal->target_name, &t)) {
        return failc(NAMED_TARGET_MISSING, "命名目标 `" + goal->target_name + "` 不存在");
      }
      if (t.kind == Kind::Pose) {
        return failc(INVALID_GOAL,
          "命名目标 `" + goal->target_name + "` 是 pose 类型，而 TARGET_POSE 本轮未实现");
      }
      goal->joint_names.assign(arm_limits::kJointNames.begin(), arm_limits::kJointNames.end());
      goal->joint_positions.assign(t.positions.begin(), t.positions.end());
      break;
    }
    case ExecuteMotion::Goal::TARGET_POSE:
      return failc(INVALID_GOAL,
        "TARGET_POSE 本轮未实现（消费者还没影，先不做）；用 TARGET_JOINTS 或 TARGET_NAMED");
    default:
      return failc(INVALID_GOAL, "未知 target_type=" + std::to_string(goal->target_type));
  }

  // 起始状态（陷阱 #37）：没有 /joint_states 或起点越界 ⇒ 早失败并说清是哪一关节
  std::string w;
  if (!check_start_state(&w)) {return failc(START_STATE_INVALID, w);}
  return true;
}

bool MotionServer::check_start_state(std::string * why)
{
  if (!last_state_) {
    *why = "还没有 /joint_states（等不到状态就不敢规划：起点未知 ⇒ 可能一上来就朝零位冲）";
    return false;
  }
  const std::vector<std::string> order(
    arm_limits::kJointNames.begin(), arm_limits::kJointNames.end());
  std::vector<double> ordered;
  if (!reorder_to(last_state_->name, last_state_->position, order, &ordered)) {
    *why = "/joint_states 缺关节（按名字匹配不到真源里的 6 个关节）";
    return false;
  }
  std::string w;
  if (!point_within_limits(ordered, &w)) {
    // ⚠️ 这是排查"为什么突然规划不了"的唯一线索：MoveIt 只会报 "Start state out of bounds"，
    //    不说哪个关节、差多少
    *why = "**起始状态越界**（陷阱 #37）：" + w +
      " —— MoveIt 只会报 'Start state out of bounds'，不会告诉你是哪个关节";
    return false;
  }
  return true;
}

// ══════════════════════════ 规划 ══════════════════════════

void MotionServer::send_move_group_goal()
{
  if (!move_group_client_->action_server_is_ready()) {
    fail(PLANNING_FAILED, "move_group 的 " + move_group_action_ +
      " 还没就绪（它需要常驻：每次拉起要重新解析 URDF/SRDF、加载 OMPL，启动秒级）");
    return;
  }

  MoveGroup::Goal mg;
  auto & req = mg.request;
  req.group_name = planning_group_;
  req.num_planning_attempts = 1;
  req.allowed_planning_time = planning_timeout_sec_;
  req.max_velocity_scaling_factor = vel_scaling_;
  req.max_acceleration_scaling_factor = acc_scaling_;

  // 关节目标用 JointConstraint 表达（不指定终点轨迹 —— 那是规划器的活）
  for (std::size_t i = 0; i < job_->goal.joint_names.size(); ++i) {
    moveit_msgs::msg::JointConstraint jc;
    jc.joint_name = job_->goal.joint_names[i];
    jc.position = job_->goal.joint_positions[i];
    jc.tolerance_above = 1e-4;
    jc.tolerance_below = 1e-4;
    jc.weight = 1.0;
    req.goal_constraints[0].joint_constraints.push_back(jc);
  }
  mg.planning_options.plan_only = true;   // D-M2：move_group **只规划不执行**

  {
    std::lock_guard<std::mutex> lk(res_mu_);
    mg_done_ = false;
    mg_ok_ = false;
    mg_result_.reset();
    mg_goal_handle_.reset();
  }
  mg_cancel_sent_ = false;

  // ⚠️ 结果只能从 `result_callback` 收（见文件头 API 教训 ①）
  rclcpp_action::Client<MoveGroup>::SendGoalOptions opts;
  opts.goal_response_callback = [this](MoveGroupGoalHandle::SharedPtr handle) {
      std::lock_guard<std::mutex> lk(res_mu_);
      mg_goal_handle_ = handle;
      if (!handle) {mg_done_ = true; mg_ok_ = false;}   // goal 被拒 ⇒ 也算"有结果了"
    };
  opts.result_callback = [this](const MoveGroupGoalHandle::WrappedResult & wrapped) {
      std::lock_guard<std::mutex> lk(res_mu_);
      mg_result_ = wrapped.result;
      mg_ok_ = (wrapped.code == rclcpp_action::ResultCode::SUCCEEDED);
      mg_done_ = true;
    };
  move_group_client_->async_send_goal(mg, opts);
}

bool MotionServer::adopt_planned_trajectory(
  const trajectory_msgs::msg::JointTrajectory & msg, std::string * why, uint16_t * code)
{
  Trajectory t;
  t.joint_names = msg.joint_names;
  t.points.reserve(msg.points.size());
  for (const auto & p : msg.points) {t.points.push_back(to_sample(p));}

  const auto check = check_trajectory(t);
  if (!check.ok) {
    // 规划出来的轨迹越界 ⇒ 这是**护栏抓到了**（目标本身的越界在 VALIDATING 就拦了）
    *why = "规划出的轨迹没通过限位校验（第 " + std::to_string(check.bad_point) + " 个点）：" +
      check.why + " —— ⚠️ 这条护栏是必要的：JTC 自己不查限位，越界会顶在机械硬限位上堵转";
    *code = JOINT_LIMIT;
    return false;
  }
  job_->planned = std::move(t);
  job_->jtc_joint_names = msg.joint_names;
  return true;
}

// ══════════════════════════ 执行 ══════════════════════════

void MotionServer::send_jtc_goal()
{
  if (!jtc_client_->action_server_is_ready()) {
    fail(HARDWARE_ERROR, "JTC 的 " + jtc_action_ + " 还没就绪（controller 没起或不在 active？）");
    return;
  }

  FollowJointTrajectory::Goal goal;
  goal.trajectory.joint_names = job_->jtc_joint_names;
  for (const auto & s : job_->planned.points) {
    trajectory_msgs::msg::JointTrajectoryPoint p;
    p.positions = s.positions;
    p.time_from_start.sec = static_cast<int32_t>(s.time_from_start);
    p.time_from_start.nanosec = static_cast<uint32_t>(
      (s.time_from_start - static_cast<double>(p.time_from_start.sec)) * 1e9);
    goal.trajectory.points.push_back(p);
  }

  {
    std::lock_guard<std::mutex> lk(res_mu_);
    jtc_done_ = false;
    jtc_ok_ = false;
    jtc_result_.reset();
    jtc_goal_handle_.reset();
  }
  jtc_cancel_sent_ = false;

  rclcpp_action::Client<FollowJointTrajectory>::SendGoalOptions opts;
  opts.goal_response_callback = [this](JtcGoalHandle::SharedPtr handle) {
      std::lock_guard<std::mutex> lk(res_mu_);
      jtc_goal_handle_ = handle;
      if (!handle) {jtc_done_ = true; jtc_ok_ = false;}
    };
  opts.result_callback = [this](const JtcGoalHandle::WrappedResult & wrapped) {
      std::lock_guard<std::mutex> lk(res_mu_);
      jtc_result_ = wrapped.result;
      jtc_ok_ = (wrapped.code == rclcpp_action::ResultCode::SUCCEEDED);
      jtc_done_ = true;
    };
  jtc_client_->async_send_goal(goal, opts);
}

}  // namespace arm_motion
