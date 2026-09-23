#!/usr/bin/env python3
"""真机 MIT 点动 —— **用 `Joint`（薄类）+ 这里自己写的控制循环**。

## 这个工具为什么存在

`joint.py` 是**薄**的：`set_mit()` 只发**一帧**，发完就不管。要让电机真的转到某个
位置，必须有个循环按 200~500Hz 不停发新目标 —— 那是"控制循环"的活，按 design.md §四
的类图它**不属于 `Joint`**。所以循环写在这里。

这也正是本工具与 `dm_bringup.py jog --mit` 的区别：

    dm_bringup jog --mit   : 自己构帧自己解帧，**绕开封装层**，验的是协议与硬件
    joint_jog.py（本文件）: 走 `Joint`,**验的是封装层** —— `joint.py` 至今没跟真电机
                             说过话，这里的每一帧都是它构造的

所以两者都要留：一个证明"硬件对了"，一个证明"我的抽象对了"。前者的循环逻辑已被
真机验证过，本文件的循环是**照着它抄的**，只把收发换成 `MotorBus`。

## 阶梯：为什么不是直接发正弦

MIT 是**上位机自己闭环**，力矩 = `kp·(q_des − q)`。所以出多大力**完全由发出去的 kp 决定**。
那就一档一档加，每档先确认上一档干净：

    阶段A  kp=0, kd=kd  → 零刚度。通电但**物理上不可能主动转**（只能感觉到阻尼）
    阶段B  kp=--kp, kd  → 原地保持 p0。应该"变硬"，试图转它会被推回
    阶段C  正弦 q_des   → 真正的点动

意义在于：任何异常都会在**最弱的那一档**暴露，而不是在满幅正弦的中途。

## 四道中止条件（全部照搬 `cmd_jog_mit`，一条不少）

    ① 使能后位置跳变 > --jump-abort        → 立刻停下
    ② 连续 --miss-tol+1 次收不到反馈       → 链路断了/电机掉线，立刻停下
    ③ ERR 不在 {0x0, 0x1}                  → 立刻停下（走 `Joint.assert_healthy()`，它会先失能）
    ④ |tau| > --tau-abort                  → 立刻停下

外加**软限位**：`Joint` 的 `position_min/max` 会被设成 `p0 ± (amp + margin)`，
这是在真机上第一次启用 `joint.py` 的软限位层（`smoke_joint.py` 里只验过语义）。

## `--exp kpsweep`：让电机自己说出"多大劲才肯动"

`--exp jog`（默认）是上面那套阶梯。`--exp kpsweep` 是**另一件事**：
用同一个 `stage()`（所以四道中止检查一条不少地共享），但把 `q_des` 钉住不动、
**逐级加大 kp**，去看**摩擦**。

原理只有一行：MIT 下 `tau = kp·(q_des − q)`。令 `q_des = p0 ± E` 固定：

    kp 还小  ⇒  tau = kp·E 顶不破摩擦  ⇒  轴一动不动，偏差恒为 E
                                                    ↓
    kp 够大  ⇒  tau 超过摩擦  ⇒  轴"啪"地跳走
                                                    ↓
    跳完之后 ⇒  轴停在新位置上  ⇒  偏差被压小，tau 从低位重新开始涨

所以左半段是 `tau = kp·E` 的**严格直线**（这就是 kp 的定义，可用来验 kp 是不是
线性放大器）；右半段是**一串"停住 → 涨 → 跳"**。

**这个工具只采集，不判读。** 判读在 `tools/friction_report.py`。

### 为什么判读要分出去（这一条是真机撞出来的）

2026-09-23 第一次真机跑，**本工具**当场判读，报：

    静摩擦（下界）= 0.5128 N·m    ← 错的

那个 0.5128 是**一条单调爬升曲线的中位数**，不是平台高度。真实的脱离力矩是
0.64~0.71 N·m（4 次跳变），文档里记着的 0.144 更是差了 4.8 倍。

根因不是"判读少写了一个条件"，而是：**判读只跑那一次，错了没法复跑、没法回归** ——
逐帧样本算完中位数就丢了，事后再想换个算法判一遍，没料了。

所以现在切成两半：

    本工具     安全地采数据 + 存盘（这是它唯一能在真机上验证的部分）
    friction_report.py   判读（能拿存档数据反复跑，能进回归测试，不用上电）

**每次跑完都会存盘**（`registers/<id>/<时间戳>_kpsweep.json`，含全部逐帧样本，
约 200 KB）。不想存加 `--no-save`。跑完照着打印出来的那行命令跑判读即可。

⚠️ **诚实说明**：这里测的是电机**主动去转**时的摩擦，比手推（反向驱动）感受到的**小**。
       而且它**不是一个常数** —— 实测量到的 4 次脱离力矩散布在 0.56~0.71 N·m。
       详见 `friction_report.py` 的输出。

## ⚠️ 安全

本工具**会真的让电机出力**。跑之前必须：裸电机固定好 / 周围无人 / **手边能立刻断电**。
不加 `--yes` 它只打印不执行。它**不写任何寄存器、不设零位、不切模式**
（`limit` 是只读的 0x15/0x16/0x17）。

用法（在 DM_Armx 根目录）：

    pixi run python tools/joint_jog.py --type DM4340 --dry-run      # 零硬件，先看它要发什么
    pixi run python tools/joint_jog.py --type DM4340                # 只做预检，不动
    pixi run python tools/joint_jog.py --type DM4340 --yes          # 真跑阶梯

    pixi run python tools/joint_jog.py --exp kpsweep --type DM4340 --dry-run
    pixi run python tools/joint_jog.py --exp kpsweep --type DM4340 --yes   # 真扫 + 存盘
    pixi run python tools/friction_report.py registers/01/<时间戳>_kpsweep.json  # **判读**

（`--type` 两条路都必填 —— 它决定用哪个档位算编码，跟连不连硬件无关。
 所以 `--dry-run` 也不能省。）

（`--type` 是**必填**、无默认，取值 `DM4310` / `DM4340`。有默认就等于给了"猜错档位"
的机会 —— 而档位错了力矩差 2.8 倍**且不报错**。）
"""
import argparse
import json
import math
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "src" / "DMmotor_driver"))

