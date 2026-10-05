// test_dm_system_interface.cpp —— 插件逻辑（不接真机，用假串口 + 一台"模拟电机"）。
//
// 假电机干三件事：收到 0x7FF 刷新 → 回当前状态；收到寄存器帧 → 回显 RID 的回包；
// 收到 POS_VEL 命令 → **把位置跟过去**再回状态（模拟真机固件闭环）。
// 这样"命令换算 → 发帧 → 状态回读"整条链都能在测试里跑通。
#include <gtest/gtest.h>

#include <hardware_interface/component_parser.hpp>
#include <hardware_interface/types/hardware_component_interface_params.hpp>
#include <hardware_interface/types/hardware_interface_type_values.hpp>
#include <rclcpp/rclcpp.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "motor_driver_hardware/dm_system_interface.hpp"

#ifndef ARM_DYN_URDF
#error "缺少 ARM_DYN_URDF（应由 CMake target_compile_definitions 传入）"
#endif

namespace md = motor_driver_hardware;
using hardware_interface::CallbackReturn;
using hardware_interface::return_type;

namespace
{

std::vector<uint8_t> feedback_frame(std::initializer_list<uint8_t> data8)
{
  std::vector<uint8_t> f(16, 0);
  f[0] = 0xAA;
  f[1] = 0x11;
  std::size_t i = 7;
  for (uint8_t b : data8) {f[i++] = b;}
  f[15] = 0x55;
  return f;
}

template<typename T>
std::vector<uint8_t> to_vec(const T & f) {return std::vector<uint8_t>(f.begin(), f.end());}

// MIT 帧的 12 位力矩字段解码（与 dm_frames::mit_frame 的编码互逆：
//   编码 float_to_uint(t, -t_max, t_max, 12) **向零截断**；解码就会差最多 1 LSB）
double decode_mit_tau(const std::vector<uint8_t> & f, double t_max)
{
  const uint32_t t_u = (static_cast<uint32_t>(f[27] & 0x0F) << 8) | f[28];
  return (static_cast<double>(t_u) / 4095.0) * 2.0 * t_max - t_max;
}

// MIT 帧的 16 位位置字段解码（与 dm_frames::mit_frame 互逆，PMAX=12.5）
double decode_mit_pos(const std::vector<uint8_t> & f)
{
  const uint32_t q_u = (static_cast<uint32_t>(f[21]) << 8) | f[22];
  return (static_cast<double>(q_u) / 65535.0) * 25.0 - 12.5;
}

class FakeMotorBusIo : public md::SerialIo
{
public:
  void open() override {open_ = true;}
  void close() override {open_ = false;}
  bool is_open() const override {return open_;}
  void write(const uint8_t * data, std::size_t len) override
  {
    tx.insert(tx.end(), data, data + len);
    stats_.tx_calls += 1;
    stats_.tx_bytes += len;
    respond(data, len);
  }
  std::size_t read_available(std::vector<uint8_t> & out) override
  {
    const std::size_t n = rx.size();
    out.insert(out.end(), rx.begin(), rx.end());
    rx.clear();
    return n;
  }
  std::size_t bytes_available() const override {return rx.size();}
  void flush_input() override {rx.clear();}
  const md::SerialStats & stats() const override {return stats_;}

  void push(std::initializer_list<uint8_t> bytes) {rx.insert(rx.end(), bytes.begin(), bytes.end());}
  void push(const std::vector<uint8_t> & bytes) {rx.insert(rx.end(), bytes.begin(), bytes.end());}

  // 模拟电机状态：电机侧位置（rad）/ ERR。初值取自真机停放姿态（id2 实测 +1.79 rad 那一带）
  std::map<uint8_t, double> pos{{2, 1.7909}, {6, 1.7909}};
  std::map<uint8_t, double> vel{{2, 0.0}, {6, 0.0}};      // 电机侧 rad/s（用来造速度尖峰）
  bool freeze_pos = false;                                // true ⇒ 收到 MIT 也不再跟命令（造持续误差）
  std::map<uint8_t, double> vmax{{2, 10.0}, {6, 30.0}};   // 各自的 VMAX（12 位映射范围）
  std::map<uint8_t, uint8_t> err{{2, 0x0}, {6, 0x0}};
  std::vector<uint8_t> tx;
  std::vector<uint8_t> rx;
  std::vector<std::vector<uint8_t>> control_frames;   // 收到的 POS_VEL 命令
  std::vector<std::vector<uint8_t>> mit_frames;       // 收到的 MIT 命令
  std::vector<std::vector<uint8_t>> enable_frames;    // 收到的使能/失能帧
  bool open_ = false;
  md::SerialStats stats_;

  void push_feedback(uint8_t id)
  {
    const uint16_t pos_u = encode_pos(pos[id]);
    const uint8_t d0 = static_cast<uint8_t>((id & 0x0F) | ((err[id] & 0x0F) << 4));
    // 速度字段：12 位映射里 0 落在**中点 2047**（不是 2048）⇒ 默认就是 D[3]=0x7F, D[4]=0xF0。
    // ⚠️ 写 0x00,0x00 会被解成 −VMAX（第一版就这么写的，MIT 用例里 kd 项直接爆掉）。
    const uint32_t v_u = md::float_to_uint(vel[id], -vmax[id], vmax[id], 12);
    const uint8_t d3 = static_cast<uint8_t>((v_u >> 4) & 0xFF);
    const uint8_t d4 = static_cast<uint8_t>((v_u & 0x0F) << 4);   // 低 4 位是 tau 的高 4 位，这里给 0
    push(feedback_frame({d0, static_cast<uint8_t>(pos_u >> 8), static_cast<uint8_t>(pos_u & 0xFF),
      d3, d4, 0x00, 0x1E, 0x1F}));
  }

