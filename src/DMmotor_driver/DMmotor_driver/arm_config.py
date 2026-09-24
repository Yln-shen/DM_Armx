"""整臂配置层 —— **类与校验**（代码）。数据在 `config/rebotarm_b601_mixed.yaml`。

## 为什么拆成两个文件

`ARCHITECTURE.md:183/328` 定的分工，本文件照做：

  · 本文件            = 数据类 + 校验，**零第三方依赖，不 import yaml**
  · `config/…yaml`    = 数据（7 个电机 + 夹爪 + 安全阈值）

`import yaml` **只出现在 `from_yaml` 一个函数里**。这不是洁癖，是刻意的故障隔离：
pyyaml 在这个环境里是 `ros-jazzy` / `mujoco` 的**传递依赖**（没写进 `pixi.toml`），
哪天上游依赖变了它可能悄悄消失 —— 那时坏掉的**只有 YAML 这一个入口**，
数据类照用（`from_dict` 照样能喂数据进来）。否则整个配置层一起瘫。

## 规格来源（都是你文档里的，不是我发明的）

  · `DESIGN.md` §4.2  —— `JointConfig` 修订版（字段与校验）
  · `DESIGN.md` §4.3  —— 力矩阈值按型号分档（4310 峰值仅 12.5 N·m）
  · `DESIGN.md` §六    —— YAML 文件的形态
  · `DESIGN.md` §2.1  —— 硬件分配（ID / 型号 / PID / vlim）
  · `DESIGN.md` §2.2  —— 关节限位，**真源是 URDF，配置里只引用**
  · `DESIGN.md` §2.3  —— 夹爪线性换算（0.10 m ↔ -5.0 rad）
  · `TESTING.md` §2.8  —— `(PMAX,VMAX,TMAX)` **实测回读值**

## 三条硬边界（写在这里，改代码前先读）

  1. **本层不碰硬件**：不 open 串口、不发帧、不读寄存器。纯数据 + 纯校验。
  2. **本层不存映射范围**：`(PMAX,VMAX,TMAX)` 只作"标称值"用来**对拍回读结果**，
     **绝不作为发帧依据** —— 发帧一律用 `0x15/0x16/0x17` 的实测回读
     （`TESTING.md §2.8`／`§2.8`：用错档位解出来的力矩差 2.8 倍，**而且不报错**）。
  3. **`direction`/`offset` 默认 `+1`/`0.0` 是"未标定"的诚实表示**，不是标定值。
     `§2.1` 那句"默认值必须让'不标定即等于 reBot 行为'"就是这个意思。
     标定（§七）做完之前，**任何非 `+1`/`0.0` 的值都必须来自实测**。
"""
from __future__ import annotations

import xml.etree.ElementTree as ET
from dataclasses import dataclass, field
from pathlib import Path

# 仓库里那份默认配置的位置：src/DMmotor_driver/config/rebotarm_b601_mixed.yaml
DEFAULT_CONFIG_PATH = Path(__file__).resolve().parents[1] / "config" / "rebotarm_b601_mixed.yaml"


# ═══════════════════════════ 电机型号表 ═══════════════════════════
#
# ★ 这里**不复用**厂商 SDK 的 `DM_Motor_Type`（DM_CAN.py:643）。
#   原因：SDK 的 `DM4340 = 2` 指的是**普通 4340**，而台上这颗是 **4340P**（Gr=40），
#   两者在 SDK 里是同一个枚举值（`LESSONS.md §2.6 坑 2`「坑 2」）。拿它当型号标签会串档。
#   我们按**实物铭牌 + 实测**建档，只认 "4340P" / "4310" 两个字符串。

@dataclass(frozen=True)
class MotorSpec:
    """一个型号的**手册/实测标称值**。全部是"参考量"，不是发帧依据。"""

    label: str
    rated_torque: float          # 额定 N·m（手册）
    peak_torque: float           # 峰值 N·m（手册）—— 力矩阈值必须低于它，否则永不触发
    mapping_range: tuple[float, float, float]   # (PMAX, VMAX, TMAX) 实测回读，**只用于对拍**
    pos_kp_range: tuple[float, float]           # POS_VEL pos_kp 的合理档位（§4.2 防串档）
    torque_max_default: float
    monitor_default: float


