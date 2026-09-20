#!/usr/bin/env python3
"""0x09 看门狗阈值实测 —— 验证"我说的 ms 就是电机的 ms"

## 它现在回答什么问题

单位那一页已经翻过去了：手册（DM-J4310-2EC.md:698 / DM-J4340P-2EC V1.1.md:702）
明写「**一个计数周期为 50us**，只在电机使能时生效」，所以 1ms = 20 个计数。
`dm_registers` 已经在 `Reg.per_unit` 里把这条换算吃掉了，CLI 层只说毫秒。

但"手册这么写"不等于"这台电机就这么干"。所以本工具做一件事：

> **请求 `--arm-ms 500`，然后实测有效阈值是不是真的落在 500ms 附近。**

它是**单位换算的端到端校验**，而不是（原来那样）去猜单位是什么。区别在于：
即使我对 50µs 的理解是错的，只要换算和电机实际行为一致，这里就验不出来；
反过来，只要实测阈值和请求的毫秒数差了 20 倍，这里一定会炸。

## 为什么用"递增静默扫描"而不是"设个值等一等"

**关键约束：任何发出去的帧都会重置看门狗的计时器**（读状态用的 0x7FF 刷新帧
也算 —— 对电机来说那就是"主机还活着"）。所以：
- **不能轮询**：想"每 100ms 看一眼 ERR 跳没跳"，每看一次就把计时器清零了，永远测不到。
- 只能"静默固定时长 → 单点观测"。

而 ERR=13 是**锁存**的（实测 enable(0xFC) / 连发 MIT 帧 / 写回 0x09=0 都清不掉，
只能断电）。所以**每次触发都要花一次断电**。

于是唯一高效的做法就是：**在一轮里按升序扫静默时长，第一个触发的那次就停**。
这样一次上电就能把阈值夹在「最后存活」和「首次触发」之间。

默认扫描点从 `--arm-ms` 推导（0.3× / 0.6× / 0.9× / 1.2× / 1.8× / 3×），
所以它自动围着你要验的那个值收拢 —— 阈值夹出来一定在请求值的上下，
不在这条带子里就说明换算错了。

## 用法（跑之前电机要断电再上电，清掉可能锁存的 ERR=13）

    pixi run python tools/wd_probe.py --mode status                 # 只读
    pixi run python tools/wd_probe.py --mode sweep --yes            # 验 500ms
    pixi run python tools/wd_probe.py --mode sweep --arm-ms 200 --yes
"""
from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "src" / "DMmotor_driver"))
sys.path.insert(0, str(REPO / "src" / "third_party" / "Python例程" / "u2can"))
sys.path.insert(0, str(REPO / "tools"))

from DMmotor_driver import dm_bringup as B          # noqa: E402
from DMmotor_driver import dm_registers as R        # noqa: E402
from watchdog_test import Bench                     # noqa: E402

# 静默时长相对**请求阈值**的取点：往下侧至少一个点，往上侧若干个。
# 只要实测的第一个触发点落在请求值附近，换算就是对的。
SWEEP_FACTORS = (0.3, 0.6, 0.9, 1.2, 1.8, 3.0)


def sweep_from(arm_ms: float) -> list[float]:
    """由请求的阈值(ms)推导扫描点(s)，自动围着它收拢。"""
    return sorted({round(arm_ms * f / 1000.0, 3) for f in SWEEP_FACTORS})


def show(b: Bench, tag: str):
    st = b.query()
    if st is None:
        print(f"  {tag:<32} 无反馈")
        return None
    print(f"  {tag:<32} ERR={st['err']:<3} {st['err_text']:<16} pos={st['pos']: .4f}")
    return st


def hold_for(b: Bench, args, secs: float, p0: float | None):
    """原地保持 secs 秒，返回 (发了多少帧, 最后一条反馈, 参考位置)。"""
    t_end, period, n, last = time.monotonic() + secs, 1.0 / args.hz, 0, None
    while time.monotonic() < t_end:
        t0 = time.monotonic()
        q = last["pos"] if last else (p0 if p0 is not None else -0.0849)
        last = b.send_mit(q, args.kp, args.kd)
        n += 1
        if last and p0 is not None and abs(last["pos"] - p0) > args.jump_abort:
            b.disable()
            B.die(f"位置跳变 {last['pos'] - p0:+.4f} rad，已失能退出")
        dt = period - (time.monotonic() - t0)
        if dt > 0:
            time.sleep(dt)
    return n, last, (last["pos"] if last else p0)


