#!/usr/bin/env python3
"""dm_registers —— 寄存器工具：读 / 写 / 存参数 / 回滚。

## 子命令

    list      列出寄存器表（**不需要硬件**，表本身就是文档）
    dump      读一批寄存器 → registers/<id>/<时间>_<tag>.json
    verify    读 Gr + PMAX/VMAX/TMAX + CTRL_MODE，跟**型号档位表**和 `config/joint.yaml` 对拍
    set       写一个寄存器（默认只进 RAM；--save 才写 flash，且要 --yes）
    restore   拿一份 dump 把值写回去（默认只打印要做什么，--yes 才真写）

## 边界

- 只管寄存器 I/O：**不使能电机、不发控制帧、不判断安全**（限位/力矩在 `joint.py` 那一层）。
- 帧构造在 `dm_frames.py`，收发在 `dm_bus.py` —— 本文件**不碰串口**。
- `0x0A`（控制模式）是 **RAM 不是 flash**：写它之后要同步改 `Joint.mode`。

## 三条硬性纪律

1. **写之前先读**：`set` 先读当前值、写完再读一次确认；回包与发出的字节不一致会报出来。
2. **默认不碰 flash**：`set` 只写 RAM；要进 flash 必须 `--save --yes`，且**写前自动 dump 基线**。
3. **拒写清单**：改 ID / CAN 波特率 / 看门狗 / 环带宽这类寄存器默认拒绝，要 `--force`；
   手册标 RO 的寄存器**一律拒写、不可覆盖**。
"""
from __future__ import annotations

import argparse
import json
import sys
import time
from dataclasses import dataclass
from pathlib import Path

# 本文件既要能被裸跑（python dm_registers.py），也要能从包里 import
# （`DMmotor_driver.dm_registers`，例如 setup.py 的 console_scripts）。
# 把自己所在目录塞进 sys.path，两条路都能用裸 import 走通。
sys.path.insert(0, str(Path(__file__).resolve().parent))

from dm_bus import MotorBus                                    # noqa: E402
from dm_frames import (                                        # noqa: E402
    uint8s_to_uint32, uint8s_to_float32, uint32_to_uint8s, float_to_uint8s,
)

REPO = Path(__file__).resolve().parents[3]
CONFIG = REPO / "config" / "joint.yaml"

# 编码类型表：**必须与 SDK 的 `DM_CAN.is_in_ranges` 一致**（`DM_CAN.py:606`）：
#     def is_in_ranges(n): return (7 <= n <= 10) or (13 <= n <= 16) or (35 <= n <= 36)
# 这些 RID 是 uint32，其余是 float32。
# ⚠️ 手册把 0x25 Boot_ver（37）写成 uint32，而 SDK 的区间里没有 37 ⇒ 按 float 处理。
#    这是唯一一处手册与 SDK 冲突的地方（我们只读它，不写）。
_INT_RIDS = frozenset(range(7, 11)) | frozenset(range(13, 17)) | frozenset(range(35, 37))


@dataclass(frozen=True)
class Reg:
    """一个寄存器：地址、名字、读写、手册范围、说明。"""

    rid: int
    name: str
    access: str            # "RW" / "RO"
    limits: str            # 手册里那一列，纯文档
    note: str
    per_unit: float | None = None   # 1 个计数 = 多少**秒**（None = 与物理量没有换算关系）

    @property
    def kind(self) -> str:
        return "uint32" if self.rid in _INT_RIDS else "float"

    @property
    def label(self) -> str:
        return f"0x{self.rid:02X} {self.name}"

    def to_raw(self, value) -> bytes:
        return uint32_to_uint8s(value) if self.kind == "uint32" else float_to_uint8s(value)

    def from_raw(self, raw4: bytes):
        return uint8s_to_uint32(raw4) if self.kind == "uint32" else uint8s_to_float32(raw4)

    def fmt(self, value) -> str:
        """把**解码后**的值格式化成给人看的字符串。

        带 `per_unit` 的寄存器（0x09 TIMEOUT：1 计数 = 50µs）**同时**标出人看的单位 ——
        换算规则只在本字段定义这一处（DESIGN D4：别在别处再写一遍，那个坑测废过两轮）。
        """
        if value is None:
            return "读不到"
        if self.per_unit is None:
            return f"{value:g}"
        return f"{value:g}（{value * self.per_unit * 1000:g} ms）"


