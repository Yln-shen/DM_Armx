// controller_gate.hpp —— 管理层"控制器侧"的唯一出口（见 docs/MANAGE_ARCH.md §3.1）。
//
// 干什么：
//   · `switch_controller` / `list_controllers`（CM 的服务）—— 控制器的激活与停用；
//   · 持有 `arm_motion::MotionClient`（发单点轨迹给 JTC 的那一份，M9.0 抽出来的，别重写）。
//
// **为什么"控制器状态"以 list_controllers 的缓存为准**：`switch_controller` 的 `ok=true`
// 只说明"命令被受理"，不说明"现在的状态就是我要的"（BEST_EFFORT 还会容忍缺席的控制器）。
// 所以本类同时维护一个**非阻塞刷新的 active 缓存**，上层用它做判断（§3.2 的 M-3 顺序）。
#ifndef ARM_MANAGE__CONTROLLER_GATE_HPP_
#define ARM_MANAGE__CONTROLLER_GATE_HPP_

#include <arm_motion/motion_client.hpp>
#include <controller_manager_msgs/srv/list_controllers.hpp>
#include <controller_manager_msgs/srv/switch_controller.hpp>
#include <rclcpp/rclcpp.hpp>

#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace arm_manage
{

class ControllerGate
{
public:
  ControllerGate(
    rclcpp::Node * node, const std::vector<std::string> & joint_names,
    const std::string & cm_ns, const std::string & controller,
    const std::string & action_name = "/arm_controller/follow_joint_trajectory")
  : node_(node), controller_(controller), motion_(node, joint_names, action_name)
  {
    list_ = node_->create_client<controller_manager_msgs::srv::ListControllers>(
      cm_ns + "/list_controllers");
    switch_ = node_->create_client<controller_manager_msgs::srv::SwitchController>(
      cm_ns + "/switch_controller");
    request_refresh();
  }

  arm_motion::MotionClient & motion() {return motion_;}

  // 控制器是否 active（**缓存**；由 tick 里的 poll_refresh() 更新）
  bool is_active() const {return active_;}
  bool known() const {return known_;}          // 缓存里有没有找到这个控制器

  // ── 非阻塞刷新：每 N 个 tick 调一次 request_refresh()，然后每个 tick 调 poll_refresh() ──
  void request_refresh()
  {
    if (refresh_future_.valid() || !list_->service_is_ready()) {return;}
    refresh_future_ = list_->async_send_request(
      std::make_shared<controller_manager_msgs::srv::ListControllers::Request>()).future.share();
  }

  void poll_refresh()
  {
    if (!refresh_future_.valid()) {return;}
    if (refresh_future_.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
      return;
    }
    auto res = refresh_future_.get();
    refresh_future_ = {};                       // 复位 ⇒ 下一次 request_refresh() 会再发
    known_ = false;
    active_ = false;
    for (const auto & c : res->controller) {
      if (c.name == controller_) {
        known_ = true;
        active_ = (c.state == "active");
      }
    }
  }

  // ── 非阻塞切换：request_switch() 发请求，poll_switch() 取结果 ──
  bool request_switch(bool activate, std::string * why)
  {
    if (switch_pending_) {
      if (why != nullptr) {*why = "控制器切换已在进行中";}
      return false;
    }
    if (!switch_->service_is_ready()) {
      if (why != nullptr) {*why = "CM 的 switch_controller 不可用（controller_manager 起了吗？）";}
      return false;
    }
    auto req = std::make_shared<controller_manager_msgs::srv::SwitchController::Request>();
    if (activate) {
      req->activate_controllers = {controller_};
    } else {
      req->deactivate_controllers = {controller_};
    }
    // BEST_EFFORT：**容忍"本来就不是那个状态"**（停一个没 active 的控制器不该算错）。
    // 真正的判据是切换后用 list_controllers 复核（见上面的注释）。
    req->strictness = controller_manager_msgs::srv::SwitchController::Request::BEST_EFFORT;
    req->activate_asap = true;
    req->timeout.sec = 2;
    req->timeout.nanosec = 0;
    switch_future_ = switch_->async_send_request(req).future.share();
    switch_pending_ = true;
    return true;
  }

  bool poll_switch(bool * ok, std::string * msg)
  {
    if (!switch_pending_) {return false;}
    if (switch_future_.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
      return false;
    }
    auto res = switch_future_.get();
    switch_pending_ = false;
    switch_future_ = {};
    *ok = res->ok;
    *msg = res->message;
    request_refresh();                          // 立刻复核真实状态
    return true;
  }

  bool switch_pending() const {return switch_pending_;}

private:
  rclcpp::Node * node_;
  std::string controller_;
  arm_motion::MotionClient motion_;
  rclcpp::Client<controller_manager_msgs::srv::ListControllers>::SharedPtr list_;
  rclcpp::Client<controller_manager_msgs::srv::SwitchController>::SharedPtr switch_;
  std::shared_future<controller_manager_msgs::srv::ListControllers::Response::SharedPtr>
  refresh_future_;
  std::shared_future<controller_manager_msgs::srv::SwitchController::Response::SharedPtr>
  switch_future_;
  bool switch_pending_ = false;
  bool known_ = false;
  bool active_ = false;
};

}  // namespace arm_manage

#endif  // ARM_MANAGE__CONTROLLER_GATE_HPP_
