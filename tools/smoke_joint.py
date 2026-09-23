#!/usr/bin/env python3
"""冒烟测试⑤：Joint 单电机控制类（**不需要硬件、不需要 pyserial、不需要真串口**）

`Joint` 是"一个实例管一个电机"的完整控制类（design.md §四）。它是**薄**的 ——
不发线程、不做寄存器 I/O、不 poll，只做三件事：**坐标换算 / 安全钳位 / 出错保护**。
这三件事错了都是静默的（`direction` 反了是电机朝反方向跑、档位错了是力矩差 2.8 倍
而不报错），所以在这里用假串口钉死。

  [1] limit 注册到 bus；同一个 ID 出现**两个不同档位**必须当场拒绝
  [2] MIT 帧：CAN ID = SlaveID 本身（不是 0x100+ID）；q/dq/kp/kd/tau 按各自档位编码
  [3] direction / offset 换算：位置带 offset、速度与力矩是**矢量**（只翻号不加 offset）
  [4] **PMAX 硬钳位**（两个方向都要钳）；软限位在关节侧、**先于** PMAX 生效
  [5] MIT 满量程的**静默钳位**（kp>500 / tau>TMAX）必须出声并计数
  [6] 三个预留桩（POS_VEL / 力位混控 / 切模式）必须拒绝，不能装作能用
  [7] `enable()` 的时序：使能帧 + **紧跟一帧零力矩**。
      注意「零力矩」= tau 编码到**中点**，**不是全零字节**（见下，这轮踩到过）
  [8] 故障态（ERR=13 锁存）下拒绝使能，且提示"要断电才能清"
  [9] `assert_healthy()`：**先失能再抛异常**（真机安全语义，用户确认过）
  [10] `get_state()`：换算正确；没收到过反馈返回 **None**（不造零值骗调用方）
  [11] 安全边界（静态 + 动态）：`joint.py` **不 import serial / DM_CAN / dm_registers**，
       代码里**不出现** `set_zero_position` / `Serial(` / `save_motor_param`；
       运行期发出的每一帧都**不是** 0xFE（设零位）形状

  [12] **出错保护的三条路径必须各走各的**（安全语义，混在一起就是事故）：
       发送失败 → 补发失能帧 + 原异常**原样抛**；
       失能**自己也失败** → 吞掉、**不掩盖原异常**、但大喊"去断电"；
       正常路径 `disable()` 失败 → **抛出来**（与上一条故意相反）；
       钳位/换算的纯软件错 → **不失能**（不该让带电的电机松掉）
  [13] `_warn` 节流：**每秒最多一条**，但 `clamped_count`/`saturated_count`
       **一次都不能少**；跨过窗口后警告要能再出来（不是"只报一次就闭嘴"）
  [14] **边界值**：正好落在 ±PMAX / VMAX / TMAX / KP_FULL / KD_FULL / 软限位上
       ⇒ 既不报警也不钳位（差一错是经典坑）；**单边软限位**时另一边不检查。
       外加：软限位换算后超出 ±PMAX ⇒ **构造时就抛**（写在 PMAX 之外的软限位是
       **虚的** —— 电机到不了，而且一次命令会被两道钳位各计一次）；校验必须
       **真的做 direction/offset 换算**（朴素 `abs(val) <= PMAX` 是错的）；
       构造失败**不在 bus 上留残留注册**
  [15] `stats()` 的 10 个键与计数语义；`__repr__`；矢量换算两向对称、位置换算往返自洽
  [16] **非有限数与非自洽配置**（这组是**读代码**读出来的，不是测出来的）：
       NaN 会**穿过两道钳位**（比大小的结果都是 False）⇒ 老写法 `out != q_joint`
       在 NaN 上会**假计数**（没设限位也记一次、还打 `[None, None]` 的假警告）；
       NaN 一路走到 `float_to_uint` 才炸、而那一层在 `_fail_safe` 里 ⇒
       **纯软件错却让电机失能**（重力负载下关节会掉）；
       ±inf 会被 `_clamp` 悄悄钳成 PMAX，"看着正常"；
       `position_min > position_max` ⇒ 命令被**钉死在两个点上**且不报错

> **这些断言是做过变异测试的。** 往 `joint.py` 里塞 11 个 bug，**11 个全部被这套测试
> 抓到**：去掉补发失能、去掉警告节流、kp 上界差一、`disable()` 吞异常、
> `_clamp` 负边界差一、失能失败时闭嘴、软限位越界不拒、软限位校验不做换算、
> `_apply_soft_limit` 退回 `!=` 判断、不再拒绝非有限数、软限位反了不校验。
>
> ⚠️ **但别把"全绿"读成"没问题"。** 变异测试只覆盖**想得到的 bug 类型** ——
> [16] 那三条就是变异测试**抓不到**的（我塞的 11 个变异里一个都不是那三类），
> 它们是从头读代码读出来的。测试能证明的只有"验过的行为是对的"，
> 证明不了"没验过的地方也是对的"。

> ⚠️ **MIT「零力矩」不是全零字节** —— 这轮第一版测试就是栽在这里。
> `t_ff=0` 在 12 位区间 `[-TMAX, +TMAX]` 上映射到**中点 2047**，`dq=0` 同理。
> 也就是说：如果你漏填 `t_ff` 这一项、让字节保持默认 0，实际发出去的是
> **-TMAX 满负力矩**，不是零。`[7]` 把这件事钉成回归。

用法（在 DM_Armx 根目录）：
    pixi run python tools/smoke_joint.py
"""
import ast
import contextlib
import io
import sys
import time as _time
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "src" / "DMmotor_driver"))