# 手册「寄存器列表及范围」逐条抄下来（`DM-J4340P-2EC V1.1 .md:539+`）
REGISTERS = [
    Reg(0x00, "UV_Value",  "RW", "(10.0, fmax)",  "低压保护值"),
    Reg(0x01, "KT_Value",  "RW", "[0.0, fmax]",   "扭矩系数"),
    Reg(0x02, "OT_Value",  "RW", "[80.0, 200)",   "过温保护值"),
    Reg(0x03, "OC_Value",  "RW", "(0.0, 1.0)",    "过流保护值"),
    Reg(0x04, "ACC",       "RW", "(0.0, fmax)",   "加速度"),
    Reg(0x05, "DEC",       "RW", "[-fmax, 0.0)",  "减速度"),
    Reg(0x06, "MAX_SPD",   "RW", "(0.0, fmax)",   "最大速度"),
    Reg(0x07, "MST_ID",    "RW", "[0, 0x7FF]",    "反馈 ID"),
    Reg(0x08, "ESC_ID",    "RW", "[0, 0x7FF]",    "接收 ID"),
    Reg(0x09, "TIMEOUT",   "RW", "[0, 2^32-1]",   "超时警报时间", per_unit=50e-6),
    Reg(0x0A, "CTRL_MODE", "RW", "[0, 4]",        "控制模式（RAM，掉电复位）"),
    Reg(0x0B, "Damp",      "RO", "/",             "电机粘滞系数"),
    Reg(0x0C, "Inertia",   "RO", "/",             "电机转动惯量"),
    Reg(0x0D, "hw_ver",    "RO", "/",             "硬件版本（保留）"),
    Reg(0x0E, "sw_ver",    "RO", "/",             "软件版本号"),
    Reg(0x0F, "SN",        "RO", "/",             "序列号（保留）"),
    Reg(0x10, "NPP",       "RO", "/",             "电机极对数"),
    Reg(0x11, "Rs",        "RO", "/",             "相电阻"),
    Reg(0x12, "Ls",        "RO", "/",             "相电感"),
    Reg(0x13, "Flux",      "RO", "/",             "磁链值"),
    Reg(0x14, "Gr",        "RO", "/",             "齿轮减速比"),
    Reg(0x15, "PMAX",      "RW", "(0.0, fmax)",   "位置映射范围（MIT 帧编解码用）"),
    Reg(0x16, "VMAX",      "RW", "(0.0, fmax)",   "速度映射范围"),
    Reg(0x17, "TMAX",      "RW", "(0.0, fmax)",   "扭矩映射范围"),
    Reg(0x18, "I_BW",      "RW", "[100, 1e4]",    "电流环控制带宽"),
    Reg(0x19, "KP_ASR",    "RW", "[0.0, fmax]",   "速度环 Kp"),
    Reg(0x1A, "KI_ASR",    "RW", "[0.0, fmax]",   "速度环 Ki"),
    Reg(0x1B, "KP_APR",    "RW", "[0.0, fmax]",   "位置环 Kp"),
    Reg(0x1C, "KI_APR",    "RW", "[0.0, fmax]",   "位置环 Ki"),
    Reg(0x1D, "OV_Value",  "RW", "TBD",           "过压保护值"),
    Reg(0x1E, "GREF",      "RW", "(0.0, 1.0]",    "齿轮力矩效率"),
    Reg(0x1F, "Data",      "RW", "[1.0, 30.0]",   "速度环阻尼系数（POS_VEL 必须非 0）"),
    Reg(0x20, "V_BW",      "RW", "(0.0, 500.0)",  "速度环滤波带宽"),
    Reg(0x21, "IQ_cl",     "RW", "[100, 1e4]",    "电流环增强系数"),
    Reg(0x22, "VL_cl",     "RW", "(0.0, 1e4]",    "速度环增强系数"),
    Reg(0x23, "can_br",    "RW", "[0, 4]",        "CAN 波特率代码"),
    Reg(0x24, "sub_ver",   "RO", "/",             "子版本号"),
    Reg(0x25, "Boot_ver",  "RO", "/",             "Boot 版本号"),
    Reg(0x37, "dir",       "RO", "/",             "方向"),
    Reg(0x38, "m_off",     "RO", "/",             "电机侧角度偏移"),
    Reg(0x3B, "Imax",      "RO", "/",             "驱动板最大电流"),
    Reg(0x3C, "VBus",      "RO", "/",             "电源电压"),
    Reg(0x3D, "Tpcb",      "RO", "/",             "驱动板温度"),
    Reg(0x3E, "Tmt",       "RO", "/",             "电机温度"),
    Reg(0x3F, "Iu_off",    "RO", "/",             "U 相电流偏置"),
    Reg(0x40, "Iv_off",    "RO", "/",             "V 相电流偏置"),
    Reg(0x41, "Iw_off",    "RO", "/",             "W 相电流偏置"),
    Reg(0x50, "p_m",       "RO", "/",             "电机当前位置"),
    Reg(0x51, "xout",      "RO", "/",             "输出轴位置"),
]
BY_RID = {r.rid: r for r in REGISTERS}
BY_NAME = {r.name.lower(): r for r in REGISTERS}
BY_LABEL = {r.label: r for r in REGISTERS}

