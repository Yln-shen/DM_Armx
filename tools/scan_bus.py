#!/usr/bin/env python3
"""扫总线：现在这条 CAN 上挂了哪些电机、各是什么型号（**只读**）

为什么要单独一个工具：达妙的 ID 是烧在电机里的，接线换了、多挂一个、或者
"我明明接的是 4340 怎么读出来是 4310"，都得先有个东西能一眼说清楚。7 个关节
陆续上线的过程中这个工具会被反复用到。

**全程只发 0x7FF 刷新帧（查询）和 0x33 读寄存器帧，不写任何寄存器、不使能。**

## 两步

  [1] 扫 ID：对 1~8 逐个发刷新帧，谁应答谁在线。**不发就没反馈**（1:1 规律），
      所以"没反馈"是干净的判据，不需要超时猜。
  [2] 认型号：对在线的 ID 读 0x14(Gr) / 0x15(PMAX) / 0x16(VMAX) / 0x17(TMAX)，
      用这张表对照：

        Gr    PMAX    VMAX    TMAX    型号       备注
        10    12.5    30      10      4310       关节 4-6 + 夹爪
        40    ?       ?       ?       4340P      关节 1-3（1:40）
        1     ?       ?       ?       3507       夹爪（若有）

      4340P **没有 SDK 枚举**（design.md §2.6 坑 2），SDK 表里 DM4340=[12.5,10,28]
      与手册的 40 N·m / 5.86 rad/s 打架 —— 以回读值为准，这也是本工具存在的理由。

## 用法（在 DM_Armx 根目录）

    pixi run python tools/scan_bus.py
    pixi run python tools/scan_bus.py --port /dev/ttyACM0 --ids 1-8
"""
from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "src" / "DMmotor_driver"))
sys.path.insert(0, str(REPO / "src" / "third_party" / "Python例程" / "u2can"))

from DMmotor_driver import dm_bringup as B  # noqa: E402

# 已知型号 → (Gr, PMAX, VMAX, TMAX)。回读值对上哪一行就是哪个型号。
KNOWN = {
    (10, 12.5, 30.0, 10.0): "DM4310     关节4-6/夹爪",
    (40, 12.5, 10.0, 28.0): "DM4340     (SDK 表值)",
    (1, 12.5, 50.0, 5.0): "DM3507     夹爪(若用)",
}

ERR_TEXT = {0x0: "失能", 0x1: "使能", 0x8: "超压", 0x9: "欠压", 0xA: "过流",
            0xB: "MOS过温", 0xC: "线圈过温", 0xD: "通讯丢失", 0xE: "过载"}

