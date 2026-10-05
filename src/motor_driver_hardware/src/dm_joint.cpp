// dm_joint.cpp —— 单关节实现（只走 POS_VEL；语义照 Python 的 joint.py）。
#include "motor_driver_hardware/dm_joint.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace motor_driver_hardware
{

namespace
{
constexpr double kEps = 1e-12;

bool limit_equal(const Limit & a, const Limit & b)
{
  return std::abs(a.p_max - b.p_max) < kEps && std::abs(a.v_max - b.v_max) < kEps &&
         std::abs(a.t_max - b.t_max) < kEps;
}

std::string hex_id(uint8_t motor_id)
{
  static const char * kDigits = "0123456789ABCDEF";
  std::string s = "0x";
  s += kDigits[(motor_id >> 4) & 0x0F];
  s += kDigits[motor_id & 0x0F];
  return s;
}

std::string mode_name(int mode)
{
  switch (mode) {
    case 1: return "1(MIT)";
    case 2: return "2(位置速度)";
    case 3: return "3(速度)";
    case 4: return "4(力位混控)";
    default: return std::to_string(mode) + "(?)";
  }
}
}  // namespace

Joint::Joint(MotorBus & bus, const JointConfig & cfg)
: bus_(bus), cfg_(cfg)
{
  if (cfg_.direction != 1 && cfg_.direction != -1) {
    throw std::invalid_argument(
      "关节 " + cfg_.name + "：direction 只能是 ±1，收到 " + std::to_string(cfg_.direction));
  }
  if (!(cfg_.limit.p_max > 0.0) || !(cfg_.limit.v_max > 0.0) || !(cfg_.limit.t_max > 0.0)) {
    throw std::invalid_argument(
      "关节 " + cfg_.name + "：档位 PMAX/VMAX/TMAX 必须都是正数（它们来自寄存器回读）");
  }
  if (cfg_.position_min.has_value() && cfg_.position_max.has_value() &&
    *cfg_.position_min > *cfg_.position_max)
  {
    throw std::invalid_argument("关节 " + cfg_.name + "：position_min 比 position_max 还大");
  }
  if (cfg_.mode != 1 && cfg_.mode != 2) {
    throw std::invalid_argument(
      "关节 " + cfg_.name + "：C++ 这一层只实现 mode 1(MIT) 与 mode 2(位置速度)，收到 " +
      mode_name(cfg_.mode) + "。轨迹执行走 2；需要力矩前馈的场合才走 1。");
  }
  if (cfg_.torque_max.has_value() && !(*cfg_.torque_max > 0.0)) {
    throw std::invalid_argument(
      "关节 " + cfg_.name + "：torque_max 必须是正数（它是要钳到的那条上限，"
      "不是档位里的 t_max=电机峰值）");
  }

  // 注册到总线：已注册且档位不同 ⇒ 拒（同一台电机两个映射范围 = 力矩静默差数倍）
  if (bus_.has_motor(cfg_.motor_id)) {
    const Limit & existing = bus_.limit(cfg_.motor_id);
    if (!limit_equal(existing, cfg_.limit)) {
      throw std::invalid_argument(
        "关节 " + cfg_.name + "：电机 " + hex_id(cfg_.motor_id) + " 在总线上已注册为另一组档位，拒绝构造");
    }
  } else {
    bus_.add_motor(cfg_.motor_id, cfg_.limit);
  }
}

// ── 换算与钳位 ──
double Joint::joint_to_motor(double joint_value) const
{
  return static_cast<double>(cfg_.direction) * joint_value + cfg_.offset;
}

double Joint::motor_to_joint(double motor_value) const
{
  return static_cast<double>(cfg_.direction) * (motor_value - cfg_.offset);
}

double Joint::clamp(double joint_value) const
{
  double v = joint_value;
  if (cfg_.position_min.has_value()) {v = std::max(v, *cfg_.position_min);}
  if (cfg_.position_max.has_value()) {v = std::min(v, *cfg_.position_max);}
  return v;
}

double Joint::clamp_pmax(double motor_value) const
{
  return std::max(-cfg_.limit.p_max, std::min(cfg_.limit.p_max, motor_value));
}

double Joint::prepare_frame(double joint_pos) const
{
  if (!std::isfinite(joint_pos)) {
    // NaN/inf 是软件错：拒发，但**不失能**（失能反而危险）
    throw std::invalid_argument("关节 " + cfg_.name + "：目标是 NaN/inf，拒发");
  }
  return clamp_pmax(joint_to_motor(clamp(joint_pos)));
}

double Joint::prepare_frame_raw(double joint_pos) const
{
  if (!std::isfinite(joint_pos)) {
    throw std::invalid_argument("关节 " + cfg_.name + "：目标是 NaN/inf，拒发");
  }
  // 不做 clamp()，但仍要 clamp_pmax()：MIT 帧的位置字段只有 16 位、只覆盖 ±PMAX
  return clamp_pmax(joint_to_motor(joint_pos));
}

// ── 发送 ──
void Joint::set_pos_vel(double joint_pos, double vlim)
{
  if (cfg_.mode != 2) {
    throw std::runtime_error(
      "关节 " + cfg_.name + "：声明模式是 " + mode_name(cfg_.mode) + "，不是 2(位置速度)，拒发");
  }
  if (!std::isfinite(joint_pos) || !std::isfinite(vlim)) {
    throw std::invalid_argument(
      "关节 " + cfg_.name + "：非有限数（pos 或 vlim 是 NaN/inf）");
  }
  if (vlim < 0.0) {
    throw std::invalid_argument(
      "关节 " + cfg_.name + "：vlim 是负的；协议里是无符号，负值会被钳成 0");
  }
  bus_.send_pos_vel(cfg_.motor_id, prepare_frame(joint_pos), vlim);
}

void Joint::set_mit(double kp, double kd, double joint_pos, double dq, double tau,
  bool bypass_soft_limits)
{
  if (cfg_.mode != 1) {
    throw std::runtime_error(
      "关节 " + cfg_.name + "：声明模式是 " + mode_name(cfg_.mode) + "，不是 1(MIT)，拒发 —— "
      "模式不符时帧会被电机**静默丢掉**（陷阱 #8）");
  }
  using Named = std::pair<const char *, double>;
  const Named vals[] = {{"kp", kp}, {"kd", kd}, {"q", joint_pos}, {"dq", dq}, {"tau", tau}};
  for (const auto & kv : vals) {
    if (!std::isfinite(kv.second)) {
      throw std::invalid_argument(
        std::string("关节 ") + cfg_.name + "：" + kv.first + " 是非有限数（NaN/inf），拒发");
    }
  }
  const double q_motor = bypass_soft_limits ? prepare_frame_raw(joint_pos) : prepare_frame(joint_pos);
  const double dq_motor = static_cast<double>(cfg_.direction) * dq;
  // 力矩是矢量：只乘 direction，不加 offset（与 Python 一致）
  double tau_motor = static_cast<double>(cfg_.direction) * tau;
  if (cfg_.torque_max.has_value()) {
    tau_motor = clamp_mit_torque(kp, kd, q_motor, dq_motor, tau_motor);
  }
  bus_.send_mit(cfg_.motor_id, q_motor, dq_motor, kp, kd, tau_motor);
}

double Joint::clamp_mit_torque(double kp, double kd, double q_des, double dq_des,
  double tau_ff) const
{
  const auto m = bus_.get_state(cfg_.motor_id);
  if (!m.has_value()) {
    // 算不出来就别装作算过了
    throw std::runtime_error(
      "关节 " + cfg_.name + "：没有缓存位置/速度，预测不了力矩（torque_max=" +
      std::to_string(*cfg_.torque_max) + "）—— 先 poll() 拿到反馈再发 MIT 帧");
  }
  const double limit = *cfg_.torque_max;
  const double pd = kp * (q_des - m->pos) + kd * (dq_des - m->vel);
  if (std::abs(pd) > limit) {
    std::ostringstream os;
    os << "关节 " << cfg_.name << "：PD 项 " << pd << " N·m 已超过 torque_max=" << limit
       << "（kp=" << kp << ", kd=" << kd << ", q_des−q=" << (q_des - m->pos)
       << " rad, dq_des−dq=" << (dq_des - m->vel) << " rad/s）—— 钳 tau_ff 救不回来，拒发；"
       << "要么降 kp/kd，要么把目标挪近";
    throw std::runtime_error(os.str());
  }
  const double total = pd + tau_ff;
  if (std::abs(total) > limit) {
    // 钳是单调安全的（只会让力矩更小）⇒ 钳 + 限流打日志，不像 PD 超限那样拒发
    const double clamped = std::copysign(limit - std::abs(pd), total);
    const double now_s =
      std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if (now_s - clamp_warn_at_ >= kClampWarnInterval) {
      clamp_warn_at_ = now_s;
      std::fprintf(stderr,
        "[%s] tau_ff 被钳：%+.3f → %+.3f N·m（PD %+.3f + tau_ff 会到 %+.3f，超 torque_max %g）\n",
        cfg_.name.c_str(), tau_ff, clamped, pd, total, limit);
    }
    return clamped;
  }
  return tau_ff;
}

void Joint::enable()
{
  const auto st = bus_.get_state(cfg_.motor_id);
  if (cfg_.mode == 2 && st == std::nullopt) {
    throw std::runtime_error(
      "关节 " + cfg_.name + "：位置类模式(2)下没有缓存位置，拒绝使能 —— "
      "切模式时电机内部指令被清零，使能后会朝零位冲。先让总线 poll 一轮拿到位置。");
  }
  try {
    bus_.send_enable(cfg_.motor_id);
    if (cfg_.mode == 1) {
      // MIT 的保持帧是**零增益零前馈** ⇒ 出力恒为 0，不需要知道当前位置（与 Python 一致）。
      // 真正托住要由调用方随后发带 tau_ff 的 MIT 帧。
      bus_.send_mit(cfg_.motor_id, 0.0, 0.0, 0.0, 0.0, 0.0);
    } else {
      // 保持帧用**电机侧实测值**，不经过 clamp/换算：钳了就不是"保持"
      bus_.send_pos_vel(cfg_.motor_id, st->pos, kHoldVlim);
    }
  } catch (...) {
    try {
      bus_.send_disable(cfg_.motor_id);
    } catch (...) {
      // 失能也失败：这里不吞掉原异常，交给上层（上层要打日志/报警）
    }
    throw;
  }
}

void Joint::disable()
{
  bus_.send_disable(cfg_.motor_id);
}

// ── 状态 ──
bool Joint::has_state() const
{
  return bus_.get_state(cfg_.motor_id).has_value();
}

JointState Joint::get_state() const
{
  const auto st = bus_.get_state(cfg_.motor_id);
  if (st == std::nullopt) {
    throw std::runtime_error(
      "关节 " + cfg_.name + "(" + hex_id(cfg_.motor_id) + ") 还没收到过反馈帧 —— "
      "先让总线 poll 拿到状态");
  }
  JointState out;
  out.name = cfg_.name;
  out.motor_id = cfg_.motor_id;
  out.err = st->err;
  out.position = motor_to_joint(st->pos);
  out.velocity = static_cast<double>(cfg_.direction) * st->vel;   // 矢量：只乘 direction
  out.torque = static_cast<double>(cfg_.direction) * st->tau;     // 同上，不加 offset
  out.t_mos_raw = st->t_mos_raw;
  out.t_rotor_raw = st->t_rotor_raw;
  out.enabled = (st->err == 0x1);
  out.stamp = st->stamp;
  return out;
}

void Joint::assert_healthy() const
{
  const JointState st = get_state();     // 没反馈 ⇒ 直接抛，不吞
  if (st.err == 0x0 || st.err == 0x1) {return;}
  try {
    bus_.send_disable(cfg_.motor_id);
  } catch (...) {
    // 失能失败也照抛故障：上层看到异常就知道要断电
  }
  const char * text = err_text(st.err);
  throw std::runtime_error(
    "关节 " + cfg_.name + "(" + hex_id(cfg_.motor_id) + ") 故障 ERR=" + std::to_string(st.err) +
    "（" + (text != nullptr ? std::string(text) : std::string("未知错误码")) + "），已发失能。" +
    "ERR=13(通讯丢失) 是**锁存**的，只能断电再上电清除；失能只是让电机松掉，不会让它停下 —— "
    "重力负载下它会掉");
}

}  // namespace motor_driver_hardware
