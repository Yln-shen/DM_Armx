#!/usr/bin/env python3
"""dm_registers —— 寄存器读写（PID / 映射范围 / 看门狗）：**先读后写、可回滚**

## 和 dm_bringup.py 的分工

    dm_bringup     只读 + 点动，**永不写寄存器** —— 用来验"链路本身通不通"
    dm_registers   读**并且写**寄存器 —— 用来改 PID / 映射范围 / 看门狗

这个边界是刻意的：dm_bringup 敢在你电机上随便跑，正因为它是只读的。
要改配置就得换这个工具，而它自带回滚。

## 写操作的三条纪律（design.md D7「PID 先读后写、可回滚」）

  1. **没有基线不许写**。`set` 在真写之前，会自动把该寄存器的当前值落到
     `registers/<id>/<时间戳>_pre.json`。改坏了用 `restore` 回去。
  2. **写之前必须清 SDK 的寄存器缓存**（本文件最容易踩的坑，见 `_write`）：
     `Motor.temp_param_dict` 缓存的是"上次读到的值"，而 SDK 的
     `change_motor_param` 判断成败的方式就是**拿缓存值跟目标值比**。
     不清缓存 → 它拿**旧值**跟新值比 → 返回 `False`，但**其实已经写进去了**。
     你会以为写失败，反复重试，甚至去查线——这是个纯软件陷阱。
  3. **默认 dry-run**。不加 `--commit` 一个字节都不写。

## 编码类型由 RID 决定（写错就是静默写坏）

`DM_CAN.is_in_ranges(RID)` 决定这个寄存器按 uint32 还是 float32 编解码：
**只有 RID 7~10 / 13~16 / 35~36 是 uint32，其余全是 float32**（`DM_CAN.py:606`）。
所以 `MST_ID(0x07)` / `TIMEOUT(0x09)` / `CTRL_MODE(0x0A)` 按**整数**写，
`Gr(0x14)` / `PMAX~TMAX(0x15-17)` / `KP/KI(0x19-1C)` 按**浮点**写。
SDK 内部自己分派，所以这里只需保证传进去的是 int 还是 float 正确。
`list` 子命令会把这张表连编码类型一起打出来。

## 子命令

    list     打印本工具认识的寄存器表（RID / 名字 / 编码类型 / 可读可写 / 含义）
    dump     读一批寄存器 → 打印 + 落盘 JSON（这是"先读后写"里的"先读"）
    verify   读型号 / 控制模式 / 看门狗 / 映射范围，逐条和期望值比对
    set      写单个寄存器（**写前自动 dump 基线**；默认 dry-run）
    restore   从 dump 出的 JSON 回写

## 用法（DM_Armx 根目录，一切走 pixi；改完代码不用重编译）

    # 先看有哪些寄存器、哪些是只读的
    pixi run python src/DMmotor_driver/DMmotor_driver/dm_registers.py list

    # 读基线（无风险）。落盘到 registers/<id>/
    pixi run python src/DMmotor_driver/DMmotor_driver/dm_registers.py dump --id 0x01
    pixi run python src/DMmotor_driver/DMmotor_driver/dm_registers.py verify --id 0x01 --type DM4340

    # 写：默认 dry-run，只会打印"将把 X 从 A 改成 B"
    pixi run python src/DMmotor_driver/DMmotor_driver/dm_registers.py set --id 0x01 --rid 0x09 --value 200
    # 真写：加 --commit。默认**不 save**，所以断电即回到原值
    pixi run python src/DMmotor_driver/DMmotor_driver/dm_registers.py set --id 0x01 --rid 0x09 --value 200 --commit
    # 要写进 flash（断电不丢）再加 --save —— 会二次确认，且打印断电后才会生效
    pixi run python src/DMmotor_driver/DMmotor_driver/dm_registers.py set --id 0x01 --rid 0x0A --value 2 --commit --save

    # 回滚
    pixi run python src/DMmotor_driver/DMmotor_driver/dm_registers.py restore --file registers/01/20260920-120000_pre.json --commit

设计依据见 src/DMmotor_driver/design.md D7（先读后写）/ §2.4（寄存器表）/ D4（看门狗）。

## 依赖说明

链路原语（`open_port` / `load_sdk` / `find_sdk_dir` / `make_motor`）直接复用
`dm_bringup.py` —— 那是目前唯一在真机上验证过的收发代码。等 `MotorBus` 落地后，
两者都应迁到 `MotorBus` 上。**这里不重复实现一遍**，否则同一段协议代码会有两份，
而协议代码"有两份"比"有依赖"危险得多。
"""
from __future__ import annotations

