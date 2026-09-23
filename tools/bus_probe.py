#!/usr/bin/env python3
"""MotorBus 真机只读冒烟 —— 这条总线上到底有没有一台电机能跟 MotorBus 说上话

**全程只读**：只发 `0x7FF` 刷新帧（查询），**不使能、不写任何寄存器、不切控制模式、
不调用 set_zero_position**。电机从头到尾不动，可以随时 Ctrl-C。

## 跟 smoke_dm_bus.py 是一对

    tools/smoke_dm_bus.py   假串口，验**时序**（拼帧 / 非阻塞 / 1:1 / flush-发-等）
                            —— 不需要硬件，每次改代码都该跑
    tools/bus_probe.py      真串口，验**通路**（真电机真的应答、路由对不对、
                            非阻塞在真串口上是不是真的）
                            —— 换了电机 / 重接线 / 怀疑硬件时跑

## 验四件事

  [1] 连通：逐台发刷新帧，谁应答谁在线（不发就没反馈，是干净的判据，不用超时猜）
  [2] 路由：反馈只能按 **`D[0] & 0x0F`** 归属，**不能靠 rx 帧的 CAN ID** ——
      [2] 直接把原始帧打出来给你看：CAN ID 全是 0x000，靠它路由会把 5 台挤进同一格
  [3] 非阻塞：空缓冲上连调 `poll()`，单次必须是**微秒级**。若退化成毫秒级，
      说明它在等串口 timeout —— 那 MotorBus 就没有存在的理由了
  [4] 1:1 记账：发 N 轮刷新帧（每轮每台一条），比对 recv 与预期，
      并确认刷新帧计在 `sent_broadcast` 而**没有**混进 `sent`

## ⚠️ 映射范围是写死的，换了电机必须先跑 scan_bus

下面 `LAYOUT` 里的型号是 **2026-09-23 实测**（读 0x14/0x15/0x16/0x17 回读）得到的。
**4310 与 4340P 的 VMAX/TMAX 几乎是互换的**（4340P: 10/28，4310: 30/10），写错档位
解出来的力矩差 2.8 倍**而且不报错**。换了电机先跑：

    pixi run python tools/scan_bus.py

## 用法（在 DM_Armx 根目录）

    pixi run python tools/bus_probe.py
    pixi run python tools/bus_probe.py --ids 1-5 --rounds 30
    pixi run python tools/bus_probe.py --port /dev/ttyACM0

跑之前确认"开机三连"：**电源开了吗 / CAN 线接回来了吗 / 没有别的程序占着串口吗**
（`fuser -v /dev/ttyACM0`）。这三条是台架上最容易假报警的地方 —— 报"0 台应答"
十次有九次是这三条之一，不是工具坏了。
"""
from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "src" / "DMmotor_driver"))

from DMmotor_driver import dm_bus as BUS  # noqa: E402
from DMmotor_driver import dm_frames as F  # noqa: E402

# 型号 → (PMAX, VMAX, TMAX)。SDK 的 Limit_Param 表里 4340 那行与实测不符（见 scan_bus）。
MODEL_LIMITS = {
    "4340P": (12.5, 10.0, 28.0),
    "4310": (12.5, 30.0, 10.0),
}

# 当前台架布局（2026-09-23 实测：0x01-0x03 = 4340P，0x04-0x05 = 4310）。
# 换了接线或电机，**先改这里**（或者改 --ids 只探在线的几台）。
LAYOUT = {0x01: "4340P", 0x02: "4340P", 0x03: "4340P", 0x04: "4310", 0x05: "4310"}

ERR_TEXT = {0x0: "失能", 0x1: "使能", 0x8: "超压", 0x9: "欠压", 0xA: "过流",
            0xB: "MOS过温", 0xC: "线圈过温", 0xD: "通讯丢失", 0xE: "过载"}


def parse_ids(spec: str) -> list[int]:
    """`1-8` 或 `1,3,5` 或 `0x01-0x08`。"""
    out: list[int] = []
    for part in spec.split(","):
        part = part.strip()
        if "-" in part[1:]:
            lo, hi = part.split("-", 1)
            out += list(range(int(lo, 0), int(hi, 0) + 1))
        else:
            out.append(int(part, 0))
    return out


def drain_for(bus, seconds: float) -> int:
    """在 seconds 秒内反复 poll()，返回收到的帧数。

    这就是真实控制循环里"收"那一半的形态：**轮询 + 非阻塞抽干**，不阻塞在串口上。
    """
    end = time.monotonic() + seconds
    got = 0
    while time.monotonic() < end:
        got += bus.poll()
        time.sleep(0.0002)
    return got


