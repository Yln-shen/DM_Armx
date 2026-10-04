// test_dm_bus.cpp —— 总线层用**假串口**（内存字节流）验证收发与状态缓存。
//
// 这一层不重复验"字节对不对"（那是 test_dm_frames.cpp 与 Python 逐字节对拍的事），
// 这里验的是**管线**：帧有没有从正确的出口出去、反馈有没有进缓存、脏东西有没有被挡住。
#include <gtest/gtest.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "motor_driver_hardware/dm_bus.hpp"

namespace md = motor_driver_hardware;

namespace
{

const md::Limit kLimit4340{12.5, 10.0, 28.0};

// 内存假串口：tx 收下主机写出的字节，rx 是"等待被读走"的字节。
// `on_write` 可以装一个"应答器"：收到刷新帧就回一条反馈 —— 这样 sync_states 那种
// "先 flush 再主动问"的流程也能被完整测到（真实的适配器就是这么工作的）。
class FakeSerial : public md::SerialIo
{
public:
  void open() override {open_ = true;}
  void close() override {open_ = false;}
  bool is_open() const override {return open_;}
  void write(const uint8_t * data, std::size_t len) override
  {
    tx.insert(tx.end(), data, data + len);
    stats_.tx_calls += 1;                 // 与真 SerialPort 同一契约：谁写谁计数
    stats_.tx_bytes += len;
    if (on_write) {on_write(data, len);}
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
  std::function<void(const uint8_t *, std::size_t)> on_write;
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

template<typename FrameT>
std::string to_hex(const FrameT & f) {return to_hex(f.data(), f.size());}

// 一条完整的 16 字节反馈帧：[0]=0xAA [1]=0x11 [7:15]=数据 [15]=0x55
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

std::vector<uint8_t> reg_response_frame(uint8_t id, uint8_t cmd, uint8_t rid,
  std::initializer_list<uint8_t> data4)
{
  return feedback_frame({id, 0x00, cmd, rid, *data4.begin(), *(data4.begin() + 1),
    *(data4.begin() + 2), *(data4.begin() + 3)});
}

// 真机实测的那条 id2 反馈（低字节 0x55 —— 就是会被 is_reg_response 误吞的那条）
const std::vector<uint8_t> kRealId2Frame = feedback_frame(
  {0x02, 0x92, 0x55, 0x7F, 0xF7, 0xFE, 0x1C, 0x1B});

}  // namespace

// ① 发送：走的出口、CAN ID、字节数、统计
TEST(DmBus, SendsThroughSingleExit)
{
  FakeSerial io;
  md::MotorBus bus(io);
  bus.open();
  bus.add_motor(1, kLimit4340);
  bus.add_motor(6, kLimit4340);

  bus.send_pos_vel(1, 0.5, 0.3);
  bus.send_enable(6);
  bus.send_disable(6);
  bus.send_refresh(2);

  // 每条 30 字节；内容就是 dm_frames 造出来的（这里只检查"总线没改写它"）
  std::vector<uint8_t> expect;
  for (const auto & f : {md::pos_vel_frame(1, 0.5, 0.3), md::cmd_frame(6, md::kCmdEnable),
      md::cmd_frame(6, md::kCmdDisable), md::refresh_frame(2)})
  {
    expect.insert(expect.end(), f.begin(), f.end());
  }
  EXPECT_EQ(to_hex(io.tx.data(), io.tx.size()), to_hex(expect.data(), expect.size()));
  EXPECT_EQ(io.tx.size(), 4u * md::kTxFrameLen);
  EXPECT_EQ(io.stats().tx_calls, 4u);
  EXPECT_EQ(io.stats().tx_bytes, 4u * md::kTxFrameLen);
}

// ② 接收：注入真实反馈 → 缓存里的数值必须等于 decode_feedback 的结果
TEST(DmBus, DecodesInjectedFeedback)
{
  FakeSerial io;
  md::MotorBus bus(io);
  bus.open();
  bus.add_motor(2, kLimit4340);

  io.push(kRealId2Frame);
  EXPECT_EQ(bus.poll(), 1u);

  const auto st = bus.get_state(2);
  ASSERT_TRUE(st.has_value());
  const md::Feedback fb = md::decode_feedback(kRealId2Frame.data() + 7, kLimit4340);
  EXPECT_EQ(st->id, 2u);
  EXPECT_EQ(st->err, 0u);
  EXPECT_DOUBLE_EQ(st->pos, fb.pos);
  EXPECT_DOUBLE_EQ(st->vel, fb.vel);
  EXPECT_DOUBLE_EQ(st->tau, fb.tau);
  EXPECT_EQ(st->t_mos_raw, fb.t_mos_raw);
  EXPECT_EQ(st->t_rotor_raw, fb.t_rotor_raw);
  EXPECT_NEAR(st->pos, 1.79, 0.01);      // 真机记录：id2 停在 +1.79 rad
}

// ③ 脏东西挡住：寄存器回包不进缓存；陌生 ID 记账不污染
TEST(DmBus, FiltersRegResponsesAndUnknownIds)
{
  FakeSerial io;
  md::MotorBus bus(io);
  bus.open();
  bus.add_motor(1, kLimit4340);

  io.push(reg_response_frame(1, md::kRegCmdRead, 0x0A, {0x00, 0x00, 0x48, 0x41}));
  io.push(feedback_frame({0x0A, 0x11, 0x22, 0x33, 0x44, 0x55, 0x20, 0x21}));   // 陌生 id=10
  EXPECT_EQ(bus.poll(), 0u);
  EXPECT_FALSE(bus.get_state(1).has_value());          // 寄存器回包不能被当反馈
  const auto unknown = bus.unknown_ids();
  ASSERT_EQ(unknown.size(), 1u);
  EXPECT_EQ(unknown[0], 0x0Au);

  // 反过来的陷阱 #26：D[2] 是 0x55 的**真反馈**必须被解出来
  io.push(kRealId2Frame);
  bus.add_motor(2, kLimit4340);
  EXPECT_EQ(bus.poll(), 1u);
  EXPECT_TRUE(bus.get_state(2).has_value());
}

// ④ 一帧被拆成两次 poll：不丢、不串位
TEST(DmBus, KeepsPartialFrameAcrossPolls)
{
  FakeSerial io;
  md::MotorBus bus(io);
  bus.open();
  bus.add_motor(2, kLimit4340);

  std::vector<uint8_t> part1(kRealId2Frame.begin(), kRealId2Frame.begin() + 10);
  std::vector<uint8_t> part2(kRealId2Frame.begin() + 10, kRealId2Frame.end());
  io.push(part1);
  EXPECT_EQ(bus.poll(), 0u) << "半帧不该被当成一帧";
  EXPECT_FALSE(bus.get_state(2).has_value());

  io.push(part2);
  EXPECT_EQ(bus.poll(), 1u) << "拼上之后必须解出来";
  const auto st = bus.get_state(2);
  ASSERT_TRUE(st.has_value());
  EXPECT_NEAR(st->pos, 1.79, 0.01);
}

// ⑤ 寄存器 I/O：匹配请求的回包才算数
TEST(DmBus, RegisterIoMatchesRequest)
{
  FakeSerial io;
  md::MotorBus bus(io);
  bus.open();
  bus.add_motor(3, kLimit4340);

  io.push(reg_response_frame(3, md::kRegCmdRead, 0x15, {0x00, 0x00, 0x48, 0x41}));  // PMAX=12.5
  md::Word4 out{};
  EXPECT_TRUE(bus.read_register(3, 0x15, out, 0.05));
  EXPECT_EQ(out, (md::Word4{0x00, 0x00, 0x48, 0x41}));
  EXPECT_NEAR(md::uint8s_to_float32(out.data()), 12.5, 1e-6);

  // 回包 RID 不匹配 ⇒ 不算（等超时）
  io.push(reg_response_frame(3, md::kRegCmdRead, 0x16, {0x00, 0x00, 0x20, 0x41}));
  md::Word4 wrong{};
  EXPECT_FALSE(bus.read_register(3, 0x15, wrong, 0.02));

  // 写寄存器的回包（cmd=0x55）
  io.push(reg_response_frame(3, md::kRegCmdWrite, 0x0A, {0x02, 0x00, 0x00, 0x00}));
  EXPECT_TRUE(bus.write_register(3, 0x0A, {0x02, 0x00, 0x00, 0x00}, 0.05));
}

// ⑥ wait_feedback：拿到**这一台**的原始帧；没数据就超时
TEST(DmBus, WaitFeedbackReturnsRawFrame)
{
  FakeSerial io;
  md::MotorBus bus(io);
  bus.open();
  bus.add_motor(2, kLimit4340);

  md::RxFrame got{};
  EXPECT_FALSE(bus.wait_feedback(2, got, 0.01)) << "没有数据必须超时，不能返回假帧";

  io.push(kRealId2Frame);
  EXPECT_TRUE(bus.wait_feedback(2, got, 0.05));
  EXPECT_EQ(to_hex(got), to_hex(kRealId2Frame));
}

// ⑦ sync_states：先 flush 再主动刷新，等到安静
TEST(DmBus, SyncStatesDrainsUntilQuiet)
{
  FakeSerial io;
  md::MotorBus bus(io);
  bus.open();
  bus.add_motor(1, kLimit4340);
  bus.add_motor(2, kLimit4340);

  // 装应答器：收到**刷新帧**（0x7FF + 数据段前两字节是目标 ID）就回一条该电机的反馈。
  // 这正是真适配器的行为，也顺便证明 sync_states 的"先丢旧的、再主动问"能拿到真相。
  io.on_write = [&io](const uint8_t * frame, std::size_t len) {
      if (len != md::kTxFrameLen) {return;}
      const uint16_t can_id = static_cast<uint16_t>(frame[13] | (frame[14] << 8));
      if (can_id != md::kCanIdBroadcast || frame[23] != 0xCC) {return;}
      const uint8_t target = frame[21];
      io.push(feedback_frame({target, 0x99, 0x99, 0x00, 0x00, 0x00, 0x1E, 0x1F}));
    };

  // 缓冲里先塞一条**陈旧**回包（低位 → 负角度，位置与这次应答明显不同）
  io.push(feedback_frame({0x01, 0x10, 0x00, 0x00, 0x00, 0x00, 0x1E, 0x1F}));

  ASSERT_TRUE(bus.sync_states(0.5, 0.02));
  EXPECT_TRUE(bus.get_state(1).has_value());
  EXPECT_TRUE(bus.get_state(2).has_value());
  // 两台都发出了刷新帧（每台 30 字节）
  EXPECT_EQ(io.tx.size(), 2u * md::kTxFrameLen);
  // 缓存里的位置来自**这次**应答（0x9999 → 正角度），不是那条陈旧的 0x1000（负角度）
  const auto st = bus.get_state(1);
  ASSERT_TRUE(st.has_value());
  EXPECT_GT(st->pos, 0.0) << "不能是陈旧回包解出来的负角度";
  EXPECT_NEAR(st->pos, md::decode_feedback(
      feedback_frame({0x01, 0x99, 0x99, 0x00, 0x00, 0x00, 0x1E, 0x1F}).data() + 7, kLimit4340).pos,
    1e-12);
}

// ⑧ 前置条件：重复注册 / 未注册 / 没开总线都要当场拒
TEST(DmBus, RejectsBadUse)
{
  FakeSerial io;
  md::MotorBus bus(io);
  EXPECT_THROW(bus.send_pos_vel(1, 0.0, 0.1), std::runtime_error);   // 没 open
  bus.open();
  bus.add_motor(1, kLimit4340);
  EXPECT_THROW(bus.add_motor(1, kLimit4340), std::invalid_argument);
  EXPECT_THROW(bus.limit(9), std::invalid_argument);
  md::RxFrame scratch{};
  EXPECT_THROW(bus.wait_feedback(9, scratch, 0.01), std::invalid_argument);
}
