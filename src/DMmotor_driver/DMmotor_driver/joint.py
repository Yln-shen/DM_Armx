#!/usr/bin/env python3
"""joint.py —— Joint：单个电机的完整控制类（design.md §四 / §4.1）

## 它是什么

一个 `Joint` 实例管**一个电机**，持有一个 `MotorBus` 引用（**不自己开串口**）。
它做三件事，别的都不做：

    1. 坐标换算  电机侧 rad ←→ 关节侧 rad（`direction` / `offset`）
    2. 安全钳位  软限位（关节侧，可空）+ **PMAX 硬钳位（电机侧，必须有）**
                 软限位换算后必须落在 ±PMAX 之内，否则 `__init__` 直接抛
    3. 出错保护  任何异常 → **先失能再抛**（用户确认的语义）

## 它不是什么（边界，改之前先读）

- ❌ **不拥有控制循环**。`set_mit()` 是「发一帧」，不是「走到位」。
  MIT 是上位机自己闭环 —— 电机只按**最后一帧**出力矩 `kp·(q_des−q)+kd·(dq_des−dq)+tau_ff`。
  要维持/推进就得持续发，那是 `DmArm` / ROS 节点的活（`dm_bringup.py cmd_jog_mit` 里
  那个 `while` 循环就是原型的形态）。
- ❌ **不写任何寄存器**、**不设零位**、**不切控制模式**（`switch_mode` 是预留桩）。
- ❌ **不 poll**。读是调用方的事 —— 满载时 `MotorBus.poll()` 在独立的收循环里跑（D2）。

## 本轮实现了什么（用户指定：MIT 优先）

| 方法 | 状态 | 为什么 |
|---|---|---|
| `set_mit()` | ✅ 可用 | `CTRL_MODE` 实测就是 **1 (MIT)**，**零寄存器写入**就能跑 |
| `set_pos_vel()` | 🔒 预留桩 | 需要 `CTRL_MODE == 2`，实测当前是 1；切模式是寄存器写入，要单独批准 |
| `set_force_pos()` | 🔒 预留桩 | 依赖 POS_VEL，同上 |
| `switch_mode()` | 🔒 预留桩 | 同上 |

## ⚠️ 使能后的第一个动作（真机踩过）

`enable()` 发完使能帧会**立刻补一帧零力矩**（kp=0/kd=0/tau=0），把"已使能但还没命令"
的窗口压到最小 —— 这个时序抄的是 `dm_bringup.py cmd_jog_mit` 的 `[1]` 步。

**但接下来要把 kp 升上去时，`q` 必须先给当前位置**，否则电机会朝着 `q` 猛冲：

    st = j.get_state()                       # 先读到当前位置
    j.enable()
    if st is not None:
        j.set_mit(kp=1.0, kd=0.1, q=st.position)   # ← q 是"现在在哪"，不是"要去哪"

`kp` 也从小的来：`cmd_jog_mit` 的默认只有 **kp=1.0 / kd=0.1**（满量程 500 / 5），
是刻意保守的 —— 先用小 kp 确认方向对不对，再往上加。
"""
from __future__ import annotations

import contextlib
import math
import sys
import time
from dataclasses import dataclass
from pathlib import Path

# 同 dm_bus：既被当模块 import，也可能被裸脚本直跑，所以绝对导入 + 失败时塞 sys.path。
try:
    from DMmotor_driver.dm_bus import MotorBus, MotorState
    from DMmotor_driver.dm_frames import ERR_OK
except ImportError:  # 裸脚本直跑：parents[1] 就是 src/DMmotor_driver
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
    from DMmotor_driver.dm_bus import MotorBus, MotorState
    from DMmotor_driver.dm_frames import ERR_OK

# MIT 定点映射的满量程（`dm_frames.float_to_uint` 的钳位边界，硬编码在协议里）。
# ⚠️ 超范围不会报错 —— `float_to_uint` 会**静默钳位**。所以下面 set_mit 自己查一遍。
KP_FULL = 500.0
KD_FULL = 5.0


