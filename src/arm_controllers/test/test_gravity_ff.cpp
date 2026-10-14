// test_gravity_ff.cpp —— 前馈控制律的**纯逻辑**测试（LAYERS §8.1：只喂数值、只看输出）。
//
// 这里没有 rclcpp、没有串口、没有假电机：`GravityFfLaw` 的全部判定都靠输入与返回值表达。
// 守住的是从硬件层迁过来的那几条真机教训：
//   · 摩擦前馈**只在有速度参考时**给（静止时方向不确定，猜错等于推着关节走）；
//   · 守卫按"保持 / 跑轨迹"**分开判** + 宽限 + 防抖 + **锁存**（陷阱 #42）；
//   · 姿态有 NaN ⇒ 本周期**不写 effort**（宁可不动，也不发 NaN 力矩）；
//   · 积分有**门控**与**饱和**（大 ki 会极限环，陷阱 #50）。
#include <gtest/gtest.h>

#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "arm_controllers/gravity_ff.hpp"
#include "arm_controllers/gravity_model.hpp"

#ifndef ARM_DYN_URDF
#error "缺少 ARM_DYN_URDF（应由 CMake target_compile_definitions 传入）"
#endif

namespace ac = arm_controllers;

namespace
{
const std::vector<std::string> kJoints = {
  "joint1", "joint2", "joint3", "joint4", "joint5", "joint6"};
// align.yaml 的真实值：j1~j5 direction/sign = −1、j6 = +1
const std::vector<int> kSign = {-1, -1, -1, -1, -1, 1};
const double kNaN = std::numeric_limits<double>::quiet_NaN();

// 一个"什么都关掉"的律：只有 τ_g × scale。用它把重力那一段单独隔离出来。
ac::GravityFfLaw make_plain(double scale = 1.0)
{
  ac::GravityFfLaw law(ARM_DYN_URDF, kJoints, kSign);
  law.set_scale(scale);
  law.set_dt(0.01);
  return law;
}

std::vector<double> zeros() {return std::vector<double>(6, 0.0);}

std::vector<double> real_pose()
{
  return {1.4709, -0.5528, -0.7828, 0.6176, -0.0088, 0.0111};   // 真机实测张开位
}
}  // namespace

// ① τ_ff 就是 τ_g（模型坐标 → 关节侧只乘 sign）；与 GravityModel 逐位一致
TEST(GravityFf, IsGravityTimesSignWhenEverythingElseOff)
{
  const auto q = real_pose();
  const auto dq = zeros();
  const auto q_ref = q;            // 有命令（不是保持）⇒ 摩擦关掉时 τ_ff 就是 τ_g
  const auto dq_ref = zeros();

  ac::GravityFfLaw law = make_plain(1.0);
  law.on_activate(q);
  const auto out = law.compute(q, dq, q_ref, dq_ref, 1.0, 0.0);
  ASSERT_TRUE(out.valid);
  EXPECT_FALSE(out.guard_tripped);

  ac::GravityModel gm(ARM_DYN_URDF, kJoints, kSign);
  std::vector<double> ref(6, 0.0);
  gm.tau_ours(q.data(), ref.data());
  for (std::size_t i = 0; i < 6; ++i) {
    EXPECT_NEAR(out.tau[i], ref[i], 1e-12) << kJoints[i];
  }
  // j6 的 sign 是 +1、j2 是 −1 ⇒ 两者必须**反号**（这条如果写错，符号链就断了）
  const std::vector<int> all_plus(6, 1);
  ac::GravityModel gp(ARM_DYN_URDF, kJoints, all_plus);
  std::vector<double> ref_plus(6, 0.0);
  gp.tau_ours(q.data(), ref_plus.data());
  EXPECT_NEAR(out.tau[5], ref_plus[5], 1e-12) << "j6 sign=+1";
  EXPECT_NEAR(out.tau[1], -ref_plus[1], 1e-12) << "j2 sign=−1";
}

// ② scale 是线性缩放；越界当场拒（只允许减弱）
TEST(GravityFf, ScaleIsLinearAndBounded)
{
  const auto q = real_pose();
  const auto z = zeros();
  ac::GravityFfLaw full = make_plain(1.0);
  ac::GravityFfLaw half = make_plain(0.5);
  full.on_activate(q);
  half.on_activate(q);
  const auto a = full.compute(q, z, q, z, 1.0, 0.0);
  const auto b = half.compute(q, z, q, z, 1.0, 0.0);
  ASSERT_TRUE(a.valid);
  ASSERT_TRUE(b.valid);
  for (std::size_t i = 0; i < 6; ++i) {
    EXPECT_NEAR(b.tau[i], 0.5 * a.tau[i], 1e-12) << kJoints[i];
  }
  EXPECT_THROW(make_plain(1.5), std::invalid_argument);
  EXPECT_THROW(make_plain(-0.1), std::invalid_argument);
}