import argparse
import json
import sys
import time
from dataclasses import dataclass
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO / "src" / "DMmotor_driver"))
sys.path.insert(0, str(REPO / "src" / "third_party" / "Python例程" / "u2can"))

from DMmotor_driver import dm_bringup as B  # noqa: E402


# ───────────────────────── 1. 寄存器表 ─────────────────────────

# 编码类型表。**这里必须与 SDK 的 `DM_CAN.is_in_ranges` 逐字一致**（DM_CAN.py:606）：
#     def is_in_ranges(number):
#         return (7 <= number <= 10) or (13 <= number <= 16) or (35 <= number <= 36)
# 不直接调 SDK 那个函数，是因为 `list` 子命令要能在**不接硬件、不加载 SDK** 时跑
# （表本身就是文档）。代价是要自己维护 —— 所以下面 `check_encoding()` 会在
# 每次接上硬件时拿 SDK 的真函数跟这张表**全量对拍**，不一致直接终止。
_INT_RIDS = frozenset(range(7, 11)) | frozenset(range(13, 17)) | frozenset(range(35, 37))


def check_encoding(DM_CAN) -> None:
    """把本地编码表和 SDK 的 `is_in_ranges` 全量对拍。不一致就别写任何东西。"""
    bad = [rid for rid in range(0x00, 0x40)
           if (rid in _INT_RIDS) != bool(DM_CAN.is_in_ranges(rid))]
    if bad:
        B.die(
            "本文件的编码表与 SDK 的 DM_CAN.is_in_ranges 不一致，RID "
            + ", ".join(f"0x{r:02X}" for r in bad)
            + "\n  → SDK 可能被更新过。**在修好之前不要用 set**："
              "按错误的类型编码写寄存器不会报错，只会静默写坏。\n"
              f"  本地表：{sorted(_INT_RIDS)}\n"
              "  请把 dm_registers.py 的 _INT_RIDS 改成与 DM_CAN.py 的 is_in_ranges 一致"
        )


@dataclass(frozen=True)
class Reg:
    rid: int
    name: str
    unit: str
    note: str
    w: bool = True              # 可写？RO 的写进去会被电机忽略或报错，直接拦掉
    lo: float | None = None     # 安全写入范围（None = 不做范围检查，只提示）
    hi: float | None = None
    per_unit: float = 1.0       # 1 个「人话单位」= 多少个寄存器计数。见下
    # per_unit 存在的唯一理由：**有些寄存器的计数单位不是人话**。
    # 0x09 TIMEOUT 就是典型 —— 手册：「整数 32 位，表示多少个计数周期……
    # 一个计数周期为 50us」→ 1ms = 20 个计数。谁要是按"毫秒"直接写 200，
    # 实际拿到的是 **10ms 的看门狗**（而不是 200ms），会在正常控制循环里
    # 疯狂误触发。所以本工具在 CLI 层统一说「毫秒」，在这里换算成计数，
    # 并且**两个数都打印出来**，不藏。

    @property
    def kind(self) -> str:
        """编码类型：`u32` 还是 `f32`。见 `_INT_RIDS`。"""
        return "u32" if self.rid in _INT_RIDS else "f32"

    @property
    def label(self) -> str:
        return f"0x{self.rid:02X} {self.name}"

    def to_raw(self, human: float) -> float:
        """人话单位 → 寄存器计数。"""
        v = human * self.per_unit
        return int(round(v)) if self.kind == "u32" else v

    def to_human(self, raw: float) -> float:
        """寄存器计数 → 人话单位。"""
        return raw / self.per_unit

    def fmt(self, raw) -> str:
        """带单位的显示：换算是 1:1 就只显示一个数，否则人话在前、原始计数在后。"""
        if raw is None:
            return "?"
        if self.per_unit == 1.0:
            return f"{_label(raw)} {self.unit}".rstrip()
        return (f"{_label(raw / self.per_unit)} {self.unit}"
                f"（寄存器原始计数 {_label(raw)}）")


