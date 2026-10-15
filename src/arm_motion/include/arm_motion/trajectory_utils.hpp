// trajectory_utils.hpp —— 已规划轨迹的**校验与装配**（纯逻辑，不依赖 rclcpp）。
//
// 为什么单独一个文件：这段逻辑是 R3 最要紧的**护栏**，而且它不需要节点、不需要 action server
// 就能测。`MotionClient` 只发**单点**轨迹，而 MoveIt 给的是**多点**轨迹 ⇒ 执行那条线要自己写，
// 但"哪些点越界了 / 相邻两点的关节名对不对"这类判断不该埋在几百行的状态机里。
//
// 边界：
//   · 不碰 ROS 类型（下面用 `Sample` 这种朴素结构）⇒ 单测不需要 rclcpp；
//   · 真源是构建期生成的 `arm_limits::kModelLimits` / `kJointNames`（joint.yaml + align.yaml）。
//
// ⚠️ **为什么逐点查限位**：JTC 不校验限位。真机实测 joint2 给 +99 rad 它照执行 ⇒ 臂顶在机械
//    硬限位上堵转 60 s、effort 22.5 N·m、线圈 62 ℃，**全程 err=0 无故障码**（陷阱 #59）。
//    MoveIt 的位姿目标经过 IK 后同样是一串关节值，所以两种目标都要过这道闸（见 MOTION_ARCH §6.1）。
#ifndef ARM_MOTION__TRAJECTORY_UTILS_HPP_
#define ARM_MOTION__TRAJECTORY_UTILS_HPP_

#include <cstddef>
#include <string>
#include <vector>

namespace arm_motion
{

// 轨迹上的一个点。`positions` 的顺序 = `joint_names`；`time_from_start` 单位秒。
struct Sample
{
  std::vector<double> positions;
  double time_from_start = 0.0;
};

struct Trajectory
{
  std::vector<std::string> joint_names;   // 必须与 arm_limits::kJointNames 逐个一致
  std::vector<Sample> points;
};

struct TrajectoryCheck
{
  bool ok = false;
  std::string why;              // 不合格的原因（含**哪一点、哪一关节、数值、允许区间**）
  std::size_t bad_point = 0;    // 第一个出问题的点下标
};

// 关节名是否与真源逐个一致（顺序也一致）。不一致时填 why。
// 与 `NamedTargets::joint_names_match` 同款判据 —— 那一个是给命名目标用的静态函数，
// 这一个给"轨迹里的名字"用；两者都按**名字**比，不按下标。
bool trajectory_joint_names_match(const std::vector<std::string> & names, std::string * why);

// 逐点校验一条轨迹：名字、点数、每点维度、有限性、**逐点逐关节限位**、时间单调不减。
// ⚠️ 时间单调这条不是形式主义：JTC 拿到乱序时间戳会得到莫名其妙的速度，
//    而 MoveIt 的轨迹本就单调 —— 不单调说明上游出了问题，早报比晚报好。
TrajectoryCheck check_trajectory(const Trajectory & traj);

// 单点是否在模型限位内（越界时 why 里报哪一关节、差多少）。给"起始状态"与"轨迹末点"共用。
bool point_within_limits(const std::vector<double> & positions, std::string * why);

// 两个位置向量的最大逐关节绝对差（长度不等 ⇒ 返回 NaN）。给残差验证用。
double max_abs_diff(const std::vector<double> & a, const std::vector<double> & b);

// 按名字把 `from`（自带顺序）重排成 `to_order` 的顺序。缺名字 ⇒ 返回 false。
bool reorder_to(const std::vector<std::string> & from_order, const std::vector<double> & from,
  const std::vector<std::string> & to_order, std::vector<double> * out);

}  // namespace arm_motion

#endif  // ARM_MOTION__TRAJECTORY_UTILS_HPP_
