#!/usr/bin/env python3
"""冒烟测试④：MotorBus 非阻塞收发（**不需要硬件、不需要 pyserial、不需要真串口**）

`MotorBus` 是 7 关节共用的地基（design.md D2）。它最容易错的地方不是"算错"，而是
**时序**：什么时候读、读之前要不要清缓冲、帧被拆成两半怎么办、广播帧会不会把
1:1 计数搞乱。这些在真机上表现为"偶尔丢一条""数据慢一拍"，非常难查 —— 所以在这里
用一个假串口把时序钉死。

  [1] 发送出口：POS_VEL 帧的 CAN ID = 0x100+ID、数据 = float32(P)+float32(V)
  [2] 使能/失能帧：CAN ID = ID 本身（**不是** 0x100+ID）、数据 = FF*7 + FC/FD
  [3] MIT 帧经 bus 发出去，与厂商 SDK `controlMIT` 逐字节一致
  [4] **poll() 非阻塞**：空串口上必须立刻返回 0，不能被串口 timeout 拖住
  [5] **poll() 不吞帧**：3 帧按 1/5/9/16/17 字节分块到达，跨多次 poll() 必须一帧不少
  [6] 路由与缓存：反馈落到正确的 motor_id；ERR 高 4 位、ID 低 4 位
  [7] **映射范围必须分档**：同一段字节用 4310 / 4340P 两套 limit 解出的力矩不同
      —— 把"用错档位力矩差 4 倍"钉成回归测试
  [8] 未注册的 ID 不解码，只记进 unknown_ids（宁可少一条，不用错的档位解）
  [9] 1:1 计数：广播刷新帧单独计，不混进 sent，否则比值会假性 >100%
  [10] **send_and_wait 的原子性**：flush 与 send 之间不能被插队，否则拿到的应答
       慢一拍 —— 这里用"残留帧 + write 之后才到的应答"按真实时序复现正反两面
  [11] 结构不变量：协议代码全项目只有 `dm_frames` 一份，`dm_bringup` 只是再导出

用法（在 DM_Armx 根目录）：
    pixi run python tools/smoke_dm_bus.py
"""
import struct
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "src" / "DMmotor_driver"))
sys.path.insert(0, str(REPO / "src" / "third_party" / "Python例程" / "u2can"))

import DM_CAN  # noqa: E402
from DMmotor_driver import dm_bus as BUS  # noqa: E402
from DMmotor_driver import dm_frames as F  # noqa: E402

# 两套映射范围（都用实机/SDK 的回读值）—— 4310 与 4340P **不一样**，这正是要分档的原因
LIM_4310 = (12.5, 30.0, 10.0)
LIM_4340 = (12.5, 10.0, 28.0)


class FakeSerial:
    """假串口：有 `in_waiting` / `read` / `write` / `close` 就够 MotorBus 用了。

    `chunk` 控制一次最多吐多少字节，用来模拟"一帧被拆成几次到达"。
    写入的帧都记在 `written` 里，供断言检查。
    """

    def __init__(self, stream: bytes = b"", chunk: int = 1 << 20):
        self.buf = bytes(stream)
        self.chunk = chunk
        self.written: list[bytes] = []
        self.closed = False

    @property
    def in_waiting(self):
        return min(self.chunk, len(self.buf))

    def read(self, n):
        out, self.buf = self.buf[:n], self.buf[n:]
        return out

    def write(self, b):
        self.written.append(bytes(b))
        return len(b)

    def close(self):
        self.closed = True

    def reset_input_buffer(self):
        self.buf = b""


