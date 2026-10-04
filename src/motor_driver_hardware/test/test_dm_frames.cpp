// test_dm_frames.cpp —— C++ 协议层与 Python 侧 dm_frames.py 的**逐字节对拍**。
//
// 为什么要它：`dm_frames` 现在有两份实现（Python 的驱动层 + C++ 的硬件接口层），
// 任何一处改错都会让"电机收到不一样的帧"却又**不报错**。所以这里不写"我期望的字节"，
// 而是**直接调 Python 那份**造同样的帧，逐字节比 —— 一份实现漂了，测试就红。
//
// 跑法：colcon test --packages-select motor_driver_hardware
//       （CMake 把 Python 源码目录经 MOTOR_DRIVER_PY_DIR 编译进来）
#include <gtest/gtest.h>

#include <cstdio>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "motor_driver_hardware/dm_frames.hpp"

#ifndef MOTOR_DRIVER_PY_DIR
#error "缺少 MOTOR_DRIVER_PY_DIR（应由 CMake target_compile_definitions 传入）"
#endif

namespace md = motor_driver_hardware;

namespace
{

const md::Limit kLimit4340{12.5, 10.0, 28.0};   // 实测回读档位
const md::Limit kLimit4310{12.5, 30.0, 10.0};

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

template<typename FrameT>
std::string to_hex(const FrameT & f) {return to_hex(f.data(), f.size());}

// Python 侧驱动脚本：逐行读用例 → 调 motor_driver/dm_frames.py → 逐行打印结果
const char * kPyDriver = R"PY(
import sys
sys.path.insert(0, sys.argv[1])
import dm_frames as F

def hx(b):
    return " ".join("%02x" % x for x in b)

for line in sys.stdin:
    a = line.split()
    if not a:
        continue
    k = a[0]
    if k == "mit":
        sid = int(a[1]); pd, vd, kp, kd, tf = [float(x) for x in a[2:7]]
        lim = tuple(float(x) for x in a[7:10])
        print(hx(F.mit_frame(sid, pd, vd, kp, kd, tf, lim)))
    elif k == "posvel":
        print(hx(F.pos_vel_frame(int(a[1]), float(a[2]), float(a[3]))))
    elif k == "forcepos":
        print(hx(F.force_pos_frame(int(a[1]), float(a[2]), float(a[3]), float(a[4]))))
    elif k == "cmd":
        print(hx(F.cmd_frame(int(a[1]), int(a[2], 0))))
    elif k == "refresh":
        print(hx(F.refresh_frame(int(a[1]))))
    elif k == "regread":
        print(hx(F.reg_read_frame(int(a[1]), int(a[2], 0))))
    elif k == "regwrite":
        print(hx(F.reg_write_frame(int(a[1]), int(a[2], 0),
                                   bytes(int(x, 0) for x in a[3:7]))))
    elif k == "saveparams":
        print(hx(F.save_params_frame(int(a[1]))))
    elif k == "extract":
        raw = bytes(int(x, 16) for x in a[1:])
        print("|".join(hx(f) for f in F.extract_rx(raw)))
    elif k == "rxbuf":
        # a[1:] 是若干 chunk（每个 chunk 用逗号分隔的十六进制），逐 chunk feed+打印 drain
        rx = F.RxBuf()
        outs = []
        for chunk in a[1:]:
            rx.feed(bytes(int(x, 16) for x in chunk.split(",")))
            outs.append("|".join(hx(f) for f in rx.drain()))
        print(" ".join("<%s>" % o for o in outs))
    elif k == "feedback":
        data = bytes(int(x, 16) for x in a[1:9])
        lim = tuple(float(x) for x in a[9:12])
        d = F.decode_feedback(data, lim)
        print("%d %d %.17g %.17g %.17g %d %d" % (d["id"], d["err"], d["pos"], d["vel"],
                                                 d["tau"], d["t_mos_raw"], d["t_rotor_raw"]))
    elif k == "regresp":
        print(hx(F.decode_reg_response(bytes(int(x, 16) for x in a[1:17]))["raw"]))
    else:
        print("UNKNOWN_CASE")
)PY";

// 把用例喂给 Python，拿回逐行结果
std::vector<std::string> python_results(const std::vector<std::string> & cases)
{
  const std::string script = "/tmp/dm_frames_crosscheck.py";
  const std::string input = "/tmp/dm_frames_crosscheck_in.txt";
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

}  // namespace