# `dump` 的默认读回集合（都不是"危险"寄存器）
DEFAULT_DUMP = ["MST_ID", "ESC_ID", "CTRL_MODE", "TIMEOUT", "OT_Value", "OC_Value",
                "Gr", "PMAX", "VMAX", "TMAX", "KP_ASR", "KI_ASR", "KP_APR", "KI_APR", "Data"]

# 写这些要 `--force`：改错会失联 / 失去保护 / 震荡
REFUSE = {
    0x07: "反馈 ID（MST_ID）：改错就再也收不到这台电机的回包",
    0x08: "接收 ID（ESC_ID）：改错就再也控制不了它",
    0x23: "CAN 波特率：改错整条总线失联，得重新配适配器",
    0x09: "看门狗超时：单位 50µs，写错要么一动就报警、要么失去保护",
    0x00: "低压保护值：写错会在电压正常时停机",
    0x1D: "过压保护值：手册范围还是 TBD",
    0x18: "电流环带宽：写错可能震荡",
    0x20: "速度环滤波带宽：写错可能震荡",
    0x21: "电流环增强系数：写错可能震荡",
    0x22: "速度环增强系数：写错可能震荡",
    0x01: "扭矩系数 KT：影响 MIT 的力矩换算，要改先标定",
}

# verify 用的期望值（实测回读，见 docs/TESTING.md §2.5/§2.6）
EXPECTED_GR = {"4340P": 40.0, "4310": 10.0}
EXPECTED_LIMIT = {"4340P": (12.5, 10.0, 28.0), "4310": (12.5, 30.0, 10.0)}
MODEL_BY_GR = {40.0: "4340P", 10.0: "4310"}


def die(msg: str) -> None:
    print(f"✗ {msg}", file=sys.stderr)
    raise SystemExit(2)


def resolve_reg(token: str) -> Reg:
    """`--rid` 既收 0x15 / 21，也收 PMAX / pmax。"""
    t = str(token).strip()
    if t.lower() in BY_NAME:
        return BY_NAME[t.lower()]
    try:
        rid = int(t, 0)
    except ValueError:
        die(f"不认识寄存器 {token!r}。先跑 `list` 看有哪些")
    if rid in BY_RID:
        return BY_RID[rid]
    die(f"表里没有 RID {rid}（0x{rid:02X}）")


