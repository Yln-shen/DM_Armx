// named_targets.cpp —— 命名目标表的加载与校验（实现见头文件里的契约说明）。
#include "arm_motion/named_targets.hpp"

#include <yaml-cpp/yaml.h>

#include <cmath>
#include <sstream>

namespace arm_motion
{

namespace
{

constexpr std::size_t kJointCount = arm_limits::kJointNames.size();

// 数值必须有限（NaN/inf 一律拒，陷阱 #1 的同类思路：拦在门口）
bool finite(double v) {return std::isfinite(v);}

std::string fmt(double v)
{
  std::ostringstream ss;
  ss.precision(6);
  ss << v;
  return ss.str();
}

// 读一个 double 序列并检查长度 + 有限性
bool read_doubles(const YAML::Node & node, std::size_t expect, const std::string & what,
  std::vector<double> * out, std::string * error)
{
  if (!node || !node.IsSequence()) {
    *error = what + " 必须是数列";
    return false;
  }
  out->clear();
  for (const auto & item : node) {
    double v = 0.0;
    try {
      v = item.as<double>();
    } catch (const std::exception &) {
      *error = what + " 里有非数字项";
      return false;
    }
    if (!finite(v)) {
      *error = what + " 里有 NaN/inf";
      return false;
    }
    out->push_back(v);
  }
  if (out->size() != expect) {
    *error = what + " 要有 " + std::to_string(expect) + " 个数，实际 " + std::to_string(out->size());
    return false;
  }
  return true;
}

// 拒绝未知/多余字段：宁可启动失败，也别让打错的键静默失效（例如写成 `position:` 而代码只认 `positions`）
bool reject_unknown_keys(const YAML::Node & node, const std::vector<std::string> & allowed,
  const std::string & what, std::string * error)
{
  for (const auto & kv : node) {
    const std::string key = kv.first.as<std::string>();
    bool ok = false;
    for (const auto & a : allowed) {if (key == a) {ok = true; break;}}
    if (!ok) {
      std::string list;
      for (std::size_t i = 0; i < allowed.size(); ++i) {
        if (i) {list += " / ";}
        list += allowed[i];
      }
      *error = what + " 里有未知字段 `" + key + "`（允许：" + list + "）";
      return false;
    }
  }
  return true;
}

}  // namespace

bool NamedTargets::joint_names_match(const std::vector<std::string> & names, std::string * error)
{
  if (names.size() != kJointCount) {
    *error = "关节数不对：要有 " + std::to_string(kJointCount) + " 个，收到 " +
      std::to_string(names.size());
    return false;
  }
  for (std::size_t i = 0; i < kJointCount; ++i) {
    if (names[i] != arm_limits::kJointNames[i]) {
      *error = "第 " + std::to_string(i) + " 个关节名是 `" + names[i] + "`，真源是 `" +
        arm_limits::kJointNames[i] + "`（顺序必须与 joint_limits.hpp 一致，按名字比）";
      return false;
    }
  }
  return true;
}

bool NamedTargets::within_limits(const std::array<double, 6> & positions, std::string * error)
{
  for (std::size_t i = 0; i < kJointCount; ++i) {
    const double lo = arm_limits::kModelLimits[i][0];
    const double hi = arm_limits::kModelLimits[i][1];
    if (positions[i] < lo || positions[i] > hi) {
      // ⚠️ 报出**哪一关节、差多少** —— 这条信息是排查"为什么突然规划不了"的唯一线索
      const double delta = positions[i] < lo ? (positions[i] - lo) : (positions[i] - hi);
      *error = std::string(arm_limits::kJointNames[i]) + " 越界：" + fmt(positions[i]) +
        " 不在 [" + fmt(lo) + ", " + fmt(hi) + "] 内（超出 " + fmt(delta) + " rad）";
      return false;
    }
  }
  return true;
}

bool NamedTargets::load(const std::string & yaml_path, std::string * error)
{
  auto fail = [error](const std::string & s) {if (error) {*error = s;} return false;};

  YAML::Node root;
  try {
    root = YAML::LoadFile(yaml_path);
  } catch (const std::exception & e) {
    return fail("打不开/解析不了 " + yaml_path + "：" + e.what());
  }
  if (!root || !root.IsMap()) {return fail(yaml_path + " 的顶层必须是映射");}

  // 顶层只认 frame_id / tip_link / targets
  if (!reject_unknown_keys(root, {"frame_id", "tip_link", "targets"}, "顶层", error)) {return false;}

  // frame_id 只允许 base_link（本层 TF 依赖面越小越好；视觉侧负责变换过来，§4.4）
  if (root["frame_id"]) {
    frame_id_ = root["frame_id"].as<std::string>();
    if (frame_id_ != "base_link") {
      return fail("frame_id 只能是 base_link，收到 `" + frame_id_ + "`");
    }
  }
  if (root["tip_link"]) {
    default_tip_link_ = root["tip_link"].as<std::string>();
    if (default_tip_link_.empty()) {return fail("tip_link 不能是空串");}
  }

  const YAML::Node targets = root["targets"];
  if (!targets || !targets.IsMap() || targets.size() == 0) {
    return fail(yaml_path + " 里没有 targets（或它不是非空映射）");
  }

  std::map<std::string, Target> loaded;
  for (const auto & kv : targets) {
    const std::string name = kv.first.as<std::string>();
    const YAML::Node node = kv.second;
    const std::string where = "命名目标 `" + name + "`";

    if (name.empty()) {return fail("有个命名目标是空名字");}
    if (!node.IsMap()) {return fail(where + " 必须是映射");}

    const std::string type = node["type"] ? node["type"].as<std::string>() : "";
    if (type.empty()) {return fail(where + " 缺 `type`（只能是 joints / pose）");}

    Target t;
    if (type == "joints") {
      if (!reject_unknown_keys(node, {"type", "positions"}, where, error)) {return false;}
      std::vector<double> vals;
      if (!read_doubles(node["positions"], kJointCount, where + " 的 positions", &vals, error)) {
        return false;
      }
      for (std::size_t i = 0; i < kJointCount; ++i) {t.positions[i] = vals[i];}
      t.kind = Kind::Joints;

      // joints 类型**加载期就查限位**（护栏第一道；越界的目标不该等到发出去才拦）
      if (!within_limits(t.positions, error)) {
        if (error) {*error = where + "：" + *error;}
        return false;
      }
    } else if (type == "pose") {
      if (!reject_unknown_keys(node, {"type", "position", "rpy"}, where, error)) {return false;}
      std::vector<double> pos, rpy;
      if (!read_doubles(node["position"], 3, where + " 的 position", &pos, error)) {return false;}
      // rpy 可选；给了就必须是 3 个有限数
      if (node["rpy"]) {
        if (!read_doubles(node["rpy"], 3, where + " 的 rpy", &rpy, error)) {return false;}
      } else {
        rpy = {0.0, 0.0, 0.0};
      }
      for (std::size_t i = 0; i < 3; ++i) {t.position[i] = pos[i];}
      // rpy → 四元数（zyx 内旋约定，与 URDF 的 rpy 一致）
      const double cr = std::cos(rpy[0] * 0.5), sr = std::sin(rpy[0] * 0.5);
      const double cp = std::cos(rpy[1] * 0.5), sp = std::sin(rpy[1] * 0.5);
      const double cy = std::cos(rpy[2] * 0.5), sy = std::sin(rpy[2] * 0.5);
      t.orientation[0] = sr * cp * cy - cr * sp * sy;   // x
      t.orientation[1] = cr * sp * cy + sr * cp * sy;   // y
      t.orientation[2] = cr * cp * sy - sr * sp * cy;   // z
      t.orientation[3] = cr * cp * cy + sr * sp * sy;   // w
      t.kind = Kind::Pose;
      // ⚠️ pose 类型**不做限位校验**（可达性交给 IK，§5.2）；但 position 已是有限数
    } else {
      return fail(where + " 的 type 是 `" + type + "`，只能是 joints / pose");
    }

    if (loaded.count(name)) {return fail("命名目标 `" + name + "` 重复定义");}
    loaded.emplace(name, t);
  }

  targets_ = std::move(loaded);
  return true;
}

bool NamedTargets::find(const std::string & name, Target * out) const
{
  const auto it = targets_.find(name);
  if (it == targets_.end()) {return false;}
  if (out) {*out = it->second;}
  return true;
}

std::vector<std::string> NamedTargets::names() const
{
  std::vector<std::string> out;
  out.reserve(targets_.size());
  for (const auto & kv : targets_) {out.push_back(kv.first);}
  return out;
}

}  // namespace arm_motion
