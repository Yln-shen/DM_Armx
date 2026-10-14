// test_frame_plan.cpp —— 帧装配**决策**的纯逻辑测试（LAYERS §8.1 的"纯输入→输出"）。
//
// 这里**没有** rclcpp、没有串口、没有假电机：只喂数值、只看返回值 ⇒ 用例小、毫秒级。
// 它守住的是三条最容易悄悄漂开的判据：
//   · `effort` 的 NaN 语义选模式（D-L1）；
//   · 参考点的选择（有命令用命令 / 没有用锁定的保持点，陷阱 #38）；
//   · MIT 的力矩上限（PD 自己超 ⇒ 拒发；总力矩超 ⇒ 钳 tau_ff，陷阱 #22/#51）。
#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include "motor_driver_hardware/dm_frame_plan.hpp"

namespace md = motor_driver_hardware;

namespace
{
const double kNaN = std::numeric_limits<double>::quiet_NaN();
const double kInf = std::numeric_limits<double>::infinity();
}  // namespace

// ── pick_mode：effort 是不是 NaN（D-L1）──
TEST(FramePlan, PickModeIsDecidedByEffortNaN)
{
  // 没有控制器声索力矩 ⇒ POS_VEL（固件闭环，最稳）
  EXPECT_EQ(md::pick_mode(kNaN), md::ControlMode::PosVel);
  // 控制器在写力矩 ⇒ MIT。**0.0 也是有限值** ⇒ 必须进 MIT
  // （它表示"有控制器在跑，但前馈量是 0"——正是 gravity_ff=true 使能瞬间的样子）
  EXPECT_EQ(md::pick_mode(0.0), md::ControlMode::Mit);
  EXPECT_EQ(md::pick_mode(-0.5), md::ControlMode::Mit);
  EXPECT_EQ(md::pick_mode(1e-12), md::ControlMode::Mit);
  // inf 不是"控制器给了个合理的力矩" ⇒ 不当作 MIT（别把 inf 送进帧里）
  EXPECT_EQ(md::pick_mode(kInf), md::ControlMode::PosVel);
  EXPECT_EQ(md::pick_mode(-kInf), md::ControlMode::PosVel);
}

TEST(FramePlan, ModeNamesMatchRegisterValues)
{
  // 名字要和人看到的一致；值就是 CTRL_MODE 寄存器（0x0A）的值
  EXPECT_STREQ(md::mode_name(md::ControlMode::Mit), "MIT");
  EXPECT_STREQ(md::mode_name(md::ControlMode::PosVel), "POS_VEL");
  EXPECT_EQ(static_cast<unsigned>(md::ControlMode::Mit), 1u);
  EXPECT_EQ(static_cast<unsigned>(md::ControlMode::PosVel), 2u);
}

// ── pick_reference：有命令用命令，没有用"使能那一刻锁定的"保持点 ──
TEST(FramePlan, PickReferencePrefersCommandThenHold)
{
  double out = 0.0;
  EXPECT_TRUE(md::pick_reference(0.7, 0.2, &out));
  EXPECT_DOUBLE_EQ(out, 0.7);
  // 0.0 是**有效命令**（不是"没命令"）；"没命令"只认 NaN
  EXPECT_TRUE(md::pick_reference(0.0, 0.2, &out));
  EXPECT_DOUBLE_EQ(out, 0.0);
  // 命令是 NaN ⇒ 用保持点（陷阱 #38：保持目标锁定，不每圈重读）
  EXPECT_TRUE(md::pick_reference(kNaN, 0.25, &out));
  EXPECT_DOUBLE_EQ(out, 0.25);
  // 命令 NaN 且保持点还没有（还没使能成功）⇒ false ⇒ 调用方必须拒发，**不许拿 0 顶上**
  EXPECT_FALSE(md::pick_reference(kNaN, kNaN, &out));
}

// ── clamp_mit_torque：三种结局 ──
TEST(FramePlan, MitTorquePassesThroughWhenWithinLimit)
{
  // PD = 7.0·0.01 + 0.8·0.0 = 0.07；+ tau_ff 0.5 = 0.57 ≤ 3.5
  const auto p = md::clamp_mit_torque(7.0, 0.8, 0.01, 0.0, 0.5, 3.5);
  EXPECT_FALSE(p.refuse);
  EXPECT_FALSE(p.clamped);
  EXPECT_DOUBLE_EQ(p.value, 0.5);
  EXPECT_NEAR(p.pd, 0.07, 1e-12);
  EXPECT_NEAR(p.predicted, 0.57, 1e-12);
}

TEST(FramePlan, MitTorqueIsClampedWhenOnlyTotalExceedsLimit)
{
  // PD = 7.0·0.1 = 0.7（≤ 3.5）；+ tau_ff 5.0 ⇒ 5.7 > 3.5 ⇒ 钳到 3.5 − 0.7 = 2.8
  const auto p = md::clamp_mit_torque(7.0, 0.8, 0.1, 0.0, 5.0, 3.5);
  EXPECT_FALSE(p.refuse);
  EXPECT_TRUE(p.clamped);
  EXPECT_NEAR(p.value, 2.8, 1e-12);
  EXPECT_NEAR(p.predicted, 5.7, 1e-12);
}

TEST(FramePlan, MitTorqueKeepsSignWhenClampingNegative)
{
  // 反向：PD = 7.0·(−0.1) = −0.7；+ τ_ff −5.0 ⇒ −5.7 ⇒ 钳到 −(3.5 − 0.7) = −2.8
  const auto p = md::clamp_mit_torque(7.0, 0.8, -0.1, 0.0, -5.0, 3.5);
  EXPECT_FALSE(p.refuse);
  EXPECT_TRUE(p.clamped);
  EXPECT_NEAR(p.value, -2.8, 1e-12);
}

TEST(FramePlan, MitTorqueRefusesWhenPdAloneExceedsLimit)
{
  // PD = 15.0·0.5 = 7.5 > 3.5 ⇒ 钳 tau_ff 救不回来 ⇒ 拒发（调用方据此抛 ⇒ 别发出去掉臂）
  const auto p = md::clamp_mit_torque(15.0, 0.0, 0.5, 0.0, 0.0, 3.5);
  EXPECT_TRUE(p.refuse);
  EXPECT_NEAR(p.pd, 7.5, 1e-12);
  // 即使 tau_ff 是"帮忙"的反号，也不许放行：PD 项自己就超了
  const auto q = md::clamp_mit_torque(15.0, 0.0, 0.5, 0.0, -7.5, 3.5);
  EXPECT_TRUE(q.refuse);
}

TEST(FramePlan, MitTorqueIsUnlimitedWhenNoLimitGiven)
{
  // 没给 torque_max（单关节直用）⇒ 什么都不做，原样放行
  const auto p = md::clamp_mit_torque(100.0, 0.0, 1.0, 0.0, 50.0, 0.0);
  EXPECT_FALSE(p.refuse);
  EXPECT_FALSE(p.clamped);
  EXPECT_DOUBLE_EQ(p.value, 50.0);
}

TEST(FramePlan, MitTorqueExactBoundaryIsNotClamped)
{
  // PD = 3.5（恰好等于上限，**不**算超）⇒ 钳位不动 τ_ff
  const auto p = md::clamp_mit_torque(7.0, 0.0, 0.5, 0.0, 0.0, 3.5);
  EXPECT_FALSE(p.refuse);
  EXPECT_FALSE(p.clamped);
  EXPECT_DOUBLE_EQ(p.value, 0.0);
}
