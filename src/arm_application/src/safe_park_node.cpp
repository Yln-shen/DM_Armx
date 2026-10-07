// safe_park_node.cpp —— **应用层**的收臂节点（safe_park）。
//
// 为什么在应用层、而不是驱动层：
//   驱动层（`motor_driver_hardware/DmSystemInterface`）只该管"电机 I/O + 状态"。
//   "摆到哪个姿态"是**业务**，属于应用层。这里用 ros2_control 的**正常轨迹通路**
//   （JointTrajectoryController 的 FollowJointTrajectory）把臂送回**折叠位**，不碰协议、不碰串口。
//
// 数据流：
//   本节点 --FollowJointTrajectory--> arm_controller(JTC) --position 命令接口--> DmSystemInterface
//                                                                              --POS_VEL--> 电机
//
// ⚠️ **本节点结束后臂仍然使能**（硬件组件停在 ACTIVE，插件继续每周期发保持帧托着）。
//    `on_deactivate()` 只在 `ros2_control_node` 退出 / 显式切硬件组件状态时才触发；
//    **停掉 arm_controller 并不会让硬件 deactivate**（控制器与硬件组件是两套生命周期）。
//    ⇒ 收臂只负责"摆到折叠位并停住"；真正失能交给 `ros2_control_node` 正常退出时的
//      `on_deactivate()`（那一步是一次性全部失能，没有 j4 最后）。
//
// ⚠️⚠️ **两条真机踩出来的实现约束**（改这个文件前先读，否则会重蹈覆辙）：
//   1. **不许在 executor 回调里阻塞等数据**。第一版 `main()` 里先 `park_once()`、后 `spin`：
//      订阅回调根本没机会跑。改成"定时器里跑 park_once"之后**仍然失败** —— 因为
//      `wait_for_state()` 在定时器回调里 `sleep` 5 秒，executor 一直出不来，
//      订阅回调**照样进不来** ⇒ 恒报"拿不到 /joint_states"。
//      ⇒ 正确做法是**非阻塞状态机**：每 tick 只做一点、立刻返回，把执行器让出来。
//   2. **QoS 必须匹配 `joint_state_broadcaster`**（RELIABLE + TRANSIENT_LOCAL）。
//      用 `SensorDataQoS()`（BEST_EFFORT）收不到任何一帧。排查入口：
//      `ros2 topic info /joint_states --verbose` 看对端 QoS。
//
// 用法：
//   ros2 run arm_application safe_park_node
//   ros2 run arm_application safe_park_node --ros-args -p vlim:=0.1 -p park_pose:="[...]"
// 前置：`real_control.launch.py enable_on_activate:=true spawn_arm_controller:=true`
#include <rclcpp/rclcpp.hpp>
#include <rclcpp_action/rclcpp_action.hpp>

#include <arm_description/joint_limits.hpp>
#include <action_msgs/srv/cancel_goal.hpp>
#include <control_msgs/action/follow_joint_trajectory.hpp>
#include <sensor_msgs/msg/joint_state.hpp>
#include <trajectory_msgs/msg/joint_trajectory.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
using FollowJointTrajectory = control_msgs::action::FollowJointTrajectory;
using GoalHandle = rclcpp_action::ClientGoalHandle<FollowJointTrajectory>;

// 折叠位（**模型坐标**，2026-10-05 真机实测）：失能后机械臂会自由塌回它附近，
// 而且 j2/j3 会顶在机械硬限位上（见 AGENTS 陷阱 #39/#47）。
const std::vector<double> kDefaultParkPose = {
  1.4724, 0.0008, -0.0233, 0.0027, 0.0069, 0.0149};
// 关节名与**模型坐标限位**都来自 arm_description 构建期生成的头文件
// （真源：joint.yaml + align.yaml；生成器 scripts/gen_joint_limits_header.py）。
// ⚠️ 别在这里另抄一份 —— JTC **不校验关节限位**，越界目标会顶在机械硬限位上堵转
//   （见陷阱 #59），所以这份限位是应用层唯一的护栏，必须与真源同源。
}  // namespace