# [1] 阶段还不知道型号，只能先按 4310 量程粗解位置。**只影响这里显示的数值**，
# 不影响"谁在线"的判断（那个只看有没有反馈帧）。真值在 [2] 读回来。
ROUGH = (12.5, 30.0, 10.0)


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


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--port", default="/dev/ttyACM0")
    p.add_argument("--ids", default="1-8", help="要扫的 ID，默认 1-8")
    p.add_argument("--detail", action="store_true", default=True,
                   help="对在线电机读 Gr/PMAX/VMAX/TMAX（默认开）")
    p.add_argument("--no-detail", dest="detail", action="store_false")
    p.add_argument("--wait", type=float, default=0.05, help="每个 ID 等反馈的秒数")
    args = p.parse_args()

    ids = parse_ids(args.ids)
    print("=" * 78)
    print(f"scan_bus · {args.port} @921600 8N1   扫描 ID: {args.ids}   【只读，不写寄存器】")
    print("=" * 78)

    ser = B.open_port(args.port, timeout=0.05)
    DM_CAN = B.load_sdk(B.find_sdk_dir(None))
    rx = B.RxBuf()
    try:
        # ── [1] 扫 ID ───────────────────────────────────────────────
        print("\n[1] 扫 ID（发 0x7FF 刷新帧，谁应答谁在线；不发就没反馈）")
        online: list[tuple[int, dict, bytes]] = []
        for sid in ids:
            fb, _raw, _data = B.refresh_and_read(ser, DM_CAN, sid, ROUGH,
                                                 wait=args.wait, rx=rx)
            if not fb:
                print(f"    0x{sid:02X}  —")
                continue
            f = fb[-1]
            can_id = int.from_bytes(f[3:7], "little")
            data8 = f[7:15]
            err = (data8[0] >> 4) & 0x0F
            # 位置解码依赖 TMAX/PMAX，先按 4310 量程粗解；[2] 拿到真值后再修正
            st = B.decode_feedback(data8, (12.5, 30.0, 10.0))
            print(f"    0x{sid:02X}  ✓ 应答   反馈 CAN ID=0x{can_id:03X}   "
                  f"D[0]=0x{data8[0]:02X} (id=0x{data8[0] & 0x0F:X} err=0x{err:X} "
                  f"{ERR_TEXT.get(err, '?')})   T_MOS={st['t_mos_raw']}℃ T_Rotor={st['t_rotor_raw']}℃")
            online.append((sid, st, data8))

        if not online:
            print("\n    总线上一个电机都没应答。检查：")
            print("      · 电机供电了吗（24V）？适配器灯亮吗？")
            print("      · 波特率是 921600 吗？")
            print("      · 被别的程序占着？`fuser -v /dev/ttyACM0`")
            return 1

        if not args.detail:
            return 0

        # ── [2] 认型号 ─────────────────────────────────────────────
        print("\n[2] 认型号（读 Gr/PMAX/VMAX/TMAX；0x15/0x16/0x17 是软件映射范围，")
        print("    **每个 MIT 帧的换算都依赖它**，所以必须回读确认）")
        for sid, _st, _d in online:
            motor = DM_CAN.Motor(DM_CAN.DM_Motor_Type.DM4310, sid, 0x00)
            ctrl = _ParamReader(ser, DM_CAN, motor)
            vals = {}
            for name, rid in (("Gr", 0x14), ("PMAX", 0x15), ("VMAX", 0x16), ("TMAX", 0x17)):
                v, tries = ctrl.read(rid)
                vals[name] = v
                tag = "" if tries == 1 else f"（重试 {tries} 次）"
                print(f"    0x{sid:02X}  {name:<4} (0x{rid:02X}) = {v}{tag}")
            key = (vals["Gr"], vals["PMAX"], vals["VMAX"], vals["TMAX"])
            guess = KNOWN.get(key, None)
            print(f"    → 0x{sid:02X} = {guess or '未知组合，请查手册（并记进 design.md）'}")

        # ── 一致性检查 ──────────────────────────────────────────────
        if len(online) > 1:
            print(f"\n[3] 同总线 {len(online)} 个电机 —— 注意每个 ID 必须唯一，"
                  "重号会互相打架（两个都发、反馈分不清）")
        return 0
    finally:
        ser.close()
        print("\n串口已关闭（未使能过任何电机，电机应处于上次退出时的状态）")


class _ParamReader:
    """把 `B.read_param` 接到一条**已经打开的**串口上。

    SDK 的 `read_motor_param` 要 `self.serial_`，所以这里绕过 `MotorControl.__init__`
    （它会自己开串口）手工塞进去 —— 同一时刻只能有一个程序占串口，不能开两条。
    """

    def __init__(self, ser, DM_CAN, motor):
        self.ser = ser
        self.DM_CAN = DM_CAN
        self.motor = motor
        c = DM_CAN.MotorControl.__new__(DM_CAN.MotorControl)
        c.serial_ = ser
        c.data_save = b""
        c.motors_map = {motor.SlaveID: motor}
        c.Limit_Param = [list((12.5, 30.0, 10.0))] * len(DM_CAN.MotorControl.Limit_Param)
        self.ctrl = c

    def read(self, rid):
        return B.read_param(self.ctrl, self.motor, rid)


if __name__ == "__main__":
    sys.exit(main())