  // "电机实际报出来的"位置（经过 16 位量化）—— 测试算期望时要用这个，不能用未量化的 pos[id]
  double reported_pos(uint8_t id) const
  {
    const uint16_t pos_u = encode_pos(pos.at(id));
    return (static_cast<double>(pos_u) / 65535.0) * 25.0 - 12.5;
  }

private:
  static uint16_t encode_pos(double p_m)
  {
    const double p_max = 12.5;      // 位置反馈的映射范围（j2/j6 都是 4340P 档位里的 12.5）
    double u = (p_m + p_max) / (2.0 * p_max) * 65535.0;
    u = std::max(0.0, std::min(u, 65535.0));
    return static_cast<uint16_t>(u);
  }

  void respond(const uint8_t * f, std::size_t len)
  {
    if (len != md::kTxFrameLen) {return;}
    const uint16_t can_id = static_cast<uint16_t>(f[13] | (f[14] << 8));
    if (can_id == md::kCanIdBroadcast) {
      const uint8_t target = f[21];
      const uint8_t cmd = f[23];
      if (cmd == 0xCC) {                                   // 刷新帧 → 回状态
        push_feedback(target);
      } else if (cmd == md::kRegCmdRead || cmd == md::kRegCmdWrite || cmd == md::kRegCmdSave) {
        push(feedback_frame({target, 0x00, cmd, f[24], 0, 0, 0, 0}));   // 寄存器回包（回显 RID）
      }
      return;
    }
    if (can_id >= md::kCanIdPosVelBase && can_id < md::kCanIdPosVelBase + 0x0100) {
      const uint8_t id = static_cast<uint8_t>(can_id - md::kCanIdPosVelBase);
      pos[id] = md::uint8s_to_float32(f + 21);             // 固件跟到命令位置
      control_frames.emplace_back(f, f + len);
      push_feedback(id);
      return;
    }
    if (can_id >= 1 && can_id <= 6) {
      // 使能/失能帧的数据段恒为 {0xFF×7, 0xFC/0xFD}（见 dm_frames::cmd_frame）——
      // 必须**整体**判，只看最后一个字节会把某些 MIT 帧误判成使能帧。
      bool all_ff = true;
      for (std::size_t k = 21; k <= 27; ++k) {if (f[k] != 0xFF) {all_ff = false;}}
      if (all_ff && (f[28] == md::kCmdEnable || f[28] == md::kCmdDisable)) {
        const uint8_t id = static_cast<uint8_t>(can_id);
        err[id] = (f[28] == md::kCmdEnable) ? 0x1 : 0x0;
        enable_frames.emplace_back(f, f + len);
        push_feedback(id);
        return;
      }
      // MIT 帧：CAN ID **就是电机 id 本身**（与使能帧同 ID，靠数据段区分）。
      // ⚠️ 只有 kp>0 时才"跟过去"：`Joint::enable()` 在 MIT 下发的是**零增益零前馈**保持帧，
      //    真机那时出力恒为 0、根本不会动（假电机若无脑跟随，位置会跳到 p_des）。
      const uint8_t id = static_cast<uint8_t>(can_id);
      const uint32_t kp_u = (static_cast<uint32_t>(f[24] & 0x0F) << 8) | f[25];
      if (kp_u > 0 && !freeze_pos) {
        const uint32_t q_u = (static_cast<uint32_t>(f[21]) << 8) | f[22];
        pos[id] = (static_cast<double>(q_u) / 65535.0) * 25.0 - 12.5;
      }
      mit_frames.emplace_back(f, f + len);
      push_feedback(id);
    }
  }
};

// ── 用真机的标定值建 HardwareInfo（direction/offset 来自 joint.yaml，sign/δ 来自 align.yaml）──
hardware_interface::ComponentInfo make_joint(const std::string & name,
  std::initializer_list<std::pair<const std::string, std::string>> params)
{
  hardware_interface::InterfaceInfo info;
  info.name = hardware_interface::HW_IF_POSITION;
  hardware_interface::InterfaceInfo vel;
  vel.name = hardware_interface::HW_IF_VELOCITY;

  hardware_interface::ComponentInfo j;
  j.name = name;
  j.parameters = params;
  j.command_interfaces.push_back(info);
  j.state_interfaces.push_back(info);
  j.state_interfaces.push_back(vel);
  return j;
}

hardware_interface::HardwareInfo make_info(bool enable_on_activate, bool gravity_ff = false,
  const std::string & urdf_path = "", double gravity_ff_scale = 1.0, double ki_hold = 0.0)
{
  hardware_interface::HardwareInfo info;
  info.name = "ArmReal";
  info.type = "system";
  info.hardware_parameters["device"] = "/dev/fake";
  info.hardware_parameters["baud"] = "921600";
  info.hardware_parameters["enable_on_activate"] = enable_on_activate ? "true" : "false";
  info.hardware_parameters["vlim"] = "1.0";
  if (gravity_ff) {
    info.hardware_parameters["gravity_ff"] = "true";
    info.hardware_parameters["urdf_path"] = urdf_path;
    info.hardware_parameters["gravity_ff_scale"] = std::to_string(gravity_ff_scale);
    info.hardware_parameters["_ki_hold_for_test"] = "";   // 占位（每关节 param 见下）
  }
  // torque_max / kp_hold / kd_hold 只有 gravity_ff 时才被校验；POS_VEL 路径不看它们。
  info.joints.push_back(make_joint("joint2", {
    {"motor_id", "2"}, {"motor_type", "4340P"}, {"direction", "-1"}, {"offset", "1.594229"},
    {"p_max", "12.5"}, {"v_max", "10.0"}, {"t_max", "28.0"},
    {"position_min", "-0.25"}, {"position_max", "2.188275"},
    {"sign", "-1"}, {"zero_shift", "-0.191649"},
    {"torque_max", "12.0"}, {"kp_hold", "7.0"}, {"kd_hold", "0.8"},
    {"ki_hold", std::to_string(ki_hold)}}));
  info.joints.push_back(make_joint("joint6", {
    {"motor_id", "6"}, {"motor_type", "4310"}, {"direction", "1"}, {"offset", "2.010141"},
    {"p_max", "12.5"}, {"v_max", "30.0"}, {"t_max", "10.0"},
    {"position_min", "-3.141593"}, {"position_max", "3.141593"},
    {"sign", "1"}, {"zero_shift", "1.782591"},
    {"torque_max", "3.5"}, {"kp_hold", "7.0"}, {"kd_hold", "0.8"},
    {"ki_hold", std::to_string(ki_hold)}}));
  return info;
}

CallbackReturn init_and_activate(md::DmSystemInterface & iface,
  const hardware_interface::HardwareInfo & info)
{
  hardware_interface::HardwareComponentInterfaceParams params;
  params.hardware_info = info;
  EXPECT_EQ(iface.on_init(params), CallbackReturn::SUCCESS);
  EXPECT_EQ(iface.on_configure(rclcpp_lifecycle::State()), CallbackReturn::SUCCESS);
  return iface.on_activate(rclcpp_lifecycle::State());
}

double state_value(md::DmSystemInterface & iface, const std::string & joint,
  const std::string & iface_name)
{
  for (auto & itf : iface.export_state_interfaces()) {
    if (itf.get_prefix_name() == joint && itf.get_interface_name() == iface_name) {
      const auto value = itf.get_optional<double>();   // get_value() 已弃用
      if (!value.has_value()) {
        ADD_FAILURE() << "状态接口 " << joint << "/" << iface_name << " 还没有值（NaN）";
        return std::nan("");
      }
      return *value;
    }
  }
  ADD_FAILURE() << "找不到状态接口 " << joint << "/" << iface_name;
  return std::nan("");
}

void set_command(md::DmSystemInterface & iface, const std::string & joint, double value)
{
  for (auto & itf : iface.export_command_interfaces()) {
    if (itf.get_prefix_name() == joint &&
      itf.get_interface_name() == hardware_interface::HW_IF_POSITION)
    {
      ASSERT_TRUE(itf.set_value(value));
      return;
    }
  }
  ADD_FAILURE() << "找不到命令接口 " << joint;
}

const rclcpp::Time kT0(0, 0, RCL_ROS_TIME);
const rclcpp::Duration kDt = rclcpp::Duration::from_seconds(0.01);

}  // namespace

