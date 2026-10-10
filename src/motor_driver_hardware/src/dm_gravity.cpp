// dm_gravity.cpp —— 重力项实现（pinocchio 的 RNEA 静态特例：零速度、零加速度 ⇒ 只剩重力）。
// 输入机械臂当前角度，输出每个关节的重力力矩

#include "motor_driver_hardware/dm_gravity.hpp"

#include <pinocchio/algorithm/rnea.hpp>

#include <cmath>
#include <stdexcept>

namespace motor_driver_hardware
{

GravityModel::GravityModel(const std::string & urdf_path,
  const std::vector<std::string> & joint_names,
  const std::vector<int> & sign)
: urdf_path_(urdf_path)
{
  if (joint_names.empty()) {
    throw std::invalid_argument("GravityModel：一个关节都没给");
  }
  if (joint_names.size() != sign.size()) {
    throw std::invalid_argument(
      "GravityModel：joint_names(" + std::to_string(joint_names.size()) + ") 与 sign(" +
      std::to_string(sign.size()) + ") 长度不一致");
  }
  for (int s : sign) {
    if (s != 1 && s != -1) {
      throw std::invalid_argument(
        "GravityModel：sign 只能是 ±1（收到 " + std::to_string(s) + "）");
    }
  }

  pinocchio::urdf::buildModel(urdf_path, model_);
  data_ = std::make_unique<pinocchio::Data>(model_);
  q_buf_ = Eigen::VectorXd::Zero(model_.nq);

  jidx_.reserve(joint_names.size());
  for (const auto & name : joint_names) {
    if (!model_.existJointName(name)) {
      throw std::invalid_argument(
        "GravityModel：模型里没有关节 \"" + name + "\"（URDF: " + urdf_path + "）");
    }
    const pinocchio::JointIndex id = model_.getJointId(name);
    if (model_.joints[id].nq() != 1 || model_.joints[id].nv() != 1) {
      throw std::invalid_argument(
        "GravityModel：关节 \"" + name + "\" 不是单自由度关节（nq=" +
        std::to_string(model_.joints[id].nq()) + ", nv=" +
        std::to_string(model_.joints[id].nv()) + "），这一层只支持单自由度");
    }
    jidx_.push_back(id);
  }
  sign_ = sign;
}

void GravityModel::tau_ours(const double * q_urdf, double * out)
{
  // 1. 检查输入有限
  // 2. 写进预分配缓冲
  // 3. Pinocchio 算
  // 4. 乘 sign
  for (std::size_t k = 0; k < jidx_.size(); ++k) {
    if (!std::isfinite(q_urdf[k])) {
      throw std::invalid_argument(
        "GravityModel：q 里有非有限数（NaN/inf），拒绝算重力项 —— "
        "宁可让调用方退回 POS_VEL，也不要发一个 NaN 力矩出去");
    }
  }
  // 按 idx_q 摆进"模型顺序"的缓冲：零分配（q_buf_ 在构造时建好）
  for (std::size_t k = 0; k < jidx_.size(); ++k) {
    q_buf_[model_.joints[jidx_[k]].idx_q()] = q_urdf[k];
  }
  const auto & g = pinocchio::computeGeneralizedGravity(model_, *data_, q_buf_);
  // 力矩是矢量：模型坐标 → 关节侧**只乘 sign**（不加 zero_shift）
  for (std::size_t k = 0; k < jidx_.size(); ++k) {
    out[k] = static_cast<double>(sign_[k]) * g[model_.joints[jidx_[k]].idx_v()];
  }
}

}  // namespace motor_driver_hardware
