// hardware_gate.hpp —— 管理层"硬件侧"的唯一出口（见 docs/MANAGE_ARCH.md §3.1）。
//
// 干什么：调插件的 `/arm/set_enabled` 与 `/arm/emergency_stop`；订阅 `/arm/hardware_status`
// 拿到外部节点**读不到**的东西（逐关节 ERR / 温度 / 重力守卫 —— ros2_control 的状态接口
// 只有控制器能读，见 M-5）。
//
// **为什么是"请求 + 轮询"**：本节点的 tick 每 20 ms 推进一步，而服务响应由 executor 完成
// ⇒ 这里只 `async_send_request` 存 future，由 `poll()` 取结果（tick 里**不许阻塞等**，陷阱 #57）。
//
// ⚠️ **没有插件时的行为**（mock 链用的是 `mock_components/GenericSystem`，没有我们的服务）：
//    硬件步骤变成**显式跳过的 no-op**，并在 message 里说清楚（`skipped=true`，绝不假装"使能成功"）。
//    这样 `ArmOn`/`SafeDisarm` 的**控制器侧 + MotionClient 流程**仍然能在 mock 上跑通（M9.3 验收）；
//    硬件那一半在真机（M9.5）走正常路径。
#ifndef ARM_MANAGE__HARDWARE_GATE_HPP_
#define ARM_MANAGE__HARDWARE_GATE_HPP_

#include <arm_msgs/msg/hardware_status.hpp>
#include <arm_msgs/srv/hardware_emergency_stop.hpp>
#include <arm_msgs/srv/set_enabled.hpp>
#include <rclcpp/rclcpp.hpp>

#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace arm_manage
{

class HardwareGate
{
public:
  enum class Req {None, Enable, Disable, EStop};

  struct Result
  {
    bool ok = false;
    bool skipped = false;               // 无插件（mock）⇒ 显式跳过
    std::string message;
    std::vector<std::string> failed;    // 急停时"没确认失能"的关节
  };

  explicit HardwareGate(rclcpp::Node * node)
  : node_(node)
  {
    set_enabled_ = node_->create_client<arm_msgs::srv::SetEnabled>("/arm/set_enabled");
    estop_ = node_->create_client<arm_msgs::srv::HardwareEmergencyStop>("/arm/emergency_stop");
    // latched：新订阅者立刻拿到最后一份（所以不需要 GetStatus 服务，M-6）
    status_sub_ = node_->create_subscription<arm_msgs::msg::HardwareStatus>(
      "/arm/hardware_status", rclcpp::QoS(1).transient_local(),
      [this](arm_msgs::msg::HardwareStatus::ConstSharedPtr m) {
        status_ = *m;
        have_status_ = true;
      });
  }

  // 插件在不在？**每次都重查** —— 插件可能比本节点晚起来（launch 顺序不保证）。
  bool plugin_present() const
  {
    return set_enabled_->service_is_ready() && estop_->service_is_ready();
  }

  bool have_status() const {return have_status_;}
  const arm_msgs::msg::HardwareStatus & status() const {return status_;}
  // 有没有请求在处理中（上层用它决定"发新请求"还是"取结果"）
  bool pending() const {return req_ != Req::None;}

  // 硬件是否使能：**以 /arm/hardware_status 的逐关节 `armed` 为准**（那是唯一真值）。
  // 没有插件（mock）时退化为"按我们最近一次请求"（mock 硬件本来就没有使能概念）。
  bool armed() const
  {
    if (!plugin_present() || !have_status_ || status_.armed.empty()) {return assumed_armed_;}
    for (bool a : status_.armed) {
      if (!a) {return false;}
    }
    return true;
  }

  // 发一个请求。false ⇒ **立刻失败**（why 里写原因）；true ⇒ 已受理，去 poll()。
  bool request(Req r, std::string * why)
  {
    if (req_ != Req::None) {
      set_why(why, "硬件侧已有一个请求在处理中（等它完成）");
      return false;
    }
    if (r == Req::None) {
      set_why(why, "内部错误：空请求");
      return false;
    }

    // ── 没有插件（mock）：显式跳过，**下一次 poll() 立刻返回** ──
    if (!plugin_present()) {
      immediate_ = true;
      immediate_res_ = Result{};
      immediate_res_.ok = true;
      immediate_res_.skipped = true;
      immediate_res_.message =
        "⚠️ 没有插件运行期使能接口（mock 链？）—— 硬件步骤**已跳过**，不是「使能成功」";
      req_ = r;
      set_why(why, immediate_res_.message);
      return true;
    }

    req_ = r;
    if (r == Req::EStop) {
      estop_future_ = estop_->async_send_request(
        std::make_shared<arm_msgs::srv::HardwareEmergencyStop::Request>()).future.share();
    } else {
      auto req = std::make_shared<arm_msgs::srv::SetEnabled::Request>();
      req->enable = (r == Req::Enable);
      enable_future_ = set_enabled_->async_send_request(req).future.share();
    }
    set_why(why, "已受理");
    return true;
  }

  // 非阻塞轮询。返回 true ⇒ 这一次请求**完成了**（结果在 *out 里，含失败）。
  bool poll(Result * out)
  {
    if (req_ == Req::None) {return false;}

    if (immediate_) {
      const Req r = req_;
      immediate_ = false;
      req_ = Req::None;
      // mock：按请求更新"我们认为的"使能态（没有真值可读）
      if (r == Req::Enable) {assumed_armed_ = true;}
      if (r == Req::Disable || r == Req::EStop) {assumed_armed_ = false;}
      *out = immediate_res_;
      return true;
    }

    const auto zero = std::chrono::milliseconds(0);
    if (req_ == Req::EStop) {
      if (estop_future_.wait_for(zero) != std::future_status::ready) {return false;}
      auto res = estop_future_.get();
      Result r;
      r.ok = res->success;
      r.failed = res->failed;
      r.message = res->message;
      if (r.ok) {assumed_armed_ = false;}    // 确认失能才改我们的假设
      req_ = Req::None;
      *out = r;
      return true;
    }

    if (enable_future_.wait_for(zero) != std::future_status::ready) {return false;}
    auto res = enable_future_.get();
    Result r;
    r.ok = res->success;
    r.message = res->message;
    if (r.ok) {assumed_armed_ = res->now_armed;}
    req_ = Req::None;
    *out = r;
    return true;
  }

private:
  static void set_why(std::string * why, const std::string & s)
  {
    if (why != nullptr) {*why = s;}
  }

  rclcpp::Node * node_;
  rclcpp::Client<arm_msgs::srv::SetEnabled>::SharedPtr set_enabled_;
  rclcpp::Client<arm_msgs::srv::HardwareEmergencyStop>::SharedPtr estop_;
  rclcpp::Subscription<arm_msgs::msg::HardwareStatus>::SharedPtr status_sub_;

  Req req_ = Req::None;
  std::shared_future<arm_msgs::srv::SetEnabled::Response::SharedPtr> enable_future_;
  std::shared_future<arm_msgs::srv::HardwareEmergencyStop::Response::SharedPtr> estop_future_;
  bool immediate_ = false;                  // 无插件时"立刻完成"
  Result immediate_res_;

  arm_msgs::msg::HardwareStatus status_;
  bool have_status_ = false;
  bool assumed_armed_ = false;              // 只在"没有插件"时用（mock）
};

}  // namespace arm_manage

#endif  // ARM_MANAGE__HARDWARE_GATE_HPP_