# 表来源：手册"寄存器列表及范围"（DM-J4340P-2EC V1.1 第 543-600 行同构表）
REGISTERS: list[Reg] = [
    Reg(0x00, "UV_Value", "V", "欠压保护值", lo=10.0),
    Reg(0x01, "KT_Value", "N·m/A", "扭矩系数（自动辨识出的值）"),
    Reg(0x02, "OT_Value", "℃", "过温保护阈值（手册建议 ≤100）", lo=80.0, hi=120.0),
    Reg(0x03, "OC_Value", "A", "过流保护值"),
    Reg(0x04, "ACC", "rad/s²", "加速度（非 MIT 模式用）"),
    Reg(0x05, "DEC", "rad/s²", "减速度（非 MIT 模式用，负值）"),
    Reg(0x06, "MAX_SPD", "rad/s", "限速（仅速度模式）"),
    Reg(0x07, "MST_ID", "", "反馈帧 CAN ID（达妙出厂默认 0 → 线上就是 0x000）"),
    Reg(0x08, "ESC_ID", "", "从机 ID（改它要配合硬件拨码/断电，**别在本工具里改**）"),
    Reg(0x09, "TIMEOUT", "ms",
        "★电机侧看门狗：主机崩溃时唯一兜底。0 = **关闭**。"
        "手册：单位是 **50µs 计数周期**（1ms = 20 计数），且**只在电机使能时生效**。"
        "本工具 CLI 说毫秒，这里替你换算",
        lo=1.0, hi=2.0e8, per_unit=20.0),
    Reg(0x0A, "CTRL_MODE", "", "控制模式：1=MIT 2=POS_VEL 3=VEL 4=力位混控", lo=0.0, hi=4.0),
    Reg(0x0B, "Damp", "", "阻尼系数（POS_VEL 用）"),
    Reg(0x0C, "Inertia", "", "惯量（POS_VEL 用）"),
    Reg(0x10, "NPP", "", "极对数（RO）", w=False),
    Reg(0x11, "Rs", "Ω", "定子电阻（RO）", w=False),
    Reg(0x12, "LS", "H", "定子电感（RO）", w=False),
    Reg(0x13, "Flux", "Wb", "磁链（RO）", w=False),
    Reg(0x14, "Gr", "", "★减速比（RO）：10=4310 / 40=4340P。**用它验型号填错没有**", w=False),
    Reg(0x15, "PMAX", "rad", "★位置映射范围（MIT 帧用）"),
    Reg(0x16, "VMAX", "rad/s", "★速度映射范围（MIT 帧用）"),
    Reg(0x17, "TMAX", "N·m", "★扭矩映射范围（MIT 帧用）。**不是物理峰值**，见 design.md D5"),
    Reg(0x18, "I_BW", "Hz", "电流环带宽", lo=100.0, hi=1.0e4),
    Reg(0x19, "KP_ASR", "", "速度环 Kp"),
    Reg(0x1A, "KI_ASR", "", "速度环 Ki"),
    Reg(0x1B, "KP_APR", "", "位置环 Kp（达妙出厂 54，**不是** reBot 的值）"),
    Reg(0x1C, "KI_APR", "", "位置环 Ki"),
    Reg(0x1D, "OV_Value", "V", "过压保护值"),
    Reg(0x3C, "VBus", "V", "电源电压（RO，慢速读即可）", w=False),
    Reg(0x3D, "Tpcb", "℃", "驱动板温度（RO）", w=False),
    Reg(0x3E, "Tmt", "℃", "电机温度（RO）", w=False),
]

BY_RID = {r.rid: r for r in REGISTERS}
BY_NAME = {r.name.lower(): r for r in REGISTERS}

# dump/verify 默认关注的面板：身份 + 映射 + PID + 保护 + 实时量
DEFAULT_DUMP = ["Gr", "PMAX", "VMAX", "TMAX", "KP_ASR", "KI_ASR", "KP_APR", "KI_APR",
                "TIMEOUT", "MST_ID", "CTRL_MODE", "VBus", "Tpcb", "Tmt"]

