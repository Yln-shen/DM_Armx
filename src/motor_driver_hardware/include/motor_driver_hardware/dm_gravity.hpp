// dm_gravity.hpp —— 重力项（给重力前馈用）。**不依赖 rclcpp**（和 dm_hardware 其余部分一样）。
//
// 只管三层坐标里的**第一段**：
//     URDF/模型坐标  --sign-->  关节侧 q_ours  --direction-->  电机侧
// 力矩是**矢量** ⇒ 这里只乘 `sign`（不加 `zero_shift`）；再往下的 `direction` 由 `Joint::set_mit` 负责。
//
// 实时性：模型与 `Data` 都在**构造时**建好，`tau_ours()` 只用预分配的缓冲、**不分配内存**
//         —— 它会在 100 Hz 的控制循环（`write()`）里被调用。
//
// 关节顺序：调用方给的是**它自己的顺序**（插件里 `<ros2_control>` 的关节顺序未必等于 URDF 里
//           关节的出现顺序）⇒ 内部一律按**名字**映射到 pinocchio 的 joint index。
#ifndef MOTOR_DRIVER_HARDWARE__DM_GRAVITY_HPP_
#define MOTOR_DRIVER_HARDWARE__DM_GRAVITY_HPP_

#include <Eigen/Core>
#include <pinocchio/multibody.hpp>
#include <pinocchio/parsers/urdf.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace motor_driver_hardware
{

class GravityModel
{
public:
  // `joint_names` 按**调用方自己的关节顺序**给，`sign` 与它一一对应（只能是 ±1）。
  // 某个名字在模型里找不到、或不是单自由度关节 ⇒ **构造即抛**（大声失败，别静默给 0）。
  GravityModel(const std::string & urdf_path,
    const std::vector<std::string> & joint_names,
    const std::vector<int> & sign);

  std::size_t size() const {return jidx_.size();}
  const std::string & urdf_path() const {return urdf_path_;}

  // q_urdf（**模型坐标**，长度 = size()，按构造时给的顺序）→ out（**关节侧** N·m，已套 sign）。
  // ⚠️ 非 const：内部用预分配的缓冲，不做堆分配（见文件头）。
  // 输入里有非有限数 ⇒ 抛 std::invalid_argument（调用方据此退回 POS_VEL，绝不发 NaN 力矩）。
  void tau_ours(const double * q_urdf, double * out);

private:
  std::string urdf_path_;
  pinocchio::Model model_;
  std::unique_ptr<pinocchio::Data> data_;   // 用 unique_ptr：避免"默认构造 + 赋值"这两步的坑
  Eigen::VectorXd q_buf_;                   // 预分配（model_.nq），避免每帧分配
  std::vector<pinocchio::JointIndex> jidx_; // 调用方关节顺序 → pinocchio joint index
  std::vector<int> sign_;
};

}  // namespace motor_driver_hardware

#endif  // MOTOR_DRIVER_HARDWARE__DM_GRAVITY_HPP_