// ① 只读模式：一个控制帧都不发，但状态能读到（而且模型侧 ≈ 0 = 真机停放姿态）
TEST(DmSystemInterface, ReadOnlySendsNoControlFrames)
{
  FakeMotorBusIo io;
  md::DmSystemInterface iface(&io);
  const auto info = make_info(false);
  ASSERT_EQ(init_and_activate(iface, info), CallbackReturn::SUCCESS);

  EXPECT_TRUE(io.control_frames.empty()) << "只读模式不该发 POS_VEL";
  EXPECT_TRUE(io.enable_frames.empty()) << "只读模式不该使能";
  for (std::size_t i = 0; i + md::kTxFrameLen <= io.tx.size(); i += md::kTxFrameLen) {
    const uint16_t can_id = static_cast<uint16_t>(io.tx[i + 13] | (io.tx[i + 14] << 8));
    EXPECT_EQ(can_id, md::kCanIdBroadcast) << "只读模式只允许 0x7FF（刷新/寄存器）帧";
  }

  ASSERT_EQ(iface.read(kT0, kDt), return_type::OK);
  // 模型侧：j2 与 j6 都应落在"真机停放姿态"（= 模型 0 / 1.56）附近。
  // 注意用 reported_pos（经过 16 位量化）算期望 —— 用未量化的 pos[id] 会差 6e-5 对不上。
  const double p2 = io.reported_pos(2);
  const double p6 = io.reported_pos(6);
  const double ours2 = -1.0 * (p2 - 1.594229);
  const double ours6 = 1.0 * (p6 - 2.010141);
  EXPECT_NEAR(state_value(iface, "joint2", "position"), md::ours_to_model(ours2, -1, -0.191649), 1e-9);
  EXPECT_NEAR(state_value(iface, "joint6", "position"), md::ours_to_model(ours6, 1, 1.782591), 1e-9);
  EXPECT_NEAR(state_value(iface, "joint2", "position"), 0.0, 0.02) << "真机停放姿态 = 模型 0";

  const std::size_t before = io.tx.size();
  ASSERT_EQ(iface.write(kT0, kDt), return_type::OK);
  EXPECT_EQ(io.tx.size(), before) << "只读模式的 write() 一个字节都不该发";
}