from DMmotor_driver import dm_frames as F  # noqa: E402
from DMmotor_driver.dm_bus import MotorBus, MotorState  # noqa: E402
from DMmotor_driver.joint import Joint, JointState  # noqa: E402

# 两套映射范围（都用实机回读值）。**4310 与 4340P 不一样** —— 这正是 [1] 要防的事。
LIM_4310 = (12.5, 30.0, 10.0)
LIM_4340 = (12.5, 10.0, 28.0)

# 帧里"零力矩"应该编码成的值：12 位区间的中点。见模块 docstring 的警告。
ZERO12 = int((1 << 12) - 1) // 2      # 2047
FULL16 = (1 << 16) - 1                # 65535


class FakeSerial:
    """假串口：有 `in_waiting` / `read` / `write` / `close` 就够 MotorBus 用了。

    只关心"往总线上写了什么"，所以 `read` 永远返回空 —— Joint 本来也不读。
    """

    def __init__(self):
        self.written: list[bytes] = []
        self.closed = False

    @property
    def in_waiting(self):
        return 0

    def read(self, n):
        return b""

    def write(self, b):
        self.written.append(bytes(b))
        return len(b)

    def close(self):
        self.closed = True

    def reset_input_buffer(self):
        pass


class FussySerial(FakeSerial):
    """**MIT 帧会炸，命令帧（使能/失能）照常通过。**

    用来分辨「发送失败之后到底发生了什么」：如果失能帧成功写进来了，
    说明 `_fail_safe` 真的补了失能；如果什么都没有，说明它只是把异常往上抛。
    """

    def write(self, b):
        if b[21:28] != b"\xff" * 7:      # 数据段不是 FF*7 ⇒ 这是 MIT 帧
            raise OSError("MIT 帧发送失败（模拟链路抖动）")
        return super().write(b)


class BoomSerial(FakeSerial):
    """**写什么都炸** —— USB 线掉了的那种。连失能帧也发不出去。"""

    def write(self, b):
        raise OSError("串口炸了（模拟 USB 掉了）")


def make_bus(ser=None):
    """建一个 MotorBus 并把假串口直接塞进去 —— 绕过 open()，
    这样连 pyserial 和 /dev/ttyACM0 都不需要（同 smoke_dm_bus 的做法）。"""
    bus = MotorBus("/dev/null")
    bus.ser = ser if ser is not None else FakeSerial()
    return bus


def frame_at(ser, i):
    """第 i 条写出去的 30 字节帧。"""
    return ser.written[i]


def mit_fields(frame):
    """把 MIT 帧的数据字节解回来，供断言读。

    ⚠️ 这**不是**独立的对拍预言机 —— 帧本身的字节级正确性由 `smoke_dm_frames.py`
    对着厂商 SDK 的 `controlMIT` 逐字节钉住了（那里是整帧对拍）。这里解回来只是
    为了让断言读得懂"到底是哪个字段错了"，布局照抄 `dm_frames.mit_frame`：

        D[0:2]=q(16)  D[2:4]=dq(12)  D[4:6]=kp(12)  D[6:8]=kd(12) 与 tau(12) 交错
    """
    d = frame[21:29]
    return dict(
        can_id=int.from_bytes(frame[13:15], "little"),
        q=((d[0] << 8) | d[1]),
        dq=((d[2] << 4) | (d[3] >> 4)),
        kp=(((d[3] & 0x0F) << 8) | d[4]),
        kd=((d[5] << 4) | (d[6] >> 4)),
        tau=(((d[6] & 0x0F) << 8) | d[7]),
    )