# 本工具**拒绝**写的 RID。理由必须具体，否则以后自己会想绕过。
REFUSE = {
    0x08: "ESC_ID 改完必须断电并按硬件拨码确认，属于 dm-calibrate 的活",
    0x10: "NPP 是只读的硬件参数",
    0x14: "Gr 是只读的，而且改它会让全链路换算全错（D1）",
    0x3C: "VBus 只读",
    0x3D: "Tpcb 只读",
    0x3E: "Tmt 只读",
}


def resolve_reg(token: str) -> Reg:
    """`--rid` 既接受 0x09 / 9，也接受 TIMEOUT / timeout。"""
    t = token.strip()
    if t.lower() in BY_NAME:
        return BY_NAME[t.lower()]
    try:
        rid = int(t, 0)
    except ValueError:
        pass
    else:
        if rid in BY_RID:
            return BY_RID[rid]
        raise SystemExit(f"不认识 RID {rid}（0x{rid:02X}）。先跑 `list` 看有哪些")
    raise SystemExit(f"不认识寄存器 {token!r}。先跑 `list` 看有哪些")


# ───────────────────────── 2. 读写原语 ─────────────────────────
def _fresh(ctrl, motor, ser, rid: int, attempts: int = 3):
    """读一个寄存器的**当前真值**，返回 (值, 尝试次数)。

    两道保险，缺一不可：
      ① `ser.reset_input_buffer()` —— 丢掉串口里可能残留的旧反馈帧。
         不清的话，第一次 `recv` 可能读到的是**上一次操作**的应答。
      ② `temp_param_dict.pop(rid)` —— 清 SDK 的寄存器缓存。SDK 的
         `read_motor_param` 命中缓存就**直接返回旧值**，调用方无从分辨。
    """
    for i in range(1, attempts + 1):
        ser.reset_input_buffer()
        motor.temp_param_dict.pop(int(rid), None)
        val = ctrl.read_motor_param(motor, rid)
        if val is not None:
            return val, i
    return None, attempts


def collect_dump(ctrl, motor, ser, regs) -> tuple[dict, list]:
    """读一批寄存器，返回 (values, misses)。**只读，不写。**"""
    values, misses = {}, []
    for r in regs:
        v, _ = _fresh(ctrl, motor, ser, r.rid)
        if v is None:
            misses.append(r.label)
        else:
            values[r.label] = v
    return values, misses


def _dump_before_write(args, ctrl, motor, ser) -> Path | None:
    """写寄存器**之前**留一份基线快照。

    为什么必须要：`--commit --save` 会写 flash，而 flash 是电机里**最难撤销**的东西 ——
    "改坏了用 restore 回滚"（D7 的承诺）只在**存在一份改动前的快照**时才成立。
    没有快照，回滚就无从谈起，工具就变成了单向操作。

    代价：默认那 13 个寄存器每个要 ~150ms，一轮 ~2 秒，且这两秒电机侧是静默的
    （所以看门狗阈值没配好时别在使能状态下调用）。
    """
    regs = [BY_NAME[n.lower()] for n in DEFAULT_DUMP]
    values, misses = collect_dump(ctrl, motor, ser, regs)
    p = save_dump(args.id, args.type, args.port, values, "baseline")
    print(f"  基线快照：{len(values)} 个寄存器已存 → {p.relative_to(REPO)}")
    if misses:
        print(f"    ⚠ 有 {len(misses)} 个读不到，没进快照：{', '.join(misses)}")
    print("    （改动前存、改动后可 `restore` 回滚 —— 这是 D7「先读后写可回滚」的另一半）")
    return p


def _write(ctrl, motor, ser, reg: Reg, value, commit: bool):
    """写一个寄存器，返回 (before, ok, after)。

    **本文件的重点就是 `pop` 那一行**，见模块 docstring 第 2 条纪律：

        `change_motor_param` 的成败判断 = `abs(temp_param_dict[RID] - data) < 0.1`
        而 `temp_param_dict` 是缓存。刚做完基线读 → 缓存里是**旧值** →
        它拿旧值跟新值比 → 不等 → `return False`。
        但 `__write_motor_param` 已经**先把帧发出去了**，所以其实写成功了。
        结果就是：真实世界对了，返回值骗你。

    所以顺序必须是「读基线 → **再清一次缓存** → 写」。
    """
    before, _ = _fresh(ctrl, motor, ser, reg.rid)
    if not commit:
        return before, None, None

    ser.reset_input_buffer()
    motor.temp_param_dict.pop(int(reg.rid), None)   # ← 关键的一行，别删
    ok = ctrl.change_motor_param(motor, reg.rid, value)
    after, _ = _fresh(ctrl, motor, ser, reg.rid)
    return before, ok, after