// ② 使能模式：激活时写 0x0A=2、使能、并用**电机侧实测值**发保持帧
TEST(DmSystemInterface, EnabledActivationWritesModeEnableAndHold)
{
  FakeMotorBusIo io;
  md::DmSystemInterface iface(&io);
  const auto info = make_info(true);
  ASSERT_EQ(init_and_activate(iface, info), CallbackReturn::SUCCESS);

  // 每台都写了 0x0A = 2（uint32 小端 02 00 00 00）
  std::size_t mode_writes = 0;
  for (std::size_t i = 0; i + md::kTxFrameLen <= io.tx.size(); i += md::kTxFrameLen) {
    const uint16_t can_id = static_cast<uint16_t>(io.tx[i + 13] | (io.tx[i + 14] << 8));
    if (can_id == md::kCanIdBroadcast && io.tx[i + 23] == 0x55 && io.tx[i + 24] == 0x0A) {
      EXPECT_EQ(io.tx[i + 25], 0x02);
      mode_writes += 1;
    }
  }
  EXPECT_EQ(mode_writes, 2u);

  ASSERT_EQ(io.enable_frames.size(), 2u);
  EXPECT_EQ(io.enable_frames[0], to_vec(md::cmd_frame(2, md::kCmdEnable)));
  EXPECT_EQ(io.enable_frames[1], to_vec(md::cmd_frame(6, md::kCmdEnable)));
  ASSERT_EQ(io.control_frames.size(), 2u);      // 保持帧
  for (std::size_t k = 0; k < 2; ++k) {
    const uint8_t id = static_cast<uint8_t>(2 + 4 * k);            // 2, 6
    EXPECT_EQ(io.control_frames[k], to_vec(md::pos_vel_frame(id, io.pos[id], md::kHoldVlim)));
    // 保持帧用的是**电机侧实测值**（不是模型 0、也不是软限位钳过的值）
    EXPECT_NEAR(md::uint8s_to_float32(io.control_frames[k].data() + 21), io.pos[id], 1e-9);
  }

  // 命令还是 NaN（没人写过）⇒ write() 继续发保持帧，不能当 0
  io.control_frames.clear();
  ASSERT_EQ(iface.write(kT0, kDt), return_type::OK);
  ASSERT_EQ(io.control_frames.size(), 2u);
  EXPECT_NEAR(md::uint8s_to_float32(io.control_frames[0].data() + 21), io.pos[2], 1e-9);
}

// ②b 保持目标是**使能那一刻锁定**的，不是每圈重读实测值（陷阱 #38）
//     重读 = follow-me：没有回复力，重力能把关节慢慢压走且永远不纠正
//     （2026-10-05 真机：20 s 里 j1 −0.0027 rad）。旧实现下这条**必然失败**。
TEST(DmSystemInterface, HoldTargetIsLatchedNotReread)
{
  FakeMotorBusIo io;
  md::DmSystemInterface iface(&io);
  const auto info = make_info(true);
  ASSERT_EQ(init_and_activate(iface, info), CallbackReturn::SUCCESS);

  // 激活时那条保持帧就是"锁定值"（电机侧的量化读数）
  ASSERT_EQ(io.control_frames.size(), 2u);
  const std::vector<uint8_t> locked_frame = io.control_frames[0];          // joint2
  const double locked_pos = md::uint8s_to_float32(locked_frame.data() + 21);

  // 模拟重力/手推把它挪走 0.05 rad，并让总线缓存读到新位置
  io.pos[2] = locked_pos - 0.05;
  io.pos[6] = io.pos[6] + 0.02;
  io.push_feedback(2);
  io.push_feedback(6);
  ASSERT_EQ(iface.read(kT0, kDt), return_type::OK);

  // 命令仍是 NaN（控制器没起）⇒ 发出去的目标必须还是**锁定值**
  io.control_frames.clear();
  ASSERT_EQ(iface.write(kT0, kDt), return_type::OK);
  ASSERT_EQ(io.control_frames.size(), 2u);
  EXPECT_EQ(io.control_frames[0], to_vec(md::pos_vel_frame(2, locked_pos, md::kHoldVlim)))
    << "j2 的保持目标跟着实测值跑了 —— 那就是 follow-me（旧行为），不是锁定";
}

// ③ 命令通路：模型坐标 →（sign/δ）→ q_ours →（direction/offset）→ 电机侧，再闭环读回来
TEST(DmSystemInterface, CommandConvertsAndRoundTrips)
{
  FakeMotorBusIo io;
  md::DmSystemInterface iface(&io);
  const auto info = make_info(true);
  ASSERT_EQ(init_and_activate(iface, info), CallbackReturn::SUCCESS);

  io.control_frames.clear();
  set_command(iface, "joint2", 0.0);            // 模型 0 = 真机停放姿态
  set_command(iface, "joint6", 1.56);
  ASSERT_EQ(iface.write(kT0, kDt), return_type::OK);
  ASSERT_EQ(io.control_frames.size(), 2u);

  // j2：模型 0 ⇒ 电机侧 1.785878（M1b 那天从真机读数解出来的那个数）
  EXPECT_NEAR(md::uint8s_to_float32(io.control_frames[0].data() + 21), 1.785878, 1e-6);
  // vlim 用参数里的 1.0（不是保持帧的 0.1）
  EXPECT_NEAR(md::uint8s_to_float32(io.control_frames[0].data() + 25), 1.0, 1e-9);
  // j6：模型 1.56 ⇒ 电机侧 = direction·(sign·(1.56−δ)) + offset
  const double ours6 = md::model_to_ours(1.56, 1, 1.782591);
  EXPECT_NEAR(md::uint8s_to_float32(io.control_frames[1].data() + 21), 1.0 * ours6 + 2.010141, 1e-6);

  // 假电机已跟过去 ⇒ 下一圈 read() 的状态应回到命令值附近（16 位量化误差 ~4e-4 rad）
  ASSERT_EQ(iface.read(kT0, kDt), return_type::OK);
  EXPECT_NEAR(state_value(iface, "joint2", "position"), 0.0, 1e-3);
  EXPECT_NEAR(state_value(iface, "joint6", "position"), 1.56, 1e-3);
}