// ① 发送帧 + 接收切分 + 解码：C++ 与 Python 逐字节/逐数值对拍
TEST(DmFrames, CrossCheckWithPython)
{
  struct Case
  {
    std::string line;      // 喂给 Python 的同一用例
    std::string cpp;       // C++ 侧算出来的结果
  };

  const std::vector<Case> cases = {
    // ── MIT（CAN ID = slave_id 本身）──
    {"mit 1 0.5 0.0 10.0 1.0 0.0 12.5 10 28", to_hex(md::mit_frame(1, 0.5, 0.0, 10.0, 1.0, 0.0, kLimit4340))},
    {"mit 2 -1.25 2.5 30.0 0.5 -1.0 12.5 10 28", to_hex(md::mit_frame(2, -1.25, 2.5, 30.0, 0.5, -1.0, kLimit4340))},
    {"mit 3 0.0 0.0 0.0 0.0 0.0 12.5 10 28", to_hex(md::mit_frame(3, 0.0, 0.0, 0.0, 0.0, 0.0, kLimit4340))},
    // 边界：q/v/t 取 ±上限、kp=500、kd=5
    {"mit 4 12.5 10.0 500.0 5.0 28.0 12.5 10 28", to_hex(md::mit_frame(4, 12.5, 10.0, 500.0, 5.0, 28.0, kLimit4340))},
    {"mit 5 -12.5 -10.0 500.0 5.0 -28.0 12.5 10 28", to_hex(md::mit_frame(5, -12.5, -10.0, 500.0, 5.0, -28.0, kLimit4340))},
    // 超范围（应被钳位）：q=20 → PMAX、kp=600 → 500
    {"mit 6 20.0 20.0 600.0 9.0 50.0 12.5 10 28", to_hex(md::mit_frame(6, 20.0, 20.0, 600.0, 9.0, 50.0, kLimit4340))},
    // 4310 档位（12.5/30/10）
    {"mit 4 0.3 -1.0 18.0 2.0 3.5 12.5 30 10", to_hex(md::mit_frame(4, 0.3, -1.0, 18.0, 2.0, 3.5, kLimit4310))},
    {"mit 6 -0.0001 29.99 7.0 0.8 -9.99 12.5 30 10", to_hex(md::mit_frame(6, -0.0001, 29.99, 7.0, 0.8, -9.99, kLimit4310))},

    // ── POS_VEL（CAN ID = 0x100 + id）──
    {"posvel 1 0.5 0.3", to_hex(md::pos_vel_frame(1, 0.5, 0.3))},
    {"posvel 3 -1.2345678 -2.0", to_hex(md::pos_vel_frame(3, -1.2345678, -2.0))},
    {"posvel 6 0.0 0.0", to_hex(md::pos_vel_frame(6, 0.0, 0.0))},
    {"posvel 4 12.5 10.0", to_hex(md::pos_vel_frame(4, 12.5, 10.0))},

    // ── 力位混控（CAN ID = 0x300 + id；两个 uint16 无符号、有钳位）──
    {"forcepos 1 0.5 0.3 0.4", to_hex(md::force_pos_frame(1, 0.5, 0.3, 0.4))},
    {"forcepos 6 -0.7 1.0 1.0", to_hex(md::force_pos_frame(6, -0.7, 1.0, 1.0))},
    {"forcepos 2 0.1 200.0 2.0", to_hex(md::force_pos_frame(2, 0.1, 200.0, 2.0))},    // 双双超上限
    {"forcepos 3 -0.1 -5.0 -1.0", to_hex(md::force_pos_frame(3, -0.1, -5.0, -1.0))},  // 双双为负 → 0

    // ── 使能 / 失能 ──
    {"cmd 1 0xfc", to_hex(md::cmd_frame(1, md::kCmdEnable))},
    {"cmd 6 0xfd", to_hex(md::cmd_frame(6, md::kCmdDisable))},

    // ── 刷新 / 寄存器（CAN ID = 0x7FF 广播）──
    {"refresh 1", to_hex(md::refresh_frame(1))},
    {"refresh 6", to_hex(md::refresh_frame(6))},
    {"regread 1 0x0a", to_hex(md::reg_read_frame(1, 0x0A))},
    {"regread 6 0x15", to_hex(md::reg_read_frame(6, 0x15))},
    {"regwrite 1 0x0a 1 0 0 0", to_hex(md::reg_write_frame(1, 0x0A, {0x01, 0x00, 0x00, 0x00}))},
    {"regwrite 3 0x15 0 0 72 65", to_hex(md::reg_write_frame(3, 0x15, {0x00, 0x00, 0x48, 0x41}))},
    {"regwrite 5 0x09 16 39 0 0", to_hex(md::reg_write_frame(5, 0x09, {0x10, 0x27, 0x00, 0x00}))},
    {"saveparams 2", to_hex(md::save_params_frame(2))},

    // ── 接收切分：干净流 / 含垃圾 / 尾部残片（extract_rx 会丢残片）──
    {"extract aa 11 00 00 00 00 00 01 00 00 00 00 00 00 00 55 aa 11 00 00 00 00 00 02 00 00 00 00 00 00 00 55",
      [&] {
        const std::vector<uint8_t> raw = {
          0xAA, 0x11, 0, 0, 0, 0, 0, 0x01, 0, 0, 0, 0, 0, 0, 0, 0x55,
          0xAA, 0x11, 0, 0, 0, 0, 0, 0x02, 0, 0, 0, 0, 0, 0, 0, 0x55};
        const auto fs = md::extract_rx(raw);
        std::string s;
        for (std::size_t i = 0; i < fs.size(); ++i) {s += (i ? "|" : "") + to_hex(fs[i]);}
        return s;
      }()},
    {"extract 01 02 03 aa 11 00 00 00 00 00 01 00 00 00 00 00 00 00 55 09",
      [&] {
        const std::vector<uint8_t> raw = {
          0x01, 0x02, 0x03, 0xAA, 0x11, 0, 0, 0, 0, 0, 0x01, 0, 0, 0, 0, 0, 0, 0, 0x55, 0x09};
        const auto fs = md::extract_rx(raw);
        std::string s;
        for (std::size_t i = 0; i < fs.size(); ++i) {s += (i ? "|" : "") + to_hex(fs[i]);}
        return s;
      }()},
    {"extract aa 11 00 00 00 00 00 01 00 00 00 00 00 00 00 55 aa 11 00 00 00 00 00",
      [&] {
        const std::vector<uint8_t> raw = {
          0xAA, 0x11, 0, 0, 0, 0, 0, 0x01, 0, 0, 0, 0, 0, 0, 0, 0x55,
          0xAA, 0x11, 0, 0, 0, 0, 0};   // 第二帧只有 7 字节 → 残片
        const auto fs = md::extract_rx(raw);
        std::string s;
        for (std::size_t i = 0; i < fs.size(); ++i) {s += (i ? "|" : "") + to_hex(fs[i]);}
        return s;
      }()},
  };

  std::vector<std::string> lines;
  for (const auto & c : cases) {lines.push_back(c.line);}
  const auto got = python_results(lines);
  ASSERT_EQ(got.size(), cases.size()) << "Python 侧没有逐行给结果（脚本/import 出错？）";
  for (std::size_t i = 0; i < cases.size(); ++i) {
    EXPECT_EQ(got[i], cases[i].cpp) << "用例 #" << i << "：" << cases[i].line;
  }
}

