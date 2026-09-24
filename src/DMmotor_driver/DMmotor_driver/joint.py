#!/usr/bin/env python3
"""joint.py —— Joint：单个电机的完整控制类（DESIGN.md §四 / §4.1）

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
- ❌ **不写任何寄存器**、**不设零位**、**不切控制模式**（`switch_mode` 永远抛；
  `declare_mode` 只改本地声明，不写寄存器）。
- ❌ **不 poll**。读是调用方的事 —— 满载时 `MotorBus.poll()` 在独立的收循环里跑（D2）。

## 三种控制模式都实现了（帧不同，靠 `mode=` 分岔）

| 方法 | 帧 / CAN ID | 前置条件 |
|---|---|---|
| `set_mit()` | MIT 帧，CAN ID = **`id`** | `0x0A == 1` |
| `set_pos_vel()` | 位置速度帧，CAN ID = **`0x100+id`** | `0x0A == 2` |
| `set_force_pos()` | 力位混控帧，CAN ID = **`0x300+id`** | `0x0A == 4`（**不是 3**） |

**`0x0A` 本层读不到**（寄存器 I/O 不在本层），所以模式是**声明**的：构造时给
`mode=MODE_*`，切完模式再 `declare_mode()`。声明错了不会报错 —— 错帧会被电机
**静静丢掉**。每个 `set_*()` 都会先查一次声明（`_require_mode`），把"静默无效"
变成"当场抛"，但**它核对不了硬件**。

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
    from DMmotor_driver.dm_frames import ERR_OK, build_tx, float_to_uint8s
except ImportError:  # 裸脚本直跑：parents[1] 就是 src/DMmotor_driver
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
    from DMmotor_driver.dm_bus import MotorBus, MotorState
    from DMmotor_driver.dm_frames import ERR_OK, build_tx, float_to_uint8s

# MIT 定点映射的满量程（`dm_frames.float_to_uint` 的钳位边界，硬编码在协议里）。
# ⚠️ 超范围不会报错 —— `float_to_uint` 会**静默钳位**。所以下面 set_mit 自己查一遍。
KP_FULL = 500.0
KD_FULL = 5.0

# ───────────────────────── 控制模式（寄存器 0x0A）─────────────────────────
# 取自**手册**的「模式切换」表，不是 SDK 的枚举（SDK 里根本没列这张表）：
#
#     | 编码 | 模式 |
#     | 1 | MIT |
#     | 2 | 位置速度 |
#     | 3 | 速度 |
#     | 4 | 力位混控 |
#
# ⚠️ **力位混控是 4 不是 3** —— 3 是速度模式。按 3 去配力位混控，电机进的是速度模式，
# 而速度模式的帧是 CAN ID `0x200+id`，你发的 `0x300+id` 它**根本不看** ⇒ 静默不动。
MODE_MIT = 1
MODE_POS_VEL = 2
MODE_VEL = 3
MODE_FORCE_POS = 4
MODE_NAMES = {MODE_MIT: "MIT", MODE_POS_VEL: "位置速度", MODE_VEL: "速度",
              MODE_FORCE_POS: "力位混控"}

# 使能后补的那一帧"保持"用什么速度上限。
# 手册：位置速度模式下 v_des 是**梯形加减速的匀速段速度**，不是"立刻达到的速度"，
# 所以给小值不会限制住正常运动，只会在真的需要挪动时慢一点。
HOLD_VLIM = 0.5          # rad/s
# 力位混控保持帧的电流标幺上限（1.0 = 满量程）。给 0 就是"不额外给力矩"。
HOLD_I_PU = 0.0

# 力位混控帧里 v_des / i_des 的放大倍数与协议上限（手册「力位混控模式下控制帧」）。
_FP_V_SCALE = 100.0      # v_des：rad/s × 100，uint16，上限 10000 ⇒ 实际 0~100 rad/s
_FP_I_SCALE = 10000.0    # i_des：标幺值 × 10000，uint16，上限 10000 ⇒ 实际 0~1.0
_FP_U16_MAX = 10000


def _force_pos_frame(slave_id: int, pos: float, vlim: float, i_pu: float) -> bytes:
    """力位混控帧（手册「力位混控模式下控制帧」）。

        CAN ID = 0x300 + slave_id
        D[0:4] = float32(p_des)          低位在前
        D[4:6] = uint16(v_des × 100)     低位在前，上限 10000
        D[6:8] = uint16(i_des × 10000)   低位在前，上限 10000

    ⚠️ **本函数放错了层**：按 DESIGN.md §1.2，协议原语应该全在 `dm_frames.py`，
    那里也确实有 `pos_vel_frame` / `mit_frame` / `cmd_frame`，唯独没有力位混控帧。
    这次只允许改 `joint.py`，所以先落在这里；下次动 `dm_frames.py` 时**搬过去**。

    ⚠️ 两个 uint16 用 **`int()` 截断**，不是 `round()` —— 与 SDK 的 `np.uint16(x)`
    以及 `dm_frames.float_to_uint` 保持一致（那里也是截断，注释里写明了）。
    差 1 个 LSB 换不来什么，但"和厂商行为不一致"会让整帧对拍失去意义。
    """
    v = int(max(0.0, min(float(vlim), _FP_U16_MAX / _FP_V_SCALE)) * _FP_V_SCALE)
    i = int(max(0.0, min(float(i_pu), 1.0)) * _FP_I_SCALE)
    data = (bytes(float_to_uint8s(float(pos)))
            + v.to_bytes(2, "little")
            + i.to_bytes(2, "little"))
    return build_tx(0x300 + slave_id, data)


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
                 position_max: float | None = None,
                 mode: int = MODE_MIT):
        """
        参数:
            bus        : 已注册好的 `MotorBus`（本类不 open/close 它）
            motor_id   : 电机 SlaveID（使能/失能/MIT 帧的 CAN ID 就是它本身）
            limit      : (PMAX, VMAX, TMAX)。**必须来自 0x15/0x16/0x17 的回读值** ——
                         4310 与 4340P 的档位不同，用错档位解出来的力矩差 2.8 倍
                         **而且不报错**（DESIGN.md D5）。
            direction  : +1 / -1。电机正转方向与关节正方向相反时给 -1
            offset     : 关节零位对应的电机读数（电机侧 rad）
            position_min/max : 软限位（**关节侧**）。None = 不检查（本轮默认留空）。
                              **换算到电机侧后必须落在 ±PMAX 之内**，否则构造时就抛 ——
                              写在 PMAX 之外的软限位是虚的（电机到不了），还会让
                              `clamped_count` 一次命令记两次（见校验处的注释）
            mode       : **你声明的**控制模式，取值见 `MODE_*` 常量（0x0A 的实际值）。
                         默认 `MODE_MIT`。

        ⚠️ `mode` 是**声明，不是读数**。本层不做寄存器 I/O，**读不到 0x0A**，所以它
        唯一的作用是"你告诉 Joint 电机现在是什么模式"。**它永远不会去核对。**
        声明错了会怎样：位置类模式下 `enable()` 补的那一帧"保持"用的是位置命令，
        而真 MIT 的电机不认这个 CAN ID ⇒ **那帧石沉大海**，电机处在"已使能但没命令"
        的窗口里。反之亦然（真 POS_VEL 却声明成 MIT ⇒ `enable()` 补的是零力矩 MIT 帧，
        同样送不到 ⇒ 电机内部目标还是 0 ⇒ **朝零位冲**）。

        所以切完模式**必须立刻**把这里改成对应的值：
        `dm_registers.py set --id 0xNN --rid 0x0A --value 2 --commit`
        （0x0A 是 **RAM 不是 flash**，断电回默认；不写 `--save` 就不会进 flash）
        """
        if direction not in (1, -1):
            raise ValueError(f"direction 只能是 +1 或 -1，收到 {direction!r}")
        # 只认手册「模式切换」表里那四个。写个 5 进来多半是打错 —— 而模式号打错的
        # 后果是"帧发出去没人看"，不是报错。
        if mode not in MODE_NAMES:
            raise ValueError(
                f"mode 只能是 {sorted(MODE_NAMES)}（手册「模式切换」表），收到 {mode!r}。\n"
                f"  1=MIT　2=位置速度　3=速度　4=力位混控 —— **力位混控是 4 不是 3**，"
                f"配成 3 电机进的是速度模式，你发的控制帧它根本不看。"
            )

        self.bus = bus
        self.motor_id = motor_id
        self.name = name or f"0x{motor_id:02X}"
        self.mode = mode          # 声明，不是读数 —— 见上面 mode 参数的说明
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
        self.n_pos_vel = 0
        self.n_force_pos = 0
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
        """使能，并**立刻补一帧"保持当前姿态"**把"已使能但没命令"的窗口压到最小。

        补什么**按 `self.mode` 分岔** —— 不同模式认的帧完全不同：

        | `self.mode` | 补的那一帧 | 为什么它安全 |
        |---|---|---|
        | `MODE_MIT` | MIT 零力矩（`kp=kd=tau=0`） | 增益全零 ⇒ 出力恒为 0，`q` 给什么都不动 |
        | `MODE_POS_VEL` | POS_VEL，`pos=当前位置`，`vlim=HOLD_VLIM` | 目标就是"现在在哪" ⇒ 不动 |
        | `MODE_FORCE_POS` | 力位混控，`pos=当前位置`，`vlim=HOLD_VLIM`，电流限 0 | 同上，且不给额外力矩 |
        | `MODE_VEL` | **拒绝使能** | 本轮没实现速度模式的保持帧，硬发一帧是拿电机赌 |

        ⚠️ **为什么位置类模式比 MIT 危险得多。** 手册「模式切换」一节明写：切进位置类
        模式时，电机**会把位置/速度指令清零** —— 也就是它内部的目标变回 **0**。
        此时如果使能了而没有立刻把目标改回"现在在哪"：

        > **电机会朝自己的零位满速冲过去，不减速、不看你在哪、不看你手在哪。**

        MIT 模式没有这个问题 —— 零增益帧本身就是"不出力"，不需要知道位置。
        所以本方法在位置类模式下**拿不到缓存状态就直接拒绝使能**：
        连"现在在哪"都不知道，就没有安全的第一帧可发。

        ⚠️ 本方法**不核对**电机真实的 `0x0A`（本层读不到寄存器，见 `mode` 参数的说明）。
        声明与实际不符时分岔会选错 —— 而错帧**会被电机忽略，不报错**。所以：
        切完模式，**立刻**把 `Joint(mode=...)` 也改掉，两件事不要分开做。

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

        if self.mode == MODE_VEL:
            raise RuntimeError(
                f"关节 {self.name} 声明为速度模式（mode=3），但本层没有速度模式的"
                f"\"保持\"帧 —— 拒绝使能。\n"
                f"  · 位置类模式靠\"目标=当前位置\"保持；速度模式的保持是 v=0，"
                f"  但那帧本轮没写，硬发一帧等于拿电机赌它的默认行为\n"
                f"  · 要用速度模式，先补 `set_vel()` 和对应的保持帧"
            )

        # 位置类模式：连"现在在哪"都不知道，就没有安全的第一帧 —— 宁可不使能。
        if self.mode in (MODE_POS_VEL, MODE_FORCE_POS) and st is None:
            raise RuntimeError(
                f"关节 {self.name} 声明为 {MODE_NAMES[self.mode]}（mode={self.mode}），"
                f"但缓存里没有状态 —— **拒绝使能**。\n"
                f"  · 切进位置类模式时，电机内部的位置指令被清零（手册「模式切换」），"
                f"目标变回 0\n"
                f"  · 使能后必须**立刻**把目标改回\"现在在哪\"，而它只能从反馈帧拿\n"
                f"  · 所以先 `bus.poll()`（或 `bus.wait_feedback({self.motor_id})`）读到位置，"
                f"再调 enable()\n"
                f"  · 这一步不能省 —— 省了就是让电机朝自己的零位冲"
            )

        with self._fail_safe():
            self.bus.send_enable(self.motor_id)
            self.n_enable += 1
            # 保持帧。写在这里而不是让调用方补 —— 这是使能时序的一部分，
            # 分两步就有漏掉的机会。
            self._send_hold(st)

    def _send_hold(self, st: MotorState | None) -> None:
        """使能后立刻补的"保持"帧。**按 `self.mode` 分岔**，理由见 `enable()`。

        直接调 `bus.send_*` 而**不走 `set_*`**：那几个方法会再查一遍软限位 / PMAX 钳位，
        而这里的输入是**电机自己报回来的实测位置** —— 钳它纯属多余，
        还可能把"现在在哪"改成"限位边上"，那就不是保持了。
        """
        if self.mode == MODE_MIT:
            # 增益全零 ⇒ t_u 从 0 映射到 0 N·m，q 给什么都不出力
            self.bus.send_mit(self.motor_id, 0.0, 0.0, 0.0, 0.0, 0.0)
            self.n_mit += 1
            return

        # enable() 已经保证过：位置类模式下 st 不可能是 None
        assert st is not None, "位置类模式下的保持帧需要状态，enable() 本该拦住这种情况"

        if self.mode == MODE_POS_VEL:
            self.bus.send_pos_vel(self.motor_id, st.pos, HOLD_VLIM)
            self.n_pos_vel += 1
            return

        self.bus.send_frame(_force_pos_frame(self.motor_id, st.pos,
                                             HOLD_VLIM, HOLD_I_PU))
        self.n_force_pos += 1

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

        ⚠️ 稳态残差有上界 ≈ 摩擦/kp，压不到 0。要零稳态误差得用 `set_pos_vel()`。

        ⚠️ **只在 `self.mode == MODE_MIT` 时发**。声明成别的模式还调它，会直接抛 ——
        因为 MIT 帧的 CAN ID 是 `slave_id`，而位置类模式电机只听 `0x100+slave_id`，
        这帧**送不到、也不报错**。与其让你以为在控制、其实什么都没发生，不如当场炸。

        `kp`/`kd`/`dq`/`tau` 超出满量程时**会被静默钳位**（`float_to_uint` 的行为）——
        所以这里自己先查一遍并打警告：要 20 N·m 实际只给到 TMAX，臂会软趴趴地掉下来，
        而你不会知道为什么。`q` 走 `_apply_soft_limit` → 换算 → `_clamp`(PMAX)。
        """
        self._require_mode(MODE_MIT, "MIT")
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

    def _require_mode(self, expected: int, what: str) -> None:
        """发帧前查一次**声明的**模式。**这不是核对硬件** —— 是防你自己写错。

        本层读不到寄存器 `0x0A`，唯一依据就是构造时那个 `mode=`。它的价值在于：
        每个模式认的 CAN ID 不同（MIT=`id`、位置速度=`0x100+id`、速度=`0x200+id`、
        力位混控=`0x300+id`），**发错 ID 的帧会被电机静静丢掉** —— 不报错、不回应。
        你会以为在控制，其实什么都没发生。

        把这种"静默无效"变成"当场抛"，是这一层唯一能做的自检。
        """
        if self.mode != expected:
            raise RuntimeError(
                f"关节 {self.name} 声明为 {MODE_NAMES[self.mode]}（mode={self.mode}），"
                f"但你要发的是「{what}」帧 —— 模式不对，拒绝发送。\n"
                f"  · 本层读不到寄存器 0x0A，只能信你声明的 `mode`\n"
                f"  · 各模式的 CAN ID：MIT=id　位置速度=0x100+id　速度=0x200+id　"
                f"力位混控=0x300+id。发错 ID 的帧会被电机**静默丢掉**\n"
                f"  · 真换了模式就跑：`dm_registers.py set --id 0x{self.motor_id:02X} "
                f"--rid 0x0A --value <n> --commit`，然后 `declare_mode(<n>)`"
            )

    def declare_mode(self, mode: int) -> None:
        """告诉这个 `Joint`：电机现在的 `0x0A` **已经是** `mode` 了。只改本地声明。

        ⚠️ 名字是 declare 不是 switch —— **它一个字节都不写**。真正切模式的是
        `dm_registers.py`（寄存器 I/O 不在本层）。这个方法存在的唯一理由是：
        切完模式之后 `Joint` 得知道该发哪种帧。

        切模式前后**必须**按这个顺序做（手册「模式切换」）：

          1. 电机**失能**、**零速** —— 切进位置类模式时电机内部指令会被清零
          2. `poll()` / `wait_feedback()` 拿到**当前位置**（位置类模式必须先知道"现在在哪"）
          3. 跑 `dm_registers.py ... --rid 0x0A --value <n> --commit`
          4. `declare_mode(<n>)` ← 就这一步
          5. 再 `enable()`（它会按新模式补对应的保持帧）

        第 1、2 步不是形式 —— 手册原话：*「由一种模式切换到位置控制的模式时，
        为防止冲击，建议先读取精确的位置后，再考虑切换，尽量在电机零速的时候进行切换。」*
        """
        if mode not in MODE_NAMES:
            raise ValueError(f"mode 只能是 {sorted(MODE_NAMES)}，收到 {mode!r}")
        self.mode = mode

    # ───────────────────────── 位置速度 / 力位混控 ─────────────────────────
    def _check_vlim(self, vlim: float, what: str) -> None:
        """`vlim` 的合法区间。

        · **负值当场抛** —— 那是调用方的 bug（想反方向应该改 `pos`）
        · 超过协议上限只**警告** —— 电机会自己钳到 100 rad/s，但那样你就不知道
          自己写的数没生效
        """
        vlim_full = _FP_U16_MAX / _FP_V_SCALE          # = 100.0 rad/s（手册的协议上限）
        if vlim < 0:
            raise ValueError(
                f"{what} 的 vlim={vlim:g} 是负的。速度上限只能是 0~{vlim_full:g} rad/s。\n"
                f"  要往反方向走是**改 pos**，不是把 vlim 写负 —— 负值在协议里是"
                f"无符号 16 位，不报错，只会变成一个你不认识的速度。"
            )
        if vlim > vlim_full:
            self.saturated_count += 1
            self._warn(f"{what} 的 vlim={vlim:g} 超过协议上限 {vlim_full:g} rad/s，"
                       f"电机会钳到 {vlim_full:g}"
                       f"（累计第 {self.saturated_count} 次）")

    def set_pos_vel(self, pos: float, vlim: float) -> None:
        """POS_VEL：位置 + 速度上限。**本机主用的控制律**（固件闭环，稳态误差能到 0）。

        `pos` 是**关节侧**。`vlim`（rad/s）是**梯形加减速的匀速段速度** —— 手册原话，
        不是"立刻达到的速度"，所以它不会让起步变猛，只在需要挪远时限制巡航速度。

        和 `set_mit` 的区别：这是**电机自己闭环**，所以持续发帧的意义是"更新目标"，
        不是"维持力矩"。发一帧就到那儿了（在 vlim 和固件 PID 的约束下）。

        ⚠️ **前置条件：电机的 `0x0A` 必须真是 2。** 本层读不到它（见 `_require_mode`），
        只能信你声明的 `self.mode`。声明成 2 而实际是 1 时，这帧被电机静静丢掉 ——
        你以为在控制，其实什么都没发生。

        ⚠️ **POS_VEL 丢掉了 MIT 的那道保险：上位机限不了力矩。** 力矩只受电机侧
        `0x03`(过流) 与 `0x17`(TMAX) 约束（DESIGN.md §2.2）。**带负载时这条最要命** ——
        顶到东西时 POS_VEL 会一直出力直到过流保护；MIT 至少还能把 kp 收小。
        要主动限力就用 `set_force_pos()`。
        """
        self._require_mode(MODE_POS_VEL, "POS_VEL")
        self._require_finite(pos=pos, vlim=vlim)
        self._check_vlim(vlim, "POS_VEL")
        q_motor = self._clamp(self._pos_to_motor(self._apply_soft_limit(float(pos))))
        with self._fail_safe():
            self.bus.send_pos_vel(self.motor_id, q_motor, float(vlim))
            self.n_pos_vel += 1

    def set_force_pos(self, pos: float, vel: float, current: float) -> None:
        """力位混控（EMIT）：位置 + 限速 + **扭矩电流上限**。

        这是**唯一能主动限力矩的位置模式** —— POS_VEL 限不了，MIT 得自己算前馈。
        带负载、抓取、怕撞的时候用它。

        参数（单位按手册「力位混控模式下控制帧」）：
            pos     : 期望位置，**关节侧** rad
            vel     : 限速值，rad/s。协议里放大 100 倍存 uint16，范围 0~100
            current : **扭矩电流限定标幺值 0~1.0**（实际电流 ÷ 最大相电流），
                      **不是安培** —— 手册没给最大相电流的数值，所以只能给标幺值

        ⚠️ 需要 `0x0A == 4`。**力位混控是 4 不是 3**（3 是速度模式）—— 配成 3 的话
        电机进的是速度模式，认的是 CAN ID `0x200+id`，本帧发的 `0x300+id` 它根本不看。

        ⚠️ **电流上限 ≠ 力矩上限。** 力矩 = 电流 × 力矩常数，而这个常数手册没给。
        所以"限到 0.2 标幺"到底是多少 N·m，**只能实测标定**，别当成已知量用。
        """
        self._require_mode(MODE_FORCE_POS, "力位混控")
        self._require_finite(pos=pos, vel=vel, current=current)
        self._check_vlim(vel, "力位混控")
        if not (0.0 <= current <= 1.0):
            self.saturated_count += 1
            self._warn(f"电流标幺值 current={current:g} 不在 [0, 1]，电机会钳到区间内"
                       f"（累计第 {self.saturated_count} 次）")
        q_motor = self._clamp(self._pos_to_motor(self._apply_soft_limit(float(pos))))
        with self._fail_safe():
            self.bus.send_frame(_force_pos_frame(self.motor_id, q_motor, vel, current))
            self.n_force_pos += 1

    def switch_mode(self, mode: int) -> None:
        """🔒 **永远抛** —— 切模式是寄存器 I/O，不属本层。

        留这个方法只为了给一句能照着做的错误信息。真要切模式，见 `declare_mode()`
        里那五步；本方法一个字节都不写。
        """
        raise NotImplementedError(
            f"切控制模式 = 写寄存器 0x0A，而本层（MotorBus/Joint）**不做任何寄存器 I/O**。\n"
            f"  两个理由，都不是洁癖：\n"
            f"   · 写一次要 ~150ms，期间发不出帧 ⇒ 运行期绝不能做\n"
            f"   · 手册要求「零速、先读位置」再切 ⇒ 那是调用方的时序，一个方法保证不了\n"
            f"  真要切（以 0x{self.motor_id:02X} → 模式 {mode} 为例）：\n"
            f"    1. disable()，并确认已经零速\n"
            f"    2. bus.poll() 拿到当前位置\n"
            f"    3. pixi run python src/DMmotor_driver/DMmotor_driver/dm_registers.py \\\n"
            f"         set --id 0x{self.motor_id:02X} --rid 0x0A --value {mode} --commit\n"
            f"       （0x0A 是 **RAM 不是 flash**；不加 --save 就不会进 flash）\n"
            f"    4. declare_mode({mode})   ← 只改本地声明，不写寄存器\n"
            f"    5. enable()               ← 它会按新模式补对应的保持帧\n"
            f"  顺序不能换。手册原话：「由一种模式切换到位置控制的模式时，为防止冲击，\n"
            f"  建议先读取精确的位置后，再考虑切换，尽量在电机零速的时候进行切换。」"
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
            "mode": f"{self.mode}({MODE_NAMES[self.mode]})",
            "limit": self.limit,
            "direction": self.direction,
            "offset": self.offset,
            "n_mit": self.n_mit,
            "n_pos_vel": self.n_pos_vel,
            "n_force_pos": self.n_force_pos,
            "n_enable": self.n_enable,
            "n_disable": self.n_disable,
            "clamped_count": self.clamped_count,
            "saturated_count": self.saturated_count,
        }

    def __repr__(self) -> str:
        return (f"Joint({self.name}, motor=0x{self.motor_id:02X}, "
                f"mode={self.mode}({MODE_NAMES[self.mode]}), "
                f"limit={self.limit}, dir={self.direction:+d}, offset={self.offset:g})")
