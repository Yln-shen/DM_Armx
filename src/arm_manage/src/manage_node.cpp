// manage_node.cpp —— 管理层节点（设计见 docs/MANAGE_ARCH.md）。
//
// 一句话：把"**控制器先 inactive → 硬件使能 → 校验 → 控制器 active**"这个顺序做对，
// 并把"收臂到折叠位**再**失能"编排成一次调用。对外只有 3 个服务 + 1 个状态话题。
//
// ⚠️ 与设计文档的一处**必然偏离**（§3.3 原写"服务回调只做常量时间的事、长流程交给 tick"）：
//    rclcpp 的**服务回调无法延迟回复**（回调返回时响应就发出去了），所以"先返回、稍后再答"
//    在 srv 上做不到。既然选了 A 方案（响应语义真实），回调就必须**等**状态机完成。
//    做法：main() 里用 **MultiThreadedExecutor**，且服务与 tick 分属**不同回调组**
//    ⇒ 某个回调在等的时候，tick 仍在别的线程推进。（否则就是陷阱 #57 的死锁形状：
//    回调等 tick、而 tick 进不来。）
//
// ⚠️ 另一处偏离：急停的"取消进行中的目标"**没有**调 `MotionClient::cancel()`
//    （那个类目前没有公开的 cancel，M9.0 定稿的 API 里没有）—— 而是用
//    "**立即失能 + 停控制器**"达到同样的物理效果：硬件一失能就没有力矩，
//    而 JTC 在控制器 deactivate 时会中止它的 goal。见 advance_estop()。
#include <arm_description/joint_limits.hpp>

