#!/usr/bin/env python3
"""看门狗（0x09 TIMEOUT）A/B 对照实测 —— 这是 design.md D4 的判据来源

## 为什么要"对照"，而不是"测一次"

只跑 `TIMEOUT=200`、发现"停发帧后电机失能了"是**没有说服力的**：
你排除不掉「MIT 模式下电机本来就会在断流后自己停」这个替代解释。
所以必须有 `TIMEOUT=0` 的对照组 —— 那一组**应该**表现出"停了也不失能"。
两行都拿到，才叫"看门狗生效"。

## 为什么要"卡阈值"，而不是"等 3 秒"

单点观测只能证明"停了会停"，证明不了"是 0x09 这个值在起作用"。所以再加两行，
把停发时长卡在设定值的两侧（这里全用**人话毫秒**，换算见下）：

    TIMEOUT=500ms, 停发 0.3s  → 期望【仍使能】（还没到点）
    TIMEOUT=500ms, 停发 2.0s  → 期望【已失能】（过点了）

这两行一夹，阈值就被夹出来了，而不是我嘴上说的。

## ★ 0x09 的单位是 50µs 计数，不是毫秒

前两轮测试都测废了，根因就是这个（手册 DM-J4310-2EC.md:698 / DM-J4340P-2EC V1.1.md:702）：

> 表示多少个计数周期后仍未检测到 CAN 命令，进行电机保护，**一个计数周期为 50us**，
> 只在电机使能时生效

也就是 1ms = **20 个计数**。所以当时写的 `TIMEOUT=200` 实际只武装了 **10ms**，
`TIMEOUT=1000` 实际只有 **50ms** —— 而给 0x09 赋值这一个写操作本身就要 ~150ms
（`_write` = 基线读 + 写 + 回读，SDK 的 `change_motor_param` 内部还有 sleep 重试循环），
这 150ms **电机侧是全静默的**。两者一比就清楚了：不是"那个洞很邪门"，
是**阈值本来就比那个洞小**，武装这个动作自己就把看门狗触发了。

后来用 `tools/wd_probe.py` 做递增静默扫描独立验证过一遍：写 `0x09 = 10000`（＝500ms）
时，静默 400ms 存活、800ms 触发 → 实测阈值落在 (400, 800] ms，和 500ms 吻合。

**换算只在 `Reg.per_unit` 一处定义**，本文件通过 `Bench.set_timeout(ms)` 调用它，
自己不做任何乘法。人话毫秒进，原始计数出，两个数都会打印。

## 另外两条实测事实

1. **ERR=13（通讯丢失）是锁存的**：实测 `enable(0xFC)`、连发 20 帧 MIT、
   甚至写回 `0x09=0` 都清不掉，**必须电机断电再上电**（USB-CAN 不用动）。
   所以脚本进入每一组前都要求 ERR=0；一旦中途锁存，这一组的结论直接作废，别硬解释。

2. 上一条不只是脚本参数问题，它是驱动设计的硬约束：**运行中做寄存器 I/O 与短看门狗
   天生冲突**。所以正确做法是上电时把 0x09 写进 flash（`set --save`）然后运行期再不碰它。
   而阈值本身也不该贴着控制周期取 —— 见 design.md D4 的取值建议。

## 为什么不停发 3 秒就够，却不能用"轮询"去测

**关键约束：任何一条发出去的帧都会重置看门狗的计时器**（包括读状态用的
0x7FF 刷新帧 —— 对电机来说那也是"主机还活着"的证据）。所以停发期间**一个字
节都不能发**，只能 sleep 到点再看一次。这决定了测试只能是"设定时长 → 静默 →
单点观测"，不能是"循环轮询直到 ERR 跳变"。

## 观测方式：不停发、但不关串口

比拔 USB-CAN 更好：拔线之后 CAN 也断了，**你没法观测**，只能插回去再看，
而且插回去时适配器要重新枚举。这里改成"控制循环停发、进程不退、串口不关"——
电机看到的东西完全一样（没有 CAN 帧），而我们还能继续读 ERR。
这正是"主机没死但控制线程死了"那个场景。

## 安全

- 电机**原地保持**，几乎不转动（q_des = 使能时的位置，误差≈0，力矩只是个静摩擦量级）
- 裸 4310 空载实测静摩擦 ~0.15 N·m，kp 默认 10 → 保持力矩是零头
- 使能瞬间有 `--jump-abort` 跳变保护（同 dm_bringup）
- 启动前要求电机**必须是失能状态** —— 不接管一只已经在出力的电机
- `finally` 里**必定**失能 + 把 TIMEOUT 还原成进入时的值
- 有输出的操作需要 `--yes`

## 用法（DM_Armx 根目录，一切走 pixi）

    pixi run python tools/watchdog_test.py --dry-run     # 先看流程，不碰硬件
    pixi run python tools/watchdog_test.py --yes         # 真跑

跑完记得把结论写进 design.md D4（0x09 那一节现在还挂着"未实测"）。
"""
from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "src" / "DMmotor_driver"))
sys.path.insert(0, str(REPO / "src" / "third_party" / "Python例程" / "u2can"))