class SafeParkNode : public rclcpp::Node
{
public:
  SafeParkNode()
  : rclcpp::Node("safe_park_node")
  {
    vlim_ = declare_parameter<double>("vlim", 0.15);      // 默认很慢；首轮上真机别加大
    tol_rad_ = declare_parameter<double>("tol_rad", 0.05);
    timeout_sec_ = declare_parameter<double>("timeout_sec", 60.0);
    state_wait_sec_ = declare_parameter<double>("state_wait_sec", 10.0);
    park_pose_ = declare_parameter<std::vector<double>>("park_pose", kDefaultParkPose);
    const std::vector<std::string> default_names(
      arm_limits::kJointNames.begin(), arm_limits::kJointNames.end());
    joint_names_ = declare_parameter<std::vector<std::string>>("joint_names", default_names);
    (void)declare_parameter<bool>("disable_on_exit", false);   // 只为兼容；见文件头

    if (!(vlim_ > 0.0)) {throw std::invalid_argument("vlim 必须 > 0（rad/s）");}
    validate_joint_names();
    validate_park_pose();

    // ⚠️ QoS 见文件头第 2 条
    auto qos = rclcpp::QoS(rclcpp::KeepLast(1)).reliable().transient_local();
    state_sub_ = create_subscription<sensor_msgs::msg::JointState>(
      "joint_states", qos,
      [this](sensor_msgs::msg::JointState::SharedPtr msg) {last_state_ = std::move(msg);});
    action_client_ = rclcpp_action::create_client<FollowJointTrajectory>(
      this, "/arm_controller/follow_joint_trajectory");
    started_at_ = now();
  }

  // 关节名必须与生成的限位表**逐个一致**（顺序也一致）—— 否则下面按下标比限位就是错的
  void validate_joint_names() const
  {
    const std::size_t n = arm_limits::kJointNames.size();
    if (joint_names_.size() != n) {
      throw std::invalid_argument(
        "joint_names 有 " + std::to_string(joint_names_.size()) + " 个，限位表有 " +
        std::to_string(n) + " 个");
    }
    for (std::size_t i = 0; i < n; ++i) {
      if (joint_names_[i] != arm_limits::kJointNames[i]) {
        throw std::invalid_argument(
          "joint_names[" + std::to_string(i) + "] = " + joint_names_[i] +
          "，但限位表里是 " + arm_limits::kJointNames[i] +
          " —— 顺序/名字必须与 arm_description/config 的真源一致");
      }
    }
  }

  // ★ 核心护栏：**发目标前先自己查限位**。
  //   `arm_controller`(JTC) 不校验 —— 越界目标它照执行，臂会顶在机械硬限位上堵转到超时
  //   （真机实测：joint2 给 +99 rad ⇒ 堵转 60 s、effort 22.5 N·m、线圈 62 ℃、全程 err=0）。
  void validate_park_pose() const
  {
    if (park_pose_.size() != joint_names_.size()) {
      throw std::invalid_argument(
        "park_pose 有 " + std::to_string(park_pose_.size()) + " 个数，joint_names 有 " +
        std::to_string(joint_names_.size()) + " 个 —— 必须一一对应");
    }
    for (std::size_t i = 0; i < park_pose_.size(); ++i) {
      const double lo = arm_limits::kModelLimits[i][0];
      const double hi = arm_limits::kModelLimits[i][1];
      if (!std::isfinite(park_pose_[i])) {
        throw std::invalid_argument(
          "park_pose[" + std::to_string(i) + "]（" + joint_names_[i] + "）不是有限数");
      }
      if (park_pose_[i] < lo || park_pose_[i] > hi) {
        throw std::invalid_argument(
          "park_pose[" + std::to_string(i) + "]（" + joint_names_[i] + "）= " +
          std::to_string(park_pose_[i]) + " rad **越界**，允许 [" + std::to_string(lo) +
          ", " + std::to_string(hi) + "]（模型坐标；真源 joint.yaml + align.yaml）。"
          "**拒绝发目标** —— JTC 不会替你挡，越界会把臂顶在硬限位上堵转");
      }
    }
  }

  // 每 tick 推进一步；返回 true 表示**收工**（成功与否看 ok()）。
  bool tick()
  {
    switch (phase_) {
      case Phase::WaitState: return tick_wait_state();
      case Phase::WaitServer: return tick_wait_server();
      case Phase::WaitGoalReply: return tick_wait_goal_reply();
      case Phase::WaitResult: return tick_wait_result();
      case Phase::Verify: return tick_verify();
      case Phase::Cancelling: return tick_cancelling();
      case Phase::Done: return true;
    }
    return true;
  }
  bool ok() const {return ok_;}

private:
  enum class Phase {WaitState, WaitServer, WaitGoalReply, WaitResult, Verify, Cancelling, Done};

  bool have_full_state() const
  {
    return last_state_ != nullptr && current_positions().size() == joint_names_.size();
  }