// ② RxBuf：分块喂 + 残片保留（与 Python 的 RxBuf 对拍）
TEST(DmFrames, RxBufKeepsPartialFrames)
{
  // 一帧被拆成两半 + 后面再来一帧：第一半必须留到第二次 feed
  const std::string chunk1 = "aa,11,00,00,00,00,00,01,00,00";
  const std::string chunk2 = "00,00,00,00,00,55,aa,11,00,00,00,00,00,02,00,00,00,00,00,00,00,55";
  const std::string line = "rxbuf " + chunk1 + " " + chunk2;
  const auto py = python_results({line});
  ASSERT_EQ(py.size(), 1u);

  md::RxBuf rx;
  std::vector<uint8_t> c1 = {0xAA, 0x11, 0, 0, 0, 0, 0, 0x01, 0, 0};
  std::vector<uint8_t> c2 = {0, 0, 0, 0, 0, 0x55,
    0xAA, 0x11, 0, 0, 0, 0, 0, 0x02, 0, 0, 0, 0, 0, 0, 0, 0x55};
  rx.feed(c1);
  std::string cpp;
  {
    const auto fs = rx.drain();
    std::string s;
    for (std::size_t i = 0; i < fs.size(); ++i) {s += (i ? "|" : "") + to_hex(fs[i]);}
    cpp += "<" + s + "> ";
    EXPECT_EQ(fs.size(), 0u) << "第一块只有 10 字节，不该切出帧";
    EXPECT_EQ(rx.pending(), 10u) << "残片必须留着";
  }
  rx.feed(c2);
  {
    const auto fs = rx.drain();
    std::string s;
    for (std::size_t i = 0; i < fs.size(); ++i) {s += (i ? "|" : "") + to_hex(fs[i]);}
    cpp += "<" + s + ">";
    EXPECT_EQ(fs.size(), 2u);
  }
  EXPECT_EQ(py[0], cpp) << "与 Python 的 RxBuf 行为不一致";
}

