// trajectory_utils.cpp —— 轨迹校验与装配（实现见头文件里的契约说明）。
#include "arm_motion/trajectory_utils.hpp"

#include <arm_description/joint_limits.hpp>

#include <cmath>
#include <sstream>

namespace arm_motion
{

namespace
{

constexpr std::size_t kJointCount = arm_limits::kJointNames.size();

std::string fmt(double v)
{
  std::ostringstream ss;
  ss.precision(6);
  ss << v;
  return ss.str();
}

std::string jname(std::size_t i)
{
  return i < kJointCount ? std::string(arm_limits::kJointNames[i]) : ("#" + std::to_string(i));
}

}  // namespace

bool trajectory_joint_names_match(const std::vector<std::string> & names, std::string * why)
{
  auto fail = [why](const std::string & s) {if (why) {*why = s;} return false;};
  if (names.size() != kJointCount) {
    return fail("关节数不对：要有 " + std::to_string(kJointCount) + " 个，收到 " +
      std::to_string(names.size()));
  }
  for (std::size_t i = 0; i < kJointCount; ++i) {
    if (names[i] != arm_limits::kJointNames[i]) {
      return fail("第 " + std::to_string(i) + " 个关节名是 `" + names[i] + "`，真源是 `" +
        arm_limits::kJointNames[i] + "`");
    }
  }
  return true;
}

bool point_within_limits(const std::vector<double> & positions, std::string * why)
{
  auto fail = [why](const std::string & s) {if (why) {*why = s;} return false;};
  if (positions.size() != kJointCount) {
    return fail("维度不对：要有 " + std::to_string(kJointCount) + " 个数，收到 " +
      std::to_string(positions.size()));
  }
  for (std::size_t i = 0; i < kJointCount; ++i) {
    if (!std::isfinite(positions[i])) {return fail(jname(i) + " 是 NaN/inf");}
    const double lo = arm_limits::kModelLimits[i][0];
    const double hi = arm_limits::kModelLimits[i][1];
    if (positions[i] < lo || positions[i] > hi) {
      const double delta = positions[i] < lo ? (positions[i] - lo) : (positions[i] - hi);
      return fail(jname(i) + " 越界：" + fmt(positions[i]) + " 不在 [" + fmt(lo) + ", " +
        fmt(hi) + "] 内（超出 " + fmt(delta) + " rad）");
    }
  }
  return true;
}

TrajectoryCheck check_trajectory(const Trajectory & traj)
{
  TrajectoryCheck out;
  auto fail = [&out](std::size_t point, const std::string & s) {
      out.ok = false; out.bad_point = point; out.why = s; return out;
    };

  std::string why;
  if (!trajectory_joint_names_match(traj.joint_names, &why)) {return fail(0, "轨迹的 " + why);}
  if (traj.points.empty()) {return fail(0, "轨迹一个点都没有");}

  double last_t = -1.0;
  for (std::size_t p = 0; p < traj.points.size(); ++p) {
    const Sample & s = traj.points[p];
    const std::string where = "第 " + std::to_string(p) + " 个点";

    if (s.positions.size() != kJointCount) {
      return fail(p, where + " 的维度不对：要有 " + std::to_string(kJointCount) + " 个数，收到 " +
        std::to_string(s.positions.size()));
    }
    if (point_within_limits(s.positions, &why) == false) {
      return fail(p, where + "：" + why);
    }
    if (!std::isfinite(s.time_from_start)) {return fail(p, where + " 的时间戳是 NaN/inf");}
    if (s.time_from_start < last_t) {
      return fail(p, where + " 的时间戳回退了（" + fmt(s.time_from_start) + " < " +
        fmt(last_t) + "）—— JTC 拿到乱序时间会算出莫名其妙的速度");
    }
    last_t = s.time_from_start;
  }

  out.ok = true;
  return out;
}

double max_abs_diff(const std::vector<double> & a, const std::vector<double> & b)
{
  if (a.size() != b.size() || a.empty()) {return std::nan("");}
  double worst = 0.0;
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (!std::isfinite(a[i]) || !std::isfinite(b[i])) {return std::nan("");}
    const double d = std::fabs(a[i] - b[i]);
    if (d > worst) {worst = d;}
  }
  return worst;
}

bool reorder_to(const std::vector<std::string> & from_order, const std::vector<double> & from,
  const std::vector<std::string> & to_order, std::vector<double> * out)
{
  if (from_order.size() != from.size() || out == nullptr) {return false;}
  std::vector<double> result;
  result.reserve(to_order.size());
  for (const auto & name : to_order) {
    bool found = false;
    for (std::size_t i = 0; i < from_order.size(); ++i) {
      if (from_order[i] == name) {
        result.push_back(from[i]);
        found = true;
        break;
      }
    }
    if (!found) {return false;}
  }
  *out = std::move(result);
  return true;
}

}  // namespace arm_motion