def pack_feedback(pos, vel, tau, err, t_mos, t_rotor, motor_id, limit):
    """按手册「反馈帧」表把物理量拼成 8 个数据字节（`decode_feedback` 的逆向）。"""
    p_max, v_max, t_max = limit
    to_u = lambda x, bits, hi: int(round((x + hi) / (2 * hi) * ((1 << bits) - 1)))
    pos_u = to_u(pos, 16, p_max)
    vel_u = to_u(vel, 12, v_max)
    tau_u = to_u(tau, 12, t_max)
    return bytes([
        (motor_id & 0x0F) | ((err & 0x0F) << 4),
        (pos_u >> 8) & 0xFF, pos_u & 0xFF,
        (vel_u >> 4) & 0xFF, ((vel_u & 0x0F) << 4) | ((tau_u >> 8) & 0x0F),
        tau_u & 0xFF,
        t_mos & 0xFF, t_rotor & 0xFF,
    ])


def wrap_rx(can_id, data8, cmd=0x11):
    """把 8 个数据字节包成适配器的 16 字节接收帧（[0]=0xAA [15]=0x55）。"""
    return bytes([0xAA, cmd, 0x00,
                  can_id & 0xFF, (can_id >> 8) & 0xFF,
                  (can_id >> 16) & 0xFF, (can_id >> 24) & 0xFF,
                  *data8, 0x55])


def make_bus(ser, motors):
    """建一个 MotorBus，但**把假串口直接塞进去** —— 绕过 open()，
    这样连 pyserial 和 /dev/ttyACM0 都不需要。"""
    bus = BUS.MotorBus("/dev/null")
    bus.ser = ser
    for mid, limit in motors.items():
        bus.add_motor(mid, limit)
    return bus


def sdk_mit(kp, kd, q, dq, tau, limit, motor_id=0x01):
    """让厂商 SDK 自己构造一条 MIT 帧，用来对拍（同 smoke_dm_frames 的做法）。"""
    class _S:
        def __init__(self):
            self.written = []

        def write(self, b):
            self.written.append(bytes(b))
            return len(b)

        def read_all(self):
            return b""

        def is_open(self):
            return True

    fs = _S()
    ctrl = DM_CAN.MotorControl.__new__(DM_CAN.MotorControl)   # 绕过会开串口的 __init__
    ctrl.serial_ = fs
    ctrl.data_save = b""
    ctrl.motors_map = {motor_id: DM_CAN.Motor(DM_CAN.DM_Motor_Type.DM4310, motor_id, 0x11)}
    ctrl.Limit_Param = [list(limit)] * len(DM_CAN.MotorControl.Limit_Param)
    ctrl.controlMIT(ctrl.motors_map[motor_id], kp, kd, q, dq, tau)
    return fs.written[-1]


