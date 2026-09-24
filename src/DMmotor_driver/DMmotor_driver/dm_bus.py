#!/usr/bin/env python3
"""dm_bus —— MotorBus：一条总线上的非阻塞收发（DESIGN.md D2）

## 它解决什么

7 个关节共用一条 USB-CAN。要同时控制它们，就必须**发和收解耦**：

    发：200~300 Hz 定时循环 → bus.send_pos_vel_batch({id: (pos, vlim)})   # 只写串口，不等回包
    收：100 Hz 定时循环     → bus.poll()                                   # 解析缓冲，更新状态

原来的 `dm_bringup.py` 只有"发一帧等一帧"，一次只能驱动一个 ID。

## 为什么不能直接用厂商 SDK

`control_Pos_Vel`（`DM_CAN.py:166-185`）= `__send_data` → `sleep(0.001)` → `recv()`，
而 `recv()` 是 `read_all()` + 串口 `timeout=0.5`。**7 电机 × 500Hz 直接出局**
（LESSONS.md §2.6 坑 1）。所以本层自构帧、自己 write、自己轮询收。

**本层现在不 import SDK** —— 帧的字节布局与定点映射都自持在 `dm_frames.py` 里
（逐字节对拍厂商实现，见 `tools/smoke_dm_frames.py`）。封装层不该依赖一个厂商工具包：
它的接口随版本变，而我们的字节格式不该跟着变。

## 安全边界（重要 —— 改这个文件前先读）

**MotorBus 是纯传输层。它：**

- ✅ 发控制帧、读反馈、维护状态缓存
- ❌ **不写任何寄存器**（PID / 0x09 看门狗 / 映射范围都是 `RegisterTool` 的活）
- ❌ **不设零位**（`set_zero_position` 0xFE 全项目从未被调用）
- ❌ **不自动使能** —— 它能发使能/失能帧，但**何时发由 `Joint` / `DmArm` 决定**
- ❌ 不判断安全（限位 / 力矩阈值 / 急停都在更上层）

⚠️ 三条会咬人的硬性事实（详见 ARCHITECTURE.md §六）：

1. **全仓库只有一处会写 flash** —— `dm_registers.py` 的 `save_motor_param`。这里没有。
2. **ERR=13（通讯丢失）是锁存的** —— `enable` / 连发帧 / 写 `0x09=0` 都清不掉，
   只能给电机断电再上电。所以看门狗实验每触发一次就要人工断一次电。
3. **写一次寄存器要 ~150ms 且期间发不出帧** —— 运行期绝不能做寄存器 I/O。
   本模块不做寄存器 I/O，正是为了守住这条。

## 两种收法（DESIGN.md D2 说"两者并存"）

| 方法 | 用在哪 | 代价 |
|---|---|---|
| `poll()` | 7 关节满载的常规控制循环 | **非阻塞**，只收已经到了的字节，不占循环时间 |
| `send_and_wait()` | 单路调试 / 需要逐帧归因（标定、寄存器读写） | 阻塞到 deadline，**正确性 > 吞吐**，吞吐掉一大截 |
"""
from __future__ import annotations

import sys
import threading
import time
from dataclasses import dataclass
from pathlib import Path

# 同 dm_bringup：本文件既被当模块 import，也可能被裸脚本/工具直跑，
# 所以用绝对导入 + 失败时把 src/DMmotor_driver 塞进 sys.path（详见 dm_bringup.py 的注释）。
try:
    from DMmotor_driver.dm_frames import (
        ERR_OK, CMD_ENABLE, CMD_DISABLE,
        pos_vel_frame, mit_frame, cmd_frame, refresh_frame,
        RxBuf, read_frames, flush_rx, decode_feedback,
    )
except ImportError:  # 裸脚本直跑：parents[1] 就是 src/DMmotor_driver
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
    from DMmotor_driver.dm_frames import (
        ERR_OK, CMD_ENABLE, CMD_DISABLE,
        pos_vel_frame, mit_frame, cmd_frame, refresh_frame,
        RxBuf, read_frames, flush_rx, decode_feedback,
    )