def probe_one(bus, motor_id: int, timeout: float = 0.05):
    """发一条刷新帧给某台，等它**这一次**的应答。返回 MotorState 或 None。

    判据是 `timestamp` 变新，而不是"有没有值" —— 缓存里可能还留着上一轮的旧值，
    那样"没应答"会被误判成"应答了"（真机第一次点动就是被这种过期数据坑的）。
    """
    old = bus.get_state(motor_id)
    ts0 = old.timestamp if old is not None else 0.0

    bus.send_refresh(motor_id)
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        bus.poll()
        st = bus.get_state(motor_id)
        if st is not None and st.timestamp > ts0:
            return st
        time.sleep(0.0002)
    return None


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--port", default="/dev/ttyACM0")
    p.add_argument("--ids", default="1-5", help="要探的 ID，默认 1-5")
    p.add_argument("--rounds", type=int, default=30, help="[4] 的轮数，默认 30")
    p.add_argument("--wait", type=float, default=0.05, help="每台等应答的秒数")
    args = p.parse_args()

    ids = parse_ids(args.ids)
    fails: list[str] = []

    def check(cond, label, extra=""):
        print(f"    {'✓' if cond else '✗'} {label}" + (f"   {extra}" if extra else ""))
        if not cond:
            fails.append(label)

    print("=" * 78)
    print(f"bus_probe · {args.port} @921600 8N1   ID: {args.ids}   【只读，不使能、不写寄存器】")
    print("=" * 78)

    # ── 开串口 + 注册映射范围 ───────────────────────────────────────
    print("\n[0] 打开串口 + 注册映射范围（MotorBus 要求每台先注册，未注册的反馈不解码）")
    known = [(i, LAYOUT[i]) for i in ids if i in LAYOUT]
    unknown = [i for i in ids if i not in LAYOUT]
    if unknown:
        print(f"    ⚠️ ID {['0x%02X' % i for i in unknown]} 不在 LAYOUT 表里，"
              "跳过 —— 先跑 scan_bus 确认型号再补进来")
    if not known:
        print("    没有可探的 ID，退出。")
        return 1

    bus = BUS.MotorBus(args.port)
    try:
        bus.open()
        for mid, model in known:
            bus.add_motor(mid, MODEL_LIMITS[model], model)
        print(f"    已注册 {len(known)} 台："
              + "  ".join(f"0x{mid:02X}={model}" for mid, model in known))
        # 清掉开串口时可能已经躺在缓冲里的字节（上一次运行/别的程序的残留）
        bus.flush()

        # ── [1] 连通 ────────────────────────────────────────────────
        print(f"\n[1] 连通（逐台发刷新帧；不发就没反馈，所以'没反馈'是干净判据）")
        online: list[int] = []
        for mid, model in known:
            st = probe_one(bus, mid, timeout=args.wait)
            if st is None:
                print(f"    0x{mid:02X}  —  无应答")
                continue
            online.append(mid)
            print(f"    0x{mid:02X}  ✓  err=0x{st.err:X} {ERR_TEXT.get(st.err, '?'):<6}"
                  f" P={st.pos:>8.4f} rad  V={st.vel:>8.4f} rad/s  T={st.tau:>7.4f} N·m"
                  f"  MOS={st.temp_mos:>3}℃ 转子={st.temp_rotor:>3}℃")
        check(bool(online), f"{len(online)}/{len(known)} 台应答")
        if not online:
            print("\n    一台都没应答。按顺序查这三条（十次有九次是这里）：")
            print("      · 电机供电了吗（24V）？适配器灯亮吗？")
            print("      · CAN 线接回来了吗？")
            print("      · 被别的程序占着？`fuser -v %s`" % args.port)
            return 1

        # ── [2] 路由的原始证据 ──────────────────────────────────────
        print("\n[2] 路由 —— 为什么不能靠 rx 帧的 CAN ID（直接看原始字节）")
        bus.flush()
        for mid in online:
            bus.send_refresh(mid)
        raw = F.read_frames(bus.ser, bus.rx, want=len(online), timeout=0.2)
        print(f"    发出 {len(online)} 条刷新帧，拿回 {len(raw)} 条原始帧：")
        can_ids, data_ids = [], []
        for f in raw:
            cid = int.from_bytes(f[3:7], "little")
            did = f[7] & 0x0F
            can_ids.append(cid)
            data_ids.append(did)
            print(f"      rx 帧 CAN ID=0x{cid:03X}   D[0]=0x{f[7]:02X} → ID={did}  "
                  f"err=0x{(f[7] >> 4) & 0x0F:X}")
        check(all(c == 0 for c in can_ids),
              "所有反馈帧的 CAN ID 都是 0x000",
              "← 靠 CAN ID 路由的话 5 台会挤进同一格，而且那格是 0")
        check(sorted(data_ids) == sorted(online),
              f"D[0]&0x0F 恰好给出 {len(online)} 个互不相同的 ID",
              "← 这才是唯一可靠的归属依据")
        check(not bus.unknown_ids,
              f"没有出现未注册 ID（unknown_ids={sorted(bus.unknown_ids)}）")

        # ── [3] poll() 真的不阻塞吗 ─────────────────────────────────
        print("\n[3] poll() 非阻塞 —— 空缓冲上连调，单次必须是微秒级")
        bus.flush()
        drain_for(bus, 0.05)          # 先彻底抽干，确保测的是"空"的情况
        n_call = 1000
        t0 = time.perf_counter()
        total_got = 0
        for _ in range(n_call):
            total_got += bus.poll()
        dt = time.perf_counter() - t0
        per_us = dt / n_call * 1e6
        print(f"    {n_call} 次 poll() 共 {dt * 1000:.2f} ms → 单次 {per_us:.1f} µs"
              f"（期间收到 {total_got} 帧）")
        check(per_us < 1000.0,
              f"单次 poll() = {per_us:.1f} µs，远小于串口 timeout",
              "← 若接近 3000µs 说明它在等串口超时，那就退化成了阻塞读")

        # ── [4] 1:1 记账 ────────────────────────────────────────────
        print(f"\n[4] 1:1 记账 —— {args.rounds} 轮 × {len(online)} 台 = "
              f"{args.rounds * len(online)} 条刷新帧")
        bus.flush()
        bus.n_sent = bus.n_recv = bus.n_sent_broadcast = 0   # 从干净状态起算
        bus.rx_bytes = 0
        for _ in range(args.rounds):
            for mid in online:
                bus.send_refresh(mid)
            drain_for(bus, 0.002)      # 一轮里只给 2ms 收；收不满下一轮补
        drain_for(bus, 0.05)           # 最后一轮给够时间

        st = bus.stats()
        sent_bc, recv = st["sent_broadcast"], st["received"]
        expect_1to1 = args.rounds * len(online)
        print(f"    sent_broadcast={sent_bc}  sent={st['sent']}  received={recv}")
        print(f"    预期（每台各回一条）= {expect_1to1}")
        check(st["sent"] == 0,
              "刷新帧计在 sent_broadcast，没有混进 sent",
              f"sent={st['sent']}（混进去的话比值会假性 >100%）")
        check(sent_bc == expect_1to1,
              f"实际发出 {sent_bc} 条刷新帧，与预期一致")
        check(recv == expect_1to1,
              f"收回 {recv} 条，与预期 {expect_1to1} 一致",
              "" if recv == expect_1to1
              else f"← 差 {expect_1to1 - recv} 条；若恰好是 {expect_1to1 * len(online)}"
                   " 则是所有电机都应答了广播（见下）")
        if recv != expect_1to1:
            if recv == expect_1to1 * len(online):
                print(f"    ⚠️ 收回条数正好是预期的 {len(online)} 倍 —— "
                      "说明 0x7FF 在 CAN 层被所有电机当广播应了，不是每台只回自己的。")
                print("       这不是 bug，但要写进 design.md：**刷新帧的 1:1 假设不成立**。")
            else:
                print(f"    ⚠️ 既不是 1:1 也不是 N 倍，差值 {expect_1to1 - recv} —— "
                      "可能是丢帧（总线负载/线缆），也可能是残留被 flush 掉了。")
                print("       先看 write_max_ms 和 pending_bytes，再决定是不是硬件问题。")
        print(f"    写延迟：avg={st['write_avg_ms']:.3f} ms  p99={st['write_p99_ms']:.3f} ms"
              f"  max={st['write_max_ms']:.3f} ms")
        print(f"    残留字节={st['pending_bytes']}  rx_bytes={st['rx_bytes']}  "
              f"unknown_ids={st['unknown_ids']}")
        print("    （残留字节不必为 0：切帧时尾部残片本来就该留着等下一圈拼上）")

        # ── 结论 ────────────────────────────────────────────────────
        print("\n" + "=" * 78)
        if fails:
            print(f"有 {len(fails)} 项不通过：")
            for f in fails:
                print(f"  ✗ {f}")
        else:
            print("全部通过：MotorBus 在真机上能收发、路由正确、poll() 非阻塞、1:1 记账对得上")
        print("=" * 78)
        return 1 if fails else 0
    finally:
        bus.close()
        print("\n串口已关闭（**未使能过任何电机，未写任何寄存器**，电机应在原状态）")


if __name__ == "__main__":
    sys.exit(main())
