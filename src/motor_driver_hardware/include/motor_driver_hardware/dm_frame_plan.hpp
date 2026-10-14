// dm_frame_plan.hpp —— 硬件层的**纯决策**：这一帧发哪一种帧、用哪个参考、力矩钳到哪。
//
// 为什么单独一份（LAYERS §8.1）：硬件层里真正需要"想清楚"的只有几个判断，它们与
// 串口 / rclcpp / 电机对象都无关 ⇒ 抽成**纯函数**就能毫秒级测（`test_frame_plan.cpp`），
// 插件那层只剩"按决策去读写串口"的接线（`test_iface_wiring.cpp`，约 6 条）。
//
// 边界：
//   - 不 import rclcpp、不碰串口、不读 yaml、不知道 Joint；
//   - 不产生副作用（不打日志、不抛异常）—— 决策结果是**返回值**，拒发由调用方处置；
//   - 不做换算（`direction`/`offset`/`sign`/`zero_shift` 归 dm_joint）与协议编码（归 dm_frames）。
//
// 核心决策：`effort` 命令接口的 **NaN 语义**（D-L1，见 docs/LAYERS.md §4）
//     effort[i] 是 NaN    ⇒ 没有控制器声索力矩 ⇒ **POS_VEL**（固件位置环，最稳）
//     effort[i] 是有限值  ⇒ 有控制器在写力矩   ⇒ **MIT**（把它的值当 tau_ff，硬件出自己的 PD）
//   ⚠️ 这个判据**每次 write() 重新算**：控制器失活时 CM 会把它的命令留成 NaN ⇒ 自动回落 POS_VEL。
#ifndef MOTOR_DRIVER_HARDWARE__DM_FRAME_PLAN_HPP_
#define MOTOR_DRIVER_HARDWARE__DM_FRAME_PLAN_HPP_

#include <cmath>
#include <cstdint>

namespace motor_driver_hardware
{

// ── 控制模式（值就是 CTRL_MODE 寄存器 `0x0A` 的值，便于直接比对与上报）──
enum class ControlMode : uint8_t
{
  Mit = 1,       // 1 = MIT：直接给力矩（tau_ff 来自 effort 命令接口）
  PosVel = 2,    // 2 = 位置速度：给位置，固件闭环
};

inline const char * mode_name(ControlMode m)
{
  return m == ControlMode::Mit ? "MIT" : "POS_VEL";
}

// 这一帧该用哪个模式。`effort` 有限 ⇒ MIT，否则（NaN/inf/没有控制器）⇒ POS_VEL。
inline ControlMode pick_mode(double effort)
{
  return std::isfinite(effort) ? ControlMode::Mit : ControlMode::PosVel;
}

// 位置类帧的目标（**关节侧**）：有命令就用命令，没有就用"使能那一刻锁定的保持点"。
// 返回 false ⇒ 保持点还没有（还没使能成功）⇒ 调用方拒发，别拿 0 顶上（那就是朝零位冲）。
//
// 为什么不是"每圈重读实测值"（陷阱 #38）：「读哪停哪」没有回复力，重力会把关节慢慢压走。
inline bool pick_reference(double position_cmd, double hold_joint, double * out)
{
  if (std::isfinite(position_cmd)) {
    *out = position_cmd;
    return true;
  }
  if (std::isfinite(hold_joint)) {
    *out = hold_joint;
    return true;
  }
  return false;
}

// MIT 的力矩上限判定（陷阱 #22/#51），入参全部是**电机侧**量（与 dm_joint 的换算顺序一致）：
//   predicted = kp·(q_des−q) + kd·(dq_des−dq) + tau_ff
//   · 预测总力矩不超限 ⇒ 原样返回 tau_ff；
//   · 超限但 **PD 项自己没超** ⇒ 把 tau_ff 钳到"总力矩 = ±limit"（单调安全：只会让力矩更小）；
//   · **PD 项自己就超** ⇒ 钳 tau_ff 救不回来 ⇒ 返回 `refuse = true`，由调用方拒发。
//
// ⚠️ `q_des−q` 与 `dq_des−dq` 来自**缓存**（滞后 1~20 ms）⇒ 这只是**近似**保证，不是硬保证。
// ⚠️ 返回值的两个字段**都要看**：refuse=true 时 `value` 无意义。
struct MitTorquePlan
{
  double value = 0.0;      // 该发出去的 tau_ff（电机侧 N·m）
  bool clamped = false;    // 是否动过 tau_ff（调用方据此打限流日志）
  bool refuse = false;     // true ⇒ 拒发（PD 项自己超限）
  double predicted = 0.0;  // 钳位**之前**的预测总力矩（诊断与日志用）
  double pd = 0.0;         // PD 项（诊断与日志用）
};

constexpr MitTorquePlan clamp_mit_torque(
  double kp, double kd, double err_q, double err_dq, double tau_ff, double limit)
{
  MitTorquePlan p;
  p.pd = kp * err_q + kd * err_dq;
  p.predicted = p.pd + tau_ff;
  if (limit <= 0.0) {                    // 没给上限（单关节直用）⇒ 什么都不做
    p.value = tau_ff;
    return p;
  }
  const double abs_pd = p.pd < 0.0 ? -p.pd : p.pd;
  if (abs_pd > limit) {
    p.refuse = true;
    p.value = tau_ff;
    return p;
  }
  const double abs_total = p.predicted < 0.0 ? -p.predicted : p.predicted;
  if (abs_total > limit) {
    // 用 copysign 保留方向：limit − |pd| ≥ 0（上面已保证 |pd| ≤ limit）
    const double clamped = (p.predicted < 0.0 ? -1.0 : 1.0) * (limit - abs_pd);
    p.clamped = true;
    p.value = clamped;
    return p;
  }
  p.value = tau_ff;
  return p;
}

}  // namespace motor_driver_hardware

#endif  // MOTOR_DRIVER_HARDWARE__DM_FRAME_PLAN_HPP_
