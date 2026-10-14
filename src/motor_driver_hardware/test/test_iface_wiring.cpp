// test_iface_wiring.cpp —— 插件的**接线**测试（LAYERS §8.1 ②）：验证插件真的按约定的顺序
// 去读写串口，以及使能/保持/故障这几条**不可能只靠纯函数覆盖**的路径。
//
// 假串口在这里退化成**记录器**：只记 `write()` 收到了什么；另配一台最小"假电机"回答刷新帧与
// 寄存器帧（否则 on_activate 的 sync_states 会等满 8 秒）。
// ⚠️ 本文件**不测控制律**，也**不测 MIT** —— 2026-10-14 起 C++ 侧只走 POS_VEL（`0x0A=2`）。
//    MIT 帧的构造仍在协议层（`dm_frames::mit_frame`），由 `test_dm_frames` 与 Python 逐字节对拍。
#include <gtest/gtest.h>

#include <hardware_interface/component_parser.hpp>
#include <hardware_interface/types/hardware_component_interface_params.hpp>
#include <hardware_interface/types/hardware_interface_type_values.hpp>
#include <rclcpp/rclcpp.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "motor_driver_hardware/dm_system_interface.hpp"

namespace md = motor_driver_hardware;
using hardware_interface::CallbackReturn;
using hardware_interface::return_type;

namespace
{
const double kNaN = std::numeric_limits<double>::quiet_NaN();

// 假电机的初始姿态：真机停放姿态（电机侧 1.7909 rad）。
// joint2：direction=−1、offset=1.594229 ⇒ q_ours = 0.196671、q_urdf = −0.003379
// joint6：direction=+1、offset=2.010141 ⇒ q_ours = −0.219241、q_urdf = −0.004100
constexpr double kMotorPos2 = 1.7909;
constexpr double kMotorPos6 = 1.7909;
constexpr double kPmax = 12.5;

uint16_t encode_pos(double p_m)
{
  double u = (p_m + kPmax) / (2.0 * kPmax) * 65535.0;
  u = std::max(0.0, std::min(u, 65535.0));
  return static_cast<uint16_t>(u);
}

// 反馈帧里的位置是 16 位量化过的 ⇒ 算期望值要用**它**，不能用未量化的 kMotorPos2
double reported_motor_pos(uint8_t id)
{
  const double raw = (id == 2) ? kMotorPos2 : kMotorPos6;
  return (static_cast<double>(encode_pos(raw)) / 65535.0) * 2.0 * kPmax - kPmax;
}

// 一帧 30 字节：CAN ID 在 f[13..14]，数据段在 f[21..28]（与 dm_frames::build_tx 一致）
//
// 帧类型判据（按 CAN ID 的取值域分，**互斥**）：
//   · 刷新 / 寄存器：CAN ID = 广播 0x7FF（目标电机在数据段里）
//   · 位置速度：      CAN ID = 0x100 + id，数据段 = float32 p_des + float32 v_des
//   · MIT / 使能失能：CAN ID = **电机 id 本身**（同一个 ID，只能靠数据段区分）
//       使能失能 = 数据段前 7 字节全 0xFF 且第 8 个是 0xFC/0xFD；其余就是 MIT
struct Frame
{
  std::vector<uint8_t> b;
  uint16_t can_id() const {return static_cast<uint16_t>(b[13] | (b[14] << 8));}
  uint8_t d(std::size_t i) const {return b[21 + i];}
  bool is_broadcast() const {return can_id() == md::kCanIdBroadcast;}
  bool is_pos_vel() const
  {
    return can_id() >= md::kCanIdPosVelBase && can_id() < md::kCanIdPosVelBase + 0x0100;
  }
  // ⚠️ 广播帧（刷新/寄存器）**绝不是**控制帧 —— 别靠"CAN ID 落在 1~6"来判控制帧，
  //    寄存器帧的数据段[0] 也正是目标电机 ID（我第一版就栽在这里）。
  bool is_enable_cmd() const
  {
    if (is_broadcast() || can_id() < 1 || can_id() > 6) {return false;}
    for (std::size_t k = 0; k <= 6; ++k) {if (d(k) != 0xFF) {return false;}}
    return d(7) == md::kCmdEnable || d(7) == md::kCmdDisable;
  }
  bool is_mit() const
  {
    return !is_broadcast() && !is_pos_vel() && can_id() >= 1 && can_id() <= 6 && !is_enable_cmd();
  }
  bool is_reg_write(uint8_t rid) const
  {
    return is_broadcast() && d(2) == md::kRegCmdWrite && d(3) == rid;
  }
  uint8_t reg_target_id() const {return d(0);}
  uint8_t reg_raw_byte() const {return d(4);}       // uint32 小端 ⇒ 最低字节就是 1/2
  double pos_vel_target() const {return md::uint8s_to_float32(b.data() + 21);}
  double mit_position() const
  {
    const uint32_t q_u = (static_cast<uint32_t>(d(0)) << 8) | d(1);
    return (static_cast<double>(q_u) / 65535.0) * 2.0 * kPmax - kPmax;
  }
  double mit_tau(double t_max) const
  {
    const uint32_t t_u = (static_cast<uint32_t>(d(6) & 0x0F) << 8) | d(7);
    return (static_cast<double>(t_u) / 4095.0) * 2.0 * t_max - t_max;
  }
};

class RecorderIo : public md::SerialIo
{
public:
  void open() override {open_ = true;}
  void close() override {open_ = false;}
  bool is_open() const override {return open_;}
  void write(const uint8_t * data, std::size_t len) override
  {
    if (len != md::kTxFrameLen) {return;}
    stats_.tx_calls += 1;
    stats_.tx_bytes += len;
    Frame f;
    f.b.assign(data, data + len);
    frames.push_back(f);
    respond(f);
  }
  std::size_t read_available(std::vector<uint8_t> & out) override
  {
    const std::size_t n = rx.size();
    out.insert(out.end(), rx.begin(), rx.end());
    rx.clear();
    stats_.rx_calls += 1;
    stats_.rx_bytes += n;
    return n;
  }
  std::size_t bytes_available() const override {return rx.size();}
  void flush_input() override {rx.clear();}
  const md::SerialStats & stats() const override {return stats_;}

