// dm_system_interface.cpp —— SystemInterface 插件实现（真机通路）。
//
// **职责边界**（LAYERS §2/§5）：只做串口与帧 I/O、帧装配、使能/失能、物理钳位、ERR 上报。
// **只有一种控制模式：POS_VEL**（`0x0A = 2`）；MIT 已砍（2026-10-14，见头文件里的理由）。
//
// 结构：`on_init` 解析参数 → `on_configure` 建串口/总线/关节 → `on_activate` 走使能序列
// （写 0x0A=2）+ 使能 + 锁定保持目标 → 之后每周期 `read()` / `write()`。
// 本文件的顺序：参数解析 → 骨架 → 使能序列 → 坐标换算 → 生命周期 → 接口导出
//                → 周期读写 → 运行期使能状态机 → 状态发布。
#include "motor_driver_hardware/dm_system_interface.hpp"

#include <hardware_interface/types/hardware_interface_type_values.hpp>

#include <glob.h>

#include <algorithm>   // std::fill, std::max, std::min
#include <chrono>      // 时间间隔
#include <cmath>       // std::fabs, std::isfinite, std::isnan
#include <limits>      // std::numeric_limits
#include <stdexcept>   // 异常
#include <thread>      // std::this_thread::sleep_for
#include <unordered_map> // 参数表

