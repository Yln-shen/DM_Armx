#!/usr/bin/env python3
"""dm_frames —— 达妙 CAN 帧的编解码原语（纯函数，没有 CLI）

**这一层是整个项目的地基，也是最危险的地方**：一帧拼错就是"电机不动"或"读数全错"
这种说不清的毛病。所以协议代码**全项目只允许有一份** —— 原先它在 `dm_bringup.py`
里，但那样封装层（`MotorBus`）就得依赖一个 CLI 脚本，方向是倒的。现在下沉到这里：
`dm_bringup.py`、`dm_registers.py`、`tools/*`、`dm_bus.py` 全部 import 本模块。

## 边界（改这个文件前先读）

- **本模块不 import 本包其它模块**，不 import argparse，不打屏，不 `sys.exit`。
  它只做「字节 ↔ 帧 ↔ 物理量」的换算，没有任何状态（`RxBuf` 除外，那是收帧缓冲）。
- **本模块不发任何帧**。所有 `*_frame()` 都是**构造**函数，返回 `bytes`；
  真正 `serial.write()` 的唯一出口是 `MotorBus.send_frame()`。
- **本模块不判断安全**。限位、力矩阈值、急停都在更上层（`Joint` / `DmArm`）。

## 帧格式（实测坐标）

    发送（主机→适配器）30 字节： [0:2]=55 AA  [2]=0x1e(帧长)  [13:15]=CAN ID 小端
                                  [21:29]=8 字节 CAN 数据
    接收（适配器→主机）16 字节： [0]=0xAA  [1]=CMD(反馈=0x11)  [3:7]=CAN ID  [7:15]=数据
                                  [15]=0x55

## CAN ID 规则（易错，两套别搞混）

    POS_VEL   0x100 + SlaveID      ← 不是 SlaveID 本身
    MIT       SlaveID 本身
    使能/失能  SlaveID 本身         （数据 FF*7 + 0xFC/0xFD）
    读寄存器   0x33 ； 刷新  0x7FF  （广播）
"""
from __future__ import annotations

import struct
import time

TX_FRAME_LEN = 30          # 主机→适配器，send_data_frame 共 30 个元素，[2]=0x1e=30
RX_FRAME_LEN = 16          # 适配器→主机，__extract_packets 的 frame_length = 16
FEEDBACK_CMD = 0x11        # 反馈帧的 CMD

CMD_ENABLE = 0xFC
CMD_DISABLE = 0xFD
CANID_REFRESH = 0x7FF      # 刷新帧是广播，所有电机都回

# ERR 是反馈帧 D[0] 的高 4 位（手册"反馈帧"表 + ERR 状态表）
ERR_DECODE = {
    0x0: "失能（未使能 / 已失能）",
    0x1: "使能",
    0x8: "超压",
    0x9: "欠压",
    0xA: "过电流",
    0xB: "MOS 过温",
    0xC: "电机线圈过温",
    0xD: "通讯丢失（超时）",
    0xE: "过载",
}

# 只有这两个是"正常"状态；其余都是故障，且电机已经自行退出使能。
# ⚠️ 0xD（通讯丢失）是**锁存**的：enable / 连发帧 / 写 0x09=0 都清不掉，
#    唯一办法是给电机断电再上电。详见 ARCHITECTURE.md §六 三条硬性事实。
ERR_OK = (0x0, 0x1)


# ───────────────────────────── 1. 发送帧构造 ─────────────────────────────
# 发送帧模板（30 字节）。原本直接读 SDK 的 `MotorControl.send_data_frame` 类属性，
# 现在自持 —— 这是本模块最后一个 SDK 依赖，去掉后本层不再需要厂商 SDK。
#
# 实测回读的 30 字节（`bytes(...).hex(' ')`）：
#     55 aa 1e 03 01 00 00 00 0a 00 | 00 00 00 00 00 00 00 00 08 00 | 00 00 00 00 00 00 00 00 00 00
#   [0:2]=55 AA 帧头  [2]=0x1e=30 帧长  [8]=0x0a  [18]=0x08，其余固定 0。
#
# ⚠️ 这些是**不可推导的魔数**：改错任何一个字节 = 适配器不认帧 = 电机不动，
#    而且不会有任何报错。唯一的证据是 `tools/smoke_dm_frames.py` [6] 拿 SDK 的
#    `controlMIT` 输出做**整帧 30 字节对拍** —— 那条测试挂了就是这里错了。
_TX_TEMPLATE = bytes([
    0x55, 0xAA, 0x1E, 0x03, 0x01, 0x00, 0x00, 0x00, 0x0A, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
])


