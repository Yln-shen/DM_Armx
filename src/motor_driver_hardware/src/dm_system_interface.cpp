// dm_system_interface.cpp —— SystemInterface 插件实现（真机通路）。
#include "motor_driver_hardware/dm_system_interface.hpp"

#include <hardware_interface/types/hardware_interface_type_values.hpp>

#include <glob.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_map>

namespace motor_driver_hardware
{

namespace
{
using Params = std::unordered_map<std::string, std::string>;
using hardware_interface::CallbackReturn;
using hardware_interface::return_type;

constexpr uint8_t kRegCtrlMode = 0x0A;   // CTRL_MODE（RAM；uint32）
// 重力前馈的残差守卫阈值：偏离保持点这么多就**撤掉全部 tau_ff**（仍留在 MIT + kp_hold 下）
constexpr double kGravityGuardRad = 0.1;        // 位置偏差（rad）
constexpr double kGravityGuardRadPerS = 0.5;    // 速度（rad/s）
// 速度那一路的宽限期与防抖（见 gravity_guard_since_ 的注释：使能瞬间机械臂真的在掉）
constexpr double kGravityGraceSec = 0.5;
constexpr double kGravityVelDebounceSec = 0.2;
constexpr uint8_t kRegKpAsr = 0x19;
constexpr uint8_t kRegKiAsr = 0x1A;
constexpr uint8_t kRegKpApr = 0x1B;
constexpr uint8_t kRegKiApr = 0x1C;

bool has(const Params & m, const std::string & key) {return m.count(key) != 0;}

std::string as_str(const Params & m, const std::string & key, const std::string & def)
{
  auto it = m.find(key);
  return it == m.end() ? def : it->second;
}

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

int as_int(const Params & m, const std::string & key, int def)
{
  return static_cast<int>(as_double(m, key, static_cast<double>(def)));
}

bool as_bool(const Params & m, const std::string & key, bool def)
{
  auto it = m.find(key);
  if (it == m.end()) {return def;}
  const std::string v = it->second;
  if (v == "true" || v == "True" || v == "1") {return true;}
  if (v == "false" || v == "False" || v == "0") {return false;}
  throw std::invalid_argument("参数 " + key + " 不是布尔：" + v);
}

// 打不开串口时把**当前存在的** ttyACM 设备列出来 —— 设备号会随插拔顺序变（AGENTS 陷阱 #28），
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

}  // namespace

DmSystemInterface::~DmSystemInterface()
{
  teardown();
}

// ── 参数 ──
bool DmSystemInterface::parse_params(const hardware_interface::HardwareInfo & info)
{
  try {
    const Params & hp = info.hardware_parameters;
    device_ = as_str(hp, "device", "/dev/ttyACM1");
    baud_ = as_int(hp, "baud", 921600);
    // ⚠️ 默认 **false = 只读**：不使能、不发控制帧。要动电机必须显式打开。
    enable_on_activate_ = as_bool(hp, "enable_on_activate", false);
    vlim_ = as_double(hp, "vlim", 1.0);
    fail_streak_limit_ = as_int(hp, "fail_streak_limit", 10);
    // 重力前馈总开关（默认 false = 一切照旧走 POS_VEL）。2.4.2 只解析，**尚未使用**。
    gravity_ff_ = as_bool(hp, "gravity_ff", false);
    urdf_path_ = as_str(hp, "urdf_path", "");
    gravity_ff_scale_ = as_double(hp, "gravity_ff_scale", 1.0);
    if (!(vlim_ > 0.0)) {throw std::invalid_argument("vlim 必须是正数（rad/s）");}
    if (fail_streak_limit_ <= 0) {throw std::invalid_argument("fail_streak_limit 必须是正整数");}
    if (!std::isfinite(gravity_ff_scale_) || gravity_ff_scale_ < 0.0 || gravity_ff_scale_ > 1.0) {
      // 只允许**减弱**：放大前馈没有正当用途，只会把符号错误放大
      throw std::invalid_argument("gravity_ff_scale 必须落在 [0, 1]（只用来分级上电，不允许放大）");
    }
    if (gravity_ff_ && urdf_path_.empty()) {
      throw std::invalid_argument("gravity_ff=true 时必须给 <param name=\"urdf_path\">（动力学模型）");
    }

    joints_.clear();
    for (const auto & j : info.joints) {
      const Params & p = j.parameters;
      if (!has(p, "motor_id")) {
        throw std::invalid_argument("关节 " + j.name + " 缺 <param name=\"motor_id\">");
      }
      JointParams jp;
      jp.name = j.name;
      jp.cfg.name = j.name;
      jp.cfg.motor_id = static_cast<uint8_t>(as_int(p, "motor_id", 0));
      if (jp.cfg.motor_id < 1 || jp.cfg.motor_id > 6) {
        throw std::invalid_argument("关节 " + j.name + " 的 motor_id 必须在 1~6");
      }
      jp.cfg.direction = as_int(p, "direction", 1);
      jp.cfg.offset = as_double(p, "offset", 0.0);
      jp.cfg.motor_type = as_str(p, "motor_type", "");
      jp.cfg.limit = Limit{as_double(p, "p_max", 12.5), as_double(p, "v_max", 10.0),
        as_double(p, "t_max", 28.0)};
      if (has(p, "position_min")) {jp.cfg.position_min = as_double(p, "position_min", 0.0);}
      if (has(p, "position_max")) {jp.cfg.position_max = as_double(p, "position_max", 0.0);}
      // gravity_ff=true ⇒ 整条链走 MIT（只有 MIT 能直接给力矩，重力前馈必须有它）；
      // false ⇒ 一切照旧 POS_VEL（固件闭环，静态精度最好）
      jp.cfg.mode = gravity_ff_ ? 1 : 2;
      jp.sign = as_int(p, "sign", 1);
      if (jp.sign != 1 && jp.sign != -1) {
        throw std::invalid_argument("关节 " + j.name + " 的 sign 只能是 ±1");
      }
      jp.zero_shift = as_double(p, "zero_shift", 0.0);
      // MIT（gravity_ff=true）才用得上：力矩上限与保持增益。2.4.2 只解析，**不使用**。
      if (has(p, "torque_max")) {jp.cfg.torque_max = as_double(p, "torque_max", 0.0);}
      jp.kp_hold = as_double(p, "kp_hold", 0.0);
      jp.kd_hold = as_double(p, "kd_hold", 0.0);
      if (gravity_ff_) {
        // 打开重力前馈才校验 —— 默认 false 时这些参数不该拦住任何东西
        if (!jp.cfg.torque_max.has_value() || !(*jp.cfg.torque_max > 0.0)) {
          throw std::invalid_argument(
            "关节 " + j.name + "：gravity_ff=true 时 torque_max 必须是正数（关节侧 N·m）");
        }
        if (!(jp.kp_hold > 0.0)) {
          throw std::invalid_argument(
            "关节 " + j.name + "：gravity_ff=true 时 kp_hold 必须是正数");
        }
        if (!(jp.kd_hold >= 0.0)) {
          throw std::invalid_argument("关节 " + j.name + "：kd_hold 不能是负数");
        }
      }
      // PID 四个都给了才写（不给 = 保留电机 RAM 里的当前值）
      if (has(p, "kp_asr") && has(p, "ki_asr") && has(p, "kp_apr") && has(p, "ki_apr")) {
        jp.pid = std::array<double, 4>{as_double(p, "kp_asr", 0.0), as_double(p, "ki_asr", 0.0),
          as_double(p, "kp_apr", 0.0), as_double(p, "ki_apr", 0.0)};
      }
      joints_.push_back(std::move(jp));
    }
    if (joints_.empty()) {throw std::invalid_argument("URDF 里一个关节都没有");}

    const double nan = std::numeric_limits<double>::quiet_NaN();
    hw_positions_.assign(joints_.size(), nan);      // 还没读到就保持 NaN，别谎报 0
    hw_velocities_.assign(joints_.size(), nan);
    hw_efforts_.assign(joints_.size(), nan);
    hw_commands_.assign(joints_.size(), nan);       // NaN ⇒ write() 走"保持"
    hw_vel_commands_.assign(joints_.size(), nan);   // 同理：没人写过就是 NaN（MIT 分支会查）

    std::string summary;
    for (const auto & jp : joints_) {
      summary += " " + jp.name + "(id" + std::to_string(jp.cfg.motor_id) + ",dir" +
        std::to_string(jp.cfg.direction) + ",off" + std::to_string(jp.cfg.offset) + ",s" +
        std::to_string(jp.sign) + ",δ" + std::to_string(jp.zero_shift) +
        (jp.cfg.torque_max.has_value() ? ",tm" + std::to_string(*jp.cfg.torque_max) : "") +
        ",kp" + std::to_string(jp.kp_hold) + ",kd" + std::to_string(jp.kd_hold) +
        (jp.pid.has_value() ? ",pid" : "") + ")";
    }
    RCLCPP_INFO(get_logger(),
      "参数就绪：%zu 关节%s\n  device=%s baud=%d vlim=%.3f rad/s enable_on_activate=%s"
      "\n  gravity_ff=%s scale=%.3f urdf_path=%s",
      joints_.size(), summary.c_str(), device_.c_str(), baud_, vlim_,
      enable_on_activate_ ? "true（会动电机）" : "false（只读）",
      gravity_ff_ ? "true（整链 MIT + 重力前馈）" : "false（走 POS_VEL）", gravity_ff_scale_,
      urdf_path_.empty() ? "(未给)" : urdf_path_.c_str());
    return true;
  } catch (const std::exception & e) {
    RCLCPP_FATAL(get_logger(), "参数有问题：%s", e.what());
    return false;
  }
}

void DmSystemInterface::build_hardware()
{
  if (injected_io_ == nullptr) {owned_io_ = std::make_unique<SerialPort>(device_, baud_);}
  io_ = (injected_io_ != nullptr) ? injected_io_ : owned_io_.get();
  bus_ = std::make_unique<MotorBus>(*io_);
  for (auto & jp : joints_) {jp.joint = std::make_unique<Joint>(*bus_, jp.cfg);}
}

void DmSystemInterface::teardown()
{
  for (auto & jp : joints_) {jp.joint.reset();}
  bus_.reset();
  owned_io_.reset();
  io_ = nullptr;
}

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

// ── 生命周期 ──
CallbackReturn DmSystemInterface::on_init(
  const hardware_interface::HardwareComponentInterfaceParams & params)
{
  if (hardware_interface::SystemInterface::on_init(params) != CallbackReturn::SUCCESS) {
    return CallbackReturn::ERROR;
  }
  if (!parse_params(params.hardware_info)) {return CallbackReturn::ERROR;}
  return CallbackReturn::SUCCESS;
}

CallbackReturn DmSystemInterface::on_configure(const rclcpp_lifecycle::State &)
{
  try {
    build_hardware();
  } catch (const std::exception & e) {
    RCLCPP_FATAL(get_logger(), "建立串口/总线/关节对象失败：%s", e.what());
    teardown();
    return CallbackReturn::ERROR;
  }
  if (gravity_ff_) {
    // 重力模型在这里一次性建好（Data/缓冲都预分配）⇒ write() 里零堆分配。
    // 构造失败**不静默降级**：少算一个关节的重力就敢发力矩，比直接起不来危险得多。
    try {
      std::vector<std::string> names;
      std::vector<int> signs;
      names.reserve(joints_.size());
      signs.reserve(joints_.size());
      for (const auto & jp : joints_) {
        names.push_back(jp.name);
        signs.push_back(jp.sign);
      }
      gravity_ = std::make_unique<GravityModel>(urdf_path_, names, signs);
      q_urdf_buf_.assign(joints_.size(), 0.0);
      tau_ours_buf_.assign(joints_.size(), 0.0);
      RCLCPP_INFO(get_logger(), "重力模型已加载：%s（%zu 关节，sign 已按关节套上）",
        urdf_path_.c_str(), gravity_->size());
    } catch (const std::exception & e) {
      RCLCPP_FATAL(get_logger(),
        "gravity_ff=true 但重力模型建不起来（urdf_path=%s）：%s", urdf_path_.c_str(), e.what());
      teardown();
      return CallbackReturn::ERROR;
    }
  }
  return CallbackReturn::SUCCESS;
}

CallbackReturn DmSystemInterface::on_activate(const rclcpp_lifecycle::State &)
{
  if (bus_ == nullptr) {
    RCLCPP_FATAL(get_logger(), "还没 configure 就 activate");
    return CallbackReturn::ERROR;
  }
  try {
    bus_->open();
    RCLCPP_INFO(get_logger(), "串口已打开：%s @%d", device_.c_str(), baud_);

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

    // 逐台：切 POS_VEL（0x0A 是 RAM，掉电回 MIT ⇒ 每次激活都要写）+ 可选写 PID
    for (const auto & jp : joints_) {
      const uint32_t mode = gravity_ff_ ? 1u : 2u;      // 1 = MIT（重力前馈）/ 2 = 位置速度
      if (!bus_->write_register(jp.cfg.motor_id, kRegCtrlMode, uint32_to_uint8s(mode))) {
        throw std::runtime_error(
          "给电机 " + std::to_string(jp.cfg.motor_id) + " 写 0x0A=" + std::to_string(mode) +
          " 没收到回包");
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

    // 命令先全部置 NaN ⇒ write() 里会走"保持"（用**锁定的**保持目标），绝不朝零位冲
    const double nan = std::numeric_limits<double>::quiet_NaN();
    std::fill(hw_commands_.begin(), hw_commands_.end(), nan);
    hw_positions_.assign(joints_.size(), nan);
    hw_velocities_.assign(joints_.size(), nan);
    hw_efforts_.assign(joints_.size(), nan);
    hw_vel_commands_.assign(joints_.size(), nan);   // 控制器还没写过速度命令
    if (enable_on_activate_) {
      for (auto & jp : joints_) {jp.joint->enable();}      // 内部会先查缓存位置，再补保持帧
      // 锁定保持目标 = 刚使能时的电机侧实测位置，之后 write() 一直发它。
      // 为什么不每圈重读："读哪停哪"没有回复力，重力能把关节慢慢压走（陷阱 #38）。
      for (auto & jp : joints_) {
        const auto st = bus_->get_state(jp.cfg.motor_id);
        if (!st.has_value()) {
          throw std::runtime_error(
            "关节 " + jp.name + " 使能后仍没有实测位置，锁不住保持目标");
        }
        jp.hold_pos = st->pos;
        // 同一个保持目标的**关节侧**值：POS_VEL 的保持帧要电机侧（绕过换算=真保持），
        // MIT 的保持帧要关节侧（set_mit 会自己换算回来）。两者是同一点，别混用。
        jp.hold_ours = jp.joint->motor_to_joint(st->pos);
      }
      RCLCPP_WARN(get_logger(), "已使能 %zu 台（保持目标 = **使能那一刻锁定**的电机侧位置 + vlim %.2f）",
        joints_.size(), kHoldVlim);
    } else {
      RCLCPP_WARN(get_logger(),
        "enable_on_activate=false：**只读** —— 不使能、不发控制帧，只发 0x7FF 刷新帧读状态");
    }
    return CallbackReturn::SUCCESS;
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

CallbackReturn DmSystemInterface::on_deactivate(const rclcpp_lifecycle::State &)
{
  RCLCPP_INFO(get_logger(), "deactivate：全部失能");
  disable_all_quietly();
  return CallbackReturn::SUCCESS;
}

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
  return CallbackReturn::SUCCESS;
}

CallbackReturn DmSystemInterface::on_error(const rclcpp_lifecycle::State &)
{
  RCLCPP_ERROR(get_logger(), "进入 error 状态：尽力失能并关串口");
  disable_all_quietly();
  if (bus_ != nullptr) {
    try {
      bus_->close();
    } catch (...) {
    }
  }
  return CallbackReturn::SUCCESS;
}

// ── 接口导出 ──
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
    // velocity：目前只被 MIT 分支（gravity_ff=true）使用；POS_VEL 路径不看它。
    out.emplace_back(joints_[i].name, hardware_interface::HW_IF_VELOCITY, &hw_vel_commands_[i]);
  }
  return out;
}

// ── 周期读写 ──
return_type DmSystemInterface::read(const rclcpp::Time &, const rclcpp::Duration &)
{
  if (bus_ == nullptr || io_ == nullptr || !io_->is_open()) {return return_type::ERROR;}

  bus_->poll();
  for (std::size_t i = 0; i < joints_.size(); ++i) {
    JointParams & jp = joints_[i];
    if (!bus_->get_state(jp.cfg.motor_id).has_value()) {
      if (++jp.missing_streak >= fail_streak_limit_) {
        if (enable_on_activate_) {
          RCLCPP_ERROR(get_logger(),
            "关节 %s（电机 %u）连续 %d 圈没有反馈 —— 报 ERROR（串口掉了 / 电机断电？）",
            jp.name.c_str(), jp.cfg.motor_id, jp.missing_streak);
          return return_type::ERROR;
        }
        // 只读模式：不致命，只提示一次（能看几台是几台）
        if (jp.missing_streak == fail_streak_limit_) {
          RCLCPP_WARN(get_logger(), "只读模式：关节 %s（电机 %u）一直没有反馈 —— 跳过它",
            jp.name.c_str(), jp.cfg.motor_id);
        }
      }
      continue;
    }
    jp.missing_streak = 0;
    const JointState st = jp.joint->get_state();      // q_ours
    hw_positions_[i] = ours_to_model(i, st.position);
    hw_velocities_[i] = static_cast<double>(jp.sign) * st.velocity;   // 矢量只乘 sign
    // 力矩同样只乘 sign（矢量）。它来自电机的**电流估计**（12 位定点）⇒ 只当量级参考，
    // 别当力矩传感器用；用途是"POS_VEL 托住时读真实保持力矩"和 ROS 侧监控。
    hw_efforts_[i] = static_cast<double>(jp.sign) * st.torque;
    if (st.err != 0x0 && st.err != 0x1) {
      const char * text = err_text(st.err);
      if (enable_on_activate_) {
        RCLCPP_ERROR(get_logger(),
          "关节 %s 故障 ERR=%u（%s）—— 报 ERROR。注意 ERR=13(通讯丢失) 是锁存的，只能断电再上电清；"
          "失能只是让电机松掉，重力负载下会掉", jp.name.c_str(), st.err,
          text != nullptr ? text : "未知错误码");
        return return_type::ERROR;
      }
      // 只读模式：报错但不中断（否则整条只读链被一台故障拖死，反而看不见其它关节）
      if (jp.err_warned != st.err) {
        jp.err_warned = st.err;
        RCLCPP_ERROR(get_logger(),
          "只读模式：关节 %s 故障 ERR=%u（%s）—— 继续显示其它关节。ERR=13 锁存，需断电重上电",
          jp.name.c_str(), st.err, text != nullptr ? text : "未知错误码");
      }
    } else {
      jp.err_warned = 0;
    }
  }
  // 只读模式要主动问状态（电机不主动报）；使能后命令帧本身就带回包，不必再刷
  if (!enable_on_activate_) {
    for (const auto & jp : joints_) {bus_->send_refresh(jp.cfg.motor_id);}
  }
  return return_type::OK;
}

return_type DmSystemInterface::write(const rclcpp::Time & time, const rclcpp::Duration &)
{
  if (bus_ == nullptr) {return return_type::ERROR;}
  // ⚠️ 只读模式下 write() **什么都不发**：这是"不接真机也能先只看"的前提
  if (!enable_on_activate_) {return return_type::OK;}
  if (gravity_ff_) {return write_gravity_ff(time);}

  for (std::size_t i = 0; i < joints_.size(); ++i) {
    JointParams & jp = joints_[i];
    if (std::isnan(hw_commands_[i])) {
      // 还没人写过命令（控制器没起 / 刚激活）：发保持帧 —— 目标是**使能那一刻锁定**的电机侧实测值，
      // 不换算不钳位、也不重读（重读=follow-me，没有回复力，见陷阱 #38）
      if (!jp.hold_pos.has_value()) {
        RCLCPP_ERROR(get_logger(), "关节 %s 没有锁定的保持目标，无法发保持帧", jp.name.c_str());
        return return_type::ERROR;
      }
      bus_->send_pos_vel(jp.cfg.motor_id, *jp.hold_pos, kHoldVlim);
      continue;
    }
    try {
      jp.joint->set_pos_vel(model_to_ours(i, hw_commands_[i]), vlim_);
    } catch (const std::exception & e) {
      RCLCPP_ERROR(get_logger(), "关节 %s 拒发：%s", jp.name.c_str(), e.what());
      return return_type::ERROR;
    }
  }
  return return_type::OK;
}

return_type DmSystemInterface::write_gravity_ff(const rclcpp::Time & time)
{
  // ── ① 用**实测姿态**算这一帧的重力项（hw_positions_ 就是模型坐标 q_urdf）──
  bool have_q = true;
  for (std::size_t i = 0; i < joints_.size(); ++i) {
    q_urdf_buf_[i] = hw_positions_[i];
    if (!std::isfinite(hw_positions_[i])) {have_q = false;}
  }
  if (have_q) {
    try {
      gravity_->tau_ours(q_urdf_buf_.data(), tau_ours_buf_.data());
      for (std::size_t i = 0; i < joints_.size(); ++i) {
        joints_[i].last_tau_ours = tau_ours_buf_[i];
      }
    } catch (const std::exception & e) {
      RCLCPP_ERROR_ONCE(get_logger(), "重力项算不出来（这一帧沿用上一次的值）：%s", e.what());
    }
  } else {
    // ⚠️ 这一帧**绝不能跳过不发**：500ms 看门狗一超时就锁存 ERR=13，只能断电清（陷阱 #24/#25）
    RCLCPP_WARN_ONCE(get_logger(),
      "还没有实测位置 ⇒ 重力项沿用上一次的值（首次为 0）；仍继续发帧喂狗");
  }

  // ── ② 残差守卫：越界就**锁存**并把 tau_ff 全部置 0（仍在 MIT + kp_hold 下，不失阻尼）──
  if (!gravity_guard_tripped_) {
    if (!gravity_guard_since_set_) {
      gravity_guard_since_ = time;
      gravity_guard_since_set_ = true;
    }
    const bool vel_checked = (time - gravity_guard_since_).seconds() >= kGravityGraceSec;

    bool vel_over = false;
    for (std::size_t i = 0; i < joints_.size(); ++i) {
      const JointParams & jp = joints_[i];
      if (!jp.hold_ours.has_value() || !std::isfinite(hw_positions_[i])) {continue;}
      // 位置：即时判 —— 越界是**持续**的，而且它才是"真的跑偏了"的证据
      const double dev_pos = std::fabs(hw_positions_[i] - ours_to_model(i, *jp.hold_ours));
      if (dev_pos > kGravityGuardRad) {
        gravity_guard_tripped_ = true;
        RCLCPP_ERROR(get_logger(),
          "‼ 残差守卫触发（位置）：关节 %s 偏离保持点 %.3f rad（阈值 %.2f）"
          " ⇒ **把 tau_ff 全部置 0**（仍在 MIT + kp_hold 下，不会失去阻尼）",
          jp.name.c_str(), dev_pos, kGravityGuardRad);
        break;
      }
      if (vel_checked) {
        const double vel = std::isfinite(hw_velocities_[i]) ? hw_velocities_[i] : 0.0;
        if (std::fabs(vel) > kGravityGuardRadPerS) {vel_over = true;}
      }
    }
    // 速度：要**持续**越限才算 —— 使能到第一次 write 之间 MIT 是零增益的，那一瞬机械臂真的在掉
    //（2026-10-05 实测 0.7~1.5 rad/s），一个采样就锁死会把整轮前馈白废掉。
    if (!gravity_guard_tripped_) {
      if (vel_over) {
        if (!gravity_vel_over_) {
          gravity_vel_over_ = true;
          gravity_vel_over_since_ = time;
        } else if ((time - gravity_vel_over_since_).seconds() >= kGravityVelDebounceSec) {
          gravity_guard_tripped_ = true;
          RCLCPP_ERROR(get_logger(),
            "‼ 残差守卫触发（速度）：连续 %.2f s 有连接续超过 %.2f rad/s（位置偏差仍在阈值内）"
            " ⇒ **把 tau_ff 全部置 0**（仍在 MIT + kp_hold 下，不会失去阻尼）",
            kGravityVelDebounceSec, kGravityGuardRadPerS);
        }
      } else {
        gravity_vel_over_ = false;
      }
    }
  }

  // ── ③ 逐关节发 MIT 帧 ──
  for (std::size_t i = 0; i < joints_.size(); ++i) {
    JointParams & jp = joints_[i];
    const double tau = gravity_guard_tripped_ ? 0.0 : gravity_ff_scale_ * jp.last_tau_ours;
    double q_cmd = 0.0;
    double dq_cmd = 0.0;
    if (std::isnan(hw_commands_[i])) {
      // 控制器没起 / 刚激活 ⇒ 保持帧：目标是**使能那一刻锁定**的关节侧位置（陷阱 #38 的教训）
      if (!jp.hold_ours.has_value()) {
        RCLCPP_ERROR(get_logger(), "关节 %s 没有锁定的保持目标，无法发 MIT 保持帧", jp.name.c_str());
        return return_type::ERROR;
      }
      q_cmd = *jp.hold_ours;
    } else {
      q_cmd = model_to_ours(i, hw_commands_[i]);
      // 速度是矢量：模型坐标 → 关节侧只乘 sign（不加 zero_shift）
      if (std::isfinite(hw_vel_commands_[i])) {
        dq_cmd = static_cast<double>(jp.sign) * hw_vel_commands_[i];
      }
    }
    try {
      jp.joint->set_mit(jp.kp_hold, jp.kd_hold, q_cmd, dq_cmd, tau);
    } catch (const std::exception & e) {
      RCLCPP_ERROR(get_logger(), "关节 %s 拒发 MIT 帧：%s", jp.name.c_str(), e.what());
      return return_type::ERROR;
    }
  }
  return return_type::OK;
}

}  // namespace motor_driver_hardware

#include <pluginlib/class_list_macros.hpp>   // NOLINT
#include <chrono>
#include <thread>
PLUGINLIB_EXPORT_CLASS(motor_driver_hardware::DmSystemInterface, hardware_interface::SystemInterface)