def run_default_mode(b: Bench, args, st, orig_timeout: int | None) -> int:
    """验『电机的上电默认值』是否真的在保护 —— **全程不写 0x09**。

    为什么单独有这个模式：`sweep` 是"我先写一个值进去，再测它管不管用"，证明的是
    **写入路径 + 看门狗**都对。但驱动实际依赖的不是这个 —— 驱动指望的是「电机一上电
    就带着保护」（`set --save` 的产物），而这条链路上没有谁去写 0x09。
    两者会不一致的典型场景：flash 写失败了、或写成功了但被别的程序覆盖了。
    所以这里一个字节都不写，只使能 → 静默 → 看它自己会不会退。
    """
    tmo = R.BY_NAME["timeout"]
    if orig_timeout in (None, 0):
        B.die(f"上电默认 0x09 = {orig_timeout}（即**没开保护**），没得测。\n"
              f"  先 `set --rid 0x09 --value 500 --commit --save` 并断电重启，再来跑这个。")

    print("\n" + "=" * 80)
    print(f"  验上电默认值：0x09 = {orig_timeout} 计数（≈ {tmo.fmt(orig_timeout)}）")
    print(f"  **全程不写 0x09** —— 测的就是它自己上电带来的那个值")
    print("=" * 80)

    # 使能 + 原地保持。注意这里**不能**先 set_timeout(0)：那正是要避免的"偷偷拆保护"。
    b.enable()
    p0 = st["pos"]
    b.send_mit(p0, 0.0, 0.0)
    n, last, p0 = hold_for(b, args, args.hold, p0)
    print(f"\n[0] 使能并原地保持 @ {args.hz:g}Hz，{n} 帧，"
          f"ERR={last['err'] if last else '?'}  pos={p0: .4f}")
    if not last or last["err"] != 0x1:
        B.die("没进使能态，测不了")

    # 计数→人话毫秒（走 tmo 自己的换算，别在这里再写一份 /20），再 ×2 留余量
    starve = max(2.0 * tmo.to_human(orig_timeout) / 1000.0, 1.2)
    print(f"\n[1] 停发帧 {starve:.2f}s（≈ 上电默认阈值的 2 倍），期间一个字都不发…")
    b.starve(starve)
    st2 = b.query()
    if st2 is None:
        B.die("静默后读不到反馈，链路可能掉了")

    err = st2["err"]
    print(f"    静默后 ERR={err}（{st2['err_text']}）   pos={st2['pos']: .4f}")
    print()
    if err in (0x0, 0xD):
        print("  ★ 通过：**电机靠自己的上电默认值就退出了使能** ——")
        print("    驱动不需要在运行期写任何寄存器，保护是天生就有的。")
        print("    这正是 D4 想要的形态（运行期零寄存器 I/O，见 D4 三条约束的第 2 条）。")
        rc = 0
    elif err == 0x1:
        print("  ✗ 不通过：静默超过阈值后**电机仍在使能** ——")
        print("    上电默认没起作用。查：flash 是否真的写进去了（本模式前面读到的值）、")
        print(f"    读到的 {orig_timeout} 计数是不是被误解成了更大的阈值。")
        rc = 1
    else:
        print(f"  ? 异常态 ERR={err}，别急着下结论")
        rc = 1
    b.disable()
    return rc


