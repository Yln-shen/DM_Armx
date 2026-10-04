// test_dm_joint.cpp —— 关节层：与 Python 的 joint.py 对拍（换算、发帧、保持帧语义）。
#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "motor_driver_hardware/dm_bus.hpp"
#include "motor_driver_hardware/dm_joint.hpp"

#ifndef MOTOR_DRIVER_PY_DIR
#error "缺少 MOTOR_DRIVER_PY_DIR（应由 CMake target_compile_definitions 传入）"
#endif

namespace md = motor_driver_hardware;

namespace
{

const md::Limit kLimit4340{12.5, 10.0, 28.0};

class FakeSerial : public md::SerialIo
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

  std::vector<uint8_t> tx;
  std::vector<uint8_t> rx;
  bool open_ = false;
  md::SerialStats stats_;
};

std::string to_hex(const uint8_t * p, std::size_t n)
{
  static const char * kDigits = "0123456789abcdef";
  std::string out;
  for (std::size_t i = 0; i < n; ++i) {
    if (i) {out += ' ';}
    out += kDigits[(p[i] >> 4) & 0x0F];
    out += kDigits[p[i] & 0x0F];
  }
  return out;
}

template<typename T>
std::string to_hex(const T & f) {return to_hex(f.data(), f.size());}

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

// Python 侧驱动：用**真的 joint.py** 算同样的事（FakeBus 收下发送调用）
const char * kPyDriver = R"PY(
import sys
sys.path.insert(0, sys.argv[1])
import dm_frames as F
from joint import Joint

class FakeBus:
    def __init__(self): self.calls = []; self._m = {}
    def motors(self): return self._m
    def add_motor(self, mid, limit): self._m[mid] = tuple(float(x) for x in limit)
    def get_state(self, mid): return None
    def send_pos_vel(self, mid, p, v): self.calls.append(("pos_vel", mid, p, v))
    def send_enable(self, mid): self.calls.append(("enable", mid))
    def send_disable(self, mid): self.calls.append(("disable", mid))

def hx(b): return " ".join("%02x" % x for x in b)

for line in sys.stdin:
    a = line.split()
    if not a: continue
    # name direction offset pmin pmax p_motor target vlim
    name = a[0]; direction = int(a[1]); offset = float(a[2])
    pmin, pmax, p_motor, target, vlim = [float(x) for x in a[3:8]]
    bus = FakeBus()
    j = Joint(bus, 1, name, direction, (p_motor, 30.0, 10.0), offset,
              position_min=pmin, position_max=pmax, mode=2)
    motor_pos = j.prepare_frame(target)
    j.set_pos_vel(target, vlim)
    _, mid, p_sent, v_sent = bus.calls[-1]
    print("%.17g %s" % (motor_pos, hx(F.pos_vel_frame(mid, p_sent, v_sent))))
)PY";

std::vector<std::string> python_results(const std::vector<std::string> & cases)
{
  const std::string script = "/tmp/dm_joint_crosscheck.py";
  const std::string input = "/tmp/dm_joint_crosscheck_in.txt";
  {
    std::ofstream f(script);
    f << kPyDriver;
  }
  {
    std::ofstream f(input);
    for (const auto & c : cases) {f << c << "\n";}
  }
  const std::string cmd = std::string("python3 ") + script + " " + MOTOR_DRIVER_PY_DIR +
    " < " + input;
  FILE * pipe = popen(cmd.c_str(), "r");
  std::vector<std::string> out;
  if (pipe == nullptr) {return out;}
  char buf[4096];
  while (std::fgets(buf, sizeof(buf), pipe) != nullptr) {
    std::string s(buf);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) {s.pop_back();}
    out.push_back(s);
  }
  pclose(pipe);
  return out;
}

std::string format_motor_pos(double v)
{
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%.17g", v);
  return std::string(buf);
}

// 一台 j2 那样的关节（direction=−1，零位在 offset），软限位是我们标定的那两个数
md::JointConfig config_j2()
{
  md::JointConfig cfg;
  cfg.name = "joint2";
  cfg.motor_id = 2;
  cfg.motor_type = "4340P";
  cfg.direction = -1;
  cfg.offset = 1.594229;
  cfg.limit = kLimit4340;
  cfg.position_min = -0.25;
  cfg.position_max = 2.188275;
  cfg.mode = 2;
  return cfg;
}

}  // namespace