def tx_template(DM_CAN) -> bytearray:
    """发送帧模板（30 字节）。**每次返回新对象** —— 调用方原地改它不会互相污染。

    ⚠️ `DM_CAN` 参数现在用不上了（原先读 SDK 类属性），只为不改调用方而留着，
    第二步统一删掉。
    """
    return bytearray(_TX_TEMPLATE)


def build_tx(DM_CAN, can_id: int, data8) -> bytes:
    """自构一条 30 字节适配器发送帧。CAN ID 走 [13],[14]（小端），数据走 [21:29]。

    数据**必须正好 8 字节**：`bytearray` 的切片赋值在长度不等时会把帧**悄悄改长**，
    后面所有字节跟着错位。原来用 numpy 会直接报错，这里显式挡一下，别丢掉这个保护。
    """
    data = bytes(data8)
    if len(data) != 8:
        raise ValueError(f"CAN 数据必须 8 字节，收到 {len(data)} 字节：{data.hex(' ')}")

    frame = tx_template(DM_CAN)
    frame[13] = can_id & 0xFF
    frame[14] = (can_id >> 8) & 0xFF
    frame[21:29] = data
    return bytes(frame)


# 定点映射原语。原本调 SDK 的 `float_to_uint` / `float_to_uint8s`，现在自持。
# 与 SDK `DM_CAN.py:LIMIT_MIN_MAX/float_to_uint/float_to_uint8s` 逐字一致：
#   先钳位到 [x_min, x_max]，再线性映射到 [0, 2^bits - 1]。
# ⚠️ 是**截断**（`int()` / numpy 的 `uint16(float)` 都是向零截断），不是四舍五入。
#    差 1 个 LSB 换不来什么，但"和厂商行为不一致"会让整帧对拍失去意义。
def limit_min_max(x, x_min, x_max):
    """SDK `LIMIT_MIN_MAX` 的等价实现。注意边界：`x == x_min` 判进下界，`x == x_max` 判进上界。"""
    if x <= x_min:
        return x_min
    if x > x_max:
        return x_max
    return x


def float_to_uint(x, x_min, x_max, bits: int) -> int:
    """线性映射到定长无符号整数。返回**普通 int**（SDK 返回 np.uint16）。"""
    x = limit_min_max(x, x_min, x_max)
    return int((x - x_min) / (x_max - x_min) * ((1 << bits) - 1))


def float_to_uint8s(value) -> bytes:
    """float32 **小端** 4 字节。SDK 写的是 `unpack('4B', pack('f', v))`，字节相同；
    显式 `<f` 比 SDK 的本地字节序更不容易出错。"""
    return struct.pack("<f", float(value))


def mit_frame(DM_CAN, slave_id, p_des, v_des, kp, kd, t_ff, limit) -> bytes:
    """MIT 控制帧。CAN ID = **电机ID 本身**（不是 POS_VEL 的 0x100+ID）。

    位布局与 SDK 的 `controlMIT` 逐位一致（`DM_CAN.py` controlMIT 函数）：
        D[0:2]=p_des[15:0]   D[2:4]=v_des[11:0]   D[4:6]=Kp[11:0]
        D[6:8]=Kd[11:0] 与 t_ff[11:0] 交错
    Kp/Kd 是**线性映射**（手册:373：Kp 范围 [0,500]、Kd 范围 [0,5]），
    即 kp=1.0 → 1.0/500*4095 = 8，不是把 1 当原始值写进去。
    映射用本模块的 `float_to_uint`，与厂商实现逐字节一致（smoke [6] 整帧对拍）。
    """
    p_max, v_max, t_max = limit
    u12 = float_to_uint
    q_u = u12(p_des, -p_max, p_max, 16)
    dq_u = u12(v_des, -v_max, v_max, 12)
    kp_u = u12(kp, 0, 500, 12)
    kd_u = u12(kd, 0, 5, 12)
    t_u = u12(t_ff, -t_max, t_max, 12)
    data = bytes([
        (q_u >> 8) & 0xFF, q_u & 0xFF,
        (dq_u >> 4) & 0xFF,
        ((dq_u & 0x0F) << 4) | ((kp_u >> 8) & 0x0F),
        kp_u & 0xFF,
        (kd_u >> 4) & 0xFF,
        ((kd_u & 0x0F) << 4) | ((t_u >> 8) & 0x0F),
        t_u & 0xFF,
    ])
    return build_tx(DM_CAN, slave_id, data)