MOTOR_SPECS: dict[str, MotorSpec] = {
    # 0x01–0x03 实测 (PMAX,VMAX,TMAX) = 12.5/10/28（§2.8，与 SDK 表 DM4340 一致）
    "4340P": MotorSpec(
        label="4340P",
        rated_torque=12.0,
        peak_torque=40.0,
        mapping_range=(12.5, 10.0, 28.0),
        pos_kp_range=(100.0, 200.0),
        torque_max_default=12.0,
        monitor_default=15.0,
    ),
    # 0x04–0x07 实测 = 12.5/30/10（§2.7 第 2 条，与 SDK 表 DM4310 一致）
    "4310": MotorSpec(
        label="4310",
        rated_torque=3.5,
        peak_torque=12.5,
        mapping_range=(12.5, 30.0, 10.0),
        pos_kp_range=(50.0, 100.0),
        torque_max_default=3.5,
        monitor_default=5.0,
    ),
}


def _check_type_gearing(label: str, motor_type: str, vlim: float, pos_kp: float) -> None:
    """型号 ↔ 档位一致性。`JointConfig` 和 `GripperConfig` 共用。

    抽出来是因为两个类各写一份的话，**报错信息会漂移** —— 一边说后果一边不说，
    用的人就得记两套。同一件事只该有一种说法。
    """
    spec = MOTOR_SPECS[motor_type]
    v_max = spec.mapping_range[1]
    if not (0 < vlim <= v_max):
        raise ValueError(
            f"{label}: vlim={vlim:g} 超出 {motor_type} 的映射 VMAX={v_max:g}\n"
            f"  · vlim 是 POS_VEL 帧里那个电机侧速度上限。超过 VMAX 就是虚的 —— "
            f"电机到不了那个速度，写大了只会让人以为限住了"
        )
    lo, hi = spec.pos_kp_range
    if not (lo <= pos_kp <= hi):
        raise ValueError(
            f"{label}: 型号 {motor_type} 的 pos_kp={pos_kp:g} 不在合理档位 "
            f"[{lo:g}, {hi:g}] 内\n"
            f"  · 十有八九是从另一个型号的关节那行复制过来的"
            f"（4340P 用 100~200，4310 用 50~100）"
        )


def check_mapping_range(motor_type: str, pmax: float, vmax: float, tmax: float,
                        *, tol: float = 0.05) -> list[str]:
    """把电机**回读**的 `(PMAX,VMAX,TMAX)` 和型号标称值对拍。返回差异说明（空列表 = 一致）。

    这是 `DESIGN.md` D5 那条坑（"用错档位力矩差 2.8 倍**而且不报错**"）在配置层的抓手：
    回读值和型号对不上，说明**配置里写的型号和你插上去的那颗不是同一个** ——
    要么改了 YAML，要么拔错了电机。此时应当停下来查，而不是继续发帧。

    用法（将来 `RegisterTool.verify_mapping` 里调）：

        diff = check_mapping_range(cfg.motor_type, *readback_0x15_0x16_0x17)
        if diff:
            raise RuntimeError("\\n".join(diff))

    ⚠️ 它**只报错，不自动改**。标称值(`MOTOR_SPECS`)和回读值谁对，得人来判断：
    标称值来自文档/实测记录，回读值来自**此刻这颗电机** —— 冲突时通常回读更可信，
    但**也**可能是回读路径本身错了（读串了 ID / 解码档位用错）。
    """
    spec = MOTOR_SPECS.get(motor_type)
    if spec is None:
        return [f"未知型号 {motor_type!r}，可选：{sorted(MOTOR_SPECS)}"]
    want = spec.mapping_range
    got = (float(pmax), float(vmax), float(tmax))
    out = []
    for name, w, g in zip(("PMAX", "VMAX", "TMAX"), want, got):
        if abs(g - w) > tol * max(abs(w), 1e-9):
            out.append(f"  {name}: 回读 {g:g} ≠ 型号 {spec.label} 标称 {w:g}")
    if out:
        out.insert(0, f"型号 {spec.label} 的映射范围和回读值对不上（{spec.label} 是不是写错了？）")
    return out


# ═══════════════════════════ 关节 ═══════════════════════════

