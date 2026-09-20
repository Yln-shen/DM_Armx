#!/usr/bin/env python3
"""冒烟测试③：达妙 CAN 帧的构造与解析（**不需要硬件、不需要 pyserial**）

`dm_bringup.py` 里有三处"自己动手"的地方，都是协议层的东西，一旦写错就是
"电机不动/读数全错"这种说不清的毛病。所以这里逐条验，而且尽量用**厂商 SDK 自己
的实现做对照**，而不是自己验自己：

  [1] TX 帧构造：30 字节、[13:15]=CAN ID 小端、[21:29]=数据、float32 小端往返
  [2] RX 切帧：把同一段字节流同时喂给我的 `extract_rx` 和 SDK 的
      `__extract_packets`，两者的切分结果必须逐字节一致
      （含脏字节前缀、帧间粘连、尾部半帧这三种情况）
  [3] 反馈解码：按手册「反馈帧」表自己拼出 D[0:8]，再让我的 `decode_feedback` 和
      SDK 的 `__process_packet` 分别解，位置/速度/力矩必须一致；
      而温度 D[6]/D[7] 只有我能解出来（SDK 丢掉了）—— 这正是要自己解析的理由
  [4] 打印函数不崩
  [5] **接收缓冲 RxBuf**：一帧被拆成两次到达时不能丢字节、不能错位
      （真机第一次点动就栽在这上面：`ser.read_all()` 是非阻塞的，写完立刻读
       什么都读不到，被误判成"收不到反馈"而停机；而且残片丢了会让后续字节错位）
  [6] **MIT 帧**：与 SDK `controlMIT` 逐字节对拍（含两端饱和值）—— 发错帧电机就不动

用法（在 DM_Armx 根目录）：
    pixi run python tools/smoke_dm_frames.py
"""
import struct
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "src" / "DMmotor_driver"))
sys.path.insert(0, str(REPO / "src" / "third_party" / "Python例程" / "u2can"))

import DM_CAN  # noqa: E402
from DMmotor_driver import dm_bringup as B  # noqa: E402

MT = DM_CAN.DM_Motor_Type.DM4310
LIMIT = (12.5, 30.0, 10.0)  # 4310 官方 Limit_Param 顺序 = [PMAX, VMAX, TMAX]


def bare_ctrl():
    """绕过 __init__（它会真的开串口）。__extract_packets 只用 self.data_save。"""
    ctrl = DM_CAN.MotorControl.__new__(DM_CAN.MotorControl)
    ctrl.motors_map = {}
    ctrl.data_save = b""
    return ctrl


def pack_feedback(pos, vel, tau, err, t_mos, t_rotor, motor_id=0x04):
    """按手册「反馈帧」表把物理量拼成 8 个数据字节（逆向 self.decode_feedback）。"""
    p_max, v_max, t_max = LIMIT
    to_u = lambda x, bits, hi: int(round((x + hi) / (2 * hi) * ((1 << bits) - 1)))
    pos_u, vel_u, tau_u = to_u(pos, 16, p_max), to_u(vel, 12, v_max), to_u(tau, 12, t_max)
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
                  can_id & 0xFF, (can_id >> 8) & 0xFF, (can_id >> 16) & 0xFF, (can_id >> 24) & 0xFF,
                  *data8, 0x55])