// ③ 摩擦前馈：**只有速度参考**才给；保持（NaN）时一律 0；而且是**正号**（抵消反向摩擦）
TEST(GravityFf, FrictionOnlyWithVelocityReference)
{
  const auto q = real_pose();
  const auto z = zeros();
  ac::GravityFfLaw law = make_plain(0.0);       // 关掉重力，只看摩擦
  law.set_friction(0, /*coulomb=*/0.4, /*vel_eps=*/0.02);
  law.on_activate(q);

  // 有速度参考且非零 ⇒ +c·tanh(v/eps)
  {
    auto q_ref = q;
    auto dq_ref = z;
    dq_ref[0] = 0.5;                            // eps 的 25 倍 ⇒ tanh ≈ 1
    const auto out = law.compute(q, z, q_ref, dq_ref, 1.0, 0.0);
    ASSERT_TRUE(out.valid);
    EXPECT_NEAR(out.tau[0], 0.4 * std::tanh(0.5 / 0.02), 1e-12);
    EXPECT_GT(out.tau[0], 0.0) << "必须是正号（摩擦力矩反对速度）";
  }
  // 反方向速度 ⇒ 反号
  {
    auto dq_ref = z;
    dq_ref[0] = -0.5;
    const auto out = law.compute(q, z, q, dq_ref, 1.0, 0.0);
    ASSERT_TRUE(out.valid);
    EXPECT_NEAR(out.tau[0], -0.4 * std::tanh(0.5 / 0.02), 1e-12);
  }
  // 保持（参考是 NaN）⇒ 摩擦一律 0，哪怕实测速度很大
  {
    auto q_ref = q;
    q_ref[0] = kNaN;
    auto dq_ref = z;
    dq_ref[0] = 1.0;
    auto dq_meas = z;
    dq_meas[0] = 1.0;
    const auto out = law.compute(q, dq_meas, q_ref, dq_ref, 1.0, 0.0);
    ASSERT_TRUE(out.valid);
    EXPECT_DOUBLE_EQ(out.tau[0], 0.0) << "保持时摩擦方向不确定 ⇒ 必须给 0";
  }
}

// ④ 姿态里有 NaN/inf ⇒ valid=false（调用方本周期**不写 effort**），绝不发 NaN 力矩
TEST(GravityFf, NonFinitePoseIsNotValid)
{
  const auto q = real_pose();
  const auto z = zeros();
  ac::GravityFfLaw law = make_plain(1.0);
  law.on_activate(q);

  for (double bad : {kNaN, std::numeric_limits<double>::infinity()}) {
    auto q_bad = q;
    q_bad[3] = bad;
    const auto out = law.compute(q_bad, z, q, z, 1.0, 0.0);
    EXPECT_FALSE(out.valid) << "坏姿态必须 valid=false，而不是给个 NaN 力矩";
    EXPECT_FALSE(out.reason.empty());
  }
  // 输入长度不对也拒
  {
    const auto out = law.compute({1.0, 2.0}, z, q, z, 1.0, 0.0);
    EXPECT_FALSE(out.valid);
  }
}

// ⑤ 守卫·位置：保持态偏离保持点 > 阈值 ⇒ 立刻触发并**锁存**；触发后 τ 全 0 但仍 valid
TEST(GravityFf, GuardTripsOnHoldPositionDeviationAndLatches)
{
  auto q = real_pose();
  const auto z = zeros();
  ac::GravityFfLaw law = make_plain(1.0);
  ac::GuardParams g;
  g.hold_pos_rad = 0.1;
  g.track_rad = 0.3;
  g.grace_sec = 0.0;
  law.set_guard(g);
  law.on_activate(q);                             // 保持点 = 当前姿态

  // 保持（q_ref 全 NaN）且只偏 0.05 rad ⇒ 不触发
  {
    auto q_meas = q;
    q_meas[0] += 0.05;
    auto q_ref = z;
    for (auto & v : q_ref) {v = kNaN;}
    const auto out = law.compute(q_meas, z, q_ref, z, 1.0, 0.0);
    ASSERT_TRUE(out.valid);
    EXPECT_FALSE(out.guard_tripped);
  }
  // 偏 0.2 rad ⇒ 触发
  {
    auto q_meas = q;
    q_meas[0] += 0.2;
    auto q_ref = z;
    for (auto & v : q_ref) {v = kNaN;}
    const auto out = law.compute(q_meas, z, q_ref, z, 1.0, 0.1);
    EXPECT_TRUE(out.guard_tripped);
    EXPECT_FALSE(out.reason.empty());
    for (std::size_t i = 0; i < 6; ++i) {EXPECT_DOUBLE_EQ(out.tau[i], 0.0);}
  }
  // 锁存：回到保持点也不再给前馈
  {
    auto q_ref = z;
    for (auto & v : q_ref) {v = kNaN;}
    const auto out = law.compute(q, z, q_ref, z, 1.0, 10.0);
    EXPECT_TRUE(out.guard_tripped) << "守卫必须锁存";
    for (std::size_t i = 0; i < 6; ++i) {EXPECT_DOUBLE_EQ(out.tau[i], 0.0);}
  }
}