@dataclass
class JointConfig:
    """单个关节。字段与校验按 `DESIGN.md` §4.2。

    注意**没有** `gear_ratio` / `pmax` / `vmax` / `tmax` —— §4.2 的修订版里没有它们：
      · 减速比不进换算（`D1`：换算只有 `direction` + `offset`）
      · 映射范围不落在配置里（`§161`：发帧用**回读值**，写死一份只会让人忘了去读）
    """

    name: str
    motor_type: str                    # "4340P" / "4310"
    slave_id: int                      # 0x01–0x07
    master_id: int                     # 0x11–0x17

    direction: int = 1                 # ±1，**标定得出**；默认 +1 = 未标定
    offset: float = 0.0                # rad，**标定得出**；默认 0.0 = 未标定

    position_min: float | None = None  # None = 从 URDF 取（§2.2）
    position_max: float | None = None

    velocity_max: float = 1.0          # rad/s，来自 MoveIt/URDF 限速（§2.2）

    # ── POS_VEL：本机唯一实际生效的控制律 ──
    pos_kp: float = 150.0
    pos_ki: float = 0.5
    vel_kp: float = 0.0125
    vel_ki: float = 0.004
    vlim: float = 5.0                  # 电机侧速度上限（POS_VEL 帧里那个）

    # ── MIT ──
    mit_kp: float = 120.0
    mit_kd: float = 8.0

    # ── 力矩 ──
    # ⚠️ POS_VEL 下**上位机无法限力矩**（力矩只受电机侧 0x03 OC_Value / 0x17 TMAX 约束）。
    #    所以 torque_max 只在 MIT/力位模式下生效；POS_VEL 下唯一的保护是
    #    torque_monitor（拿反馈帧里的力矩估读做阈值判断）。
    #
    # ★ 默认 None ⇒ **从 MOTOR_SPECS[motor_type] 派生**，不写死。
    #   为什么这两个必须派生：它们是**按型号定的**（§4.3），而写错的方向恰恰是
    #   **静默通过**的 —— 4310 上填 4340P 的 torque_max=12.0，12.0 < 4310 峰值 12.5，
    #   校验照样放行，但电机根本用不上那么大的力矩上限，等于这行白写。
    #   派生之后没有"抄错"的机会。显式给值仍然可以（夹爪就要给，它比 4310 小得多），
    #   给了就照给的校验。
    #   对照：`pos_kp` 这类**没有**派生，因为写错档位会被 pos_kp_range 当场抓住 ——
    #   "填错了能测出来"的字段不必派生，"填错了会静默通过"的字段才必须。
    torque_max: float | None = None             # 仅 MIT/力位模式生效；None = 按型号派生
    torque_monitor: bool = True
    torque_monitor_threshold: float | None = None   # None = 按型号派生
    torque_monitor_count: int = 10     # 连续 N 次越限才急停；采样率 = feedback_hz

    # ── 安全 ──
    motor_timeout_ms: int = 200        # 电机侧看门狗，寄存器 0x09

    # ── 摩擦（N·m，实测）──
    # **刻意写成区间，不是单个常数。** §2.8 的实测结论是：裸 4340P 的脱离力矩在
    # 0.564~0.706 N·m 之间摆动（22% 离散度），是**粘滑**，不是"静摩擦 = 一个定值"。
    # 存一个 0.62 会让人以为它是标定值 —— 而任何按常数设计的补偿/阈值都会在
    # 区间端点处失败。两个都留空 = **未测**（§823：7 台里目前只测了 0x01 和 0x04）。
    # 用它的人自己决定取哪一端：重力补偿取大端（否则推不动），力矩阈值取小端更保守。
    friction_min: float | None = None
    friction_max: float | None = None

    def __post_init__(self) -> None:
        n = self.name
        spec = MOTOR_SPECS.get(self.motor_type)
        if spec is None:
            raise ValueError(
                f"{n}: 未知电机型号 {self.motor_type!r}。可选：{sorted(MOTOR_SPECS)}\n"
                f"  · 别写厂商 SDK 的枚举名（DM4340 之类）—— SDK 的 `DM4340` 是**普通 4340**，"
                f"和台上这颗 4340P 是同一个枚举值，串档不报错（LESSONS.md §2.6 坑 2）"
            )

        # 力矩阈值按型号派生（见字段处的注释：这两个写错了会**静默通过**）
        if self.torque_max is None:
            self.torque_max = spec.torque_max_default
        if self.torque_monitor_threshold is None:
            self.torque_monitor_threshold = spec.monitor_default

        if self.direction not in (1, -1):
            raise ValueError(f"{n}: direction 必须是 +1 或 -1，收到 {self.direction!r}")

        if not (1 <= self.slave_id <= 0x7F):
            raise ValueError(f"{n}: slave_id 应在 1..0x7F，收到 {self.slave_id}")
        if self.master_id == 0:
            raise ValueError(f"{n}: master_id 不能是 0（那是广播/无效值）")
        if self.master_id == self.slave_id:
            raise ValueError(f"{n}: master_id 和 slave_id 相同（都是 {self.slave_id}）—— 抄错了吧")

        if (self.position_min is not None and self.position_max is not None
                and self.position_min >= self.position_max):
            raise ValueError(
                f"{n}: 软限位反了：position_min={self.position_min:g} "
                f">= position_max={self.position_max:g}\n"
                f"  · 后果：命令会被钉死在两个点上，想要 0 得到 min、想要别的得到另一个，"
                f"**而且不报错**（`Joint` 的 `_apply_soft_limit` 那条注释写了同一件事，"
                f"只是那时已经晚了 —— 在这里拦能报出**是哪个关节**）"
            )

        if self.velocity_max <= 0:
            raise ValueError(f"{n}: velocity_max 必须 > 0，收到 {self.velocity_max:g}")

        # ── 型号 ↔ 档位一致性：vlim 落在映射 VMAX 内、pos_kp 在合理档位（§4.2）──
        _check_type_gearing(n, self.motor_type, self.vlim, self.pos_kp)

        # ── 力矩阈值必须低于该型号**峰值**，否则永远不触发（§4.3）──
        if self.torque_max > spec.peak_torque:
            raise ValueError(
                f"{n}: torque_max={self.torque_max:g} > {spec.label} 峰值 "
                f"{spec.peak_torque:g} N·m —— 电机根本到不了，等于没写"
            )
        if self.torque_monitor and self.torque_monitor_threshold >= spec.peak_torque:
            raise ValueError(
                f"{n}: torque_monitor_threshold={self.torque_monitor_threshold:g} "
                f">= {spec.label} 峰值 {spec.peak_torque:g} N·m\n"
                f"  · **这个阈值永远不会触发**，等于没有保护（§4.3 原话）\n"
                f"  · 4310 峰值只有 12.5 —— 别把 4340P 的 15 抄过来"
            )
        if self.torque_monitor_count < 1:
            raise ValueError(
                f"{n}: torque_monitor_count 至少为 1；它按 feedback_hz 计数，"
                f"是用来给力矩估读做防抖的（放 500Hz 发帧循环里数会误触发，§4.3）"
            )

        # ── 摩擦：要么不写，要么成对写 ──
        if (self.friction_min is None) != (self.friction_max is None):
            raise ValueError(
                f"{n}: friction_min / friction_max 要么都留空（=未测），要么都填\n"
                f"  · 只填一端的话，读的人会以为另一端是 0 或无穷"
            )
        if self.friction_min is not None:
            if self.friction_min < 0:
                raise ValueError(f"{n}: friction_min={self.friction_min:g} 是负的 —— "
                                 f"摩擦是耗散项，不可能是负的（符号留给命令）")
            if self.friction_min > self.friction_max:
                raise ValueError(f"{n}: friction_min={self.friction_min:g} > "
                                 f"friction_max={self.friction_max:g}")

        if self.motor_timeout_ms <= 0:
            raise ValueError(
                f"{n}: motor_timeout_ms 必须 > 0（0 = 关掉电机侧看门狗）\n"
                f"  · 想关是可以的，但那是**故意**的决定，别靠留空/写 0 蒙混"
            )

    def __repr__(self) -> str:                      # 刷屏友好
        return (f"JointConfig({self.name}, {self.motor_type}, "
                f"id=0x{self.slave_id:02X}, dir={self.direction:+d}, off={self.offset:+.3f})")


