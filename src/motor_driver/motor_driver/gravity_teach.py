#!/usr/bin/env python3
"""gravity_teach —— 零重力手推示教（**Python 旁路**，演示期间不要起 ros2_control）。

为什么走旁路、而不是 ros2_control 插件：
    插件里"保持"的语义是**锁定激活那一刻的位置**（陷阱 #38 的修复：20 s 漂移从 −0.0027 降到 1 LSB）。
    "推得动"要的恰恰是**相反**的语义（命令跟随实测位置）。改插件等于动安全关键路径，
    所以演示走 Python 直连 —— 用的全是已验证的件：`dm_bus` / `Joint` / `DmArm`
    （100 Hz 双循环、力矩+温度监控、Ctrl-C 先急停、看门狗余量都真机跑过）。

手感由三态组成（每个关节各自判定）：
    ① **跟随**：`q_des = 实测位置` ⇒ PD 项 ≈ 0 ⇒ 零刚度，推得动
    ② **闩锁**：静止（|v| < v_latch）持续 t_latch ⇒ `q_des` 钉在当前位置 ⇒ 松手停住
       （软弹簧 kp_demo；死区 ≈ 前馈误差/kp ≈ 0.42/4 ≈ 0.10 rad）
    ③ **软墙**：进入缓冲带 ⇒ `q_des` 钉在**自由区边界**、kp 升向 kp_wall ⇒ 顶住，
       **绝不到机械硬限位**（陷阱 #59：JTC/驱动都不替你挡越界，真机堵转 60 s / 22.5 N·m）

"拒发 = 掉臂"（陷阱 #51）在数学上不可能发生：
    `Joint._clamp_mit_torque` 的拒发判据是 `|kp·Δq + kd·Δv| > torque_max`（PD 项单独算）。
    本脚本每帧在**同一份缓存状态**上先算预算，保证
        `|tau_ff| + |kp·Δq| + |kd·Δv| ≤ torque_budget_frac · torque_max`   (frac < 1)
    ⇒ 拒发判据恒不成立，且 `tau_ff` 也到不了钳位边界 ⇒ 重力给得足、不会因拒发掉臂。
    ⚠️ 预算里的速度必须用**原始反馈速度**（拒发判据用的就是它），不能用滤波值。

用法：
    # ① 只读预检：不发一帧、不使能（**先跑这个**）
    PYTHONPATH=src/motor_driver/motor_driver pixi run python \
        src/motor_driver/motor_driver/gravity_teach.py --check

    # ② 真机演示（先手扶/托住；接受"失能即塌"，见陷阱 #39）
    PYTHONPATH=src/motor_driver/motor_driver pixi run python \
        src/motor_driver/motor_driver/gravity_teach.py --run --yes --scale 1.0 --ramp 20

⚠️ 与 `arm_bringup/real_control.launch.py` **互斥**（两边都独占 /dev/dm_can，且都驱动同一组电机）。
⚠️ 本脚本**不写任何寄存器**（包括 0x0A）；模式不对就拒绝启动，改模式请用 `dm_registers` 并另行确认。
"""
from __future__ import annotations

import argparse
import math
import sys
import time
from dataclasses import dataclass, fields
from pathlib import Path

import yaml

from arm import DmArm
from arm_config import load_arm_config, load_joint_configs
from dm_gravity import GravityModel
from dm_modes import MODE_MIT, MODE_NAMES
from dm_registers import RegisterTool

_PKG_DIR = Path(__file__).resolve().parent          # .../src/motor_driver/motor_driver
_CFG_DIR = _PKG_DIR.parent / "config"               # .../src/motor_driver/config
_REPO = _PKG_DIR.parents[2]                         # .../DM_Armx
RID_CTRL_MODE = 0x0A


