#!/usr/bin/env python3
"""dm_bringup —— 单电机上电验证脚本（design.md §11.2）

**刻意站在封装层之外**：只依赖 vendored 的 `DM_CAN.py` + `pyserial`，不 import 本包
其它模块。目的有两个：
  1. 在写自己的 `MotorBus`/`Joint` 之前，先证明「官方 SDK + 你的适配器 + 你的电机」这条
     路本身是通的（否则后面排障分不清是封装层写错还是链路本身有问题）；
  2. 把「一条 CAN 帧长什么样」打出来看 —— 这是"吃透协议"最直接的一步。

## 本脚本的写操作边界（重要）

- **默认只读**。`read` / `monitor` 全程不发控制帧、不写任何寄存器。
- **永不调用 `set_zero_position`（0xFE）**，永不调用 `change_motor_param` / `save_motor_param`。
  零位/方向标定是 `dm-calibrate` 的事，不在这里做。
- 只有 `jog` 和 `bandwidth --enable` 会让电机通电出力，两者都必须显式加 `--yes`。
  且都是"从当前位置出发、最后回到当前位置、退出前必失能"（含 Ctrl-C 路径）。
- 本脚本**不切控制模式**。若 `read` 报 CTRL_MODE != 2（POS_VEL），只提示、不自动改。

## 子命令

    read       （默认）读型号/映射范围/PID 现状/看门狗/电压温度，只读
    monitor    按 --hz 连续刷新状态（含温度，SDK 丢掉的 D[6]/D[7] 这里自己解析）
    jog        单电机低速点动 ±A rad，打印每条命令的收/发帧
                加 --mit 走 MIT 模式（零寄存器写入，电机停在哪个模式都能跑）
    bandwidth  链路吞吐实测：按 --hz 硬发 N 秒，量实际速率与单帧写延迟

## 用法（在 DM_Armx 根目录，一切走 pixi）

    # 不接硬件先看帧长什么样（不需要 pyserial，现在就能跑）
    pixi run python src/DMmotor_driver/DMmotor_driver/dm_bringup.py read --dry-run
    pixi run python src/DMmotor_driver/DMmotor_driver/dm_bringup.py jog --dry-run

    # 接上硬件。先只读（默认就是 joint4 = 0x04/0x14，4310，力矩最小的一档）
    pixi run python src/DMmotor_driver/DMmotor_driver/dm_bringup.py read
    pixi run python src/DMmotor_driver/DMmotor_driver/dm_bringup.py monitor --duration 10

    # 确认无误后，单电机低速点动（必须 --yes）
    #   MIT 模式：零寄存器写入，裸电机首选。三段阶梯：零刚度 → 原地保持 → 正弦
    pixi run python src/DMmotor_driver/DMmotor_driver/dm_bringup.py jog --mit --dry-run
    pixi run python src/DMmotor_driver/DMmotor_driver/dm_bringup.py jog --mit --yes
    #   POS_VEL 模式：需电机已切到 CTRL_MODE=2（本脚本不切）
    pixi run python src/DMmotor_driver/DMmotor_driver/dm_bringup.py jog --yes --amp 0.2

    # 链路吞吐实测：先不加 --enable（纯发帧，电机不动），再加 --enable 看完整闭环
    pixi run python src/DMmotor_driver/DMmotor_driver/dm_bringup.py bandwidth --hz 500 --duration 30
    pixi run python src/DMmotor_driver/DMmotor_driver/dm_bringup.py bandwidth --hz 500 --duration 30 --enable --yes

设计依据见 src/DMmotor_driver/design.md §7（标定）/ §8（上电序列）/ §11.2（本脚本范围）。
"""
from __future__ import annotations

import argparse
import math
import struct
import sys
import time
from pathlib import Path

# 链路容量：921600 8N1 = 921600 / 10 = 92160 B/s
LINK_BYTES_PER_S = 921600 / 10.0
TX_FRAME_LEN = 30          # 主机→适配器，send_data_frame 共 30 个元素，[2]=0x1e=30
RX_FRAME_LEN = 16          # 适配器→主机，__extract_packets 的 frame_length = 16
FEEDBACK_CMD = 0x11        # 反馈帧的 CMD

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

# 待读寄存器。RID 用 DM_variable 枚举；60/61/62 是裸 int，专门用来验证
# "SDK 能否用裸 int RID 读到手册里 SDK 没枚举的寄存器"（design.md §2.6 坑 2）。
READ_REGISTERS = [
    (20, "0x14 Gr", "减速比。应为 10(4310) / 40(4340P) —— 用它验型号填错没有"),
    (21, "0x15 PMAX", "位置映射范围。下面会拿这个回读值覆盖 SDK 硬编码表"),
    (22, "0x16 VMAX", "速度映射范围。同上"),
    (23, "0x17 TMAX", "扭矩映射范围。4340P 手册 40 N·m，SDK 表里写的是 28 → 以回读为准"),
    (25, "0x19 KP_ASR", "速度环 Kp。PID 现状，先读后写要用它做回滚基线"),
    (26, "0x1A KI_ASR", "速度环 Ki"),
    (27, "0x1B KP_APR", "位置环 Kp（reBot 抄的就是这个）"),
    (28, "0x1C KI_APR", "位置环 Ki"),
    (9, "0x09 TIMEOUT", "uint32。电机侧看门狗：主机崩溃时唯一兜底，务必确认非 0"),
    (7, "0x07 MST_ID", "uint32。反馈帧 CAN ID，达妙出厂默认 0（实测反馈就是 0x000）"),
    (10, "0x0A CTRL_MODE", "uint32。2 = POS_VEL（本脚本要求的值，只读不改）"),
    (60, "0x3C VBus", "裸 int RID（手册有、SDK 未枚举）→ 电源电压"),
    (61, "0x3D Tpcb", "裸 int RID → 驱动板温度"),
    (62, "0x3E Tmt", "裸 int RID → 电机温度"),
]


# ───────────────────────────── 0. 找 SDK 并加载 ─────────────────────────────
def find_sdk_dir(explicit: str | None) -> Path:
    """定位 vendored 的 DM_CAN.py。路径含中文与空格，全程 pathlib。

    从本文件向上最多 8 层找 `src/third_party/Python例程/u2can/DM_CAN.py`，
    这样源码树（src/.../DMmotor_driver/dm_bringup.py）和 --symlink-install 后的
    安装树（install/.../site-packages/...）都能找到。
    """
    if explicit:
        p = Path(explicit).expanduser().resolve()
        return p if p.name == "u2can" else p

    rel = Path("src/third_party/Python例程/u2can")
    here = Path(__file__).resolve()
    for parent in list(here.parents)[:8]:
        cand = parent / rel
        if (cand / "DM_CAN.py").is_file():
            return cand
    die(
        "找不到 vendored 的 DM_CAN.py。请用 --sdk-dir 指定，例如\n"
        "  --sdk-dir ~/DM_Armx/src/third_party/Python例程/u2can"
    )


def load_sdk(sdk_dir: Path):
    if not (sdk_dir / "DM_CAN.py").is_file():
        die(f"{sdk_dir} 下没有 DM_CAN.py")
    sys.path.insert(0, str(sdk_dir))
    try:
        import DM_CAN  # noqa: E402  （必须在插入 sys.path 之后）
    except ImportError as e:  # numpy 缺失等
        die(f"加载 DM_CAN.py 失败：{e}\npixi 环境里缺 numpy？试试 `pixi add numpy`")
    return DM_CAN


def die(msg: str) -> None:
    print(f"\n[错误] {msg}\n", file=sys.stderr)
    raise SystemExit(2)


def _w(s: str) -> int:
    """字符串显示宽度。CJK 字符占两格，用 len() 对不齐中文表头。"""
    import unicodedata

    return sum(2 if unicodedata.east_asian_width(c) in "WF" else 1 for c in str(s))


def pad(s: str, n: int, right: bool = False) -> str:
    spaces = " " * max(0, n - _w(s))
    return spaces + str(s) if right else str(s) + spaces


# ───────────────────────────── 1. 帧的构造与解读 ─────────────────────────────
def tx_template(DM_CAN):
    """取 SDK 的发送帧模板（30 字节）。直接读它的类属性，保证与 SDK 同步。"""
    import numpy as np

    return np.array(DM_CAN.MotorControl.send_data_frame, dtype=np.uint8)