# ═══════════════════════════ 夹爪 ═══════════════════════════

@dataclass
class GripperConfig:
    """夹爪。换算常数按 `DESIGN.md` §2.3（来源 `hardware_manager.py:14-15`）。

    换算**线性、与开度无关**：`0.10 m 全开 ↔ -5.0 rad`，所以 `m_per_rad = -0.02`。

    ⚠️ §2.3 记了 reBot 的一个疑似不一致：它发 `/joint_states` 时给 `finger_left`
    乘了 0.5（最大只发到 0.05 m，而 URDF 限位是 0.0285）。**本层不照抄这个缩放** ——
    以米为准，缩放留给可视化层。

    ⚠️⚠️ **待定：夹爪有两套换算，谁管哪一段还没定。**
      · `direction` / `offset` —— 关节侧 ↔ 电机侧（和 `JointConfig` 同一套）
      · `m_per_rad` —— **米 ↔ 电机侧**
    但 §2.3 的公式 `distance = (pos / -5.0) * 0.10` 里 `pos` 就是**电机侧读数**，
    也就是说 `m_per_rad` 已经一步跨到电机侧了。那么 `direction`/`offset`
    要么是多余的，要么是**第二道还没接上的换算**（谁先谁后会算出不同结果，
    而且两种都"看着对"）。

    **本文件的立场**：`angle_from_meters` 返回的是**电机侧**弧度
    （照 §2.3 原式，不额外套 `direction`/`offset`）。`direction`/`offset` 目前
    **没有消费者**。夹爪装到臂上标定之前不要假定它们生效了 —— 见本轮提给你的问题。
    """

    motor_type: str = "4310"
    slave_id: int = 0x07
    master_id: int = 0x17
    direction: int = 1
    offset: float = 0.0

    m_open: float = 0.10               # 全开行程（m）
    rad_open: float = -5.0             # 全开对应的电机侧角度（rad）

    # ── POS_VEL：夹爪也是靠这条控制律动的（§2.1 给了数，但 §4.2/§六 漏了字段）──
    pos_kp: float = 50.0
    pos_ki: float = 1.0
    vel_kp: float = 0.0008
    vel_ki: float = 0.002
    vlim: float = 3.0

    # ── MIT ──
    mit_kp: float = 8.0
    mit_kd: float = 1.0

    default_force: float = 0.30
    force_min: float = 0.05
    force_max: float = 1.5
    arrive_tol_rad: float = 0.12

    # ★ 这两个**刻意显式写死、不按型号派生** —— 和 `JointConfig` 的规矩刚好相反，
    #   因为这里"覆盖"本身就是目的：夹爪是 4310，型号派生会给 3.5/5.0，
    #   但夹爪限力本来就该远低于同型号的关节（§4.3 给的正是 1.5/1.8）。
    #   JointConfig 那边派生是因为"抄错会静默通过"；这里不存在抄错 ——
    #   gripper 只有一份，没有第二个夹爪可以抄。
    torque_max: float = 1.5            # 夹爪限力
    torque_monitor: bool = True
    torque_monitor_threshold: float = 1.8
    torque_monitor_count: int = 10
    motor_timeout_ms: int = 200

    def __post_init__(self) -> None:
        g = "gripper"
        if self.motor_type not in MOTOR_SPECS:
            raise ValueError(f"{g}: 未知电机型号 {self.motor_type!r}")
        if self.direction not in (1, -1):
            raise ValueError(f"{g}: direction 必须是 +1 或 -1")
        if self.m_open <= 0:
            raise ValueError(f"{g}: m_open 必须 > 0（它是换算的分母）")

        # 和 JointConfig 共用同一套型号档位校验（夹爪也是 4310，也会串档）
        # 注意 §4.3 给的夹爪 pos_kp = 50 **刚好落在 4310 档位的下界上**（闭区间，放行）
        _check_type_gearing(g, self.motor_type, self.vlim, self.pos_kp)
        if self.rad_open == 0:
            raise ValueError(
                f"{g}: rad_open 不能是 0 —— 它是 `m_per_rad = m_open / rad_open` 的分母，"
                f"为 0 就是除零"
            )
        if not (self.force_min < self.default_force < self.force_max):
            raise ValueError(
                f"{g}: 夹持力要满足 force_min < default_force < force_max，"
                f"现在 {self.force_min} / {self.default_force} / {self.force_max}"
            )
        if self.arrive_tol_rad <= 0:
            raise ValueError(f"{g}: arrive_tol_rad 必须 > 0")
        if self.torque_monitor and self.torque_monitor_threshold >= MOTOR_SPECS[
                self.motor_type].peak_torque:
            raise ValueError(f"{g}: torque_monitor_threshold 不低于 "
                             f"{self.motor_type} 峰值，永不触发")

    @property
    def m_per_rad(self) -> float:
        """米 / 弧度。§2.3：0.10 / -5.0 = **-0.02**（负号 = 转得越多开口越大）。"""
        return self.m_open / self.rad_open

    def angle_from_meters(self, meters: float) -> float:
        """开口（m，0 = 闭合）→ 电机侧角度（rad）。**会钳到 [闭合, 全开]。**"""
        lo, hi = sorted((0.0, self.rad_open))
        return min(max(meters / self.m_per_rad, lo), hi)

    def meters_from_angle(self, rad: float) -> float:
        """电机侧角度（rad）→ 开口（m）。"""
        return rad * self.m_per_rad

    def clamp_force(self, force: float) -> float:
        """夹持力钳到 [force_min, force_max]（§2.3：钳到 [0.05, 1.5]）。"""
        return min(max(force, self.force_min), self.force_max)