// ④ 故障：ERR 不是 0/1 ⇒ read() 报 ERROR（让 CM 停控制器）
TEST(DmSystemInterface, FaultReturnsError)
{
  FakeMotorBusIo io;
  md::DmSystemInterface iface(&io);
  const auto info = make_info(true);
  ASSERT_EQ(init_and_activate(iface, info), CallbackReturn::SUCCESS);

  io.err[2] = 0x0D;                    // 通讯丢失（锁存）
  io.push_feedback(2);
  EXPECT_EQ(iface.read(kT0, kDt), return_type::ERROR);

  // 收尾：deactivate 会全部失能
  ASSERT_EQ(iface.on_deactivate(rclcpp_lifecycle::State()), CallbackReturn::SUCCESS);
  std::size_t disables = 0;
  for (const auto & f : io.enable_frames) {
    if (f[28] == md::kCmdDisable) {disables += 1;}
  }
  EXPECT_EQ(disables, 2u);
}

// ⑤ 参数错误当场拒（不能带着错参数去动电机）
TEST(DmSystemInterface, RejectsBadParams)
{
  auto expect_init_error = [](hardware_interface::HardwareInfo info, const std::string & what) {
      SCOPED_TRACE(what);
      FakeMotorBusIo io;
      md::DmSystemInterface iface(&io);
      hardware_interface::HardwareComponentInterfaceParams params;
      params.hardware_info = info;
      EXPECT_EQ(iface.on_init(params), CallbackReturn::ERROR);
    };

  auto info = make_info(false);
  info.joints[0].parameters.erase("motor_id");
  expect_init_error(info, "缺 motor_id");

  info = make_info(false);
  info.joints[0].parameters["sign"] = "0";
  expect_init_error(info, "sign=0");

  info = make_info(false);
  info.hardware_parameters["vlim"] = "0";
  expect_init_error(info, "vlim=0");

  // position_min > position_max 不是在 on_init 拦的（那是 dm_joint 构造函数的活），
  // 但也不会漏过去：on_configure 里建 Joint 时抛 ⇒ configure 返回 ERROR（同样在碰硬件之前）
  info = make_info(false);
  info.joints[0].parameters["position_min"] = "3.0";    // j2 的 position_max 是 2.188275
  {
    FakeMotorBusIo io;
    md::DmSystemInterface iface(&io);
    hardware_interface::HardwareComponentInterfaceParams params;
    params.hardware_info = info;
    ASSERT_EQ(iface.on_init(params), CallbackReturn::SUCCESS);
    EXPECT_EQ(iface.on_configure(rclcpp_lifecycle::State()), CallbackReturn::ERROR);
  }
}

// ⑥ URDF 解析这一关：ros2_control 的解析器到底有没有把 <joint> 里的 <param> 读出来？
//    这一条是真机现象逼出来的：如果 `joint.parameters` 是空的，插件只能吃默认值
//    （sign=1 / zero_shift=0）⇒ j1 会显示 ≈0 而不是 +1.5708，正好差 90°。
TEST(DmSystemInterface, ParsesJointParamsFromUrdf)
{
  // 与 arm_description 的 xacro 输出**同构**的最小 URDF
  const std::string urdf = R"URDF(<?xml version="1.0"?>
<robot name="t">
  <link name="base_link"/>
  <link name="link1"/>
  <joint name="joint1" type="revolute">
    <parent link="base_link"/>
    <child link="link1"/>
    <limit lower="-1.0" upper="1.0" effort="1.0" velocity="1.0"/>
  </joint>
  <ros2_control name="ArmSystem" type="system">
    <hardware>
      <plugin>motor_driver_hardware/DmSystemInterface</plugin>
      <param name="device">/dev/fake</param>
      <param name="enable_on_activate">false</param>
      <param name="vlim">1.0</param>
    </hardware>
    <joint name="joint1">
      <command_interface name="position"/>
      <state_interface name="position"/>
      <state_interface name="velocity"/>
      <param name="motor_id">1</param>
      <param name="direction">-1</param>
      <param name="offset">1.306486</param>
      <param name="p_max">12.5</param>
      <param name="v_max">10.0</param>
      <param name="t_max">28.0</param>
      <param name="position_min">-0.096419</param>
      <param name="position_max">0.137522</param>
      <param name="sign">-1</param>
      <param name="zero_shift">1.529724</param>
    </joint>
  </ros2_control>
</robot>
)URDF";

  const auto infos = hardware_interface::parse_control_resources_from_urdf(urdf);
  ASSERT_EQ(infos.size(), 1u);
  const auto & info = infos[0];
  ASSERT_EQ(info.joints.size(), 1u);
  ASSERT_TRUE(info.joints[0].parameters.count("sign") != 0)
    << "解析器没把 <joint> 里的 <param> 读进 parameters ⇒ 插件只能吃默认 sign=1/δ=0 ⇒ 差 90°";
  EXPECT_EQ(info.joints[0].parameters.at("sign"), "-1");
  EXPECT_EQ(info.joints[0].parameters.at("zero_shift"), "1.529724");
  EXPECT_EQ(info.joints[0].parameters.at("motor_id"), "1");
  EXPECT_EQ(info.hardware_parameters.at("device"), "/dev/fake");

  // 端到端：j1 的原始读数 +1.347562（M1b 实测）⇒ 模型侧必须是 +1.5708
  FakeMotorBusIo io;
  io.pos[1] = 1.347562;
  md::DmSystemInterface iface(&io);
  ASSERT_EQ(init_and_activate(iface, info), CallbackReturn::SUCCESS);
  ASSERT_EQ(iface.read(kT0, kDt), return_type::OK);
  EXPECT_NEAR(state_value(iface, "joint1", "position"), 1.5708, 5e-4)
    << "模型侧 j1 不对 ⇒ 真机 RViz 里就会差这 90°";
}

