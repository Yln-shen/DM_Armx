// test_named_targets.cpp —— 命名目标表的加载与校验（**纯逻辑**，不需要 rclcpp/节点/串口）。
//
// 手法：往 `$TMPDIR` 写临时 YAML → 加载 → 断言结果。⚠️ 刻意**不读**真实的
// `config/named_targets.yaml` —— 否则改一个真实放置点就会把单测搞红（那是数据，不是契约）。
#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <string>

#include "arm_motion/named_targets.hpp"

namespace
{

// 写临时 YAML，返回路径
std::string write_tmp(const std::string & name, const std::string & body)
{
  const std::string path = std::string(std::getenv("TMPDIR") ? std::getenv("TMPDIR") : "/tmp") +
    "/arm_motion_test_" + name + ".yaml";
  std::ofstream f(path);
  f << body;
  f.close();
  return path;
}

// 把折叠位的真实限位值填进去，好写"能通过校验"的样本
std::string park_ok()
{
  return "[1.4724, 0.0008, -0.0233, 0.0027, 0.0069, 0.0149]";
}

TEST(NamedTargets, LoadsJointsTarget)
{
  const auto p = write_tmp("ok", R"(
frame_id: base_link
tip_link: gripper_tcp
targets:
  park:
    type: joints
    positions: )" + park_ok() + "\n");
  arm_motion::NamedTargets t;
  std::string err;
  ASSERT_TRUE(t.load(p, &err)) << err;
  EXPECT_EQ(t.default_tip_link(), "gripper_tcp");
  EXPECT_EQ(t.frame_id(), "base_link");
  EXPECT_TRUE(t.has("park"));
  EXPECT_FALSE(t.has("nope"));

  arm_motion::Target out;
  ASSERT_TRUE(t.find("park", &out));
  EXPECT_EQ(out.kind, arm_motion::Kind::Joints);
  EXPECT_NEAR(out.positions[0], 1.4724, 1e-9);
  EXPECT_NEAR(out.positions[5], 0.0149, 1e-9);
}

TEST(NamedTargets, RejectsOutOfLimitsAndSaysWhichJointAndHowMuch)
{
  // joint1 的下界是 +1.392202 ⇒ 给 1.30 应被拒，且 message 要指名道姓
  const auto p = write_tmp("oob", R"(
targets:
  bad:
    type: joints
    positions: [1.30, 0.0, 0.0, 0.0, 0.0, 0.0]
)");
  arm_motion::NamedTargets t;
  std::string err;
  EXPECT_FALSE(t.load(p, &err));
  EXPECT_NE(err.find("joint1"), std::string::npos) << "没报出是哪一关节：" << err;
  EXPECT_NE(err.find("越界"), std::string::npos) << err;
  // 差多少也要有（-0.0922 左右）
  EXPECT_NE(err.find("0.09"), std::string::npos) << "没报出差多少：" << err;
}

TEST(NamedTargets, RejectsWrongJointCount)
{
  const auto p = write_tmp("count", R"(
targets:
  bad:
    type: joints
    positions: [1.5, 0.0, 0.0]
)");
  arm_motion::NamedTargets t;
  std::string err;
  EXPECT_FALSE(t.load(p, &err));
  EXPECT_NE(err.find("6"), std::string::npos) << err;
}

TEST(NamedTargets, RejectsUnknownField)
{
  // 常见笔误：写成 position 而不是 positions（少个 s）—— 必须报错，不能静默失效
  const auto p = write_tmp("typo", R"(
targets:
  bad:
    type: joints
    position: )" + park_ok() + "\n");
  arm_motion::NamedTargets t;
  std::string err;
  EXPECT_FALSE(t.load(p, &err));
  EXPECT_NE(err.find("未知字段"), std::string::npos) << err;
  EXPECT_NE(err.find("position"), std::string::npos) << err;
}

TEST(NamedTargets, RejectsBadType)
{
  const auto p = write_tmp("type", R"(
targets:
  bad:
    type: whatever
    positions: )" + park_ok() + "\n");
  arm_motion::NamedTargets t;
  std::string err;
  EXPECT_FALSE(t.load(p, &err));
  EXPECT_NE(err.find("joints / pose"), std::string::npos) << err;
}

TEST(NamedTargets, RejectsNonFinite)
{
  const auto p = write_tmp("nan", R"(
targets:
  bad:
    type: joints
    positions: [.nan, 0.0, 0.0, 0.0, 0.0, 0.0]
)");
  arm_motion::NamedTargets t;
  std::string err;
  EXPECT_FALSE(t.load(p, &err));
  EXPECT_NE(err.find("NaN"), std::string::npos) << err;
}

TEST(NamedTargets, RejectsNonBaseLinkFrame)
{
  const auto p = write_tmp("frame", R"(
frame_id: tool0
targets:
  park:
    type: joints
    positions: )" + park_ok() + "\n");
  arm_motion::NamedTargets t;
  std::string err;
  EXPECT_FALSE(t.load(p, &err));
  EXPECT_NE(err.find("base_link"), std::string::npos) << err;
}

TEST(NamedTargets, RejectsMissingTargets)
{
  const auto p = write_tmp("empty", "frame_id: base_link\n");
  arm_motion::NamedTargets t;
  std::string err;
  EXPECT_FALSE(t.load(p, &err));
  EXPECT_NE(err.find("targets"), std::string::npos) << err;
}

TEST(NamedTargets, LoadsPoseTargetWithRpyOptional)
{
  const auto p = write_tmp("pose", R"(
targets:
  no_rpy:
    type: pose
    position: [0.20, -0.10, 0.15]
  with_rpy:
    type: pose
    position: [0.20, -0.10, 0.15]
    rpy: [3.14159, 0.0, 0.0]
)");
  arm_motion::NamedTargets t;
  std::string err;
  ASSERT_TRUE(t.load(p, &err)) << err;

  arm_motion::Target a, b;
  ASSERT_TRUE(t.find("no_rpy", &a));
  EXPECT_EQ(a.kind, arm_motion::Kind::Pose);
  EXPECT_NEAR(a.orientation[3], 1.0, 1e-12);   // 单位四元数 w=1

  ASSERT_TRUE(t.find("with_rpy", &b));
  // rpy = (π, 0, 0) ⇒ q ≈ (1, 0, 0, ~0)：绕 x 转 180°
  EXPECT_NEAR(std::abs(b.orientation[0]), 1.0, 1e-5);
  EXPECT_NEAR(b.orientation[3], 0.0, 1e-5);
}

TEST(NamedTargets, RejectsUnknownTopLevelKey)
{
  const auto p = write_tmp("top", R"(
frame_id: base_link
oops: 1
targets:
  park:
    type: joints
    positions: )" + park_ok() + "\n");
  arm_motion::NamedTargets t;
  std::string err;
  EXPECT_FALSE(t.load(p, &err));
  EXPECT_NE(err.find("oops"), std::string::npos) << err;
}

TEST(NamedTargets, JointNamesAreCheckedByNameNotIndex)
{
  std::string err;
  std::vector<std::string> good = {"joint1", "joint2", "joint3", "joint4", "joint5", "joint6"};
  EXPECT_TRUE(arm_motion::NamedTargets::joint_names_match(good, &err)) << err;

  auto swapped = good;
  std::swap(swapped[0], swapped[1]);              // 顺序错 ⇒ 拒
  EXPECT_FALSE(arm_motion::NamedTargets::joint_names_match(swapped, &err));
  EXPECT_NE(err.find("joint1"), std::string::npos) << err;

  std::vector<std::string> shortv = {"joint1"};
  EXPECT_FALSE(arm_motion::NamedTargets::joint_names_match(shortv, &err));
}

TEST(NamedTargets, WithinLimitsAcceptsBoundaryValues)
{
  std::string err;
  // 恰好等于上下界 ⇒ 不算越界（判定用 < / >，不是 <= / >=）
  std::array<double, 6> on_lo{}, on_hi{};
  for (std::size_t i = 0; i < 6; ++i) {
    on_lo[i] = arm_limits::kModelLimits[i][0];
    on_hi[i] = arm_limits::kModelLimits[i][1];
  }
  EXPECT_TRUE(arm_motion::NamedTargets::within_limits(on_lo, &err)) << err;
  EXPECT_TRUE(arm_motion::NamedTargets::within_limits(on_hi, &err)) << err;
}

}  // namespace