# ═══════════════════════════ 安全 ═══════════════════════════

@dataclass
class SafetyConfig:
    """安全阈值。字段按 `DESIGN.md` §4.2 的"增加"那一句。

    注意 `max_temperature` / `min_voltage` / `max_voltage` 被**取代**了：
    §4.2 要的是 `temp_warn`/`temp_fault` 两档（80/100）和 `vbus_min`/`vbus_max`。
    单个"最大温度"没法区分"该降速了"和"该断电了"。
    """

    watchdog_timeout: float = 0.1      # 主机侧看门狗：多久没喂就急停（s）
    motor_timeout_ms: int = 200        # 电机侧看门狗，寄存器 0x09 的默认值

    temp_warn: float = 80.0            # 到这儿开始告警/降速
    temp_fault: float = 100.0          # 到这儿急停（手册：线圈别超 100℃）

    vbus_min: float = 15.0             # 24V 版建议 ≥15V
    vbus_max: float = 32.0             # ≤32V

    feedback_hz: float = 100.0         # 收帧循环频率 —— 也是 torque_monitor_count 的时基
    enable_delay: float = 3.0          # 使能后等多久再发第一条运动命令
    emergency_on_error: bool = True

    def __post_init__(self) -> None:
        if self.watchdog_timeout <= 0:
            raise ValueError("safety: watchdog_timeout 必须 > 0；想关看门狗应当显式说明，"
                             "而不是靠写 0 或留空让它变成 0")
        if self.motor_timeout_ms <= 0:
            raise ValueError("safety: motor_timeout_ms 必须 > 0")
        if not (self.temp_warn < self.temp_fault):
            raise ValueError(
                f"safety: temp_warn={self.temp_warn:g} 必须 < temp_fault={self.temp_fault:g}\n"
                f"  · 两档的意义是「先降速、再断电」；反过来就只剩断电一档，"
                f"而且永远先触发 fault"
            )
        if not (0 < self.vbus_min < self.vbus_max):
            raise ValueError(f"safety: 母线电压要先满足 0 < vbus_min < vbus_max，"
                             f"现在 {self.vbus_min:g} / {self.vbus_max:g}")
        if self.feedback_hz <= 0:
            raise ValueError("safety: feedback_hz 必须 > 0")