from DMmotor_driver import dm_bringup as B          # noqa: E402
from DMmotor_driver import dm_frames as F           # noqa: E402
from DMmotor_driver.dm_bus import MotorBus          # noqa: E402
from DMmotor_driver.joint import Joint              # noqa: E402

# 两个档位。**必须来自 0x15/0x16/0x17 的回读值**，用错档位解出来的力矩差 2.8 倍
# 而且不报错（design.md D5）—— 所以下面每次运行都会去回读，并用它交叉核对 --type。
LIMIT_OF_TYPE = {
    "DM4310": (12.5, 30.0, 10.0),
    "DM4340": (12.5, 10.0, 28.0),   # 枚举里没有 4340P，用 DM4340 代替
}
REG_PMAX, REG_VMAX, REG_TMAX = 0x15, 0x16, 0x17


# ───────────────────────── 预检（只读，SDK 路径）─────────────────────────
def preflight(a):
    """读映射范围 + 当前位置 + ERR + 温度。**全程只读**。

    为什么要单独开一段 SDK 会话：读寄存器（0x15/0x16/0x17）的帧构造在 SDK 里，
    `dm_frames` 没有（寄存器 I/O 不是 `MotorBus` 的活）。但**同一时刻只能有一个
    控制器占用总线**（readme 安全规则 1），所以这段读完就 `ser.close()`，
    之后才开 `MotorBus` —— 两段**串行**，不并发。

    返回 (limit, p0, err, err_text, t_mos, t_rotor)，任何一步读不到就 `die`。
    """
    sdk_dir = B.find_sdk_dir(a.sdk_dir)
    DM_CAN = B.load_sdk(sdk_dir)

    print(f"\n[预检] 打开 {a.port}（只读：0x15/0x16/0x17 + 一帧刷新）")
    ser = B.open_port(a.port, a.timeout)
    try:
        ns = argparse.Namespace(type=a.type, id=a.id, fb_id=a.fb_id)
        motor = B.make_motor(ns, DM_CAN)
        ctrl = DM_CAN.MotorControl(ser)
        ctrl.addMotor(motor)

        # ① 映射范围。**不能信 --type**，以回读为准（--type 只用来交叉核对）
        dims = []
        for rid, name in ((REG_PMAX, "PMAX"), (REG_VMAX, "VMAX"), (REG_TMAX, "TMAX")):
            val, tries = B.read_param(ctrl, motor, rid, attempts=3)
            if val is None:
                B.die(f"0x{rid:02X} {name} 读不到（试了 {tries} 次）。\n"
                      "  映射范围拿不到就不能使能 —— 用错范围会让**每一个命令值都是错的**，\n"
                      "  而且是静默的。先排查链路（scan_bus.py）再回来。")
            dims.append(float(val))
            print(f"    0x{rid:02X} {name:8s} = {val:g}"
                  + (f"（试了 {tries} 次）" if tries > 1 else ""))
        limit = tuple(dims)

        # ② 交叉核对：回读值必须和 --type 声称的档位一致
        expect = LIMIT_OF_TYPE[a.type]
        if limit != expect:
            B.die(
                f"回读到的映射范围 {limit} 与 --type {a.type} 声称的 {expect} **不一致**。\n"
                f"  这两者必须相等才能继续：用错档位解出来的力矩差 2.8 倍**而且不报错**。\n"
                f"  多半是台架上装的不是 {a.type} —— 用 tools/scan_bus.py 认清楚再来。"
            )
        print(f"    ✓ 与 --type {a.type} 的 {expect} 一致")

        # ③ 当前位置 + ERR + 温度（刷新帧，只读）
        frames, _, _ = B.refresh_and_read(ser, DM_CAN, a.id, limit)
        if not frames:
            B.die("读不到反馈。**不知道当前位置就不能使能** —— 使能后第一帧 MIT 的 "
                  "q_des 一旦和实际位置差得远，电机就会猛地往那边冲。")
        d = B.decode_feedback(frames[0][7:15], limit)
        p0 = d["pos"]
        print(f"    位置 p0 = {p0: .4f} rad   ERR={d['err']}（{d['err_text']}）"
              f"   T_MOS={d['t_mos_raw']}℃  T_rotor={d['t_rotor_raw']}℃")
        if d["err"] not in (0x0, 0x1):
            B.die(f"电机报错 ERR={d['err']}（{d['err_text']}），先排查。\n"
                  "  ERR=13 通讯丢失是**锁存**的：enable / 连发帧 / 写 0x09=0 都清不掉，\n"
                  "  只能给电机**断电再上电**。")
        return limit, p0, d["err"], d["err_text"], d["t_mos_raw"], d["t_rotor_raw"]
    finally:
        ser.close()
        print(f"    （{a.port} 已关闭 —— 下面交给 MotorBus，同一时刻只有一个控制器）")


# ───────────────────────── 打印小工具 ─────────────────────────
def _row(t, q_des, st):
    print(f"    {t:7.2f}{q_des:9.4f}{st.position:9.4f}{q_des - st.position:9.4f}"
          f"{st.torque:9.4f}{st.temp_rotor:9d}  {st.err}({_err_text(st.err)})")


def _err_text(e: int) -> str:
    return F.ERR_DECODE.get(e, "未知")


