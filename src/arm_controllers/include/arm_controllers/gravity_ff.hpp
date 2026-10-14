// gravity_ff.hpp —— **前馈量的控制律**（纯逻辑；R2 从硬件层的 `write_gravity_ff()` 那 4 个函数迁来）。
//
// 这是控制层唯一"会算数"的地方：给实测姿态 + 参考位置/速度，算出该写进 `effort` 命令接口的
// **前馈力矩**（关节侧 N·m）。硬件层只出 PD（`kp_hold`/`kd_hold`）并把 `effort` 当 `tau_ff`
// 叠上去（D-L2）⇒ 本层**不重复**算 PD，也**不做力矩钳位**（那是硬件的物理边界，LAYERS §5）。
//
// 边界（LAYERS §2）：
//   - 不 import rclcpp、不碰串口、不做 I/O ⇒ `test_gravity_ff.cpp` 只喂数值、只看输出；
//   - 不拥有线程、不 sleep；
//   - 只在**构造/配置期**分配；`compute()` 是实时路径（100 Hz，无堆分配）。
//
// 三项相加（与真机 2026-10-05 跑通的那条链同构）：
//   τ_ff = scale · τ_g(q)  +  τ_i(宿主侧积分)  +  τ_fric(库仑摩擦前馈)
//   ⚠️ 摩擦项**只在有速度参考时**给：静止时摩擦方向不确定，猜错等于推着关节走。
//   ⚠️ `effort` 是**前馈**不是总力矩：总力矩 = 硬件的 PD + 本层的 τ_ff（硬件再按 `torque_max` 钳）。
#ifndef ARM_CONTROLLERS__GRAVITY_FF_HPP_
#define ARM_CONTROLLERS__GRAVITY_FF_HPP_

#include <cstddef>
#include <string>
#include <vector>

#include "arm_controllers/gravity_model.hpp"

namespace arm_controllers
{

// 一帧的前馈结果。`valid=false` ⇒ 调用方**本周期不要写 effort**（保持上一次的值）。
struct FfResult
{
  std::vector<double> tau;          // 每关节的**关节侧**前馈力矩（N·m）
  bool valid = false;               // 姿态可用且重力项算出来了
  bool guard_tripped = false;       // 本帧（或之前）处于守卫触发态 ⇒ τ 全 0
  std::string reason;               // 守卫触发原因（给控制器打日志用；空 = 没触发）
};

// 守卫的策略（LAYERS §5 / D-L3：**策略**在控制层，硬件层只留"超时/ERR ⇒ REPORT ERROR"的物理兜底）。
// 阈值与宽限都按"保持 / 跑轨迹"分开判（陷阱 #42）—— 否则一跑轨迹就误触发。
struct GuardParams
{
  double hold_pos_rad = 0.1;        // 保持态：偏离保持点超过它 ⇒ 触发
  double hold_vel_rad_s = 0.5;      // 保持态：速度超过它（需持续 vel_debounce_sec）⇒ 触发
  double track_rad = 0.3;           // 跑轨迹态：只查跟踪误差（位置即时判）
  double grace_sec = 0.5;           // 激活后多久内不查速度（使能瞬间机械臂真的在掉）
  double vel_debounce_sec = 0.2;    // 速度越限需持续多久才算
};

// `kp` 上界检查（陷阱 #51）：`kp × 期望滞后` 一旦超过 `torque_max`，硬件那边"PD 项自己超"
// 是**拒发** ⇒ write() 报 ERROR ⇒ CM 失能 ⇒ **掉臂**。所以这是配置期就该拦住的事。
// 返回允许的最大 kp（`tau_max`/`lag`）；`tau_max <= 0` ⇒ 不限制（返回 +inf）。
inline double kp_upper_bound(double tau_max, double expected_lag_rad)
{
  if (!(tau_max > 0.0)) {return 0.0;}                 // 0 表示"不限制"，见调用方
  if (!(expected_lag_rad > 0.0)) {return 0.0;}
  return tau_max / expected_lag_rad;
}

class GravityFfLaw
{
public:
  // `joint_names` 按调用方自己的顺序；`sign` 一一对应（±1）。模型建不起来 ⇒ 抛（大声失败）。
  GravityFfLaw(const std::string & urdf_path, const std::vector<std::string> & joint_names,
    const std::vector<int> & sign);