# ═══════════════════════════ URDF（限位真源） ═══════════════════════════

def read_urdf_joint_limits(path: str | Path) -> dict[str, tuple[float, float]]:
    """从 URDF 读**可动**关节的 `[lower, upper]`。返回 `{关节名: (下界, 上界)}`。

    为什么读 URDF 而不是抄进 YAML（`DESIGN.md` §2.2 原话）：
    **"限位以 URDF 为唯一真源，配置里改为引用而非硬编码。"**
    硬编码的问题是：URDF 改了限位（换仿件、改装配），配置里那份不会跟着动，
    于是软限位和真实机构行程对不上 —— 而且**对不上不会报错**。

    用 stdlib 的 `xml.etree`，不引新依赖。只收 `revolute`/`prismatic`（有 limit 的可动关节），
    `fixed` 关节直接跳过。
    """
    root = ET.parse(str(path)).getroot()
    out: dict[str, tuple[float, float]] = {}
    for j in root.findall("joint"):
        if j.get("type") not in ("revolute", "prismatic", "continuous"):
            continue
        lim = j.find("limit")
        if lim is None:
            continue
        lo, hi = lim.get("lower"), lim.get("upper")
        if lo is None or hi is None:
            continue                      # continuous 关节没有上下界
        out[j.get("name")] = (float(lo), float(hi))
    return out


# ═══════════════════════════ 整臂 ═══════════════════════════