# ───────────────────────── 参数 ─────────────────────────
@dataclass
class Teach:
    loop_hz: float = 100.0
    start_scale: float = 0.3
    ramp_sec: float = 15.0
    kp_demo: float = 4.0
    kd: float = 0.8
    kp_wall: float = 25.0
    torque_budget_frac: float = 0.7
    kd_share: float = 0.5
    free_half_width: float = 0.25
    min_buffer: float = 0.05
    min_travel: float = 0.02
    release_band: float = 0.15
    v_latch: float = 0.05
    t_latch: float = 0.3
    vel_filter_alpha: float = 0.3
    friction_ff: bool = True
    friction_deadband: float = 0.02
    v_warn: float = 2.0
    heartbeat_sec: float = 2.0
    wall_warn_sec: float = 1.0

    @classmethod
    def load(cls, path):
        raw = yaml.safe_load(Path(path).read_text(encoding="utf-8")) or {}
        unknown = set(raw) - {f.name for f in fields(cls)}
        if unknown:
            raise ValueError(f"{path} 里有认不出的键 {sorted(unknown)} —— 拼错了吗？")
        cfg = cls(**raw)
        cfg.validate()
        return cfg

    def validate(self):
        for name, lo, hi in (("loop_hz", self.loop_hz, None), ("start_scale", self.start_scale, 1.0),
                             ("kp_demo", self.kp_demo, None), ("kd", self.kd, None),
                             ("kp_wall", self.kp_wall, None),
                             ("torque_budget_frac", self.torque_budget_frac, 1.0),
                             ("free_half_width", self.free_half_width, None),
                             ("min_buffer", self.min_buffer, None),
                             ("min_travel", self.min_travel, None),
                             ("release_band", self.release_band, None),
                             ("vel_filter_alpha", self.vel_filter_alpha, 1.0)):
            if not (math.isfinite(lo) and lo > 0 and (hi is None or lo <= hi)):
                raise ValueError(f"teach: {name}={lo} 不合理（要 >0"
                                 + (f" 且 ≤{hi}" if hi is not None else "") + "）")
        if not 0.0 <= self.kd_share < 1.0:
            raise ValueError(f"teach: kd_share={self.kd_share} 必须落在 [0,1)")
        if not self.torque_budget_frac < 1.0:
            raise ValueError("torque_budget_frac 必须 <1，否则拒发判据可能成立 ⇒ 掉臂")
        if self.release_band <= 0:
            raise ValueError("release_band 必须 >0")
        if self.friction_ff and self.friction_deadband <= 0:
            raise ValueError("开了 friction_ff 就得给正的 friction_deadband（静止时方向不确定）")


@dataclass
class Zone:
    lo: float                 # 自由区下界（关节侧）
    hi: float                 # 自由区上界
    buf_lo: float             # 下侧缓冲带宽度（自由边界 → 软限位）
    buf_hi: float

    def describe(self):
        return f"自由区 [{self.lo:+.4f}, {self.hi:+.4f}]  缓冲带 {self.buf_lo:.4f}/{self.buf_hi:.4f}"


@dataclass
class Rt:
    latched: "float | None" = None    # 闩锁的目标（关节侧）；None = 跟随中
    still_since: "float | None" = None
    v_filt: float = 0.0
    last_wall_warn: float = 0.0
    last_v_warn: float = 0.0
    # 上一帧实际发出去的值（心跳与事后核对用；不参与控制）
    q_des: float = 0.0
    kp_eff: float = 0.0
    kd_eff: float = 0.0
    tau_ff: float = 0.0
    depth: float = 0.0
    in_wall: bool = False


# ───────────────────────── 小工具 ─────────────────────────
def find_urdf(explicit):
    if explicit:
        p = Path(explicit)
        if not p.is_file():
            raise FileNotFoundError(f"--urdf 给的 {p} 不存在")
        return p
    for p in (_REPO / "install/arm_description/share/arm_description/urdf/arm_identified.urdf",
              _REPO / "build/arm_description/arm_identified.urdf"):
        if p.is_file():
            return p
    raise FileNotFoundError(
        "找不到 arm_identified.urdf —— 先 `colcon build --packages-select arm_description`，"
        "或用 --urdf 指定")