# 串口超时（秒）。**必须小**：这是 D2 能成立的前提。
# `timeout=0.5` 会让每次读阻塞 0.5s，控制循环直接废掉。
# 3ms 是 DESIGN.md §八「开串口(timeout=3ms)」的值 —— 够一帧往返（921600 下 0.17ms/帧），
# 又短到不会毁掉循环节拍。
DEFAULT_TIMEOUT = 0.003


@dataclass
class MotorState:
    """**电机侧**的一帧状态快照。**未经 dir/offset 换算。**

    这是"电机报了什么"，不是"关节在哪"。换算（`direction` / `offset`）和软限位钳位
    是 `Joint` 的事，产出的是另一个类型 `JointState`（DESIGN.md §4.1）—— 两者别合并。

    注：`JointState` 这个名字与 `sensor_msgs.msg.JointState` 同名但无关，ROS 层 import 时留意。
    """
    motor_id: int
    err: int
    err_text: str
    pos: float          # 电机侧 rad（输出轴）
    vel: float          # 电机侧 rad/s
    tau: float          # N·m
    temp_mos: int       # ℃，来自反馈帧 D[6]（SDK 把它丢了，我们自己解）
    temp_rotor: int     # ℃，来自反馈帧 D[7]
    timestamp: float    # time.monotonic()

    @property
    def enabled(self) -> bool:
        """ERR == 1 表示使能。注意 ERR == 0 是**失能**，不是"正常"。"""
        return self.err == 0x1

    @property
    def faulted(self) -> bool:
        """ERR 不在 {0(失能), 1(使能)} 就是故障，且电机此时已自行退出使能。"""
        return self.err not in ERR_OK

    @classmethod
    def from_decoded(cls, d: dict, ts: float) -> "MotorState":
        return cls(
            motor_id=d["id"], err=d["err"], err_text=d["err_text"],
            pos=d["pos"], vel=d["vel"], tau=d["tau"],
            temp_mos=d["t_mos_raw"], temp_rotor=d["t_rotor_raw"],
            timestamp=ts,
        )