// ⑥ gravity_ff=true：模式写 1(MIT) + 保持帧走 MIT + 力矩前馈 = direction × 重力项
TEST(DmSystemInterface, GravityFfSendsMitWithGravityTorque)
{
  std::ifstream probe(ARM_DYN_URDF);
  ASSERT_TRUE(probe.good()) << "找不到 " << ARM_DYN_URDF
                            << " —— 先 `colcon build --packages-select arm_description`";
  probe.close();

  FakeMotorBusIo io;
  md::DmSystemInterface iface(&io);
  ASSERT_EQ(init_and_activate(iface, make_info(true, true, ARM_DYN_URDF)),
    CallbackReturn::SUCCESS);

  // ① 两台都写 0x0A = 1（MIT），且**没有**任何 POS_VEL 帧
  std::size_t mode1 = 0, mode2 = 0;
  for (std::size_t i = 0; i + md::kTxFrameLen <= io.tx.size(); i += md::kTxFrameLen) {
    const uint16_t can_id = static_cast<uint16_t>(io.tx[i + 13] | (io.tx[i + 14] << 8));
    if (can_id == md::kCanIdBroadcast && io.tx[i + 23] == 0x55 && io.tx[i + 24] == 0x0A) {
      const uint32_t v = md::uint8s_to_uint32(&io.tx[i + 25]);
      if (v == 1) {++mode1;} else if (v == 2) {++mode2;}
    }
  }
  EXPECT_EQ(mode1, 2u) << "gravity_ff=true 时该写 0x0A=1（MIT）";
  EXPECT_EQ(mode2, 0u);
  EXPECT_TRUE(io.control_frames.empty()) << "gravity_ff 下不该出现 POS_VEL 控制帧";

  ASSERT_EQ(iface.read(kT0, kDt), return_type::OK);
  io.mit_frames.clear();
  ASSERT_EQ(iface.write(kT0, kDt), return_type::OK);
  ASSERT_EQ(io.mit_frames.size(), 2u) << "命令是 NaN ⇒ 每台发一帧 MIT 保持帧";

  // ② 期望的 t_ff = direction × (重力项在**实测姿态**上的值)。
  //    重力项本身已经过 2.3 与 MuJoCo 逐点对拍，这里验的是"接进插件之后符号链还对"。
  const double q2 = state_value(iface, "joint2", "position");
  const double q6 = state_value(iface, "joint6", "position");
  std::vector<double> q_model = {q2, q6};
  md::GravityModel gm(ARM_DYN_URDF, {"joint2", "joint6"}, {-1, 1});
  std::vector<double> tau_ours(2, 0.0);
  gm.tau_ours(q_model.data(), tau_ours.data());

  struct Exp {uint8_t id; double direction; double t_max;};
  const Exp exps[] = {{2, -1.0, 28.0}, {6, 1.0, 10.0}};
  for (std::size_t k = 0; k < 2; ++k) {
    const auto & f = io.mit_frames[k];
    EXPECT_EQ(f[13], exps[k].id) << "MIT 帧的 CAN ID 必须是电机 id 本身（不是 0x100+id）";
    // 12 位定点，容差给 1.5 个 LSB（编码是**向零截断**，不是四舍五入）
    const double lsb = 2.0 * exps[k].t_max / 4095.0;
    EXPECT_NEAR(decode_mit_tau(f, exps[k].t_max), exps[k].direction * tau_ours[k], 1.5 * lsb)
      << "关节 " << static_cast<int>(exps[k].id) << " 的 t_ff 不等于 direction × 重力项";
  }
}

// ⑦ gravity_ff=true 但**只读**：仍然一个控制帧都不发（含 MIT）
TEST(DmSystemInterface, GravityFfReadOnlyStillSendsNothing)
{
  std::ifstream probe(ARM_DYN_URDF);
  ASSERT_TRUE(probe.good()) << "找不到 " << ARM_DYN_URDF;
  probe.close();

  FakeMotorBusIo io;
  md::DmSystemInterface iface(&io);
  ASSERT_EQ(init_and_activate(iface, make_info(false, true, ARM_DYN_URDF)),
    CallbackReturn::SUCCESS);
  io.mit_frames.clear();
  const std::size_t before = io.tx.size();
  ASSERT_EQ(iface.write(kT0, kDt), return_type::OK);
  EXPECT_EQ(io.tx.size(), before) << "只读模式（即使 gravity_ff=true）一个字节都不该发";
  EXPECT_TRUE(io.mit_frames.empty());
}

// ⑧ gravity_ff_scale：帧里的 t_ff 按比例减弱（分级上电靠它）
TEST(DmSystemInterface, GravityFfScaleAttenuatesTorque)
{
  std::ifstream probe(ARM_DYN_URDF);
  ASSERT_TRUE(probe.good()) << "找不到 " << ARM_DYN_URDF;
  probe.close();

  constexpr double kScale = 0.25;
  FakeMotorBusIo io;
  md::DmSystemInterface iface(&io);
  ASSERT_EQ(init_and_activate(iface, make_info(true, true, ARM_DYN_URDF, kScale)),
    CallbackReturn::SUCCESS);
  ASSERT_EQ(iface.read(kT0, kDt), return_type::OK);
  io.mit_frames.clear();
  ASSERT_EQ(iface.write(kT0, kDt), return_type::OK);
  ASSERT_EQ(io.mit_frames.size(), 2u);

  std::vector<double> q_model = {state_value(iface, "joint2", "position"),
                                 state_value(iface, "joint6", "position")};
  md::GravityModel gm(ARM_DYN_URDF, {"joint2", "joint6"}, {-1, 1});
  std::vector<double> tau_ours(2, 0.0);
  gm.tau_ours(q_model.data(), tau_ours.data());

  struct Exp {uint8_t id; double direction; double t_max;};
  const Exp exps[] = {{2, -1.0, 28.0}, {6, 1.0, 10.0}};
  for (std::size_t k = 0; k < 2; ++k) {
    const double lsb = 2.0 * exps[k].t_max / 4095.0;
    EXPECT_NEAR(decode_mit_tau(io.mit_frames[k], exps[k].t_max),
      kScale * exps[k].direction * tau_ours[k], 1.5 * lsb)
      << "scale 没有按比例作用";
  }

  // 缩放范围外必须**当场拒**（只允许减弱，不允许放大）
  FakeMotorBusIo io2;
  md::DmSystemInterface bad(&io2);
  hardware_interface::HardwareComponentInterfaceParams params;
  params.hardware_info = make_info(true, true, ARM_DYN_URDF, 2.0);
  EXPECT_EQ(bad.on_init(params), CallbackReturn::ERROR) << "scale=2.0 该被拒";
}