def load_align(path):
    raw = yaml.safe_load(Path(path).read_text(encoding="utf-8"))
    out = {}
    for name, v in raw["joints"].items():
        sign = int(v["sign"])
        if sign not in (-1, 1):
            raise ValueError(f"align.yaml: {name} 的 sign={sign} 不是 ±1")
        out[name] = (sign, float(v["zero_shift"]))
    return out


def load_friction(path):
    """从 mit_gains.yaml 取摩擦前馈系数（**单一真源**，不在 teach.yaml 里另抄一份）。"""
    raw = yaml.safe_load(Path(path).read_text(encoding="utf-8"))["joints"]
    out = {}
    for n, v in raw.items():
        c = float(v.get("friction_c", 0.0))
        eps = float(v.get("friction_v_eps", 0.0))
        if c > 0.0 and eps <= 0.0:
            raise ValueError(f"mit_gains.yaml: {n} 有 friction_c 但 friction_v_eps ≤ 0")
        out[n] = (c, eps)
    return out


def build_free_zones(joint_cfgs, q_meas, teach, strict_travel=False):
    """按**实测姿态**现算自由区；两边各留 min_buffer 的缓冲带。

    某一侧行程小于 `min_travel` **默认只警告不拒绝**：那种"紧贴"的一侧通常就是**机械硬限位**
    （失能后臂停在那儿就是证据，折叠位就是这样），软墙在那侧本来就是冗余的，推也推不动。
    真正要拒绝的是"姿态落在软限位外"或"自由区装不下"。
    `strict_travel=True`（CLI `--strict-travel`）时才把行程不足也当硬错误。
    """
    zones = {}
    cramped = []
    for name, jc in joint_cfgs.items():
        if jc.position_min is None or jc.position_max is None:
            raise ValueError(f"{name} 没有软限位（position_min/max）—— 软墙必须有它才成立")
        q = q_meas[name]
        # 缓冲带宽度：**能留 min_buffer 就留，留不下就用"可用余量的一半"**。
        # 为什么不是固定 min_buffer：失能后臂会自己落到机械硬限位附近（折叠位就是这样），
        # 那时 `soft_min + min_buffer` 会**超过起点本身**，把合法姿态误判成"姿态非法"。
        # 用一半余量 ⇒ 起点永远严格落在自由区内，缓冲带也还留着一点（那侧本来就没多少路）。
        buf_lo = min(teach.min_buffer, max(0.0, (q - jc.position_min) * 0.5))
        buf_hi = min(teach.min_buffer, max(0.0, (jc.position_max - q) * 0.5))
        lo = max(jc.position_min + buf_lo, q - teach.free_half_width)
        hi = min(jc.position_max - buf_hi, q + teach.free_half_width)
        if not lo < hi:
            raise ValueError(f"{name}: 软限位区间太窄，放不下自由区（lo={lo:+.4f}, hi={hi:+.4f}）")
        if not lo <= q <= hi:
            # 只有"姿态本身就在软限位之外"才走到这里（buf 的算法保证否则一定 lo ≤ q）
            raise ValueError(
                f"{name}: 当前姿态 {q:+.4f} 已经**在软限位之外**"
                f"（允许 [{jc.position_min:+.4f}, {jc.position_max:+.4f}]）"
                f"⇒ **请把臂摆回限位以内**（别指望演示程序替你越界）")
        travel = min(q - lo, hi - q)
        if travel < teach.min_travel:
            side = "下" if (q - lo) < (hi - q) else "上"
            msg = (f"{name}: {side}侧自由行程只有 {travel:.4f} rad（< min_travel={teach.min_travel}）"
                   f" —— 那一侧通常就是**机械硬限位**（软墙在那边是冗余的，推不动也正常）。"
                   f"⚠️ 但这个姿态很可能正靠在限位上：使能后前馈会把臂**推离**限位而自己动起来，"
                   f"手扶好；想严格一点用 --strict-travel")
            if strict_travel:
                raise ValueError(msg)
            cramped.append(msg)
        zones[name] = Zone(lo, hi, lo - jc.position_min, jc.position_max - hi)
    if cramped:
        print("[预检] ⚠ 有侧行程很窄（不拒绝，但请看仔细）：", file=sys.stderr)
        for m in cramped:
            print(f"        · {m}", file=sys.stderr)
    return zones