def main() -> int:
    fails = []

    def check(cond, label, extra=""):
        print(f"    {'✓' if cond else '✗'} {label}" + (f"   {extra}" if extra else ""))
        if not cond:
            fails.append(label)

    print("=" * 78)
    print("smoke_joint · Joint 单电机控制类   【假串口，不碰硬件】")
    print("=" * 78)

    # ── [1] limit 注册与档位冲突 ────────────────────────────────────
    print("\n[1] limit 注册到 bus；同一 ID 出现两个档位必须拒绝")
    bus = make_bus()
    j = Joint(bus, 0x01, LIM_4340)
    check(bus.motors().get(0x01) == LIM_4340, "Joint 把 limit 注册到了 bus")
    try:
        Joint(bus, 0x01, LIM_4310)          # 同一个电机、另一套档位
        check(False, "同 ID 注册不同 limit 应当抛 ValueError")
    except ValueError as e:
        check("两个映射范围" in str(e),
              "同 ID 不同档位 → ValueError（防「用错档位力矩差 2.8 倍且不报错」）")
    check(Joint(bus, 0x01, LIM_4340) is not None,
          "同一套档位重复注册是允许的（幂等）")
    for bad, why in ((0, "direction=0"), (2, "direction=2")):
        try:
            Joint(bus, 0x09, LIM_4340, direction=bad)
            check(False, f"{why} 应当抛 ValueError")
        except ValueError:
            check(True, f"{why} → ValueError（只接受 ±1）")
    try:
        Joint(bus, 0x09, (12.5, 10.0))
        check(False, "limit 长度不是 3 应当抛")
    except ValueError:
        check(True, "limit 必须是 (PMAX, VMAX, TMAX) 三元组")

    # ── [2] MIT 帧的 CAN ID 与定点映射 ──────────────────────────────
    print("\n[2] set_mit 的帧：CAN ID = SlaveID 本身；各字段按**自己的**档位编码")
    ser = FakeSerial()
    bus = make_bus(ser)
    j = Joint(bus, 0x01, LIM_4340)
    j.set_mit(kp=120.0, kd=0.8, q=1.0, dq=0.5, tau=2.0)
    m = mit_fields(frame_at(ser, 0))
    check(m["can_id"] == 0x01, "CAN ID = SlaveID 本身（**不是** POS_VEL 的 0x100+ID）")
    check(m["q"] == F.float_to_uint(1.0, -12.5, 12.5, 16), f"q 按 PMAX=12.5 → {m['q']}")
    check(m["dq"] == F.float_to_uint(0.5, -10.0, 10.0, 12), f"dq 按 VMAX=10 → {m['dq']}")
    check(m["kp"] == F.float_to_uint(120.0, 0, 500, 12), f"kp 按 [0,500] → {m['kp']}")
    check(m["kd"] == F.float_to_uint(0.8, 0, 5, 12), f"kd 按 [0,5] → {m['kd']}")
    check(m["tau"] == F.float_to_uint(2.0, -28.0, 28.0, 12), f"tau 按 TMAX=28 → {m['tau']}")
    check(j.saturated_count == 0, "全部在满量程内 → 一个字都没警告")

    # 换一台 4310：同一个 tau=2.0，编码必须不同（档位真的被查了）
    bus.ser = FakeSerial()
    j43 = Joint(bus, 0x04, LIM_4310)
    j43.set_mit(kp=1.0, kd=0.1, q=1.0, dq=0.5, tau=2.0)
    m43 = mit_fields(frame_at(bus.ser, 0))
    check(m43["dq"] == F.float_to_uint(0.5, -30.0, 30.0, 12), "4310 的 dq 按 VMAX=30 编码")
    check(m43["tau"] == F.float_to_uint(2.0, -10.0, 10.0, 12), "4310 的 tau 按 TMAX=10 编码")
    check(m43["dq"] != m["dq"] and m43["tau"] != m["tau"],
          "同一个命令值在 4310 / 4340P 上编码不同 ← 档位必须按电机查")

    # ── [3] direction / offset 换算 ────────────────────────────────
    print("\n[3] direction / offset：位置带 offset，速度与力矩是**矢量**")
    bus.ser = FakeSerial()
    jd = Joint(bus, 0x02, LIM_4340, direction=-1, offset=0.5)
    check(abs(jd._pos_to_motor(1.0) - (-0.5)) < 1e-12, "dir=-1,off=0.5：关节 1.0 → 电机 -0.5")
    check(abs(jd._pos_to_joint(-0.5) - 1.0) < 1e-12, "往返换算自洽")
    check(jd._vec_to_motor(3.0) == -3.0, "速度/力矩是矢量：只乘 direction，**不加 offset**")
    jd.set_mit(kp=1.0, kd=0.1, q=1.0, dq=0.5, tau=2.0)
    md = mit_fields(frame_at(bus.ser, 0))
    check(md["q"] == F.float_to_uint(-0.5, -12.5, 12.5, 16), "dir=-1 时 q 真的翻到了电机侧")
    check(md["tau"] == F.float_to_uint(-2.0, -28.0, 28.0, 12), "dir=-1 时 tau 也翻号（力矩是矢量）")
    check(md["dq"] == F.float_to_uint(-0.5, -10.0, 10.0, 12), "dir=-1 时 dq 也翻号")

    # ── [4] PMAX 硬钳位 + 软限位 ───────────────────────────────────
    print("\n[4] PMAX 硬钳位（两个方向）与软限位（关节侧，先于 PMAX）")
    bus.ser = FakeSerial()
    j = Joint(bus, 0x01, LIM_4340)
    j.set_mit(kp=1.0, kd=0.1, q=99.0)
    check(mit_fields(frame_at(bus.ser, 0))["q"] == FULL16,
          "q=+99 钳到 +PMAX（16 位满码）")
    check(j.clamped_count == 1, f"钳位计数 = {j.clamped_count}")
    bus.ser = FakeSerial()
    j.set_mit(kp=1.0, kd=0.1, q=-99.0)
    check(mit_fields(frame_at(bus.ser, 0))["q"] == 0, "q=-99 钳到 -PMAX（编码 0）")
    check(j.clamped_count == 2, f"钳位计数 = {j.clamped_count}")

    j3 = Joint(bus, 0x05, LIM_4340, position_min=-1.0, position_max=1.0)
    bus.ser = FakeSerial()
    j3.set_mit(kp=1.0, kd=0.1, q=5.0)
    check(mit_fields(frame_at(bus.ser, 0))["q"] == F.float_to_uint(1.0, -12.5, 12.5, 16),
          "软限位 ±1.0 生效（钳的不是 PMAX=12.5）")
    bus.ser = FakeSerial()
    j3.set_mit(kp=1.0, kd=0.1, q=0.3)
    check(mit_fields(frame_at(bus.ser, 0))["q"] == F.float_to_uint(0.3, -12.5, 12.5, 16),
          "限位内的命令不被改动")
    check(j3.clamped_count == 1, "限位内不计数")
    bus.ser = FakeSerial()
    j4 = Joint(bus, 0x06, LIM_4340)          # 不传软限位 = 不检查（本轮默认留空）
    j4.set_mit(kp=1.0, kd=0.1, q=5.0)
    check(j4.clamped_count == 0 and mit_fields(frame_at(bus.ser, 0))["q"]
          == F.float_to_uint(5.0, -12.5, 12.5, 16), "不传软限位 → 不检查（5.0 原样通过）")

    # ── [5] 静默饱和要出声 ─────────────────────────────────────────
    print("\n[5] MIT 满量程的静默钳位（kp>500 / tau>TMAX）必须出声并计数")
    bus.ser = FakeSerial()
    j = Joint(bus, 0x01, LIM_4340)
    j.set_mit(kp=999.0, kd=0.1, q=0.0, tau=999.0)
    check(j.saturated_count == 1, f"kp 与 tau 同时超范围 → 计数 = {j.saturated_count}")
    check(mit_fields(frame_at(bus.ser, 0))["kp"] == F.float_to_uint(500.0, 0, 500, 12),
          "超范围的 kp 实际被静默钳到满量程（这正是要出声的原因）")
    bus.ser = FakeSerial()
    j.set_mit(kp=1.0, kd=9.9, q=0.0)
    check(j.saturated_count == 2, "kd 超过 5 也计数")
    bus.ser = FakeSerial()
    j.set_mit(kp=1.0, kd=0.1, q=0.0, dq=999.0)
    check(j.saturated_count == 3, "dq 超过 VMAX 也计数")

    # ── [6] 预留桩必须拒绝 ─────────────────────────────────────────
    print("\n[6] 三个预留桩必须拒绝，不能装作能用")
    for fn, args in (("set_pos_vel", (0.0, 0.5)),
                     ("set_force_pos", (0.0, 0.0, 0.0)),
                     ("switch_mode", (2,))):
        try:
            getattr(j, fn)(*args)
            check(False, f"{fn} 应当抛 NotImplementedError")
        except NotImplementedError as e:
            check("POS_VEL" in str(e) or "寄存器" in str(e),
                  f"{fn} → NotImplementedError", f"（说清了为什么：{str(e)[:28]}…）")

    # ── [7] enable 的时序：使能帧 + 紧跟零力矩帧 ────────────────────
    print("\n[7] enable() 的时序：使能帧 + **紧跟一帧零力矩**")
    bus.ser = FakeSerial()
    j = Joint(bus, 0x01, LIM_4340)
    j.enable()
    check(len(bus.ser.written) == 2, f"enable() 一共发了 {len(bus.ser.written)} 帧（使能 + 零力矩）")
    f0 = frame_at(bus.ser, 0)
    check(f0[21:28] == b"\xff" * 7 and f0[28] == F.CMD_ENABLE, "第 1 帧 = 使能（FF*7 + 0xFC）")
    check(int.from_bytes(f0[13:15], "little") == 0x01, "使能帧 CAN ID = SlaveID（不是 0x100+）")
    z = mit_fields(frame_at(bus.ser, 1))
    check(z["kp"] == 0 and z["kd"] == 0, "第 2 帧 kp = kd = 0")
    check(z["tau"] == ZERO12, f"第 2 帧 tau 编码 = {z['tau']}（12 位区间**中点**才是零力矩）")
    check(frame_at(bus.ser, 1)[21:29] != bytes(8),
          "⇒ 「零力矩」帧**不是全零字节**（漏填 t_ff 会变成 -TMAX 满负力矩）")
    check(j.n_enable == 1 and j.n_mit == 1, f"计数 enable={j.n_enable} mit={j.n_mit}")
    check(F.float_to_uint(0.0, -28.0, 28.0, 12) == ZERO12,
          "把这条钉死：float_to_uint(0, -TMAX, +TMAX, 12) == 2047")

    # ── [8] 故障态拒绝使能 ─────────────────────────────────────────
    print("\n[8] 故障态（ERR=13 锁存）下拒绝使能")
    bus._cache[0x01] = MotorState(0x01, 0xD, "通讯丢失", 0., 0., 0., 30, 33, _time.monotonic())
    n_before = len(bus.ser.written)
    try:
        j.enable()
        check(False, "故障态下 enable 应当拒绝")
    except RuntimeError as e:
        check("锁存" in str(e) and "断电" in str(e),
              "ERR=13 → 拒绝使能，并提示「锁存、要断电才能清」")
        check(len(bus.ser.written) == n_before, "拒绝时**一帧都没发出去**（没使能成功）")
    # 使能态（ERR=1）不该被拒
    bus._cache[0x01] = MotorState(0x01, 0x1, "使能", 0., 0., 0., 30, 33, _time.monotonic())
    n_before = len(bus.ser.written)
    j.enable()
    check(len(bus.ser.written) == n_before + 2, "ERR=1（已使能）不拒，照常发两帧")

    # ── [9] assert_healthy：先失能再抛 ─────────────────────────────
    print("\n[9] assert_healthy()：故障 → **先失能再抛异常**")
    bus.ser = FakeSerial()
    bus._cache[0x01] = MotorState(0x01, 0xD, "通讯丢失", 0., 0., 0., 30, 33, _time.monotonic())
    j.n_disable = 0
    try:
        j.assert_healthy()
        check(False, "故障态下 assert_healthy 应当抛")
    except RuntimeError as e:
        check(j.n_disable == 1, "抛之前**发了失能帧**（这就是「先失能再抛」）")
        check(frame_at(bus.ser, 0)[28] == F.CMD_DISABLE, "发出去的确实是失能帧（0xFD）")
        check("不会让它回零" in str(e), "异常里说明了「失能 ≠ 停下」（重力负载下会掉）")
    bus._cache[0x01] = MotorState(0x01, 0x1, "使能", 0., 0., 0., 30, 33, _time.monotonic())
    check(j.assert_healthy() is not None, "正常态（ERR=1）不抛，返回状态")
    check(j.assert_healthy().enabled is True, "JointState.enabled 来自电机自报的 ERR==1")

    # ── [10] get_state ─────────────────────────────────────────────
    print("\n[10] get_state()：换算正确；没收到过反馈返回 None")
    bus = make_bus()
    j_none = Joint(bus, 0x07, LIM_4340)
    check(j_none.get_state() is None,
          "没收到过反馈 → None（**不造零值** —— 那会让调用方以为电机在 0 位）")
    bus._cache[0x01] = MotorState(0x01, 0x1, "使能", 0.4, 1.0, 2.0, 31, 34, _time.monotonic())
    jp = Joint(bus, 0x01, LIM_4340)                 # dir=+1, off=0
    st = jp.get_state()
    check(isinstance(st, JointState), "返回的是 JointState（不是 MotorState）")
    check(st.position == 0.4 and st.velocity == 1.0 and st.torque == 2.0, "dir=+1 时原样通过")
    check(st.vbus is None, "vbus 恒为 None（要读寄存器 0x3C，本层不做寄存器 I/O）")
    check(st.name == "0x01", f"name 默认 = 电机 ID：{st.name}")
    bus._cache[0x02] = MotorState(0x02, 0x1, "使能", 0.4, 1.0, 2.0, 31, 34, _time.monotonic())
    jd2 = Joint(bus, 0x02, LIM_4340, direction=-1, offset=0.5)
    std = jd2.get_state()
    check(abs(std.position - 0.1) < 1e-12, f"dir=-1,off=0.5 时 电机 0.4 → 关节 {std.position}")
    check(std.torque == -2.0 and std.velocity == -1.0, "速度/力矩在 JointState 里也翻号了")
    check("0x01" in repr(jp), f"repr 可读：{jp!r}")

    # ── [11] 安全边界（静态 + 动态）────────────────────────────────
    print("\n[11] 安全边界：不 import 危险模块、代码里不出现设零位等调用、发出的帧没有 0xFE")
    src = (REPO / "src" / "DMmotor_driver" / "DMmotor_driver" / "joint.py").read_text()
    tree = ast.parse(src)

    imported = set()
    for node in ast.walk(tree):
        if isinstance(node, ast.Import):
            imported |= {a.name.split(".")[0] for a in node.names}
        elif isinstance(node, ast.ImportFrom) and node.module:
            imported.add(node.module.split(".")[0])
    forbidden_imports = {"serial", "DM_CAN", "dm_registers", "numpy"}
    check(not (imported & forbidden_imports),
          "joint.py 不 import serial / DM_CAN / dm_registers / numpy",
          f"（实际：{sorted(imported)}）")

    called = set()
    for node in ast.walk(tree):
        if isinstance(node, ast.Call):
            f = node.func
            called.add(f.attr if isinstance(f, ast.Attribute) else getattr(f, "id", ""))
    forbidden_calls = {"set_zero_position", "Serial", "save_motor_param",
                       "write_param", "open"}
    check(not (called & forbidden_calls),
          "代码里不出现 set_zero_position / Serial( / save_motor_param / open",
          f"（交集：{sorted(called & forbidden_calls)}）")

    # 动态：把所有能发的都发一遍，然后逐帧检查形状
    bus.ser = FakeSerial()
    bus._cache.clear()
    jj = Joint(bus, 0x01, LIM_4340, direction=-1, offset=0.3, position_min=-5, position_max=5)
    jj.enable()
    jj.set_mit(kp=100.0, kd=5.0, q=1.0, dq=1.0, tau=1.0)
    jj.set_mit(kp=0.0, kd=0.0, q=0.0)
    jj.disable()
    shapes = []
    for f in bus.ser.written:
        d = f[21:29]
        if d[:7] == b"\xff" * 7:
            shapes.append({0xFC: "enable", 0xFD: "disable", 0xFE: "**set_zero_position**"}
                          .get(d[7], f"未知命令 0x{d[7]:02X}"))
        else:
            shapes.append("mit")
    check("**set_zero_position**" not in shapes,
          f"运行期发出的 {len(shapes)} 帧里没有 0xFE（设零位）",
          f"（形状：{shapes}）")
    check(shapes == ["enable", "mit", "mit", "mit", "disable"],
          "enable→零力矩→命令→失能，帧序列与预期一致")

    # ── [12] 出错保护：三条路径必须各走各的 ────────────────────────
    # 这是**安全语义**，三条路径故意做得不一样，混在一起就是事故：
    #   · 发送失败    → 发失能帧 + **原异常原样抛**（_fail_safe）
    #   · 失能也失败  → **吞掉** + 大喊"去断电"（_emergency_disable，它已在出错路径上）
    #   · 正常失能失败 → **抛出来**（disable()，正常路径不该吞）
    #   · 钳位/换算错 → **不失能**（纯软件问题，不该让带电的电机松掉）
    print("\n[12] 出错保护：发送失败 ⇒ 先失能再抛；失能自己失败 ⇒ 吞掉但喊断电")

    # 12a. 发送 MIT 失败：失能帧要真的发出去，且原异常原样抛
    ser = FussySerial()
    bus = make_bus(ser)
    j = Joint(bus, 0x01, LIM_4340)
    try:
        j.set_mit(kp=1.0, kd=0.1, q=0.0)
        check(False, "MIT 发送失败应当抛")
    except OSError as e:
        check("MIT 帧发送失败" in str(e), "原异常**原样抛出**（没被失能逻辑替换成别的）")
    check(len(ser.written) == 1 and ser.written[0][21:28] == b"\xff" * 7
          and ser.written[0][28] == F.CMD_DISABLE,
          "抛之前真的补发了失能帧（这就是「先失能再抛」）",
          f"（实际写出去 {len(ser.written)} 帧）")
    check(j.n_disable == 1, f"失能计数 = {j.n_disable}")

    # 12b. 失能自己也失败：吞掉、不掩盖原异常、但必须喊出来
    ser = BoomSerial()
    bus = make_bus(ser)
    j = Joint(bus, 0x01, LIM_4340)
    buf = io.StringIO()
    caught = None
    try:
        with contextlib.redirect_stderr(buf):
            j.set_mit(kp=1.0, kd=0.1, q=0.0)
    except OSError as e:
        caught = e
    err = buf.getvalue()
    check(caught is not None and "串口炸了" in str(caught),
          "失能失败**没有把原异常盖掉**（掩掉原始错误是排查噩梦）",
          f"（抛出的是：{caught}）")
    check("失能失败" in err and "断电" in err,
          "但失能失败本身必须喊出来 —— 人得知道「电机可能还在通电出力」")
    check(j.n_disable == 0, "失败的失能不计数（计数不能骗人）")

    # 12c. 正常路径的失能失败：**必须抛**（与 12b 正好相反）
    ser = BoomSerial()
    bus = make_bus(ser)
    j = Joint(bus, 0x01, LIM_4340)
    try:
        j.disable()
        check(False, "disable() 失败应当抛")
    except OSError:
        check(True, "disable() 失败**抛出来**（正常路径吞掉 = 以为停了其实没停）")

    # 12d. 钳位/换算阶段的纯软件错：**不该让电机失能**
    ser = FakeSerial()
    bus = make_bus(ser)
    j = Joint(bus, 0x01, LIM_4340)
    try:
        j.set_mit(kp=1.0, kd=0.1, q=None)       # float(None) → TypeError
        check(False, "q 不是数应当抛")
    except TypeError:
        check(j.n_disable == 0 and len(ser.written) == 0,
              "参数类型错（纯软件）⇒ **不失能、一帧不发** —— 钳位算错不该让带电的电机松掉")

    # ── [13] _warn 的节流：每秒一条，但计数不受影响 ─────────────────
    print("\n[13] _warn 节流：500Hz 循环不能刷屏，但计数一次都不能少")
    ser = FakeSerial()
    bus = make_bus(ser)
    j = Joint(bus, 0x01, LIM_4340)
    buf = io.StringIO()
    with contextlib.redirect_stderr(buf):
        for _ in range(50):
            j.set_mit(kp=1.0, kd=0.1, q=99.0)   # 每次都越 PMAX
    lines = [ln for ln in buf.getvalue().splitlines() if ln.strip()]
    check(j.clamped_count == 50,
          "计数**不受节流影响**：50 次越界 → clamped_count=50", f"实际 {j.clamped_count}")
    check(len(lines) == 1,
          "但 stderr 只出了 1 条（同一秒内的其余 49 次被节流）", f"实际 {len(lines)} 条")

    _time.sleep(1.05)                            # 跨过一个节流窗口
    buf2 = io.StringIO()
    with contextlib.redirect_stderr(buf2):
        j.set_mit(kp=1.0, kd=0.1, q=99.0)
    check(len([ln for ln in buf2.getvalue().splitlines() if ln.strip()]) == 1,
          "过了 1 秒，警告又能出来了（节流不是「只报一次就永远闭嘴」）")

    # 节流窗口共用：钳位与饱和各喊一次，也只出一条
    ser = FakeSerial()
    bus = make_bus(ser)
    j = Joint(bus, 0x01, LIM_4340)
    buf3 = io.StringIO()
    with contextlib.redirect_stderr(buf3):
        j.set_mit(kp=999.0, kd=0.1, q=99.0)      # 同时越 kp 满量程与 PMAX
    check(j.saturated_count == 1 and j.clamped_count == 1,
          "同一次调用里饱和与钳位**各自计数**")
    check(len([ln for ln in buf3.getvalue().splitlines() if ln.strip()]) == 1,
          "但共用同一个节流窗口 → 仍只出一条（不是两条）")

    # ── [14] 边界值：正好在量程上不该报警、不该钳位 ──────────────────
    print("\n[14] 边界：正好落在满量程/限位上 ⇒ 既不报警也不钳位（差一错是经典坑）")
    bus = make_bus()
    j = Joint(bus, 0x01, LIM_4340)
    j.set_mit(kp=500.0, kd=5.0, q=12.5, dq=10.0, tau=28.0)
    check(j.saturated_count == 0 and j.clamped_count == 0,
          "正方向正好在满量程上 → 一个字都不报",
          f"（sat={j.saturated_count}, clamp={j.clamped_count}）")
    j.set_mit(kp=500.0, kd=5.0, q=-12.5, dq=-10.0, tau=-28.0)
    check(j.saturated_count == 0 and j.clamped_count == 0, "负方向同理（对称）")

    j.set_mit(kp=-0.1, kd=5.0, q=0.0, dq=0.0, tau=0.0)
    check(j.saturated_count == 1, "kp 差一点点到 0（-0.1）→ 报警")
    j.set_mit(kp=500.0, kd=-0.1, q=0.0)
    check(j.saturated_count == 2, "kd 为负 → 报警（kd 不是「越小越安全」）")
    j.set_mit(kp=500.0, kd=5.0, q=0.0, dq=10.0001)
    check(j.saturated_count == 3, "dq 超过 VMAX 一点点 → 报警")
    j.set_mit(kp=500.0, kd=5.0, q=0.0, tau=28.0001)
    check(j.saturated_count == 4, "tau 超过 TMAX 一点点 → 报警")

    # 软限位：正好在边界上不钳；单边设限时另一边不检查
    bus = make_bus()
    jm = Joint(bus, 0x08, LIM_4340, position_min=-1.0)
    jm.set_mit(kp=1.0, kd=0.1, q=-1.0)
    check(jm.clamped_count == 0, "正好压在下界上 → 不钳（< 才算越界）")
    jm.set_mit(kp=1.0, kd=0.1, q=-5.0)
    check(jm.clamped_count == 1, "低于下界 → 钳到下界")
    jm.set_mit(kp=1.0, kd=0.1, q=+5.0)
    check(jm.clamped_count == 1, "**只设下界**时，正方向不检查（5.0 原样通过）")

    jM = Joint(bus, 0x09, LIM_4340, position_max=1.0)
    jM.set_mit(kp=1.0, kd=0.1, q=1.0)
    check(jM.clamped_count == 0, "正好压在上界上 → 不钳")
    jM.set_mit(kp=1.0, kd=0.1, q=+5.0)
    check(jM.clamped_count == 1, "高于上界 → 钳到上界")
    jM.set_mit(kp=1.0, kd=0.1, q=-5.0)
    check(jM.clamped_count == 1, "**只设上界**时，负方向不检查")

    # ★ 软限位换算后必须落在 ±PMAX 之内，否则构造时就抛（防「虚限位」与「一命令计两次」）
    bus = make_bus()
    check(Joint(bus, 0x0D, LIM_4340, position_min=-12.5, position_max=12.5) is not None,
          "软限位正好等于 ±PMAX → 允许（边界不算越界）")

    try:
        Joint(bus, 0x0E, LIM_4340, position_max=12.6)
        check(False, "软限位超出 PMAX 应当抛")
    except ValueError as e:
        check("两道钳位各计一次" in str(e) and "到不了" in str(e),
              "软限位超出 PMAX → **构造时就抛**",
              "（说清了「限位是虚的」+「计数会翻倍」）")

    # ★ 关键：校验必须**真的做换算**，不能是朴素的 abs(val) <= PMAX
    try:
        Joint(bus, 0x0F, LIM_4340, offset=5.0, position_max=12.5)
        check(False, "offset=5.0 时 12.5 换算到电机侧是 17.5，应当抛")
    except ValueError as e:
        check("17.5" in str(e),
              "换算**真的用了 offset**：12.5 本身没超 PMAX，加 offset 后才超 → 拒绝",
              f"（异常里报出了电机侧的值）")
    check(Joint(bus, 0x0E, LIM_4340, direction=-1, position_max=12.5) is not None,
          "direction=-1 时 12.5 → 电机侧 -12.5，仍在范围内 → 允许（符号真的参与了）")

    # 构造失败**不该在 bus 上留下注册**（先校验、后改动）
    bus2 = make_bus()
    try:
        Joint(bus2, 0x03, LIM_4340, position_max=99.0)
    except ValueError:
        pass
    check(bus2.motors() == {}, "构造失败后 bus 上**没有**残留注册",
          f"（实际：{bus2.motors()}）")

    # 软限位收进 PMAX 之后，「一次命令计两次」就不可达了
    ser = FakeSerial()
    bus = make_bus(ser)
    jn = Joint(bus, 0x0F, LIM_4340, position_min=-1.0, position_max=1.0)
    with contextlib.redirect_stderr(io.StringIO()):
        jn.set_mit(kp=1.0, kd=0.1, q=-30.0)
    check(jn.clamped_count == 1,
          "软限位在 PMAX 内 ⇒ 一次命令只计一次（两道钳位同时触发已不可达）",
          f"实际 {jn.clamped_count}")

    # ── [15] stats() / __repr__ / 矢量换算的对称性 ──────────────────
    print("\n[15] stats() 自描述计数；矢量换算两向对称；name 可自定义")
    ser = FakeSerial()
    bus = make_bus(ser)
    j = Joint(bus, 0x0A, LIM_4310, name="j5", direction=-1, offset=0.25)
    j.enable()
    j.set_mit(kp=1.0, kd=0.1, q=0.0)
    j.set_mit(kp=999.0, kd=0.1, q=99.0)         # 故意越界，制造两个计数
    j.disable()
    d = j.stats()
    check(d["name"] == "j5" and d["motor_id"] == "0x0A",
          "stats 带名字与电机 ID", f"（{d['name']} / {d['motor_id']}）")
    check(d["limit"] == LIM_4310 and d["direction"] == -1 and d["offset"] == 0.25,
          "带档位/方向/零位 —— 复盘时不用回去翻构造参数")
    check(d["n_enable"] == 1 and d["n_disable"] == 1,
          f"使能/失能计数 = {d['n_enable']}/{d['n_disable']}")
    check(d["n_mit"] == 3, f"n_mit = {d['n_mit']}（enable 里的零力矩帧也算一帧）")
    check(d["saturated_count"] == 1 and d["clamped_count"] == 1,
          f"越界被记下来了（sat={d['saturated_count']}, clamp={d['clamped_count']}）")
    check(set(d) == {"name", "motor_id", "limit", "direction", "offset", "n_mit",
                     "n_enable", "n_disable", "clamped_count", "saturated_count"},
          "stats 的键就是这 10 个（多了少了都说明结构变了）")

    check("j5" in repr(j) and "0x0A" in repr(j),
          f"repr 用自定义名：{j!r}")

    bus = make_bus()
    jv = Joint(bus, 0x0B, LIM_4340, direction=-1)
    check(jv._vec_to_joint(3.0) == -3.0 and jv._vec_to_motor(3.0) == -3.0,
          "矢量换算两向都是「只乘 direction」")
    check(jv._vec_to_joint(jv._vec_to_motor(1.7)) == 1.7,
          "矢量换算往返自洽")
    check(jv._vec_to_joint(0.0) == 0.0, "零乘 -1 仍是零（-0.0 == 0.0）")

    bus = make_bus()
    jo = Joint(bus, 0x0C, LIM_4340, direction=-1, offset=0.5)
    check(abs(jo._pos_to_joint(jo._pos_to_motor(0.8)) - 0.8) < 1e-12,
          "位置换算往返自洽（位置带 offset，与矢量不同）")

    # ── [16] 非有限数 与 非自洽配置 ────────────────────────────────
    # 这三条是**读代码**读出来的，不是测出来的 —— 变异测试抓不到它们，
    # 因为变异测试只覆盖"想得到的 bug 类型"。所以专门钉在这里。
    print("\n[16] NaN / ±inf：穿过钳位、假计数、还不该让电机失能；软限位反了要当场拒")
    NAN, INF = float("nan"), float("inf")

    # 16a. 「没设限位 + NaN」不该记钳位、不该打警告
    #      根因是老写法用 `out != q_joint` 判断"钳没钳"，而 `nan != nan` 为 True
    bus = make_bus()
    j = Joint(bus, 0x01, LIM_4340)
    buf = io.StringIO()
    with contextlib.redirect_stderr(buf):
        r = j._apply_soft_limit(NAN)
    check(r != r, "没设限位时 NaN 原样返回（真的没钳）")
    check(j.clamped_count == 0,
          "但**不能**记成钳位了一次（老写法 `out != q_joint` 在 NaN 上会翻车）",
          f"实际 {j.clamped_count}")
    check(buf.getvalue() == "",
          "也不能打出「超出软限位 [None, None]，已钳到 +nan」这种假警告",
          f"实际 {buf.getvalue()!r}")

    # 16b. NaN/±inf 一律**当场抛**，而且**不失能**（纯软件错）
    for label, kw in (("q=NaN", dict(q=NAN)), ("q=+inf", dict(q=INF)),
                      ("q=-inf", dict(q=-INF)), ("dq=NaN", dict(q=0.0, dq=NAN)),
                      ("tau=inf", dict(q=0.0, tau=INF)),
                      ("kp=NaN", dict(kp=NAN, q=0.0)), ("kd=inf", dict(kd=INF, q=0.0))):
        ser = FakeSerial()
        bus = make_bus(ser)
        j = Joint(bus, 0x01, LIM_4340)
        try:
            with contextlib.redirect_stderr(io.StringIO()):
                j.set_mit(kp=kw.pop("kp", 1.0), kd=kw.pop("kd", 0.1), **kw)
            check(False, f"{label} 应当抛")
        except ValueError as e:
            check(j.n_disable == 0 and len(ser.written) == 0,
                  f"{label} → ValueError 且**不发失能帧、不发命令帧**",
                  "（失能 = 重力负载下关节会掉，比抛异常危险）")

    # 16c. ±inf 不许被 `_clamp` 悄悄吃掉（inf > PMAX 为真，会被钳成 PMAX，看着"正常"）
    ser = FakeSerial()
    bus = make_bus(ser)
    j = Joint(bus, 0x01, LIM_4340)
    try:
        j.set_mit(kp=1.0, kd=0.1, q=INF)
        check(False, "q=+inf 应当抛，不该被钳成 PMAX 混过去")
    except ValueError:
        check(j.clamped_count == 0,
              "q=+inf → 抛，而不是被 `_clamp` 钳成 PMAX 然后「一切正常」")

    # 16d. 非数（None）仍然是 TypeError，且同样不失能（与 16b 一脉相承）
    ser = FakeSerial()
    bus = make_bus(ser)
    j = Joint(bus, 0x01, LIM_4340)
    try:
        j.set_mit(kp=1.0, kd=0.1, q=None)
        check(False, "q=None 应当抛 TypeError")
    except TypeError:
        check(j.n_disable == 0 and len(ser.written) == 0,
              "q=None → TypeError（不是 ValueError），同样不失能")

    # 16e. 软限位反了（min > max）必须构造时就拒
    bus = make_bus()
    try:
        Joint(bus, 0x03, LIM_4340, position_min=1.0, position_max=-1.0)
        check(False, "position_min > position_max 应当构造时就抛")
    except ValueError as e:
        check("钉死" in str(e) or "反了" in str(e),
              "软限位反了 → 构造时就抛（说清了「命令被钉死在两个点上」）")
    check(bus.motors() == {}, "拒绝时没有在 bus 上留残留注册")
    check(Joint(bus, 0x03, LIM_4340, position_min=-1.0, position_max=1.0) is not None,
          "顺序正确时正常放行")
    check(Joint(bus, 0x04, LIM_4340, position_min=0.5, position_max=0.5) is not None,
          "min == max（退化成单点）是允许的 —— 只要自洽")

    # 16f. 正常钳位路径没被这次改写弄坏（老行为要还在）
    ser = FakeSerial()
    bus = make_bus(ser)
    j = Joint(bus, 0x05, LIM_4340, position_min=-1.0, position_max=1.0)
    buf = io.StringIO()
    with contextlib.redirect_stderr(buf):
        j.set_mit(kp=1.0, kd=0.1, q=5.0)
    check(j.clamped_count == 1 and "已钳到 +1.0000" in buf.getvalue(),
          "正常越界仍然计数并警告（改写没有把老路径弄坏）")

    # ── 收尾 ──────────────────────────────────────────────────────
    print()
    if fails:
        print(f"FAIL（{len(fails)} 项）：")
        for f in fails:
            print(f"  - {f}")
        return 1
    print("PASS: Joint 的档位注册 / MIT 编码 / 坐标换算 / 双重钳位 / 满量程告警 / "
          "预留桩 / 使能时序（零力矩=中点）/ 故障拒绝 / 先失能再抛 / 状态换算 / "
          "安全边界 / 三条出错路径各走各的 / 警告节流 / 量程边界 / 计数自描述 / "
          "非有限数与非自洽配置 均符合预期")
    return 0


if __name__ == "__main__":
    sys.exit(main())