def pos_vel_frame(DM_CAN, slave_id, p_des: float, v_des: float) -> bytes:
    """POS_VEL 控制帧。CAN ID = **0x100 + SlaveID**（与 MIT 不同，别搞混）。

    数据 8 字节 = `float32(p_des)` + `float32(v_des)`，**无任何缩放** —— 与 MIT 帧
    的 16/12/12 定点映射完全不是一回事，位置单位直接是输出轴 rad。

    ⚠️ 为什么自己拼而不用 SDK 的 `control_Pos_Vel`（`DM_CAN.py:166`）：
        `__send_data` → `sleep(0.001)` → `recv()`，而 `recv()` 是 `read_all()`，
        串口 `timeout=0.5`。7 电机 × 500Hz 直接出局（design.md §2.6 坑 1）。
        本函数只构造，不发 —— 发是 `MotorBus.send_frame` 的事。

    ⚠️ 本帧只有在电机 **CTRL_MODE == 2 (POS_VEL)** 时才被认。切模式是 `Joint`/`DmArm`
        的活（design.md D3），本模块不切、也不检查。
    """
    p_bytes = float_to_uint8s(p_des)
    v_bytes = float_to_uint8s(v_des)
    return build_tx(DM_CAN, 0x100 + slave_id, p_bytes + v_bytes)


def cmd_frame(DM_CAN, slave_id, cmd: int) -> bytes:
    """使能(0xFC) / 失能(0xFD) 帧。CAN ID = **SlaveID 本身**，数据 = FF*7 + cmd。

    与 SDK 的 `__control_cmd`（`DM_CAN.py:408-410`）逐字节一致。
    """
    if cmd not in (CMD_ENABLE, CMD_DISABLE):
        raise ValueError(f"未知命令 0x{cmd:02X}，只支持 0xFC 使能 / 0xFD 失能")
    return build_tx(DM_CAN, slave_id, bytes([0xFF] * 7 + [cmd]))


def refresh_frame(DM_CAN, slave_id) -> bytes:
    """0x7FF 刷新帧（读状态用）。数据前两字节是目标电机 ID。

    与 SDK 的 `refresh_motor_status` 数据字节一致。注意它也是**广播 CAN ID**，
    但数据里带了要查的 ID，所以只有那台电机会回。
    """
    return build_tx(
        DM_CAN, CANID_REFRESH,
        bytes([slave_id & 0xFF, (slave_id >> 8) & 0xFF, 0xCC, 0, 0, 0, 0, 0]),
    )


# ───────────────────────────── 2. 接收与解码 ─────────────────────────────
def extract_rx(buf: bytes) -> list[bytes]:
    """按 SDK 的规则切出 16 字节接收帧：[0]=0xAA [15]=0x55。

    与 `MotorControl.__extract_packets` 的判据完全一致（header 0xAA / tail 0x55 /
    frame_length 16），只是我要自己拿到完整 8 个数据字节 —— 因为 SDK 的
    `recv_data` 只收 q/dq/tau/err，把 D[6]/D[7] 的温度丢掉了。
    """
    frames, i = [], 0
    while i <= len(buf) - RX_FRAME_LEN:
        if buf[i] == 0xAA and buf[i + RX_FRAME_LEN - 1] == 0x55:
            frames.append(buf[i : i + RX_FRAME_LEN])
            i += RX_FRAME_LEN
        else:
            i += 1
    return frames


class RxBuf:
    """带残留的接收缓冲：尾部不足一帧的字节留到下一次，**绝不丢**。

    为什么不能「读一次切一次」：`extract_rx` 只切完整的 16 字节帧，尾部残片会被
    丢掉。要是某一帧被拆成两次到达，丢掉的残片就让**后面所有字节错位** —— 数据里
    任何一个凑巧凑成「0xAA … 0x55」的 16 字节都会被误认成帧，切出来就是垃圾。
    所以残片必须留着，等下一个 chunk 拼上。
    """

    def __init__(self):
        self.buf = b""

    def feed(self, chunk: bytes) -> None:
        if chunk:
            self.buf += chunk

    def drain(self) -> list[bytes]:
        """切出当前所有完整帧，保留尾部残片。"""
        out, i = [], 0
        while i + RX_FRAME_LEN <= len(self.buf):
            if self.buf[i] == 0xAA and self.buf[i + RX_FRAME_LEN - 1] == 0x55:
                out.append(self.buf[i : i + RX_FRAME_LEN])
                i += RX_FRAME_LEN
            else:
                i += 1
        self.buf = self.buf[i:]
        return out