// ⑨ 残差守卫的**宽限 + 防抖**：使能瞬间的速度尖峰不该锁死前馈，持续越限才该锁死。
//   （2026-10-05 真机踩过：位置只偏 0.003 rad，却因为激活瞬间一个速度采样把前馈永久锁死，
//     整轮数据作废。使能到第一次 write 之间 MIT 是零增益的，那时机械臂真的在掉。）
TEST(DmSystemInterface, GravityGuardGracePeriodAndDebounce)
{
  std::ifstream probe(ARM_DYN_URDF);
  ASSERT_TRUE(probe.good()) << "找不到 " << ARM_DYN_URDF;
  probe.close();

  FakeMotorBusIo io;
  // 1.5 rad/s：超守卫阈值 0.5，但 kd×1.5 = 1.2 N·m 仍在 torque_max=3.5 内
  //（给 5.0 会让 kd 项到 4.0 N·m，被 clamp_mit_torque 正确地拒发 —— 那是另一条路径）
  io.vel[6] = 1.5;
  md::DmSystemInterface iface(&io);
  ASSERT_EQ(init_and_activate(iface, make_info(true, true, ARM_DYN_URDF)),
    CallbackReturn::SUCCESS);

  ASSERT_EQ(iface.read(kT0, kDt), return_type::OK);
  std::vector<double> q_model = {state_value(iface, "joint2", "position"),
                                 state_value(iface, "joint6", "position")};
  md::GravityModel gm(ARM_DYN_URDF, {"joint2", "joint6"}, {-1, 1});
  std::vector<double> tau_ours(2, 0.0);
  gm.tau_ours(q_model.data(), tau_ours.data());
  const double expect_j2 = -1.0 * tau_ours[0];          // direction × 重力项
  const double lsb_j2 = 2.0 * 28.0 / 4095.0;

  auto write_at = [&](double t) {
      return iface.write(rclcpp::Time(static_cast<int64_t>(t * 1e9), RCL_ROS_TIME), kDt);
    };
  auto last_j2_tau = [&]() {
      // 每周期按 joints_ 顺序写两帧（joint2 在前、joint6 在后）⇒ 最后两帧里的**前一帧**是 j2
      EXPECT_GE(io.mit_frames.size(), 2u);
      return decode_mit_tau(io.mit_frames[io.mit_frames.size() - 2], 28.0);
    };

  // ① 宽限期内（<0.5s）持续报 5 rad/s：**不该**触发 ⇒ j2 的前馈照发
  for (int k = 0; k < 30; ++k) {                       // 0.01~0.30 s（都在宽限期 0.5s 内）
    ASSERT_EQ(iface.read(kT0, kDt), return_type::OK);
    ASSERT_EQ(write_at(0.01 * (k + 1)), return_type::OK);
  }
  EXPECT_NEAR(last_j2_tau(), expect_j2, 1.5 * lsb_j2)
    << "宽限期内就被速度锁死了（真机就是这么废掉一整轮的）";

  // ② 过了宽限期仍持续越限 ≥ 0.2 s ⇒ **该**锁死 ⇒ 之后 t_ff 变 0
  for (int k = 0; k < 80; ++k) {                       // 0.31~1.10 s
    ASSERT_EQ(iface.read(kT0, kDt), return_type::OK);
    ASSERT_EQ(write_at(0.31 + 0.01 * k), return_type::OK);
  }
  EXPECT_NEAR(last_j2_tau(), 0.0, 1.5 * lsb_j2) << "持续越限后没锁死";

  // ③ 位置越界是**即时**判的（不给宽限）
  FakeMotorBusIo io2;
  md::DmSystemInterface iface2(&io2);
  ASSERT_EQ(init_and_activate(iface2, make_info(true, true, ARM_DYN_URDF)),
    CallbackReturn::SUCCESS);
  ASSERT_EQ(iface2.read(kT0, kDt), return_type::OK);
  io2.pos[2] += 0.5;                                   // 电机侧挪 0.5 rad ⇒ 远超 0.1 阈值
  io2.push_feedback(2);                                // read() 只 poll 不发刷新 ⇒ 得主动喂一帧
  ASSERT_EQ(iface2.read(kT0, kDt), return_type::OK);
  io2.mit_frames.clear();
  ASSERT_EQ(iface2.write(rclcpp::Time(static_cast<int64_t>(1e9), RCL_ROS_TIME), kDt),
    return_type::OK);
  ASSERT_FALSE(io2.mit_frames.empty());
  EXPECT_NEAR(decode_mit_tau(io2.mit_frames[0], 28.0), 0.0, 1.5 * lsb_j2)
    << "位置越界该在第一帧就锁死";
}