def _median(xs):
    """中位数。

    为什么不用单帧、也不用均值：静摩擦附近是**黏滑**（stick-slip）的 ——
    轴会"卡住 → 啪一下跳 → 再卡住"。单帧很容易恰好采在跳变那一瞬；
    均值又会被那一下拽偏。中位数对少量离群点免疫，正好适合这种情况。
    """
    s = sorted(xs)
    n = len(s)
    if n == 0:
        return float("nan")
    return s[n // 2] if n % 2 else 0.5 * (s[n // 2 - 1] + s[n // 2])


def _refresh_state(bus: MotorBus, mid: int, wait: float):
    """读一帧**新鲜**状态：flush → **发刷新帧** → 等。

    ⚠️ 这里必须自己发帧。`MotorBus.poll_and_wait()` 的字面意思是"只等不发"
    （它的 docstring：*"比如 0x7FF 刷新帧已经用 send_frame 发过了"*），
    什么都不发就等，只会等到超时 —— 这个坑我第一次实跑就踩了。

    也不直接用 `bus.send_refresh()` 了事：它不 flush，而 `wait_feedback` 的
    docstring 明确要求"发之前先 flush"，否则读到的是上一条命令的残留应答。
    """
    bus.flush()
    bus.send_refresh(mid)
    return bus.poll_and_wait(mid, timeout=wait)


# ───────────────────────── dry-run（零硬件）─────────────────────────
def dry_run(a) -> int:
    """只打印帧结构与阶梯顺序。**不开串口、不读寄存器、不需要电机在架**。"""
    limit = LIMIT_OF_TYPE[a.type]
    ampl = min(a.amp, 1.0)
    p0 = 0.0                                    # 真值要读硬件，这里只是占位
    print(f"\n[DRY-RUN] 档位 {a.type} → limit=(PMAX {limit[0]:g}, VMAX {limit[1]:g}, "
          f"TMAX {limit[2]:g})")
    print("  注意：真机上这三个值**以 0x15/0x16/0x17 的回读为准**，并与 --type 交叉核对；")
    print("  dry-run 只是拿它算编码，不构成'这台就是 4340'的证据。\n")
    for tag, kp, q in (("阶段A 零刚度（通电但出不了力）", 0.0, p0),
                       ("阶段B 原地保持", a.kp, p0),
                       (f"阶段C 正弦 +{ampl:g}", a.kp, p0 + ampl)):
        B.explain_tx(F.mit_frame(a.id, q, 0.0, kp, a.kd, 0.0, limit),
                     f"{tag}：CAN ID=0x{a.id:02X} kp={kp:g} kd={a.kd:g} q_des={q:.4f}")
    print(f"\n软限位会设成 p0 ± ({ampl:g} + {a.soft_margin:g}) rad"
          "（**真机第一次启用 joint.py 的软限位层**）")
    print(f"中止条件：跳变>{a.jump_abort:g} rad · 连续丢 {a.miss_tol + 1} 帧 · "
          f"ERR∉{{0,1}} · |tau|>{a.tau_abort:g} N·m")
    print("\nDRY-RUN 结束。真机顺序：预检 → 开总线 → enable → A → B → C → 回 p0 卸力 → disable")
    return 0


# ───────────────────── 静摩擦扫描（--exp kpsweep）─────────────────────
def _steps_of(a):
    """kp 的扫描序列：0, step, 2·step, … ≤ kp_max。

    用整数下标乘出来（`i * step`）而**不是累加** —— 浮点累加几十次会飘，
    而且 `--kp-step 0.1` 这类值累加到最后会漏掉或补上一档。
    """
    n = int(round(a.kp_max / a.kp_step)) + 1
    return [round(i * a.kp_step, 6) for i in range(max(n, 1))]


def _validate_sweep(a):
    """扫描参数自相矛盾就在**碰硬件之前**死掉。一条都不留到真机上才发现。"""
    if a.e <= 0:
        B.die("--e 必须 > 0（它就是要钉住的那个偏差）")
    if a.kp_step <= 0:
        B.die("--kp-step 必须 > 0")
    if a.kp_max < a.kp_step:
        B.die(f"--kp-max {a.kp_max:g} < --kp-step {a.kp_step:g}，一步都扫不了")

    # 每步、以及归位阶段，都得够采 median_n 帧
    for tag, secs in (("每步 --hold", a.hold), ("归位 --settle", a.settle)):
        n_avail = secs * a.hz
        if n_avail < a.median_n:
            B.die(f"{tag} {secs:g}s × --hz {a.hz:g} = {n_avail:.0f} 帧，"
                  f"取不出 --median-n {a.median_n} 帧的中位数。\n"
                  "  加长时长，或降 --median-n，或提 --hz。")

    # ★ 最要命的一条：扫描会把自己的中止条件触发掉。
    max_tau = a.kp_max * a.e
    if a.tau_abort > 0 and max_tau >= a.tau_abort:
        B.die(f"--kp-max × --e = {a.kp_max:g} × {a.e:g} = {max_tau:.3f} N·m，"
              f"已 ≥ --tau-abort {a.tau_abort:g} N·m。\n"
              "  扫描会触发自己的中止条件，跑一半就停 —— 数据残缺且看着像故障。\n"
              "  要么降 --kp-max，要么抬 --tau-abort（抬它 = 抬「允许出多大劲」的上限，\n"
              "  别抬到接近 TMAX）。")


def dry_run_sweep(a) -> int:
    """零硬件：只打印扫描结构与首/末两帧的字节。**不开串口、不读寄存器**。"""
    _validate_sweep(a)
    limit = LIMIT_OF_TYPE[a.type]
    steps = _steps_of(a)
    p0 = 0.0                                    # 真值要读硬件，这里只是占位
    dirs = {"plus": [1], "minus": [-1], "both": [1, -1]}[a.dir]

    print(f"\n[DRY-RUN] 档位 {a.type} → limit=(PMAX {limit[0]:g}, VMAX {limit[1]:g}, "
          f"TMAX {limit[2]:g})")
    print("  注意：真机上 limit **以 0x15/0x16/0x17 的回读为准**并与 --type 交叉核对；")
    print("  dry-run 只是拿它算编码，不构成「这台就是 4340」的证据。\n")
    print(f"  扫描：kp {steps[0]:g} → {steps[-1]:g}，步长 {a.kp_step:g}，共 {len(steps)} 步")
    print(f"        偏差钉在 p0 ± {a.e:g} rad（轴最多只走这么远）")
    print(f"        每步保持 {a.hold:g}s ≈ {a.hold*a.hz:.0f} 帧，读末尾 "
          f"{a.median_n} 帧中位数")
    print(f"        方向 {a.dir}；每方向 ≈ 归位 {a.settle:g}s + {len(steps)*a.hold:.0f}s"
          f" = {a.settle + len(steps)*a.hold:.0f}s")
    print(f"        **每个方向开扫前先归位到 p0 并重新取基准** —— 否则反向那趟的"
          "起始误差是 E+残留，脱离点会假")
    print(f"  最大指令力矩 {steps[-1]:g} × {a.e:g} = {steps[-1]*a.e:.3f} N·m")
    print(f"  中止条件：跳变>{a.jump_abort:g} rad · 连续丢 {a.miss_tol + 1} 帧 · "
          f"ERR∉{{0,1}} · |tau|>{a.tau_abort:g} N·m\n")

    for tag, kp in ((f"第 1 档 kp={steps[0]:g}（零刚度，出不了力）", steps[0]),
                    (f"中间档 kp={steps[len(steps)//2]:g}", steps[len(steps)//2]),
                    (f"末档 kp={steps[-1]:g}（出最大力 {steps[-1]*a.e:.3f} N·m）",
                     steps[-1])):
        for sign in dirs:
            q = p0 + sign * a.e
            B.explain_tx(F.mit_frame(a.id, q, 0.0, kp, a.kd, 0.0, limit),
                         f"{tag}，q_des={q:+.4f}（p0{sign:+g}·{a.e:g}）"
                         f"：CAN ID=0x{a.id:02X} kd={a.kd:g}")

    print(f"\n软限位会设成 p0 ± ({a.e:g} + {a.soft_margin:g}) rad")
    print("DRY-RUN 结束。真机顺序：预检 → 开总线 → enable → 正方向扫描 → 负方向扫描 "
          "→ 回 p0 卸力 → disable")
    return 0


def sweep(a, p0, stage):
    """采数：固定偏差、逐级加大 kp，把**每一帧**都记下来。

    **这里不做判读。** 判读在 `tools/friction_report.py`，理由见那个文件的开头 ——
    一句话：2026-09-23 第一次真机跑，工具自己判读报了个错的数（0.5128 N·m，
    实际是 0.64~0.71），而且**错了没法复跑、没法拿存档数据回归**，因为判读
    只跑那一次、逐帧样本算完中位数就丢了。采集与判读分开之后，判读能反复跑。

    为什么复用 `stage()` 而不是自己写循环：`stage()` 里那四道中止检查
    （跳变 / 丢帧 / ERR / |tau| 超阈）是**真机验证过**的，重写一份就等于
    把最危险的部分变成"没跑过的第二份"。所以这里只做"喂参数 + 记帧"。

    签名里没有 `bus` / `j` / `period` —— 这里一个字节都不往总线写，发帧全在
    `stage()` 里。**故意留成这么窄的依赖面**：`tools/smoke_joint_sweep.py`
    能塞一个假的 `stage` 进来，把采集流程在没有电机的情况下跑一遍。

    返回存盘用的 dict（含逐帧样本）；**中止时返回 None** —— 调用方两种都能吃。
    """
    steps = _steps_of(a)
    dirs = {"plus": [1], "minus": [-1], "both": [1, -1]}[a.dir]
    sweeps = []
    t_all = time.monotonic()

    for sign in dirs:
        # ★ 归位 + **重新取基准**。这一步不能省：
        #   上一个方向扫完时轴是**偏着的**（停在 kp·err = 摩擦 的地方）。
        #   不归位就直接反向扫，起始误差会变成 `E + 残留` 而不是 `E` ——
        #   脱离点会被推到一个**假的 kp** 上，而输出看着完全正常。
        #   （静默错误，正是本项目已经踩过三次的那一类。）
        s0 = []
        if not stage("归位 p0", a.settle, (lambda t: p0), a.kp, samples=s0, quiet=True):
            return None
        if not s0:
            print("    ⚠ 归位阶段一帧都没采到 —— 链路有问题，停止")
            return None
        p_start = _median([x[2] for x in s0[-min(a.median_n, len(s0)):]])
        d0 = p_start - p0
        print(f"\n[归位] 目标 p0={p0: .4f} → 实测停在 {p_start: .4f}"
              f"（残留 {d0:+.4f} rad）"
              + ("" if abs(d0) <= a.move_tol else
                 f"\n    ⚠ 残留超了 {a.move_tol:g} rad —— kp={a.kp:g} 顶不动摩擦。"
                 "基准改成「实际停的位置」，起始误差仍严格等于 E"))

        # q_des 以**实测停的位置**为基准（不是 p0）：这样起始误差恰好是 E
        q_des = p_start + sign * a.e
        print(f"\n[扫描] q_des 钉在 基准{sign:+g}·{a.e:g} = {q_des: .4f} rad"
              f"（{len(steps)} 步 × {a.hold:g}s ≈ {len(steps)*a.hold:.0f}s）"
              "  —— 下面是**采集**，判读等扫描完再跑")
        print(f"    {'kp':>6}{'q_des':>10}{'实测':>10}{'偏差':>10}{'tau':>10}"
              f"{'tau/(kp·E)':>13}  上一档相对")

        rec_steps = []
        prev_q = p_start
        for kp in steps:
            s = []
            if not stage(f"kp={kp:g}", a.hold, (lambda t, q=q_des: q), kp,
                         samples=s, quiet=True):
                return None
            if not s:
                print(f"    ⚠ kp={kp:g} 一步都没采到样本 —— 链路有问题，停止")
                return None

            tail = s[-min(a.median_n, len(s)):]
            q_med = _median([x[2] for x in tail])
            tau_med = _median([x[3] for x in tail])
            err = q_des - q_med
            d_prev = q_med - prev_q        # 相对**上一档**（不复位），看每档动没动
            prev_q = q_med
            # 这一列只是给操作者看的**事实**，不是判读：判读（脱离事件、零偏、
            # 线性、散布）全在 friction_report.py，那里才下结论。
            ratio = f"{tau_med/(kp*a.e):13.3f}" if kp > 0 else f"{'—':>13}"
            print(f"    {kp:6.1f}{q_des:10.4f}{q_med:10.4f}{err:10.4f}{tau_med:10.4f}"
                  f"{ratio}  {d_prev:+.4f}")
            rec_steps.append({
                "kp": kp, "q_des": q_des,
                "q_med": round(q_med, 6), "tau_med": round(tau_med, 6),
                "err": round(err, 6), "n_frames": len(s),
                # ⚠ **存全精度，不存打屏那 4 位**。打屏的舍入会让
                # `q_des − q` 和 `偏差` 列互相差 0.0001 rad；判读要拿这些数
                # 做 `kp × 偏差` 的减法，低 kp 处这点差别会被放大成假信号。
                "samples": [[round(x[0], 4), round(x[1], 6), round(x[2], 6),
                             round(x[3], 5)] for x in s],
            })
        sweeps.append({
            "sign": sign, "baseline": round(p_start, 6), "q_des": round(q_des, 6),
            "settle": {"target": round(p0, 6), "q_end": round(p_start, 6),
                       "n_frames": len(s0)},
            "steps": rec_steps,
        })

    n_fr = sum(st["n_frames"] for sw in sweeps for st in sw["steps"])
    print(f"\n[采集完成] {len(steps)} 档 × {len(dirs)} 方向，共 {n_fr} 帧，"
          f"用时 {time.monotonic()-t_all:.1f}s")
    return {"sweeps": sweeps, "n_frames": n_fr}


def _save_record(a, limit, p0, rec, bus_stats):
    """原始数据写盘 —— 落到 `registers/<id>/<时间戳>_kpsweep.json`。

    **这是本次改动的重点，不是附赠。** 上一轮判读错了没人发现，根因就是
    逐帧样本算完中位数就丢了 —— 事后再想换个算法判一遍，没料了。
    """
    if a.no_save:
        print("\n[存盘] ⚠ --no-save：原始数据**没有**存下来。\n"
              "       判读可以另外跑，但一旦发现判读有问题，这批数据就再也回不来了。")
        return None

    d = Path(a.reg_dir) / f"{a.id:02d}"
    d.mkdir(parents=True, exist_ok=True)
    ts = time.strftime("%Y%m%d-%H%M%S")
    fp = d / f"{ts}_kpsweep.json"
    doc = {
        "saved_at": time.strftime("%Y-%m-%dT%H:%M:%S"),
        "kind": "kpsweep",
        "tool": "tools/joint_jog.py",
        "command": " ".join(sys.argv),
        "port": a.port, "motor_id": a.id, "motor_type": a.type,
        "limit": list(limit),
        "params": {"e": a.e, "kp_max": a.kp_max, "kp_step": a.kp_step, "kd": a.kd,
                   "hz": a.hz, "hold": a.hold, "settle": a.settle,
                   "median_n": a.median_n, "move_tol": a.move_tol, "dir": a.dir},
        "p0_preflight": round(p0, 6),
        "bus_stats": bus_stats,
        **rec,
    }
    fp.write_text(json.dumps(doc, ensure_ascii=False, indent=1) + "\n",
                  encoding="utf-8")
    return fp


# ───────────────────────── 主流程 ─────────────────────────
def run(a) -> int:
    if a.dry_run:
        # **dry-run 绝不碰硬件**（不开串口、不读寄存器）。位置无从得知，用 0 代替 ——
        # 这里要看的只是帧结构与阶梯/扫描顺序，不是真值。
        return dry_run_sweep(a) if a.exp == "kpsweep" else dry_run(a)

    if a.exp == "kpsweep":
        # 参数自相矛盾在**碰硬件之前**就死掉（尤其是"扫描会触发自己的中止条件"那条）
        _validate_sweep(a)

    limit, p0, _err0, _err_text0, _t_mos, _t_rotor = preflight(a)

    p_max, v_max, t_max = limit
    # `ampl` = "关节要离开 p0 多远"，两条路各自定义：
    #   jog     → 正弦幅值
    #   kpsweep → 固定偏差 E（轴最多也只走这么远）
    ampl = a.e if a.exp == "kpsweep" else min(a.amp, 1.0)
    soft_lo = p0 - ampl - a.soft_margin
    soft_hi = p0 + ampl + a.soft_margin
    cycle_s = a.cycles / a.sine_hz

    print(f"\n[计划] 电机 0x{a.id:02X}  档位 {a.type}  limit(PMAX/VMAX/TMAX)={limit}")
    print(f"  p0 = {p0: .4f} rad，软限位 [{soft_lo: .4f}, {soft_hi: .4f}]（±{ampl:g} + "
          f"{a.soft_margin:g} 余量）")
    if a.exp == "kpsweep":
        steps = _steps_of(a)
        n_dir = 2 if a.dir == "both" else 1
        print(f"  阶梯扫描：偏差钉在 p0 ± {a.e:g} rad，kp {steps[0]:g} → {steps[-1]:g}"
              f"（步长 {a.kp_step:g}，{len(steps)} 步 × {a.hold:g}s ≈ "
              f"{len(steps)*a.hold*n_dir:.0f}s）")
        print(f"          方向 {a.dir}；每步读末尾 {a.median_n} 帧的中位数")
        print(f"          最大指令力矩 {steps[-1]:g} × {a.e:g} = {steps[-1]*a.e:.3f} N·m"
              f"（TMAX={t_max:g} 的 {100*steps[-1]*a.e/t_max:.1f}%）")
        print(f"          （--amp / --sine-hz / --cycles / --t-a / --t-b 本次不生效）")
    else:
        print(f"  A 零刚度 {a.t_a:g}s → B 原地保持 {a.t_b:g}s（kp={a.kp:g}）"
              f" → C 正弦 ±{ampl:g} rad @ {a.sine_hz:g}Hz ×{a.cycles:g} ≈ {cycle_s:.1f}s")
        peak_v = ampl * 2 * math.pi * a.sine_hz
        print(f"  正弦峰值速度 {peak_v:.3f} rad/s（VMAX={v_max:g}，占 "
              f"{100*peak_v/v_max:.1f}%）")
    print(f"  发帧 {a.hz:g} Hz（周期 {1000/a.hz:.1f} ms），kd={a.kd:g}")
    print(f"  中止条件：跳变>{a.jump_abort:g} rad · 连续丢 {a.miss_tol + 1} 帧 · "
          f"ERR∉{{0,1}} · |tau|>{a.tau_abort:g} N·m（TMAX 的 "
          f"{100*a.tau_abort/t_max:.1f}%）")

    # 「同一时刻只有一个控制器」：预检的串口已在 preflight 里关掉了
    bus = MotorBus(a.port, baud=a.baud, timeout=a.timeout)
    bus.open()
    enabled = False
    sweep_rec = None
    try:
        # 软限位是**关节侧**。direction=+1 / offset=0 ⇒ 关节侧 == 电机侧，
        # 所以可以直接用电机侧读数算。将来标定了方向/零位，这里要跟着改。
        j = Joint(bus, a.id, limit, name=f"0x{a.id:02X}", direction=1, offset=0.0,
                  position_min=soft_lo, position_max=soft_hi)
        print(f"\n[0] {j!r}")
        print("    注册到 bus —— 它的 limit 与预检回读值必须一致，不一致 `Joint` 会直接拒绝")

        # 使能前先拿一帧新鲜状态：`Joint.enable()` 会拒绝故障态，而它看的是**缓存**
        st = _refresh_state(bus, a.id, a.wait)
        if st is None:
            B.die("开总线后读不到反馈（刷新帧发出去没应答），先排查链路。\n"
                  "  可能：适配器没插好 / 电机没通电 / 这个 ID 不在总线上。\n"
                  "  `tools/scan_bus.py` 能列出到底谁在应答。")
        print(f"    poll 到：pos={st.pos: .4f} err={st.err}({st.err_text}) "
              f"enabled={st.enabled}")
        print(f"    `Joint` 已构造：limit={j.limit} 软限位=[{soft_lo: .4f}, {soft_hi: .4f}]")

        # ⚠️ **闸门放在这里**，不是更早。位置是刻意的：不加 --yes 时，上面这一整条
        # 链路（预检 → 开总线 → 构造 Joint → 首次 poll → 打印状态）**全部跑过一遍**，
        # 而且全程**零出力**。这样"先空跑一次确认链路"真的能确认到最后一刻，
        # 只差使能那一下 —— 而不是只确认到预检就开始赌总线那半截。
        if not a.yes:
            print("\n[未执行] 上面是**全程只读**的部分，都通过了。再往下 `j.enable()` "
                  "就会让电机**通电出力**。\n"
                  "  确认（裸电机已固定 / 周围无人 / 手边随时能断电）后加 --yes：\n"
                  f"    pixi run python tools/joint_jog.py --type {a.type} --yes\n")
            return 2

        # [1] 使能 + 紧跟一帧零力矩（`Joint.enable()` 内部做，见 joint.py:251）
        print("[1] enable（内部紧跟一帧 kp=0/kd=0/tau=0 零力矩）...")
        j.enable()
        enabled = True
        # ⚠️ `enable()` 内部发了 **2 帧**（使能帧 + 紧跟的零力矩帧），所以会有 **2 条**
        # 应答。只读 1 条的话缓冲里剩 1 条，往下整个循环就**错位一帧** ——
        # 每圈读到的都是上一圈命令的应答，而且不会报错、只会让数据"慢一拍"。
        # 所以这里重新刷新一次（`_refresh_state` 会先 flush 把两条都丢掉）。
        st = _refresh_state(bus, a.id, a.wait)
        if st is None:
            B.die("使能后收不到反馈，立刻失能退出。")
        js = j.get_state()
        print(f"    使能后：pos={js.position: .4f}（相对 p0 {js.position - p0:+.5f}）"
              f"  ERR={js.err}({_err_text(js.err)})")
        if abs(js.position - p0) > a.jump_abort:
            print(f"    ⚠ 位置跳变 {abs(js.position - p0):.4f} > {a.jump_abort:g} rad "
                  "—— 立即失能退出")
            return 1

        t_start = time.monotonic()
        period = 1.0 / a.hz

        def stage(label, secs, q_of_t, kp, note="", samples=None, quiet=False):
            """跑一个阶段，返回 True 表示正常结束。q_of_t(t) → q_des（关节侧）。

            `samples`：给一个 list 就往里追加 `(t, q_des, pos, tau)`。**不给就一行不记**
            （默认 None，所以原有的 A/B/C 三条调用完全不受影响）。
            `--exp kpsweep` 靠它取每一步末尾的中位数。
            `quiet`：不打逐行表、不打标题 —— 扫描有几十步，逐行打会把结果淹掉。
            """
            if not quiet:
                print(f"\n{label}（{secs:g}s，kp={kp:g} kd={a.kd:g}）{note}")
                print(f"    {'t(s)':>7}{'q_des':>9}{'实测':>9}{'误差':>9}"
                      f"{'tau':>9}{'T_rotor':>9}  状态")
            t0 = time.monotonic()
            n = 0
            n_miss = 0
            next_print = 0.0
            tau_max = 0.0
            while True:
                t = time.monotonic() - t0
                if t >= secs:
                    break
                tick = time.monotonic()
                q_des = q_of_t(t)
                # 收发：**flush → 由 Joint 构帧发出 → 等它自己那条应答**。
                # 这就是 `MotorBus.send_and_wait()` 的三步，只是帧必须由
                # `joint.set_mit()` 构造（本工具要验的正是它），所以合并不成一个调用。
                # ⚠️ flush 不能省：1:1 规律下"发之前不 flush"会读到上一条命令的应答，
                # 而这类错**不会报错**，只表现为"数据慢一拍"，非常难查
                # （`wait_feedback` 的 docstring 专门警告过）。
                # 单线程 + 只有本电机在应答 ⇒ flush 与 send 之间那个空隙实际是安全的；
                # 哪天循环里多了别的发送方（7 关节），必须换成真正的原子对。
                bus.flush()
                j.set_mit(kp, a.kd, q_des)
                n += 1
                st = bus.poll_and_wait(a.id, timeout=a.wait)
                if st is None:
                    n_miss += 1
                    for _ in range(a.miss_tol):
                        bus.flush()
                        j.set_mit(kp, a.kd, q_des)
                        n += 1
                        st = bus.poll_and_wait(a.id, timeout=a.wait)
                        if st is not None:
                            break
                    if st is None:
                        print(f"    ⚠ 连续 {a.miss_tol + 1} 次收不到反馈"
                              "（链路断了 / 电机掉线），立即停止")
                        return False

                # ③ ERR 检查走 `Joint.assert_healthy()` —— 它**先失能再抛**，
                #    这正是真机上想要的动作（异常消息里会写明「失能 ≠ 停下」）
                js = j.assert_healthy()
                tau_max = max(tau_max, abs(js.torque))

                # ④ 力矩超阈
                if a.tau_abort > 0 and abs(js.torque) > a.tau_abort:
                    print(f"    ⚠ |tau|={abs(js.torque):.3f} > {a.tau_abort:g} N·m，"
                          "立即停止")
                    return False

                if samples is not None:
                    samples.append((t, q_des, js.position, js.torque))

                if not quiet and t >= next_print:
                    next_print = t + 1.0 / a.print_hz
                    _row(t, q_des, js)

                dt = time.monotonic() - tick
                if dt < period:
                    time.sleep(period - dt)

            el = max(time.monotonic() - t0, 1e-9)
            if not quiet:
                print(f"    发 {n} 帧，用时 {el:.2f}s → 实测 {n/el:.0f} Hz"
                      + (f"；丢帧后重发 {n_miss} 次" if n_miss else "；零丢帧"))
                print(f"    峰值 |tau| = {tau_max:.4f} N·m"
                      f"（TMAX={t_max:g} 的 {100*tau_max/t_max:.1f}%）")
            elif n_miss:
                # 静默模式也不许把丢帧吞掉 —— 这正是最该被看见的信号
                print(f"      ⚠ kp={kp:g}：丢帧后重发 {n_miss} 次")
            return True

        if a.exp == "kpsweep":
            # 静摩擦扫描。**复用同一个 `stage()`** —— 四道中止检查与 jog 完全同一份代码
            sweep_rec = sweep(a, p0, stage)
            if not sweep_rec:
                return 1
        else:
            # [2] 阶段A：零刚度，只有阻尼
            if not stage("[2] 阶段A：kp=0（零刚度，只有阻尼）", a.t_a, lambda t: p0, 0.0,
                         "→ 通电但无力矩；用手转它应该是轻的、转完不会自己回去"):
                return 1

            # [3] 阶段B：原地保持
            if not stage("[3] 阶段B：原地保持 p0", a.t_b, lambda t: p0, a.kp,
                         f"→ 会明显'变硬'，试图转它会被推回 p0={p0: .4f}"):
                return 1

            # [4] 阶段C：正弦
            if not stage(f"[4] 阶段C：正弦 ±{ampl:g} rad @ {a.sine_hz:g}Hz", cycle_s,
                         lambda t: p0 + ampl * math.sin(2 * math.pi * a.sine_hz * t), a.kp,
                         f"→ 应该看到 {a.cycles:g} 个来回"):
                return 1

        # [5] 停回 p0 再卸力 —— **不要在偏离位置时突然失去刚度**
        print(f"\n[5] 回 p0={p0: .4f} 并卸力")
        for _ in range(int(a.hz * 0.6)):
            bus.flush()
            j.set_mit(a.kp, a.kd, p0)
            bus.poll_and_wait(a.id, timeout=a.wait)
            time.sleep(period)
        for _ in range(int(a.hz * 0.2)):
            bus.flush()
            j.set_mit(0.0, 0.0, p0)
            bus.poll_and_wait(a.id, timeout=a.wait)
            time.sleep(period)

        js = j.get_state()
        if js is not None:
            off = js.position - p0
            print(f"    末态：pos={js.position: .4f}（相对 p0 {off:+.5f} rad）"
                  f"  T_rotor={js.temp_rotor}℃")
            # 真的回去了吗？kp 太小推不动摩擦，所以这里必须验，不能只喊一声就完事
            if abs(off) > 0.01:
                print(f"    ⚠ 没回到 p0（差 {abs(off):.4f} rad）。kp={a.kp:g} 在偏差 "
                      f"{abs(off):.3f} rad 处只有 {a.kp*abs(off):.3f} N·m，推不动静摩擦 ——"
                      " 这是 kp 太小的典型症状，不是回零逻辑坏了。\n"
                      "      裸电机上无害；**装到臂上会留下残余偏角，务必先加大 kp**。")
            else:
                print(f"    ✓ 已回到 p0（残差 {abs(off)*1000:.2f} mrad）")

        # [6] disable —— **必须放在打印统计之前**。
        # `n_disable` 是 `Joint` 自己的计数器，原先 disable 只在 `finally` 里做，
        # 于是上面那行 `[统计]` 永远打 `n_disable: 0` —— 紧跟着却是 `[6] disable 完成`，
        # **自相矛盾**（2026-09-23 第一次真机跑就是这么打出来的）。
        # `finally` 里那份**保留**，作为异常/提前返回路径的兜底；重复 disable 无害
        # （`Joint.disable()` 的 docstring：总是可以安全调用）。
        print("\n[6] disable")
        j.disable()
        enabled = False

        print(f"\n[统计] {j.stats()}")
        if j.clamped_count or j.saturated_count:
            print(f"    ⚠ 有 {j.clamped_count} 次钳位 / {j.saturated_count} 次满量程告警 —— "
                  "命令被静默改过，回头看看告警是哪个")
        print(f"    总线：{bus.stats()}")

        # 存盘放在 disable **之后**：万一上面抛出异常，finally 里还会失能，
        # 而那时数据已经落盘了 —— 失败的那一半数据往往比成功的那半更值钱。
        if sweep_rec is not None:
            fp = _save_record(a, limit, p0, sweep_rec, bus.stats())
            if fp is not None:
                kb = fp.stat().st_size / 1024
                print(f"\n[存盘] {fp}（{len(sweep_rec['sweeps'])} 个方向 / "
                      f"{sweep_rec['n_frames']} 帧 / {kb:.0f} KB）")
                print("       判读（脱离事件、零偏、kp 线性、散布）在离线脚本里跑：")
                print(f"         pixi run python tools/friction_report.py {fp}")

        print(f"\n总耗时 {time.monotonic()-t_start:.1f}s")
        return 0
    finally:
        if enabled:
            try:
                for _ in range(3):
                    j.set_mit(0.0, 0.0, p0)      # 先卸力
                    time.sleep(0.02)
                j.disable()
                print("[6] disable 完成（异常/提前返回路径的兜底）")
            except Exception as e:
                print(f"[6] ⚠ 失能时出错：{e} —— **请直接断电**")
        try:
            bus.close()
        except Exception as e:
            print(f"[6] ⚠ 关串口出错：{e}")


def build_parser():
    p = argparse.ArgumentParser(
        description="真机 MIT 点动（走 Joint + 自写控制循环）。不加 --yes 不会动。",
        formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--id", type=lambda s: int(s, 0), default=0x01, help="电机 SlaveID")
    p.add_argument("--type", required=True, choices=sorted(LIMIT_OF_TYPE),
                   help="台架上的型号。**必填、无默认** —— 档位错了力矩差 2.8 倍且不报错。"
                        "注意枚举里没有 4340P，用它就填 DM4340")
    p.add_argument("--port", default="/dev/ttyACM0")
    p.add_argument("--baud", type=int, default=921600)
    p.add_argument("--fb-id", type=lambda s: int(s, 0), default=0x11)
    p.add_argument("--sdk-dir", default=None, help="vendor 的 u2can 目录（一般不用给）")
    p.add_argument("--timeout", type=float, default=0.003,
                   help="串口读超时，秒。小值才能保证 poll 非阻塞")
    p.add_argument("--wait", type=float, default=0.05,
                   help="发一帧后最多等多久反馈（秒）。默认 0.05 = cmd_jog_mit 的实测值；"
                        "调大只会拖慢循环，实测 Hz 会自己报出来")
    p.add_argument("--hz", type=float, default=200.0, help="发帧频率")
    p.add_argument("--print-hz", type=float, default=10.0, help="打屏频率")
    p.add_argument("--kp", type=float, default=20.0, help="阶段B/C 的 kp")
    p.add_argument("--kd", type=float, default=1.0)
    p.add_argument("--amp", type=float, default=0.15, help="正弦幅值 rad（上限截到 1.0）")
    p.add_argument("--sine-hz", type=float, default=0.25)
    p.add_argument("--cycles", type=float, default=2.0)
    p.add_argument("--t-a", type=float, default=2.0, help="阶段A 时长 s")
    p.add_argument("--t-b", type=float, default=2.0, help="阶段B 时长 s")
    p.add_argument("--soft-margin", type=float, default=0.2,
                   help="软限位在正弦幅值外再留的余量 rad")
    p.add_argument("--jump-abort", type=float, default=0.1, help="使能后位置跳变阈值 rad")
    p.add_argument("--tau-abort", type=float, default=2.0, help="|tau| 阈值 N·m，0 = 关")
    p.add_argument("--miss-tol", type=int, default=3, help="允许的重发次数")

    # ── 实验选择 ──
    p.add_argument("--exp", choices=["jog", "kpsweep"], default="jog",
                   help="jog（默认）= 原来的 A/B/C 点动阶梯，行为一字未改；"
                        "kpsweep = 把 q_des 钉住、逐级加大 kp，测静摩擦")

    # ── kpsweep 专用（--exp jog 时全部不生效）──
    g = p.add_argument_group("--exp kpsweep 专用")
    g.add_argument("--e", type=float, default=0.05,
                   help="钉住的偏差 rad。**它同时也是「轴最多只走多远」**，默认 0.05")
    g.add_argument("--kp-max", type=float, default=24.0,
                   help="kp 扫到多大为止。必须满足 kp-max × e < tau-abort")
    g.add_argument("--kp-step", type=float, default=1.0, help="kp 步长")
    g.add_argument("--hold", type=float, default=0.5, help="每一步保持多少秒")
    g.add_argument("--settle", type=float, default=1.0,
                   help="每个方向开扫前「归位到 p0 并停稳」多少秒。**不能设 0**："
                        "上一方向扫完时轴是偏的，不归位会让反向的起始误差变成 E+残留")
    g.add_argument("--median-n", type=int, default=20,
                   help="每步读末尾 N 帧的中位数（黏滑会让单帧恰好采在跳变瞬间）")
    g.add_argument("--dir", choices=["plus", "minus", "both"], default="both",
                   help="扫哪个方向。静摩擦不对称，both 才看得出来")
    g.add_argument("--move-tol", type=float, default=0.002,
                   help="判定「轴动了」的位置阈值 rad。默认 0.002：在编码器量化噪声"
                        "（约 0.0005）之上、在偏差 E 之下")
    g.add_argument("--reg-dir", default="registers",
                   help="原始数据存到哪（默认 registers/，下面的 <id>/ 自动建）")
    g.add_argument("--no-save", action="store_true",
                   help="**不存原始数据**。默认是存的 —— 上一轮判读报了个错数却没"
                        "法复跑，根因就是逐帧样本丢了。除非磁盘实在紧张，别加这个")

    p.add_argument("--dry-run", action="store_true", help="只打印帧，不连硬件")
    p.add_argument("--yes", action="store_true", help="**真的会动**，确认安全后再加")
    return p


def main() -> int:
    a = build_parser().parse_args()
    return run(a)


if __name__ == "__main__":
    sys.exit(main())