// ① prepare_frame / set_pos_vel 与 Python 逐字节 + 逐数值对拍
TEST(DmJoint, PrepareFrameMatchesPython)
{
  const std::vector<std::string> lines = {
    // name  direction offset   pmin     pmax      p_motor  target  vlim
    "joint2 -1 1.594229 -0.25  2.188275  12.5     0.0     0.3",
    "joint2 -1 1.594229 -0.25  2.188275  12.5     1.0     0.5",
    "joint2 -1 1.594229 -0.25  2.188275  12.5     -0.5    0.1",
    "joint1 -1 1.306486 -0.096419 0.137522 12.5   0.0     0.2",
    "joint6  1 2.010141 -3.141593 3.141593 12.5   1.7     0.05",
    "joint4 -1 1.675259 -1.095672 0.0     12.5    0.3     0.4",
  };
  const auto got = python_results(lines);
  ASSERT_EQ(got.size(), lines.size()) << "Python 侧没有逐行给结果（脚本/import 出错？）";

  for (std::size_t i = 0; i < lines.size(); ++i) {
    // 解析这一行成 C++ 的配置 + 目标
    std::istringstream is(lines[i]);
    std::string name, motor_type;
    int direction = 1;
    double offset = 0, pmin = 0, pmax = 0, p_motor = 0, target = 0, vlim = 0;
    is >> name >> direction >> offset >> pmin >> pmax >> p_motor >> target >> vlim;

    FakeSerial io;
    md::MotorBus bus(io);
    bus.open();
    md::JointConfig cfg;
    cfg.name = name;
    cfg.motor_id = 1;
    cfg.direction = direction;
    cfg.offset = offset;
    cfg.limit = md::Limit{p_motor, 30.0, 10.0};
    cfg.position_min = pmin;
    cfg.position_max = pmax;
    cfg.mode = 2;
    md::Joint joint(bus, cfg);
    const double motor_pos = joint.prepare_frame(target);
    joint.set_pos_vel(target, vlim);

    const std::string cpp = format_motor_pos(motor_pos) + " " + to_hex(io.tx.data(), io.tx.size());
    EXPECT_EQ(got[i], cpp) << "用例 #" << i << "：" << lines[i];
  }
}

// ② 使能：没缓存位置必须拒；有位置则发"命令帧 + 用实测值的保持帧"
TEST(DmJoint, EnableNeedsStateAndSendsHoldFrame)
{
  FakeSerial io;
  md::MotorBus bus(io);
  bus.open();
  md::Joint joint(bus, config_j2());

  EXPECT_THROW(joint.enable(), std::runtime_error);      // 没位置 ⇒ 拒（否则会朝零位冲）
  EXPECT_EQ(io.tx.size(), 0u) << "拒使能时一个字节都不该发";

  // 注入一条反馈（电机侧 pos = +1.0），poll 进缓存
  io.push(feedback_frame({0x02, 0x28, 0xF6, 0x00, 0x00, 0x00, 0x1E, 0x1F}));
  ASSERT_EQ(bus.poll(), 1u);
  const double measured = bus.get_state(2)->pos;

  io.tx.clear();
  joint.enable();
  // 两帧：使能命令 + 保持帧（保持帧用**电机侧实测值**、vlim = 0.1）
  std::vector<uint8_t> expect;
  for (const auto & f : {md::cmd_frame(2, md::kCmdEnable), md::pos_vel_frame(2, measured, md::kHoldVlim)}) {
    expect.insert(expect.end(), f.begin(), f.end());
  }
  EXPECT_EQ(to_hex(io.tx.data(), io.tx.size()), to_hex(expect.data(), expect.size()));
}