// ⑩ 宿主侧积分（ki_hold）：默认 0 不生效；>0 时按误差爬升、到上限停积、命令跳变/守卫触发就复位
TEST(DmSystemInterface, GravityIntegratorRampsClampsAndResets)
{
  std::ifstream probe(ARM_DYN_URDF);
  ASSERT_TRUE(probe.good()) << "找不到 " << ARM_DYN_URDF;
  probe.close();

  constexpr double kKi = 100.0;                 // τ_i 每周期 += ki·err·dt
  FakeMotorBusIo io;
  md::DmSystemInterface iface(&io);
  ASSERT_EQ(init_and_activate(iface, make_info(true, true, ARM_DYN_URDF, 1.0, kKi)),
    CallbackReturn::SUCCESS);
  ASSERT_EQ(iface.read(kT0, kDt), return_type::OK);
  const double hold_model = state_value(iface, "joint2", "position");   // 激活时的（= 保持目标）

  io.freeze_pos = true;                         // 之后假电机不再跟命令 ⇒ 误差持续存在
  io.pos[2] += 0.05;                            // 偏离保持点 0.05 rad（< 守卫 0.1，不会触发守卫）
  io.push_feedback(2);
  ASSERT_EQ(iface.read(kT0, kDt), return_type::OK);

  // 每周期按 joints_ 顺序写两帧 ⇒ 最后两帧里的**前一帧**是 joint2
  auto step = [&](double t) {
      io.mit_frames.clear();
      EXPECT_EQ(iface.read(kT0, kDt), return_type::OK);
      EXPECT_EQ(iface.write(rclcpp::Time(static_cast<int64_t>(t * 1e9), RCL_ROS_TIME), kDt),
        return_type::OK);
      EXPECT_GE(io.mit_frames.size(), 2u);
      return decode_mit_tau(io.mit_frames[io.mit_frames.size() - 2], 28.0);
    };
  const double lsb = 2.0 * 28.0 / 4095.0;
  const double limit = 0.3 * 12.0;              // τ_i 上限 = 0.3 × torque_max(joint2)

  const double tau_ff0 = step(0.01);
  const double tau_ff1 = step(0.02);
  // 误差 ≈ −0.05（motor 侧 +0.05、direction=−1）⇒ 每周期 Δτ_i ≈ 100×(−0.05)×0.01 = −0.05
  EXPECT_NEAR(tau_ff1 - tau_ff0, kKi * (-0.05) * 0.01, 3 * lsb) << "积分没有按误差爬升";

  // 一直积到撞上限（|τ_i| = 0.3×12 = 3.6），然后**停住不再涨**
  double tau_clamped = tau_ff1;
  for (int k = 2; k < 220; ++k) {tau_clamped = step(0.01 * (k + 1));}
  EXPECT_GT(std::fabs(tau_clamped - tau_ff0), limit - 0.2) << "没积到上限";
  const double tau_more = step(2.21);
  EXPECT_NEAR(tau_more, tau_clamped, 3 * lsb) << "饱和后还在涨（条件积分没生效）";

  // 复位：命令参考**单周期跳变 0.10 rad**（> 阈值 0.05），而跟踪误差 0.15 rad（< 守卫 0.3 ⇒ 不触发守卫）
  //   direction=−1 ⇒ 模型坐标下"实测 +0.05"对应"参考 −0.10"（详见 make_info 里 j2 的 sign/δ）
  set_command(iface, "joint2", hold_model - 0.10);
  const double tau_after = step(2.22);
  EXPECT_GT(tau_after - tau_clamped, limit - 0.2)
    << "命令跳变后没复位（旧积分被带过去了）：τ_i 本该从 −3.6 回到 0";
}

// ⑪ 使能时的保持帧**必须绕过软限位钳位** —— 旧实现下这个用例必失败。
//    语义是"待在你现在的位置" ⇒ **任何钳位都会凭空造出一个 PD 项**。
//    真机 2026-10-05 踩过：激活时 j4 被软限位钳掉 0.176 rad ⇒ kp=25 下 PD=4.39 > torque_max=3.5
//    ⇒ **拒发 ⇒ FATAL ⇒ 整条链起不来**（kp=7 时 7×0.176=1.23 侥幸过关，所以一直被掩盖）。
TEST(DmSystemInterface, ActivationHoldFrameBypassesSoftLimits)
{
  std::ifstream probe(ARM_DYN_URDF);
  ASSERT_TRUE(probe.good()) << "找不到 " << ARM_DYN_URDF;
  probe.close();

  FakeMotorBusIo io;
  // j6：sign=+1、offset=2.010141、软限位 ours ∈ [-3.141593, 3.141593]
  //   电机侧 5.4 ⇒ ours 3.39 ⇒ **越上限 0.248 rad**（钳位版会把它拉回 3.1416，差 0.248）
  io.pos[6] = 5.4;
  md::DmSystemInterface iface(&io);
  hardware_interface::HardwareInfo info = make_info(true, true, ARM_DYN_URDF, 1.0);
  for (auto & j : info.joints) {
    if (j.name == "joint6") {j.parameters["kp_hold"] = "25.0";}
  }
  // 钳位版：PD = 25 × 0.248 = 6.2 N·m > torque_max 3.5 ⇒ 拒发 ⇒ on_activate 抛异常
  ASSERT_EQ(init_and_activate(iface, info), CallbackReturn::SUCCESS)
    << "使能时的保持帧被软限位钳位了（旧实现的 bug：钳位造出 PD ⇒ kp 大时拒发 ⇒ 链起不来）";

  // 帧序：on_activate 先给每个关节 enable()（零增益帧），再逐个发真实保持帧
  //   ⇒ [j2.enable, j6.enable, j2.hold, j6.hold] ⇒ **最后一帧**是 j6 的保持帧
  ASSERT_GE(io.mit_frames.size(), 4u);
  const double q_des = decode_mit_pos(io.mit_frames.back());
  EXPECT_NEAR(q_des, 5.4, 2.0 * 25.0 / 65535.0)
    << "保持帧的目标被钳到 " << q_des << " 了；应该等于实测位置 5.4（保持帧不钳位）";
}

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  const int rc = RUN_ALL_TESTS();
  rclcpp::shutdown();
  return rc;
}