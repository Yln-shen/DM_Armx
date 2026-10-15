// motion_server_node.cpp —— `arm_motion` 的可执行节点：把 `ExecuteMotion` action server 挂起来。
//
// 职责边界：本文件**只做装配**（读参数 → 加载命名目标 → 建 server → spin）。
// 状态机在 `MotionServer` 里，校验在 `named_targets` / `trajectory_utils` 里 —— 都能脱离节点单测。
//
// ⚠️ 单线程 executor：状态机全程非阻塞（陷阱 #57）。**别改成多线程**，除非把
//    `MotionServer` 里的共享状态重新审一遍。
#include <rclcpp/rclcpp.hpp>

#include <ament_index_cpp/get_package_share_directory.hpp>

#include <memory>
#include <string>

#include "arm_motion/motion_server.hpp"
#include "arm_motion/named_targets.hpp"

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  auto node = std::make_shared<rclcpp::Node>("arm_motion_server");

  // ── 参数（默认值见 config/motion_params.yaml）──
  const std::string group = node->declare_parameter<std::string>("planning_group", "arm");
  const double plan_timeout = node->declare_parameter<double>("planning_timeout_sec", 5.0);
  const double exec_timeout = node->declare_parameter<double>("exec_timeout_sec", 30.0);
  const double state_wait = node->declare_parameter<double>("state_wait_sec", 2.0);
  const double settle = node->declare_parameter<double>("verify_settle_sec", 0.30);
  const double vel_scaling = node->declare_parameter<double>("default_velocity_scaling", 0.10);
  const double acc_scaling = node->declare_parameter<double>("default_acceleration_scaling", 0.10);
  const double max_residual = node->declare_parameter<double>("max_residual_rad", 0.05);
  const double cancel_confirm =
    node->declare_parameter<double>("cancel_confirm_timeout_sec", 1.0);
  const std::string mg_action = node->declare_parameter<std::string>("move_group_action", "/move_action");
  const std::string cart_srv =
    node->declare_parameter<std::string>("cartesian_path_service", "/compute_cartesian_path");
  const std::string jtc_action =
    node->declare_parameter<std::string>("jtc_action", "/arm_controller/follow_joint_trajectory");
  const std::string js_topic = node->declare_parameter<std::string>("joint_states_topic", "/joint_states");
  std::string named_file = node->declare_parameter<std::string>("named_targets_file", "");

  if (named_file.empty()) {
    named_file = ament_index_cpp::get_package_share_directory("arm_motion") +
      "/config/named_targets.yaml";
  }

  // ── 命名目标表：**启动即校验**，不合格就 FATAL（宁可起不来，也别带着越界目标跑）──
  arm_motion::NamedTargets targets;
  std::string err;
  if (!targets.load(named_file, &err)) {
    RCLCPP_FATAL(node->get_logger(), "命名目标表加载失败：%s", err.c_str());
    RCLCPP_FATAL(node->get_logger(),
      "文件：%s —— 这条校验是**故意的**：越界的目标不该等到发出去才拦（JTC 自己不查限位）",
      named_file.c_str());
    rclcpp::shutdown();
    return 1;
  }
  {
    std::string list;
    for (const auto & n : targets.names()) {list += " " + n;}
    RCLCPP_INFO(node->get_logger(), "命名目标表已加载：%s（%zu 个：%s）",
      named_file.c_str(), targets.names().size(), list.c_str());
  }

  // ── 装配 ──
  arm_motion::MotionServer server(node.get(), targets, group);
  server.set_scalings(vel_scaling, acc_scaling);
  server.set_timeouts(plan_timeout, exec_timeout, state_wait, settle);
  server.set_tolerances(max_residual, cancel_confirm);
  server.set_topic_names(jtc_action, js_topic);
  server.set_move_group_names(mg_action, cart_srv);
  server.start();

  RCLCPP_INFO(node->get_logger(),
    "ExecuteMotion 已就绪：规划组=%s、JTC=%s、move_group=%s\n"
    "  ⚠️ 本轮支持 TARGET_JOINTS 与 TARGET_NAMED；TARGET_POSE 未实现（会返回 error 1）\n"
    "  ⚠️ 默认缩放 vel=%.2f acc=%.2f（真机保守；首轮别调大）",
    group.c_str(), jtc_action.c_str(), mg_action.c_str(), vel_scaling, acc_scaling);

  rclcpp::spin(node);
  rclcpp::shutdown();
  return 0;
}
