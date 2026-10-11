// motion_client.cpp —— 实现见头文件顶部的说明（非阻塞、无日志、只产出事件）。
#include "arm_motion/motion_client.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <stdexcept>

namespace arm_motion
{

namespace
{
std::string fmt(const char * f, ...)
{
  char buf[512];
  va_list ap;
  va_start(ap, f);
  std::vsnprintf(buf, sizeof(buf), f, ap);
  va_end(ap);
  return std::string(buf);
}
}  // namespace

MotionClient::MotionClient(
  rclcpp::Node * node,
  const std::vector<std::string> & joint_names,
  const std::string & action_name,
  const std::string & state_topic)
: node_(node), joint_names_(joint_names)
{
  if (node_ == nullptr) {throw std::invalid_argument("MotionClient: node 不能是空指针");}
  if (joint_names_.empty()) {throw std::invalid_argument("MotionClient: joint_names 不能是空");}
  // ⚠️ QoS 必须与 joint_state_broadcaster 一致（RELIABLE + TRANSIENT_LOCAL）；
  //    用 SensorDataQoS()（BEST_EFFORT）会**静默零帧**（陷阱 #58）。
  auto qos = rclcpp::QoS(rclcpp::KeepLast(1)).reliable().transient_local();
  state_sub_ = node_->create_subscription<sensor_msgs::msg::JointState>(
    state_topic, qos,
    [this](sensor_msgs::msg::JointState::SharedPtr msg) {last_state_ = std::move(msg);});
  action_client_ = rclcpp_action::create_client<FollowJointTrajectory>(node_, action_name);
}

void MotionClient::set_vlim(double rad_s)
{
  if (!(rad_s > 0.0)) {throw std::invalid_argument("MotionClient: vlim 必须 > 0（rad/s）");}
  vlim_ = rad_s;
}
void MotionClient::set_tolerance(double tol_rad)
{
  if (!(tol_rad >= 0.0)) {throw std::invalid_argument("MotionClient: tolerance 不能是负数");}
  tol_rad_ = tol_rad;
}
void MotionClient::set_timeout(double sec)
{
  if (!(sec > 0.0)) {throw std::invalid_argument("MotionClient: timeout 必须 > 0");}
  timeout_sec_ = sec;
}
void MotionClient::set_state_wait(double sec)
{
  if (!(sec > 0.0)) {throw std::invalid_argument("MotionClient: state_wait 必须 > 0");}
  state_wait_sec_ = sec;
}
void MotionClient::set_action_wait(double sec)
{
  if (!(sec > 0.0)) {throw std::invalid_argument("MotionClient: action_wait 必须 > 0");}
  action_wait_sec_ = sec;
}
void MotionClient::set_goal_reply_wait(double sec)
{
  if (!(sec > 0.0)) {throw std::invalid_argument("MotionClient: goal_reply_wait 必须 > 0");}
  goal_reply_wait_sec_ = sec;
}
void MotionClient::set_verify_delay(double sec)
{
  if (!(sec >= 0.0)) {throw std::invalid_argument("MotionClient: verify_delay 不能是负数");}
  verify_delay_sec_ = sec;
}
void MotionClient::set_cancel_wait(double sec)
{
  if (!(sec > 0.0)) {throw std::invalid_argument("MotionClient: cancel_wait 必须 > 0");}
  cancel_wait_sec_ = sec;
}

// ───────────────────────── 护栏 ─────────────────────────

bool MotionClient::validate_joint_names(std::string * why) const
{
  const std::size_t n = arm_limits::kJointNames.size();
  auto fail = [&](const std::string & m) {if (why) {*why = m;} return false;};
  if (joint_names_.size() != n) {
    return fail(fmt("joint_names 有 %zu 个，限位表有 %zu 个", joint_names_.size(), n));
  }
  for (std::size_t i = 0; i < n; ++i) {
    if (joint_names_[i] != arm_limits::kJointNames[i]) {
      return fail(fmt(
        "joint_names[%zu] = %s，但限位表里是 %s —— 顺序/名字必须与 arm_description/config 的真源一致",
        i, joint_names_[i].c_str(), arm_limits::kJointNames[i]));
    }
  }
  return true;
}

bool MotionClient::validate_target(
  const std::vector<double> & target, std::string * why, const std::string & label) const
{
  auto fail = [&](const std::string & m) {if (why) {*why = m;} return false;};
  if (target.size() != joint_names_.size()) {
    return fail(fmt(
      "%s 有 %zu 个数，joint_names 有 %zu 个 —— 必须一一对应",
      label.c_str(), target.size(), joint_names_.size()));
  }
  for (std::size_t i = 0; i < target.size(); ++i) {
    const double lo = arm_limits::kModelLimits[i][0];
    const double hi = arm_limits::kModelLimits[i][1];
    if (!std::isfinite(target[i])) {
      return fail(fmt("%s[%zu]（%s）不是有限数", label.c_str(), i, joint_names_[i].c_str()));
    }
    if (target[i] < lo || target[i] > hi) {
      return fail(fmt(
        "%s[%zu]（%s）= %.6f rad **越界**，允许 [%.6f, %.6f]（模型坐标；真源 joint.yaml + align.yaml）",
        label.c_str(), i, joint_names_[i].c_str(), target[i], lo, hi));
    }
  }
  return true;
}

// ───────────────────────── 状态 ─────────────────────────

bool MotionClient::have_full_state() const
{
  return last_state_ != nullptr && current_positions().size() == joint_names_.size();
}

std::vector<double> MotionClient::current_positions() const
{
  std::vector<double> out(joint_names_.size(), std::nan(""));
  if (last_state_ == nullptr) {return out;}
  for (std::size_t i = 0; i < joint_names_.size(); ++i) {
    for (std::size_t k = 0; k < last_state_->name.size() && k < last_state_->position.size(); ++k) {
      if (last_state_->name[k] == joint_names_[i]) {
        out[i] = last_state_->position[k];
        break;
      }
    }
  }
  return out;
}

const char * MotionClient::phase_name() const
{
  switch (phase_) {
    case Phase::WaitState: return "WAIT_STATE";
    case Phase::WaitServer: return "WAIT_SERVER";
    case Phase::WaitGoalReply: return "WAIT_GOAL_REPLY";
    case Phase::WaitResult: return "WAIT_RESULT";
    case Phase::Verify: return "VERIFY";
    case Phase::Cancelling: return "CANCELLING";
    case Phase::Done: return "DONE";
  }
  return "?";
}

bool MotionClient::running() const {return started_ && phase_ != Phase::Done;}

void MotionClient::push(MotionEvent::Level level, const std::string & text)
{
  events_.push_back(MotionEvent{level, text});
}

// ───────────────────────── 一次运动 ─────────────────────────

bool MotionClient::start(const std::vector<double> & target, std::string * why)
{
  if (!validate_joint_names(why)) {return false;}
  if (!validate_target(target, why)) {return false;}
  target_ = target;
  started_ = true;
  ok_ = false;
  phase_ = Phase::WaitState;
  phase_since_ = started_at_ = node_->now();
  start_positions_.clear();
  final_positions_.clear();
  travel_ = duration_ = max_residual_ = 0.0;
  goal_error_code_ = 0;
  goal_error_string_.clear();
  goal_future_ = std::shared_future<GoalHandle::SharedPtr>();
  goal_handle_.reset();
  result_future_ = std::shared_future<GoalHandle::WrappedResult>();
  cancel_future_ = std::shared_future<action_msgs::srv::CancelGoal::Response::SharedPtr>();
  cancel_sent_ = false;
  cancel_confirmed_ = false;
  events_.clear();
  return true;
}

bool MotionClient::tick()
{
  if (!started_ || phase_ == Phase::Done) {return true;}
  bool done = false;
  switch (phase_) {
    case Phase::WaitState: done = tick_wait_state(); break;
    case Phase::WaitServer: done = tick_wait_server(); break;
    case Phase::WaitGoalReply: done = tick_wait_goal_reply(); break;
    case Phase::WaitResult: done = tick_wait_result(); break;
    case Phase::Verify: done = tick_verify(); break;
    case Phase::Cancelling: done = tick_cancelling(); break;
    case Phase::Done: return true;
  }
  if (done) {phase_ = Phase::Done;}
  return done;
}

bool MotionClient::tick_wait_state()
{
  if (have_full_state()) {
    start_positions_ = current_positions();
    travel_ = 0.0;
    for (std::size_t i = 0; i < joint_names_.size(); ++i) {
      travel_ = std::max(travel_, std::fabs(target_[i] - start_positions_[i]));
    }
    duration_ = std::max(1.0, travel_ / vlim_);
    // ⚠️ 这里**没有事件**：措辞（"折叠位"）属于调用方，它用 start_positions()/travel()/
    //    planned_duration() 自己打那两行日志。
    phase_ = Phase::WaitServer;
    phase_since_ = node_->now();
    return false;                                  // 只是推进，整个运动没结束
  }
  if ((node_->now() - started_at_).seconds() > state_wait_sec_) {
    push(MotionEvent::kError, fmt(
      "%.0f s 内没拿到完整的 /joint_states ⇒ 拒绝发轨迹。"
      "（查：`ros2 topic hz /joint_states` 与 `ros2 topic info /joint_states --verbose` 的 QoS）",
      state_wait_sec_));
    return true;
  }
  return false;
}

bool MotionClient::tick_wait_server()
{
  if (action_client_->action_server_is_ready()) {
    return send_goal();                            // send_goal 恒返回 false（还没结束）
  }
  if ((node_->now() - phase_since_).seconds() > action_wait_sec_) {
    push(MotionEvent::kError, fmt(
      "等不到 /arm_controller/follow_joint_trajectory（%.0f s）—— "
      "launch 要 spawn_arm_controller:=true，且别把硬件停在只读模式", action_wait_sec_));
    return true;
  }
  return false;
}

bool MotionClient::send_goal()
{
  FollowJointTrajectory::Goal goal;
  goal.trajectory.header.stamp = node_->now();
  goal.trajectory.joint_names = joint_names_;
  trajectory_msgs::msg::JointTrajectoryPoint point;
  point.positions = target_;
  point.velocities.assign(joint_names_.size(), 0.0);   // 末端速度 0
  point.time_from_start = rclcpp::Duration::from_seconds(duration_);
  goal.trajectory.points.push_back(point);

  goal_future_ = action_client_->async_send_goal(goal);
  phase_ = Phase::WaitGoalReply;
  phase_since_ = node_->now();
  return false;
}

void MotionClient::cancel_goal(const std::string & why)
{
  push(MotionEvent::kError, why + " ⇒ **取消这条轨迹**（臂保持在取消时的位置、仍使能）");
  if (goal_handle_ && !cancel_sent_) {
    cancel_sent_ = true;
    cancel_future_ = action_client_->async_cancel_goal(goal_handle_);
  }
  phase_ = Phase::Cancelling;
  phase_since_ = node_->now();
}

bool MotionClient::tick_cancelling()
{
  if (cancel_future_.valid() &&
    cancel_future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
  {
    cancel_confirmed_ = true;
    push(MotionEvent::kWarn, "取消已确认");
    return true;
  }
  if ((node_->now() - phase_since_).seconds() > cancel_wait_sec_) {
    push(MotionEvent::kWarn, fmt(
      "%.0f s 内没等到取消确认 —— 不再等（轨迹可能还在走，臂仍使能）", cancel_wait_sec_));
    return true;
  }
  return false;
}

bool MotionClient::tick_wait_goal_reply()
{
  if (goal_future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
    goal_handle_ = goal_future_.get();
    if (!goal_handle_) {
      push(MotionEvent::kError,
        "goal 被 arm_controller 拒绝（目标越界 / 不可达？） —— 没有 goal 可取消，臂停在原地、仍使能");
      return true;
    }
    push(MotionEvent::kInfo, "轨迹已接受，等待执行…");
    result_future_ = action_client_->async_get_result(goal_handle_);
    phase_ = Phase::WaitResult;
    phase_since_ = node_->now();
    return false;
  }
  if ((node_->now() - phase_since_).seconds() > goal_reply_wait_sec_) {
    cancel_goal(fmt("发 goal %.0f s 没回应", goal_reply_wait_sec_));
  }
  return false;                                    // 已切到 CANCELLING，等下一 tick
}

bool MotionClient::tick_wait_result()
{
  if (result_future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
    auto wrapped = result_future_.get();
    goal_error_code_ = wrapped.result->error_code;
    goal_error_string_ = wrapped.result->error_string;
    const bool reached = (wrapped.code == rclcpp_action::ResultCode::SUCCEEDED) &&
      (wrapped.result->error_code == FollowJointTrajectory::Result::SUCCESSFUL);
    if (!reached) {
      push(MotionEvent::kError, fmt(
        "轨迹没到位。error_code=%d（%s）code=%d",
        wrapped.result->error_code, wrapped.result->error_string.c_str(),
        static_cast<int>(wrapped.code)));
      return true;
    }
    phase_ = Phase::Verify;
    phase_since_ = node_->now();
    return false;
  }
  if ((node_->now() - phase_since_).seconds() > timeout_sec_) {
    cancel_goal(fmt("%.1f s 内没跑完", timeout_sec_));
  }
  return false;                                    // 已切到 CANCELLING（或还在跑）
}

bool MotionClient::tick_verify()
{
  if ((node_->now() - phase_since_).seconds() < verify_delay_sec_) {return false;}  // 等状态刷新
  if (!have_full_state()) {return false;}
  final_positions_ = current_positions();
  max_residual_ = 0.0;
  for (std::size_t i = 0; i < joint_names_.size(); ++i) {
    max_residual_ = std::max(max_residual_, std::fabs(final_positions_[i] - target_[i]));
  }
  if (max_residual_ > tol_rad_) {
    // 逐关节明细是业务日志（调用方用 final_positions()/target() 自己打）
    push(MotionEvent::kError, fmt(
      "最大残差 %.4f rad > tol_rad %.4f ⇒ 判失败（臂仍使能、停在原地）", max_residual_, tol_rad_));
    return true;
  }
  ok_ = true;
  return true;
}

}  // namespace arm_motion