// ③ 反馈解码：与 Python 逐数值对拍（含 2026-10-03 那条真实 id2 帧）
TEST(DmFrames, DecodeFeedbackMatchesPython)
{
  struct C
  {
    std::string line;
    std::string cpp;
  };
  auto fmt = [](const md::Feedback & f) {
      char buf[256];
      std::snprintf(buf, sizeof(buf), "%d %d %.17g %.17g %.17g %d %d",
        f.id, f.err, f.pos, f.vel, f.tau, f.t_mos_raw, f.t_rotor_raw);
      return std::string(buf);
    };
  const auto dec = [](const char * hex8, const md::Limit & lim) {
      std::vector<uint8_t> d;
      std::istringstream is(hex8);
      std::string t;
      while (is >> t) {d.push_back(static_cast<uint8_t>(std::stoul(t, nullptr, 16)));}
      return md::decode_feedback(d.data(), lim);
    };
  const std::vector<C> cases = {
    // 真机实测：id2 停在 +1.79 rad（低字节 0x55，就是被 is_reg_response 误吞的那条）
    {"feedback 02 92 55 7f f7 fe 1c 1b 12.5 10 28", fmt(dec("02 92 55 7f f7 fe 1c 1b", kLimit4340))},
    {"feedback 11 00 00 00 00 00 20 21 12.5 30 10", fmt(dec("11 00 00 00 00 00 20 21", kLimit4310))},
    {"feedback 0d 7f ff 7f ff 08 25 26 12.5 10 28", fmt(dec("0d 7f ff 7f ff 08 25 26", kLimit4340))},
    {"feedback 01 80 00 80 00 80 1e 1f 12.5 10 28", fmt(dec("01 80 00 80 00 80 1e 1f", kLimit4340))},
    {"feedback 06 ff ff 00 00 00 00 00 12.5 30 10", fmt(dec("06 ff ff 00 00 00 00 00", kLimit4310))},
  };
  std::vector<std::string> lines;
  for (const auto & c : cases) {lines.push_back(c.line);}
  const auto got = python_results(lines);
  ASSERT_EQ(got.size(), cases.size());
  for (std::size_t i = 0; i < cases.size(); ++i) {
    EXPECT_EQ(got[i], cases[i].cpp) << "用例 #" << i << "：" << cases[i].line;
  }
}

// ④ is_reg_response：把"寄存器回包"与"反馈帧"分开（陷阱 #26 的边界）
TEST(DmFrames, IsRegResponseBoundaries)
{
  const auto rx = [](std::initializer_list<uint8_t> data) {
      md::RxFrame f{};
      f[0] = 0xAA;
      f[1] = md::kFeedbackCmd;
      f[15] = 0x55;
      std::size_t i = 7;
      for (uint8_t b : data) {f[i++] = b;}
      return f;
    };
  // 真·寄存器回包：D[0:2] = 目标 ID 小端
  EXPECT_TRUE(md::is_reg_response(rx({0x01, 0x00, 0x33, 0x0A, 0x00, 0x00, 0x00, 0x00})));
  EXPECT_TRUE(md::is_reg_response(rx({0x06, 0x00, 0x55, 0x09, 0x10, 0x27, 0x00, 0x00})));
  EXPECT_TRUE(md::is_reg_response(rx({0x02, 0x00, 0xAA, 0x01, 0x00, 0x00, 0x00, 0x00})));
  // 真·反馈帧（D[2] 恰好是 0x55 也一样）：id2 那条真实帧
  EXPECT_FALSE(md::is_reg_response(rx({0x02, 0x92, 0x55, 0x7F, 0xF7, 0xFE, 0x1C, 0x1B})));
  EXPECT_FALSE(md::is_reg_response(rx({0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x21})));
  // 使能中的反馈（D[0] = 0x11 > 0x0F）
  EXPECT_FALSE(md::is_reg_response(rx({0x11, 0x00, 0x33, 0x00, 0x00, 0x00, 0x00, 0x00})));
  // D[1] != 0 的反馈帧
  EXPECT_FALSE(md::is_reg_response(rx({0x02, 0x01, 0x33, 0x00, 0x00, 0x00, 0x00, 0x00})));
}

// ⑤ 参数校验 / 边界：非法命令要抛，不能静默发出去
TEST(DmFrames, RejectsBadInput)
{
  EXPECT_THROW(md::cmd_frame(1, 0x00), std::invalid_argument);
  EXPECT_THROW(md::cmd_frame(1, 0xAA), std::invalid_argument);
  EXPECT_EQ(md::err_text(0x01), std::string("使能"));
  EXPECT_EQ(md::err_text(0x0D), std::string("通讯丢失（超时，锁存：只能断电清）"));
  EXPECT_EQ(md::err_text(0x07), nullptr);
  // 定点映射的截断方向（向零）与钳位边界
  EXPECT_EQ(md::float_to_uint(-1.0, 0.0, 500.0, 12), 0u);
  EXPECT_EQ(md::float_to_uint(500.0, 0.0, 500.0, 12), 4095u);
  EXPECT_EQ(md::float_to_uint(1.0, 0.0, 500.0, 12), 8u);   // 1/500*4095 = 8.19 → 8
  EXPECT_EQ(md::limit_min_max(-5.0, -2.0, 2.0), -2.0);
  EXPECT_EQ(md::limit_min_max(5.0, -2.0, 2.0), 2.0);
}
