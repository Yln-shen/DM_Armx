from dm_bus import MotorBus
from dm_frames import ERR_OK
from dm_modes import MODE_MIT, MODE_POS_VEL, MODE_FORCE_POS, MODE_NAMES
from dataclasses import dataclass
import math
import sys
import time

# 使能后补的那一帧"保持"的速度上限（rad/s）
HOLD_VLIM = 0.1

# tau_ff 被钳位时的日志间隔（秒）：钳位会改变控制律，应该看得见，但 500Hz 下不能刷屏。
# （arm.py 里的 MONITOR_WARN_INTERVAL 是同类东西，但 joint 在 arm 下面，不能反向 import）
CLAMP_WARN_INTERVAL = 5.0

@dataclass
class JointState:
    name: str
    position: float      # rad（关节侧）
    velocity: float      # rad/s
    torque: float        # N·m
    err: int             # 反馈帧 D[0] 高 4 位
    err_text: str        # 由 dm_frames.ERR_DECODE 解出（MotorState 已带，直接透传）
    temp_mos: int        # ℃  D[6]
    temp_rotor: int      # ℃  D[7]
    enabled: bool        # ERR == 1。注意 ERR == 0 是"失能"，不是"正常"
    timestamp: float     # time.monotonic()

class Joint:
    def __init__(self, bus: MotorBus,
                 motor_id: int,
                 name: str,
                 direction: int,
                 limit: tuple[float, float, float],
                 offset: float = 0.0,
                 position_min: float | None = None,
                 position_max: float | None = None,
                 mode: int = MODE_MIT,
                 torque_max: float | None = None,
                 nm_per_unit: float | None = None):

        if mode not in MODE_NAMES:
            allowed = "/".join(f"{k}({v})" for k, v in sorted(MODE_NAMES.items()))
            raise ValueError(f"mode 只能是 {allowed}，收到 {mode!r}")
        if torque_max is not None and not torque_max > 0:      # 顺手挡住 NaN
            raise ValueError(f"torque_max 要么是 None，要么是正数，收到 {torque_max!r}")
        if nm_per_unit is not None and not nm_per_unit > 0:    # 顺手挡住 NaN
            raise ValueError(f"nm_per_unit 要么是 None，要么是正数，收到 {nm_per_unit!r}")

        self.bus = bus
        self.motor_id = motor_id
        self.name = name
        self.direction = direction
        self.offset = offset
        self.limit = limit
        self.position_min = position_min
        self.position_max = position_max
        self.mode = mode
        self.torque_max = torque_max                           # None = 不钳位
        self.nm_per_unit = nm_per_unit                         # 力位混控 i_des↔N·m；None = 不钳
        self._clamp_warn_at = -CLAMP_WARN_INTERVAL             # 首次钳位一定打日志

        # 注册到 bus：发帧与解反馈都要用这份映射范围。冲突直接拒 ——
        # 同一个电机出现两个映射范围 ⇒ 解出来的力矩差数倍且不报错
        already = self.bus.motors().get(motor_id)
        if already is not None and tuple(already) != tuple(float(x) for x in limit):
            raise ValueError(
                f"电机 0x{motor_id:02X} 在 bus 上已注册为 {tuple(already)}，"
                f"与本次的 {tuple(float(x) for x in limit)} 不一致，拒绝构造"
            )
        self.bus.add_motor(motor_id, limit)

    def enable(self):
        st = self.bus.get_state(self.motor_id)
        if self.mode in (MODE_POS_VEL, MODE_FORCE_POS) and st is None:
            raise RuntimeError(
                "位置类模式(2/4)下没有缓存位置，拒绝使能："
                "切模式时电机内部指令被清零，使能后会朝零位冲"
            )
        try:
            self.bus.send_enable(self.motor_id)
            if self.mode == MODE_MIT:
                # 零增益零前馈 ⇒ 出力恒为 0，不需要知道当前位置
                self.bus.send_mit(self.motor_id, 0.0, 0.0, 0.0, 0.0, 0.0)
            elif self.mode == MODE_POS_VEL:
                # 保持帧用实测位置，不经过 clamp/换算（钳了就不是"保持"了）
                self.bus.send_pos_vel(self.motor_id, st.pos, HOLD_VLIM)
            else:
                # 力位混控：i_des 是**电流上限**，1.0 = 满力矩真保持
                # （给 0.0 是"一点力都不给"，重力负载下会垂下去，不是保持）
                self.bus.send_force_pos(self.motor_id, st.pos, HOLD_VLIM, 1.0)
        except Exception:
            try:
                self.bus.send_disable(self.motor_id)
            except Exception as e:
                print(f"[{self.name}] ‼ 失能也失败：{e} —— 电机可能仍在通电出力，请直接断电",
                      file=sys.stderr)
            raise

    def disable(self):
        self.bus.send_disable(self.motor_id)

    def prepare_frame(self, pos):
        """统一的发送前处理：软限位 → 换算 → PMAX"""
        if not math.isfinite(pos):
            raise ValueError(f"pos 是非有限数：{pos}")
        pos = self.clamp(pos)                     # 软限位
        motor_pos = self.joint_to_motor(pos)      # 换算
        motor_pos = self.clamp_pmax(motor_pos)   # PMAX
        return motor_pos

    def set_mit(self, kp, kd, q, dq=0.0, tau=0.0):
        """MIT 发一帧。`torque_max` 不是 None 时，先按**预测总力矩**钳 `tau_ff`（见 `_clamp_mit_torque`）。"""
        if self.mode != MODE_MIT:
            raise RuntimeError(
                f"声明模式是 {self.mode}({MODE_NAMES.get(self.mode, '?')})，"
                f"但 MIT 帧的 CAN ID 是电机 ID 本身 —— "
                f"模式不符，帧会被电机静默丢掉，拒绝发送"
            )
        for name, val in [("kp", kp), ("kd", kd), ("q", q), ("dq", dq), ("tau", tau)]:
            if not math.isfinite(val):
                raise ValueError(f"{name} 是非有限数：{val}")
        q_motor = self.prepare_frame(q)
        dq_motor = self.direction * dq
        tau_motor = self.direction * tau
        if self.torque_max is not None:
            tau_motor = self._clamp_mit_torque(kp, kd, q_motor, dq_motor, tau_motor)
        self.bus.send_mit(self.motor_id, kp, kd, q_motor, dq_motor, tau_motor)

    def _clamp_mit_torque(self, kp, kd, q_des, dq_des, tau_ff):
        """按 `torque_max` 钳 MIT 的 `tau_ff`（入参与返回都是**电机侧**量）。

        电机出的是 `tau = kp·(q_des − q) + kd·(dq_des − dq) + tau_ff`，所以：

        - **PD 项自己就超** `torque_max` ⇒ 钳 `tau_ff` 救不回来（得改 `q_des` 或 `kp`）⇒ **拒发**
        - 否则把 `tau_ff` 钳到"总力矩 = ±`torque_max`"的边界

        ⚠️ 用的是**缓存里**的 `pos`/`vel`（滞后 1~20ms）⇒ 这是**近似**钳位，不是硬保证：
        慢速时可忽略，5 rad/s 时 kp=10 大约差 1 N·m。
        ⚠️ 没有缓存状态就**拒发**（算不出来就别装作算过了）。
        """
        m = self.bus.get_state(self.motor_id)
        if m is None:
            raise RuntimeError(
                f"{self.name}: 没有缓存位置/速度，预测不了力矩（torque_max={self.torque_max}）"
                f" —— 先 poll() 拿到反馈再发 MIT 帧"
            )
        limit = self.torque_max
        pd = kp * (q_des - m.pos) + kd * (dq_des - m.vel)
        if abs(pd) > limit:
            raise RuntimeError(
                f"{self.name}: PD 项 {pd:+.3f} N·m 已超过 torque_max={limit} "
                f"（kp={kp}, kd={kd}, q_des−q={q_des - m.pos:+.4f} rad, "
                f"dq_des−dq={dq_des - m.vel:+.4f} rad/s）—— 钳 tau_ff 救不回来，拒发；"
                f"要么降 kp/kd，要么把目标挪近"
            )
        total = pd + tau_ff
        if abs(total) > limit:
            clamped = math.copysign(limit - abs(pd), total)
            now = time.monotonic()
            if now - self._clamp_warn_at >= CLAMP_WARN_INTERVAL:
                self._clamp_warn_at = now
                print(f"[{self.name}] tau_ff 被钳：{tau_ff:+.3f} → {clamped:+.3f} N·m"
                      f"（PD {pd:+.3f} + tau_ff 会到 {total:+.3f}，超 torque_max {limit}）",
                      file=sys.stderr)
            return clamped
        return tau_ff

    def _limit_i_des(self, current):
        """按 `torque_max` 钳力位混控的电流上限 `i_des`（入参/返回都是 0~1 标幺）。

        换算是**实测**的：`i_des = torque_max / nm_per_unit`（`arm_config.NM_PER_I_DES`，
        2026-10-01 摩擦阈值扫描；见 AGENTS §5）。
        `current` 是**上限**不是需求 ⇒ 钳它是单调安全的（只会让力矩更小），所以**钳 + 限流日志**，
        不像 MIT 那样拒发。没有 `torque_max` / `nm_per_unit`（单关节直用）就原样放行。
        ⚠️ `enable()` 的保持帧**不经过这里**（直发 `i_des=1.0`），使能仍是真保持。
        """
        if self.torque_max is None or self.nm_per_unit is None:
            return current
        cap = self.torque_max / self.nm_per_unit
        if current <= cap:
            return current
        now = time.monotonic()
        if now - self._clamp_warn_at >= CLAMP_WARN_INTERVAL:
            self._clamp_warn_at = now
            print(f"[{self.name}] i_des 被钳：{current:+.3f} → {cap:+.3f}"
                  f"（torque_max {self.torque_max:g} N·m ÷ {self.nm_per_unit:g} N·m/单位"
                  f" ≈ {self.torque_max:g} N·m 上限）", file=sys.stderr)
        return cap

    def set_pos_vel(self, pos, vlim):
        if self.mode != MODE_POS_VEL:
            raise RuntimeError(
                f"声明模式是 {self.mode}({MODE_NAMES.get(self.mode, '?')})，"
                f"但 POS_VEL 帧的 CAN ID 是 0x100+ID —— "
                f"模式不符，帧会被电机静默丢掉，拒绝发送"
            )
        if not math.isfinite(pos) or not math.isfinite(vlim):
            raise ValueError(f"非有限数：pos={pos}, vlim={vlim}")
        if vlim < 0:
            raise ValueError(f"vlim={vlim} 是负的；协议里是无符号，"
                             f"负值会变成别的速度，要反方向是改 pos")
        self.bus.send_pos_vel(self.motor_id, self.prepare_frame(pos), vlim)

    def set_force_pos(self, pos, vel, current):
        if self.mode != MODE_FORCE_POS:
            raise RuntimeError(
                f"声明模式是 {self.mode}({MODE_NAMES.get(self.mode, '?')})，"
                f"但力位混控帧的 CAN ID 是 0x300+ID —— "
                f"模式不符，帧会被电机静默丢掉，拒绝发送"
            )
        for name, val in [("pos", pos), ("vel", vel), ("current", current)]:
            if not math.isfinite(val):
                raise ValueError(f"{name} 是非有限数：{val}")
        if vel < 0:
            raise ValueError(f"vel={vel} 是负的；协议里是无符号，负值会钳成 0")
        if not (0.0 <= current <= 1.0):
            raise ValueError(f"current={current} 不在 [0, 1]；协议里是 uint16 标幺值，"
                             f"超出会被静默钳掉")
        self.bus.send_force_pos(self.motor_id, self.prepare_frame(pos), vel,
                                self._limit_i_des(current))

    def switch_mode(self, mode):
        #模式切换
        raise NotImplementedError(
            f"切控制模式 = 写寄存器 0x0A，本层（MotorBus/Joint）不做任何寄存器 I/O。\n"
            f"  真要切（以 0x{self.motor_id:02X} → 模式 {mode}({MODE_NAMES.get(mode, '?')}) 为例）：\n"
            f"    1. disable()，确认电机已零速\n"
            f"    2. bus.poll() 拿到当前位置（位置类模式必须先知道\"现在在哪\"）\n"
            f"    3. 写寄存器 0x0A = {mode}（写寄存器不属本层；0x0A 是 RAM，掉电复位）\n"
            f"    4. self.mode = {mode}  ← 只改本地声明，本层核对不了硬件\n"
            f"    5. enable()（它会按新模式补对应的保持帧）\n"
            f"  顺序不能换：切进位置类模式时电机内部指令被清零，先读位置再切。"
        )

    def get_state(self):
        #状态
        m = self.bus.get_state(self.motor_id)
        if m is None:
            raise RuntimeError(
                f"{self.name}(0x{self.motor_id:02X}) 还没有收到过反馈帧 —— "
                f"先 bus.poll() 拿到状态，再调 get_state()"
                # 注：MotorBus.get_state() 返回 None 是对的（"从没收到过"是正常状态）；
                # 关节侧要状态就是要用，所以这里不给 None，直接抛。
                # "反馈过期"是另一回事，由上层按 timestamp 判断。
            )
        return JointState(
            name=self.name,
            position=self.motor_to_joint(m.pos),
            velocity=self.direction * m.vel,   # 速度是矢量：只乘 direction，不加 offset
            torque=self.direction * m.tau,     # 力矩同上
            err=m.err,
            err_text=m.err_text,
            temp_mos=m.temp_mos,
            temp_rotor=m.temp_rotor,
            enabled=m.enabled,
            timestamp=m.timestamp,
        )

    def assert_healthy(self):
        """查缓存的 ERR，有故障就**先失能再抛**。返回 None。温度策略未实现。

        ⚠️ 读的是**缓存**（`get_state()` 不发帧、不 poll）。循环里必须有 `bus.poll()`
        在跑，否则永远是同一个旧状态，等于没查。
        """
        st = self.get_state()            # 没有反馈 → 直接抛，不吞
        if st.err not in ERR_OK:         # 不是 0(失能)/1(使能) ⇒ 故障，电机已自行退出使能
            try:
                self.bus.send_disable(self.motor_id)
            except Exception as e:
                print(f"[{self.name}] ‼ 失能也失败：{e} —— 电机可能仍在通电出力，请直接断电",
                      file=sys.stderr)
            raise RuntimeError(
                f"关节 {self.name}(0x{self.motor_id:02X}) 故障 ERR={st.err}（{st.err_text}），"
                f"已发失能。ERR=13(通讯丢失) 是**锁存**的，只能断电再上电清除；"
                f"失能只是让电机松掉，不会让它停下 —— 重力负载下它会掉"
            )

    # 内部换算
    def joint_to_motor(self, v):
        """关节侧 → 电机侧（位置）"""
        return self.direction * v + self.offset

    def motor_to_joint(self, v):
        """电机侧 → 关节侧（位置）"""
        return self.direction * (v - self.offset)

    def clamp(self, pos):
        # ← 钳位（动作）
        if self.position_min is not None:
            pos = max(pos, self.position_min)
        if self.position_max is not None:
            pos = min(pos, self.position_max)
        return pos

    def clamp_pmax(self, motor_pos):
        p_max = self.limit[0]
        return max(-p_max, min(p_max, motor_pos))