def main() -> int:
    fails = []

    def check(cond, label, extra=""):
        print(f"    {'✓' if cond else '✗'} {label}" + (f"   {extra}" if extra else ""))
        if not cond:
            fails.append(label)

    # ── [1] TX 帧构造 ───────────────────────────────────────────────
    print("[1] TX 帧构造（POS_VEL：CAN ID = 0x100+电机ID，D[0:4]/D[4:8] = float32 P/V）")
    p_des, v_des = 0.0125581, 0.5
    frame = B.build_tx(DM_CAN, 0x104, struct.pack("<ff", p_des, v_des))
    check(len(frame) == 30, f"帧长 = {len(frame)}（应为 30）")
    check(frame[0] == 0x55 and frame[1] == 0xAA, f"帧头 = {frame[0]:02x} {frame[1]:02x}（应为 55 AA）")
    check(frame[2] == 30, f"[2] = {frame[2]}（应为 30，与帧长一致）")
    check(frame[13] == 0x04 and frame[14] == 0x01,
          f"[13:15] = {frame[13]:02x} {frame[14]:02x} → CAN ID 小端")
    got_p, got_v = struct.unpack("<ff", frame[21:29])
    check(abs(got_p - p_des) < 1e-9 and abs(got_v - v_des) < 1e-9,
          "D[0:4]/D[4:8] 按 float32 小端解回来一致", f"P={got_p!r} V={got_v!r}")
    # 与厂商 SDK 的写法对拍：同一组数据，SDK 的 float_to_uint8s 应给出相同 4 字节
    check(bytes(DM_CAN.float_to_uint8s(p_des)) == frame[21:25],
          "与 SDK 的 float_to_uint8s 逐字节一致")

    # ── [2] RX 切帧：和 SDK 的切法对拍 ──────────────────────────────
    print("\n[2] RX 切帧（与 SDK 的 __extract_packets 对拍，判据都是 0xAA…0x55 / 16B）")
    f1 = wrap_rx(0x14, pack_feedback(0.1, 0.2, 0.3, 0x1, 41, 45))
    f2 = wrap_rx(0x14, pack_feedback(-0.5, -0.1, -0.2, 0x1, 42, 46))
    f3 = wrap_rx(0x14, pack_feedback(1.0, 0.0, 0.05, 0x0, 40, 44))
    cases = {
        "干净 3 帧": f1 + f2 + f3,
        "前面 5 个脏字节": b"\x01\x02\x03\x04\x05" + f1 + f2,
        "帧后面拖半帧": f1 + f2 + f3[:9],
        "中间夹脏字节": f1 + b"\xAA\xAA\x00" + f2,
    }
    ctrl = bare_ctrl()
    for label, buf in cases.items():
        mine = B.extract_rx(buf)
        ctrl.data_save = b""
        theirs = ctrl._MotorControl__extract_packets(buf)
        ok = mine == theirs and all(len(f) == 16 for f in mine)
        check(ok, f"{label}：切出 {len(mine)} 帧，与 SDK 结果{'一致' if mine == theirs else '不一致'}")

    # ── [3] 反馈解码：和 SDK 的 __process_packet 对拍 ────────────────
    print("\n[3] 反馈解码（我的 decode_feedback vs SDK 的 __process_packet）")
    ctrl2 = bare_ctrl()
    for pos, vel, tau, err in [(0.1234, -0.5, 1.25, 0x1), (-3.0, 4.0, -2.5, 0x1), (12.4, 29.9, 9.9, 0xE)]:
        data8 = pack_feedback(pos, vel, tau, err, 41, 47)
        ctrl2.motors_map = {0x04: DM_CAN.Motor(MT, 0x04, 0x14)}
        ctrl2._MotorControl__process_packet(data8, 0x04, 0x11)
        m = ctrl2.motors_map[0x04]
        mine = B.decode_feedback(data8, LIMIT)
        ok = (abs(m.state_q - mine["pos"]) < 1e-9
              and abs(m.state_dq - mine["vel"]) < 1e-9
              and abs(m.state_tau - mine["tau"]) < 1e-9
              and m.state_err == mine["err"])
        check(ok, f"pos={pos} vel={vel} tau={tau} err=0x{err:x}："
                  f"与 SDK 一致（q={m.state_q:.6f} dq={m.state_dq:.4f} "
                  f"tau={m.state_tau:.4f} err={m.state_err}）")
        check(mine["t_mos_raw"] == 41 and mine["t_rotor_raw"] == 47,
              f"温度只有我解出来：T_MOS={mine['t_mos_raw']}℃ T_Rotor={mine['t_rotor_raw']}℃ "
              "（SDK 的 recv_data 只收 q/dq/tau/err，这俩被丢了）")

    # 量化误差：16 位位置 / 12 位速度力矩，往返后应回到 1 个 LSB 内
    print("\n    量化精度（线性映射的固有误差，不是 bug）：")
    print(f"      位置 16 位，量程 ±{LIMIT[0]} rad → 1 LSB = {2*LIMIT[0]/65535*1000:.3f} mrad")
    print(f"      速度 12 位，量程 ±{LIMIT[1]} rad/s → 1 LSB = {2*LIMIT[1]/4095*1000:.3f} mrad/s")
    print(f"      力矩 12 位，量程 ±{LIMIT[2]} N·m → 1 LSB = {2*LIMIT[2]/4095*1000:.3f} mN·m")

    # ── [5] 接收缓冲：拆帧到达不能丢字节 ─────────────────────────────
    print("\n[5] 接收缓冲 RxBuf（重现真机停机的那两个坑）")

    class DribbleSerial:
        """假串口：一次只吐 chunk 个字节，模拟一帧被拆成多次到达。

        注意 `in_waiting` 报的是"本次能给的量"，不是"总量" —— 真适配器在
        高速下就是这样一小块一小块交出来的。
        """

        def __init__(self, stream, chunk=5):
            self.buf = bytes(stream)
            self.chunk = chunk

        @property
        def in_waiting(self):
            return min(self.chunk, len(self.buf))

        def read(self, n):
            out, self.buf = self.buf[:n], self.buf[n:]
            return out

    stream = f1 + f2 + f3

    # 坑①：残片丢了 → 后续字节全错位。直接对每一小块调 extract_rx 就是这样：
    naive = []
    ser = DribbleSerial(stream, chunk=5)
    while ser.buf:
        naive += [f for f in B.extract_rx(ser.read(ser.in_waiting)) if f[1] == B.FEEDBACK_CMD]
    check(len(naive) < 3,
          f"反面教材：逐块 extract_rx 只切出 {len(naive)}/3 帧（残片被丢 → 错位丢帧）",
          "← 这正是 RxBuf 存在的理由")

    # 坑②：RxBuf 把残片留住，同样按 5 字节一块喂，必须一帧不少
    for chunk in (1, 5, 9, 16, 17):
        rx = B.RxBuf()
        ser = DribbleSerial(stream, chunk=chunk)
        got = B.read_frames(ser, rx, want=3, timeout=0.05)
        check(len(got) == 3 and got == [f1, f2, f3],
              f"按 {chunk} 字节/块喂 3 帧 → 切出 {len(got)} 帧，逐字节一致",
              f"残留 {len(rx.buf)}B" + ("（=0，干净）" if not rx.buf else ""))

    # 坑③：非阻塞读在"反馈还没到"时必须返回空，而不是抛异常或死等
    rx = B.RxBuf()
    empty = DribbleSerial(b"", chunk=5)
    got = B.read_frames(empty, rx, want=1, timeout=0.02)
    check(got == [], "对空串口读 20ms → 返回空列表（不抛异常、不卡死）")

    # ── [6] MIT 帧 vs SDK controlMIT ────────────────────────────────
    print("\n[6] MIT 帧（我的 mit_frame vs SDK controlMIT，逐字节）")

    class FakeSerial:
        """记录 SDK 真正写出去的字节，不碰真硬件。

        必须实现 `read_all()`：`controlMIT` 内部会调 `self.recv()`（DM_CAN.py:150），
        而 `recv()` 就是 `self.serial_.read_all()`（:322）。返回空字节即可 ——
        我们只关心 SDK **写出去**的那 30 字节对不对。
        """

        def __init__(self):
            self.written = []

        def write(self, b):
            self.written.append(bytes(b))
            return len(b)

        def read_all(self):
            return b""

        def is_open(self):
            return True

    def sdk_mit(kp, kd, q, dq, tau):
        fs = FakeSerial()
        ctrl = DM_CAN.MotorControl.__new__(DM_CAN.MotorControl)   # 绕过会开串口的 __init__
        ctrl.serial_ = fs
        ctrl.data_save = b""
        ctrl.motors_map = {0x01: DM_CAN.Motor(DM_CAN.DM_Motor_Type.DM4310, 0x01, 0x11)}
        # Limit_Param 按实机回读值（12.5/30/10），否则 SDK 会用自己的默认表
        ctrl.Limit_Param = [list(LIMIT)] * len(DM_CAN.MotorControl.Limit_Param)
        ctrl.controlMIT(ctrl.motors_map[0x01], kp, kd, q, dq, tau)
        return fs.written[-1]

    # 覆盖：零增益 / 中间值 / 负值 / 两端饱和。饱和那两组最能验映射公式
    for kp, kd, q, dq, tau in [(0.0, 0.1, 0.0795, 0.0, 0.0),
                               (1.0, 0.1, 0.3795, 0.0, 0.0),
                               (18.0, 2.0, -0.5, 0.3, 0.5),
                               (500.0, 5.0, 12.5, 30.0, 10.0),
                               (0.0, 0.0, -12.5, -30.0, -10.0)]:
        mine = B.mit_frame(DM_CAN, 0x01, q, dq, kp, kd, tau, LIMIT)
        theirs = sdk_mit(kp, kd, q, dq, tau)
        check(mine == theirs,
              f"kp={kp:<6g} kd={kd:<4g} q={q:<7g} dq={dq:<6g} t_ff={tau:<6g}"
              f" → D={mine[21:29].hex(' ')}",
              "" if mine == theirs else f"SDK 给出 {theirs[21:29].hex(' ')}")

    # CAN ID 必须是电机 ID 本身，不是 POS_VEL 的 0x100+ID —— 发错了电机是不动的
    mt_frame = B.mit_frame(DM_CAN, 0x01, 0.0, 0.0, 0.0, 0.0, 0.0, LIMIT)
    check(mt_frame[13] == 0x01 and mt_frame[14] == 0x00,
          "MIT 帧的 CAN ID = 电机 ID 本身（0x001），不是 POS_VEL 的 0x101")
    check(mt_frame[21:29] != B.build_tx(DM_CAN, 0x101, struct.pack("<ff", 0.0, 0.0))[21:29],
          "同一个 ID 上，MIT 帧与 POS_VEL 帧的数据字节完全不同（两种模式两种帧）")

    # ── [7] 打印函数不崩 ────────────────────────────────────────────
    print("\n[7] explain_tx / explain_rx 不崩")
    try:
        B.explain_tx(frame, "冒烟")
        B.explain_rx(f1, LIMIT)
        print("    ✓ 两个打印函数都跑通")
    except Exception as e:
        check(False, f"打印函数抛异常：{e!r}")

    print()
    if fails:
        print(f"FAIL（{len(fails)} 项）：")
        for f in fails:
            print(f"  - {f}")
        return 1
    print("PASS: 帧构造 / 切帧 / 反馈解码 / MIT 帧四项都与厂商 SDK 对拍一致，"
          "接收缓冲抗拆帧；"
          "温度解析是 SDK 之外自己补的")
    return 0


if __name__ == "__main__":
    sys.exit(main())