# ───────────────────────── 演示主体 ─────────────────────────
class TeachRunner:
    def __init__(self, arm, joint_cfgs, align, friction, zones, gravity, teach,
                 scale_target, ramp_sec, duration):
        self.arm = arm
        self.jc = joint_cfgs
        self.align = align
        self.friction = friction
        self.zones = zones
        self.gravity = gravity
        self.t = teach
        self.scale_target = scale_target
        self.ramp_sec = ramp_sec
        self.duration = duration
        self.order = list(joint_cfgs)          # 与 GravityModel / arm.joints 同一个顺序
        self.rt = {n: Rt() for n in self.order}
        self.t0 = None
        self.last_beat = 0.0
        self.frames = 0

    def scale_now(self, now):
        if self.ramp_sec <= 0 or self.t0 is None:
            return self.scale_target
        frac = min(1.0, (now - self.t0) / self.ramp_sec)
        return self.t.start_scale + (self.scale_target - self.t.start_scale) * frac

    def step(self, arm):
        """一圈：poll（由 run_feedback_loop 做）之后进来算 + 发。返回 None = 继续跑。"""
        now = time.monotonic()
        if self.t0 is None:
            self.t0 = now
        st = arm.get_state()
        q = {}
        for n in self.order:
            s = st.get(n)
            if s is None:
                raise RuntimeError(
                    f"{n} 没有反馈 —— 拒发 MIT 帧（没有缓存状态就算不出力矩，见 _clamp_mit_torque）")
            q[n] = s.position
        # 重力项一次性算全臂（模型级计算），q_model = sign*q_ours + zero_shift
        tau_g = self.gravity.tau_ours(
            [self.align[n][0] * q[n] + self.align[n][1] for n in self.order])
        scale = self.scale_now(now)
        for i, n in enumerate(self.order):
            self._one_joint(n, arm.joints[n], st[n], q[n], tau_g[i], scale, now)
        arm.check_health()                     # ERR 非 0/1 会抛 ⇒ 循环先急停
        self.frames += 1
        if now - self.last_beat >= self.t.heartbeat_sec:
            self.last_beat = now
            self._heartbeat(now, st, q, tau_g, scale)
        if self.duration is not None and now - self.t0 >= self.duration:
            print(f"[teach] 到达 --duration {self.duration} s ⇒ 正常退出（**不失能**，臂保持在原位）",
                  file=sys.stderr)
            return False
        return None

    def _one_joint(self, name, joint, s, q, tau_g, scale, now):
        jc = self.jc[name]
        z = self.zones[name]
        rt = self.rt[name]
        v = s.velocity                          # 原始反馈速度（拒发判据用的就是它）
        rt.v_filt += self.t.vel_filter_alpha * (v - rt.v_filt)

        # ① 目标：跟随 / 闩锁
        if rt.latched is None:
            q_des = q
            if abs(rt.v_filt) < self.t.v_latch:
                if rt.still_since is None:
                    rt.still_since = now
                elif now - rt.still_since >= self.t.t_latch:
                    rt.latched = q               # 静止够久 ⇒ 闩锁（松手停住）
            else:
                rt.still_since = None
        elif abs(q - rt.latched) > self.t.release_band:
            rt.latched = None                    # 被推开了 ⇒ 解锁，回到跟随
            rt.still_since = None
            q_des = q
        else:
            q_des = rt.latched

        # ② 软墙：目标钉在自由区内（PD 项因此被"钉住"的那一侧撑起来）
        q_des = min(max(q_des, z.lo), z.hi)
        depth = abs(q_des - q)
        in_wall = not (z.lo <= q <= z.hi)
        if in_wall and now - rt.last_wall_warn >= self.t.wall_warn_sec:
            rt.last_wall_warn = now
            side = "下" if q < z.lo else "上"
            edge = jc.position_min if q < z.lo else jc.position_max
            print(f"[teach] ⚠ {name} 进入{side}侧缓冲带：q={q:+.4f}，距软限位 {abs(edge - q):.4f} rad"
                  f"（再推也不会到机械硬限位，但请推回自由区）", file=sys.stderr)
        if abs(v) > self.t.v_warn and now - rt.last_v_warn >= self.t.wall_warn_sec:
            rt.last_v_warn = now
            print(f"[teach] ⚠ {name} 速度 {v:+.2f} rad/s（> {self.t.v_warn}）—— 推慢一点",
                  file=sys.stderr)

        # ③ 力矩：重力前馈（关节侧）+ 摩擦前馈，都在**预算**之内
        tau_ff = scale * tau_g
        if self.t.friction_ff and abs(rt.v_filt) >= self.t.friction_deadband:
            c, eps = self.friction[name]
            # 符号：摩擦力矩**反对**速度 ⇒ 抵消它必须给 **+**·tanh(v)。写反等于把摩擦加倍（陷阱 #53）
            tau_ff += c * math.tanh(rt.v_filt / eps)

        budget = self.t.torque_budget_frac * jc.torque_max
        pd_budget = budget - abs(tau_ff)
        kp_target = self.t.kp_wall if in_wall else self.t.kp_demo
        if pd_budget <= 0.0:
            kp_eff = kd_eff = 0.0                # 前馈已吃满预算 ⇒ 纯前馈（没有 PD 就没有拒发）
        else:
            av = abs(v)
            kd_eff = min(self.t.kd, self.t.kd_share * pd_budget / av) if av > 1e-9 else self.t.kd
            rem = pd_budget - kd_eff * av
            kp_eff = min(kp_target, max(0.0, rem) / depth) if depth > 1e-9 else kp_target
        joint.set_mit(kp_eff, kd_eff, q_des, 0.0, tau_ff)
        rt.q_des, rt.kp_eff, rt.kd_eff, rt.tau_ff = q_des, kp_eff, kd_eff, tau_ff
        rt.depth, rt.in_wall = depth, in_wall

    def _heartbeat(self, now, st, q, tau_g, scale):
        el = now - self.t0 if self.t0 else 0.0
        parts = []
        for i, n in enumerate(self.order):
            s, rt = st[n], self.rt[n]
            parts.append(f"{n[-1]}:{q[n]:+.3f}(深{rt.depth:.3f},kp{rt.kp_eff:.1f},"
                         f"τg{tau_g[i]:+.2f},{s.temp_mos}/{s.temp_rotor}℃)")
        lk = sum(1 for n in self.order if self.rt[n].latched is not None)
        wall = sum(1 for n in self.order if self.rt[n].in_wall)
        print(f"[teach] {el:6.1f}s scale={scale:.2f} 闩锁{lk}/6 墙内{wall} | " + " ".join(parts))