class RegisterTool:
    """一台电机的寄存器读写。持 `MotorBus` 引用，**不自己开串口**。"""

    def __init__(self, bus: MotorBus, motor_id: int, motor_type: str | None = None):
        self.bus = bus
        self.motor_id = motor_id
        self.motor_type = motor_type

    # ── 读 ──
    def read(self, rid: int, timeout: float = 0.05):
        raw = self.bus.read_register(self.motor_id, rid, timeout)
        return None if raw is None else BY_RID[rid].from_raw(raw)

    # ── 写 ──
    def check_writable(self, reg: Reg, force: bool = False) -> None:
        """RO 一律拒；REFUSE 清单要 `--force`。"""
        if reg.access == "RO":
            die(f"{reg.label} 是只读（RO），拒绝写 —— 写它没有意义，还可能是编错了 RID")
        if reg.rid in REFUSE and not force:
            die(f"{reg.label} 在拒写清单里：{REFUSE[reg.rid]}\n"
                f"  确认要写就加 --force（先备份、想清楚怎么回滚）")

    def write(self, reg: Reg, value, force: bool = False, timeout: float = 0.05):
        """写一个寄存器，返回 (before, after, echo_ok)。**只进 RAM。**"""
        self.check_writable(reg, force)
        before = self.read(reg.rid, timeout)
        raw = reg.to_raw(value)
        resp = self.bus.write_register(self.motor_id, reg.rid, raw, timeout)
        echo_ok = None if resp is None else (resp == raw)
        after = self.read(reg.rid, timeout)
        return before, after, echo_ok

    # ── 批量读 ──
    def dump(self, regs=None, timeout: float = 0.05):
        """读一批寄存器 → (values, misses)。**只读，不写。**"""
        regs = [resolve_reg(x) for x in (regs or DEFAULT_DUMP)]
        values, misses = {}, []
        for r in regs:
            v = self.read(r.rid, timeout)
            if v is None:
                misses.append(r.label)
            else:
                values[r.label] = v
        return values, misses

    # ── 对拍 ──
    def verify(self, timeout: float = 0.05):
        """跟型号档位表 + `config/joint.yaml` 对拍。返回 (rows, ok)。"""
        rows = []

        def row(item, got, want, ok, hint=""):
            rows.append((item, got, want, ok, hint))

        gr = self.read(0x14, timeout)
        if gr is None:
            row("0x14 Gr", "读不到", "—", False, "没有任何回包：适配器/接线/ID 先查一遍")
            return rows, False
        state = self.bus.get_state(self.motor_id)
        if state is not None:
            row("回包里的电机 ID", f"0x{state.motor_id:02X}", f"0x{self.motor_id:02X}", True)

        model = self.motor_type or MODEL_BY_GR.get(round(float(gr), 1))
        row("0x14 Gr（减速比）", f"{float(gr):g}",
            "40 → 4340P／10 → 4310", model is not None,
            "" if model else "认不出型号，请用 --type 指定")
        if model is None:
            return rows, False

        got_raw = [self.read(r, timeout) for r in (0x15, 0x16, 0x17)]
        if any(v is None for v in got_raw):
            row("0x15/0x16/0x17 映射范围", "有读不到的", "—", False,
                "先确认这三个 RID 都能读回（适配器/接线/ID）")
            return rows, False
        got = tuple(float(v) for v in got_raw)
        want = EXPECTED_LIMIT[model]
        row("0x15/0x16/0x17 映射范围", f"{got[0]:g}/{got[1]:g}/{got[2]:g}",
            f"{want[0]:g}/{want[1]:g}/{want[2]:g}", got == want,
            "" if got == want else "与实测档位表不一致：**别用它编解码**，先查是不是换过电机/刷过固件")

        mode = self.read(0x0A, timeout)
        data = self.read(0x1F, timeout)
        if mode is not None:
            row("0x0A CTRL_MODE", f"{int(mode)}", "1/2/4", int(mode) in (1, 2, 4),
                "" if int(mode) in (1, 2, 4) else "3 是速度模式，本工程不实现")
            if int(mode) in (2, 4) and data is not None and float(data) == 0.0:
                row("0x1F Data（位置类模式）", "0", "非 0（推荐 4.0）", False,
                    "手册：POS_VEL 的阻尼因子必须非 0，否则震荡/过冲")

        # 与 config/joint.yaml 对拍（找不到就跳过，不算失败）
        try:
            from arm_config import load_joint_configs
            cfgs = load_joint_configs(CONFIG)
            cfg = next((c for c in cfgs.values() if c.slave_id == self.motor_id), None)
            if cfg is None:
                row("config/joint.yaml", "没有这个 slave_id", str(self.motor_id), True, "跳过")
            else:
                row(f"yaml {cfg.name} 的 limit", f"{got[0]:g}/{got[1]:g}/{got[2]:g}",
                    f"{cfg.limit[0]:g}/{cfg.limit[1]:g}/{cfg.limit[2]:g}",
                    tuple(cfg.limit) == got, "yaml 里的 limit 该改回读值了")
                if mode is not None:
                    row(f"yaml {cfg.name} 的 mode", f"{int(mode)}", f"{cfg.mode}",
                        int(mode) == int(cfg.mode),
                        "mode 是**声明**，不一致就得改 yaml（或把电机写回 yaml 声明的模式）")
        except Exception as e:                       # noqa: BLE001 —— 配置读不到不该让 verify 挂掉
            row("config/joint.yaml", f"读不了（{type(e).__name__}）", "—", True, "跳过")

        return rows, all(r[3] for r in rows)


# ───────────────────────── 落盘 ─────────────────────────
def dump_dir(motor_id: int) -> Path:
    return REPO / "registers" / f"{motor_id:02d}"