from DMmotor_driver import dm_bringup as B          # noqa: E402
from DMmotor_driver import dm_registers as R        # noqa: E402

ENABLE_CMD = 0xFC
DISABLE_CMD = 0xFD

# 期望失能的 ERR 值。0x0 = 正常失能；有些固件在看门狗动作时报 0xD（通讯丢失），
# 也算"看门狗确实动作了" —— 判定时两者都接受，但报告里如实区分。
DISABLED_ERRS = {0x0, 0xD}
ENABLED_ERRS = {0x1}


class Bench:
    """一条**常开**的串口 + 电机的封装。

    刻意不用 SDK 的 `ctrl.enable()` / `ctrl.disable()`：它们内部会调 `recv()`，
    而 `recv()` 自己 `read_all()` 把字节吃掉 —— 我们要自己解 D[6]/D[7] 的温度，
    也要让"发出去什么、什么时候发"完全可控。0xFC/0xFD 两个控制帧的字节与
    `MotorControl.__control_cmd` 完全一致，只是不夹带那次隐藏的读。
    """

    def __init__(self, args, DM_CAN):
        self.args = args
        self.DM_CAN = DM_CAN
        self.timeout = args.timeout
        self.ser = B.open_port(args.port, args.timeout)
        self.motor = B.make_motor(args, DM_CAN)
        self.ctrl = DM_CAN.MotorControl(self.ser)
        self.ctrl.addMotor(self.motor)
        self.rx = B.RxBuf()
        self.limit = (12.5, 30.0, 10.0)   # 占位，read_limit() 会用实机回读值覆盖
        self.enabled = False
        self.dirty_timeout = False        # 是否改过 TIMEOUT（决定 finally 要不要还原）

    # ── 读 ──
    def read_limit(self):
        """读实机 PMAX/VMAX/TMAX。MIT 帧按这三个值线性映射，用错范围=命令值全错。"""
        dims = list(self.limit)
        for i, rid in enumerate((21, 22, 23)):
            val, _ = B.read_param(self.ctrl, self.motor, rid, attempts=2)
            if val:
                dims[i] = float(val)
        self.limit = tuple(dims)
        B.flush_rx(self.ser, self.rx)     # SDK 的读会吃字节，清掉我们这边的残留
        return self.limit

    def query(self, wait: float = 0.2, tries: int = 3):
        """发 0x7FF 刷新帧读一次状态。**注意：这次发送本身会重置看门狗。**"""
        for _ in range(tries):
            frames, _raw, _d = B.refresh_and_read(
                self.ser, self.DM_CAN, self.motor.SlaveID, self.limit, wait=wait, rx=self.rx)
            if frames:
                return B.decode_feedback(frames[0][7:15], self.limit)
        return None

    # ── 写 ──
    def _cmd_frame(self, cmd: int, wait: float | None = None):
        data = bytes([0xFF] * 7 + [cmd])
        B.flush_rx(self.ser, self.rx)
        self.ser.write(B.build_tx(self.motor.SlaveID, data))
        fb = B.read_frames(self.ser, self.rx, want=1, timeout=wait or self.timeout)
        return B.decode_feedback(fb[-1][7:15], self.limit) if fb else None

    def send_mit(self, q, kp, kd, wait: float | None = None):
        """发一帧 MIT 并等它自己那条应答（1:1 规律）。"""
        B.flush_rx(self.ser, self.rx)
        self.ser.write(B.mit_frame(self.motor.SlaveID,
                                   q, 0.0, kp, kd, 0.0, self.limit))
        fb = B.read_frames(self.ser, self.rx, want=1, timeout=wait or self.timeout)
        return B.decode_feedback(fb[-1][7:15], self.limit) if fb else None

    def enable(self):
        self.enabled = True
        return self._cmd_frame(ENABLE_CMD)

    def disable(self):
        st = self._cmd_frame(DISABLE_CMD)
        self.enabled = False
        return st

    def set_timeout(self, ms: int):
        """写 0x09，**参数是人话毫秒**。

        ⚠ 换算规则只存在于 `Reg.per_unit`，别在这里重写：手册说 0x09 的单位是
        「计数周期」，**一个计数周期 50µs**，所以 1ms = 20 个计数。直接写 200 进去
        得到的是 200 个计数 = **10ms** 的看门狗 —— 不是"没生效"，是生效了一个小
        20 倍的阈值，正常控制循环里会疯狂误触发。
        """
        return self.set_timeout_raw(R.BY_NAME["timeout"].to_raw(float(ms)))

    def set_timeout_raw(self, counts: int):
        """按**寄存器原始计数**写 0x09。还原旧值时用这个 —— 读回来的是什么就写回什么。"""
        _b, _ok, after = R._write(self.ctrl, self.motor, self.ser,
                                  R.BY_NAME["timeout"], int(counts), True)
        self.dirty_timeout = True
        B.flush_rx(self.ser, self.rx)
        return after

    # ── 静默 ──
    def starve(self, secs: float):
        """**一个字节都不发**，干等 secs 秒。

        这是整个测试的核心动作：对电机来说，"主机不说话了"和"主机崩了"是同一件事。
        期间绝对不可以调 query() —— 那会重置计时器，测试就废了。
        """
        time.sleep(secs)

    def close(self):
        self.ser.close()