  std::size_t size() const {return n_;}

  // ── 逐关节参数（配置期设，之后只读）──
  void set_scale(double scale);                              // τ_g 的缩放（分级上电 0~1）
  void set_integrator(std::size_t i, double ki, double limit_frac);
  void set_friction(std::size_t i, double coulomb, double vel_eps);
  // 力矩上限：**只用来算积分器的饱和值**（真正的力矩钳位在硬件层，LAYERS §5）。
  void set_torque_max(std::size_t i, double tau_max);
  void set_guard(const GuardParams & g) {guard_ = g;}
  void set_dt(double dt) {dt_ = dt;}

  // ── 逐周期 ──
  // 激活时调用：把"保持点"锁成当前位置（陷阱 #38：锁定而不是每圈重读），并复位守卫与积分。
  // `q_model` 的长度必须等于 size()；内部**不分配**（缓冲在构造时建好）。
  void on_activate(const std::vector<double> & q_model);
  // 参考位置**跳变**（轨迹刚起步 / 从保持切到命令）时复位积分，别把旧积分带过去。
  void note_reference(const std::vector<double> & q_ref_model);

  // 算这一帧的前馈。
  //   q_model / dq_model : **实测**（模型坐标）
  //   q_ref_model        : 参考位置（模型坐标）。NaN ⇒ 该关节视为"保持"（用 reset() 记的保持点）
  //   dq_ref_model       : 参考速度（模型坐标）。NaN ⇒ 该关节摩擦项给 0
  //   elapsed_sec        : 激活以来的秒数（守卫宽限从这里算）
  //   now_sec            : 单调时钟秒（守卫防抖从这里算）
  FfResult compute(const std::vector<double> & q_model, const std::vector<double> & dq_model,
    const std::vector<double> & q_ref_model, const std::vector<double> & dq_ref_model,
    double elapsed_sec, double now_sec);

  bool guard_tripped() const {return guard_tripped_;}

private:
  struct JointCfg
  {
    double ki = 0.0;
    double integ_limit_frac = 0.3;   // τ_i 上限 = frac × torque_max
    double tau_i_limit_ = 0.0;       // 实际的 τ_i 上限（由 set_torque_max/set_integrator 算出）
    double friction_c = 0.0;
    double friction_v_eps = 0.0;
    double tau_max = 0.0;            // 关节侧上限（N·m），≤0 = 只用来算积分饱和
    double tau_i = 0.0;              // 宿主侧积分器状态
  };

  // τ_i 上限 = integ_limit_frac × tau_max_i（tau_max 变了要重算）
  void refresh_integ_limits();

  std::size_t n_ = 0;
  GravityModel gravity_;
  std::vector<JointCfg> cfg_;
  std::vector<int> sign_;
  std::vector<double> q_ref_buf_;    // 预分配（模型坐标）
  std::vector<double> q_prev_ref_;   // 上一次的参考（判跳变）
  std::vector<char> has_prev_ref_;
  std::vector<double> hold_model_;   // 激活那一刻锁定的**模型坐标**保持点
  std::vector<char> has_hold_;
  std::vector<double> tau_buf_;      // τ_g 的输出缓冲（关节侧）

  GuardParams guard_;
  double scale_ = 1.0;
  double dt_ = 0.01;
  bool guard_tripped_ = false;
  std::string trip_reason_;
  bool since_set_ = false;
  double since_sec_ = 0.0;
  bool vel_over_ = false;
  double vel_over_since_sec_ = 0.0;
};

}  // namespace arm_controllers

#endif  // ARM_CONTROLLERS__GRAVITY_FF_HPP_