# ───────────────────────── 3. 落盘 ─────────────────────────
def dump_dir(motor_id: int) -> Path:
    return REPO / "registers" / f"{motor_id:02d}"


def save_dump(motor_id: int, motor_type: str, port: str, values: dict, tag: str) -> Path:
    d = dump_dir(motor_id)
    d.mkdir(parents=True, exist_ok=True)
    p = d / f"{time.strftime('%Y%m%d-%H%M%S')}_{tag}.json"
    p.write_text(json.dumps({
        "saved_at": time.strftime("%Y-%m-%dT%H:%M:%S"),
        "port": port,
        "motor_id": motor_id,
        "motor_type": motor_type,
        "registers": values,
    }, indent=2, ensure_ascii=False) + "\n")
    return p


def load_dump(path: Path) -> dict:
    return json.loads(Path(path).read_text())


def _label(v) -> str:
    if isinstance(v, float):
        return f"{v:g}"
    return str(v)


# ───────────────────────── 4. 子命令 ─────────────────────────
def cmd_list(args, DM_CAN, sdk_dir: Path):
    print("=" * 84)
    print("dm_registers · list  —— 本工具认识的寄存器")
    print("=" * 84)
    # "名字"/"编码" 是 CJK，`len()` 算 2 但终端占 4 格 —— 用 dm_bringup 的 pad()
    print("  " + B.pad("RID", 6) + B.pad("名字", 12) + B.pad("编码", 7)
          + B.pad("读写", 7) + B.pad("单位", 9) + "含义")
    print("  " + "-" * 80)
    for r in REGISTERS:
        rw = "RW" if (r.w and r.rid not in REFUSE) else "RO"
        print("  " + B.pad(f"0x{r.rid:02X}", 6) + B.pad(r.name, 12) + B.pad(r.kind, 7)
              + B.pad(rw, 7) + B.pad(r.unit, 9) + r.note)
    print()
    print("  编码类型 = SDK 的 is_in_ranges 分派（DM_CAN.py:606）：")
    print("    只有 RID 7~10 / 13~16 / 35~36 是 uint32，其余全是 float32。")
    print("    写之前必须知道自己在写哪种 —— 传错类型不会报错，只会静默写坏。")
    return 0


def _open(args, DM_CAN):
    ser = B.open_port(args.port, args.timeout)
    motor = B.make_motor(args, DM_CAN)
    ctrl = DM_CAN.MotorControl(ser)
    ctrl.addMotor(motor)
    return ser, motor, ctrl


def cmd_dump(args, DM_CAN, sdk_dir: Path):
    regs = [resolve_reg(x) for x in args.regs] if args.regs else [BY_NAME[n.lower()] for n in DEFAULT_DUMP]
    print("=" * 84)
    print(f"dm_registers · dump  —— 只读。电机 0x{args.id:02X} ({args.type})")
    print("=" * 84)
    ser, motor, ctrl = _open(args, DM_CAN)
    try:
        values, misses = collect_dump(ctrl, motor, ser, regs)
        for r in regs:
            if r.label in values:
                print(f"  {r.label:<22} = {r.fmt(values[r.label])}")
            else:
                print(f"  {r.label:<22} 读不到（可能这台没这个寄存器）")
    finally:
        ser.close()

    if values:
        p = save_dump(args.id, args.type, args.port, values, "dump")
        print(f"\n  已落盘：{p.relative_to(REPO)}")
        print("  ← 这就是 D7「先读后写」里的**基线**。以后改坏了用 restore 回滚。")
    if misses:
        print(f"\n  ⚠ {len(misses)} 个读不到：{', '.join(misses)}")
    return 0