class MotorBus:
    """一条总线。所有电机共用同一个实例。

    **每个电机必须先 `add_motor()` 注册自己的映射范围**，否则发送/解码一律报错 ——
    这不是形式主义：4310 与 4340P 的 PMAX/VMAX/TMAX 档位不同，**用错档位解出来的
    力矩差 4 倍**（DESIGN.md D5），不注册就没有机会发现这件事。
    """

    def __init__(self, port: str = "/dev/ttyACM0", *,
                 baud: int = 921600, timeout: float = DEFAULT_TIMEOUT):
        self.port = port
        self.baud = baud
        self.timeout = timeout

        self.ser = None
        self.rx = RxBuf()
        self._limits: dict[int, tuple[float, float, float]] = {}
        self._types: dict[int, str] = {}
        self._cache: dict[int, MotorState] = {}
        # 保护 write 与 poll 的配对。D2 的 500Hz 发 / 100Hz 收是两个独立循环，
        # 分开跑线程是文档里的形态；一个锁的成本极低，不留这个坑。
        self._lock = threading.RLock()

        # 计数（给 1:1 校验与链路质量用）。控制帧与广播刷新帧**必须分开数** ——
        # 混在一起会得出"回帧比发帧还多"的假象（cmd_bandwidth 里踩过）。
        self.n_sent = 0
        self.n_sent_broadcast = 0
        self.n_recv = 0
        self.rx_bytes = 0
        self.write_lat: list[float] = []
        self.unknown_ids: set[int] = set()

    # ───────────────────────── 生命周期 ─────────────────────────
    @classmethod
    def connect(cls, port: str = "/dev/ttyACM0", **kw) -> "MotorBus":
        """建好并立刻开串口。"""
        bus = cls(port, **kw)
        bus.open()
        return bus

    def open(self, port: str | None = None, baud: int | None = None,
             timeout: float | None = None) -> "MotorBus":
        """开串口。**不改任何电机侧配置**（不使能、不写寄存器、不切模式）。"""
        import serial

        if port is not None:
            self.port = port
        if baud is not None:
            self.baud = baud
        if timeout is not None:
            self.timeout = timeout

        if not Path(self.port).exists():
            import glob
            found = sorted(glob.glob("/dev/ttyACM*") + glob.glob("/dev/ttyUSB*"))
            raise FileNotFoundError(
                f"{self.port} 不存在。当前可见串口：{found or '（一个都没有）'}\n"
                "  · 适配器插好了吗？`lsusb` 能看到吗？\n"
                "  · 权限：`ls -l " + self.port + "`；需要时 `sudo usermod -aG dialout $USER` 后重新登录\n"
                "  · 被占用：`fuser -v " + self.port + "` —— 同一时刻只能有一个程序占用总线"
            )
        try:
            self.ser = serial.Serial(self.port, self.baud, timeout=self.timeout)
        except serial.SerialException as e:
            raise OSError(f"打不开 {self.port}：{e}") from e

        self.ser.reset_input_buffer()
        self.ser.reset_output_buffer()
        self.rx = RxBuf()
        return self

    def close(self) -> None:
        """关串口。**不负责失能电机** —— 那是 `Joint`/`DmArm.shutdown()` 的事
        （readme 守则 5：先失能/回零，再关电源）。"""
        if self.ser is not None:
            try:
                self.ser.close()
            finally:
                self.ser = None

    # ───────────────────────── 注册 ─────────────────────────
    def add_motor(self, motor_id: int, limit, motor_type: str | None = None) -> None:
        """注册一台电机。`limit` = (PMAX, VMAX, TMAX)，**必须来自 0x15/0x16/0x17 的回读值**。

        ⚠️ 不要写死常数。4310 与 4340P 的档位不同，用错档位解出来的力矩差 4 倍；
        而且万一某台的映射范围被改过，回读是唯一能发现的办法（DESIGN.md D5）。
        """
        p, v, t = (float(x) for x in limit)
        if not (p > 0 and v > 0 and t > 0):
            raise ValueError(f"电机 0x{motor_id:02X} 的映射范围必须都是正数，收到 {limit}")
        self._limits[motor_id] = (p, v, t)
        if motor_type:
            self._types[motor_id] = motor_type

    def set_limit(self, motor_id: int, limit) -> None:
        """改已注册电机的映射范围（比如上电回读之后发现和配置里写的不一样）。"""
        self.add_motor(motor_id, limit, self._types.get(motor_id))

    def motors(self) -> dict[int, tuple[float, float, float]]:
        return dict(self._limits)

    def _limit(self, motor_id: int) -> tuple[float, float, float]:
        try:
            return self._limits[motor_id]
        except KeyError:
            raise KeyError(
                f"电机 0x{motor_id:02X} 没注册。先调 add_motor(id, limit)，"
                f"limit 用 0x15/0x16/0x17 的回读值（别写死 —— 4310 与 4340P 档位不同，"
                f"用错档位力矩差 4 倍）"
            ) from None

    # ───────────────────────── 发送 ─────────────────────────
    def send_frame(self, frame: bytes) -> int:
        """**唯一的发送出口。**非阻塞：只 write，不等回包、不读串口。

        参数是**已拼好的 30 字节适配器帧**（CAN ID 已经在 `[13:15]` 里了），
        所以不再单独传 motor_id —— 传两个来源的 ID 就有不一致的机会。
        所有语义化方法（`send_pos_vel` 等）都经这里出去。
        """
        if self.ser is None:
            raise RuntimeError("串口没开，先调 open() / connect()")
        with self._lock:
            t0 = time.perf_counter()
            n = self.ser.write(frame)
            self.write_lat.append(time.perf_counter() - t0)
            self.n_sent += 1
        return n

    def send_pos_vel(self, motor_id: int, pos: float, vlim: float) -> int:
        """POS_VEL：位置 + 速度上限。**本机主用的控制律**（固件闭环，稳态误差能到 0）。

        ⚠️ 只有电机 `CTRL_MODE == 2` 时才被认。切模式是 `Joint`/`DmArm` 的活（D3），
        本层不切、也不检查。
        """
        self._limit(motor_id)          # 注册检查（也是"ID 打错"的检查）
        return self.send_frame(pos_vel_frame(motor_id, pos, vlim))

    def send_pos_vel_batch(self, targets) -> int:
        """`{motor_id: (pos, vlim)}` → 每台一帧。返回发出的帧数。

        7 关节一条命令、一次循环。**不做批量拼帧** —— 适配器帧本来就是一帧一电机，
        "批量"在这里只是省掉调用方自己循环。
        """
        n = 0
        for mid, (pos, vlim) in targets.items():
            self.send_pos_vel(mid, pos, vlim)
            n += 1
        return n

    def send_mit(self, motor_id: int, kp: float, kd: float,
                 q: float, dq: float, tau: float) -> int:
        """MIT：上位机自己做 PD + 力矩前馈。唯一能**直接给力矩**的模式（重力补偿用）。

        稳态残差有上界 ≈ 摩擦/kp，压不到 0 —— 所以常规轨迹跟踪还是用 POS_VEL
        （DESIGN.md D8 的适用边界表）。
        """
        limit = self._limit(motor_id)
        return self.send_frame(mit_frame(motor_id, q, dq, kp, kd, tau, limit))

    def send_enable(self, motor_id: int) -> int:
        """使能（0xFC）。⚠️ 只发帧，**不检查结果、不等待**。

        ⚠️ 使能之后必须**立刻**补一条保持命令（reBot 的做法），否则电机会处于
        "已使能但没命令"的窗口。这个时序由 `Joint`/`DmArm` 保证。
        """
        return self.send_frame(cmd_frame(motor_id, CMD_ENABLE))

    def send_disable(self, motor_id: int) -> int:
        """失能（0xFD）。"""
        return self.send_frame(cmd_frame(motor_id, CMD_DISABLE))

    def send_refresh(self, motor_id: int) -> int:
        """0x7FF 刷新帧：查某台电机的状态。**广播 CAN ID，但数据里带了目标 ID。**"""
        with self._lock:
            n = self.send_frame(refresh_frame(motor_id))
            # send_frame 已经 +1 到 n_sent 了，这里把它挪到广播计数去 ——
            # 否则 1:1 比值会因为"一发多回"而看起来 >100%
            self.n_sent -= 1
            self.n_sent_broadcast += 1
        return n

    # ───────────────────────── 接收 ─────────────────────────
    def poll(self) -> int:
        """**非阻塞抽干**：把"已经到了"的字节收进来、切帧、更新缓存。**绝不等待。**

        返回本轮解析出的反馈帧数（0 是正常的，不代表出错）。

        实现上就是 `read_frames(want=0, timeout=0.0)` —— `want=0` 让 deadline 判断
        立刻为真，于是只读 `in_waiting` 里现成的字节。跨圈复用同一个 `RxBuf`，
        所以**被拆成两次到达的帧不会丢**。

        没注册过的 ID 会进 `unknown_ids` 而不是解码 —— 宁可少一条数据，也不用错的
        映射范围解出一个差 4 倍的力矩。
        """
        if self.ser is None:
            raise RuntimeError("串口没开，先调 open() / connect()")
        with self._lock:
            raw_sink: list[bytes] = []
            fb = read_frames(self.ser, self.rx, want=0, timeout=0.0, raw_sink=raw_sink)
            if raw_sink:
                self.rx_bytes += sum(len(c) for c in raw_sink)
            ts = time.monotonic()
            n = 0
            for f in fb:
                mid = f[7] & 0x0F
                if mid not in self._limits:
                    self.unknown_ids.add(mid)
                    continue
                d = decode_feedback(f[7:15], self._limits[mid])
                self._cache[mid] = MotorState.from_decoded(d, ts)
                n += 1
            self.n_recv += n
        return n

    def wait_feedback(self, motor_id: int, timeout: float = 0.05) -> MotorState | None:
        """**阻塞**读到这台电机的一帧反馈，或超时返回 None。会更新缓存。

        ⚠️ 单独用它是不够的：按 1:1 规律，**发命令之前必须先 `flush()`**，
        否则读到的可能是上一条命令的应答。直接用 `send_and_wait()` 更安全 ——
        它把"flush → 发 → 等"这三步做成原子的。
        """
        if self.ser is None:
            raise RuntimeError("串口没开，先调 open() / connect()")
        deadline = time.monotonic() + timeout
        with self._lock:
            while True:
                left = deadline - time.monotonic()
                if left <= 0:
                    return None
                raw_sink: list[bytes] = []
                fb = read_frames(self.ser, self.rx, want=1, timeout=left,
                                 raw_sink=raw_sink)
                if raw_sink:
                    self.rx_bytes += sum(len(c) for c in raw_sink)
                self.n_recv += len(fb)
                for f in fb:
                    mid = f[7] & 0x0F
                    if mid not in self._limits:
                        self.unknown_ids.add(mid)
                        continue
                    d = decode_feedback(f[7:15], self._limits[mid])
                    st = MotorState.from_decoded(d, time.monotonic())
                    self._cache[mid] = st
                    if mid == motor_id:
                        return st

    def send_and_wait(self, frame: bytes, motor_id: int,
                      timeout: float = 0.05) -> MotorState | None:
        """「发一帧等一帧」，原子的一对：**flush → 发 → 等这台电机的一帧**。

        这是 DESIGN.md D2 的"正确性 > 吞吐"那条路，给单路调试、标定、寄存器读写用
        （7 关节满载的常规循环用 `poll()`）。

        为什么必须合成一个方法：flush 和 send 之间的空隙如果被别的代码发了帧，
        你等到的就不是自己这条命令的应答 —— 而这类错误的表现是"数据慢一拍"，
        非常难查。合成一步就不会被误用。

        ⚠️ 它**会阻塞** timeout。别放进 500Hz 循环。
        """
        if self.ser is None:
            raise RuntimeError("串口没开，先调 open() / connect()")
        with self._lock:
            flush_rx(self.ser, self.rx)
            self.send_frame(frame)
            return self.wait_feedback(motor_id, timeout)

    def poll_and_wait(self, motor_id: int, timeout: float = 0.05) -> MotorState | None:
        """只等不发的版本（比如 0x7FF 刷新帧已经用 `send_frame` 发过了）。"""
        return self.wait_feedback(motor_id, timeout)

    # ───────────────────────── 状态查询 ─────────────────────────
    def get_state(self, motor_id: int) -> MotorState | None:
        """最近一次收到的状态快照，没有就 None。**不触发总线通信。**"""
        return self._cache.get(motor_id)

    def states(self) -> dict[int, MotorState]:
        return dict(self._cache)

    def flush(self) -> int:
        """丢掉缓冲里已有的帧，返回丢掉的条数。手动 `send_and_wait` 时才需要。"""
        if self.ser is None:
            raise RuntimeError("串口没开，先调 open() / connect()")
        with self._lock:
            return flush_rx(self.ser, self.rx)

    def stats(self) -> dict:
        """计数与写延迟。`ratio` 只在**逐台直发**时才是 1:1。

        ⚠️ 发了广播刷新帧（0x7FF）时，一条帧会回多条 —— 那种情况下
        `n_sent` 与 `n_recv` 的比值**本来就会 >1，不是异常**。所以刷新帧单独计在
        `sent_broadcast` 里，不混进 `sent`。
        """
        lat = sorted(self.write_lat)
        if lat:
            avg = sum(lat) / len(lat)
            p99 = lat[min(len(lat) - 1, int(len(lat) * 0.99))]
            mx = lat[-1]
        else:
            avg = p99 = mx = 0.0
        return {
            "sent": self.n_sent,
            "sent_broadcast": self.n_sent_broadcast,
            "received": self.n_recv,
            "ratio": (self.n_recv / self.n_sent) if self.n_sent else 0.0,
            "rx_bytes": self.rx_bytes,
            "write_avg_ms": avg * 1000.0,
            "write_p99_ms": p99 * 1000.0,
            "write_max_ms": mx * 1000.0,
            "pending_bytes": len(self.rx.buf),
            "unknown_ids": sorted(self.unknown_ids),
        }

    def __enter__(self) -> "MotorBus":
        return self

    def __exit__(self, *exc) -> None:
        self.close()