// ⑥ 守卫·轨迹态**只查跟踪误差**，不查"离激活点多远"。
//    ⚠️ 这一条正是陷阱 #42：按"偏离激活姿态"判的话，一跑轨迹就误触发（机械臂本来就要离开）。
//    注意"跟踪误差"= 命令 − 实测。机械臂**跟得上**命令时，哪怕已经离激活点很远也不该触发；
//    命令跳变而实测没跟上时，误差真的超了 ⇒ 该触发。
TEST(GravityFf, GuardInTrackingModeOnlyChecksTrackingError)
{
  const auto q = real_pose();
  const auto z = zeros();
  ac::GravityFfLaw law = make_plain(1.0);
  ac::GuardParams g;
  g.hold_pos_rad = 0.1;
  g.track_rad = 0.3;
  g.grace_sec = 1e9;                              // 本用例只关心位置那一路
  law.set_guard(g);
  law.on_activate(q);

  // ① 已经飞出激活点 0.5 rad（远超 hold 阈值 0.1），但**跟得上**命令（误差 0）⇒ 不该触发
  auto q_ref = q;
  q_ref[0] += 0.5;
  const auto out = law.compute(q_ref, z, q_ref, z, 1.0, 0.0);
  ASSERT_TRUE(out.valid);
  EXPECT_FALSE(out.guard_tripped) << "跑轨迹态不该按'偏离保持点'判（陷阱 #42 回归）";

  // ② 跟得上时，跟踪误差一路都在阈值内 ⇒ 连走多帧也不触发
  for (int k = 0; k < 20; ++k) {
    auto q_ref_k = q_ref;
    q_ref_k[0] += 0.01 * k;                       // 命令缓慢移动
    const auto o = law.compute(q_ref_k, z, q_ref_k, z, 1.0, 0.1 * k);
    ASSERT_TRUE(o.valid);
    EXPECT_FALSE(o.guard_tripped) << "跟得上就不该触发（第 " << k << " 帧）";
  }

  // ③ 命令**跳变**而实测没跟上 ⇒ 跟踪误差 0.5 > 0.3 ⇒ 触发
  auto q_ref_jump = q;
  q_ref_jump[0] += 0.5;
  const auto out2 = law.compute(q, z, q_ref_jump, z, 1.0, 9.0);
  EXPECT_TRUE(out2.guard_tripped) << "跟踪误差 0.5 > 0.3 该触发";
  EXPECT_NE(out2.reason.find("跟踪误差"), std::string::npos) << "原因里要写清是跟踪误差";
}

// ⑦ 守卫·速度：宽限期内不查；过了宽限要**持续**越限才触发（防抖）
TEST(GravityFf, GuardVelocityRespectsGraceAndDebounce)
{
  const auto q = real_pose();
  auto dq = zeros();
  dq[2] = 1.0;                                    // 保持态速度 1.0 rad/s
  auto q_ref = zeros();
  for (auto & v : q_ref) {v = kNaN;}              // 保持

  ac::GravityFfLaw law = make_plain(1.0);
  ac::GuardParams g;
  g.hold_vel_rad_s = 0.5;
  g.grace_sec = 0.5;
  g.vel_debounce_sec = 0.2;
  law.set_guard(g);
  law.on_activate(q);

  // 宽限期内（elapsed=0.1）⇒ 不查速度
  EXPECT_FALSE(law.compute(q, dq, q_ref, zeros(), 0.1, 0.0).guard_tripped);
  // 过了宽限，第一帧只**起表**，不触发
  EXPECT_FALSE(law.compute(q, dq, q_ref, zeros(), 1.0, 1.0).guard_tripped);
  // 持续不到防抖时长 ⇒ 仍不触发
  EXPECT_FALSE(law.compute(q, dq, q_ref, zeros(), 1.0, 1.1).guard_tripped);
  // 持续够了 ⇒ 触发
  EXPECT_TRUE(law.compute(q, dq, q_ref, zeros(), 1.0, 1.25).guard_tripped);
}

