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
//    （2026-10-07 M9.0：这条语义将来由 `arm_manage` 的 `SafeDisarm` 保证 —— 它先收臂再失能。）
//
// ⚠️⚠️ **两条真机踩出来的实现约束**（改这个文件前先读，否则会重蹈覆辙）：
//   1. **不许在 executor 回调里阻塞等数据**。第一版 `main()` 里先 `park_once()`、后 `spin`：
//      订阅回调根本没机会跑。改成"定时器里跑 park_once"之后**仍然失败** —— 因为
//      `wait_for_state()` 在定时器回调里 `sleep` 5 秒，executor 一直出不来，
//      订阅回调**照样进不来** ⇒ 恒报"拿不到 /joint_states"。
//      ⇒ 正确做法是**非阻塞状态机**：每 tick 只做一点、立刻返回，把执行器让出来。
//      （2026-10-07 M9.0：这套状态机连同 QoS / 取消 / 残差验证已抽到 `arm_motion::MotionClient`，
//        本节点只做参数与业务日志。）
//   2. **QoS 必须匹配 `joint_state_broadcaster`**（RELIABLE + TRANSIENT_LOCAL）。
//      用 `SensorDataQoS()`（BEST_EFFORT）收不到任何一帧。排查入口：
//      `ros2 topic info /joint_states --verbose` 看对端 QoS。
//
// 用法：
//   ros2 run arm_application safe_park_node
//   ros2 run arm_application safe_park_node --ros-args -p vlim:=0.1 -p park_pose:="[...]"
// 前置：`real_control.launch.py enable_on_activate:=true spawn_arm_controller:=true`
#include <rclcpp/rclcpp.hpp>

#include <arm_description/joint_limits.hpp>
#include <arm_motion/motion_client.hpp>

#include <cmath>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
// 折叠位（**模型坐标**，2026-10-05 真机实测）：失能后机械臂会自由塌回它附近，
// 而且 j2/j3 会顶在机械硬限位上（见 AGENTS 陷阱 #39/#47）。
const std::vector<double> kDefaultParkPose = {
  1.4724, 0.0008, -0.0233, 0.0027, 0.0069, 0.0149};
// 关节名与**模型坐标限位**都来自 arm_description 构建期生成的头文件
// （真源：joint.yaml + align.yaml；生成器 scripts/gen_joint_limits_header.py）。
// ⚠️ 别在这里另抄一份 —— JTC **不校验关节限位**，越界目标会顶在机械硬限位上堵转
//   （见陷阱 #59），所以这份限位是应用层唯一的护栏，必须与真源同源。
//   （护栏本身现在在 arm_motion::MotionClient 里，本节点通过它校验 park_pose。）
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

    motion_ = std::make_unique<arm_motion::MotionClient>(this, joint_names_);
    validate_park_pose();                                 // ★ 核心护栏：发目标前先自己查限位
    motion_->set_vlim(vlim_);
    motion_->set_tolerance(tol_rad_);
    motion_->set_timeout(timeout_sec_);
    motion_->set_state_wait(state_wait_sec_);
    std::string why;
    if (!motion_->start(park_pose_, &why)) {throw std::invalid_argument(why);}
  }

  // 每 tick 推进一步；返回 true 表示**收工**（成功与否看 ok()）。
  bool tick()
  {
    if (done_) {return true;}
    const std::string before = motion_->phase_name();
    const bool finished = motion_->tick();
    log_events();                                   // 机制类日志（来自 MotionClient 的事件）
    if (!logged_start_ && before == "WAIT_STATE" && !motion_->start_positions().empty()) {
      log_start_plan();
      logged_start_ = true;
    }
    if (finished) {
      if (before == "VERIFY") {log_verify_table();}
      log_final();
      done_ = true;
    }
    return finished;
  }
  bool ok() const {return motion_->ok();}

private:
  // ★ 核心护栏：**发目标前先自己查限位**。
  //   `arm_controller`(JTC) 不校验 —— 越界目标它照执行，臂会顶在机械硬限位上堵转到超时
  //   （真机实测：joint2 给 +99 rad ⇒ 堵转 60 s、effort 22.5 N·m、线圈 62 ℃、全程 err=0）。
  void validate_park_pose() const
  {
    std::string why;
    if (!motion_->validate_joint_names(&why)) {throw std::invalid_argument(why);}
    if (!motion_->validate_target(park_pose_, &why, "park_pose")) {
      throw std::invalid_argument(
        why + "。**拒绝发目标** —— JTC 不会替你挡，越界会把臂顶在硬限位上堵转");
    }
  }

  // 事件队列 → ROS 日志（前缀 "safe_park："；严重级跟着事件走）
  void log_events()
  {
    for (const auto & e : motion_->events()) {
      const std::string text = "safe_park：" + e.text;
      if (e.level == arm_motion::MotionEvent::kError) {
        RCLCPP_ERROR(get_logger(), "%s", text.c_str());
      } else if (e.level == arm_motion::MotionEvent::kWarn) {
        RCLCPP_WARN(get_logger(), "%s", text.c_str());
      } else {
        RCLCPP_INFO(get_logger(), "%s", text.c_str());
      }
    }
    motion_->clear_events();
  }

  void log_start_plan()
  {
    const std::vector<double> & start = motion_->start_positions();
    RCLCPP_WARN(get_logger(),
      "safe_park：已读到当前姿态 → 折叠位（模型坐标）。行程 %.3f rad、vlim %.2f ⇒ 预计 %.1f s",
      motion_->travel(), vlim_, motion_->planned_duration());
    for (std::size_t i = 0; i < joint_names_.size(); ++i) {
      RCLCPP_INFO(get_logger(), "  %s: 现在 %+.4f → 目标 %+.4f（走 %+.4f）",
        joint_names_[i].c_str(), start[i], park_pose_[i], park_pose_[i] - start[i]);
    }
  }

  void log_verify_table()
  {
    const std::vector<double> & end = motion_->final_positions();
    if (end.size() != joint_names_.size()) {return;}
    for (std::size_t i = 0; i < joint_names_.size(); ++i) {
      RCLCPP_INFO(get_logger(), "  %s: 终点 %+.4f（目标 %+.4f，差 %.4f rad）",
        joint_names_[i].c_str(), end[i], park_pose_[i], std::fabs(end[i] - park_pose_[i]));
    }
  }

  // 失败路径的说明由事件打过（"最大残差 … ⇒ 判失败" / "轨迹没到位" / "取消…"），这里只管成功
  void log_final()
  {
    if (!motion_->ok()) {return;}
    RCLCPP_WARN(get_logger(),
      "safe_park：**完成** —— 臂已在折叠位并保持（最大残差 %.4f rad）。\n"
      "  ⚠️ 臂**仍然使能**：硬件还在 ACTIVE，插件继续每周期发保持帧托着它。\n"
      "  要真正失能：让 `ros2_control_node` 正常退出（Ctrl-C 那个 launch）——"
      "它会在 on_deactivate() 里失能全部电机。", motion_->max_residual());
  }

  double vlim_ = 0.15;
  double tol_rad_ = 0.05;
  double timeout_sec_ = 60.0;
  double state_wait_sec_ = 10.0;
  std::vector<double> park_pose_;
  std::vector<std::string> joint_names_;
  std::unique_ptr<arm_motion::MotionClient> motion_;
  bool done_ = false;
  bool logged_start_ = false;
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