def save_dump(motor_id: int, motor_type: str | None, port: str, values: dict, tag: str) -> Path:
    d = dump_dir(motor_id)
    d.mkdir(parents=True, exist_ok=True)
    p = d / f"{time.strftime('%Y%m%d-%H%M%S')}_{tag}.json"
    p.write_text(json.dumps({
        "saved_at": time.strftime("%Y-%m-%dT%H:%M:%S"),
        "port": port,
        "motor_id": motor_id,
        "motor_type": motor_type,
        "registers": values,
    }, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    return p


def load_dump(path: Path) -> dict:
    return json.loads(Path(path).read_text(encoding="utf-8"))


# ───────────────────────── CLI ─────────────────────────
def _add_hw_args(p):
    p.add_argument("--port", default="/dev/ttyACM0", help="USB-CAN 适配器串口")
    p.add_argument("--baud", type=int, default=921600)
    p.add_argument("--timeout", type=float, default=0.05, help="单次寄存器等待（秒）")
    p.add_argument("--id", type=lambda s: int(s, 0), required=True, help="电机 slave id（0x01 / 1）")
    p.add_argument("--type", choices=sorted(EXPECTED_LIMIT), default=None,
                   help="电机型号；不给就从 Gr 推")


def _open(args) -> MotorBus:
    return MotorBus(args.port, baud=args.baud, timeout=args.timeout).open()


def cmd_list(args) -> int:
    print(f"{'RID':<7}{'变量':<11}{'读写':<5}{'类型':<8}{'范围':<14}说明")
    for r in REGISTERS:
        unit = "" if r.per_unit is None else f"  [1 计数 = {r.per_unit * 1e6:g} µs]"
        print(f"0x{r.rid:02X}   {r.name:<11}{r.access:<5}{r.kind:<8}{r.limits:<14}{r.note}{unit}")
    print(f"\n共 {len(REGISTERS)} 个。uint32 类型的 RID（与 SDK is_in_ranges 一致）："
          f"{', '.join(f'0x{r:02X}' for r in sorted(_INT_RIDS))}")
    print(f"默认 dump 集合：{', '.join(DEFAULT_DUMP)}")
    return 0


def cmd_dump(args) -> int:
    bus = _open(args)
    tool = RegisterTool(bus, args.id, args.type)
    regs = args.regs.split(",") if args.regs else None
    values, misses = tool.dump(regs, args.timeout)
    for k, v in values.items():
        print(f"  {k:<18}{BY_LABEL[k].fmt(v)}")
    p = save_dump(args.id, args.type, args.port, values, args.tag)
    print(f"\n读了 {len(values)} 个 → {p.relative_to(REPO)}")
    if misses:
        print(f"⚠ 有 {len(misses)} 个没回包，没进快照：{', '.join(misses)}")
    return 0 if not misses else 1


def cmd_verify(args) -> int:
    bus = _open(args)
    tool = RegisterTool(bus, args.id, args.type)
    rows, ok = tool.verify(args.timeout)
    print(f"{'项目':<26}{'实测':<20}{'期望':<22}{''}结论")
    for item, got, want, good, hint in rows:
        mark = "✓" if good else "✗"
        # 提示只在**没过**时才打（过了还打"该怎么修"会误导）
        print(f"{item:<26}{str(got):<20}{str(want):<22}{mark} {hint if not good else ''}")
    print("\n全部通过 ✓" if ok else "\n有不一致 ✗ —— 见上面标 ✗ 的行")
    return 0 if ok else 1


def cmd_set(args) -> int:
    bus = _open(args)
    tool = RegisterTool(bus, args.id, args.type)
    reg = resolve_reg(args.rid)
    tool.check_writable(reg, args.force)          # 先拒，再备份（拒了就不必备份）

    before = tool.read(reg.rid, args.timeout)
    print(f"{reg.label}（{reg.kind}）  当前 = {reg.fmt(before)}")
    print(f"  将要写入 = {reg.fmt(args.value)}")
    if args.dry_run:
        print("--dry-run：一个字节都没写。")
        return 0

    p = save_dump(args.id, args.type, args.port,
                  {r.label: tool.read(r.rid, args.timeout) for r in
                   [resolve_reg(x) for x in DEFAULT_DUMP]}, "baseline")
    print(f"  写前基线：{p.relative_to(REPO)}")

    before, after, echo_ok = tool.write(reg, args.value, args.force, args.timeout)
    print(f"  写完读回 = {reg.fmt(after)}"
          f"{'（回包与发出的字节一致 ✓）' if echo_ok else '（⚠ 回包对不上/超时）'}")
    if after is None:
        print("⚠ 没读到回包：电机可能没接受（RID/类型/接线），**不代表写成功了**")
        return 1

    if args.save:
        if not args.yes:
            die("要写 flash 得加 --yes（手册：**只在失能状态下生效**，寿命约 1 万次）")
        print("  正在存参数（写 flash，最长 30ms）…")
        ok = bus.save_params(args.id)
        print("  存储回包收到 ✓" if ok else "  ⚠ 没收到存储回包（掉了？还是电机没在失能态？）")
        return 0 if ok else 1
    if reg.rid == 0x0A:
        print("  ⚠ 0x0A 是 **RAM 不是 flash**：掉电复位。要保留就 --save --yes，"
              "并同步改 `Joint.mode` / `config/joint.yaml` 的 mode")
    return 0


def cmd_restore(args) -> int:
    bus = _open(args)
    tool = RegisterTool(bus, args.id, args.type)
    data = load_dump(args.file)
    values = data.get("registers", {})
    if not values:
        die(f"{args.file} 里没有 registers 字段")
    print(f"从 {args.file} 恢复 {len(values)} 个寄存器"
          f"（存于 {data.get('saved_at', '?')}，motor_id={data.get('motor_id')}）")
    n_ok = n_skip = 0
    for label, want in values.items():
        reg = resolve_reg(label.split()[-1])
        try:
            tool.check_writable(reg, args.force)
        except SystemExit:
            n_skip += 1
            continue
        now = tool.read(reg.rid, args.timeout)
        if now is not None and abs(float(now) - float(want)) < 1e-9:
            n_skip += 1
            continue
        if not args.yes:
            print(f"  [将写] {reg.label}: {reg.fmt(now)} → {reg.fmt(want)}")
            continue
        before, after, echo_ok = tool.write(reg, want, args.force, args.timeout)
        print(f"  {reg.label}: {reg.fmt(before)} → {reg.fmt(after)}"
              + ("" if echo_ok else "  ⚠ 回包对不上"))
        n_ok += 1 if after is not None else 0
    if not args.yes:
        print("\n上面是**计划**；确认要写就加 --yes")
        return 0
    print(f"\n写完 {n_ok} 个，跳过（相同/拒写）{n_skip} 个。"
          f"0x0A 若被改过，记得 --save --yes 才留得住")
    return 0


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(
        prog="dm_registers",
        description="达妙电机寄存器工具（读 / 写 / 存参数 / 回滚）。默认不碰 flash。")
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("list", help="列出寄存器表（不需要硬件）")
    p.set_defaults(fn=cmd_list)

    p = sub.add_parser("dump", help="读一批寄存器并存成 JSON")
    _add_hw_args(p)
    p.add_argument("--regs", default=None, help="逗号分隔的名字/RID；默认一套常用集合")
    p.add_argument("--tag", default="dump")
    p.set_defaults(fn=cmd_dump)

    p = sub.add_parser("verify", help="与型号档位表 / config/joint.yaml 对拍")
    _add_hw_args(p)
    p.set_defaults(fn=cmd_verify)

    p = sub.add_parser("set", help="写一个寄存器（默认只进 RAM）")
    _add_hw_args(p)
    p.add_argument("--rid", required=True, help="寄存器名或地址，如 PMAX / 0x15 / 21")
    p.add_argument("--value", type=float, required=True)
    p.add_argument("--dry-run", action="store_true", help="只看不写")
    p.add_argument("--save", action="store_true", help="写入后再存储参数（写 flash）")
    p.add_argument("--yes", action="store_true", help="确认写 flash")
    p.add_argument("--force", action="store_true", help="越过拒写清单（RO 仍然拒）")
    p.set_defaults(fn=cmd_set)

    p = sub.add_parser("restore", help="拿一份 dump 把值写回去")
    _add_hw_args(p)
    p.add_argument("--file", required=True, help="dump 出来的 JSON 路径")
    p.add_argument("--yes", action="store_true", help="确认真写（默认只打印计划）")
    p.add_argument("--force", action="store_true", help="越过拒写清单（RO 仍然拒）")
    p.set_defaults(fn=cmd_restore)

    args = ap.parse_args(argv)
    return args.fn(args)


if __name__ == "__main__":
    raise SystemExit(main())