def run_condition(b: Bench, args, timeout_ms: int, starve_s: float, label: str) -> dict:
    print(f"\n{'─' * 78}")
    print(f"  {label}")
    print(f"  TIMEOUT={timeout_ms} ms，使能后停发帧 {starve_s:g}s")
    print(f"{'─' * 78}")

    # [0] 先**解除武装**。上一组结束时看门狗可能还开着（比如 TIMEOUT=1000），
    #     而下面这些步骤（读状态、使能、保持）都要花时间，不能在武装状态下做。
    b.set_timeout(0)

    # 使能前先记录当前位置，使能后拿它当 q_des（原地保持）
    st = b.query()
    if st is None:
        B.die("读不到反馈，不能使能（不知道当前位置就无法保证不跳变）")
    p0 = st["pos"]
    if st["err"] != 0x0:
        B.die(f"电机报错 {st['err']}（{st['err_text']}），先排查再测。\n"
              f"  注意：ERR=13(通讯丢失) 是**锁存**的 —— 实测 enable(0xFC) 和连发 MIT 帧"
              f"都清不掉。\n  唯一已知的清除办法是【电机断电再上电】（USB-CAN 不用动）。")
    if st["t_rotor_raw"] > args.temp_max or st["t_mos_raw"] > args.temp_max:
        B.die(f"温度偏高（MOS {st['t_mos_raw']}℃ / 转子 {st['t_rotor_raw']}℃）"
              f"> {args.temp_max}℃，停")

    # [1] 使能 + 原地保持。此刻看门狗是关的，所以这一段慢一点也没关系。
    print(f"  使能并原地保持（kp={args.kp:g} kd={args.kd:g}，q_des={p0: .4f}）…")
    b.enable()
    b.send_mit(p0, 0.0, 0.0)      # 零力矩一帧，把"使能→第一条命令"的窗口压到最小

    n, period = 0, 1.0 / args.hz
    last = None
    t_end = time.monotonic() + args.warm
    while time.monotonic() < t_end:
        t0 = time.monotonic()
        last = b.send_mit(p0, args.kp, args.kd)
        n += 1
        if last and abs(last["pos"] - p0) > args.jump_abort:
            b.disable()
            B.die(f"位置跳变 {last['pos'] - p0:+.4f} rad 超过 {args.jump_abort:g}，已失能退出")
        dt = period - (time.monotonic() - t0)
        if dt > 0:
            time.sleep(dt)

    hold_err = last["err"] if last else None
    if last is None:
        print(f"    {n} 帧，全程无反馈  ← 这一组废了")
    else:
        print(f"    {n} 帧，保持后 ERR={hold_err}（{last['err_text']}）"
              f"  pos={last['pos']: .4f}"
              f"  T_MOS={last['t_mos_raw']}℃  T_rotor={last['t_rotor_raw']}℃")
    if hold_err not in ENABLED_ERRS:
        print("  ⚠ 电机没进使能态，这一组数据不能用")

    # [2] ★ 武装看门狗。**必须是停发前的最后一步**：给 0x09 赋值这一个写操作实测
    #     要 ~150ms，这 150ms 电机侧是全静默的。所以武装本身就在消耗阈值预算 ——
    #     严格说阈值必须大于这个写耗时，实用上留 2 倍以上余量（默认 500ms）。
    #     前两轮测废都是因为没意识到 0x09 的单位是 50µs 计数：写 200 实际只武装了
    #     10ms，比这个写动作本身还短，于是"武装完就已经触发了"。
    print(f"  武装看门狗 0x09 ← {timeout_ms} …", end="", flush=True)
    t_w = time.monotonic()
    armed = b.set_timeout(timeout_ms)
    dt_w = time.monotonic() - t_w
    print(f" 回读={armed}（这次写耗时 {dt_w * 1000:.0f} ms，期间电机侧静默）")

    # 武装后立刻补一帧：把"武装这个动作本身有没有弄坏电机"和"后面停发多久"
    # 这两件事分开 —— 否则一个 ERR=13 你根本不知道是谁干的。
    st_a = b.send_mit(p0, args.kp, args.kd)
    if st_a is None:
        B.die("武装后收不到反馈，链路掉了")
    print(f"  武装后立刻：ERR={st_a['err']}（{st_a['err_text']}）")
    if st_a["err"] not in ENABLED_ERRS:
        b.disable()
        B.die(f"⚠ 武装 0x09 这个动作**本身**就把电机弄进了 {st_a['err']}"
              f"（{st_a['err_text']}）—— 阈值 {timeout_ms}ms 太短，容不下一次"
              f"寄存器写入（本次 {dt_w * 1000:.0f}ms）。\n"
              f"  → 这一组的结论无效，别记进文档。换大一点的 --test-ms（默认 500ms）。\n"
              f"  → 电机需断电再上电才能清掉这个锁存错误。")

    # [3] ★ 静默：一个字节都不发
    print(f"  停发帧 {starve_s:g}s（静默，期间不能读 —— 读了就重置计时器）…")
    b.starve(starve_s)

    # [4] 单点观测
    st2 = b.query()
    if st2 is None:
        b.disable()
        B.die("静默后读不到反馈，链路可能掉了")
    err = st2["err"]
    fired = err in DISABLED_ERRS
    print(f"  静默后 ERR={err}（{st2['err_text']}） → "
          f"{'已失能' if fired else '仍使能' if err in ENABLED_ERRS else '异常态'}"
          f"   pos={st2['pos']: .4f}（偏移 {st2['pos'] - p0:+.4f}）")

    b.disable()
    time.sleep(0.1)

    return {"label": label, "timeout": timeout_ms, "starve": starve_s,
            "hold_err": hold_err, "err": err, "err_text": st2["err_text"],
            "fired": fired, "pos_delta": st2["pos"] - p0}