# ───────────────────────── 预检 ─────────────────────────
def positions_from(arm):
    st = arm.get_state()
    out = {}
    for name in arm.joints:
        s = st.get(name)
        if s is None:
            raise RuntimeError(f"{name} 没有反馈 —— 拿不到实测姿态就不能演示")
        out[name] = s.position
    return out


def preflight(arm, joint_cfgs, align, friction, teach, grav_path, scale, ramp,
              strict_travel=False):
    print(f"[预检] 重力模型 {grav_path}")
    print(f"[预检] 关节顺序 {list(joint_cfgs)}")
    # ① 声明模式必须是 MIT（本脚本只发 MIT 帧）
    for name, jc in joint_cfgs.items():
        if jc.mode != MODE_MIT:
            raise RuntimeError(
                f"{name} 声明模式是 {jc.mode}({MODE_NAMES.get(jc.mode, '?')})，不是 1(MIT) "
                f"—— 本脚本只发 MIT 帧；要改模式请用 dm_registers 写 0x0A 并**另行确认**")
    # ② 实际模式必须也是 1（`mode` 是声明不是读数 —— 陷阱 #8）
    print("[预检] 读 0x0A（CTRL_MODE），每台约 150 ms…")
    for name, jc in joint_cfgs.items():
        tool = RegisterTool(arm.bus, jc.slave_id, jc.motor_type)
        val = tool.read(RID_CTRL_MODE)
        got = int(round(float(val)))
        if got != MODE_MIT:
            raise RuntimeError(
                f"{name}(id={jc.slave_id}) 实际 0x0A={got}（{MODE_NAMES.get(got, '?')}），不是 1(MIT)"
                f" —— 声明与实际不符时 MIT 帧会被电机**静默丢掉**。拒绝启动；"
                f"要用 `dm_registers set --rid CTRL_MODE --value 1` 改（需另行确认）")
        print(f"         {name}: 0x0A={got} ✓")
    # ③ 拿实测姿态
    arm.sync_states()
    q = positions_from(arm)
    print("[预检] 实测姿态（关节侧 rad）:")
    for name in joint_cfgs:
        print(f"         {name}: {q[name]:+.4f}")
    # ④ 自由区（姿态落在软限位外会抛；某侧行程窄只警告）
    zones = build_free_zones(joint_cfgs, q, teach, strict_travel=strict_travel)
    # ⑤ 打印计划 + 预测下垂
    gravity = GravityModel(str(grav_path), list(joint_cfgs),
                           [align[n][0] for n in joint_cfgs])
    tau_g = gravity.tau_ours([align[n][0] * q[n] + align[n][1] for n in joint_cfgs])
    print("\n[计划] 每关节（模型坐标重力项 / 关节侧前馈 / 自由区 / 缓冲带 / 预算 / 上电瞬间的下垂）:")
    for i, name in enumerate(joint_cfgs):
        jc = joint_cfgs[name]
        z = zones[name]
        qn = q[name]
        tau = tau_g[i]
        # 上电瞬间的下垂估计（**必须含软墙**）：scale 低时缺口 = (1−scale)·|τg|，
        # 先在自由区里由 kp_demo 托，托不住就进缓冲带由 kp_wall 托。
        # ⚠️ 只按 deficit/kp_demo 算会严重高估（j3 会算出 0.74 rad，实际 ~0.12）——
        #    因为软墙的 PD 上限是力矩预算，不是 kp_demo。
        deficit = (1.0 - teach.start_scale) * abs(tau)
        free = (z.hi - qn) if tau > 0 else (qn - z.lo)       # 朝下垂方向的自由行程
        kp = max(teach.kp_demo, 1e-9)
        if deficit <= kp * max(free, 0.0):
            sag = deficit / kp
        else:
            sag = max(free, 0.0) + (deficit - kp * max(free, 0.0)) / max(teach.kp_wall, 1e-9)
        pd_budget = max(teach.torque_budget_frac * jc.torque_max - teach.start_scale * abs(tau), 0.0)
        print(f"  {name}: τg={tau:+.3f} N·m  torque_max={jc.torque_max:.1f}  "
              f"{z.describe()}  预算={teach.torque_budget_frac * jc.torque_max:.2f}")
        if sag > 0.02:
            print(f"           ⚠ start_scale={teach.start_scale} 时预计下垂 ≈ {sag:.3f} rad "
                  f"（{math.degrees(sag):.1f}°），升到 --scale 的过程中会自己抬回来")
        if deficit > pd_budget:
            print(f"           ❗ 缺口 {deficit:.2f} N·m 超过软墙可用力矩预算 {pd_budget:.2f} N·m "
                  f"⇒ **软墙托不住**，会一直沉到机械硬限位；请提高 --scale 或用支架托住")
    print(f"\n[计划] scale {teach.start_scale} → {scale}（{ramp:.0f} s 线性）")
    print(f"[计划] kp_demo={teach.kp_demo} kd={teach.kd} kp_wall≤{teach.kp_wall} "
          f"（实际每帧按预算压）")
    return zones, gravity