def cmd_verify(args, DM_CAN, sdk_dir: Path):
    """读身份/模式/保护，逐条和期望值比。**只读。**"""
    print("=" * 84)
    print(f"dm_registers · verify  —— 只读。电机 0x{args.id:02X} ({args.type})")
    print("=" * 84)
    ser, motor, ctrl = _open(args, DM_CAN)
    got = {}
    try:
        for name in ("Gr", "PMAX", "VMAX", "TMAX", "TIMEOUT", "MST_ID", "CTRL_MODE"):
            v, _ = _fresh(ctrl, motor, ser, BY_NAME[name.lower()].rid)
            got[name] = v
    finally:
        ser.close()

    want_gr = 10.0 if "4310" in args.type else 40.0
    mode_names = {0: "MIT(旧)", 1: "MIT", 2: "POS_VEL", 3: "VEL", 4: "力位混控"}

    def line(ok, text, hint=""):
        """ok=None → 纯信息行（用 `·`，不要用 ✓ 假装通过）。"""
        mark = "·" if ok is None else ("✓" if ok else "✗")
        print(f"  {mark} {text}" + (f"\n      ← {hint}" if hint and ok is not True else ""))

    line(got["Gr"] == want_gr, f"0x14 Gr = {_label(got['Gr'])}（{args.type} 应为 {want_gr:g}）",
         "型号填错了，或电机不是这个型号")
    line(None, f"0x15/16/17 PMAX/VMAX/TMAX = {_label(got['PMAX'])} / {_label(got['VMAX'])} / "
               f"{_label(got['TMAX'])}")
    print("      ← 这三个是 **MIT 帧的线性映射范围**，全链路编解码都用回读值；"
          "手册里的额定/峰值 N·m 是物理能力，算重力补偿时用（design.md D5）")
    tmo = BY_NAME["timeout"]
    line(got["TIMEOUT"] not in (None, 0), f"0x09 TIMEOUT = {tmo.fmt(got['TIMEOUT'])}",
         "**是 0（或读不到）= 电机侧看门狗是关的**。主机崩溃时电机会一直保持使能出力，"
         "这是安全底线，用 `set --rid 0x09 --value 500 --commit` 打开"
         "（CLI 认毫秒，写进去的原始计数由工具换算，见 list）")
    line(None, f"0x07 MST_ID = {_label(got['MST_ID'])}（达妙出厂 0，线上反馈 CAN ID 就是 0x000）")
    m = got["CTRL_MODE"]
    line(m == 2, f"0x0A CTRL_MODE = {_label(m)}（{mode_names.get(int(m) if m is not None else -1, '?')}）",
         "POS_VEL 路径（jog / monitor / reBot 原版控制律）需要 2；jog --mit 任何模式都能跑")
    print("\n  ⚠ 本命令**只读**。改值请用 `set`，它写前会自动 dump 基线。")
    return 0