  // ── 记录器的查询接口（测试用）──
  const Frame * last_mit(uint8_t id) const
  {
    for (auto it = frames.rbegin(); it != frames.rend(); ++it) {
      if (it->is_mit() && it->can_id() == id) {return &*it;}
    }
    return nullptr;
  }
  const Frame * last_pos_vel(uint8_t id) const
  {
    for (auto it = frames.rbegin(); it != frames.rend(); ++it) {
      if (it->is_pos_vel() && it->can_id() == md::kCanIdPosVelBase + id) {return &*it;}
    }
    return nullptr;
  }
  std::vector<Frame> slice(std::size_t from) const
  {
    return std::vector<Frame>(frames.begin() + static_cast<long>(from), frames.end());
  }
  void clear() {frames.clear();}

  std::map<uint8_t, double> motor_pos{{2, kMotorPos2}, {6, kMotorPos6}};
  std::map<uint8_t, uint8_t> motor_err{{2, 0x1}, {6, 0x1}};

private:
  void push_feedback(uint8_t id)
  {
    const uint16_t pos_u = encode_pos(motor_pos[id]);
    const uint8_t d0 = static_cast<uint8_t>((id & 0x0F) | ((motor_err[id] & 0x0F) << 4));
    // 速度字段：12 位映射里 0 落在中点 2047 ⇒ D[3]=0x7F、D[4] 低 4 位给 0（tau 高 4 位）
    std::vector<uint8_t> f(16, 0);
    f[0] = 0xAA; f[1] = 0x11;
    f[7] = d0;
    f[8] = static_cast<uint8_t>(pos_u >> 8);
    f[9] = static_cast<uint8_t>(pos_u & 0xFF);
    f[10] = 0x7F; f[11] = 0xF0;
    f[12] = 0x00; f[13] = 0x1E; f[14] = 0x1F;
    f[15] = 0x55;
    rx.insert(rx.end(), f.begin(), f.end());
  }
  // 最小假电机：刷新帧 → 回状态；寄存器帧 → 回显 RID（write_register 要匹配回包）；
  // 使能帧 → 置 ERR=1；POS_VEL 命令 → 固件跟到命令位置。**MIT 帧不跟随**
  // （真机 MIT 的实际运动由 PD + tau_ff 决定，测试只关心"发出的字节对不对"）。
  void respond(const Frame & f)
  {
    if (f.can_id() == md::kCanIdBroadcast) {
      const uint8_t target = f.d(0);
      const uint8_t cmd = f.d(2);
      if (cmd == 0xCC) {
        push_feedback(target);
      } else if (cmd == md::kRegCmdRead || cmd == md::kRegCmdWrite || cmd == md::kRegCmdSave) {
        std::vector<uint8_t> r(16, 0);
        r[0] = 0xAA; r[1] = 0x11;
        r[7] = target; r[8] = 0x00; r[9] = cmd; r[10] = f.d(3);
        r[15] = 0x55;
        rx.insert(rx.end(), r.begin(), r.end());
      }
      return;
    }
    if (f.is_pos_vel()) {
      const uint8_t id = static_cast<uint8_t>(f.can_id() - md::kCanIdPosVelBase);
      motor_pos[id] = md::uint8s_to_float32(f.b.data() + 21);
      push_feedback(id);
      return;
    }
    if (f.is_enable_cmd()) {
      const uint8_t id = f.can_id();
      motor_err[id] = (f.d(7) == md::kCmdEnable) ? 0x1 : 0x0;
      push_feedback(id);
    }
  }

public:
  std::vector<Frame> frames;
  std::vector<uint8_t> rx;
  bool open_ = false;
  md::SerialStats stats_;
};

// ── 建 HardwareInfo（direction/offset 来自 joint.yaml，sign/δ 来自 align.yaml）──
hardware_interface::ComponentInfo make_joint(const std::string & name,
  std::initializer_list<std::pair<const std::string, std::string>> params)
{
  hardware_interface::InterfaceInfo pos;
  pos.name = hardware_interface::HW_IF_POSITION;
  hardware_interface::InterfaceInfo vel;
  vel.name = hardware_interface::HW_IF_VELOCITY;
  hardware_interface::ComponentInfo j;
  j.name = name;
  j.parameters = params;
  j.command_interfaces.push_back(pos);
  j.state_interfaces.push_back(pos);
  j.state_interfaces.push_back(vel);
  return j;
}

hardware_interface::HardwareInfo make_info(bool enable_on_activate)
{
  hardware_interface::HardwareInfo info;
  info.name = "ArmReal";
  info.type = "system";
  info.hardware_parameters["device"] = "/dev/fake";
  info.hardware_parameters["baud"] = "921600";
  info.hardware_parameters["enable_on_activate"] = enable_on_activate ? "true" : "false";
  info.hardware_parameters["vlim"] = "1.0";
  info.joints.push_back(make_joint("joint2", {
    {"motor_id", "2"}, {"motor_type", "4340P"}, {"direction", "-1"}, {"offset", "1.594229"},
    {"p_max", "12.5"}, {"v_max", "10.0"}, {"t_max", "28.0"},
    {"position_min", "-0.25"}, {"position_max", "2.188275"},
    {"sign", "-1"}, {"zero_shift", "-0.191649"}}));
  info.joints.push_back(make_joint("joint6", {
    {"motor_id", "6"}, {"motor_type", "4310"}, {"direction", "1"}, {"offset", "2.010141"},
    {"p_max", "12.5"}, {"v_max", "30.0"}, {"t_max", "10.0"},
    {"position_min", "-3.141593"}, {"position_max", "3.141593"},
    {"sign", "1"}, {"zero_shift", "1.782591"}}));
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

// 只改命令接口；没有这个接口就当场失败
void set_command(md::DmSystemInterface & iface, const std::string & joint,
  const std::string & iface_name, double value)
{
  for (auto & itf : iface.export_command_interfaces()) {
    if (itf.get_prefix_name() == joint && itf.get_interface_name() == iface_name) {
      ASSERT_TRUE(itf.set_value(value));
      return;
    }
  }
  ADD_FAILURE() << "找不到命令接口 " << joint << "/" << iface_name;
}

const rclcpp::Time kT0(0, 0, RCL_ROS_TIME);
const rclcpp::Duration kDt = rclcpp::Duration::from_seconds(0.01);

std::size_t count_reg_writes(const std::vector<Frame> & fs, uint8_t rid)
{
  std::size_t n = 0;
  for (const auto & f : fs) {if (f.is_reg_write(rid)) {++n;}}
  return n;
}
}  // namespace

// ① 只读模式：**一个控制帧都不发**（连 effort 命令接口被写也不发——没使能）
TEST(IfaceWiring, ReadOnlySendsNoControlFrames)
{
  RecorderIo io;
  md::DmSystemInterface iface(&io);
  ASSERT_EQ(init_and_activate(iface, make_info(/*enable_on_activate=*/false)),
    CallbackReturn::SUCCESS);
  io.clear();
  // 就算控制器来写命令，只要没使能就一个字节都不发
  set_command(iface, "joint2", hardware_interface::HW_IF_POSITION, 0.3);
  set_command(iface, "joint2", hardware_interface::HW_IF_EFFORT, 0.5);
  iface.read(kT0, kDt);
  iface.write(kT0, kDt);
  for (const auto & f : io.frames) {
    EXPECT_FALSE(f.is_pos_vel()) << "只读模式发了 POS_VEL 帧";
    EXPECT_FALSE(f.is_mit()) << "只读模式发了 MIT 帧";
    EXPECT_FALSE(f.is_enable_cmd()) << "只读模式发了使能/失能帧";
  }
}

// ② 激活：写模式寄存器 0x0A=2 → 使能帧 → 补保持帧（POS_VEL）
TEST(IfaceWiring, ActivationWritesModeThenEnablesAndHolds)
{
  RecorderIo io;
  md::DmSystemInterface iface(&io);
  ASSERT_EQ(init_and_activate(iface, make_info(/*enable_on_activate=*/true)),
    CallbackReturn::SUCCESS);

  // ① 两台各写了一次 0x0A=2，且**在使能帧之前**（顺序不变量：先切模式再使能，
  //    否则使能瞬间电机还在旧模式，补的保持帧会发错）
  std::size_t n_mode = 0;
  std::size_t last_mode_at = 0;
  std::size_t first_enable_at = io.frames.size();
  for (std::size_t i = 0; i < io.frames.size(); ++i) {
    const Frame & f = io.frames[i];
    if (f.is_reg_write(0x0A)) {
      ++n_mode;
      last_mode_at = i;
      EXPECT_EQ(f.reg_raw_byte(), 2) << "本链只走 POS_VEL，必须写 0x0A=2";
    }
    if (f.is_enable_cmd() && f.d(7) == md::kCmdEnable && first_enable_at == io.frames.size()) {
      first_enable_at = i;
    }
  }
  EXPECT_EQ(n_mode, 2u) << "激活时没有逐台写 0x0A";
  EXPECT_LT(last_mode_at, first_enable_at) << "写 0x0A 必须在使能帧之前";

  // ② 两台各一条使能帧
  std::size_t n_enable = 0;
  for (const auto & f : io.frames) {if (f.is_enable_cmd() && f.d(7) == md::kCmdEnable) {++n_enable;}}
  EXPECT_EQ(n_enable, 2u);

  // ③ 使能后补的保持帧是 **POS_VEL** 帧、位置 = 当前实测位置（不换算不钳位）
  const Frame * hold = io.last_pos_vel(2);
  ASSERT_NE(hold, nullptr) << "激活后没有补 POS_VEL 保持帧";
  EXPECT_NEAR(hold->pos_vel_target(), reported_motor_pos(2), 0.001)
    << "保持帧的位置不是当前实测位置";

  // ④ **一个 MIT 帧都不该有**：C++ 侧的 MIT 路径已砍（协议层的 mit_frame 仍在，但没人调）
  for (const auto & f : io.frames) {
    EXPECT_FALSE(f.is_mit()) << "本链已砍 MIT，不该出现 MIT 帧";
  }
}

// ③ 命令是 NaN ⇒ 发的是**保持帧**，目标是"使能那一刻锁定的"位置，**不每圈重读**
//    （陷阱 #38：重读 = follow-me，没有回复力）
TEST(IfaceWiring, HoldFrameTargetsLatchedPositionNotReread)
{
  RecorderIo io;
  md::DmSystemInterface iface(&io);
  ASSERT_EQ(init_and_activate(iface, make_info(/*enable_on_activate=*/true)),
    CallbackReturn::SUCCESS);                       // 控制模式固定 POS_VEL
  const double latched = reported_motor_pos(2);
  ASSERT_NE(io.last_pos_vel(2), nullptr);
  EXPECT_NEAR(io.last_pos_vel(2)->pos_vel_target(), latched, 0.001);

  // 让假电机"被别人推走"：之后的保持帧**仍然指向锁定的那一点**
  io.motor_pos[2] += 0.4;
  io.clear();
  iface.write(kT0, kDt);
  const Frame * hold = io.last_pos_vel(2);
  ASSERT_NE(hold, nullptr);
  EXPECT_NEAR(hold->pos_vel_target(), latched, 0.001)
    << "保持帧重读了实测位置（follow-me）—— 陷阱 #38 回归";
  EXPECT_NEAR(hold->pos_vel_target(), latched, 0.001);
}

// ④ 故障：已使能时 ERR≠0/1 ⇒ read() 报 ERROR（让 CM 停控制器）；只读时只告警不中断
TEST(IfaceWiring, FaultInArmedModeReportsErrorButIsToleratedWhenReadOnly)
{
  // 只读：容忍（一台故障不该把整条只读链拖死，否则看不见别的关节）
  {
    RecorderIo io;
    md::DmSystemInterface iface(&io);
    ASSERT_EQ(init_and_activate(iface, make_info(/*enable_on_activate=*/false)),
      CallbackReturn::SUCCESS);
    // 刷新帧 → 假电机按 motor_err 回状态；read() 里的刷新每圈都发
    io.motor_err[2] = 0x0B;                        // MOS 过温
    for (int i = 0; i < 3; ++i) {EXPECT_EQ(iface.read(kT0, kDt), return_type::OK);}
  }
  // 已使能：报 ERROR
  {
    RecorderIo io;
    md::DmSystemInterface iface(&io);
    ASSERT_EQ(init_and_activate(iface, make_info(/*enable_on_activate=*/true)),
      CallbackReturn::SUCCESS);
    // 已使能时命令帧本身就带回包 ⇒ 让假电机在**任何**回包里都报这个故障
    io.motor_err[2] = 0x0B;
    iface.write(kT0, kDt);                         // 发一帧命令 ⇒ 假电机回一帧带故障的状态
    EXPECT_EQ(iface.read(kT0, kDt), return_type::ERROR) << "已使能 + 故障却没有报 ERROR";
  }
}
