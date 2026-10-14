// gravity_ff.cpp —— 前馈控制律实现。顺序固定为：判守卫 → 算 τ_g → 积分 + 摩擦 → 相加。
#include "arm_controllers/gravity_ff.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace arm_controllers
{

namespace
{
constexpr double kIntegResetRad = 0.05;   // 参考单周期跳变超过它 ⇒ 复位积分
constexpr double kIntegGateRad = 0.15;    // 误差门控：|q_ref−q| 超过它就不积分

bool finite(double v) {return std::isfinite(v);}
}  // namespace

GravityFfLaw::GravityFfLaw(const std::string & urdf_path,
  const std::vector<std::string> & joint_names, const std::vector<int> & sign)
: n_(joint_names.size()), gravity_(urdf_path, joint_names, sign), sign_(sign)
{
  cfg_.assign(n_, JointCfg{});
  q_prev_ref_.assign(n_, 0.0);
  has_prev_ref_.assign(n_, 0);
  hold_model_.assign(n_, 0.0);
  has_hold_.assign(n_, 0);
  tau_buf_.assign(n_, 0.0);
}

void GravityFfLaw::set_scale(double scale)
{
  if (!finite(scale) || scale < 0.0 || scale > 1.0) {
    throw std::invalid_argument(
      "GravityFfLaw：scale 必须落在 [0, 1]（只用来分级上电，放大前馈没有正当用途）");
  }
  scale_ = scale;
}

void GravityFfLaw::set_integrator(std::size_t i, double ki, double limit_frac)
{
  if (i >= n_) {throw std::out_of_range("GravityFfLaw::set_integrator 下标越界");}
  if (!finite(ki) || ki < 0.0) {
    throw std::invalid_argument("GravityFfLaw：ki 必须是非负数");
  }
  if (!finite(limit_frac) || limit_frac < 0.0) {
    throw std::invalid_argument("GravityFfLaw：积分上限比例必须是非负数");
  }
  cfg_[i].ki = ki;
  cfg_[i].integ_limit_frac = limit_frac;
  refresh_integ_limits();
}

void GravityFfLaw::set_torque_max(std::size_t i, double tau_max)
{
  if (i >= n_) {throw std::out_of_range("GravityFfLaw::set_torque_max 下标越界");}
  if (!finite(tau_max)) {throw std::invalid_argument("GravityFfLaw：torque_max 不是有限数");}
  cfg_[i].tau_max = tau_max;
  refresh_integ_limits();
}

// τ_i 上限 = frac × torque_max。没给 torque_max（≤0）⇒ 用 0 ⇒ 积分器被钳成 0（不生效），
// 这是**有意的保守失败**：算不出合理的饱和值就不要积分（否则容易极限环，陷阱 #50）。
void GravityFfLaw::refresh_integ_limits()
{
  for (auto & c : cfg_) {
    c.tau_i_limit_ = (c.tau_max > 0.0) ? (c.integ_limit_frac * c.tau_max) : 0.0;
  }
}

void GravityFfLaw::set_friction(std::size_t i, double coulomb, double vel_eps)
{
  if (i >= n_) {throw std::out_of_range("GravityFfLaw::set_friction 下标越界");}
  if (!finite(coulomb) || coulomb < 0.0) {
    throw std::invalid_argument("GravityFfLaw：friction_c 必须是非负数");
  }
  if (coulomb > 0.0 && !(vel_eps > 0.0)) {
    throw std::invalid_argument(
      "GravityFfLaw：开了 friction_c 就必须给正的 friction_v_eps（它是 tanh 的速度尺度，给 0 会除零）");
  }
  cfg_[i].friction_c = coulomb;
  cfg_[i].friction_v_eps = vel_eps;
}

void GravityFfLaw::on_activate(const std::vector<double> & q_model)
{
  if (q_model.size() != n_) {throw std::invalid_argument("GravityFfLaw::on_activate 长度不对");}
  for (std::size_t i = 0; i < n_; ++i) {
    hold_model_[i] = q_model[i];
    has_hold_[i] = 1;
    cfg_[i].tau_i = 0.0;
    has_prev_ref_[i] = 0;
  }
  guard_tripped_ = false;
  trip_reason_.clear();
  since_set_ = false;
  since_sec_ = 0.0;
  vel_over_ = false;
  vel_over_since_sec_ = 0.0;
}

void GravityFfLaw::note_reference(const std::vector<double> & q_ref_model)
{
  if (q_ref_model.size() != n_) {
    throw std::invalid_argument("GravityFfLaw::note_reference 长度不对");
  }
  for (std::size_t i = 0; i < n_; ++i) {
    const double r = q_ref_model[i];
    if (!finite(r)) {continue;}
    // 参考**单周期跳变**超过阈值 ⇒ 轨迹刚起步 / 从保持切到命令，别把旧积分带过去
    if (has_prev_ref_[i] && std::fabs(r - q_prev_ref_[i]) > kIntegResetRad) {
      cfg_[i].tau_i = 0.0;
    }
    q_prev_ref_[i] = r;
    has_prev_ref_[i] = 1;
  }
}

FfResult GravityFfLaw::compute(const std::vector<double> & q_model,
  const std::vector<double> & dq_model, const std::vector<double> & q_ref_model,
  const std::vector<double> & dq_ref_model, double elapsed_sec, double now_sec)
{
  FfResult out;
  out.tau.assign(n_, 0.0);
  if (q_model.size() != n_ || dq_model.size() != n_ || q_ref_model.size() != n_ ||
    dq_ref_model.size() != n_)
  {
    out.reason = "compute(): 输入向量长度不对";
    return out;
  }
  // ① 姿态不能有非有限数：宁可本周期不写 effort，也不要写 NaN 力矩出去
  for (std::size_t i = 0; i < n_; ++i) {
    if (!finite(q_model[i]) || !finite(dq_model[i])) {
      out.reason = "实测姿态里有非有限数";
      return out;
    }
  }

  // ② 守卫（**锁存**）。触发后仍然返回 valid=true、但 τ 全 0 —— 这样硬件继续出 PD 与阻尼，
  //    不会因为"不发帧"而掉进 500 ms 看门狗（陷阱 #24/#25）。
  if (!guard_tripped_) {
    if (!since_set_) {
      since_sec_ = now_sec;
      since_set_ = true;
    }
    const bool vel_checked = (elapsed_sec >= guard_.grace_sec);
    bool vel_over = false;
    for (std::size_t i = 0; i < n_; ++i) {
      const bool holding = !finite(q_ref_model[i]);
      // 参照点分两种语义（否则**一跑轨迹就误触发**，机械臂本来就要离开激活姿态）：
      //   保持 ⇒ 比"偏离激活时锁定的保持点"；有命令 ⇒ 比"跟踪误差"（命令 − 实测）
      const double ref = holding ? hold_model_[i] : q_ref_model[i];
      const double lim = holding ? guard_.hold_pos_rad : guard_.track_rad;
      const double dev = std::fabs(q_model[i] - ref);
      // 位置：**即时判** —— 越界是持续的，而且它才是"真的跑偏了"的证据
      if (dev > lim) {
        guard_tripped_ = true;
        trip_reason_ = std::string(holding ? "保持态偏离保持点 " : "跟踪误差 ") +
          std::to_string(dev) + " rad > " + std::to_string(lim);
        break;
      }
      // 速度只在**保持**态查：跑轨迹时机械臂本来就以 ~1 rad/s 在动（阈值 0.5 会误触发）
      if (vel_checked && holding && std::fabs(dq_model[i]) > guard_.hold_vel_rad_s) {
        vel_over = true;
      }
    }
    if (!guard_tripped_ && vel_over) {
      // 速度要**持续**越限才算：使能到第一次 update 之间 MIT 是零增益的，那一瞬机械臂真的在掉
      if (!vel_over_) {
        vel_over_ = true;
        vel_over_since_sec_ = now_sec;
      } else if (now_sec - vel_over_since_sec_ >= guard_.vel_debounce_sec) {
        guard_tripped_ = true;
        trip_reason_ = "保持态速度持续超过 " + std::to_string(guard_.hold_vel_rad_s) + " rad/s";
      }
    } else if (!vel_over) {
      vel_over_ = false;
    }
  }

  // ③ τ_g（关节侧，已套 sign）。算不出来 ⇒ 本周期不写 effort
  try {
    gravity_.tau_ours(q_model.data(), tau_buf_.data());
  } catch (const std::exception & e) {
    out.reason = std::string("重力项算不出来：") + e.what();
    return out;
  }

  // ④ 积分 + 摩擦 + 相加。守卫触发 ⇒ τ 全 0（但仍 valid，让硬件照常出 PD）
  for (std::size_t i = 0; i < n_; ++i) {
    JointCfg & c = cfg_[i];
    const bool holding = !finite(q_ref_model[i]);
    const double q_ref = holding ? hold_model_[i] : q_ref_model[i];

    if (guard_tripped_) {
      c.tau_i = 0.0;                      // 复位条件之一：守卫触发
      continue;
    }

    // 积分：误差与力矩都在**关节侧**（参考是模型坐标 ⇒ 差值乘 sign 换过去）
    if (c.ki > 0.0 && has_hold_[i]) {
      const double err_joint = static_cast<double>(sign_[i]) * (q_ref - q_model[i]);
      // ⚠️ `tau_i_limit_` 在 set_torque_max/set_integrator 里**已经**乘过 frac ⇒ 这里别再乘一次
      //    （双重相乘会让实际上限变成 frac²·torque_max，测试里表现为饱和值只有 1/10）。
      const double limit = c.tau_i_limit_;
      if (std::fabs(err_joint) < kIntegGateRad && dt_ > 0.0 && limit > 0.0) {
        const double next = c.tau_i + c.ki * err_joint * dt_;
        // 抗饱和：已在饱和、误差还往同方向推 ⇒ **停在饱和值**（不是"保持当前值"）。
        // ⚠️ 写成"保持当前值"是错的：**单步增量大于上限余量**时（大 ki / 大 dt / 小上限），
        //    积分器会**冻结在上限之下**、再也到不了饱和值 —— 测试里实测到
        //    τ_i 卡在 −0.05（limit=0.09），而不是收敛到 −0.09。
        const bool pushing =
          (next > limit && err_joint > 0.0) || (next < -limit && err_joint < 0.0);
        c.tau_i = pushing
          ? (err_joint > 0.0 ? limit : -limit)
          : std::max(-limit, std::min(limit, next));
      }
    } else {
      c.tau_i = 0.0;
    }

    // 库仑摩擦前馈：**只在有速度参考时**给（静止时摩擦方向不确定，猜错等于推着关节走）。
    // ⚠️ 正号：摩擦力矩**反对**速度 ⇒ 要抵消就必须给 +F_c·tanh(v/eps)。
    //    第一版写成负号 = 把摩擦**加倍**，是真机测出来的（单测也一起编错了同一个假设）。
    double tau_fric = 0.0;
    const double dq_ref = dq_ref_model[i];
    if (!holding && finite(dq_ref) && c.friction_c > 0.0 && c.friction_v_eps > 0.0) {
      tau_fric = c.friction_c * std::tanh(dq_ref / c.friction_v_eps);
    }

    out.tau[i] = scale_ * tau_buf_[i] + c.tau_i + tau_fric;
  }

  out.valid = true;
  out.guard_tripped = guard_tripped_;
  out.reason = trip_reason_;
  return out;
}

}  // namespace arm_controllers