def cmd_set(args, DM_CAN, sdk_dir: Path):
    reg = resolve_reg(args.rid)
    value = args.value

    # ── 静态拦截：这些不该由本工具写 ──
    if reg.rid in REFUSE:
        B.die(f"拒绝写 {reg.label}：{REFUSE[reg.rid]}")
    if not reg.w:
        B.die(f"拒绝写 {reg.label}：它在手册里是只读（RO）")

    # ── 人话单位 → 寄存器计数（0x09 的 1ms = 20 计数，见 Reg.per_unit）──
    human = float(value)
    raw = reg.to_raw(human)
    if reg.kind == "u32" and raw != int(raw):     # to_raw 已取整，这里只兜 f32 之外的怪值
        B.die(f"{reg.label} 是 uint32 编码，换算后 {raw} 不是整数")

    # ── 范围提醒（不硬拦，因为手册的范围依型号而定）──
    range_note = ""
    if reg.lo is not None and human < reg.lo:
        range_note = f"  ⚠ 低于手册建议下限 {reg.lo:g} {reg.unit}"
    if reg.hi is not None and human > reg.hi:
        range_note = f"  ⚠ 高于手册建议上限 {reg.hi:g} {reg.unit}"
    if reg.rid == 0x09 and human == 0:
        range_note += ("\n     ⚠⚠ TIMEOUT=0 是**关闭看门狗**。这不是「把参数调小」，"
                       "是主动放弃主机崩溃时唯一的兜底 —— 电机将保持使能一直出力。"
                       "只有在明确要关掉它做对比实验时才这么写。")

    print("=" * 84)
    print(f"dm_registers · set  —— {reg.label} ← {_label(human)} {reg.unit}"
          f"   {'【真写】' if args.commit else '【DRY-RUN：不会写】'}")
    print("=" * 84)
    print(f"  含义：{reg.note}")
    print(f"  编码：{reg.kind}   写 flash：{'是' if args.save else '否（断电即恢复）'}{range_note}")
    if reg.per_unit != 1.0:
        print(f"  换算：{_label(human)} {reg.unit} × {reg.per_unit:g} = "
              f"**{_label(raw)}**（写进寄存器的原始计数）")

    ser, motor, ctrl = _open(args, DM_CAN)
    try:
        if args.commit:
            _dump_before_write(args, ctrl, motor, ser)
        before, ok, after = _write(ctrl, motor, ser, reg, raw, args.commit)

        print(f"\n  写入前 = {reg.fmt(before)}")

        if not args.commit:
            print(f"  将改为 = {reg.fmt(raw)}")
            print(f"\n  [未执行] 确认无误后加 --commit。")
            if reg.rid == 0x09:
                print("  看门狗实测流程（design.md D4）：")
                print("    1) 先让电机断电再上电（清掉可能锁存的 ERR=13）")
                print("    2) tools/watchdog_test.py --yes   ← 跑 A/B 对照，别手动拔线")
                print("       它用'停发帧但不关串口'来模拟主机崩溃：电机看到的一样（没有 CAN 帧），")
                print("       但我们还能继续读 ERR —— 拔线的话观测要等插回去，而且适配器要重新枚举。")
                print("       ⚠ 注意：任何发出去的帧都会重置计时器（含读状态用的 0x7FF 刷新帧），")
                print("         所以要么持续发帧、要么静默到点单点观测，中断发会前功尽弃。")
            if reg.rid == 0x0A:
                print("  ⚠ 切 CTRL_MODE 只对**本次上电**有效。要不丢必须加 --save（写 flash），")
                print("     而且手册说**切换模式时电机会清零指令值**（含 MIT 的 t_ff/KP/KD）→ 先失能再切。")
            return 0

        print(f"  写入后 = {reg.fmt(after)}")
        # SDK 的返回值**可能骗人**（缓存陷阱），所以以回读为准，不一致时才提 SDK 的说法
        agree = after is not None and abs(float(after) - float(raw)) < 0.1
        print()
        if agree:
            print(f"  ✓ 回读校验通过（{_label(after)} ≈ {_label(raw)} 原始计数）")
            if not ok:
                print("    注意：SDK 的 change_motor_param 返回了 False，但回读是对的 ——")
                print("    这正是模块 docstring 第 2 条那个缓存陷阱的表现。**以回读为准。**")
            if reg.rid == 0x09:
                print(f"    即电机侧看门狗 = {_label(human)} {reg.unit}。"
                      f"**只在电机使能时生效**（手册），所以失能状态下测不出来。")
                print(f"    注意每写一次寄存器要 ~150ms（期间发不出帧），"
                      f"阈值设得比它小的话，写这个动作自己就会触发看门狗。")
        else:
            print(f"  ✗ 回读不匹配：目标 {_label(raw)}，实际 {_label(after)}"
                  f"（SDK 返回 {'True' if ok else 'False'}）")

        if args.save:
            print("\n  --save：写入 flash 中…")
            ctrl.save_motor_param(motor)
            time.sleep(0.2)
            print("  ⚠ 已写 flash。**必须人工断电重新上电**才会以新值启动，"
                  "在此之前电机行为未定义。")
        else:
            print(f"\n  未写 flash：断电重新上电后 {reg.name} 会回到 {reg.fmt(before)}。")
    finally:
        ser.close()
    return 0