def main(argv=None):
    ap = argparse.ArgumentParser(description="零重力手推示教（Python 旁路）")
    ap.add_argument("--check", action="store_true", help="只读预检（默认行为）")
    ap.add_argument("--run", action="store_true", help="真正使能并进入跟随循环（需 --yes）")
    ap.add_argument("--yes", action="store_true", help="确认要动真机（--run 必须带）")
    ap.add_argument("--scale", type=float, default=1.0, help="重力前馈最终比例 (0,1]，默认 1.0")
    ap.add_argument("--ramp", type=float, default=None, help="分级升到 --scale 的秒数（默认取 teach.yaml）")
    ap.add_argument("--kp", type=float, default=None, help="覆盖 kp_demo（软弹簧刚度，默认取 teach.yaml）")
    ap.add_argument("--kd", type=float, default=None, help="覆盖 kd（阻尼，默认取 teach.yaml）")
    ap.add_argument("--kp-wall", type=float, default=None, dest="kp_wall",
                    help="覆盖 kp_wall（缓冲带刚度上限，实际每帧仍受力矩预算压）")
    ap.add_argument("--duration", type=float, default=None, help="跑多少秒后正常退出（默认一直跑）")
    ap.add_argument("--strict-travel", action="store_true", dest="strict_travel",
                    help="把「起点某侧自由行程不足」当硬错误"
                         "（默认只警告：那种一侧通常就是机械硬限位）")
    ap.add_argument("--config", default=str(_CFG_DIR / "teach.yaml"))
    ap.add_argument("--joint-config", default=str(_CFG_DIR / "joint.yaml"))
    ap.add_argument("--align", default=str(_REPO / "src/arm_description/config/align.yaml"))
    ap.add_argument("--mit-gains", default=str(_REPO / "src/arm_description/config/mit_gains.yaml"))
    ap.add_argument("--urdf", default=None)
    ap.add_argument("--channel", default=None,
                    help="覆盖 joint.yaml 的串口设备（陷阱 #28：设备号会随插拔在 ttyACM0/1 之间翻转；"
                         "根治是按适配器序列号加 udev 规则固定成 /dev/dm_can）")
    args = ap.parse_args(argv)

    if not 0.0 < args.scale <= 1.0:
        ap.error(f"--scale 必须落在 (0,1]，收到 {args.scale}（重力前馈不允许放大）")
    if args.run and not args.yes:
        ap.error("--run 必须同时给 --yes（要动真机，先手扶/托住，并接受失能即塌）")

    teach = Teach.load(args.config)
    if args.kp is not None:
        teach.kp_demo = args.kp
    if args.kd is not None:
        teach.kd = args.kd
    if args.kp_wall is not None:
        teach.kp_wall = args.kp_wall
    teach.validate()                        # 覆盖之后再校验一次（别让 --kp -1 溜过去）
    joint_cfgs = load_joint_configs(args.joint_config)
    align = load_align(args.align)
    friction = load_friction(args.mit_gains)
    grav_path = find_urdf(args.urdf)
    missing = [n for n in joint_cfgs if n not in align or n not in friction]
    if missing:
        raise RuntimeError(f"align.yaml / mit_gains.yaml 里缺这些关节：{missing}")
    ramp = teach.ramp_sec if args.ramp is None else args.ramp

    arm_cfg = load_arm_config(args.joint_config)
    if args.channel:
        arm_cfg.channel = args.channel
    print(f"[teach] 串口 {arm_cfg.channel}（来自 joint.yaml 的 channel"
          + ("，已被 --channel 覆盖" if args.channel else "") + "）", file=sys.stderr)
    arm = DmArm(arm_cfg)
    try:
        arm.connect()                       # 打开串口即独占（TIOCEXCL）⇒ 与 ros2_control 互斥
    except Exception as e:                  # noqa: BLE001 —— 打开失败就是"另一套在跑"
        print(f"[teach] 打不开串口：{e}\n[teach] 是不是 ros2_control 还开着？"
              f"（mock/真机/本脚本**三者只能开一套**）", file=sys.stderr)
        return 2
    try:
        try:
            zones, gravity = preflight(arm, joint_cfgs, align, friction, teach,
                                       grav_path, args.scale, ramp,
                                       strict_travel=args.strict_travel)
        except Exception as e:              # noqa: BLE001 —— 预检不通过就报清楚，别甩 traceback
            print(f"\n[teach] 预检不通过，**没有使能、没有发过一帧控制帧**：\n        {e}",
                  file=sys.stderr)
            return 2
        if not args.run:
            print("\n[预检] 通过。**没有使能、没有发过一帧控制帧。**"
                  "\n        要真机演示：加 --run --yes（先手扶/托住）", file=sys.stderr)
            return 0

        print("\n[teach] 使能…（MIT 下 enable 发的是零增益帧 ⇒ 出力恒 0，"
              "所以紧接着必须补真实 MIT 帧，否则臂会掉）\n"
              "        ⚠️ 从这一刻到第一帧真实重力帧之间约 30 ms **臂完全没有力矩** —— 手扶好",
              file=sys.stderr)
        arm.enable_all()
        runner = TeachRunner(arm, joint_cfgs, align, friction, zones, gravity, teach,
                             args.scale, ramp, args.duration)
        try:
            # ⚠️ 这里**不要**再 sync_states()：它要 ~100 ms，等于把"零增益窗口"拉长。
            #    预检刚拿到的缓存状态还够用（_clamp_mit_torque 只要缓存里有 pos/vel），
            #    立刻补真实 MIT 帧，把窗口压到最小（C++ 侧实测那个窗口里 j4 掉过 0.104 rad）。
            for _ in range(3):
                runner.step(arm)
                time.sleep(0.01)
            print(f"[teach] 进入跟随循环 {teach.loop_hz:.0f} Hz。"
                  f"现在观察：臂应该变轻/不动，**绝不该朝一个方向猛冲**（猛冲立刻 Ctrl-C）",
                  file=sys.stderr)
            stats = arm.run_feedback_loop(teach.loop_hz, runner.step)
            print(f"[teach] 循环结束 {stats}", file=sys.stderr)
            if stats.get("interrupted"):
                print("[teach] Ctrl-C ⇒ 已先急停（失能）", file=sys.stderr)
        except KeyboardInterrupt:
            print("[teach] Ctrl-C", file=sys.stderr)
        except Exception as e:              # noqa: BLE001 —— 循环已先急停，这里只负责报清楚
            print(f"[teach] 运行中出错（循环已先急停）：{e}", file=sys.stderr)
            return 1
        return 0
    finally:
        # 先失能再关串口（shutdown 自己就是这个顺序）；失能 ⇒ 臂会塌回折叠位（陷阱 #39）
        try:
            arm.shutdown()
        except Exception as e:              # noqa: BLE001
            print(f"[teach] shutdown 出错（请直接断电）：{e}", file=sys.stderr)


if __name__ == "__main__":
    sys.exit(main())
