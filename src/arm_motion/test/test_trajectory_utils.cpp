// test_trajectory_utils.cpp —— 轨迹校验与装配（**纯逻辑**，不需要 rclcpp/节点/串口）。
//
// 这是 R3 最要紧的护栏：JTC 不校验限位，越界轨迹照执行 ⇒ 臂顶在机械硬限位上堵转。
#include <gtest/gtest.h>

// 直接 include：`trajectory_utils.hpp` 刻意不暴露 arm_limits（用不着），但用例要读边界值
#include <arm_description/joint_limits.hpp>

#include <cmath>
#include <string>
#include <vector>

#include "arm_motion/trajectory_utils.hpp"

namespace
{

std::vector<std::string> names()
{
  return {"joint1", "joint2", "joint3", "joint4", "joint5", "joint6"};
}

// 折叠位（落在这台臂的模型限位内）
std::vector<double> park()
{
  return {1.4724, 0.0008, -0.0233, 0.0027, 0.0069, 0.0149};
}

arm_motion::Trajectory two_point_traj()
{
  arm_motion::Trajectory t;
  t.joint_names = names();
  t.points.push_back({park(), 0.0});
  t.points.push_back({{1.50, -0.30, -0.30, 0.20, 0.0, 0.0}, 1.0});
  return t;
}

TEST(TrajectoryUtils, AcceptsAValidTrajectory)
{
  const auto c = arm_motion::check_trajectory(two_point_traj());
  EXPECT_TRUE(c.ok) << c.why;
  EXPECT_TRUE(c.why.empty());
}

TEST(TrajectoryUtils, RejectsWrongJointNamesAndSaysWhich)
{
  auto t = two_point_traj();
  std::swap(t.joint_names[0], t.joint_names[1]);
  const auto c = arm_motion::check_trajectory(t);
  EXPECT_FALSE(c.ok);
  EXPECT_NE(c.why.find("joint1"), std::string::npos) << c.why;
}

TEST(TrajectoryUtils, RejectsOutOfLimitPointAndSaysWhichPointWhichJointHowMuch)
{
  auto t = two_point_traj();
  t.points[1].positions[1] = 9.0;                  // joint2 的上界是 +0.058351
  const auto c = arm_motion::check_trajectory(t);
  EXPECT_FALSE(c.ok);
  EXPECT_EQ(c.bad_point, 1u) << "该报出是第 1 个点（0-based）";
  EXPECT_NE(c.why.find("第 1 个点"), std::string::npos) << c.why;
  EXPECT_NE(c.why.find("joint2"), std::string::npos) << c.why;
  EXPECT_NE(c.why.find("越界"), std::string::npos) << c.why;
}

TEST(TrajectoryUtils, RejectsEmptyTrajectory)
{
  arm_motion::Trajectory t;
  t.joint_names = names();
  const auto c = arm_motion::check_trajectory(t);
  EXPECT_FALSE(c.ok);
  EXPECT_NE(c.why.find("一个点都没有"), std::string::npos) << c.why;
}

TEST(TrajectoryUtils, RejectsWrongPointDimension)
{
  auto t = two_point_traj();
  t.points[1].positions = {1.5, -0.3};             // 只有 2 维
  const auto c = arm_motion::check_trajectory(t);
  EXPECT_FALSE(c.ok);
  EXPECT_EQ(c.bad_point, 1u);
  EXPECT_NE(c.why.find("维度"), std::string::npos) << c.why;
}

TEST(TrajectoryUtils, RejectsNonFinitePoint)
{
  auto t = two_point_traj();
  t.points[1].positions[3] = std::nan("");
  const auto c = arm_motion::check_trajectory(t);
  EXPECT_FALSE(c.ok);
  EXPECT_NE(c.why.find("NaN"), std::string::npos) << c.why;
}

TEST(TrajectoryUtils, RejectsNonMonotonicTime)
{
  auto t = two_point_traj();
  t.points[1].time_from_start = -1.0;              // 比前一点还早
  const auto c = arm_motion::check_trajectory(t);
  EXPECT_FALSE(c.ok);
  EXPECT_NE(c.why.find("回退"), std::string::npos) << c.why;
}

TEST(TrajectoryUtils, AllowsEqualTimestamps)
{
  // 同一时刻两个点（零时长段）在 MoveIt 的轨迹里合法 ⇒ 不算回退
  auto t = two_point_traj();
  t.points[1].time_from_start = 0.0;
  EXPECT_TRUE(arm_motion::check_trajectory(t).ok);
}

TEST(TrajectoryUtils, AcceptsExactBoundaryValues)
{
  std::vector<double> on_lo, on_hi;
  for (std::size_t i = 0; i < arm_limits::kJointNames.size(); ++i) {
    on_lo.push_back(arm_limits::kModelLimits[i][0]);
    on_hi.push_back(arm_limits::kModelLimits[i][1]);
  }
  std::string why;
  EXPECT_TRUE(arm_motion::point_within_limits(on_lo, &why)) << why;
  EXPECT_TRUE(arm_motion::point_within_limits(on_hi, &why)) << why;
}

TEST(TrajectoryUtils, MaxAbsDiffComputesResidual)
{
  const std::vector<double> a = {1.0, 2.0, 3.0};
  const std::vector<double> b = {1.1, 1.9, 3.05};
  EXPECT_NEAR(arm_motion::max_abs_diff(a, b), 0.1, 1e-12);
  EXPECT_NEAR(arm_motion::max_abs_diff(a, a), 0.0, 1e-12);
  // 长度不等 / 空 ⇒ NaN（"算不出来"就该是 NaN，不是 0）
  EXPECT_TRUE(std::isnan(arm_motion::max_abs_diff(a, {1.0, 2.0})));
  EXPECT_TRUE(std::isnan(arm_motion::max_abs_diff({}, {})));
  EXPECT_TRUE(std::isnan(arm_motion::max_abs_diff({std::nan("")}, {0.0})));
}

TEST(TrajectoryUtils, ReorderByNameHandlesJointStateOrder)
{
  // /joint_states 的顺序不保证与真源一致 ⇒ 必须按名字重排才能比残差
  const std::vector<std::string> from = {"joint6", "joint1", "joint2", "joint3", "joint4", "joint5"};
  const std::vector<double> vals = {6.0, 1.0, 2.0, 3.0, 4.0, 5.0};
  std::vector<double> out;
  ASSERT_TRUE(arm_motion::reorder_to(from, vals, names(), &out));
  EXPECT_EQ(out, (std::vector<double>{1.0, 2.0, 3.0, 4.0, 5.0, 6.0}));

  // 缺名字 ⇒ 拒（别猜）
  EXPECT_FALSE(arm_motion::reorder_to({"joint1"}, {1.0}, names(), &out));
}

}  // namespace