def read_frames(ser, rx: RxBuf, want: int = 1, timeout: float = 0.05,
                raw_sink: list | None = None) -> list[bytes]:
    """读到 want 个反馈帧，或超时返回已经拿到的（可能为空）。

    **这是本项目最容易写错的一处**：pyserial 的 `read_all()` 是**非阻塞**的，
    写完一帧马上调它，反馈还在路上，它直接返回空 —— 于是"收不到反馈"。真机第一次
    点动就是这么在第 1.6 秒误判停机的（电机其实好好的，位置/温度/错误码全程正常）。

    所以改成：轮询 `in_waiting` 把能读的都读进 `RxBuf` → 切帧 → 不够就等到
    deadline。不直接调 `ser.read(n)`，因为那会吃满串口自带的超时，高速循环
    会被它拖垮；这里用 0.5ms 的睡眠去等，一帧在 921600 下只要 0.17ms 就到。

    **`want=0` 是"非阻塞抽干"的用法**：deadline 判断 `len(fb) >= 0` 立刻为真，
    只收"已经到了的"字节，绝不等。`MotorBus.poll()` 走的就是这条。
    """
    deadline = time.monotonic() + timeout
    fb: list[bytes] = []
    while True:
        n = ser.in_waiting
        if n:
            chunk = ser.read(n)
            if raw_sink is not None:
                raw_sink.append(chunk)   # 给调用方留证据：到底回来了多少字节
            rx.feed(chunk)
        fb += [f for f in rx.drain() if f[1] == FEEDBACK_CMD]
        if len(fb) >= want or time.monotonic() >= deadline:
            return fb
        time.sleep(0.0005)


def flush_rx(ser, rx: RxBuf) -> int:
    """丢掉缓冲区里已有的帧，返回丢掉的条数。

    发命令**之前**调，才能保证随后读到的那帧是**本条命令的**应答（1:1 规律），
    而不是上一条的残留 —— 否则每次读到的都慢一帧，位置/力矩全是旧值，
    安全判断（跳变、超力矩）就建立在过期数据上了。
    """
    n = 0
    while True:
        got = read_frames(ser, rx, want=1, timeout=0.0)
        if not got:
            return n
        n += len(got)


def decode_feedback(data: bytes, limit: tuple[float, float, float]) -> dict:
    """解一条反馈帧的 8 个数据字节。

    字节布局（手册「反馈帧」表）：
        D[0]=ID|ERR<<4   D[1:3]=POS[15:0]   D[3:5]=VEL[11:0]
        D[5:7]=T[11:0]   D[6]=T_MOS(℃)      D[7]=T_Rotor(℃)
    位域拼法与 SDK 的 `__process_packet` 一字不差，只是我多留了 D[6]/D[7]。

    ⚠️ `limit` = (PMAX, VMAX, TMAX) 是**每台电机自己的**映射范围，必须来自
       0x15/0x16/0x17 的回读值。4310 与 4340P 的档位不同，**用错档位解出来的
       力矩会差 4 倍**（design.md D5）。这不是可以全局写死的常数。
    """
    p_max, v_max, t_max = limit
    err = (data[0] >> 4) & 0x0F
    motor_id = data[0] & 0x0F
    pos_u = (data[1] << 8) | data[2]
    vel_u = (data[3] << 4) | (data[4] >> 4)
    tau_u = ((data[4] & 0x0F) << 8) | data[5]
    to_float = lambda u, bits, lo, hi: (u / ((1 << bits) - 1)) * (hi - lo) + lo
    return {
        "id": motor_id,
        "err": err,
        "err_text": ERR_DECODE.get(err, f"未知错误码 {err}"),
        "pos": to_float(pos_u, 16, -p_max, p_max),
        "vel": to_float(vel_u, 12, -v_max, v_max),
        "tau": to_float(tau_u, 12, -t_max, t_max),
        "t_mos_raw": data[6],
        "t_rotor_raw": data[7],
        "raw": bytes(data),
    }