// ⑧ 积分：门控（大误差不积）+ 饱和（钳到 frac×torque_max）+ 参考跳变复位
TEST(GravityFf, IntegratorGateSaturationAndReset)
{
  const auto q = real_pose();
  const auto z = zeros();
  ac::GravityFfLaw law = make_plain(0.0);         // 关掉重力，只看积分
  law.set_torque_max(0, 1.0);                     // 饱和上限 = frac × 1.0
  law.set_integrator(0, /*ki=*/10.0, /*frac=*/0.1);   // ⇒ 上限 0.1，每步 0.05
  law.set_dt(0.1);
  ac::GuardParams g;
  g.track_rad = 10.0;                             // 守卫别来捣乱
  g.grace_sec = 1e9;                              // 也不查速度
  law.set_guard(g);
  law.on_activate(q);

  // 参考 +0.05（<0.15 门控）⇒ 关节侧误差 = sign·(+0.05) = −0.05（j1 的 sign=−1）
  // 每步 Δτ = ki·err·dt = 10 · (−0.05) · 0.1 = −0.05
  auto q_ref = q;
  q_ref[0] = q[0] + 0.05;
  const auto o1 = law.compute(q, z, q_ref, z, 1.0, 0.0);
  ASSERT_TRUE(o1.valid);
  EXPECT_NEAR(o1.tau[0], -0.05, 1e-12);
  // 第二步会撞上限 0.1 ⇒ **停在饱和值**（这里正是抗饱和那个 bug 的回归点：
  // 写成"保持当前值"时会冻结在上限之下，到不了 −0.1）
  const auto o2 = law.compute(q, z, q_ref, z, 1.0, 0.1);
  EXPECT_NEAR(o2.tau[0], -0.10, 1e-12) << "撞上限必须停在饱和值";

  // 误差 0.5（>0.15 门控）⇒ **停止积分**（τ_i 保持不动，仍是饱和值）
  auto q_ref_big = q;
  q_ref_big[0] = q[0] + 0.5;
  const auto o3 = law.compute(q, z, q_ref_big, z, 1.0, 0.2);
  EXPECT_NEAR(o3.tau[0], -0.10, 1e-12) << "大误差必须门控住，不积";

  // 再多跑几十帧：饱和值不变（不 windup）
  for (int k = 0; k < 40; ++k) {
    const auto o = law.compute(q, z, q_ref, z, 1.0, 1.0 + 0.1 * k);
    EXPECT_NEAR(o.tau[0], -0.10, 1e-12) << "饱和后不该继续涨（第 " << k << " 帧）";
  }

  // 参考跳变 > 0.05 rad ⇒ 复位积分；下一帧从 0 重新爬，单步 0.05 撞上限 0.1 ⇒ 停在 −0.1
  law.note_reference(q_ref);
  auto q_ref_jump = q;
  q_ref_jump[0] = q[0] + 0.1;                     // 跳 0.05 rad（> kIntegResetRad）
  law.note_reference(q_ref_jump);
  const auto o4 = law.compute(q, z, q_ref_jump, z, 1.0, 999.0);
  EXPECT_NEAR(o4.tau[0], -0.10, 1e-12) << "参考跳变后该从 0 重新爬（本帧就撞上限）";
}

// ⑨ 参数校验：不合法就当场抛，别静默
TEST(GravityFf, RejectsBadParams)
{
  ac::GravityFfLaw law = make_plain(1.0);
  EXPECT_THROW(law.set_integrator(0, -1.0, 0.3), std::invalid_argument);
  EXPECT_THROW(law.set_friction(0, 0.4, 0.0), std::invalid_argument);   // tanh 会除零
  EXPECT_THROW(law.set_friction(0, -0.1, 0.02), std::invalid_argument);
  EXPECT_THROW(law.set_integrator(99, 1.0, 0.3), std::out_of_range);
  EXPECT_THROW(law.set_friction(99, 0.1, 0.02), std::out_of_range);
  EXPECT_THROW(law.set_torque_max(99, 1.0), std::out_of_range);
  // 模型路径 / 关节名错 ⇒ 构造即抛（大声失败）
  EXPECT_THROW(ac::GravityFfLaw("/tmp/没有这个模型.urdf", kJoints, kSign), std::exception);
  EXPECT_THROW(ac::GravityFfLaw(ARM_DYN_URDF, {"joint1", "没有这个关节"}, {1, 1}),
    std::invalid_argument);
}
