// named_targets.hpp —— 命名目标表（`arm_motion/config/named_targets.yaml`）的加载与校验。
//
// **职责**：把 YAML 读成 `Target`，并在**加载期**把能查的都查掉（启动即失败，不带病运行）。
// **边界**：不碰 ROS（不 import rclcpp、不打日志）—— 错误以"返回 false + 填 message"的形式
//   交出去，措辞与严重级由调用方决定（与 `MotionClient` 同一条纪律，见 LAYERS §2）。
//   ⇒ 单测不需要 rclcpp、不需要节点、不需要串口。
//
// 校验依据是**构建期生成**的 `arm_description/joint_limits.hpp`（`arm_limits::*`），
// 那是"模型坐标限位"的唯一真源（joint.yaml + align.yaml 推出来的，见 AGENTS §4）。
// ⚠️ 为什么非查不可：JTC **不校验限位** —— 越界目标照执行，臂会顶在机械硬限位上堵转
//   （真机实测 joint2 给 +99 rad ⇒ 堵转 60 s、effort 22.5 N·m、线圈 62 ℃，全程 err=0）。
#ifndef ARM_MOTION__NAMED_TARGETS_HPP_
#define ARM_MOTION__NAMED_TARGETS_HPP_

#include <array>
#include <cstdint>   // uint8_t（enum 的底层类型）
#include <map>
#include <string>
#include <vector>

#include <arm_description/joint_limits.hpp>

namespace arm_motion
{

// 目标种类：与 `arm_msgs/action/ExecuteMotion.action` 的 `TARGET_*` 对应。
// 这里**不复用**那套常量，是为了让本文件不依赖 arm_msgs ⇒ 可脱离 ROS 单测。
enum class Kind : uint8_t
{
  Joints = 0,
  Pose = 1,
};

struct Target
{
  Kind kind = Kind::Joints;

  // kind == Joints：**模型坐标** rad，顺序 = `arm_limits::kJointNames`
  std::array<double, 6> positions{};

  // kind == Pose：base_link 下的位置 m + 姿态（四元数）
  std::array<double, 3> position{};
  std::array<double, 4> orientation{};   // x, y, z, w
};

class NamedTargets
{
public:
  // 从 YAML 文件加载。任何一项不合格 ⇒ 返回 false、`error` 里写清**是哪一个目标、哪一项、差多少**。
  // 空文件 / 缺 targets 段 ⇒ 也算错（宁可启动失败，也别让一个空表静默通过）。
  bool load(const std::string & yaml_path, std::string * error);

  // 查一个命名目标。不存在 ⇒ 返回 false。
  bool find(const std::string & name, Target * out) const;

  bool has(const std::string & name) const {return targets_.count(name) != 0;}

  // 所有名字（测试与错误信息用）
  std::vector<std::string> names() const;

  // 默认末端链（Goal.tip_link 为空时用）；加载后一定有值
  const std::string & default_tip_link() const {return default_tip_link_;}
  const std::string & frame_id() const {return frame_id_;}

  // 校验关节名列表是否与真源顺序一致（按**名字**比，不按下标）。
  // 给 `TARGET_JOINTS` 与 `TARGET_NAMED`(joints) 共用 —— 别各写一遍。
  static bool joint_names_match(const std::vector<std::string> & names, std::string * error);

  // 逐个查限位（模型坐标）。越界 ⇒ false，`error` 里报**哪一关节、差多少**。
  static bool within_limits(const std::array<double, 6> & positions, std::string * error);

private:
  std::map<std::string, Target> targets_;
  std::string frame_id_ = "base_link";
  std::string default_tip_link_ = "gripper_tcp";
};

}  // namespace arm_motion

#endif  // ARM_MOTION__NAMED_TARGETS_HPP_