@dataclass
class JointState:
    """**关节侧**的一帧状态快照：已经过 `direction` / `offset` 换算。

    和 `MotorBus.MotorState`（电机侧原始量）是两个不同的东西，别合并 ——
    `MotorState` 说"电机报了什么"，`JointState` 说"关节在哪"。

    注：这个名字与 `sensor_msgs.msg.JointState` 同名但无关，ROS 层 import 时留意。
    """

    name: str
    position: float        # rad（关节侧）
    velocity: float        # rad/s
    torque: float          # N·m
    err: int               # 反馈帧 ERR 半字节
    temp_mos: int          # ℃  反馈帧 D[6]
    temp_rotor: int        # ℃  反馈帧 D[7]
    vbus: float | None     # V   寄存器 0x3C。**本层读不到寄存器 ⇒ 恒为 None**（见下）
    enabled: bool          # 电机自报的 ERR == 0x1。注意 ERR == 0 是**失能**不是"正常"
    timestamp: float       # time.monotonic()

    # `vbus` 恒为 None：母线电压在寄存器 0x3C 里，而 MotorBus/Joint 都不做寄存器 I/O
    # （写一次 ~150ms 且期间发不出帧，运行期绝不能做）。要它得等 RegisterTool 落地，
    # 在**上电自检**阶段读一次塞进来。字段先留着，免得以后改结构。