@dataclass
class ArmConfig:
    """整臂配置。YAML 顶层形态按 `DESIGN.md` §六。

    `from_dict` 是唯一的解析入参 —— `from_yaml` 只是"先 `yaml.safe_load` 再调它"，
    所以**即使哪天 pyyaml 没了，测试和其它入口仍能走 `from_dict`**。
    """

    name: str
    channel: str = "/dev/ttyACM0"
    baudrate: int = 921600             # USB-CAN 适配器的 **UART 侧**；CAN 侧固定 1Mbps
    serial_timeout: float = 0.003
    send_hz: float = 500.0
    feedback_hz: float = 100.0
    control_mode: str = "POS_VEL"
    can_bitrate: int = 1_000_000
    enable_delay: float = 3.0
    urdf_path: str | None = None       # 相对 config_dir；config_dir 缺省时必须是绝对路径

    # 配置文件所在目录 —— 用来解释相对的 urdf_path。
    # 显式字段，**不是**偷偷挂的 `cfg._config_dir`：挂属性的版本在
    # 「手搓 ArmConfig 再调 load_limits()」时会静默退回仓库默认目录，
    # 于是相对 urdf_path 解到别处 —— **而且不报错**。宁可抛。
    config_dir: str | None = None

    joints: dict[str, JointConfig] = field(default_factory=dict)
    gripper: GripperConfig | None = None
    safety: SafetyConfig = field(default_factory=SafetyConfig)

    def __post_init__(self) -> None:
        if not self.joints:
            raise ValueError("ArmConfig: joints 是空的 —— 一条关节都没有的臂没有意义")

        if self.control_mode not in ("POS_VEL", "MIT"):
            raise ValueError(
                f"ArmConfig: control_mode={self.control_mode!r}，只认 'POS_VEL' / 'MIT'\n"
                f"  · 本机全 POS_VEL 固件闭环（§2.6），'MIT' 目前是预留"
            )

        # 关节名要和 JointConfig.name 对得上 —— 否则 `joints["joint1"]` 拿到的是
        # 一个叫别的名字的对象，排查时看着就懵。
        for key, j in self.joints.items():
            if j.name != key:
                raise ValueError(
                    f"ArmConfig: joints 的键 {key!r} 和 JointConfig.name {j.name!r} 不一致"
                )

        # ── ID 唯一性：两条链路各自唯一 ──
        # slave_id 撞了 = 两条命令发给同一颗电机（另一颗永远不动，且不报错）
        # master_id 撞了 = 将来 verify_mapping 读 0x07 时两边都以为自己是它
        seen_slave: dict[int, str] = {}
        seen_master: dict[int, str] = {}
        items = list(self.joints.items())
        if self.gripper is not None:
            items.append(("gripper", self.gripper))
        for key, obj in items:
            sid, mid = obj.slave_id, obj.master_id
            if sid in seen_slave:
                raise ValueError(f"ArmConfig: slave_id 0x{sid:02X} 被 {seen_slave[sid]!r} "
                                 f"和 {key!r} 同时占用 —— 命令会发给同一颗电机")
            if mid in seen_master:
                raise ValueError(f"ArmConfig: master_id 0x{mid:02X} 被 {seen_master[mid]!r} "
                                 f"和 {key!r} 同时占用")
            seen_slave[sid] = key
            seen_master[mid] = key

        if self.baudrate <= 0:
            raise ValueError("ArmConfig: baudrate 必须 > 0")
        if self.send_hz <= 0 or self.feedback_hz <= 0:
            raise ValueError("ArmConfig: send_hz / feedback_hz 必须 > 0")

    # ── 关节访问 ──

    def joint_names(self) -> list[str]:
        return list(self.joints)

    def __getitem__(self, name: str) -> JointConfig:
        try:
            return self.joints[name]
        except KeyError:
            raise KeyError(f"没有关节 {name!r}；本配置里有 {sorted(self.joints)}") from None

    # ── 限位（URDF 是唯一真源，§2.2）──

    def resolved_limits(self, config_dir: str | Path | None = None
                        ) -> dict[str, tuple[float, float]]:
        """把 `position_min/max` 和 URDF 合起来，返回**最终生效**的关节限位。

        规则（§4.2：`position_min: None = 从 URDF 取`）：
          · 两个都写了      → 用配置里的（人是故意的）
          · 有一个是 None   → **取 URDF 的那一侧**（不是"整条都不要"）
          · 两个都是 None 且 URDF 里没这个关节 → 该关节不进结果（= 不检查软限位）

        返回 `{关节名: (min, max)}`；**只包含最终两侧都有值的关节**。
        另外会打印 URDF 里有、但配置里没配的关节名（多半是命名不一致，值得看一眼）。
        """
        urdf_lim: dict[str, tuple[float, float]] = {}
        if self.urdf_path:
            p = Path(self.urdf_path)
            if not p.is_absolute():
                base = config_dir if config_dir is not None else self.config_dir
                if base is None:
                    raise ValueError(
                        f"urdf_path={self.urdf_path!r} 是相对路径，但不知道相对于谁\n"
                        f"  · 走 `from_yaml` 时它自动填好；手搓 ArmConfig 就得自己给\n"
                        f"    （构造时传 config_dir=，或调 resolved_limits(config_dir=...)）\n"
                        f"  · 这里**刻意不猜** —— 猜错目录的后果是软限位静默落空，"
                        f"而落空看起来和「检查通过」一模一样"
                    )
                p = Path(base) / p
            if p.exists():
                urdf_lim = read_urdf_joint_limits(p)
            else:
                raise FileNotFoundError(
                    f"urdf_path 指向的文件不存在：{p}\n"
                    f"  · 是相对**配置文件所在目录** {base} 解释的\n"
                    f"  · §2.2 说限位的唯一真源是 URDF；路径错了软限位就只能空着，"
                    f"空着 = **不检查**，不是「检查并通过」"
                )

        out: dict[str, tuple[float, float]] = {}
        for name, j in self.joints.items():
            um = urdf_lim.get(name)
            lo = j.position_min if j.position_min is not None else (um[0] if um else None)
            hi = j.position_max if j.position_max is not None else (um[1] if um else None)
            if lo is None or hi is None:
                continue
            out[name] = (lo, hi)

        # ★ 这里**刻意不**打"URDF 里有但配置里没有"的警告。
        #   我第一版打了，结果构造一个只含 joint1 的配置时，joint2~joint6 全被
        #   误报成"命名对不上" —— **假警告比没有警告更糟**：它会训练人忽略警告。
        #   "有关节没拿到限位"该由上层（`DmArm`）在拿到**完整**关节表时报告，
        #   那时它才知道"缺"是真的缺。本函数只老实返回它能确定的那些。
        return out

    # ── 解析入口 ──

    @classmethod
    def from_dict(cls, data: dict) -> "ArmConfig":
        """从**已经解析好的 dict** 构造。`from_yaml` 也走这条路。

        显式列白名单键，不 `**data` 全塞 —— 这样 YAML 里写错一个字段名会
        **报错**，而不是被静默丢掉（静默丢掉意味着你以为配了、其实没配）。
        """
        known = {"name", "channel", "baudrate", "serial_timeout", "send_hz",
                 "feedback_hz", "control_mode", "can_bitrate", "enable_delay",
                 "urdf_path", "joints", "gripper", "safety"}
        unknown = sorted(set(data) - known)
        if unknown:
            raise ValueError(
                f"YAML 顶层有认不出的键：{unknown}\n"
                f"  · 认得的只有：{sorted(known)}\n"
                f"  · 拼错一个字段名而不报错，等于你以为配了、其实没配"
            )
        if "name" not in data:
            raise ValueError("YAML 顶层缺少 name")

        raw_joints = data.get("joints") or {}
        joints: dict[str, JointConfig] = {}
        for jname, jd in raw_joints.items():
            d = dict(jd)
            d.setdefault("name", jname)      # 键就是名字，除非 YAML 里另外写了
            # ★ 键名与 name 是否一致的检查**只有一处**，在 ArmConfig.__post_init__。
            #   这里原本还有一份，但两份互相兜底，导致**谁都测不出来** ——
            #   删掉一个变异，另一个照样拦住，测试永远绿。同一件事只该有一种说法。
            joints[jname] = JointConfig(**d)

        gripper = GripperConfig(**data["gripper"]) if data.get("gripper") else None
        safety = SafetyConfig(**(data.get("safety") or {}))

        arm = {k: v for k, v in data.items()
               if k not in ("joints", "gripper", "safety")}
        return cls(joints=joints, gripper=gripper, safety=safety, **arm)

    @classmethod
    def from_yaml(cls, path: str | Path) -> "ArmConfig":
        """读 YAML 数据文件。**`import yaml` 只在这里出现** —— 见模块 docstring 的故障隔离。"""
        import yaml                                     # noqa: PLC0415  刻意延迟导入

        p = Path(path)
        with open(p, encoding="utf-8") as f:
            data = yaml.safe_load(f)
        if not isinstance(data, dict):
            raise ValueError(f"{p} 解析出来不是字典（是 {type(data).__name__}）—— 文件格式不对")
        cfg = cls.from_dict(data)
        cfg.config_dir = str(p.parent)                  # 给 resolved_limits 解释相对路径
        return cfg

    def load_limits(self) -> dict[str, tuple[float, float]]:
        """`resolved_limits` 的便捷版：用 `config_dir`（`from_yaml` 会填好）。"""
        return self.resolved_limits(self.config_dir)


def load_arm_config(path: str | Path | None = None) -> ArmConfig:
    """加载整臂配置。`path=None` 用仓库里那份 `config/rebotarm_b601_mixed.yaml`。"""
    return ArmConfig.from_yaml(path or DEFAULT_CONFIG_PATH)