def verdict(row: dict) -> tuple[bool, str]:
    """根据这一行是否符合预期，返回 (是否通过, 说明)。"""
    if row["hold_err"] not in ENABLED_ERRS:
        return False, "未进入使能态，数据无效"
    if row["timeout"] == 0:
        ok = not row["fired"]
        return ok, ("看门狗关闭 → 主机静默后电机【仍使能】"
                    if ok else "看门狗关闭却自己失能了 —— 说明电机另有内建超时，"
                               "0x09 的作用需要重新解释")
    expect_fired = row["starve"] * 1000 > row["timeout"]
    ok = row["fired"] == expect_fired
    if expect_fired:
        return ok, (f"停发 {row['starve'] * 1000:.0f}ms > TIMEOUT {row['timeout']}ms "
                    f"→ 期望已失能，实测{'已失能' if row['fired'] else '仍使能'}")
    return ok, (f"停发 {row['starve'] * 1000:.0f}ms < TIMEOUT {row['timeout']}ms "
                f"→ 期望仍使能，实测{'仍使能' if not row['fired'] else '已失能'}")


def main(argv=None) -> int:
    p = argparse.ArgumentParser(
        description="看门狗 0x09 A/B 对照实测",
        formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--port", default="/dev/ttyACM0")
    p.add_argument("--id", type=lambda s: int(s, 0), default=0x01)
    p.add_argument("--fb-id", type=lambda s: int(s, 0), default=None)
    p.add_argument("--type", default="DM4310")
    p.add_argument("--timeout", type=float, default=0.05, help="单次收发串口超时")
    p.add_argument("--sdk-dir", default=None)
    p.add_argument("--kp", type=float, default=10.0,
                   help="保持用的 MIT kp（裸 4340P 要 25~60；4310 10 就够）")
    p.add_argument("--kd", type=float, default=0.8)
    p.add_argument("--hz", type=float, default=100.0, help="保持阶段的发帧频率")
    p.add_argument("--warm", type=float, default=0.6, help="保持阶段时长(s)")
    p.add_argument("--test-ms", type=int, default=500,
                   help="被测的看门狗阈值，**人话毫秒**（CLI 层统一说毫秒，写进寄存器的\n"
                        "原始计数 = ms×20，由 Reg.per_unit 换算并打印）。\n"
                        "B/C 两行就是拿它当分界去卡。\n"
                        "**别取得比 ~300ms 还小**：给 0x09 赋值这一个写操作实测要 ~150ms，\n"
                        "期间电机侧完全静默（见 design.md D4）——阈值贴着它就会自己触发自己。")
    p.add_argument("--starve-short", type=float, default=0.3,
                   help="阈值下侧：应【还没到】失能（须 < test-ms）")
    p.add_argument("--starve-long", type=float, default=1.2,
                   help="阈值上侧：应【已经】失能（须 > test-ms）")
    p.add_argument("--temp-max", type=float, default=60.0)
    p.add_argument("--jump-abort", type=float, default=0.1)
    p.add_argument("--yes", action="store_true")
    p.add_argument("--dry-run", action="store_true")
    args = p.parse_args(argv)

    if args.fb_id is None:
        args.fb_id = 0x10 + args.id

    # B/C 两行的判据就是"停发时长在阈值哪一侧"，两边的间隔不成立结论就无从谈起
    if not (args.starve_short * 1000 < args.test_ms < args.starve_long * 1000):
        B.die(f"--starve-short ({args.starve_short:g}s) < --test-ms ({args.test_ms}ms) "
              f"< --starve-long ({args.starve_long:g}s) 必须成立，否则 B/C 两行"
              f"夹不出阈值。\n  当前：{args.starve_short * 1000:.0f}ms / "
              f"{args.test_ms}ms / {args.starve_long * 1000:.0f}ms")

    plan = [
        ("A 对照：看门狗关闭", 0, args.starve_long),
        ("B 阈值下侧", args.test_ms, args.starve_short),
        ("C 阈值上侧", args.test_ms, args.starve_long),
    ]

    print("=" * 78)
    print(f"看门狗 0x09 A/B 对照实测   电机 0x{args.id:02X} ({args.type})  端口 {args.port}")
    print("=" * 78)
    print("  计划：")
    for label, to, st in plan:
        print(f"    {label:<18} TIMEOUT={to:<4} 停发 {st:g}s")
    print(f"  保持参数 kp={args.kp:g} kd={args.kd:g} @ {args.hz:g}Hz，{args.warm:g}s")
    print("\n  安全前提（readme 真机守则 1/2/3）：电机空载固定、周围无人、能随时断电；")
    print("  全程原地保持几乎不转动；结束后必定失能并把 0x09 还原成进入时的值。")

    if args.dry_run or not args.yes:
        print(f"\n[未执行] {'DRY-RUN' if args.dry_run else '缺少 --yes'}。"
              f"确认现场安全后加 --yes 真跑。")
        return 0

    DM_CAN = B.load_sdk(B.find_sdk_dir(args.sdk_dir))
    R.check_encoding(DM_CAN)      # 编码表与 SDK 对拍，不一致立刻停

    b = Bench(args, DM_CAN)
    rows, orig_timeout = [], None
    try:
        # 先读实机映射范围：**必须在第一次 query() 之前**，否则位置是按占位范围解的，
        # 换上 4340P 就会差 30%（4310 恰好和占位值一致，所以这里不修会静默通过）
        print(f"\n[映射范围] 回读 PMAX/VMAX/TMAX = {b.read_limit()}")

        # 进入时必须失能 —— 不接管一只已经在出力的电机
        st = b.query()
        if st is None:
            B.die("读不到反馈。检查：接线 / 电机供电 / 是否只有一个程序占总线")
        print(f"\n[入口检查] ERR={st['err']}（{st['err_text']}）  pos={st['pos']: .4f} rad"
              f"  T_MOS={st['t_mos_raw']}℃  T_rotor={st['t_rotor_raw']}℃")
        if st["err"] == 0x1:
            B.die("电机当前是【使能】状态。本脚本会接管它，所以要求先失能。\n"
                  "  → 用 dm_bringup 的 jog 跑完自然失能，或先确认它没在出力")
        if st["err"] != 0x0:
            B.die(f"电机报错 {st['err']}（{st['err_text']}），先排查")

        # 读回来的是**原始计数**，还原时也按原始计数写回 —— 中间不经过任何换算，
        # 这样即使这台电机的单位解读错了，退出时也还是把它恢复成进来的样子。
        tmo = R.BY_NAME["timeout"]
        v, _ = B.read_param(b.ctrl, b.motor, tmo.rid, attempts=2)
        orig_timeout = int(v) if v is not None else 0
        print(f"[入口检查] 0x09 TIMEOUT 原值 = {orig_timeout} 计数"
              f"（≈ {tmo.fmt(orig_timeout)}，结束时按原计数还原）")

        for label, to, st_s in plan:
            rows.append(run_condition(b, args, to, st_s, label))

    finally:
        try:
            if b.enabled:
                b.disable()
        except Exception as e:
            print(f"\n  ⚠ 失能失败：{e}   ← 手动断电位")
        try:
            if b.dirty_timeout and orig_timeout is not None:
                b.set_timeout_raw(orig_timeout)
                print(f"\n[还原] 0x09 TIMEOUT 已还原为原始计数 {orig_timeout}"
                      f"（≈ {R.BY_NAME['timeout'].fmt(orig_timeout)}）")
        except Exception as e:
            print(f"\n  ⚠ TIMEOUT 还原失败：{e}")
        b.close()

    # ── 汇总 ──
    print("\n" + "=" * 78)
    print("  结果")
    print("=" * 78)
    # 用 B.pad：'条件'/'停发' 这些表头是 CJK，len() 对不齐
    print("  " + B.pad("条件", 20) + B.pad("TIMEOUT", 10) + B.pad("停发", 9)
          + B.pad("静默后 ERR", 16) + "结论")
    print("  " + "-" * 74)
    n_pass = 0
    for r in rows:
        ok, why = verdict(r)
        n_pass += ok
        print("  " + B.pad(r["label"], 20) + B.pad(f"{r['timeout']} ms", 10)
              + B.pad(f"{r['starve']:g}s", 9)
              + B.pad(f"{r['err']} {r['err_text']}", 16)
              + ("✓ " if ok else "✗ ") + why)
    print()
    print(f"  {n_pass}/{len(rows)} 行符合预期")

    ctrl = rows[0]
    fire = [r for r in rows if r["timeout"] > 0]
    long_starve = [r for r in fire if r["starve"] * 1000 > r["timeout"]]
    short_starve = [r for r in fire if r["starve"] * 1000 <= r["timeout"]]
    if (not ctrl["fired"] and long_starve and all(r["fired"] for r in long_starve)
            and all(not r["fired"] for r in short_starve)):
        print("\n  ★ 结论：0x09 看门狗【确实生效】。")
        print(f"      TIMEOUT=0      → 主机静默 {ctrl['starve']:g}s 后电机【仍使能】"
              f"（这正是 D4 说的危险，也是本测试的对照组）")
        for r in short_starve:
            print(f"      TIMEOUT={r['timeout']:<5} → 静默 {r['starve']:g}s"
                  f"（{r['starve'] * 1000:.0f}ms，未到阈值）→ 【仍使能】")
        for r in long_starve:
            print(f"      TIMEOUT={r['timeout']:<5} → 静默 {r['starve']:g}s"
                  f"（超过阈值）→ 【自动失能】")
        print("      B/C 两行一夹，阈值就被夹出来了 —— 可以写进 design.md D4。")
        print("      **但这只在本次上电有效**，要断电不丢需 set --save。")
    else:
        print("\n  ⚠ 结论不明确，别急着写文档。逐行看上面的 ✗。")

    print("\n  注：本次 0x09 的值**没有写 flash**，断电重新上电即恢复原值。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