class Joint:
    """单个电机的完整控制类。持 `MotorBus` 引用，**不自己开串口**。

    典型用法（一个循环里驱动一台，MIT 模式）：

        bus = MotorBus("/dev/ttyACM0").open()
        j = Joint(bus, 0x01, (12.5, 10.0, 28.0))   # limit 用 0x15/0x16/0x17 的回读值
        st = j.get_state()                          # 先读，才知道现在在哪
        j.enable()
        j.set_mit(kp=1.0, kd=0.1, q=st.position)    # 从"在哪"开始，不是"去哪"
        for _ in range(200):
            j.set_mit(kp=1.0, kd=0.1, q=target)
            j.assert_healthy()                      # 有故障 → 先失能再抛
            time.sleep(0.01)
        j.disable()
    """

    def __init__(self, bus: MotorBus, motor_id: int, limit,
                 *, name: str | None = None,
                 direction: int = 1, offset: float = 0.0,
                 position_min: float | None = None,
                 position_max: float | None = None):
        """
        参数:
            bus        : 已注册好的 `MotorBus`（本类不 open/close 它）
            motor_id   : 电机 SlaveID（使能/失能/MIT 帧的 CAN ID 就是它本身）
            limit      : (PMAX, VMAX, TMAX)。**必须来自 0x15/0x16/0x17 的回读值** ——
                         4310 与 4340P 的档位不同，用错档位解出来的力矩差 2.8 倍
                         **而且不报错**（design.md D5）。
            direction  : +1 / -1。电机正转方向与关节正方向相反时给 -1
            offset     : 关节零位对应的电机读数（电机侧 rad）
            position_min/max : 软限位（**关节侧**）。None = 不检查（本轮默认留空）。
                              **换算到电机侧后必须落在 ±PMAX 之内**，否则构造时就抛 ——
                              写在 PMAX 之外的软限位是虚的（电机到不了），还会让
                              `clamped_count` 一次命令计两次（见校验处的注释）

        `limit` 会顺带注册到 bus 上（`add_motor`）。若该 ID 已注册过**不同的**档位，
        直接抛 `ValueError` —— 同一个电机有两个映射范围，是会静默出错的那种 bug。
        """
        if direction not in (1, -1):
            raise ValueError(f"direction 只能是 +1 或 -1，收到 {direction!r}")

        self.bus = bus
        self.motor_id = motor_id
        self.name = name or f"0x{motor_id:02X}"
        self.direction = direction
        self.offset = float(offset)
        self.position_min = position_min
        self.position_max = position_max

        limit = tuple(float(x) for x in limit)
        if len(limit) != 3:
            raise ValueError(f"limit 必须是 (PMAX, VMAX, TMAX)，收到 {limit!r}")
        # 软限位自己得先自洽：下界不能高于上界。
        # 反了的话 `_apply_soft_limit` 的两条判断会**交替命中**，命令被钉死在一个
        # 两点集合上（q < min 钳到 min；否则若 q > max 钳到 max），发什么都不是你要的，
        # 而且一声不响 —— 配置写错就该在构造时炸，别等运行期表现成诡异行为。
        if (position_min is not None and position_max is not None
                and position_min > position_max):
            raise ValueError(
                f"软限位反了：position_min={position_min:g} > position_max={position_max:g}。\n"
                f"  后果：命令会被钉死在 {{min, max}} 这两个点上 —— 想要 0 得到 min，"
                f"想要别的得到另一个，**你怎么发都不是你要的位置**，而且不报错。\n"
                f"  注意 direction={self.direction:+d} 只影响换算到电机侧的符号，"
                f"**不影响这个比较**（两个都是关节侧）。"
            )

        # 软限位（关节侧）换算后必须落在 ±PMAX（电机侧）之内。
        # 为什么在**构造时**拒绝，而不是等运行期表现成怪数字：
        #   · 软限位在 PMAX 之外 ⇒ 电机**根本到不了**那个位置，这条限位是虚的，
        #     永远不会按写下的值生效 —— 你以为设了保护，其实没设
        #   · 而且一次命令会被 软限位 与 PMAX **两道钳位各计一次**，
        #     `clamped_count` 就不再是「多少条命令被改过」，这个计数是给人排查用的，
        #     数字不可信比没有数字更糟
        # 放在 add_motor **之前**：构造失败就不该在 bus 上留下注册（先校验、后改动）。
        p_max = limit[0]
        for label, val in (("position_min", position_min),
                           ("position_max", position_max)):
            if val is None:
                continue
            q_motor = self.direction * float(val) + self.offset
            if not (-p_max <= q_motor <= p_max):
                raise ValueError(
                    f"{label}={val:g}（关节侧）换算到电机侧是 {q_motor:+.4f} rad，"
                    f"超出 PMAX=±{p_max:g} 了。\n"
                    f"  换算：电机侧 = direction({self.direction:+d}) × 关节侧 "
                    f"+ offset({self.offset:g})\n"
                    f"  · 电机**到不了**这个位置 ⇒ 这条软限位是虚的，不会按你写的值生效\n"
                    f"  · 而且一次命令会被 软限位 + PMAX **两道钳位各计一次**，"
                    f"clamped_count 就不再是「多少条命令被改过」\n"
                    f"  要么把软限位收进 ±PMAX 之内，要么先确认 PMAX 本身对不对"
                    f"（要用 0x15 的回读值，别写死 —— 4310 与 4340P 档位不同）"
                )

        existing = bus.motors().get(motor_id)
        if existing is None:
            bus.add_motor(motor_id, limit)
        elif existing != limit:
            raise ValueError(
                f"电机 0x{motor_id:02X} 在 bus 上已注册为 {existing}，"
                f"但 Joint 收到 {limit} —— 同一个电机出现了两个映射范围。\n"
                f"  用错档位解出来的力矩会差 2.8 倍而且不报错，所以这里直接拒绝。\n"
                f"  要么统一（`bus.set_limit()`），要么先跑 scan_bus 确认哪个是真的。"
            )
        self.limit = limit

        # 计数（给控制循环/复盘用）
        self.n_mit = 0
        self.n_enable = 0
        self.n_disable = 0
        self.clamped_count = 0      # 被钳位的命令次数（含软限位与 PMAX）
        self.saturated_count = 0    # MIT 增益/前馈超出满量程的次数
        self._last_warn_t = -1e9    # 警告节流用

    # ───────────────────────── 坐标换算 ─────────────────────────
    # 电机侧 = direction * 关节侧 + offset
    # 关节侧 = direction * (电机侧 - offset)      （direction² == 1）
    # 速度/力矩是矢量，只乘 direction、不加 offset。
    def _pos_to_motor(self, q: float) -> float:
        return self.direction * float(q) + self.offset

    def _pos_to_joint(self, q: float) -> float:
        return self.direction * (float(q) - self.offset)

    def _vec_to_motor(self, v: float) -> float:
        return self.direction * float(v)

    def _vec_to_joint(self, v: float) -> float:
        return self.direction * float(v)

    # ───────────────────────── 警告 ─────────────────────────
    def _warn(self, msg: str) -> None:
        """打一条警告到 stderr。**每秒最多一条** —— 控制循环是 200~500Hz，
        不节流会把终端刷爆，反而看不见别的信息。**计数不受节流影响**。"""
        now = time.monotonic()
        if now - self._last_warn_t >= 1.0:
            self._last_warn_t = now
            print(f"[Joint {self.name}] ⚠ {msg}", file=sys.stderr)

    def _emergency_disable(self) -> None:
        """尽力失能。**自己的异常一律吞掉** —— 它是在已经出错时被调用的，
        再抛就会把原始异常盖掉。但失能失败必须喊出来：人得知道该去断电。"""
        try:
            self.bus.send_disable(self.motor_id)
            self.n_disable += 1
        except Exception as e:  # noqa: BLE001 —— 这里就是要吞掉一切
            print(f"\n[Joint {self.name}] ‼ **失能失败**：{e}\n"
                  f"    电机可能仍在通电出力 —— 手边有急停/断电开关吗？请直接断电。",
                  file=sys.stderr)

    @contextlib.contextmanager
    def _fail_safe(self):
        """出错先失能再抛异常（用户确认的语义）。

        只护住**发送**这一步：钳位算错是纯软件问题，不该让电机失能；
        而写串口失败意味着这条链路已经不可信了，继续让电机带电就是赌博。
        """
        try:
            yield
        except Exception:
            self._emergency_disable()
            raise

    # ───────────────────────── 钳位 ─────────────────────────
    def _clamp(self, q_motor: float) -> float:
        """**电机侧**硬钳位到 ±PMAX。这是物理/电气上的真界限，必须有。

        钳位发生在**换算之后**：软限位是关节侧的人为约定，PMAX 是电机侧的硬事实，
        所以顺序是 软限位(关节侧) → 换算 → PMAX(电机侧)。

        ⚠️ 超范围**钳位并打警告**（不抛异常）：7 关节满载时，一个关节越界把整条
        控制循环炸掉，比走到边界上更糟。但越界一定看得见 —— 计数 + 限速日志。
        """
        p_max = self.limit[0]
        if q_motor < -p_max:
            out = -p_max
        elif q_motor > p_max:
            out = p_max
        else:
            return q_motor
        self.clamped_count += 1
        self._warn(f"位置命令 {q_motor:+.4f} rad 超出电机量程 ±{p_max:g}，已钳到 {out:+.4f}"
                   f"（累计第 {self.clamped_count} 次；同类警告每秒最多一条）")
        return out

    def _apply_soft_limit(self, q_joint: float) -> float:
        """**关节侧**软限位。`position_min/max` 为 None 就是不检查（本轮默认留空）。

        ⚠️ 判断"到底钳没钳"用的是**显式标志**（`hit is None`），不是 `out != q_joint`。
        `!=` 那种写法在 NaN 上会翻车：`nan != nan` 为 True，于是**没设限位也会记一次
        钳位**、还打出 `超出软限位 [None, None]，已钳到 +nan` 这种假警告 ——
        `clamped_count` 是给人排查用的，被假数污染比没有这个数更糟。
        """
        hit = None
        if self.position_min is not None and q_joint < self.position_min:
            hit = self.position_min
        elif self.position_max is not None and q_joint > self.position_max:
            hit = self.position_max
        if hit is None:
            return q_joint
        self.clamped_count += 1
        self._warn(f"位置命令 {q_joint:+.4f} rad 超出软限位 "
                   f"[{self.position_min}, {self.position_max}]，已钳到 {hit:+.4f}"
                   f"（累计第 {self.clamped_count} 次）")
        return hit

    # ───────────────────────── 使能 / 失能 ─────────────────────────
    def enable(self) -> None:
        """使能，并**立刻补一帧零力矩**把"已使能但没命令"的窗口压到最小。

        零力矩帧 = `kp=0, kd=0, tau=0` —— 增益全零，所以 `q` 给什么都不出力
        （`t_u` 也从 0 映射到 0 N·m）。这个时序抄的是 `cmd_jog_mit` 的 `[1]` 步。

        ⚠️ **本方法不检查 `CTRL_MODE`**。零力矩帧只在 MIT 模式下是这个含义；
        别的模式下这 8 个字节会被解释成完全不同的东西。切模式是寄存器 I/O，
        不在本层（见 `switch_mode` 的说明）。

        ⚠️ 使能前请先 `poll()` 拿到**新鲜**状态。若缓存里是故障态（ERR 非 0/1）会拒绝
        使能 —— ERR=13 是**锁存**的，只能断电清；但如果你刚断过电而没重新 poll，
        缓存里那个故障是**过期**的，这时会误拒（保守方向，安全）。
        """
        st = self.bus.get_state(self.motor_id)
        if st is not None and not st.enabled and st.err not in ERR_OK:
            raise RuntimeError(
                f"电机 0x{self.motor_id:02X} 处于故障态 ERR={st.err}（{st.err_text}），"
                f"拒绝使能。\n"
                f"  · ERR=13 通讯丢失是**锁存**的，enable / 连发帧 / 写 0x09=0 都清不掉，"
                f"只能给电机断电再上电\n"
                f"  · 若刚断过电：先 `bus.poll()` 刷新缓存，这条判断用的是缓存里的旧状态"
            )
        with self._fail_safe():
            self.bus.send_enable(self.motor_id)
            self.n_enable += 1
            # 零力矩帧。写在这里而不是让调用方补 —— 这是使能时序的一部分，
            # 分两步就有漏掉的机会。send_mit 直接调以绕开 _fail_safe 的嵌套。
            self.bus.send_mit(self.motor_id, 0.0, 0.0, 0.0, 0.0, 0.0)
            self.n_mit += 1

    def disable(self) -> None:
        """失能（0xFD）。**总是可以安全调用** —— 没使能时发它也无害。

        不复用 `_emergency_disable`：这里是正常路径，失能失败应该**抛出来**，
        而不是吞掉。异常路径才需要吞（见 `_emergency_disable`）。
        """
        self.bus.send_disable(self.motor_id)
        self.n_disable += 1

    # ───────────────────────── 控制 ─────────────────────────
    def set_mit(self, kp: float, kd: float, q: float,
                dq: float = 0.0, tau: float = 0.0) -> None:
        """MIT：发**一帧** PD + 力矩前馈命令。`q`/`dq` 是关节侧。

        ⚠️ **发一帧 ≠ 走到位。** 电机只按最后一帧出力矩 `kp·(q_des−q)+kd·(dq_des−dq)+tau_ff`，
        所以要让关节停在 `q` 就得**持续发**（控制循环的活）。发完就不管，电机会一直
        按这一帧出力矩直到收到下一帧或看门狗超时。

        ⚠️ 稳态残差有上界 ≈ 摩擦/kp，压不到 0。要零稳态误差得用 POS_VEL（本轮是桩）。

        `kp`/`kd`/`dq`/`tau` 超出满量程时**会被静默钳位**（`float_to_uint` 的行为）——
        所以这里自己先查一遍并打警告：要 20 N·m 实际只给到 TMAX，臂会软趴趴地掉下来，
        而你不会知道为什么。`q` 走 `_apply_soft_limit` → 换算 → `_clamp`(PMAX)。
        """
        self._require_finite(kp=kp, kd=kd, q=q, dq=dq, tau=tau)
        self._check_saturation(kp, kd, dq, tau)
        q_motor = self._clamp(
            self._pos_to_motor(self._apply_soft_limit(float(q))))
        with self._fail_safe():
            self.bus.send_mit(self.motor_id, kp, kd, q_motor,
                              self._vec_to_motor(dq), self._vec_to_motor(tau))
            self.n_mit += 1

    def _require_finite(self, **named) -> None:
        """命令里的数值必须是**有限数**。NaN / ±inf 一律当**纯软件错**当场抛。

        为什么要在这一层拦，而不是让它一路走到 `float_to_uint` 再炸：

        · **NaN 会穿过两道钳位** —— `nan < -PMAX` 和 `nan > +PMAX` 都是 False，
          于是 `_apply_soft_limit` 与 `_clamp` 都原样放行。它一直走到
          `float_to_uint` 的 `int(nan)` 才抛，而**那一层在 `_fail_safe` 里面** ⇒
          触发「先失能再抛」。但同样是纯软件错的 `q=None` 却不失能（它在外层就炸了）。
          同一个类别的错，两种相反的处置，而且失能那条是**危险**的方向
          —— 重力负载下关节会直接掉。
        · **±inf 更阴**：`_clamp` 会把它悄悄钳成 ±PMAX，看起来"一切正常"，
          实际是拿一个算错的数去驱动电机。

        非数（比如 None / 字符串）在这里会自然抛 `TypeError`，那也正是我们要的。
        """
        bad = [f"{k}={v!r}" for k, v in named.items() if not math.isfinite(v)]
        if bad:
            raise ValueError(
                "命令里有非有限数（NaN / ±inf）：" + "；".join(bad) + "\n"
                "  · 这是**纯软件错**（上位机插值 / 标定 / 话题里传进来的），"
                "所以**不发失能帧** —— 让带电的电机松掉，比重力负载下掉下来强\n"
                "  · NaN 还会**穿过两道钳位**（比大小的结果都是 False），"
                "非在这里拦不可\n"
                "  · 查一下：目标位置是怎么算出来的？除以 0、log(负数)、"
                "没初始化的变量、还是标定文件里就是空的？"
            )

    def _check_saturation(self, kp, kd, dq, tau) -> None:
        """MIT 的四个量都有满量程，超了会被静默钳位 —— 这里替它出声。

        kp∈[0,500]、kd∈[0,5] 是**协议硬编码**的；dq/tau 的满量程是电机自己的
        VMAX/TMAX（每台不同，所以按本关节注册的 limit 查）。
        """
        p_max, v_max, t_max = self.limit
        bad = []
        if not (0.0 <= kp <= KP_FULL):
            bad.append(f"kp={kp:g} 不在 [0, {KP_FULL:g}]")
        if not (0.0 <= kd <= KD_FULL):
            bad.append(f"kd={kd:g} 不在 [0, {KD_FULL:g}]")
        if not (-v_max <= dq <= v_max):
            bad.append(f"dq={dq:g} 超出 ±VMAX({v_max:g})")
        if not (-t_max <= tau <= t_max):
            bad.append(f"tau={tau:g} 超出 ±TMAX({t_max:g})")
        if bad:
            self.saturated_count += 1
            self._warn("将静默钳位：" + "；".join(bad)
                       + f"（累计第 {self.saturated_count} 次）")

    # ───────────────────────── 预留桩（本轮不实现）─────────────────────────
    def set_pos_vel(self, pos: float, vlim: float) -> None:
        """🔒 预留：POS_VEL 位置+速度上限。**本轮未实现**。

        为什么没实现：POS_VEL 只在电机 `CTRL_MODE == 2` 时被认，而 2026-09-23 只读
        实测 0x01 与 0x04 的 `CTRL_MODE`(0x0A) **都是 1 (MIT)**。切成 2 是一次
        **寄存器写入**（`dm_registers.py --commit`），要单独批准、单独复盘。

        好在 0x0A 是 **RAM 不是 flash** ⇒ 断电就回到 MIT，不是不可逆的改动。

        落地时要一起做的事（别只改这一行）：
          · 固件 PID 寄存器 `KP_ASR/KI_ASR/KP_APR/KI_APR` **至今没在真机上读过**，
            出厂值是多少、合不合适，都是空白
          · POS_VEL 下**上位机无法限力矩**，力矩只受电机侧 0x03(过流)/0x17(TMAX) 约束
            （design.md:212）—— 这是 POS_VEL 相对 MIT 丢掉的那道保险
        """
        raise NotImplementedError(
            "POS_VEL 本轮是预留接口。它需要先写 CTRL_MODE(0x0A)=2 —— "
            "实测当前是 1(MIT)，而切模式是寄存器写入，需单独批准。"
            "现在可用的是 MIT：`set_mit(kp, kd, q, dq, tau)`。"
        )

    def set_force_pos(self, pos: float, vel: float, current: float) -> None:
        """🔒 预留：力位混控。**本轮未实现**（依赖 POS_VEL）。"""
        raise NotImplementedError(
            "力位混控本轮是预留接口，它建立在 POS_VEL 之上，而 POS_VEL 还没落地。"
            "现在可用的是 MIT：`set_mit(kp, kd, q, dq, tau)`（MIT 能直接给力矩前馈，"
            "要重力补偿的话先用它）。"
        )

    def switch_mode(self, mode: int) -> None:
        """🔒 预留：切控制模式。**本轮未实现** —— 这是寄存器 I/O，不属本层。"""
        raise NotImplementedError(
            "切控制模式 = 写寄存器 0x0A，是 RegisterTool 的活（`dm_registers.py`），"
            "而本层（MotorBus/Joint）**不做任何寄存器 I/O** —— 写一次要 ~150ms "
            "且期间发不出帧，运行期绝不能做。"
        )

    # ───────────────────────── 读 ─────────────────────────
    def get_state(self) -> JointState | None:
        """关节侧的最近一帧状态。**没收到过就返回 None**（不是造一个零值 —— 那会让
        调用方以为电机在 0 位）。

        数据来自 `MotorBus` 的缓存，所以新鲜度取决于调用方有没有在 `poll()`。
        """
        m: MotorState | None = self.bus.get_state(self.motor_id)
        if m is None:
            return None
        return JointState(
            name=self.name,
            position=self._pos_to_joint(m.pos),
            velocity=self._vec_to_joint(m.vel),
            torque=self._vec_to_joint(m.tau),
            err=m.err,
            temp_mos=m.temp_mos,
            temp_rotor=m.temp_rotor,
            vbus=None,              # 见 JointState 的注释：要读寄存器，本层不做
            enabled=m.enabled,
            timestamp=m.timestamp,
        )

    def assert_healthy(self) -> JointState | None:
        """查缓存状态，**有故障就"先失能再抛"**（用户确认的语义）。返回状态或 None。

        控制循环里每圈调一次：

            j.set_mit(...)
            j.assert_healthy()      # ERR 跳变 → 失能 + 抛异常，循环自然停下来

        ⚠️ 它看的是**缓存**。要让这条检查有意义，循环里必须有 `bus.poll()` 在跑 ——
        否则永远是同一个旧状态，等于没查。
        """
        st = self.get_state()
        if st is None:
            return None
        if st.err not in ERR_OK:
            self._emergency_disable()
            raise RuntimeError(
                f"关节 {self.name}（电机 0x{self.motor_id:02X}）故障 "
                f"ERR={st.err}（{self.bus.get_state(self.motor_id).err_text}）"
                f"，已发送失能。\n"
                f"  · ERR=13 通讯丢失是锁存的，要断电才能清\n"
                f"  · 失能只是让电机松掉，**不会让它回零/停下** —— 重力负载下它会掉"
            )
        return st

    def stats(self) -> dict:
        """本关节的计数。`clamped_count`/`saturated_count` 不为 0 就说明有东西被悄悄改了。"""
        return {
            "name": self.name,
            "motor_id": f"0x{self.motor_id:02X}",
            "limit": self.limit,
            "direction": self.direction,
            "offset": self.offset,
            "n_mit": self.n_mit,
            "n_enable": self.n_enable,
            "n_disable": self.n_disable,
            "clamped_count": self.clamped_count,
            "saturated_count": self.saturated_count,
        }

    def __repr__(self) -> str:
        return (f"Joint({self.name}, motor=0x{self.motor_id:02X}, "
                f"limit={self.limit}, dir={self.direction:+d}, offset={self.offset:g})")