def cmd_restore(args, DM_CAN, sdk_dir: Path):
    data = load_dump(args.file)
    regs = data["registers"]
    print("=" * 84)
    print(f"dm_registers · restore  —— 从 {args.file}")
    print(f"  该快照存于 {data.get('saved_at')}，电机 0x{data.get('motor_id'):02X} "
          f"({data.get('motor_type')})")
    print("=" * 84)
    print("  注：快照里是**寄存器原始计数**，不是人话单位 —— 回滚时**不做任何换算**，")
    print("      直接原样写回。所以 dump 出来的 JSON 别手改、也别拿到别处去按 ms 解释。")

    if args.id is not None and int(args.id) != int(data.get("motor_id", -1)):
        B.die(f"快照是电机 0x{data.get('motor_id'):02X} 的，你却指定了 0x{args.id:02X}。"
              "回滚到错误的电机上比不回滚更糟。")

    ser, motor, ctrl = _open(args, DM_CAN)
    n_ok = n_skip = 0
    try:
        for label, target in regs.items():
            reg = resolve_reg(label.split()[-1])
            if reg.rid in REFUSE or not reg.w:
                print(f"  跳过 {reg.label}：只读或本工具不写")
                n_skip += 1
                continue
            target = int(target) if reg.kind == "u32" else float(target)
            before, ok, after = _write(ctrl, motor, ser, reg, target, args.commit)
            if not args.commit:
                print(f"  {reg.label:<22} {_label(before)} → {_label(target)}   (dry-run)")
                continue
            agree = after is not None and abs(float(after) - float(target)) < 0.1
            print(f"  {reg.label:<22} {_label(before)} → {_label(after)}   {'✓' if agree else '✗'}")
            n_ok += agree
    finally:
        ser.close()

    if not args.commit:
        print(f"\n  [未执行] 这是 dry-run。加 --commit 才真正回写（{len(regs)} 个寄存器）。")
    else:
        print(f"\n  完成：{n_ok} 个写成功，{n_skip} 个跳过。")
    return 0


# ───────────────────────── 5. CLI ─────────────────────────
def main(argv=None) -> int:
    p = argparse.ArgumentParser(
        description="达妙寄存器读写（先读后写、可回滚）",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="所有子命令都需要接硬件。`list` 除外。")
    p.add_argument("cmd", nargs="?", default="dump",
                   choices=["list", "dump", "verify", "set", "restore"])
    p.add_argument("--port", default="/dev/ttyACM0")
    p.add_argument("--id", type=lambda s: int(s, 0), default=0x01,
                   help="电机 ID，**实测手上的是 0x01**（dm_bringup 默认 0x04）")
    p.add_argument("--fb-id", type=lambda s: int(s, 0), default=None)
    p.add_argument("--type", default="DM4310",
                   help="DM_Motor_Type 成员名。4340P 用 DM4340（并以回读值为准）")
    p.add_argument("--timeout", type=float, default=0.05)
    p.add_argument("--sdk-dir", default=None, help="vendored DM_CAN.py 所在目录（一般不用给）")
    p.add_argument("--regs", nargs="*", default=None,
                   help=f"dump 要读哪些（名字或 RID）。默认：{' '.join(DEFAULT_DUMP)}")
    p.add_argument("--rid", default="0x09", help="set 要写哪个寄存器（名字或 RID）")
    p.add_argument("--value", type=float, default=None, help="set 写什么值")
    p.add_argument("--commit", action="store_true",
                   help="**真的写**。不加就是 dry-run")
    p.add_argument("--save", action="store_true",
                   help="写进 flash（断电不丢）。默认不写，断电即恢复")
    p.add_argument("--file", default=None, help="restore 用的 JSON")
    args = p.parse_args(argv)

    if args.fb_id is None:
        args.fb_id = 0x10 + args.id

    if args.cmd == "set" and args.value is None:
        B.die("set 需要 --value，例如：set --rid 0x09 --value 200")
    if args.cmd == "restore" and not args.file:
        B.die("restore 需要 --file，指向 dump 出来的 JSON")

    if args.cmd == "list":
        return cmd_list(args, None, None)

    sdk_dir = B.find_sdk_dir(args.sdk_dir)
    DM_CAN = B.load_sdk(sdk_dir)
    check_encoding(DM_CAN)   # 编码表与 SDK 对拍。不一致就地终止，绝不带病往下写
    dispatch = {"dump": cmd_dump, "verify": cmd_verify, "set": cmd_set, "restore": cmd_restore}
    return dispatch[args.cmd](args, DM_CAN, sdk_dir)


if __name__ == "__main__":
    sys.exit(main())