// ③ get_state：位置换算、速度/力矩只乘 direction
TEST(DmJoint, GetStateConvertsToJointSide)
{
  FakeSerial io;
  md::MotorBus bus(io);
  bus.open();
  const md::JointConfig cfg = config_j2();
  md::Joint joint(bus, cfg);

  // 数据段：id2 / ERR=1(使能) / pos=0x8000(=0) / vel=0x800 / tau=0x800
  io.push(feedback_frame({0x12, 0x80, 0x00, 0x80, 0x08, 0x00, 0x1E, 0x1F}));
  ASSERT_EQ(bus.poll(), 1u);
  const md::MotorState m = *bus.get_state(2);
  const md::JointState st = joint.get_state();

  EXPECT_EQ(st.name, "joint2");
  EXPECT_EQ(st.err, 1u);
  EXPECT_TRUE(st.enabled);
  EXPECT_DOUBLE_EQ(st.position, cfg.direction * (m.pos - cfg.offset));
  EXPECT_DOUBLE_EQ(st.velocity, cfg.direction * m.vel);
  EXPECT_DOUBLE_EQ(st.torque, cfg.direction * m.tau);
  EXPECT_DOUBLE_EQ(st.position, -1.0 * (m.pos - 1.594229));
  EXPECT_EQ(st.t_mos_raw, 0x1Eu);
}

// ④ 故障：assert_healthy 先失能再抛
TEST(DmJoint, AssertHealthyDisablesThenThrows)
{
  FakeSerial io;
  md::MotorBus bus(io);
  bus.open();
  md::Joint joint(bus, config_j2());

  // ERR = 0xD（通讯丢失，锁存）
  io.push(feedback_frame({0xD2, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1E, 0x1F}));
  ASSERT_EQ(bus.poll(), 1u);

  io.tx.clear();
  try {
    joint.assert_healthy();
    FAIL() << "故障必须抛";
  } catch (const std::runtime_error & e) {
    const std::string msg = e.what();
    EXPECT_NE(msg.find("ERR=13"), std::string::npos) << msg;
    EXPECT_NE(msg.find("锁存"), std::string::npos) << msg;
  }
  EXPECT_EQ(to_hex(io.tx.data(), io.tx.size()), to_hex(md::cmd_frame(2, md::kCmdDisable)));

  // ERR=1（使能）是正常状态，不抛
  FakeSerial io2;
  md::MotorBus bus2(io2);
  bus2.open();
  md::Joint joint2(bus2, config_j2());
  io2.push(feedback_frame({0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1E, 0x1F}));
  ASSERT_EQ(bus2.poll(), 1u);
  EXPECT_NO_THROW(joint2.assert_healthy());
}

// ⑤ 参数与用法错误：当场拒，不静默
TEST(DmJoint, RejectsBadConfigAndUse)
{
  FakeSerial io;
  md::MotorBus bus(io);
  bus.open();

  md::JointConfig bad = config_j2();
  bad.direction = 0;
  EXPECT_THROW(md::Joint(bus, bad), std::invalid_argument);

  bad = config_j2();
  bad.mode = 1;                                  // C++ 这层只做 POS_VEL
  EXPECT_THROW(md::Joint(bus, bad), std::invalid_argument);

  bad = config_j2();
  bad.position_min = 2.0;
  bad.position_max = 1.0;
  EXPECT_THROW(md::Joint(bus, bad), std::invalid_argument);

  bad = config_j2();
  bad.limit = md::Limit{-1.0, 10.0, 28.0};       // 档位必须正
  EXPECT_THROW(md::Joint(bus, bad), std::invalid_argument);

  md::Joint joint(bus, config_j2());
  EXPECT_THROW(joint.set_pos_vel(0.0, -0.1), std::invalid_argument);      // vlim 负
  EXPECT_THROW(joint.set_pos_vel(std::nan(""), 0.1), std::invalid_argument);   // NaN 拒
  EXPECT_THROW(joint.prepare_frame(std::nan("")), std::invalid_argument);
  EXPECT_DOUBLE_EQ(joint.clamp_pmax(1e9), 12.5);       // PMAX 钳位
  EXPECT_DOUBLE_EQ(joint.clamp_pmax(-1e9), -12.5);

  // 同一台电机、档位不同 ⇒ 第二个 Joint 构造即拒（否则力矩静默差数倍）
  md::JointConfig other = config_j2();
  other.name = "joint2-again";
  other.limit = md::Limit{12.5, 30.0, 10.0};
  EXPECT_THROW(md::Joint(bus, other), std::invalid_argument);
}