def build_tx(DM_CAN, can_id: int, data8) -> bytes:
    """自构一条 30 字节适配器发送帧。CAN ID 走 [13],[14]（小端），数据走 [21:29]。"""
    import numpy as np

    frame = tx_template(DM_CAN)
    frame[13] = can_id & 0xFF
    frame[14] = (can_id >> 8) & 0xFF
    frame[21:29] = np.frombuffer(bytes(data8), dtype=np.uint8)
    return bytes(frame)


_LEGEND_SHOWN = False


def explain_tx(frame: bytes, title: str = "", legend: bool | None = None) -> None:
    """把 30 字节发送帧按字段拆开打印。只标注能从代码/手册确证的部分。

    完整字段图例只在本次运行里打一次（后面几帧只打 "已确证" 那行），
    否则连发 3 帧就是 9 行噪声。
    """
    global _LEGEND_SHOWN
    if legend is None:
        legend = not _LEGEND_SHOWN
    _LEGEND_SHOWN = True

    can_id = frame[13] | (frame[14] << 8)
    print(f"  TX {len(frame)}B {frame.hex(' ')}")
    if title:
        print(f"     └ {title}")
    print(
        f"     └ [0:2]=55 AA 帧头  [2]=0x{frame[2]:02x}(={frame[2]} 应为帧长)"
        f"  [13:15]={frame[13]:02x} {frame[14]:02x} → CAN ID 0x{can_id:03x}"
        f"  [21:29]={frame[21:29].hex(' ')}"
    )
    if legend:
        print(
            f"     └ 上表之外都是适配器固定字段（官方 Python 例程没说明，纯按位置列出）："
            f"[3]=0x{frame[3]:02x} [4]=0x{frame[4]:02x} [5:13]={frame[5:13].hex(' ')}"
            f" [15:20]={frame[15:20].hex(' ')}（其中 [18]=0x{frame[18]:02x}，推测 CAN DLC=8）"
            f" [29]=0x{frame[29]:02x}"
        )