  // 按 joint_names_ 的顺序从最近一帧 /joint_states 取位置（没见过的关节留 NaN）
  std::vector<double> current_positions() const
  {
    std::vector<double> out(joint_names_.size(), std::nan(""));
    if (last_state_ == nullptr) {return out;}
    for (std::size_t i = 0; i < joint_names_.size(); ++i) {
      for (std::size_t k = 0; k < last_state_->name.size() && k < last_state_->position.size();
        ++k)
      {
        if (last_state_->name[k] == joint_names_[i]) {
          out[i] = last_state_->position[k];
          break;
        }
      }
    }
    return out;
  }

  bool tick_wait_state()
  {
    if (have_full_state()) {
      const std::vector<double> start = current_positions();
      double travel = 0.0;
      for (std::size_t i = 0; i < joint_names_.size(); ++i) {
        travel = std::max(travel, std::fabs(park_pose_[i] - start[i]));
      }
      duration_ = std::max(1.0, travel / vlim_);
      RCLCPP_WARN(get_logger(),
        "safe_park：已读到当前姿态 → 折叠位（模型坐标）。行程 %.3f rad、vlim %.2f ⇒ 预计 %.1f s",
        travel, vlim_, duration_);
      for (std::size_t i = 0; i < joint_names_.size(); ++i) {
        RCLCPP_INFO(get_logger(), "  %s: 现在 %+.4f → 目标 %+.4f（走 %+.4f）",
          joint_names_[i].c_str(), start[i], park_pose_[i], park_pose_[i] - start[i]);
      }
      phase_ = Phase::WaitServer;
      phase_since_ = now();
      return false;
    }
    if ((now() - started_at_).seconds() > state_wait_sec_) {
      RCLCPP_ERROR(get_logger(),
        "safe_park：%.0f s 内没拿到完整的 /joint_states ⇒ 拒绝发轨迹。"
        "（查：`ros2 topic hz /joint_states` 与 `ros2 topic info /joint_states --verbose` 的 QoS）",
        state_wait_sec_);
      return true;
    }
    return false;
  }

  bool tick_wait_server()
  {
    if (action_client_->action_server_is_ready()) {return send_goal();}
    if ((now() - phase_since_).seconds() > 10.0) {
      RCLCPP_ERROR(get_logger(),
        "safe_park：等不到 /arm_controller/follow_joint_trajectory（10 s）—— "
        "launch 要 spawn_arm_controller:=true，且别把硬件停在只读模式");
      return true;
    }
    return false;
  }

  bool send_goal()
  {
    FollowJointTrajectory::Goal goal;
    goal.trajectory.header.stamp = now();
    goal.trajectory.joint_names = joint_names_;
    trajectory_msgs::msg::JointTrajectoryPoint point;
    point.positions = park_pose_;
    point.velocities.assign(joint_names_.size(), 0.0);   // 末端速度 0
    point.time_from_start = rclcpp::Duration::from_seconds(duration_);
    goal.trajectory.points.push_back(point);

    goal_future_ = action_client_->async_send_goal(goal);
    phase_ = Phase::WaitGoalReply;
    phase_since_ = now();
    return false;
  }

  // 放弃这条 goal：**必须真的取消**，否则 JTC 会继续把轨迹走完
  // （2026-10-07 真机验收 #3 抓到的：超时后节点退出了，臂却自己走到了目标 ——
  //  所以那句"臂停在当前位置"当时是错的）。
  void cancel_goal(const char * why)
  {
    RCLCPP_ERROR(get_logger(), "safe_park：%s ⇒ **取消这条轨迹**（臂保持在取消时的位置、仍使能）", why);
    if (goal_handle_ && !cancel_sent_) {
      cancel_sent_ = true;
      cancel_future_ = action_client_->async_cancel_goal(goal_handle_);
    }
    phase_ = Phase::Cancelling;
    phase_since_ = now();
  }