#include <arm_manage/controller_gate.hpp>
#include <arm_manage/hardware_gate.hpp>
#include <arm_msgs/msg/arm_status.hpp>
#include <arm_msgs/srv/arm_on.hpp>
#include <arm_msgs/srv/manage_emergency_stop.hpp>
#include <arm_msgs/srv/safe_disarm.hpp>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/joint_state.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace arm_manage
{

class ManageNode : public rclcpp::Node
{
public:
  ManageNode()
  : Node("arm_manage")
  {
    // ── 参数（真源：config/arm_manage.yaml）──────────────────────────────
    joint_names_ = declare_parameter<std::vector<std::string>>("joint_names", std::vector<std::string>{});
    park_pose_ = declare_parameter<std::vector<double>>("park_pose", std::vector<double>{});
    controller_name_ = declare_parameter<std::string>("controller_name", "arm_controller");
    cm_ns_ = declare_parameter<std::string>("controller_manager_ns", "/controller_manager");
    vlim_ = declare_parameter<double>("vlim", 0.15);
    tol_rad_ = declare_parameter<double>("tol_rad", 0.05);
    timeout_sec_ = declare_parameter<double>("timeout_sec", 60.0);
    max_retries_ = declare_parameter<int>("max_retries", 2);
    status_hz_ = declare_parameter<double>("status_hz", 10.0);
    tick_ms_ = declare_parameter<int>("tick_ms", 20);

    config_ok_ = check_config();

    // 两个 Gate：控制器侧 / 硬件侧（本节点是它们唯一的调用者）
    ctl_ = std::make_unique<ControllerGate>(
      this, joint_names_, cm_ns_, controller_name_);
    hw_ = std::make_unique<HardwareGate>(this);

    // ── 回调组：服务与 tick **必须分开**，否则服务回调阻塞时 tick 进不来（陷阱 #57）
    srv_cbg_ = create_callback_group(rclcpp::CallbackGroupType::MutuallyExclusive);
    tick_cbg_ = create_callback_group(rclcpp::CallbackGroupType::MutuallyExclusive);

    arm_on_srv_ = create_service<arm_msgs::srv::ArmOn>(
      "/arm_manage/arm_on",
      [this](
        const std::shared_ptr<arm_msgs::srv::ArmOn::Request> req,
        std::shared_ptr<arm_msgs::srv::ArmOn::Response> res) {handle_arm_on(req, res);},
      rmw_qos_profile_services_default, srv_cbg_);
    disarm_srv_ = create_service<arm_msgs::srv::SafeDisarm>(
      "/arm_manage/safe_disarm",
      [this](
        const std::shared_ptr<arm_msgs::srv::SafeDisarm::Request> req,
        std::shared_ptr<arm_msgs::srv::SafeDisarm::Response> res) {handle_safe_disarm(req, res);},
      rmw_qos_profile_services_default, srv_cbg_);
    estop_srv_ = create_service<arm_msgs::srv::ManageEmergencyStop>(
      "/arm_manage/emergency_stop",
      [this](
        const std::shared_ptr<arm_msgs::srv::ManageEmergencyStop::Request>,
        std::shared_ptr<arm_msgs::srv::ManageEmergencyStop::Response> res) {handle_estop(res);},
      rmw_qos_profile_services_default, srv_cbg_);

    // 聚合状态：latched ⇒ 新订阅者立刻拿到最后一份（所以不需要 GetStatus 服务，M-6）
    status_pub_ = create_publisher<arm_msgs::msg::ArmStatus>(
      "/arm/status", rclcpp::QoS(1).transient_local());

    // 校验用的 /joint_states（QoS 必须与 jsb 一致：RELIABLE + TRANSIENT_LOCAL，陷阱 #58）
    js_sub_ = create_subscription<sensor_msgs::msg::JointState>(
      "/joint_states", rclcpp::QoS(1).reliable().transient_local(),
      [this](sensor_msgs::msg::JointState::ConstSharedPtr m) {
        js_names_.assign(m->name.begin(), m->name.end());
        js_pos_.assign(m->position.begin(), m->position.end());
      });

    timer_ = create_wall_timer(
      std::chrono::milliseconds(std::max(tick_ms_, 1)), [this] {tick();}, tick_cbg_);

    if (config_ok_) {
      RCLCPP_INFO(get_logger(),
        "管理层就绪：控制器 %s（CM %s）、折叠位 %zu 关节、vlim %.2f tol %.3f 超时 %.0fs 重试 %d",
        controller_name_.c_str(), cm_ns_.c_str(), park_pose_.size(), vlim_, tol_rad_, timeout_sec_,
        max_retries_);
    }
  }

private:
  // ── 状态机取值（/arm/status 的 state_machine，见 MANAGE_ARCH §4.3）──
  enum class State {ReadOnly, Arming, Armed, ArmedController, Disarming, Fault};
  // 当前在编排哪件事
  enum class Op {None, ArmOn, Disarm, EStop};

  // 各操作的步骤号（用常量而不是三个 enum：步骤少、注释里说清语义就够）
  static constexpr int kArmDeactivate = 1;
  static constexpr int kArmWaitInactive = 2;
  static constexpr int kArmEnableHw = 3;
  static constexpr int kArmVerify = 4;
  static constexpr int kArmActivate = 5;
  static constexpr int kArmWaitActive = 6;
  static constexpr int kArmDone = 9;

  static constexpr int kDisCheck = 1;
  static constexpr int kDisStartMotion = 2;
  static constexpr int kDisWaitMotion = 3;
  static constexpr int kDisDeactivate = 4;
  static constexpr int kDisWaitInactive = 5;
  static constexpr int kDisDisableHw = 6;
  static constexpr int kDisDone = 9;

  static constexpr int kEstopCancel = 1;
  static constexpr int kEstopHw = 2;
  static constexpr int kEstopDeactivate = 3;
  static constexpr int kEstopDone = 9;

  static constexpr int kWaitTicksMax = 100;        // 等一个状态变化最多 100 tick（20ms ⇒ 2s）
  static constexpr int kRefreshEveryTicks = 25;    // list_controllers 每 25 tick（0.5s）刷一次

  // ─────────────────────────── 配置校验 ───────────────────────────
  bool check_config()
  {
    bool ok = true;
    if (joint_names_.size() != arm_limits::kJointNames.size()) {
      RCLCPP_FATAL(get_logger(), "joint_names 有 %zu 个，期望 %zu 个",
        joint_names_.size(), arm_limits::kJointNames.size());
      ok = false;
    }
    if (park_pose_.size() != joint_names_.size()) {
      RCLCPP_FATAL(get_logger(), "park_pose 有 %zu 个数，期望 %zu 个（与 joint_names 对齐）",
        park_pose_.size(), joint_names_.size());
      ok = false;
      return false;                                  // 后面按索引访问，先退
    }
    for (std::size_t i = 0; i < joint_names_.size(); ++i) {
      const int idx = limit_index(joint_names_[i]);
      if (idx < 0) {
        RCLCPP_FATAL(get_logger(), "关节名 %s 不在限位表里（真源 joint.yaml + align.yaml）",
          joint_names_[i].c_str());
        ok = false;
        continue;
      }
      const auto & lim = arm_limits::kModelLimits[idx];
      const double q = park_pose_[i];
      if (!std::isfinite(q) || q < lim[0] || q > lim[1]) {
        // ⚠️ 陷阱 #59：JTC **不校验关节限位**，越界目标会顶在机械硬限位上堵转（真机烧过线圈）
        RCLCPP_FATAL(get_logger(),
          "park_pose[%zu]（%s）= %+.6f 超出模型坐标限位 [%+.6f, %+.6f] ⇒ 拒绝启动",
          i, joint_names_[i].c_str(), q, lim[0], lim[1]);
        ok = false;
      }
    }
    return ok;
  }

  static int limit_index(const std::string & name)
  {
    for (std::size_t i = 0; i < arm_limits::kJointNames.size(); ++i) {
      if (name == arm_limits::kJointNames[i]) {return static_cast<int>(i);}
    }
    return -1;
  }

  // ─────────────────────────── 服务回调 ───────────────────────────
  // 三个回调形状一样：① 起操作（忙/配置不合法 ⇒ 立刻答）② **等**状态机完成 ③ 回填结果。
  void handle_arm_on(
    const std::shared_ptr<arm_msgs::srv::ArmOn::Request> req,
    std::shared_ptr<arm_msgs::srv::ArmOn::Response> res)
  {
    std::string why;
    const bool with_ctl = req->with_controller;
    if (!begin_op(Op::ArmOn, with_ctl, &why)) {
      res->success = false;
      res->state = state_name();
      res->message = why;
      return;
    }
    // 等上限：调用方给了就用它，否则用"参数超时 + 10 s"（使能本身实测只要 ~70 ms）
    const double cap = (req->timeout_sec > 0.0) ? req->timeout_sec : (timeout_sec_ + 10.0);
    const bool waited = wait_op(cap);
    std::lock_guard<std::mutex> lk(mu_);
    res->success = success_;
    res->state = state_name();
    res->message = waited ? message_ : ("内部超时（" + std::to_string(cap) + " s）：" + message_);
  }

  void handle_safe_disarm(
    const std::shared_ptr<arm_msgs::srv::SafeDisarm::Request> req,
    std::shared_ptr<arm_msgs::srv::SafeDisarm::Response> res)
  {
    std::string why;
    // 本次调用的参数（0 ⇒ 用参数默认）
    disarm_vlim_ = (req->vlim > 0.0) ? req->vlim : vlim_;
    disarm_tol_ = (req->tol_rad > 0.0) ? req->tol_rad : tol_rad_;
    disarm_timeout_ = (req->timeout_sec > 0.0) ? req->timeout_sec : timeout_sec_;
    disarm_retries_ = (req->max_retries > 0) ? req->max_retries : max_retries_;
    if (!begin_op(Op::Disarm, false, &why)) {
      res->success = false;
      res->disarmed = !hw_->armed();
      res->max_residual = 0.0;
      res->attempts = 0;
      res->message = why;
      return;
    }
    // 收臂可能要跑满 timeout × (1+重试) ⇒ 等上限给足，再留 10 s 余量
    const double cap = disarm_timeout_ * static_cast<double>(disarm_retries_ + 1) + 10.0;
    const bool waited = wait_op(cap);
    std::lock_guard<std::mutex> lk(mu_);
    res->success = success_ && waited;
    res->disarmed = disarmed_flag_;
    res->max_residual = best_residual_;
    res->attempts = static_cast<uint8_t>(std::min(attempt_, 255));
    res->message = waited ? message_ : ("内部超时（" + std::to_string(cap) + " s）：" + message_);
  }

  void handle_estop(std::shared_ptr<arm_msgs::srv::ManageEmergencyStop::Response> res)
  {
    std::string why;
    if (!begin_op(Op::EStop, false, &why)) {
      // ⚠️ 急停是**最高优先级**：即使"忙"也不能只回一句错误就算了 —— 直接再试一次硬件急停。
      RCLCPP_WARN(get_logger(), "急停：已有操作在跑（%s）—— 仍然直接发硬件急停", why.c_str());
      HardwareGate::Result r;
      if (hw_->request(HardwareGate::Req::EStop, &why)) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (std::chrono::steady_clock::now() < deadline) {
          if (hw_->poll(&r)) {break;}
          std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
      }
      res->success = r.ok;
      res->disarmed = r.ok;
      res->failed = r.failed;
      res->message = "急停（绕过了编排）：" + (r.message.empty() ? why : r.message) +
        " 臂会塌回折叠位（陷阱 #39）";
      return;
    }
    const bool waited = wait_op(5.0);
    std::lock_guard<std::mutex> lk(mu_);
    res->success = success_ && waited;
    res->disarmed = disarmed_flag_;
    res->failed = failed_;
    res->message = waited ? message_ : ("内部超时：" + message_);
  }

  // ─────────────────────────── 操作生命周期 ───────────────────────────
  bool begin_op(Op op, bool with_controller, std::string * why)
  {
    auto set_why = [why](const std::string & s) {if (why != nullptr) {*why = s;}};
    if (!config_ok_) {set_why("配置不合法（看启动日志）：park_pose/joint_names 有问题"); return false;}
    {
      std::lock_guard<std::mutex> lk(mu_);
      if (op_ != Op::None) {set_why("已有一个操作在进行中，拒绝并发（等它完成）"); return false;}
      finished_ = false;
      success_ = false;
      disarmed_flag_ = false;
      failed_.clear();
      best_residual_ = 0.0;
      attempt_ = 0;
      wait_ticks_ = 0;
      message_.clear();
    }
    op_ = op;
    with_controller_ = with_controller;
    step_ = (op == Op::ArmOn) ? kArmDeactivate : ((op == Op::Disarm) ? kDisCheck : kEstopCancel);
    // 过渡态：arm_on ⇒ ARMING；收臂/急停 ⇒ DISARMING。
    // ⚠️ 急停的**进行中**也用 DISARMING —— FAULT 只留给**真的失败**（否则状态话题里会出现
    //    一次假的 FAULT，看起来像出事了）。
    state_ = (op == Op::ArmOn) ? State::Arming : State::Disarming;
    estop_skipped_ = false;
    RCLCPP_INFO(get_logger(), "开始操作：%s（with_controller=%d）",
      op_name(op).c_str(), static_cast<int>(with_controller));
    return true;
  }

  bool wait_op(double cap_sec)
  {
    std::unique_lock<std::mutex> lk(mu_);
    return cv_.wait_for(lk, std::chrono::duration<double>(cap_sec), [this] {return finished_;});
  }

  // tick 写完结果后调：把 finished_ 立起来并唤醒等在服务回调里的线程
  void finish_op(bool success, const std::string & message, bool disarmed)
  {
    {
      std::lock_guard<std::mutex> lk(mu_);
      success_ = success;
      message_ = message;
      disarmed_flag_ = disarmed;
      finished_ = true;
    }
    op_ = Op::None;
    step_ = 0;
    last_message_ = message;
    RCLCPP_WARN(get_logger(), "操作结束：%s", message.c_str());
    publish_status();
    cv_.notify_all();
  }

  static std::string op_name(Op op)
  {
    switch (op) {
      case Op::ArmOn: return "arm_on";
      case Op::Disarm: return "safe_disarm";
      case Op::EStop: return "emergency_stop";
      default: return "none";
    }
  }

  std::string state_name() const
  {
    switch (state_) {
      case State::ReadOnly: return "READONLY";
      case State::Arming: return "ARMING";
      case State::Armed: return "ARMED";
      case State::ArmedController: return "ARMED_CONTROLLER";
      case State::Disarming: return "DISARMING";
      case State::Fault: return "FAULT";
    }
    return "UNKNOWN";
  }

  // ─────────────────────────── tick：每步一个动作 ───────────────────────────
  void tick()
  {
    if (!config_ok_) {return;}
    ++tick_count_;
    ctl_->poll_refresh();
    if (tick_count_ % kRefreshEveryTicks == 0) {ctl_->request_refresh();}
    if (status_hz_ > 0.0 &&
      ++status_tick_ * (static_cast<double>(tick_ms_) / 1000.0) >= 1.0 / status_hz_)
    {
      status_tick_ = 0;
      publish_status();
    }
    if (op_ == Op::None) {return;}
    switch (op_) {
      case Op::ArmOn: advance_arm_on(); break;
      case Op::Disarm: advance_disarm(); break;
      case Op::EStop: advance_estop(); break;
      default: break;
    }
  }

  // ── ArmOn：停控制器 → 使能硬件 → 校验 → 起控制器（§6.1；顺序是 M-3）──
  void advance_arm_on()
  {
    std::string why;
    switch (step_) {
      case kArmDeactivate: {
        if (!with_controller_ || !ctl_->is_active()) {step_ = kArmEnableHw; return;}
        bool ok = false;
        std::string msg;
        if (ctl_->switch_pending()) {
          if (!ctl_->poll_switch(&ok, &msg)) {return;}
          if (!ok) {fail_arm_on("停 arm_controller 失败：" + msg); return;}
          wait_ticks_ = 0;
          step_ = kArmWaitInactive;
          return;
        }
        if (!ctl_->request_switch(false, &why)) {fail_arm_on(why); return;}
        return;
      }
      case kArmWaitInactive: {
        // 复核**真实**状态再往下：`switch_controller` 的 ok 只说明"受理了"
        if (!ctl_->is_active()) {step_ = kArmEnableHw; return;}
        if (++wait_ticks_ > kWaitTicksMax) {fail_arm_on("控制器没能停下来（list_controllers 仍是 active）"); return;}
        return;
      }
      case kArmEnableHw: {
        HardwareGate::Result r;
        if (hw_->pending()) {
          if (!hw_->poll(&r)) {return;}
          if (!r.ok) {fail_arm_on("硬件使能失败：" + r.message); return;}
          if (r.skipped) {RCLCPP_WARN(get_logger(), "%s", r.message.c_str());}
          step_ = kArmVerify;
          return;
        }
        if (!hw_->request(HardwareGate::Req::Enable, &why)) {fail_arm_on(why); return;}
        return;
      }
      case kArmVerify: {
        // ④ 校验：/joint_states 里有全部关节；有插件时 ERR 必须 ∈ {0,1}
        std::string bad;
        for (const auto & n : joint_names_) {
          if (std::find(js_names_.begin(), js_names_.end(), n) == js_names_.end()) {bad += " " + n;}
        }
        if (!bad.empty()) {
          rollback_disable("使能后 /joint_states 缺关节：" + bad + "（控制器没在发？）");
          return;
        }
        if (hw_->plugin_present() && hw_->have_status()) {
          std::string errs;
          for (std::size_t i = 0; i < hw_->status().err.size(); ++i) {
            const int e = hw_->status().err[i];
            if (e != 0 && e != 1) {
              errs += " " + (i < hw_->status().joint_names.size() ? hw_->status().joint_names[i] :
                std::string("?")) + "(ERR=" + std::to_string(e) + ")";
            }
          }
          if (!errs.empty()) {
            rollback_disable("使能后检测到故障：" + errs + "（ERR=13 锁存只能断电）");
            return;
          }
        }
        step_ = with_controller_ ? kArmActivate : kArmDone;
        return;
      }
      case kArmActivate: {
        bool ok = false;
        std::string msg;
        if (ctl_->switch_pending()) {
          if (!ctl_->poll_switch(&ok, &msg)) {return;}
          if (!ok) {
            // 异常矩阵 #12：控制器起不来 ⇒ **回滚到 ARMED**（硬件仍使能、停在原地），不扔掉臂
            state_ = State::Armed;
            finish_op(false, "硬件已使能，但 arm_controller 没起来：" + msg + "（停在原地，可重试 arm_on）", false);
            return;
          }
          wait_ticks_ = 0;
          step_ = kArmWaitActive;
          return;
        }
        if (!ctl_->request_switch(true, &why)) {
          state_ = State::Armed;
          finish_op(false, "硬件已使能，但发起 activate 失败：" + why, false);
          return;
        }
        return;
      }
      case kArmWaitActive: {
        if (ctl_->is_active()) {step_ = kArmDone; return;}
        if (++wait_ticks_ > kWaitTicksMax) {
          state_ = State::Armed;
          finish_op(false, "硬件已使能，但 arm_controller 一直没 active（list_controllers 没变）", false);
          return;
        }
        return;
      }
      case kArmDone: {
        state_ = with_controller_ ? State::ArmedController : State::Armed;
        finish_op(true,
          with_controller_ ? "已使能 + arm_controller 已 active（可发轨迹）" :
          "已使能（控制器未激活；要发轨迹再 arm_on with_controller=true）", false);
        return;
      }
      default: finish_op(false, "内部错误：未知步骤 " + std::to_string(step_), false); return;
    }
  }

  void fail_arm_on(const std::string & why)
  {
    state_ = State::Fault;
    finish_op(false, "arm_on 失败：" + why, false);
  }

  // 校验失败 ⇒ 尽力把硬件失能（别把臂留在"使能但没人管"的状态）
  void rollback_disable(const std::string & why)
  {
    HardwareGate::Result r;
    if (!hw_->request(HardwareGate::Req::Disable, nullptr)) {
      state_ = State::Fault;
      finish_op(false, why + "；且回滚失能请求发不出去", false);
      return;
    }
    // 等到结果（最多 3 s；这是个收尾动作，值得等）
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline) {
      if (hw_->poll(&r)) {break;}
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    state_ = State::Fault;
    finish_op(false, why + "；已尽力回滚失能（" + r.message + "）", false);
  }

  // ── SafeDisarm：收臂到折叠位 → 停控制器 → 失能（§6.2）──
  void advance_disarm()
  {
    std::string why;
    switch (step_) {
      case kDisCheck: {
        if (!hw_->armed()) {
          // ① 本来就没使能 ⇒ 直接成功（无害），但把话说清楚
          state_ = State::ReadOnly;
          finish_op(true, "本来就没使能，未做任何动作（幂等）", true);
          return;
        }
        if (in_park()) {
          state_ = State::Disarming;
          best_residual_ = max_residual_now();
          RCLCPP_INFO(get_logger(), "已经在折叠位（最大偏差 %.4f rad ≤ tol %.4f）⇒ 跳过运动，直接失能",
            best_residual_, disarm_tol_);
          step_ = kDisDeactivate;
          return;
        }
        step_ = kDisStartMotion;
        return;
      }
      case kDisStartMotion: {
        auto & m = ctl_->motion();
        m.clear_events();
        m.set_vlim(disarm_vlim_);
        m.set_tolerance(disarm_tol_);
        m.set_timeout(disarm_timeout_);
        // `attempts` = **一共发起了几次收臂**（首试成功就是 1，不是 0）。
        // ⚠️ 必须在 start() **之前**加：start 自己失败也算一次尝试，否则会无限重试。
        ++attempt_;
        if (!m.start(park_pose_, &why)) {motion_attempt_failed(why); return;}
        RCLCPP_INFO(get_logger(),
          "开始收臂（第 %d 次尝试）：最大行程 %.4f rad、vlim %.2f ⇒ 预计 %.1f s",
          attempt_, m.travel(), disarm_vlim_, m.planned_duration());
        step_ = kDisWaitMotion;
        return;
      }
      case kDisWaitMotion: {
        auto & m = ctl_->motion();
        const bool done = m.tick();          // **非阻塞**：内部等 future 用 wait_for(0)
        drain_motion_events();
        if (!done) {return;}
        best_residual_ = std::max(best_residual_, m.max_residual());
        if (m.ok() && m.max_residual() <= disarm_tol_) {
          step_ = kDisDeactivate;
          return;
        }
        // 没到位/被取消/超时 ⇒ 记下原因（MotionClient 自己不打日志，事件由我们打）
        std::string why2 = std::string("收臂没到位（阶段 ") + m.phase_name() +
          "，最大残差 " + std::to_string(m.max_residual()) + " rad";
        if (m.goal_error_code() != 0 || !m.goal_error_string().empty()) {
          why2 += "，JTC error_code " + std::to_string(m.goal_error_code()) + " " + m.goal_error_string();
        }
        why2 += m.cancel_confirmed() ? "，取消已确认" : "";
        why2 += "）";
        motion_attempt_failed(why2);
        return;
      }
      case kDisDeactivate: {
        if (!ctl_->is_active()) {step_ = kDisDisableHw; return;}
        bool ok = false;
        std::string msg;
        if (ctl_->switch_pending()) {
          if (!ctl_->poll_switch(&ok, &msg)) {return;}
          wait_ticks_ = 0;
          step_ = kDisWaitInactive;
          return;
        }
        if (!ctl_->request_switch(false, &why)) {
          // 停不下来也必须继续去失能（安全侧优先）
          RCLCPP_WARN(get_logger(), "停 arm_controller 失败（%s）—— 仍然继续失能", why.c_str());
          step_ = kDisDisableHw;
          return;
        }
        return;
      }
      case kDisWaitInactive: {
        if (!ctl_->is_active()) {step_ = kDisDisableHw; return;}
        if (++wait_ticks_ > kWaitTicksMax) {
          RCLCPP_WARN(get_logger(), "控制器还没停（%.1f s）—— 仍然继续失能",
            kWaitTicksMax * tick_ms_ / 1000.0);
          step_ = kDisDisableHw;
          return;
        }
        return;
      }
      case kDisDisableHw: {
        HardwareGate::Result r;
        if (hw_->pending()) {
          if (!hw_->poll(&r)) {return;}
          if (!r.ok) {
            state_ = State::Fault;
            finish_op(false,
              "失能没成功：" + r.message + " ⇒ **臂仍然使能**、停在当前姿态，请看 /arm/hardware_status", false);
            return;
          }
          state_ = State::ReadOnly;
          finish_op(true, "已收臂到折叠位并失能（最大残差 " + std::to_string(best_residual_) +
            " rad）" + (r.skipped ? "；" + r.message : ""), true);
          return;
        }
        if (!hw_->request(HardwareGate::Req::Disable, &why)) {
          state_ = State::Fault;
          finish_op(false, "失能请求发不出去：" + why + " ⇒ **臂仍然使能**", false);
          return;
        }
        return;
      }
      default: finish_op(false, "内部错误：未知步骤 " + std::to_string(step_), false); return;
    }
  }

  // 收臂失败 ⇒ 重试；用尽 ⇒ **保持使能、停在原地**（M-8：绝不把臂扔掉）
  void motion_attempt_failed(const std::string & why)
  {
    // `attempt_` 在 kDisStartMotion 里已经加过了（没加上的那种"start 就失败"也算一次尝试）
    if (attempt_ <= disarm_retries_) {
      RCLCPP_WARN(get_logger(), "收臂第 %d 次失败：%s —— 重试（还剩 %d 次）",
        attempt_, why.c_str(), disarm_retries_ - attempt_ + 1);
      step_ = kDisStartMotion;
      return;
    }
    state_ = State::Fault;
    finish_op(false,
      "收臂失败（已试 " + std::to_string(attempt_) + " 次）：" + why +
      " ⇒ **臂仍然使能**、停在当前姿态（没扔下它）；可再调 safe_disarm，或直接 emergency_stop",
      false);
  }

  // ── EmergencyStop：立即失能 → 停控制器（§6.3）──
  void advance_estop()
  {
    std::string why;
    switch (step_) {
      case kEstopCancel: {
        // ① "取消进行中的目标"：**没有调 MotionClient::cancel()**（那个 API 不存在，见文件头），
        //    改用"立即失能 + 停控制器"：硬件一失能就没有力矩，控制器 deactivate 会中止 JTC 的 goal。
        //    ⚠️ 这里**故意不等**任何东西 —— 急停要快。
        if (ctl_->motion().running()) {
          RCLCPP_WARN(get_logger(),
            "急停：有一个轨迹目标在跑（阶段 %s）—— 不单等它取消，直接走失能（力矩先断）",
            ctl_->motion().phase_name());
        }
        step_ = kEstopHw;
        return;
      }
      case kEstopHw: {
        HardwareGate::Result r;
        if (hw_->pending()) {
          if (!hw_->poll(&r)) {return;}
          failed_ = r.failed;
          estop_skipped_ = r.skipped;
          estop_hw_msg_ = r.message;
          step_ = kEstopDeactivate;
          return;
        }
        if (!hw_->request(HardwareGate::Req::EStop, &why)) {
          estop_hw_msg_ = why;
          step_ = kEstopDeactivate;
          return;
        }
        return;
      }
      case kEstopDeactivate: {
        // ③ 停控制器（best effort）：不为了它拖慢急停，一个 tick 内发出去就走
        bool ok = false;
        std::string msg;
        if (ctl_->switch_pending()) {ctl_->poll_switch(&ok, &msg); step_ = kEstopDone; return;}
        if (!ctl_->request_switch(false, &why)) {step_ = kEstopDone; return;}
        return;
      }
      case kEstopDone: {
        const bool disarmed = failed_.empty();
        state_ = disarmed ? State::ReadOnly : State::Fault;
        std::string m = "急停：已立即失能（不等收臂）。**臂会塌回折叠位**（陷阱 #39，这是设计行为）。";
        if (!estop_hw_msg_.empty()) {m += " 硬件侧：" + estop_hw_msg_; }
        if (!disarmed) {
          m += " ⚠️ 有 " + std::to_string(failed_.size()) + " 台**没确认失能**：";
          for (const auto & n : failed_) {m += " " + n;}
          m += " —— ERR=13(通讯丢失) 是锁存的，只能**断电重上电**清（本层不做恢复）。";
        }
        if (estop_skipped_) {m += " （无插件：硬件步骤被跳过）";}
        finish_op(disarmed, m, disarmed);
        return;
      }
      default: finish_op(false, "内部错误：未知步骤 " + std::to_string(step_), false); return;
    }
  }

  // ─────────────────────────── 小工具 ───────────────────────────
  // MotionClient **自己不打日志**（只产出事件队列）⇒ 由我们逐条打出来
  void drain_motion_events()
  {
    for (const auto & e : ctl_->motion().events()) {
      if (e.level == arm_motion::MotionEvent::kError) {
        RCLCPP_ERROR(get_logger(), "%s", e.text.c_str());
      } else if (e.level == arm_motion::MotionEvent::kWarn) {
        RCLCPP_WARN(get_logger(), "%s", e.text.c_str());
      } else {
        RCLCPP_INFO(get_logger(), "%s", e.text.c_str());
      }
    }
    ctl_->motion().clear_events();
  }

  // 当前各关节位置（模型坐标，按 joint_names_ 顺序）；缺任何一个 ⇒ false
  bool current_positions(std::vector<double> * out) const
  {
    out->assign(joint_names_.size(), 0.0);
    for (std::size_t i = 0; i < joint_names_.size(); ++i) {
      const auto it = std::find(js_names_.begin(), js_names_.end(), joint_names_[i]);
      if (it == js_names_.end()) {return false;}
      const std::size_t k = static_cast<std::size_t>(it - js_names_.begin());
      if (k >= js_pos_.size()) {return false;}
      (*out)[i] = js_pos_[k];
    }
    return true;
  }

  double max_residual_now() const
  {
    std::vector<double> q;
    if (!current_positions(&q)) {return std::nan("");}
    double worst = 0.0;
    for (std::size_t i = 0; i < q.size() && i < park_pose_.size(); ++i) {
      worst = std::max(worst, std::fabs(q[i] - park_pose_[i]));
    }
    return worst;
  }

  bool in_park() const
  {
    const double r = max_residual_now();
    return std::isfinite(r) && r <= disarm_tol_;
  }

  void publish_status()
  {
    if (status_pub_ == nullptr) {return;}
    arm_msgs::msg::ArmStatus m;
    m.stamp = now();
    m.state_machine = state_name();
    m.hardware_armed = hw_->armed();
    m.controller_active = ctl_->is_active();
    m.joint_names = joint_names_;
    if (hw_->have_status()) {
      m.hardware = hw_->status();
      m.mode = hw_->status().mode;
      for (std::size_t i = 0; i < hw_->status().err.size(); ++i) {
        m.per_joint_status_code.push_back(hw_->status().err[i]);
        const int e = hw_->status().err[i];
        if (e != 0 && e != 1) {
          m.error_codes.push_back(
            (i < hw_->status().joint_names.size() ? hw_->status().joint_names[i] : std::string("?")) +
            ":ERR=" + std::to_string(e));
        }
      }
    } else {
      // 没有插件（mock 链）：**如实说不知道**，别编一个 0
      m.mode = "unknown(没有 /arm/hardware_status：mock 链？)";
      for (std::size_t i = 0; i < joint_names_.size(); ++i) {m.per_joint_status_code.push_back(-1);}
    }
    m.message = last_message_;
    status_pub_->publish(m);
  }

  // ── 参数 ──
  std::vector<std::string> joint_names_;
  std::vector<double> park_pose_;
  std::string controller_name_;
  std::string cm_ns_;
  double vlim_ = 0.15;
  double tol_rad_ = 0.05;
  double timeout_sec_ = 60.0;
  int max_retries_ = 2;
  double status_hz_ = 10.0;
  int tick_ms_ = 20;
  bool config_ok_ = false;

  // ── 两个 Gate ──
  std::unique_ptr<ControllerGate> ctl_;
  std::unique_ptr<HardwareGate> hw_;

  // ── ROS 接口 ──
  rclcpp::CallbackGroup::SharedPtr srv_cbg_;
  rclcpp::CallbackGroup::SharedPtr tick_cbg_;
  rclcpp::Service<arm_msgs::srv::ArmOn>::SharedPtr arm_on_srv_;
  rclcpp::Service<arm_msgs::srv::SafeDisarm>::SharedPtr disarm_srv_;
  rclcpp::Service<arm_msgs::srv::ManageEmergencyStop>::SharedPtr estop_srv_;
  rclcpp::Publisher<arm_msgs::msg::ArmStatus>::SharedPtr status_pub_;
  rclcpp::Subscription<sensor_msgs::msg::JointState>::SharedPtr js_sub_;
  rclcpp::TimerBase::SharedPtr timer_;

  // ── 操作状态（tick 写、服务回调读 ⇒ 用 mu_/cv_ 保护）──
  Op op_ = Op::None;
  int step_ = 0;
  bool with_controller_ = false;
  int attempt_ = 0;
  int wait_ticks_ = 0;
  double best_residual_ = 0.0;
  double disarm_vlim_ = 0.15;
  double disarm_tol_ = 0.05;
  double disarm_timeout_ = 60.0;
  int disarm_retries_ = 2;
  bool estop_skipped_ = false;
  std::string estop_hw_msg_;

  std::mutex mu_;
  std::condition_variable cv_;
  bool finished_ = false;
  bool success_ = false;
  bool disarmed_flag_ = false;
  std::string message_;
  std::vector<std::string> failed_;

  // ── 状态与缓存 ──
  State state_ = State::ReadOnly;
  std::string last_message_ = "刚启动，未做任何动作";
  std::vector<std::string> js_names_;
  std::vector<double> js_pos_;
  int tick_count_ = 0;
  int status_tick_ = 0;
};

}  // namespace arm_manage

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  auto node = std::make_shared<arm_manage::ManageNode>();
  // ⚠️ **必须多线程**：服务回调要阻塞等状态机完成，而 tick 得在别的线程继续跑。
  //    配套前提：服务与 tick 分属不同回调组（见构造里的 srv_cbg_/tick_cbg_）。
  rclcpp::executors::MultiThreadedExecutor exec;
  exec.add_node(node);
  exec.spin();
  rclcpp::shutdown();
  return 0;
}