def mit_frame(DM_CAN, slave_id, p_des, v_des, kp, kd, t_ff, limit) -> bytes:
    """MIT 控制帧。CAN ID = **电机ID 本身**（不是 POS_VEL 的 0x100+ID）。

    位布局与 SDK 的 `controlMIT` 逐位一致（`DM_CAN.py` controlMIT 函数）：
        D[0:2]=p_des[15:0]   D[2:4]=v_des[11:0]   D[4:6]=Kp[11:0]
        D[6:8]=Kd[11:0] 与 t_ff[11:0] 交错
    Kp/Kd 是**线性映射**（手册:373：Kp 范围 [0,500]、Kd 范围 [0,5]），
    即 kp=1.0 → 1.0/500*4095 = 8，不是把 1 当原始值写进去。
    这里直接用 SDK 的 `float_to_uint`，保证与厂商实现逐字节一致。
    """
    p_max, v_max, t_max = limit
    u12 = lambda x, lo, hi, bits: int(DM_CAN.float_to_uint(x, lo, hi, bits))
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
    """阻塞读到 want 个反馈帧，或超时返回已经拿到的（可能为空）。

    **这是本文件最容易写错的一处**：pyserial 的 `read_all()` 是**非阻塞**的，
    写完一帧马上调它，反馈还在路上，它直接返回空 —— 于是"收不到反馈"。真机第一次
    点动就是这么在第 1.6 秒误判停机的（电机其实好好的，位置/温度/错误码全程正常）。

    所以改成：轮询 `in_waiting` 把能读的都读进 `RxBuf` → 切帧 → 不够就等到
    deadline。不直接调 `ser.read(n)`，因为那会吃满串口自带的 50ms 超时，高速循环
    会被它拖垮；这里用 0.5ms 的睡眠去等，一帧在 921600 下只要 0.17ms 就到。
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
    而不是上一条的残留 —— 否则每次读到的都慢一帧，位置/力矩全是 20ms 前的旧值，
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


def explain_rx(frame: bytes, limit: tuple[float, float, float]) -> None:
    data = frame[7:15]
    can_id = frame[3] | (frame[4] << 8) | (frame[5] << 16) | (frame[6] << 24)
    d = decode_feedback(data, limit)
    print(f"  RX {len(frame)}B {frame.hex(' ')}")
    print(
        f"     └ CMD=0x{frame[1]:02x}  CAN ID=0x{can_id:03x}(=0x07 MST_ID 寄存器值)  "
        f"D={data.hex(' ')}"
    )
    print(
        f"     └ D[0]=0x{data[0]:02x} → 电机ID={d['id']} ERR={d['err']}({d['err_text']})"
    )
    print(
        f"     └ POS[15:0]={((data[1] << 8) | data[2]):5d} → {d['pos']: .4f} rad   "
        f"VEL[11:0]={((data[3] << 4) | (data[4] >> 4)):4d} → {d['vel']: .4f} rad/s"
    )
    print(
        f"     └ T[11:0]={(  ((data[4] & 0x0F) << 8) | data[5]):4d} → {d['tau']: .4f} N·m"
        f"   （映射范围 ±{limit[2]:g}，取自 0x17 TMAX 回读值）"
    )
    print(
        f"     └ D[6]=T_MOS={d['t_mos_raw']:3d} ℃   D[7]=T_Rotor={d['t_rotor_raw']:3d} ℃"
        f"   ← SDK 的 recv_data 把它俩丢了，这里是自己解的"
    )


# ───────────────────────────── 2. 串口 ─────────────────────────────
def open_port(port: str, timeout: float):
    try:
        import serial
    except ImportError:
        die(
            "pixi 环境里没有 pyserial。请先安装（我不擅自改你的 lockfile）：\n"
            "    pixi add pyserial\n"
            "（不改环境也能先看协议：给任意子命令加 --dry-run）"
        )
    if not Path(port).exists():
        import glob

        found = sorted(glob.glob("/dev/ttyACM*") + glob.glob("/dev/ttyUSB*"))
        die(
            f"{port} 不存在。当前可见的串口：{found or '（一个都没有）'}\n"
            "  检查：USB-CAN 适配器插好了吗？`lsusb` 能看到吗？"
        )
    try:
        ser = serial.Serial(port, 921600, timeout=timeout)
    except serial.SerialException as e:
        die(
            f"打不开 {port}：{e}\n"
            f"  权限：`ls -l {port}` 看属主；需要的话 `sudo usermod -aG dialout $USER` 后重新登录\n"
            f"  被占用：`fuser -v {port}` —— 同一时刻只能有一个程序占用总线"
        )
    return ser


def refresh_and_read(ser, DM_CAN, slave_id: int, limit, wait: float = 0.05,
                     rx: "RxBuf | None" = None):
    """发一条 0x7FF 刷新帧，然后自己解析回来的反馈帧。

    不用 `ctrl.refresh_motor_status()`，因为它内部会 read_all() 把字节吃掉，
    我就拿不到 D[6]/D[7] 了。数据帧字节与 SDK 的 `refresh_motor_status` 完全一致。

    返回 (帧列表, 本次收到的原始字节, 发出去的数据字节)。发之前先 flush，保证拿到
    的是这条刷新帧的应答而不是上一条的残留。
    """
    rx = rx if rx is not None else RxBuf()
    flush_rx(ser, rx)
    data = bytes([slave_id & 0xFF, (slave_id >> 8) & 0xFF, 0xCC, 0, 0, 0, 0, 0])
    ser.write(build_tx(DM_CAN, 0x7FF, data))
    raw_sink: list[bytes] = []
    fb = read_frames(ser, rx, want=1, timeout=wait, raw_sink=raw_sink)
    return fb, b"".join(raw_sink), data


# ───────────────────────────── 3. 读寄存器（带重试）─────────────────────────────
def read_param(ctrl, motor, rid, attempts: int = 3):
    """读一个寄存器，返回 (值, 用了几次)。

    为什么要包一层：SDK 的 `read_motor_param` 里那个 for 循环在"没读到"时会
    直接 `return None`（`else: return None` 在循环体内），也就是**只等一个 50ms
    就放弃**；而且它命中缓存就立刻返回，缓存可能是上次的旧值。所以：
      ① 每次尝试前清掉该 RID 的缓存，保证读的是新值；
      ② 失败就重试，并如实报出用了几次 —— 重试次数本身就是链路质量的指标。
    """
    for i in range(1, attempts + 1):
        motor.temp_param_dict.pop(int(rid), None)
        val = ctrl.read_motor_param(motor, rid)
        if val is not None:
            return val, i
    return None, attempts


# ───────────────────────────── 4. 子命令 ─────────────────────────────
def banner(args, motor_type_name: str) -> None:
    print("=" * 78)
    print(f"dm_bringup · {args.cmd}   电机ID=0x{args.id:02X}  反馈ID=0x{args.fb_id:02X}"
          f"  type={motor_type_name}")
    print(f"  端口={args.port} @921600 8N1   serial timeout={args.timeout*1000:.0f}ms"
          + ("   【DRY-RUN：不开串口、不发帧】" if args.dry_run else ""))
    print("  写操作边界：不写任何寄存器、不切控制模式、永不调用 set_zero_position(0xFE)")
    print("=" * 78)


def cmd_read(args, DM_CAN, sdk_dir: Path):
    print(f"SDK: {sdk_dir}/DM_CAN.py")

    if args.dry_run:
        print("\n[1] 参数读取帧长这样（0x7FF 广播，D[2]=0x33 表示『读』）：")
        for rid, name, _ in READ_REGISTERS[:3]:
            data = bytes([args.id & 0xFF, 0, 0x33, rid & 0xFF, 0, 0, 0, 0])
            explain_tx(build_tx(DM_CAN, 0x7FF, data), f"读 {name} (RID={rid})")
        print("\n[2] 对应的反馈帧：参数回读是 CMD=0x11 + D[2]=0x33，状态反馈是 CMD=0x11 + "
              "D[2]=0xCC。两者字段布局一样，只有 D 的内容不同。")
        print("[3] 下面这些只能接上硬件才知道（本脚本测不出来）：")
        for _, name, note in READ_REGISTERS:
            print(f"      ? {name:<12} {note}")
        print("\nDRY-RUN 结束。接上硬件后去掉 --dry-run 重跑。")
        return 0

    ser = open_port(args.port, args.timeout)
    try:
        motor = make_motor(args, DM_CAN)
        ctrl = DM_CAN.MotorControl(ser)
        ctrl.addMotor(motor)

        # [1] 逐个读寄存器
        print("\n[1] 读寄存器（只读，不写）")
        print(f"    {pad('寄存器', 14)}{pad('值', 14, True)}  {pad('尝试', 4)}说明")
        values: dict[int, float] = {}
        last_tx = None
        for rid, name, note in READ_REGISTERS:
            val, tries = read_param(ctrl, motor, rid)
            if val is None:
                print(f"    {pad(name, 14)}{pad('读不到', 14, True)}  {pad(tries, 4)}{note}")
                continue
            values[rid] = val
            shown = f"{val:.6g}"
            flag = "" if tries == 1 else f"  ← 重试了 {tries-1} 次"
            print(f"    {pad(name, 14)}{pad(shown, 14, True)}  {pad(tries, 4)}{note}{flag}")
            if rid == 20 and last_tx is None:
                last_tx = bytes(tx_template(DM_CAN))

        if last_tx:
            print("\n[2] 上面这次读操作真正发出去的 30 字节帧")
            explain_tx(last_tx, "读 0x14 Gr（RID=20=0x14）")

        # [3] 用回读值校准反馈解码范围 —— 这一步直接影响后面所有力矩读数
        print("\n[3] 用 0x15/0x16/0x17 回读值覆盖 SDK 硬编码映射表")
        p_max, v_max, t_max = 12.5, 30.0, 10.0
        sdk_default = list(DM_CAN.MotorControl.Limit_Param[int(motor.MotorType)])
        got = all(r in values for r in (21, 22, 23))
        if got:
            p_max, v_max, t_max = values[21], values[22], values[23]
            DM_CAN.MotorControl.Limit_Param[int(motor.MotorType)] = [p_max, v_max, t_max]
            print(f"    SDK 表里原本是 {sdk_default} → 现改为 [{p_max:g}, {v_max:g}, {t_max:g}]")
            if abs(t_max - sdk_default[2]) > 0.01:
                print(f"    ⚠ 扭矩映射范围与 SDK 硬编码值差 {abs(t_max-sdk_default[2]):g} N·m。"
                      "这就是 design.md §D2 说的坑：不改的话所有力矩反馈都是错的")
        else:
            print(f"    ⚠ 0x15/0x16/0x17 没读全（读到 {sorted(r for r in values if r in (21,22,23))}），"
                  f"继续用 SDK 默认值 {sdk_default} —— 力矩读数可能不准")
        limit = (p_max, v_max, t_max)

        # [4] 状态反馈 + 温度
        print("\n[4] 状态反馈帧（发 0x7FF 刷新帧，自己解析回来的 16 字节）")
        frames, buf, refresh_data = refresh_and_read(ser, DM_CAN, args.id, limit)
        explain_tx(build_tx(DM_CAN, 0x7FF, refresh_data),
                   "刷新状态（0x7FF 广播，D[2]=0xCC）")
        if frames:
            explain_rx(frames[0], limit)
        else:
            print(f"  RX（{len(buf)}B）{buf.hex(' ') if buf else '（一个字节都没回来）'}")
            print("     ⚠ 没收到反馈帧。可能：电机没上电 / 电机ID 不是 "
                  f"0x{args.id:02X} / 波特率不是 921600 / 适配器没转发")

        # [5] 结论
        print("\n[5] 结论")
        if 20 in values:
            gr = values[20]
            want = 10 if "4310" in args.type else 40
            ok = abs(gr - want) < 0.5
            print(f"    {'✓' if ok else '✗'} 0x14 Gr = {gr:g}（{args.type} 应为 {want}）"
                  + ("" if ok else "  ← 型号填错了，或电机不是这个型号"))
        if 10 in values:
            mode = int(values[10])
            names = {0: "MIT(旧)", 1: "MIT", 2: "POS_VEL", 3: "VEL", 4: "力位混控"}
            pos_vel = mode == 2
            print(f"    {'✓' if pos_vel else '·'} 0x0A CTRL_MODE = {mode}"
                  f"（{names.get(mode, '?')}）"
                  + ("" if pos_vel else
                     "  ← 只影响 POS_VEL 路径（jog / monitor / reBot 原版控制律）；"
                     "jog --mit 任何模式都能跑，因为 MIT 是上位机自己闭环、不发模式帧"))
        if 9 in values:
            t = int(values[9])
            print(f"    {'✓' if t > 0 else '✗'} 0x09 TIMEOUT = {t}"
                  + ("  ← 是 0，主机崩溃时电机不会自己停，建议设个非 0 值" if t == 0 else ""))
        if 7 in values:
            # 真正的判据是「寄存器值 vs 线上实际出现的 CAN ID」是否一致，不是拿它去
            # 对某个约定值 —— 达妙出厂默认就是 0，实测反馈 CAN ID 也是 0x000。
            seen = (frames[0][3] | (frames[0][4] << 8)) if frames else None
            v = int(values[7])
            if seen is None:
                print(f"    · 0x07 MST_ID = {v}（没有反馈帧，无法与线上值对照）")
            else:
                ok = v == seen
                print(f"    {'✓' if ok else '✗'} 0x07 MST_ID = {v}，"
                      f"线上反馈 CAN ID = 0x{seen:03X}" + ("（一致）" if ok else "  ← 不一致！"))
        print(f"    {'✓' if frames else '✗'} 反馈链路：{'通' if frames else '不通'}")
        return 0
    finally:
        ser.close()


def cmd_monitor(args, DM_CAN, sdk_dir: Path):
    if args.dry_run:
        print("\nDRY-RUN：monitor 只是按 --hz 循环发 0x7FF 刷新帧并解析反馈，无需预演帧格式。")
        print("真正能测出来的事（必须接硬件）：")
        print("  · 反馈帧能否稳定按 --hz 到达、丢帧率多少")
        print("  · D[6]/D[7] 温度读数是否合理（≈室温起步，堵转时上升）")
        print("  · ERR 是否常驻 1（使能）或 0（失能），有没有 8~E 的告警")
        return 0

    ser = open_port(args.port, args.timeout)
    try:
        limit = (12.5, 30.0, 10.0)
        print(f"\n按 {args.hz:g} Hz 刷新，共 {args.duration:g} 秒"
              f"（Ctrl-C 随时停）。用 SDK 默认映射范围 {limit} 解码；"
              "想用实机 0x15/0x16/0x17 值请先跑 `read`。\n")
        print(f"    {pad('t(s)', 7)}{pad('pos(rad)', 10, True)}{pad('vel(rad/s)', 11, True)}"
              f"{pad('tau(N·m)', 10, True)}{pad('T_MOS', 7, True)}{pad('T_Rotor', 9, True)}"
              "  ERR")
        t0 = time.monotonic()
        n_sent = n_recv = 0
        period = 1.0 / args.hz
        # 采样窗口给到 1/3 周期：太短会凭空丢帧，太长会把实际速率拖到 --hz 以下
        wait = max(0.005, period / 3)
        next_tick = t0
        try:
            while True:
                now = time.monotonic()
                if args.duration > 0 and now - t0 >= args.duration:
                    break
                if now < next_tick:
                    time.sleep(min(next_tick - now, 0.005))
                    continue
                next_tick = now + period
                frames, _, _ = refresh_and_read(ser, DM_CAN, args.id, limit, wait=wait)
                n_sent += 1
                if frames:
                    n_recv += 1
                    d = decode_feedback(frames[0][7:15], limit)
                    print(f"    {now-t0:7.2f}{d['pos']:10.4f}{d['vel']:11.4f}"
                          f"{d['tau']:10.4f}{d['t_mos_raw']:7d}{d['t_rotor_raw']:9d}"
                          f"  {d['err']}({d['err_text']})")
        except KeyboardInterrupt:
            print("\n    （Ctrl-C 停止）")
        dt = time.monotonic() - t0
        print(f"\n合计：发刷新帧 {n_sent} 次，收到反馈 {n_recv} 次"
              f"（丢帧率 {100*(n_sent-n_recv)/max(n_sent,1):.1f}%），用时 {dt:.1f}s"
              f" → 实测 {n_recv/dt:.1f} 反馈/s（目标 {args.hz:g}）")
        return 0
    finally:
        ser.close()


def make_motor(args, DM_CAN):
    try:
        mt = getattr(DM_CAN.DM_Motor_Type, args.type)
    except AttributeError:
        names = [m.name for m in DM_CAN.DM_Motor_Type]
        die(f"没有电机类型 {args.type}。可选：{names}\n"
            "  注意：枚举里**没有 4340P**，用 DM4340 代替，并以 0x15/0x16/0x17 回读值为准")
    return DM_CAN.Motor(mt, args.id, args.fb_id)


def cmd_jog(args, DM_CAN, sdk_dir: Path):
    """单电机低速点动。默认走厂商 API `control_Pos_Vel`——先证明厂商路径能用。"""
    period = 1.0 / args.hz
    cycle_s = args.cycles / args.sine_hz

    print(f"\n轨迹：pos(t) = p0 + {args.amp:g}·sin(2π·{args.sine_hz:g}·t)，"
          f"发帧 {args.hz:g} Hz，vlim={args.vlim:g} rad/s，"
          f"{args.cycles:g} 个周期 ≈ {cycle_s:.1f}s")
    print("  正弦从 p0 出发、也回到 p0，所以起停都没有阶跃。")
    peak_v = args.amp * 2 * math.pi * args.sine_hz
    if peak_v > args.vlim:
        print(f"  ⚠ 峰值速度 amp·2π·f = {peak_v:.2f} rad/s > vlim {args.vlim:g}"
              f" → vlim 会把幅度削到约 {args.vlim/(2*math.pi*args.sine_hz):.3f} rad。"
              "嫌小就加大 --vlim 或降低 --sine-hz")
    if args.tau_abort > 0:
        print(f"  急停条件：|tau| > {args.tau_abort:g} N·m，或 ERR != 1（使能）")

    if args.dry_run:
        print("\nDRY-RUN 前 3 帧长这样（p0 用 0.0 代替）：")
        for k in range(3):
            t = k * period
            p = 0.0 + args.amp * math.sin(2 * math.pi * args.sine_hz * t)
            data = struct.pack("<ff", p, args.vlim)
            can_id = 0x100 + args.id
            explain_tx(
                build_tx(DM_CAN, can_id, data),
                f"POS_VEL 控制帧 CAN ID=0x{can_id:03x}，D[0:4]=float32 P_des={p:.4f}，"
                f"D[4:8]=float32 V_des={args.vlim:g}",
            )
        print("\n注意 D[0:4]/D[4:8] 是 **float32 小端**，不是 12 位定点 —— "
              "这是 POS_VEL 与 MIT 模式最大的区别。")
        print("\nDRY-RUN 结束。真机会：enable → 读当前位置当 p0 → 跑正弦 → 回 p0 → disable。")
        return 0

    if not args.yes:
        print("\n[未执行] jog 会让电机通电出力。确认安全（电机固定好、周围无人、"
              "机械限位清楚、能随时断电）后，加 --yes 重跑：\n"
              f"    ... jog --yes --amp {args.amp:g}\n")
        return 2

    ser = open_port(args.port, args.timeout)
    enabled = False
    motor = make_motor(args, DM_CAN)
    ctrl = DM_CAN.MotorControl(ser)
    ctrl.addMotor(motor)
    p0 = 0.0
    try:
        limit = (12.5, 30.0, 10.0)
        # [1] 先读一次现状（此时还没使能，电机是松的）
        frames, _, _ = refresh_and_read(ser, DM_CAN, args.id, limit)
        if frames:
            d = decode_feedback(frames[0][7:15], limit)
            p0 = d["pos"]
            print(f"\n[1] 使能前状态：pos={p0: .4f} rad  ERR={d['err']}({d['err_text']})"
                  f"  T_MOS={d['t_mos_raw']}℃  T_Rotor={d['t_rotor_raw']}℃")
            print("    （这两条温度是趁使能前用自己的解帧读的；使能后走厂商 API "
                  "就看不到了，见下面说明）")
            if d["err"] not in (0x0, 0x1):
                die(f"电机报错 {d['err']}（{d['err_text']}），先排查再点动")
        else:
            die("读不到反馈，不能点动（不知道当前位置就没法保证不跳变）")

        # [2] 使能
        print("[2] enable ...")
        ctrl.enable(motor)
        enabled = True
        time.sleep(0.2)

        # [3] 正弦点动
        print(f"[3] 点动（从 p0={p0: .4f} 出发）")
        print("    表里的 pos/tau/err 是 SDK 的缓存值 —— control_Pos_Vel 内部的 recv() "
              "已刷新过。")
        print("    温度不在这张表里：SDK 的 recv_data 只收 q/dq/tau/err，把 D[6]/D[7] 丢了。"
              "看温度用 `monitor`（那条路自己解帧）。")
        print(f"    {pad('t(s)', 7)}{pad('命令(rad)', 11, True)}{pad('实测(rad)', 11, True)}"
              f"{pad('误差', 10, True)}{pad('tau(N·m)', 10, True)}  状态")
        t0 = time.monotonic()
        shown = 0
        next_print = 0.0
        n_loop = 0
        try:
            while True:
                t = time.monotonic() - t0
                if t >= cycle_s:
                    break
                p_cmd = p0 + args.amp * math.sin(2 * math.pi * args.sine_hz * t)
                tic = time.monotonic()
                # 厂商 API：内含 sleep(0.001) + recv()，所以速率上限 ~1kHz。
                # 这里刻意不自己构帧 —— 先证明厂商这条路本身是通的，
                # 自己那套非阻塞收发是 MotorBus 的事（design.md §4.2）。
                ctrl.control_Pos_Vel(motor, p_cmd, args.vlim)
                n_loop += 1
                if shown < 3:
                    explain_tx(
                        bytes(tx_template(DM_CAN)),
                        f"POS_VEL：CAN ID=0x{0x100+args.id:03x}  P_des={p_cmd:.4f}"
                        f"  V_des={args.vlim:g}",
                    )
                    shown += 1

                err = motor.getError()
                tau = motor.getTorque()
                if t >= next_print:
                    next_print = t + 1.0 / args.print_hz
                    print(f"    {t:7.2f}{p_cmd:11.4f}{motor.getPosition():11.4f}"
                          f"{p_cmd-motor.getPosition():10.4f}{tau:10.4f}  "
                          f"{err}({ERR_DECODE.get(err, '?')})")
                if err not in (0x1, 0x0):
                    print(f"\n    ⚠ ERR={err}({ERR_DECODE.get(err,'?')})，立即停止并失能")
                    break
                if args.tau_abort > 0 and abs(tau) > args.tau_abort:
                    print(f"\n    ⚠ |tau|={abs(tau):.3f} > {args.tau_abort:g} N·m，"
                          "立即停止并失能")
                    break
                dt = time.monotonic() - tic
                if dt < period:
                    time.sleep(period - dt)
        except KeyboardInterrupt:
            print("\n    （Ctrl-C：回 p0 后失能）")
        print(f"    共发 {n_loop} 帧，用时 {time.monotonic()-t0:.2f}s"
              f" → 实测 {n_loop/max(time.monotonic()-t0, 1e-9):.1f} Hz（目标 {args.hz:g}）")

        # [4] 回 p0
        print(f"[4] 回 p0={p0: .4f} ...")
        for _ in range(int(args.hz * 0.5)):
            ctrl.control_Pos_Vel(motor, p0, args.vlim)
            time.sleep(period)
        return 0
    finally:
        # 无论正常结束、报错还是 Ctrl-C，都回到起点并失能
        if enabled:
            try:
                for _ in range(5):
                    ctrl.control_Pos_Vel(motor, p0, args.vlim)
                    time.sleep(0.02)
                ctrl.disable(motor)
                print("[5] disable 完成")
            except Exception as e:  # 串口都断了也要把话说清楚
                print(f"[5] ⚠ 失能时出错：{e} —— 手边有急停/断电开关吗？请直接断电")
        ser.close()


def cmd_jog_mit(args, DM_CAN, sdk_dir: Path):
    """MIT 模式阶梯测试：**零寄存器写入**，所以电机停在哪个模式都能跑。

    为什么要阶梯（而不是直接发正弦）：MIT 是**上位机自己闭环**，力矩 = kp·(q_des−q)。
    所以力矩大小完全由我发出去的 kp 决定 —— 那就一档一档加，每档都先确认上一档干净：

        阶段A  kp=0, kd=kd   → 零刚度，只有阻尼。通电但**物理上不可能主动转**
        阶段B  kp=--kp, kd   → 原地保持当前位置，能感觉到"变硬了"
        阶段C  正弦 q_des     → 真正的点动

    阶梯的意义在于：一旦电机有任何异常（ERR 跳变、力矩超阈），是在**最弱的那一档**
    就暴露出来的，而不是在满幅正弦中途。
    本函数走**自己构帧 + 自己解帧**（不是厂商 API）——和 design.md D2 要落地的那条路
    一致，而且因为「发一帧必回一帧反馈」（本次实测），能顺便拿到温度。厂商那套
    `control_Pos_Vel` 的验证留给 POS_VEL 模式的 `jog`。
    """
    if args.dry_run:
        print(f"\nMIT 帧长这样（CAN ID = 电机ID 本身 = 0x{args.id:02X}，不是 0x100+ID）：")
        for kp, kd, tag in [(0.0, args.kd, "阶段A 零刚度"), (args.kp, args.kd, "阶段B 原地保持")]:
            explain_tx(
                mit_frame(DM_CAN, args.id, 0.1, 0.0, kp, kd, 0.0, (12.5, 30.0, 10.0)),
                f"{tag}：kp={kp:g} kd={kd:g} q_des=0.1 v_des=0 t_ff=0",
            )
        print("\n注意 Kp/Kd 是线性映射：[0,500]→[0,4095]、[0,5]→[0,4095]。")
        print(f"  例：kp={args.kp:g} → {int(DM_CAN.float_to_uint(args.kp, 0, 500, 12))}"
              f"   kd={args.kd:g} → {int(DM_CAN.float_to_uint(args.kd, 0, 5, 12))}")
        print("\n和 POS_VEL 的对比（同一个 CAN ID 上两种完全不同的帧）：")
        explain_tx(build_tx(DM_CAN, 0x100 + args.id, struct.pack("<ff", 0.1, 0.5)),
                   "POS_VEL：0x100+ID，D[0:8] = float32 P_des + float32 V_des")
        print("\nDRY-RUN 结束。真机会：enable → A 零刚度 → B 原地保持 → C 正弦 → kp 降 0 → disable")
        return 0

    if not args.yes:
        print("\n[未执行] MIT 点动会让电机通电出力。确认安全（裸电机固定好/周围无人/"
              "随时能断电）后加 --yes：\n"
              f"    ... jog --mit --yes --kp {args.kp:g}\n")
        return 2

    ser = open_port(args.port, args.timeout)
    motor = make_motor(args, DM_CAN)
    ctrl = DM_CAN.MotorControl(ser)
    ctrl.addMotor(motor)
    enabled = False
    p0 = 0.0
    limit = (12.5, 30.0, 10.0)
    rx = RxBuf()

    def send(q_des, kp, kd=args.kd, t_ff=0.0, v_des=0.0):
        """发一帧 MIT，阻塞等它**自己**那条应答，解回反馈。

        1:1 规律（任何帧送达电机必回一帧）让这里能同步收发：先 flush 掉残留，
        写完等最多 args.timeout。**注意不能写成 `ser.write(); ser.read_all()`**
        —— read_all 非阻塞，反馈还没回来就返回空，会被误判成"收不到反馈"。
        """
        flush_rx(ser, rx)
        ser.write(mit_frame(DM_CAN, args.id, q_des, v_des, kp, kd, t_ff, limit))
        fb = read_frames(ser, rx, want=1, timeout=args.timeout)
        return decode_feedback(fb[-1][7:15], limit) if fb else None

    try:
        # [0] 使能前：读真实映射范围（只读）+ 当前位置 + 温度
        # MIT 帧的 p/v/t 全按这三个范围做线性映射，用错范围 = 命令值全错，所以先读实机值
        dims = list(limit)
        for i, (rid, name) in enumerate(((21, "PMAX"), (22, "VMAX"), (23, "TMAX"))):
            val, tries = read_param(ctrl, motor, rid, attempts=2)
            if val:
                dims[i] = float(val)
            else:
                print(f"    ⚠ 0x{rid:02X} {name} 读不到，沿用 SDK 默认值 {dims[i]:g}")
        limit = tuple(dims)
        print(f"\n[0] 实机映射范围 PMAX/VMAX/TMAX = {limit}")

        frames, _, _ = refresh_and_read(ser, DM_CAN, args.id, limit)
        if not frames:
            die("读不到反馈，不能使能（不知道当前位置就没法保证不跳变）")
        d0 = decode_feedback(frames[0][7:15], limit)
        p0 = d0["pos"]
        print(f"    使能前：pos={p0: .4f} rad  ERR={d0['err']}({d0['err_text']})"
              f"  T_MOS={d0['t_mos_raw']}℃  T_Rotor={d0['t_rotor_raw']}℃")
        if d0["err"] not in (0x0, 0x1):
            die(f"电机报错 {d0['err']}（{d0['err_text']}），先排查")

        # [1] 使能 + 立刻一帧零力矩（把"使能后到第一条命令之间"的窗口压到最小）
        print("[1] enable 并立刻补一帧 kp=0/kd=0/t_ff=0（零力矩）...")
        ctrl.enable(motor)
        enabled = True
        st = send(p0, 0.0, 0.0)
        time.sleep(0.1)
        if st:
            print(f"    使能后：pos={st['pos']: .4f} rad（相对 p0 偏移 "
                  f"{st['pos']-p0:+.5f}）  ERR={st['err']}({st['err_text']})")
            if abs(st["pos"] - p0) > args.jump_abort:
                print(f"    ⚠ 位置跳变超过 {args.jump_abort:g} rad —— 立即失能退出")
                return 1

        t_start = time.monotonic()
        period = 1.0 / args.hz

        def stage(label, secs, q_of_t, kp, note=""):
            """跑一个阶段，返回 True 表示正常结束。q_of_t(t) → q_des"""
            nonlocal t_start
            print(f"\n{label}（{secs:g}s，kp={kp:g} kd={args.kd:g}）{note}")
            print(f"    {pad('t(s)', 7)}{pad('q_des', 9, True)}{pad('实测', 9, True)}"
                  f"{pad('误差', 9, True)}{pad('tau', 9, True)}{pad('T_rotor', 9, True)}  状态")
            t0 = time.monotonic()
            n = 0        # 实际发帧数（含丢帧后的重发）
            n_miss = 0   # 首帧就收不到反馈的次数
            next_print = 0.0
            series: list[tuple[float, float, float]] = []   # (t, q_des, q_meas)
            tau_max = 0.0
            while True:
                t = time.monotonic() - t0
                if t >= secs:
                    break
                tick = time.monotonic()
                st = send(q_of_t(t), kp)
                n += 1
                if st is None:
                    n_miss += 1
                    for _ in range(args.miss_tol):
                        st = send(q_of_t(t), kp)
                        n += 1
                        if st is not None:
                            break
                    if st is None:
                        print(f"    ⚠ 连续 {args.miss_tol + 1} 次收不到反馈"
                              "（链路断了 / 电机掉线），立即停止")
                        return False
                if t >= next_print:
                    next_print = t + 1.0 / args.print_hz
                    series.append((t, q_of_t(t), st["pos"]))
                    tau_max = max(tau_max, abs(st["tau"]))
                    print(f"    {t:7.2f}{q_of_t(t):9.4f}{st['pos']:9.4f}"
                          f"{q_of_t(t)-st['pos']:9.4f}{st['tau']:9.4f}"
                          f"{st['t_rotor_raw']:9d}  {st['err']}({st['err_text']})")
                if st["err"] not in (0x1, 0x0):
                    print(f"    ⚠ ERR={st['err']}({st['err_text']})，立即停止")
                    return False
                if args.tau_abort > 0 and abs(st["tau"]) > args.tau_abort:
                    print(f"    ⚠ |tau|={abs(st['tau']):.3f} > {args.tau_abort:g} N·m，"
                          "立即停止")
                    return False
                dt = time.monotonic() - tick
                if dt < period:
                    time.sleep(period - dt)
            el = max(time.monotonic() - t0, 1e-9)
            print(f"    发 {n} 帧，用时 {el:.2f}s → 实测 {n/el:.0f} Hz"
                  + (f"；丢帧后重发 {n_miss} 次" if n_miss else "；零丢帧"))
            if len(series) >= 4:
                def peak_of(idx):
                    vals = [s[idx] for s in series]
                    lo, hi = min(vals), max(vals)
                    return lo, hi, hi - lo

                q_lo, q_hi, pp_q = peak_of(1)
                m_lo, m_hi, pp_m = peak_of(2)
                if pp_q > 1e-9:
                    print(f"    跟踪质量：指令峰峰 {pp_q:.4f} rad → 实测峰峰 {pp_m:.4f} rad"
                          f"（幅值 {100*pp_m/pp_q:.0f}%）")
                    # 相位滞后用"第一次到达自身 95% 峰高"的时刻之差，不能用全局峰值点：
                    # 正弦是周期的，全局 argmax 随便落在哪个周期都行 → 会报出 4s 这种混叠值。
                    T = 1.0 / args.sine_hz
                    def t95(idx, lo, hi):
                        th = lo + 0.95 * (hi - lo)
                        return next((s[0] for s in series if s[idx] >= th), None)
                    tq, tm = t95(1, q_lo, q_hi), t95(2, m_lo, m_hi)
                    if tq is not None and tm is not None:
                        lag = (tm - tq) % T
                        print(f"              相位滞后 {lag:.3f}s "
                              f"= {360*lag/T:.0f}°（95% 上升沿之差，对周期取模）")
                    # 峰值力矩在多个 kp 下都是同一个值（实测 0.144~0.154），说明它
                    # 不是"kp 出多大力"而是"轴本身有多粘" —— 匀速段几乎没有惯性和加速度，
                    # 上位机出的力矩基本都花在克服摩擦上。所以这个数就是**该轴静摩擦**，
                    # 是后面重力补偿/整臂标定要用的真值。别用"平均跟随误差×kp"去反推，
                    # 那个量里混了动态滞后误差，会随 kp 变大而变大，不是常数。
                    print(f"              峰值力矩 {tau_max:.4f} N·m（kp={kp:g}，"
                          f"满量程 TMAX={limit[2]:g} N·m 的 {100*tau_max/limit[2]:.1f}%）"
                          "  ← 换几档 kp 都稳定在同一值，即该轴静摩擦")
                    if pp_m / pp_q < 0.7 and kp > 0:
                        # 用实测力矩反推要多大 kp：死区宽度 = 摩擦/kp，要让它明显小于
                        # 振幅，就要 kp ≳ 摩擦/振幅。取 3 倍摩擦作为目标（留出余量）。
                        # tau_max 在这里是**下界**（没跟紧时力矩还没到摩擦值），所以推出来
                        # 的 kp 也偏小，宁可再往上加一档。
                        ampl = 0.5 * pp_q if pp_q > 1e-9 else 1.0
                        need = 3.0 * tau_max / ampl
                        print(f"    ⚠ 幅值只有 {100*pp_m/pp_q:.0f}% → kp={kp:g} 太小"
                              f"（满量程 500 的 {100*kp/500:.1f}%），力矩大半被静摩擦吃掉。"
                              f"按实测力矩反推，kp 需要 ≳ {max(need, kp*2, 5):.0f} 才压得住死区"
                              f"（死区 ≈ 摩擦/kp；这里摩擦只能算到 {tau_max:.3f} N·m 的下界）")
            return True

        # [2] 阶段A：零刚度，只有阻尼
        if not stage("[2] 阶段A：kp=0（零刚度，只有阻尼）", args.t_a,
                     lambda t: p0, 0.0,
                     "→ 通电但无力矩，你用手转它应该是轻的、转完不会自己回去"):
            return 1

        # [3] 阶段B：原地保持
        if not stage("[3] 阶段B：原地保持当前位置", args.t_b,
                     lambda t: p0, args.kp,
                     f"→ 会明显'变硬'，试图转它会被推回 p0={p0: .4f}"):
            return 1

        # [4] 阶段C：正弦
        ampl = min(args.amp, 1.0)
        if not stage(f"[4] 阶段C：正弦 ±{ampl:g} rad @ {args.sine_hz:g}Hz",
                     args.cycles / args.sine_hz,
                     lambda t: p0 + ampl * math.sin(2 * math.pi * args.sine_hz * t), args.kp,
                     f"→ 应该看到 {args.cycles:g} 个来回"):
            return 1

        # [5] 停回 p0 再卸力（不要让它在偏离位置时突然失去刚度）
        print(f"\n[5] 回 p0={p0: .4f} 并卸力")
        for _ in range(int(args.hz * 0.6)):
            send(p0, args.kp)
            time.sleep(period)
        for _ in range(int(args.hz * 0.2)):
            send(p0, 0.0, 0.0)
            time.sleep(period)
        st = send(p0, 0.0, 0.0)
        if st:
            off = st["pos"] - p0
            print(f"    末态：pos={st['pos']: .4f}（相对 p0 {off:+.5f} rad）"
                  f"  T_rotor={st['t_rotor_raw']}℃")
            # 真的回去了吗？kp 太小是推不动摩擦的（静止摩擦要恒力矩克服），
            # 所以这里必须验，不能只喊一声"回 p0"就完事。
            if abs(off) > 0.01:
                print(f"    ⚠ 没回到 p0（差 {abs(off):.4f} rad）。kp={args.kp:g} 在"
                      f"偏差 {abs(off):.3f} rad 处只有 {args.kp*abs(off):.3f} N·m"
                      "，推不动静摩擦 —— 这是 kp 太小的典型症状，不是回零逻辑坏了。"
                      "裸电机上无害；装到臂上会留下残余偏角，务必先加大 kp。")
            else:
                print(f"    ✓ 已回到 p0（残差 {abs(off)*1000:.2f} mrad）")
        print(f"\n总耗时 {time.monotonic()-t_start:.1f}s")
        return 0
    finally:
        if enabled:
            try:
                for _ in range(3):
                    send(p0, 0.0, 0.0)  # 先卸力
                    time.sleep(0.02)
                ctrl.disable(motor)
                print("[6] disable 完成")
            except Exception as e:
                print(f"[6] ⚠ 失能时出错：{e} —— 请直接断电")
        ser.close()


def cmd_bandwidth(args, DM_CAN, sdk_dir: Path):
    """链路吞吐实测。解答 design.md 里那个悬而未决的问题：

    TX 30B / RX 16B，6 关节 500Hz 发送 = 90,000 B/s，占 921600 8N1 链路
    （92,160 B/s）的 97.7%，加上反馈就超 100%。所以要么适配器不理会标称波特率
    （CDC-ACM 常见的"波特率是摆设"），要么 reBot 其实在丢帧。**只能实测**。

    本子命令默认**不使能电机**：纯发帧测链路，电机不会动，但总线负载与真机一致。
    加 --enable 才是完整闭环（含反馈）。
    """
    print(f"\n测试：按 {args.hz:g} Hz 连发 {args.duration:g} 秒 POS_VEL 帧"
          f"（{'含使能+闭环反馈' if args.enable else '不使能，电机不会动'}）")
    print(f"  帧长 TX={TX_FRAME_LEN}B / RX={RX_FRAME_LEN}B，链路容量 {LINK_BYTES_PER_S:.0f} B/s")
    if args.hz * TX_FRAME_LEN > LINK_BYTES_PER_S:
        print(f"  ⚠ 目标发送速率 {args.hz*TX_FRAME_LEN:.0f} B/s 本身就超过链路容量 "
              f"{LINK_BYTES_PER_S:.0f} B/s —— 必然发不出去，看实测值就对了")
    print("  参考：达妙官方 USAGE.md 说『推荐在每帧控制完后延迟 2ms 或者 1ms』"
          "→ 单电机建议上限 500~1000 Hz")

    if args.dry_run:
        print("\nDRY-RUN：帧格式就是 jog 那种（0x100+ID + float32 P/V），此处不重复。")
        print("接硬件后这个测试会给出四个数：实际速率、TX 字节/秒、RX 帧/秒、"
              "单帧写延迟(max)。")
        print("关键判据：`ser.write()` 是**阻塞**的，如果 max 写延迟随着 --hz 升高而"
              "明显变大，说明适配器/驱动的缓冲已经满了 —— 那就是超载的实锤。")
        return 0

    if args.enable and not args.yes:
        print("\n[未执行] --enable 会让电机通电保持位置。确认安全后加 --yes，"
              "或去掉 --enable 只测链路。\n")
        return 2

    ser = open_port(args.port, args.timeout)
    enabled = False
    motor = make_motor(args, DM_CAN)
    ctrl = DM_CAN.MotorControl(ser)
    ctrl.addMotor(motor)
    p_hold = 0.0
    try:
        limit = (12.5, 30.0, 10.0)
        frames, _, _ = refresh_and_read(ser, DM_CAN, args.id, limit)
        if frames:
            p_hold = decode_feedback(frames[0][7:15], limit)["pos"]
            print(f"\n[0] 保持位置 p_hold={p_hold: .4f} rad（读自实机）")
        elif args.enable:
            die("读不到反馈，不能使能（不知道当前位置）。先去掉 --enable 测链路，"
                "或先跑 `read` 排查为什么没有反馈")
        else:
            print("\n[0] 读不到反馈，用 p_hold=0.0 发帧（不使能，不影响安全）")

        if args.enable:
            print("[1] enable ...")
            ctrl.enable(motor)
            enabled = True
            time.sleep(0.2)

        print(f"[2] 发帧 {args.duration:g} 秒 ...")
        data = struct.pack("<ff", p_hold, args.vlim)
        can_id = 0x100 + args.id
        frame = build_tx(DM_CAN, can_id, data)
        period = 1.0 / args.hz
        t0 = time.monotonic()
        # 控制帧和刷新帧分开计：混在一起算会得出"超过 100%"这种假结论
        # （之前就是这么误报成 116% 的）。控制帧才是 500Hz 循环真正的开销。
        n_tx_ctl = n_tx_ref = n_rx = 0
        write_lat: list[float] = []
        rx_bytes = 0
        rx = RxBuf()
        # 反馈刷新：每 --fb-hz 发一条 0x7FF（reBot 真机是 100Hz）
        next_fb = t0
        fb_period = 1.0 / args.fb_hz
        next_tick = t0
        n_check = 0
        aborted = ""
        try:
            while True:
                now = time.monotonic()
                if now - t0 >= args.duration:
                    break
                if now < next_tick:
                    time.sleep(min(next_tick - now, 0.001))
                    continue
                next_tick = now + period
                # 注意：**不做追赶**。如果一次 write 慢到跨过了一个周期，就把下一个
                # tick 定在当前时刻（=立刻再发），这样"发不出去"会表现为实测速率低于
                # 目标，而不是靠补发掩盖掉。
                if next_tick < now:
                    next_tick = now
                w0 = time.perf_counter()
                ser.write(frame)
                write_lat.append(time.perf_counter() - w0)
                n_tx_ctl += 1
                if now >= next_fb:
                    next_fb = now + fb_period
                    ser.write(build_tx(
                        DM_CAN, 0x7FF,
                        bytes([args.id & 0xFF, 0, 0xCC, 0, 0, 0, 0, 0])))
                    n_tx_ref += 1
                # 非阻塞抽干（timeout=0）：这一圈只收"已经到了"的字节，绝不等。
                # 1:1 规律下每个控制帧本来就回一帧，所以这里收到的就是它们；抽干的
                # 结果累积在 RxBuf 里，短读不会让后续字节错位。
                raw_sink: list[bytes] = []
                fb = read_frames(ser, rx, want=0, timeout=0.0, raw_sink=raw_sink)
                if raw_sink:
                    rx_bytes += sum(len(c) for c in raw_sink)
                if fb:
                    n_rx += len(fb)
                    # 使能状态下每 0.5 秒查一次安危（design.md §9：POS_VEL 下上位机
                    # 唯一的力矩保护手段就是这种监控）
                    if args.enable:
                        n_check += 1
                        if n_check >= args.fb_hz * 0.5:
                            n_check = 0
                            d = decode_feedback(fb[-1][7:15], limit)
                            if d["err"] not in (0x1, 0x0):
                                aborted = f"ERR={d['err']}({d['err_text']})"
                            elif args.tau_abort > 0 and abs(d["tau"]) > args.tau_abort:
                                aborted = f"|tau|={abs(d['tau']):.3f} > {args.tau_abort:g}"
                            if aborted:
                                print(f"\n    ⚠ {aborted}，立即停止并失能")
                                break
        except KeyboardInterrupt:
            print("\n    （Ctrl-C 停止）")

        dt = time.monotonic() - t0
        write_lat.sort()
        p99 = write_lat[min(len(write_lat) - 1, int(len(write_lat) * 0.99))] if write_lat else 0
        n_tx = n_tx_ctl + n_tx_ref
        tx_bytes = n_tx * TX_FRAME_LEN
        print(f"\n[3] 实测结果（{dt:.2f}s）")
        print(f"    控制帧    : {n_tx_ctl:6d} 帧 = {n_tx_ctl*TX_FRAME_LEN:7d} B  → "
              f"{n_tx_ctl/dt:7.1f} 帧/s = {n_tx_ctl*TX_FRAME_LEN/dt:7.0f} B/s"
              f"  （目标 {args.hz:g} Hz，达成 {100*(n_tx_ctl/dt)/args.hz:.0f}%）")
        print(f"    刷新帧    : {n_tx_ref:6d} 帧 = {n_tx_ref*TX_FRAME_LEN:7d} B  → "
              f"{n_tx_ref/dt:7.1f} 帧/s（--fb-hz {args.fb_hz:g}）")
        print(f"    接收      : {n_rx:6d} 帧 = {rx_bytes:7d} B  → {n_rx/dt:7.1f} 帧/s"
              f" = {rx_bytes/dt:7.0f} B/s")
        print(f"    写延迟    : max {max(write_lat)*1000:.2f} ms   p99 {p99*1000:.2f} ms"
              f"   平均 {sum(write_lat)/len(write_lat)*1000:.3f} ms")
        tot = tx_bytes + rx_bytes
        print(f"    合计带宽  : {tot/dt:.0f} B/s  =  链路容量({LINK_BYTES_PER_S:.0f} B/s)"
              f" 的 {100*(tot/dt)/LINK_BYTES_PER_S:.1f}%")

        print("\n[4] 判读")
        if n_tx_ctl / dt < args.hz * 0.95:
            print(f"    ✗ 控制帧没达到目标速率（{n_tx_ctl/dt:.0f} vs {args.hz:g}）→ "
                  "USB-CAN 链路撑不住这个频率")
        else:
            print(f"    ✓ 控制帧达到了目标速率 {args.hz:g} Hz")
        # 1:1 规律 → 每发一帧就该回一帧。收得明显少 = 链路在丢，收得更多 = 有别的
        # 东西在总线上（或刷新帧也回了-都算在内，所以期望值就是总发送帧数）
        expect_rx = n_tx
        if expect_rx:
            ratio = 100 * n_rx / expect_rx
            print(f"    1:1 应答  : 收到 {n_rx} / 期望 {expect_rx} 帧 = {ratio:.1f}%"
                  f"{'  ← 正常（每帧必回一帧）' if 95 <= ratio <= 105 else '  ← 偏离 1:1，值得查'}")
        over_100 = 100 * (tot / dt) / LINK_BYTES_PER_S
        print(f"    实测带宽占容量 {over_100:.0f}%（容量按标称 921600 8N1 算）")
        if over_100 > 100:
            print("    ✓✗ **超过 100% 却跑通了、1:1 也没破** → CDC-ACM 的标称波特率是"
                  "摆设，实际远超 921600。design.md 里「111% 超载」那套算法不成立"
                  "（但**不等于**帧率没上限，真正的天花板是实测的帧率，见下）")
        # 7 关节外推。**关键前提**：只有当本次"冲不到目标"时，实测值才等于天花板；
        # 如果达标了，说明还留有余量，实测值只是你要求的那个数，不能当天花板用
        # （拿它去比 7×500 的需求会得出错误的"带不动"）。
        rate = n_tx_ctl / dt
        at_ceiling = rate < args.hz * 0.95
        if at_ceiling:
            print(f"    天花板    : 本次冲 {args.hz:g}Hz 只到 {rate:.0f} 帧/s → 这就是本机"
                  f"（单适配器 + 这个 Python 循环）的实际上限")
            need = 7 * 500
            print(f"    7 关节@500Hz 需要 {need} 控制帧/s → "
                  + ("**带不动**" if rate < need else "带得动"))
            print(f"                → 单总线 7 关节的实测上限约 {rate/7:.0f} Hz/关节"
                  "（这是**纯发帧**的账；真机每轮还要跑 7 个关节的控制律，实际更低）")
        else:
            print(f"    天花板    : 未触及（{rate:.0f} 帧/s 是你要的速率，不是上限）。"
                  f"想知道 7 关节能不能扛住 500Hz，用 --hz 4000 再压一次")
            print(f"    单关节成本: 1 个关节 @ {args.hz:g}Hz 占用 {n_tx_ctl/dt + n_rx/dt:.0f} "
                  "帧/s（含 1:1 应答）；7 关节全速需按此行 ×7 估算剩余余量")
        # 注意：**使能与否都一样**。1:1 规律是"任何帧送达电机必恰好回一帧"，跟
        # 使能状态、跟电机认不认这个帧都无关（实测：电机在 MIT 模式、且失能，
        # 收到 POS_VEL 帧照样每帧回一条）。所以 n_rx 的期望值就是控制帧+刷新帧的
        # 总数，不要按 --fb-hz 单独算反馈——那样会得出虚假的"丢帧"。
        print(f"    {'使能' if args.enable else '未使能'}：电机对本模式下不认的帧也照样应答，"
              "所以 RX 期望值 = 控制帧 + 刷新帧（见上条 1:1）")
        return 0
    finally:
        if enabled:
            try:
                for _ in range(5):
                    ctrl.control_Pos_Vel(motor, p_hold, args.vlim)
                    time.sleep(0.02)
                ctrl.disable(motor)
                print("[5] disable 完成")
            except Exception as e:
                print(f"[5] ⚠ 失能时出错：{e} —— 请直接断电")
        ser.close()


# ───────────────────────────── 5. 命令行 ─────────────────────────────
def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="dm_bringup",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        description="单电机上电验证（design.md §11.2）。默认只读；jog/bandwidth --enable 需 --yes。",
        epilog=__doc__.split("## 用法")[-1].strip() if "## 用法" in __doc__ else None,
    )
    p.add_argument("cmd", nargs="?", default="read",
                   choices=["read", "monitor", "jog", "bandwidth"],
                   help="默认 read")
    p.add_argument("--port", default="/dev/ttyACM0", help="默认 /dev/ttyACM0")
    p.add_argument("--id", type=lambda s: int(s, 0), default=0x04,
                   help="电机 CAN ID，默认 0x04（joint4，4310，力矩最小的一档）")
    p.add_argument("--fb-id", type=lambda s: int(s, 0), default=None,
                   help="反馈 ID，默认 0x10+电机ID")
    p.add_argument("--type", default="DM4310",
                   help="DM_Motor_Type 成员名，默认 DM4310。4340P 没有对应枚举，用 DM4340")
    p.add_argument("--timeout", type=float, default=0.05,
                   help="串口读超时(秒)，默认 0.05。别用示例里的 0.5，太大会拖慢控制环")
    p.add_argument("--sdk-dir", default=None, help="DM_CAN.py 所在目录（自动找，见 -h 顶部）")
    p.add_argument("--dry-run", action="store_true", help="不开串口、不发帧，只打印帧结构")
    p.add_argument("--yes", action="store_true", help="确认已做安全准备的显式开关")

    g = p.add_argument_group("monitor")
    g.add_argument("--hz", type=float, default=50.0, help="刷新/发帧频率，默认 50")
    g.add_argument("--duration", type=float, default=10.0, help="持续秒数，默认 10（0=直到 Ctrl-C）")

    g = p.add_argument_group("jog")
    g.add_argument("--amp", type=float, default=0.2, help="正弦振幅(rad)，默认 0.2")
    g.add_argument("--vlim", type=float, default=0.5, help="POS_VEL 的 V_des(rad/s)，默认 0.5")
    g.add_argument("--cycles", type=float, default=3.0, help="跑几个正弦周期，默认 3")
    g.add_argument("--tau-abort", type=float, default=2.0,
                   help="|tau| 超过就停(N·m)，默认 2.0（4310 额定 3.0）。0=不检查")
    g.add_argument("--mit", action="store_true",
                   help="用 MIT 模式点动（CAN ID=电机ID，零寄存器写入）。不加则走 POS_VEL")
    g.add_argument("--kp", type=float, default=1.0,
                   help="MIT 刚度，默认 1.0（范围 [0,500]）。1.0 配 0.3rad 误差 ≈ 0.3 N·m")
    g.add_argument("--kd", type=float, default=0.1,
                   help="MIT 阻尼，默认 0.1（范围 [0,5]）")
    g.add_argument("--t-a", type=float, default=4.0,
                   help="MIT 阶段A（零刚度）秒数，默认 4")
    g.add_argument("--t-b", type=float, default=4.0,
                   help="MIT 阶段B（原地保持）秒数，默认 4")
    g.add_argument("--miss-tol", type=int, default=3,
                   help="连续丢几帧才判定链路断开，默认 3（丢帧会先重发同一帧）")
    g.add_argument("--jump-abort", type=float, default=0.1,
                   help="使能瞬间位置跳变超过这个值(rad)就立刻退出，默认 0.1")
    g.add_argument("--sine-hz", type=float, default=0.5,
                   help="点动正弦频率(Hz)，默认 0.5（2 秒一个来回）。注意与发帧频率 --hz 是两回事")
    g.add_argument("--print-hz", type=float, default=10.0,
                   help="点动时打印行频率，默认 10（发帧频率仍是 --hz，不影响控制）")

    g = p.add_argument_group("bandwidth")
    g.add_argument("--fb-hz", type=float, default=100.0,
                   help="反馈刷新频率，默认 100（同 reBot 真机）")
    g.add_argument("--enable", action="store_true",
                   help="使能电机并保持位置（默认不使能：纯链路测试，电机不动）")
    return p


def main(argv=None) -> int:
    args = build_parser().parse_args(argv)
    args.fb_id = args.fb_id if args.fb_id is not None else 0x10 + args.id

    sdk_dir = find_sdk_dir(args.sdk_dir)
    DM_CAN = load_sdk(sdk_dir)

    if args.type not in [m.name for m in DM_CAN.DM_Motor_Type]:
        die(f"没有电机类型 {args.type}。可选：{[m.name for m in DM_CAN.DM_Motor_Type]}\n"
            "  注意：枚举里没有 4340P，用 DM4340 代替（并以 0x15/0x16/0x17 回读值为准）")

    banner(args, args.type)

    # --duration 对 jog 无意义（用 --cycles），hint 一下
    if args.cmd == "jog" and args.duration != 10.0:
        print("  提示：jog 用 --cycles 控制时长，--duration 不生效")

    if args.cmd == "jog" and args.mit:
        return cmd_jog_mit(args, DM_CAN, sdk_dir)
    return {
        "read": cmd_read,
        "monitor": cmd_monitor,
        "jog": cmd_jog,
        "bandwidth": cmd_bandwidth,
    }[args.cmd](args, DM_CAN, sdk_dir)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print("\n中断。若刚在使能状态，请确认电机已失能或直接断电。", file=sys.stderr)
        sys.exit(130)