namespace motor_driver_hardware
{

namespace
{
using Params = std::unordered_map<std::string, std::string>;
using hardware_interface::CallbackReturn;
using hardware_interface::return_type;

// ── 寄存器 ──
constexpr uint8_t kRegCtrlMode = 0x0A;   // CTRL_MODE（RAM；uint32）
constexpr uint8_t kRegKpAsr = 0x19;  // 速度环 Kp
constexpr uint8_t kRegKiAsr = 0x1A;  // 速度环 Ki
constexpr uint8_t kRegKpApr = 0x1B;  // 位置环 Kp
constexpr uint8_t kRegKiApr = 0x1C;  // 位置环 Ki

// ── 状态话题的发布周期（kStatusEvery 圈发一次；100 Hz 控制周期 ⇒ 10 Hz）──
constexpr int kStatusEvery = 10;

// 判断参数是否存在。
bool has(const Params & m, const std::string & key) {return m.count(key) != 0;}

// 取字符串参数，缺省返回 def。
std::string as_str(const Params & m, const std::string & key, const std::string & def)
{
  auto it = m.find(key);
  return it == m.end() ? def : it->second;
}

// 取 double。用 used 检查是否整个字符串都被解析（防止 "1.2abc" 被当作 1.2）。
double as_double(const Params & m, const std::string & key, double def)
{
  auto it = m.find(key);
  if (it == m.end()) {return def;}
  std::size_t used = 0;
  const double v = std::stod(it->second, &used);
  if (used != it->second.size()) {
    throw std::invalid_argument("参数 " + key + " 不是数字：" + it->second);
  }
  return v;
}

// 取 int（复用 double 解析）
int as_int(const Params & m, const std::string & key, int def)
{
  return static_cast<int>(as_double(m, key, static_cast<double>(def)));
}

// 严格的布尔解析，不接受其它写法。
bool as_bool(const Params & m, const std::string & key, bool def)
{
  auto it = m.find(key);
  if (it == m.end()) {return def;}
  const std::string v = it->second;
  if (v == "true" || v == "True" || v == "1") {return true;}
  if (v == "false" || v == "False" || v == "0") {return false;}
  throw std::invalid_argument("参数 " + key + " 不是布尔：" + v);
}

// 打不开串口时把**当前存在的** ttyACM 设备列出来 —— 设备号会随插拔顺序变（陷阱 #28），
// 与其让用户去猜，不如直接把可选项打进日志。
std::string list_tty_acm_devices()
{
  std::string out;
  glob_t g{};
  if (glob("/dev/ttyACM*", 0, nullptr, &g) == 0) {
    for (std::size_t i = 0; i < g.gl_pathc; ++i) {
      if (i > 0) {out += ", ";}
      out += g.gl_pathv[i];
    }
  }
  globfree(&g);
  return out.empty() ? "（一个都没有：适配器没插好或驱动没加载）" : out;
}

double nan_value() {return std::numeric_limits<double>::quiet_NaN();}

}  // namespace

// 析构函数
// 析构时释放串口/总线/关节对象。
DmSystemInterface::~DmSystemInterface()
{
  teardown();
}

// ══════════════════════════ 参数 ══════════════════════════

// parse_one_joint：解析单个关节
bool DmSystemInterface::parse_one_joint(const hardware_interface::ComponentInfo & j)
{
  // motor_id 必填
  const Params & p = j.parameters;
  if (!has(p, "motor_id")) {
    throw std::invalid_argument("关节 " + j.name + " 缺 <param name=\"motor_id\">");
  }
  // 电机 ID 范围 1~6（DM 总线协议限制）。
  JointParams jp;
  jp.name = j.name;
  jp.cfg.name = j.name;
  jp.cfg.motor_id = static_cast<uint8_t>(as_int(p, "motor_id", 0));
  if (jp.cfg.motor_id < 1 || jp.cfg.motor_id > 6) {
    throw std::invalid_argument("关节 " + j.name + " 的 motor_id 必须在 1~6");
  }

  jp.cfg.direction = as_int(p, "direction", 1);        // 电机安装方向
  jp.cfg.offset = as_double(p, "offset", 0.0);         // 零点偏移
  jp.cfg.motor_type = as_str(p, "motor_type", "");     // 型号（用于限幅查表）
  jp.cfg.limit = Limit{as_double(p, "p_max", 12.5),
                       as_double(p, "v_max", 10.0),
                       as_double(p, "t_max", 28.0)};   // 位置/速度/力矩上限（默认 DM 电机典型值）

  // 可选软限位。
  if (has(p, "position_min")) {jp.cfg.position_min = as_double(p, "position_min", 0.0);}
  if (has(p, "position_max")) {jp.cfg.position_max = as_double(p, "position_max", 0.0);}

  // sign 是矢量符号（位置/速度/力矩统一乘它），只允许 ±1。
  jp.sign = as_int(p, "sign", 1);
  if (jp.sign != 1 && jp.sign != -1) {
    throw std::invalid_argument("关节 " + j.name + " 的 sign 只能是 ±1");
  }
  // 标量零点偏移（只作用于位置，不作用于矢量）。
  jp.zero_shift = as_double(p, "zero_shift", 0.0);

  // PID 四个都给了才写（不给 = 保留电机 RAM 里的当前值），避免只改部分导致混乱
  if (has(p, "kp_asr") && has(p, "ki_asr") && has(p, "kp_apr") && has(p, "ki_apr")) {
    jp.pid = std::array<double, 4>{as_double(p, "kp_asr", 0.0), as_double(p, "ki_asr", 0.0),
      as_double(p, "kp_apr", 0.0), as_double(p, "ki_apr", 0.0)};
  }
  joints_.push_back(std::move(jp));
  return true;
}

// log_params：打印参数摘要
void DmSystemInterface::log_params() const
{
  std::string summary;
  for (const auto & jp : joints_) {
    summary += " " + jp.name + "(id" + std::to_string(jp.cfg.motor_id) + ",dir" +
      std::to_string(jp.cfg.direction) + ",off" + std::to_string(jp.cfg.offset) + ",s" +
      std::to_string(jp.sign) + ",δ" + std::to_string(jp.zero_shift) +
      (jp.pid.has_value() ? ",pid" : "") + ")";
  }
  RCLCPP_INFO(get_logger(),
    "参数就绪：%zu 关节%s\n  device=%s baud=%d vlim=%.3f rad/s enable_on_activate=%s"
    "\n  控制模式固定 POS_VEL（写 0x0A=%u）；`effort` 命令接口是**空插槽**（本层不读）",
    joints_.size(), summary.c_str(), device_.c_str(), baud_, vlim_,
    enable_on_activate_ ? "true（会动电机）" : "false（只读）", kCtrlModePosVel);
}

// parse_params：解析硬件级参数
bool DmSystemInterface::parse_params(const hardware_interface::HardwareInfo & info)
{
  try {
    // 读取硬件级参数。安全默认：只读、不使能。
    const Params & hp = info.hardware_parameters;
    device_ = as_str(hp, "device", "/dev/ttyACM0");   // 兜底值；真源是 joint.yaml 的 channel（xacro 注入）
    baud_ = as_int(hp, "baud", 921600);
    // ⚠️ 默认 **false = 只读**：不使能、不发控制帧。要动电机必须显式打开。
    enable_on_activate_ = as_bool(hp, "enable_on_activate", false);
    vlim_ = as_double(hp, "vlim", 1.0);
    fail_streak_limit_ = as_int(hp, "fail_streak_limit", 10);

    // 参数合法性校验。
    if (!(vlim_ > 0.0)) {throw std::invalid_argument("vlim 必须是正数（rad/s）");}
    if (fail_streak_limit_ <= 0) {throw std::invalid_argument("fail_streak_limit 必须是正整数");}

    // 初始化运行时数组和硬件接口数组。用 NaN 而非 0：表示"还没有数据"，避免谎报。
    joints_.clear();
    for (const auto & j : info.joints) {parse_one_joint(j);}
    if (joints_.empty()) {throw std::invalid_argument("URDF 里一个关节都没有");}
    rt_.assign(joints_.size(), JointRuntime{});
    const double nan = nan_value();
    hw_positions_.assign(joints_.size(), nan);      // 还没读到就保持 NaN，别谎报 0
    hw_velocities_.assign(joints_.size(), nan);
    hw_efforts_.assign(joints_.size(), nan);
    hw_commands_.assign(joints_.size(), nan);       // NaN ⇒ write() 走"保持"
    // 空插槽：永远是 NaN（本层没有力矩路径）。这样 `pick_mode` 那类判据将来若要回来，
    // 语义仍是"没人声索 effort"。
    hw_effort_commands_.assign(joints_.size(), nan);

    log_params();
    return true;
  } catch (const std::exception & e) {
    RCLCPP_FATAL(get_logger(), "参数有问题：%s", e.what());
    return false;
  }
}

// ══════════════════════════ 骨架 ══════════════════════════
// 支持"注入 IO"（测试用假串口），否则自建 SerialPort。然后建总线、每关节一个 Joint 对象。
void DmSystemInterface::build_hardware()
{
  if (injected_io_ == nullptr) {owned_io_ = std::make_unique<SerialPort>(device_, baud_);}
  io_ = (injected_io_ != nullptr) ? injected_io_ : owned_io_.get();
  bus_ = std::make_unique<MotorBus>(*io_);
  for (auto & jp : joints_) {jp.joint = std::make_unique<Joint>(*bus_, jp.cfg);}
}

//逆序释放。
void DmSystemInterface::teardown()
{
  for (auto & jp : joints_) {jp.joint.reset();}
  bus_.reset();
  owned_io_.reset();
  io_ = nullptr;
}

// 尽力失能所有电机，不抛异常（收尾路径）
void DmSystemInterface::disable_all_quietly()
{
  if (bus_ == nullptr || io_ == nullptr || !io_->is_open()) {return;}
  for (auto & jp : joints_) {
    if (jp.joint == nullptr) {continue;}
    try {
      jp.joint->disable();
    } catch (...) {
      // 收尾路径不许抛
    }
  }
}

// ══════════════════════════ 使能 ══════════════════════════

// ① 单台写控制模式寄存器 `0x0A`（RAM ⇒ 每次激活/运行期使能都要写），可选写 PID。
//    **一次调用只动一台**：运行期状态机一个周期只能做这一件事（~12 ms 静默），
//    所以它必须能"逐台"调用；on_activate 则在一个循环里连调 6 次。
//    模式**固定 POS_VEL**（只有一个取值，没有运行期切换）。
void DmSystemInterface::write_mode_one(std::size_t i)
{
  const JointParams & jp = joints_[i];
  if (!bus_->write_register(jp.cfg.motor_id, kRegCtrlMode, uint32_to_uint8s(kCtrlModePosVel))) {
    throw std::runtime_error(
      "给电机 " + std::to_string(jp.cfg.motor_id) + " 写 0x0A=" +
      std::to_string(kCtrlModePosVel) + " 没收到回包");
  }
  if (jp.pid.has_value()) {
    const auto & pid = *jp.pid;
    const std::array<std::pair<uint8_t, double>, 4> regs = {{
      {kRegKpAsr, pid[0]}, {kRegKiAsr, pid[1]}, {kRegKpApr, pid[2]}, {kRegKiApr, pid[3]}}};
    for (const auto & kv : regs) {
      if (!bus_->write_register(jp.cfg.motor_id, kv.first, float32_to_uint8s(kv.second))) {
        throw std::runtime_error(
          "给电机 " + std::to_string(jp.cfg.motor_id) + " 写 PID 寄存器 0x" +
          std::to_string(kv.first) + " 没收到回包");
      }
    }
  }
}

// ② 全部使能 + 锁保持目标 + 立刻补一帧真实保持帧。
//    **on_activate 与运行期状态机共用这一份** —— 两处必须一模一样，否则"激活时"和"运行期"
//    的使能语义会悄悄漂开（那正是最难查的一类 bug）。
void DmSystemInterface::enable_all_and_latch()
{
  // 使能所有电机（`Joint::enable()` 内部会先查缓存位置，再补保持帧）。
  for (auto & jp : joints_) {jp.joint->enable();}

  // 锁定保持目标 = 刚使能时的**电机侧**实测位置，之后 write() 一直发它。
  // 为什么不每圈重读："读哪停哪"没有回复力，重力能把关节慢慢压走（陷阱 #38）。
  // ⚠️ POS_VEL 的保持帧要的就是电机侧值（不换算、不钳位）⇒ 只存一个。
  for (std::size_t i = 0; i < joints_.size(); ++i) {
    const auto st = bus_->get_state(joints_[i].cfg.motor_id);
    if (!st.has_value()) {
      throw std::runtime_error(
        "关节 " + joints_[i].name + " 使能后仍没有实测位置，锁不住保持目标");
    }
    rt_[i].hold_pos = st->pos;
  }
  // 校验并补一帧统一的保持帧。
  // ⚠️ 为什么必须补：`enable()` 的保持帧用的是**那一刻**的实测值，与本轮锁定的 `hold_pos`
  //    可能有一两帧的差；补一帧统一的，后面 `write()` 的路径就与它完全一致。
  //    MIT 时代这里还要补"真实 MIT 帧"关闭零增益窗口（陷阱 #41）—— POS_VEL 不需要。
  for (std::size_t i = 0; i < joints_.size(); ++i) {
    if (!rt_[i].hold_pos.has_value()) {
      throw std::runtime_error("关节 " + joints_[i].name + " 没有锁定的保持目标");
    }
    bus_->send_pos_vel(joints_[i].cfg.motor_id, *rt_[i].hold_pos, kHoldVlim);
  }
}

// ══════════════════════════ 坐标换算 ══════════════════════════

// 坐标换算包装（调用头文件里的自由函数）。
double DmSystemInterface::model_to_ours(std::size_t i, double q_urdf) const
{
  const JointParams & jp = joints_[i];
  return motor_driver_hardware::model_to_ours(q_urdf, jp.sign, jp.zero_shift);
}

double DmSystemInterface::ours_to_model(std::size_t i, double q_ours) const
{
  const JointParams & jp = joints_[i];
  return motor_driver_hardware::ours_to_model(q_ours, jp.sign, jp.zero_shift);
}

// ══════════════════════════ 生命周期 ══════════════════════════
// 先调基类 on_init，再解析参数。
CallbackReturn DmSystemInterface::on_init(
  const hardware_interface::HardwareComponentInterfaceParams & params)
{
  if (hardware_interface::SystemInterface::on_init(params) != CallbackReturn::SUCCESS) {
    return CallbackReturn::ERROR;
  }
  if (!parse_params(params.hardware_info)) {return CallbackReturn::ERROR;}
  return CallbackReturn::SUCCESS;
}

// 构建硬件对象（此时还没开串口）。
CallbackReturn DmSystemInterface::on_configure(const rclcpp_lifecycle::State &)
{
  try {
    build_hardware();
  } catch (const std::exception & e) {
    RCLCPP_FATAL(get_logger(), "建立串口/总线/关节对象失败：%s", e.what());
    teardown();
    return CallbackReturn::ERROR;
  }

  // ── ROS 侧接口：运行期使能（自己提供） + 状态话题（只能由插件发）────────────────
  // ⚠️ 单测里**没有节点**（get_node() 为空）⇒ 不建服务/话题；逻辑照样能测 ——
  //    直接调 request_enable() / advance_enable_fsm() / publish_status()（见 test_*.cpp）。
  auto node = get_node();
  if (node != nullptr) {
    set_enabled_srv_ = node->create_service<arm_msgs::srv::SetEnabled>(
      "/arm/set_enabled",
      [this](
        const std::shared_ptr<arm_msgs::srv::SetEnabled::Request> req,
        std::shared_ptr<arm_msgs::srv::SetEnabled::Response> res) {
        handle_set_enabled(req, res);
      });
    estop_srv_ = node->create_service<arm_msgs::srv::HardwareEmergencyStop>(
      "/arm/emergency_stop",
      [this](
        const std::shared_ptr<arm_msgs::srv::HardwareEmergencyStop::Request>,
        std::shared_ptr<arm_msgs::srv::HardwareEmergencyStop::Response> res) {
        handle_emergency_stop(res);
      });
    // latched（TRANSIENT_LOCAL）：新订阅者**立刻**拿到最后一份 ⇒ 不需要 GetStatus 服务（M-6）
    status_pub_ = node->create_publisher<arm_msgs::msg::HardwareStatus>(
      "/arm/hardware_status", rclcpp::QoS(1).transient_local());
    RCLCPP_INFO(get_logger(),
      "运行期使能接口已注册：/arm/set_enabled、/arm/emergency_stop；"
      "状态话题 /arm/hardware_status（latched，10 Hz）");
  } else {
    RCLCPP_WARN(get_logger(),
      "没有节点（单测？）⇒ 未注册 /arm/set_enabled、/arm/emergency_stop、/arm/hardware_status");
  }
  return CallbackReturn::SUCCESS;
}

CallbackReturn DmSystemInterface::on_activate(const rclcpp_lifecycle::State &)
{
  // 打开串口。
  if (bus_ == nullptr) {
    RCLCPP_FATAL(get_logger(), "还没 configure 就 activate");
    return CallbackReturn::ERROR;
  }
  try {
    bus_->open();
    RCLCPP_INFO(get_logger(), "串口已打开：%s @%d", device_.c_str(), baud_);

    // 等待所有电机上线，最多 ~8 秒。这是真机踩坑后的经验。
    // 冷启动/刚上电时电机回得慢 ⇒ **重试**到全部就位（最多 ~5 秒），别"一次采样就判死刑"。
    // 2026-10-05 真机踩过：只做一次 2 秒 sync_states 就要求 6 台全在 ⇒ 偶发一台慢（Python 侧一问
    // 全部正常）⇒ on_activate 直接 FATAL ⇒ 硬件没激活 ⇒ 没有 /joint_states ⇒ RViz 里"残缺的模型"。
    constexpr int kWaitRounds = 10;                 // 10 × (0.5s sync + 0.3s 等) ≈ 8s 上限
    constexpr double kRoundTimeout = 0.5;
    std::vector<std::string> missing;
    for (int round = 1; round <= kWaitRounds; ++round) {
      bus_->sync_states(kRoundTimeout, 0.05);
      missing.clear();
      for (const auto & jp : joints_) {
        if (!bus_->get_state(jp.cfg.motor_id).has_value()) {
          missing.push_back(jp.name + "(id" + std::to_string(jp.cfg.motor_id) + ")");
        }
      }
      if (missing.empty()) {break;}
      if (round == 1 || round % 4 == 0) {
        std::string names;
        for (const auto & m : missing) {names += " " + m;}
        RCLCPP_WARN(get_logger(), "还有 %zu 台没反馈（%s）—— 继续等（第 %d/%d 轮）",
          missing.size(), names.c_str(), round, kWaitRounds);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }

    // 要动电机就要求全部在线；只读模式容忍缺失。
    if (!missing.empty()) {
      std::string names;
      for (const auto & m : missing) {names += " " + m;}
      if (enable_on_activate_) {
        // 要动电机：缺一台都不许（否则会使能一半的臂，很危险）
        throw std::runtime_error(
          "要动电机，但 " + std::to_string(missing.size()) + " 台始终没有反馈：" + names +
          " —— 查接线/供电/设备号（Python 侧 dm_bus 一问就知道是不是硬件）");
      }
      // 只读：**不致命** —— 能看几台是几台，read() 会按关节处理
      RCLCPP_WARN(get_logger(),
        "只读模式：%zu 台没有反馈（%s）—— 继续跑（不影响其它关节）", missing.size(), names.c_str());
    }

    // 写控制模式寄存器（**必须每次激活都写**：`0x0A` 是 RAM，掉电复位）。固定 POS_VEL。
    // 逐台走 `write_mode_one()`：**与运行期使能状态机共用同一份**（别写两遍）
    for (std::size_t i = 0; i < joints_.size(); ++i) {write_mode_one(i);}

    // 重置所有状态，防止跨运行污染。
    // 命令先全部置 NaN ⇒ write() 里会走"保持"（用**锁定的**保持目标），绝不朝零位冲
    const double nan = nan_value();
    std::fill(hw_commands_.begin(), hw_commands_.end(), nan);
    hw_positions_.assign(joints_.size(), nan);
    hw_velocities_.assign(joints_.size(), nan);
    hw_efforts_.assign(joints_.size(), nan);
    hw_effort_commands_.assign(joints_.size(), nan);   // 空插槽：永远 NaN

    // 只读模式直接返回。
    // ⚠️ 从这里开始组件才算"能用运行期使能接口"：服务会检查 active_ 与 bus_。
    active_ = true;
    armed_ = false;                                  // 只读时**真实**使能态 = 假
    if (!enable_on_activate_) {
      RCLCPP_WARN(get_logger(),
        "enable_on_activate=false：**只读** —— 不使能、不发控制帧，只发 0x7FF 刷新帧读状态。"
        "（要用运行期使能接口就调 /arm/set_enabled）");
      return CallbackReturn::SUCCESS;
    }

    // 使能全部 + 锁保持目标 + 补保持帧。
    // **与运行期状态机共用同一份**（enable_all_and_latch）—— 别写两遍。
    enable_all_and_latch();
    armed_ = true;
    RCLCPP_WARN(get_logger(),
      "已使能 %zu 台（POS_VEL；保持目标 = **使能那一刻锁定**的电机侧位置）", joints_.size());
    return CallbackReturn::SUCCESS;
    // 异常处理：打印可用设备、尽力失能、关串口。
  } catch (const std::exception & e) {
    RCLCPP_FATAL(get_logger(), "on_activate 失败：%s", e.what());
    RCLCPP_FATAL(get_logger(),
      "当前存在的串口设备：%s —— 设备号会随插拔顺序变（陷阱 #28）；真源是 "
      "motor_driver/config/joint.yaml 的 channel（xacro 会把它写进 <param name=\"device\">）。\n"
      "  若是 'Device or resource busy'：**另一个 launch 还占着串口**（同一时刻只能起一套："
      "先 Ctrl-C 掉旧的那套再起新的，否则两套会互相踩数据，现象是'发轨迹不动'）",
      list_tty_acm_devices().c_str());
    disable_all_quietly();
    try {
      if (bus_ != nullptr) {bus_->close();}
    } catch (...) {
    }
    return CallbackReturn::ERROR;
  }
}

// 失能（不关串口，可重新激活）。
CallbackReturn DmSystemInterface::on_deactivate(const rclcpp_lifecycle::State &)
{
  RCLCPP_INFO(get_logger(), "deactivate：全部失能");
  disable_all_quietly();
  // 失能了就回到"只读"：armed_=false ⇒ read() 重新发 0x7FF 刷新帧、write() 什么都不发。
  // ⚠️ 运行期使能接口也随之失效（request_enable 会因 !active_ 拒绝）——**这是对的**：
  //    组件不在 ACTIVE 时 read/write 都不跑，状态机根本推不动。
  armed_ = false;
  active_ = false;
  enable_req_.store(0);
  phase_ = EnablePhase::Idle;
  phase_step_ = 0;
  return CallbackReturn::SUCCESS;
}

// 彻底清理（关串口 + 释放对象）。
CallbackReturn DmSystemInterface::on_cleanup(const rclcpp_lifecycle::State &)
{
  disable_all_quietly();
  if (bus_ != nullptr) {
    try {
      bus_->close();
    } catch (...) {
    }
  }
  teardown();
  // ROS 句柄也放掉（重新 configure 时会重建）；状态机复位，避免跨次运行带状态。
  set_enabled_srv_.reset();
  estop_srv_.reset();
  status_pub_.reset();
  status_tick_ = 0;
  armed_ = false;
  active_ = false;
  enable_req_.store(0);
  enable_result_.store(0);
  phase_ = EnablePhase::Idle;
  phase_step_ = 0;
  return CallbackReturn::SUCCESS;
}

// 错误状态下也尽力安全收尾。
CallbackReturn DmSystemInterface::on_error(const rclcpp_lifecycle::State &)
{
  RCLCPP_ERROR(get_logger(), "进入 error 状态：尽力失能并关串口");
  disable_all_quietly();
  armed_ = false;
  active_ = false;
  enable_req_.store(0);
  phase_ = EnablePhase::Idle;
  phase_step_ = 0;
  if (bus_ != nullptr) {
    try {
      bus_->close();
    } catch (...) {
    }
  }
  return CallbackReturn::SUCCESS;
}

// ══════════════════════════ 接口导出 ══════════════════════════

std::vector<hardware_interface::StateInterface> DmSystemInterface::export_state_interfaces()
{
  std::vector<hardware_interface::StateInterface> out;
  for (std::size_t i = 0; i < joints_.size(); ++i) {
    out.emplace_back(joints_[i].name, hardware_interface::HW_IF_POSITION, &hw_positions_[i]);
    out.emplace_back(joints_[i].name, hardware_interface::HW_IF_VELOCITY, &hw_velocities_[i]);
    // effort：电机反馈里的**电流估计**力矩（模型坐标，矢量只乘 sign）。
    // 用途：POS_VEL 托住机械臂时读真实保持力矩（用来校核重力模型量级）、ROS 侧监控。
    out.emplace_back(joints_[i].name, hardware_interface::HW_IF_EFFORT, &hw_efforts_[i]);
  }
  return out;
}

std::vector<hardware_interface::CommandInterface> DmSystemInterface::export_command_interfaces()
{
  std::vector<hardware_interface::CommandInterface> out;
  for (std::size_t i = 0; i < joints_.size(); ++i) {
    out.emplace_back(joints_[i].name, hardware_interface::HW_IF_POSITION, &hw_commands_[i]);
    // effort：**空插槽**（见头文件）。本层永远不读它，导出只是为了将来加力矩控制器时
    // 不必改 URDF/xacro。现在没有控制器声索它 ⇒ 永远是 NaN。
    out.emplace_back(joints_[i].name, hardware_interface::HW_IF_EFFORT, &hw_effort_commands_[i]);
  }
  return out;
}

// ══════════════════════════ 周期读 ══════════════════════════

return_type DmSystemInterface::read(const rclcpp::Time &, const rclcpp::Duration &)
{
  if (bus_ == nullptr || io_ == nullptr || !io_->is_open()) {return return_type::ERROR;}

  bus_->poll();
  for (std::size_t i = 0; i < joints_.size(); ++i) {
    JointParams & jp = joints_[i];
    JointRuntime & r = rt_[i];
    if (!bus_->get_state(jp.cfg.motor_id).has_value()) {
      if (++r.missing_streak >= fail_streak_limit_) {
        if (armed_) {
          RCLCPP_ERROR(get_logger(),
            "关节 %s（电机 %u）连续 %d 圈没有反馈 —— 报 ERROR（串口掉了 / 电机断电？）",
            jp.name.c_str(), jp.cfg.motor_id, r.missing_streak);
          return return_type::ERROR;
        }
        // 只读模式：不致命，只提示一次（能看几台是几台）
        if (r.missing_streak == fail_streak_limit_) {
          RCLCPP_WARN(get_logger(), "只读模式：关节 %s（电机 %u）一直没有反馈 —— 跳过它",
            jp.name.c_str(), jp.cfg.motor_id);
        }
      }
      continue;
    }
    r.missing_streak = 0;
    const JointState st = jp.joint->get_state();      // q_ours
    hw_positions_[i] = ours_to_model(i, st.position);
    hw_velocities_[i] = static_cast<double>(jp.sign) * st.velocity;   // 矢量只乘 sign
    // 力矩同样只乘 sign（矢量）。它来自电机的**电流估计**（12 位定点）⇒ 只当量级参考，
    // 别当力矩传感器用；用途是"POS_VEL 托住时读真实保持力矩"和 ROS 侧监控。
    hw_efforts_[i] = static_cast<double>(jp.sign) * st.torque;
    if (st.err != 0x0 && st.err != 0x1) {
      const char * text = err_text(st.err);
      if (armed_) {
        RCLCPP_ERROR(get_logger(),
          "关节 %s 故障 ERR=%u（%s）—— 报 ERROR。注意 ERR=13(通讯丢失) 是锁存的，只能断电再上电清；"
          "失能只是让电机松掉，重力负载下会掉", jp.name.c_str(), st.err,
          text != nullptr ? text : "未知错误码");
        return return_type::ERROR;
      }
      // 只读模式：报错但不中断（否则整条只读链被一台故障拖死，反而看不见其它关节）
      if (r.err_warned != st.err) {
        r.err_warned = st.err;
        RCLCPP_ERROR(get_logger(),
          "只读模式：关节 %s 故障 ERR=%u（%s）—— 继续显示其它关节。ERR=13 锁存，需断电重上电",
          jp.name.c_str(), st.err, text != nullptr ? text : "未知错误码");
      }
    } else {
      r.err_warned = 0;
    }
  }
  // 只读（或**运行期已失能**）要主动问状态（电机不主动报）；使能后命令帧本身就带回包，不必再刷。
  // ⇒ 这正是 MANAGE_ARCH M-5 想要的行为：`SafeDisarm` 失能之后，插件自动回到"只发刷新帧"
  if (!armed_) {
    for (const auto & jp : joints_) {bus_->send_refresh(jp.cfg.motor_id);}
  }

  // 状态话题（10 Hz @ 100 Hz 控制周期）：**只能由插件发** —— ros2_control 的状态接口只有控制器
  // 读得到，外部节点拿不到 ERR/温度（见 MANAGE_ARCH §4.2/M-5）。放 read() 不放 write()：
  // write() 是发帧关键路径，别在那儿做序列化与分配。
  if (status_pub_ != nullptr && ++status_tick_ >= kStatusEvery) {
    status_tick_ = 0;
    publish_status();
  }
  return return_type::OK;
}

// ══════════════════════════ 周期写 ══════════════════════════

// 分派：没 bus → ERROR；使能/失能请求 → 推进使能状态机；没使能 → 什么都不发；否则装配 POS_VEL 帧。
return_type DmSystemInterface::write(const rclcpp::Time &, const rclcpp::Duration &)
{
  if (bus_ == nullptr) {return return_type::ERROR;}
  // ① 运行期使能/失能状态机**最优先**（必须在只读早返回**之前**，否则只读启动后永远使能不了）。
  //    一个周期最多做一件事（一次 `0x0A` 写 ≈12 ms）；期间**不发控制帧**。
  //    前提是"全臂失能"（request_enable 检查过）⇒ 这几十 ms 里没有已使能的关节，
  //    不会撞 500 ms 看门狗（陷阱 #24/#25）。
  if (enable_req_.load() != 0) {
    try {
      advance_enable_fsm();
      return return_type::OK;
    } catch (const std::exception & e) {
      RCLCPP_ERROR(get_logger(), "使能状态机失败：%s", e.what());
      return return_type::ERROR;
    }
  }
  // ② 没使能（只读启动 / 运行期已失能）：**什么都不发** —— "不接真机也能先只看"的前提
  if (!armed_) {return return_type::OK;}
  // ③ 稳态：装配 POS_VEL 帧（本层只有这一种模式）
  return write_pos_vel();
}

// POS_VEL 装配：命令是 NaN → 发保持帧（目标 = 使能那一刻锁定的**电机侧**实测值，
// 不换算不钳位、也不重读 —— 重读=follow-me，没有回复力，陷阱 #38）；有命令 → 正常 set_pos_vel。
return_type DmSystemInterface::write_pos_vel()
{
  for (std::size_t i = 0; i < joints_.size(); ++i) {
    if (std::isnan(hw_commands_[i])) {
      if (!rt_[i].hold_pos.has_value()) {
        RCLCPP_ERROR(get_logger(), "关节 %s 没有锁定的保持目标，无法发保持帧",
          joints_[i].name.c_str());
        return return_type::ERROR;
      }
      bus_->send_pos_vel(joints_[i].cfg.motor_id, *rt_[i].hold_pos, kHoldVlim);
      continue;
    }
    try {
      joints_[i].joint->set_pos_vel(model_to_ours(i, hw_commands_[i]), vlim_);
    } catch (const std::exception & e) {
      RCLCPP_ERROR(get_logger(), "关节 %s 拒发：%s", joints_[i].name.c_str(), e.what());
      return return_type::ERROR;
    }
  }
  return return_type::OK;
}

// ══════════════════════════ 运行期使能（M9.2）══════════════════════════
// 设计见头文件与 docs/MANAGE_ARCH.md §5。三条要点：
//   ① 服务回调只做"前提检查 + 置请求"，真正的活由 write() 里的状态机干；
//   ② **失能请求优先**，可以随时打断使能序列（急停要能立刻生效，不等 6 次寄存器写完）；
//   ③ 使能序列与 on_activate **共用** write_mode_one() / enable_all_and_latch()。

// 先写原因（加锁）再置原子量 ⇒ 读到 result≠0 时原因一定已就绪。
void DmSystemInterface::set_enable_result(int v, const std::string & why)
{
  {
    std::lock_guard<std::mutex> lk(enable_mu_);
    enable_fail_why_ = why;
  }
  enable_result_.store(v);
}

bool DmSystemInterface::request_enable(bool enable, std::string * why)
{
  auto fail = [why](const std::string & s) {if (why != nullptr) {*why = s;} return false;};
  if (enable_req_.load() != 0) {
    return fail("已有一个使能/失能请求在处理中（等它完成；急停请求会打断它）");
  }
  if (!active_) {
    return fail("硬件组件不是 ACTIVE —— 先把它切到 ACTIVE（见 MANAGE_ARCH M-1）");
  }
  if (bus_ == nullptr || io_ == nullptr || !io_->is_open()) {return fail("串口没开");}

  if (enable == armed_) {
    // 幂等：已经是想要的状态 ⇒ **一帧都不发**，直接报成功
    set_enable_result(1, enable ? "已经是使能态（幂等，未发帧）" : "已经是失能态（幂等，未发帧）");
    if (why != nullptr) {*why = enable ? "已经是使能态（幂等）" : "已经是失能态（幂等）";}
    return true;
  }

  if (enable) {
    // 前提：**全臂失能**（ERR 必须全是 0）。"使能一半的臂"是这里最危险的失败模式：
    // 已使能的关节会被喂帧、未使能的不会。
    std::string bad;
    for (const auto & jp : joints_) {
      const auto st = bus_->get_state(jp.cfg.motor_id);
      if (!st.has_value()) {
        bad += " " + jp.name + "(没有反馈)";
      } else if (st->err != 0x0) {
        bad += " " + jp.name + "(ERR=" + std::to_string(st->err) + ")";
      }
    }
    if (!bad.empty()) {
      return fail("拒绝使能：有电机不处于失能态（ERR≠0）：" + bad +
        " ⇒ 不许重新使能（会使能一半的臂）。ERR=13(通讯丢失) 锁存，只能断电重上电清");
    }
  }

  enable_result_.store(0);
  {
    std::lock_guard<std::mutex> lk(enable_mu_);
    enable_fail_why_.clear();
  }
  if (why != nullptr) {*why = "已受理";}
  enable_req_.store(enable ? 1 : -1);   // 最后置：状态机看到请求时，上面的清场一定已完成
  return true;
}

bool DmSystemInterface::wait_enable_done(double timeout_s, std::string * why)
{
  const auto t0 = std::chrono::steady_clock::now();
  while (true) {
    const int r = enable_result_.load();
    if (r != 0) {
      if (r > 0) {
        if (why != nullptr) {*why = "完成";}
        return true;
      }
      std::lock_guard<std::mutex> lk(enable_mu_);
      if (why != nullptr) {*why = enable_fail_why_.empty() ? "失败（没有原因）" : enable_fail_why_;}
      return false;
    }
    if (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() > timeout_s) {
      if (why != nullptr) {
        *why = "等状态机完成超时（" + std::to_string(timeout_s) +
          " s）—— CM 的 update 循环还在跑吗？看 /arm/hardware_status";
      }
      return false;
    }
    // 5 ms 轮询：状态机每一步是 10 ms（控制周期）或 ~12 ms（寄存器写），5 ms 足够细
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
}

// 使能/失能状态机。
// 抛出的异常由 write() 兜住并转成 return_type::ERROR。
void DmSystemInterface::advance_enable_fsm()
{
  // ① 失能（含"使能途中被急停打断"）：**优先**，一个周期做完，不碰寄存器。
  if (enable_req_.load() < 0) {
    disable_all_quietly();                       // 永不抛
    armed_ = false;
    phase_ = EnablePhase::Idle;
    phase_step_ = 0;
    enable_req_.store(0);
    // ⚠️ **必须重新问一次真实状态再下结论**：失能帧的反馈还没进缓存，直接看缓存会读到**上一次的
    //    ERR=1**（陷阱 #20 的同款形状：陈旧回包骗缓存）。`sync_states` 会 flush + 刷新到安静。
    // ⚠️ 这里会阻塞约 30~50 ms（= quiet 窗口）⇒ CM 会打一条 overrun 警告（实测 Write time 53.9 ms、
    //    missed 6 拍）。**无害**：此刻 6 台都已经不发控制帧了（失能帧刚发完），掉拍不影响看门狗。
    //    仍在陷阱 #25 的"不许 >100 ms"之内。
    try {
      bus_->sync_states(0.3, 0.03);
    } catch (...) {
    }
    // 复核：以缓存里的 ERR 为准（真失能才是 0）。**别只凭"帧发出去了"就报成功**。
    std::string still_on;
    for (const auto & jp : joints_) {
      const auto st = bus_->get_state(jp.cfg.motor_id);
      if (!st.has_value()) {
        still_on += " " + jp.name + "(没有反馈)";
      } else if (st->err != 0x0) {
        still_on += " " + jp.name + "(ERR=" + std::to_string(st->err) + ")";
      }
    }
    if (still_on.empty()) {
      set_enable_result(1, "已失能（全部 ERR=0）");
    } else {
      set_enable_result(-1, "发过失能帧，但复核仍有电机不是失能态：" + still_on +
        " —— ERR=13 锁存需断电重上电；看 /arm/hardware_status");
    }
    return;
  }

  // ② 使能：逐周期推进（一个周期最多一次寄存器 I/O）。
  if (phase_ == EnablePhase::Idle) {
    phase_ = EnablePhase::WriteMode;
    phase_step_ = 0;
    RCLCPP_WARN(get_logger(),
      "运行期使能开始：逐台写 0x0A=%u（每周期一台，共 %zu 台；**这期间不发控制帧**）",
      kCtrlModePosVel, joints_.size());
    return;                                     // 这一周期只起个头
  }
  if (phase_ == EnablePhase::WriteMode) {
    write_mode_one(phase_step_);                // 一次一台（~12 ms 静默）
    if (++phase_step_ >= joints_.size()) {phase_ = EnablePhase::EnableAll;}
    return;
  }
  if (phase_ == EnablePhase::EnableAll) {
    // 全部使能 + 锁保持目标 + 补保持帧 —— **与 on_activate 同一份代码**
    enable_all_and_latch();
    armed_ = true;
    phase_ = EnablePhase::Idle;
    phase_step_ = 0;
    enable_req_.store(0);
    set_enable_result(1, "已使能（POS_VEL，保持目标已锁定）");
    RCLCPP_WARN(get_logger(), "运行期使能完成：POS_VEL");
    return;
  }
}

void DmSystemInterface::handle_set_enabled(
  const std::shared_ptr<arm_msgs::srv::SetEnabled::Request> req,
  std::shared_ptr<arm_msgs::srv::SetEnabled::Response> res)
{
  std::string why;
  res->now_armed = armed_;                        // **调用那一刻**的状态
  if (!request_enable(req->enable, &why)) {
    res->success = false;
    res->message = why;
    return;
  }
  if (enable_req_.load() == 0) {                  // 幂等路径（request_enable 已置好结果）
    res->success = true;
    res->now_armed = armed_;
    res->message = why;
    return;
  }
  // **阻塞等状态机完成**（约 1 s，A 方案）。能这么做是因为 CM 的 read/update/write 不跑在
  // executor 上（见头文件里的说明）⇒ 回调阻塞**不会**卡住状态机（否则就是陷阱 #57 的死锁形状）。
  // 代价：这 ~1 s 内 CM 的其它服务排队（此刻控制器本来也该是 inactive）。
  const bool done = wait_enable_done(enable_timeout_s_, &why);
  res->success = done;
  res->now_armed = armed_;                        // 完成后才是**真实**状态
  res->message = done ? (armed_ ? "已使能（保持目标已锁定）" : "已失能") : ("未完成：" + why);
}

void DmSystemInterface::handle_emergency_stop(
  std::shared_ptr<arm_msgs::srv::HardwareEmergencyStop::Response> res)
{
  // 急停 = 立即失能。**不碰寄存器、不做恢复**（ERR=13 锁存只能断电清）。
  // 走同一条状态机（串行化 ⇒ 不会与 RT 线程并发写串口），失能一个周期就能做完。
  std::string why;
  bool not_confirmed = false;
  if (armed_ || enable_req_.load() == 1) {
    if (request_enable(false, &why)) {
      if (enable_req_.load() != 0 && !wait_enable_done(2.0, &why)) {not_confirmed = true;}
    } else {
      not_confirmed = true;                       // 连请求都发不出去（已有一个请求在处理）
    }
  } else {
    why = "本来就是失能态（幂等，未发帧）";
  }

  // 复核每台的真实状态：**以缓存里的 ERR 为准**，别只凭"发过失能帧"
  std::string failed_names;
  for (const auto & jp : joints_) {
    const auto st = bus_->get_state(jp.cfg.motor_id);
    if (st.has_value() && st->err == 0x0) {
      res->disabled.push_back(jp.name);
    } else {
      res->failed.push_back(jp.name);
      failed_names += " " + jp.name +
        (st.has_value() ? "(ERR=" + std::to_string(st->err) + ")" : "(没有反馈)");
    }
  }
  res->success = !not_confirmed && res->failed.empty();
  if (not_confirmed) {
    res->message = "未能确认失能：" + why +
      " —— 状态机没推进（CM 的 update 停了？）；请断电，或用 ros2 control set_hardware_component_state";
  } else if (res->failed.empty()) {
    res->message = "已失能全部 " + std::to_string(res->disabled.size()) + " 台（ERR=0）";
  } else {
    res->message = "有 " + std::to_string(res->failed.size()) + " 台仍未失能：" + failed_names +
      " —— ERR=13(通讯丢失) 锁存，只能断电重上电清";
  }
}

void DmSystemInterface::publish_status()
{
  if (status_pub_ == nullptr) {return;}
  arm_msgs::msg::HardwareStatus m;
  const auto clock = get_clock();
  if (clock != nullptr) {m.stamp = clock->now();}
  // mode：本层**固定** POS_VEL（唯一取值）。不再是"运行期真值"，因为模式不可切换了。
  m.mode = "POS_VEL";
  for (const auto & jp : joints_) {
    m.joint_names.push_back(jp.name);
    std::optional<MotorState> st;
    if (bus_ != nullptr) {st = bus_->get_state(jp.cfg.motor_id);}
    if (st.has_value()) {
      const char * text = err_text(st->err);
      m.err.push_back(static_cast<int32_t>(st->err));
      m.err_text.emplace_back(text != nullptr ? text : "未知错误码");
      // 反馈帧里的 D[6]/D[7] **就是摄氏度**，不做换算
      m.temp_mos.push_back(static_cast<double>(st->t_mos_raw));
      m.temp_rotor.push_back(static_cast<double>(st->t_rotor_raw));
      m.armed.push_back(st->err == 0x1);
    } else {
      m.err.push_back(-1);                        // -1 = 没有反馈（不是电机报的 ERR）
      m.err_text.emplace_back("没有反馈");
      m.temp_mos.push_back(nan_value());
      m.temp_rotor.push_back(nan_value());
      m.armed.push_back(false);
    }
  }
  if (bus_ != nullptr) {
    const SerialStats & s = bus_->stats();
    m.tx_calls = s.tx_calls;
    m.rx_calls = s.rx_calls;
    m.tx_bytes = s.tx_bytes;
    m.rx_bytes = s.rx_bytes;
  }
  status_pub_->publish(m);
}

}  // namespace motor_driver_hardware

#include <pluginlib/class_list_macros.hpp>   // NOLINT
PLUGINLIB_EXPORT_CLASS(motor_driver_hardware::DmSystemInterface, hardware_interface::SystemInterface)