def main() -> int:
    fails = []

    def check(cond, label, extra=""):
        print(f"    {'✓' if cond else '✗'} {label}" + (f"   {extra}" if extra else ""))
        if not cond:
            fails.append(label)

    # ── [1] POS_VEL 发送出口 ────────────────────────────────────────
    print("[1] send_pos_vel（CAN ID = 0x100+ID，数据 = float32 P + float32 V）")
    ser = FakeSerial()
    bus = make_bus(ser, {0x01: LIM_4310})
    p_des, v_des = 0.0125581, 0.5
    bus.send_pos_vel(0x01, p_des, v_des)
    frame = ser.written[-1]
    check(len(frame) == 30, f"帧长 = {len(frame)}（应为 30）")
    can_id = frame[13] | (frame[14] << 8)
    check(can_id == 0x101, f"CAN ID = 0x{can_id:03X}（应为 0x101 = 0x100+0x01）")
    expect = struct.pack("<ff", p_des, v_des)
    check(frame[21:29] == expect,
          f"数据 = {frame[21:29].hex(' ')}", f"应为 {expect.hex(' ')}")
    # float32 往返：**不能用 `==`** —— 0.0125581 是 float64 字面量，float32 只有约 7 位
    # 有效数字，解回来必然差在末几位。正确的判据是"误差在 float32 精度内"，
    # 外加"再包一次字节完全不变"（float32 往返是幂等的）。
    p_back = struct.unpack('<f', frame[21:25])[0]
    v_back = struct.unpack('<f', frame[25:29])[0]
    check(abs(p_back - p_des) < 1e-6 and abs(v_back - v_des) < 1e-6,
          f"float32 往返：P={p_back!r} V={v_back!r}（差 {abs(p_back - p_des):.2e}）")
    check(struct.pack('<ff', p_back, v_back) == frame[21:29],
          "解回来再包一次 → 字节完全不变（float32 往返幂等）")

    # 逐台直发时 CAN ID 必须跟着电机走
    bus.add_motor(0x03, LIM_4310)
    bus.send_pos_vel(0x03, 0.1, 0.2)
    cid3 = ser.written[-1][13] | (ser.written[-1][14] << 8)
    check(cid3 == 0x103, f"电机 0x03 的帧 CAN ID = 0x{cid3:03X}（应为 0x103）")

    # ── [2] 使能/失能帧 ────────────────────────────────────────────
    print("\n[2] send_enable / send_disable（CAN ID = ID 本身，数据 FF*7 + 命令）")
    ser = FakeSerial()
    bus = make_bus(ser, {0x04: LIM_4310})
    bus.send_enable(0x04)
    bus.send_disable(0x04)
    en, dis = ser.written[0], ser.written[1]
    en_id = en[13] | (en[14] << 8)
    check(en_id == 0x04,
          f"使能帧 CAN ID = 0x{en_id:02X}（应为 0x04 —— **不是** 0x104）")
    check(en[21:29] == bytes([0xFF] * 7 + [0xFC]), f"使能数据 = {en[21:29].hex(' ')}")
    check(dis[21:29] == bytes([0xFF] * 7 + [0xFD]), f"失能数据 = {dis[21:29].hex(' ')}")

    # ── [3] MIT 帧经 bus 出去，与 SDK 一致 ──────────────────────────
    print("\n[3] send_mit vs SDK controlMIT（逐字节，含两端饱和）")
    for kp, kd, q, dq, tau in [(0.0, 0.1, 0.0795, 0.0, 0.0),
                               (18.0, 2.0, -0.5, 0.3, 0.5),
                               (500.0, 5.0, 12.5, 30.0, 10.0),
                               (0.0, 0.0, -12.5, -30.0, -10.0)]:
        ser = FakeSerial()
        bus = make_bus(ser, {0x01: LIM_4310})
        bus.send_mit(0x01, kp, kd, q, dq, tau)
        mine = ser.written[-1]
        theirs = sdk_mit(kp, kd, q, dq, tau, LIM_4310)
        check(mine == theirs,
              f"kp={kp:<6g} kd={kd:<4g} q={q:<7g} dq={dq:<6g} t_ff={tau:<6g}"
              f" → D={mine[21:29].hex(' ')}",
              "" if mine == theirs else f"SDK 给出 {theirs[21:29].hex(' ')}")

    # ── [4] poll() 必须真的非阻塞 ───────────────────────────────────
    print("\n[4] poll() 非阻塞（空串口上立刻返回 0）")
    bus = make_bus(FakeSerial(b""), {0x01: LIM_4310})
    t0 = time.perf_counter()
    n = bus.poll()
    dt = time.perf_counter() - t0
    check(n == 0, f"空串口 → 返回 {n} 帧")
    check(dt < 0.005,
          f"耗时 {dt * 1000:.2f}ms（<5ms = 没有被串口 timeout 拖住）",
          "← 若这里变慢，说明 poll 里混进了阻塞读，500Hz 循环就废了")

    # ── [5] poll() 不吞帧（分块到达）────────────────────────────────
    print("\n[5] 分块到达时 poll() 一帧不少（跨多次 poll 靠 RxBuf 残留拼回来）")
    f1 = wrap_rx(0x000, pack_feedback(0.10, 0.20, 0.30, 0x1, 41, 45, 0x01, LIM_4310))
    f2 = wrap_rx(0x000, pack_feedback(-0.20, 0.00, -0.10, 0x1, 42, 46, 0x01, LIM_4310))
    f3 = wrap_rx(0x000, pack_feedback(0.05, -0.30, 0.00, 0x1, 43, 47, 0x01, LIM_4310))
    stream = f1 + f2 + f3

    for chunk in (1, 5, 9, 16, 17, 48):
        ser = FakeSerial(stream, chunk=chunk)
        bus = make_bus(ser, {0x01: LIM_4310})
        got = 0
        # 反复 poll() 直到串口吐完 —— 每轮只收"已经到了的"，绝不等待
        for _ in range(len(stream) + 5):
            got += bus.poll()
        check(got == 3, f"按 {chunk:>2} 字节/块喂 3 帧 → poll 出 {got} 帧",
              f"残留 {len(bus.rx.buf)}B" + ("（=0，干净）" if not bus.rx.buf else ""))

    # 反面教材：跨轮的残片如果被丢掉，就会错位丢帧
    ser = FakeSerial(stream, chunk=5)
    bus = make_bus(ser, {0x01: LIM_4310})
    naive = 0
    while ser.buf:
        naive += len([f for f in F.extract_rx(ser.read(ser.in_waiting))
                      if f[1] == F.FEEDBACK_CMD])
    check(naive < 3,
          f"反面教材：每轮重新切帧（不留残片）只切出 {naive}/3 帧",
          "← 这正是 MotorBus 跨轮复用同一个 RxBuf 的理由")

    # ── [6] 路由与缓存 ─────────────────────────────────────────────
    print("\n[6] 路由：D[0] 高 4 位是 ERR、低 4 位是 ID")
    ser = FakeSerial(wrap_rx(0x000, pack_feedback(0.10, 0.20, 0.30, 0x1, 41, 45, 0x03,
                                                  LIM_4310)))
    bus = make_bus(ser, {0x01: LIM_4310, 0x03: LIM_4310})
    bus.poll()
    st = bus.get_state(0x03)
    check(st is not None and st.motor_id == 0x03, "反馈落到 motor_id=0x03")
    check(bus.get_state(0x01) is None, "0x01 没有被误更新（这一帧不是它的）")
    check(st.err == 0x1 and st.enabled and not st.faulted, "ERR=1 → enabled=True")
    check(st.temp_mos == 41 and st.temp_rotor == 45,
          f"温度 D[6]={st.temp_mos} D[7]={st.temp_rotor} ℃",
          "（SDK 的 recv_data 把这两个字节丢了，是我们自己解的）")

    # 故障码：ERR=0xD 是锁存的通讯丢失，enabled 必须为 False
    ser = FakeSerial(wrap_rx(0x000, pack_feedback(0.0, 0.0, 0.0, 0xD, 41, 45, 0x01,
                                                  LIM_4310)))
    bus = make_bus(ser, {0x01: LIM_4310})
    bus.poll()
    st = bus.get_state(0x01)
    check(st.err == 0xD and st.faulted and not st.enabled,
          f"ERR=0xD → faulted=True / enabled=False（{st.err_text}）")
    check(st.err_text == "通讯丢失（超时）", f"错误码文案：{st.err_text}")

    # ERR=0 是"失能"，**不是**正常 —— 容易被当成 enabled=True
    ser = FakeSerial(wrap_rx(0x000, pack_feedback(0.0, 0.0, 0.0, 0x0, 41, 45, 0x01,
                                                  LIM_4310)))
    bus = make_bus(ser, {0x01: LIM_4310})
    bus.poll()
    check(not bus.get_state(0x01).enabled and not bus.get_state(0x01).faulted,
          "ERR=0 → enabled=False 但 faulted=False（失能不是故障）")

    # ── [7] 映射范围必须分档 ───────────────────────────────────────
    print("\n[7] 映射范围必须按电机分档（用错档位力矩差 4 倍）")
    data = pack_feedback(0.10, 0.20, 0.30, 0x1, 41, 45, 0x01, LIM_4310)
    ser_a = FakeSerial(wrap_rx(0x000, data))
    bus_a = make_bus(ser_a, {0x01: LIM_4310})
    bus_a.poll()
    ser_b = FakeSerial(wrap_rx(0x000, data))
    bus_b = make_bus(ser_b, {0x01: LIM_4340})
    bus_b.poll()
    tau_a = bus_a.get_state(0x01).tau
    tau_b = bus_b.get_state(0x01).tau
    check(abs(tau_a - tau_b) > 0.01,
          f"同一段字节：4310 档位 → tau={tau_a:.4f} N·m，"
          f"4340P 档位 → tau={tau_b:.4f} N·m",
          f"差 {abs(tau_a - tau_b):.4f} N·m ← 档位错了数据就是错的")
    check(abs(tau_a - 0.30) < 0.01,
          f"用对的档位能解回原值 0.3000 N·m（实得 {tau_a:.4f}）")

    # 未注册的电机不允许发（否则就是"凭一个猜的档位在发帧"）
    bus = make_bus(FakeSerial(), {0x01: LIM_4310})
    try:
        bus.send_pos_vel(0x09, 0.0, 0.0)
        check(False, "未注册的 ID 发帧应当报错，但它通过了")
    except KeyError as e:
        check("没注册" in str(e), "未注册的 ID 发帧 → 报 KeyError（带修复提示）")

    # ── [8] 未注册的反馈不解码 ─────────────────────────────────────
    print("\n[8] 未注册 ID 的反馈进 unknown_ids，不用错的档位硬解")
    ser = FakeSerial(wrap_rx(0x000, pack_feedback(0.0, 0.0, 0.0, 0x1, 41, 45, 0x07,
                                                  LIM_4310)))
    bus = make_bus(ser, {0x01: LIM_4310})
    n = bus.poll()
    check(n == 0 and bus.get_state(0x07) is None, "0x07 没注册 → 不解码、不建缓存")
    check(bus.stats()["unknown_ids"] == [0x07],
          f"unknown_ids = {bus.stats()['unknown_ids']}（能看出总线上有没登记的东西）")

    # ── [9] 1:1 计数 ──────────────────────────────────────────────
    print("\n[9] 1:1 计数：广播刷新帧单独计，不混进 sent")
    n_send = 5
    stream = b"".join(
        wrap_rx(0x000, pack_feedback(0.0, 0.0, 0.0, 0x1, 41, 45, 0x01, LIM_4310))
        for _ in range(n_send)
    )
    ser = FakeSerial(stream)
    bus = make_bus(ser, {0x01: LIM_4310})
    for _ in range(n_send):
        bus.send_pos_vel(0x01, 0.0, 0.0)
    bus.poll()
    st = bus.stats()
    check(st["sent"] == n_send and st["received"] == n_send,
          f"发 {st['sent']} / 收 {st['received']} → 比值 {st['ratio']:.2f}")
    check(abs(st["ratio"] - 1.0) < 1e-9, "逐台直发时比值恰好 1.00")

    # 广播刷新帧：一条帧会让**多台**电机各回一条，比值本来就会 >1 —— 所以分开数
    ser = FakeSerial(b"".join(
        wrap_rx(0x000, pack_feedback(0.0, 0.0, 0.0, 0x1, 41, 45, mid, LIM_4310))
        for mid in (0x01, 0x02)
    ))
    bus = make_bus(ser, {0x01: LIM_4310, 0x02: LIM_4310})
    bus.send_refresh(0x7FF)
    bus.poll()
    st = bus.stats()
    check(st["sent"] == 0 and st["sent_broadcast"] == 1,
          f"刷新帧计在 sent_broadcast={st['sent_broadcast']}，sent={st['sent']}")
    check(st["received"] == 2,
          f"一条广播回 {st['received']} 条（两台电机各回一条）",
          "← 混进 sent 的话比值会假性 200%，看着像 bug")

    # ── [10] send_and_wait：flush 与 send 必须是原子的 ──────────────
    print("\n[10] send_and_wait（「发一帧等一帧」= flush → 发 → 等，三步原子）")

    class RespondingSerial(FakeSerial):
        """按**真实时序**投喂：残留帧在发命令之前就躺在缓冲里，
        本条命令的应答要等 `write()` 之后才到 —— 这才是真适配器的行为。
        （直接把两条帧一起塞进缓冲是错的：应答不可能早于请求。）"""

        def __init__(self, stale, reply):
            super().__init__(stale)
            self._reply = reply

        def write(self, b):
            n = super().write(b)
            self.buf += self._reply       # 应答现在才到
            return n

    stale = wrap_rx(0x000, pack_feedback(0.10, 0.0, 0.0, 0x1, 41, 45, 0x01, LIM_4310))
    fresh = wrap_rx(0x000, pack_feedback(0.20, 0.0, 0.0, 0x1, 41, 45, 0x01, LIM_4310))
    cmd = F.pos_vel_frame(0x01, 0.0, 0.0)

    # 反面教材：手动"发一帧等一帧"但忘了 flush → 拿到的是**上一条**的应答。
    # 表现是"数据恒定慢一拍"，安全判断（跳变/超力矩）就建立在过期数据上，极难查。
    bus = make_bus(RespondingSerial(stale, fresh), {0x01: LIM_4310})
    bus.send_frame(cmd)
    st_bad = bus.wait_feedback(0x01, timeout=0.05)
    check(st_bad is not None and abs(st_bad.pos - 0.10) < 0.01,
          f"反面教材：不 flush 直接等 → 拿到残留的 pos={st_bad.pos:.4f}（慢一拍）")

    # 正确做法：send_and_wait 把 flush 和 send 合成一步
    bus = make_bus(RespondingSerial(stale, fresh), {0x01: LIM_4310})
    st = bus.send_and_wait(cmd, 0x01, timeout=0.05)
    check(st is not None and abs(st.pos - 0.20) < 0.01,
          f"send_and_wait → 拿到**本条命令**的应答 pos={st.pos:.4f}",
          "← 残留的 0.10 被 flush 掉了")

    # 只有残留、没有新应答 → 必须返回 None，而不是把残留当成本条的应答
    bus = make_bus(FakeSerial(stale), {0x01: LIM_4310})
    st = bus.send_and_wait(cmd, 0x01, timeout=0.02)
    check(st is None, "只有残留没有新应答 → 返回 None（残留不冒充新应答）")

    # ── [11] 协议只有一份 ─────────────────────────────────────────
    print("\n[11] 协议代码全项目只有一份（dm_bringup 是再导出，不是副本）")
    from DMmotor_driver import dm_bringup as BG  # noqa: E402

    for name in ("build_tx", "mit_frame", "pos_vel_frame", "cmd_frame",
                 "refresh_frame", "extract_rx", "RxBuf", "read_frames",
                 "flush_rx", "decode_feedback"):
        a, b = getattr(F, name), getattr(BG, name, None)
        check(b is a, f"dm_bringup.{name} 就是 dm_frames.{name}",
              "" if b is a else "← 出现了第二份实现！")
    check(BG.TX_FRAME_LEN == F.TX_FRAME_LEN == 30
          and BG.RX_FRAME_LEN == F.RX_FRAME_LEN == 16,
          "dm_bringup 的常量也与 dm_frames 同源")

    # ── 收尾 ──────────────────────────────────────────────────────
    print()
    if fails:
        print(f"FAIL（{len(fails)} 项）：")
        for f in fails:
            print(f"  - {f}")
        return 1
    print("PASS: MotorBus 的发送出口 / 非阻塞抽干 / 分块拼帧 / 路由 / "
          "映射范围分档 / 1:1 计数 / flush-发-等原子性全部符合预期；"
          "协议代码确认只有 dm_frames 一份")
    return 0


if __name__ == "__main__":
    sys.exit(main())