  bool tick_cancelling()
  {
    if (cancel_future_.valid() &&
      cancel_future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
    {
      RCLCPP_WARN(get_logger(), "safe_park：取消已确认");
      return true;
    }
    if ((now() - phase_since_).seconds() > 1.0) {
      RCLCPP_WARN(get_logger(),
        "safe_park：1 s 内没等到取消确认 —— 不再等（轨迹可能还在走，臂仍使能）");
      return true;
    }
    return false;
  }

  bool tick_wait_goal_reply()
  {
    if (goal_future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
      goal_handle_ = goal_future_.get();
      if (!goal_handle_) {
        RCLCPP_ERROR(get_logger(), "safe_park：goal 被 arm_controller 拒绝（目标越界 / 不可达？）"
          " —— 没有 goal 可取消，臂停在原地、仍使能");
        return true;
      }
      RCLCPP_INFO(get_logger(), "safe_park：轨迹已接受，等待执行…");
      result_future_ = action_client_->async_get_result(goal_handle_);
      phase_ = Phase::WaitResult;
      phase_since_ = now();
      return false;
    }
    if ((now() - phase_since_).seconds() > 5.0) {
      cancel_goal("发 goal 5 s 没回应");
      return false;
    }
    return false;
  }

  bool tick_wait_result()
  {
    if (result_future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
      auto wrapped = result_future_.get();
      const bool reached = (wrapped.code == rclcpp_action::ResultCode::SUCCEEDED) &&
        (wrapped.result->error_code == FollowJointTrajectory::Result::SUCCESSFUL);
      if (!reached) {
        RCLCPP_ERROR(get_logger(), "safe_park：轨迹没到位。error_code=%d（%s）code=%d",
          wrapped.result->error_code, wrapped.result->error_string.c_str(),
          static_cast<int>(wrapped.code));
        return true;
      }
      phase_ = Phase::Verify;
      phase_since_ = now();
      return false;
    }
    if ((now() - phase_since_).seconds() > timeout_sec_) {
      char why[128];
      std::snprintf(why, sizeof(why), "%.1f s 内没跑完", timeout_sec_);
      cancel_goal(why);
      return false;
    }
    return false;
  }

  bool tick_verify()
  {
    if ((now() - phase_since_).seconds() < 0.3) {return false;}   // 等状态刷新（非阻塞）
    if (!have_full_state()) {return false;}
    const std::vector<double> end = current_positions();
    double worst = 0.0;
    for (std::size_t i = 0; i < joint_names_.size(); ++i) {
      const double err = std::fabs(end[i] - park_pose_[i]);
      worst = std::max(worst, err);
      RCLCPP_INFO(get_logger(), "  %s: 终点 %+.4f（目标 %+.4f，差 %.4f rad）",
        joint_names_[i].c_str(), end[i], park_pose_[i], err);
    }
    if (worst > tol_rad_) {
      RCLCPP_ERROR(get_logger(),
        "safe_park：最大残差 %.4f rad > tol_rad %.4f ⇒ 判失败（臂仍使能、停在原地）",
        worst, tol_rad_);
      return true;
    }
    ok_ = true;
    RCLCPP_WARN(get_logger(),
      "safe_park：**完成** —— 臂已在折叠位并保持（最大残差 %.4f rad）。\n"
      "  ⚠️ 臂**仍然使能**：硬件还在 ACTIVE，插件继续每周期发保持帧托着它。\n"
      "  要真正失能：让 `ros2_control_node` 正常退出（Ctrl-C 那个 launch）——"
      "它会在 on_deactivate() 里失能全部电机。", worst);
    return true;
  }

  double vlim_ = 0.15;
  double tol_rad_ = 0.05;
  double timeout_sec_ = 60.0;
  double state_wait_sec_ = 10.0;
  double duration_ = 0.0;
  std::vector<double> park_pose_;
  std::vector<std::string> joint_names_;

  rclcpp::Time started_at_;
  rclcpp::Time phase_since_;
  Phase phase_ = Phase::WaitState;
  bool ok_ = false;

  sensor_msgs::msg::JointState::SharedPtr last_state_;
  rclcpp::Subscription<sensor_msgs::msg::JointState>::SharedPtr state_sub_;
  rclcpp_action::Client<FollowJointTrajectory>::SharedPtr action_client_;
  std::shared_future<GoalHandle::SharedPtr> goal_future_;
  GoalHandle::SharedPtr goal_handle_;
  std::shared_future<GoalHandle::WrappedResult> result_future_;
  std::shared_future<action_msgs::srv::CancelGoal::Response::SharedPtr> cancel_future_;
  bool cancel_sent_ = false;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  int rc = 1;
  try {
    auto node = std::make_shared<SafeParkNode>();
    // ⚠️ 非阻塞状态机由定时器驱动 —— **别改成在主流程里阻塞等待**（见文件头第 1 条）。
    auto timer = node->create_wall_timer(
      std::chrono::milliseconds(50), [node, &rc]() {
        if (node->tick()) {
          rc = node->ok() ? 0 : 1;
          rclcpp::shutdown();
        }
      });
    rclcpp::spin(node);
    (void)timer;
  } catch (const std::exception & e) {
    RCLCPP_FATAL(rclcpp::get_logger("safe_park_node"), "参数/运行出错：%s", e.what());
  }
  rclcpp::shutdown();
  return rc;
}