def main(argv=None) -> int:
    p = argparse.ArgumentParser(description="0x09 看门狗行为探针")
    p.add_argument("--mode", choices=["status", "sweep", "default"], default="status",
                   help="status=只读；sweep=武装指定值扫阈值；\n"
                        "default=**不写 0x09**，直接验'电机的上电默认值'是否真的在保护"
                        "（这才是驱动实际依赖的那种配置）")
    p.add_argument("--port", default="/dev/ttyACM0")
    p.add_argument("--id", type=lambda s: int(s, 0), default=0x01)
    p.add_argument("--fb-id", type=lambda s: int(s, 0), default=None)
    p.add_argument("--type", default="DM4310")
    p.add_argument("--timeout", type=float, default=0.05)
    p.add_argument("--sdk-dir", default=None)
    p.add_argument("--kp", type=float, default=10.0)
    p.add_argument("--kd", type=float, default=0.8)
    p.add_argument("--hz", type=float, default=200.0)
    p.add_argument("--hold", type=float, default=0.6, help="武装前的保持时长(s)")
    p.add_argument("--arm-ms", type=int, default=500,
                   help="请求的看门狗阈值，**人话毫秒**（换算是 Reg.per_unit 的事）。\n"
                        "扫描点默认围着它取，所以改这个值整轮就跟着挪。")
    p.add_argument("--sweep", type=float, nargs="*", default=None,
                   help="递增静默时长(s)，第一个触发就停。默认由 --arm-ms 推导")
    p.add_argument("--jump-abort", type=float, default=0.1)
    p.add_argument("--yes", action="store_true")
    args = p.parse_args(argv)
    if args.fb_id is None:
        args.fb_id = 0x10 + args.id

    sweep = sorted(args.sweep) if args.sweep else sweep_from(args.arm_ms)
    # 静默点不能贴着"写一次寄存器"的耗时：写 0x09 实测要 ~150ms，期间电机侧全静默，
    # 比它短的扫描点测的其实是"写这个动作"，不是我们要验的阈值。
    if min(sweep) < 0.12:
        B.die(f"扫描点里有 {min(sweep) * 1000:.0f}ms —— 太短了。写一次 0x09 要 ~150ms，"
              f"比它短的静默测不出阈值。要么加大 --arm-ms，要么显式给 --sweep。")

    if args.mode == "status":
        pass
    elif not args.yes:
        print("[未执行] 会动电机，确认现场后加 --yes")
        return 0

    DM_CAN = B.load_sdk(B.find_sdk_dir(args.sdk_dir))
    R.check_encoding(DM_CAN)
    b = Bench(args, DM_CAN)
    try:
        print("=" * 80)
        print(f"0x09 探针 · mode={args.mode}   电机 0x{args.id:02X} ({args.type})")
        print("=" * 80)
        print(f"[映射范围] {b.read_limit()}")
        st = show(b, "入口状态")
        if args.mode == "status":
            return 0
        if st is None or st["err"] != 0x0:
            B.die(f"电机不是干净状态（ERR={st['err'] if st else '?'}）。\n"
                  f"  ERR=13 是锁存的：enable(0xFC) / 连发 MIT 帧 / 写回 0x09=0 都清不掉。\n"
                  f"  **必须电机断电再上电**（USB-CAN 不用动），然后 --mode status 确认 ERR=0")

        # 记下**进来时**的 0x09（原始计数），结束时还原成它而不是无条件写 0
        tmo = R.BY_NAME["timeout"]
        orig_timeout, _ = B.read_param(b.ctrl, b.motor, tmo.rid, attempts=2)
        orig_timeout = int(orig_timeout) if orig_timeout is not None else None
        print(f"[入口] 0x09 TIMEOUT = {orig_timeout} 计数"
              f"（≈ {tmo.fmt(orig_timeout)}）")

        if args.mode == "default":
            return run_default_mode(b, args, st, orig_timeout)

        # [0] 解除武装 + 使能保持
        b.set_timeout(0)
        b.enable()
        b.send_mit(st["pos"], 0.0, 0.0)
        n, last, p0 = hold_for(b, args, args.hold, st["pos"])
        print(f"\n[0] 使能并原地保持 @ {args.hz:g}Hz，{n} 帧，"
              f"ERR={last['err'] if last else '?'}  pos={p0: .4f}")
        if not last or last["err"] != 0x1:
            B.die("没进使能态，测不了")

        # [1] ★ 武装。这一步本身有个 ~150ms 的洞（写寄存器要这么久，且期间无法发帧）
        print(f"\n[1] 武装 0x09 ← 请求 {args.arm_ms} ms"
              f"（这一步有个 ~150ms 的洞，顺便量一下真实的洞）")
        b.send_mit(p0, args.kp, args.kd)              # 洞起点：武装前最后一帧
        t_last_frame = time.monotonic()
        armed = b.set_timeout(args.arm_ms)
        t_write_done = time.monotonic()
        st_a = b.send_mit(p0, args.kp, args.kd)       # 洞终点：武装后第一帧
        hole = time.monotonic() - t_last_frame
        print(f"    回读（原始计数）={armed}  写耗时 {(t_write_done - t_last_frame) * 1000:.0f} ms，"
              f"**电机侧实际静默 {hole * 1000:.0f} ms**")
        print(f"    洞后第一帧 ERR={st_a['err'] if st_a else '?'}"
              f"（{st_a['err_text'] if st_a else '无反馈'}）")
        if not st_a or st_a["err"] != 0x1:
            print(f"\n  ✗ 武装这个动作本身就把电机打进了 ERR={st_a['err'] if st_a else '?'}。")
            print(f"    洞只有 {hole * 1000:.0f} ms。如果它小于请求的 {args.arm_ms} ms，")
            print(f"    那就说明**电机实际生效的阈值比请求的小** —— 换算方向反了或系数错了。")
            print(f"    （若洞本来就 > 请求值，那只是请求值太小，加大 --arm-ms 即可。）")
            print(f"    两种情况下电机都需要断电再上电才能清掉这个锁存错误。")
            return 1
        print(f"    ✓ {hole * 1000:.0f} ms 的静默没触发它，请求阈值 {args.arm_ms} ms"
              f" → 至少说明电机认的阈值 ≥ {hole * 1000:.0f} ms，换算方向是对的")

        # [2] ★ 递增静默扫描，第一个触发就停
        print(f"\n[2] 递增静默扫描（每次静默 → 单点观测 → 立刻续上发帧）")
        print(f"    扫描点由请求阈值推导：{', '.join(f'{s * 1000:.0f}' for s in sweep)} ms")
        print(f"    {'静默':>8}{'':<4}{'观测 ERR':<20}判定")
        print("    " + "-" * 62)
        survived, tripped = None, None
        for s in sweep:
            print(f"    {s * 1000:>6.0f}ms    ", end="", flush=True)
            b.starve(s)                                # ★ 一个字都不发
            st_s = b.query()                           # 单点观测（这一发会重置计时器）
            if st_s is None:
                B.die("静默后读不到反馈，链路可能掉了")
            err, txt = st_s["err"], st_s["err_text"]
            if err == 0x1:
                print(f"ERR={err} {txt:<14} 仍使能 ✓ 存活")
                survived = s
                hold_for(b, args, 0.3, p0)             # 补一段保持，把状态稳回来
            else:
                print(f"ERR={err} {txt:<14} ✗ **首次触发**")
                tripped = s
                break

        # [3] 结论：实测阈值 vs 请求毫秒，是否一致
        print("\n" + "=" * 80)
        print(f"  请求阈值 0x09 = {args.arm_ms} ms   武装时的洞 {hole * 1000:.0f} ms")
        if tripped is not None:
            print(f"  静默 {survived * 1000:.0f} ms → 存活；"
                  f"静默 {tripped * 1000:.0f} ms → 触发")
            print(f"\n  ★ 实际阈值落在 **{survived * 1000:.0f} ~ {tripped * 1000:.0f} ms**")
            ratio = args.arm_ms / (tripped * 1000)
            if survived * 1000 <= args.arm_ms <= tripped * 1000:
                print(f"    ✓ 请求值 {args.arm_ms} ms 正好夹在这个区间里 ——")
                print(f"      **换算是对的：CLI 说的毫秒，就是电机认的毫秒。**")
                print(f"      语义也确认了：距最后一条 CAN 帧超过阈值 → 失能（ERR=13）。")
            elif ratio > 5 or ratio < 0.2:
                print(f"    ✗ 请求 {args.arm_ms} ms，实测却在 {survived * 1000:.0f} ~ "
                      f"{tripped * 1000:.0f} ms —— 差了约 {1 / ratio:.1f} 倍。")
                print(f"      **换算错了。** 先看 Reg.per_unit 的系数对不对，"
                      f"再回头核对手册那句话。")
            else:
                print(f"    ~ 请求 {args.arm_ms} ms，实测区间是它的 {ratio:.1f} 倍 ——"
                      f" 不在区间内但同量级。可能是扫描点密度不够，"
                      f"也可能是电机内部还有别的延时。收紧 --sweep 再测一次。")
        else:
            print(f"  全部静默档（最大 {sweep[-1] * 1000:.0f} ms）都存活 → "
                  f"阈值 > {sweep[-1] * 1000:.0f} ms，或者根本没武装上。")
            if armed in (None, 0):
                print(f"    ⚠ 武装回读是 {armed} —— 值没写进去，整轮无效，先查写入路径。")
            else:
                print(f"    回读是 {armed}（非 0），所以值写进去了。那问题在别处：")
                print(f"    要么电机认的单位比换算出的更大（阈值其实更大），要么看门狗"
                      f"在这台上压根没生效。加大 --arm-ms 再来一轮。")
        return 0

    finally:
        try:
            if b.enabled:
                b.disable()
        except Exception as e:
            print(f"\n  ⚠ 失能失败：{e}")
        # 还原成**进来时的值**，不是无条件写 0。
        # 0x09 现在是 flash 里的持久配置（上电默认就带看门狗），无条件写 0 会把
        # 运行中的保护悄悄拆掉 —— 而 flash 里还是 500ms，于是"看起来配好了、
        # 实际这次上电没保护"。只在本轮真改过它（dirty_timeout）时还原。
        try:
            if b.dirty_timeout and orig_timeout is not None:
                b.set_timeout_raw(orig_timeout)
                print(f"\n[还原] 0x09 已还原为进来时的原始计数 {orig_timeout}")
            elif not b.dirty_timeout:
                print(f"\n[未改动] 0x09 保持电机的上电默认值（本轮没写过它）")
        except Exception as e:
            print(f"\n  ⚠ 0x09 还原失败：{e}   ← 注意 motor 侧保护可能处于非预期状态")
        b.close()


if __name__ == "__main__":
    sys.exit(main())
