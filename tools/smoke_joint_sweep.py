#!/usr/bin/env python3
"""冒烟测试⑤：静摩擦扫描的**采集 + 判读**离线回归 —— 不需要硬件。

## 上一版为什么没挡住错

2026-09-23 第一版判读报「静摩擦（下界）= 0.5128 N·m」，真值是 0.64~0.71。**这个
测试当时是绿的。** 原因是那个假电机只会"平滑滑到平衡位置"：

    |kp·(q_des − q)| 顶不破摩擦 → 不动
    顶破了                     → 滑到 kp·(q_des−q) = 摩擦 的地方，然后**越滑越顺**

于是脱离之后 tau 恒等于摩擦 —— **真的有一条平台**。而我写的判读就是"找平台"。
`用模型测模型，测不出模型错`：我拿自己的物理模型去验证建立在这个模型上的判读。

真电机不是这样的。它**一次一次地跳**：挡在 0.69 N·m 上不动，跳走之后落在新的
齿位，那里又要 0.69 才推得动 —— 中间 tau 一直在涨、位置纹丝不动。
判读取"脱离之后 tau 的中位数"，取到的就是**一条单调爬升曲线的中位数**，
一个物理上没有任何意义的数。

## 这一版加了什么

**黏滑假电机**（`stick_slip`）：两级摩擦 —— 静摩擦 `theta_s`（顶破才跳）和
残余摩擦 `theta_k < theta_s`（跳完落在那里，被它顶住）。这一条规则就能复现真机：

    theta_s=0.689, theta_k=0.417  ⇒  kp 13→14 跳、14~23 纹丝不动、kp 23→24 再跳
    位置 -1.0084 → -0.9953 → -0.9857   （真机：-1.0279 → -1.0077 → -0.9943，同一形状）

另外把**已经删掉的老算法复刻了一份**（`old_plateau`），专门喂黏滑数据给它，
断言它算出来的数**明显错**。这样万一以后有人（包括我）再把"取中位数当平台"
写回来，会被当场拦下。

## 测什么

  [1] 采集结构：`sweep()` 存下的记录含逐帧样本、档数、基线，能直接被判读脚本吃下
  [2] **平滑电机**（θ_k = θ_s）：脱离力矩下界必须**精确**还原出已知摩擦 0.45
  [3] **黏滑电机**：两次脱离的区间必须逐个对上；档内跳变时刻要检得出来
  [4] **老算法在黏滑数据上必须给出明显错的数**（把坑钉死）
  [5] 存盘往返：`_save_record` 写出的文件读回来判读，结论必须一致
  [6] 不对称摩擦（+0.689 / −0.45）两侧必须判出不同的脱离力矩
  [7] 摩擦大到扫不出来 ⇒ 明说"一次脱离都没有"，而不是给个数
  [8] `stage()` 报中止 ⇒ `sweep()` 返回 None
  [9] **真机存档数据**：对 `registers/01/20260923-195137_kpsweep.json` 判读，
      必须复现 [0.640,0.689] / [0.676,0.706] / [0.645,0.694] / [0.564,0.594]
  [10] `_steps_of` 不累加浮点；`_validate_sweep` 在碰硬件之前就死

用法（在 DM_Armx 根目录）：
    pixi run python tools/smoke_joint_sweep.py
"""
import io
import json
import math
import sys
import tempfile
from contextlib import redirect_stdout
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "src" / "DMmotor_driver"))
sys.path.insert(0, str(REPO / "tools"))

import friction_report as R      # noqa: E402
import joint_jog as J            # noqa: E402

# 台架上那颗 4340P 的真实位置（上一轮实测），让测试数据和真机对得上
P0 = -1.0279
TAU_RES = 28.0 / 2047            # 力矩回读分辨率 ≈ 0.01368 N·m
# 真机上 kp=0 处两向读数都是 −0.0205 —— 传感器有一个恒定零偏。
# 假电机也加上，这样"零偏检查"那条路径每次都被走到。
OFFSET = -0.0205


def _quant(x, res=TAU_RES):
    """把力矩量化到回读分辨率 —— 真机就是这样跳的，测试要跟着跳。"""
    return round(x / res) * res


def make_stage(rule, *, q0=P0, hz=200.0, slip_at=0.3):
    """造一个假的 `stage()`，返回 `(stage, state)`。

    `rule(kp, err, q_des)` → 滑到哪个位置；返回 `None` 表示这一档不动。
    err 的符号就是方向，所以 `rule` 里可以按方向给不同的摩擦。

    跳变发生在每档的 `slip_at` 处（不是第 0 帧）—— 这样"档内跳变时刻"那条
    判读才有东西可测。平滑假电机也这么跳，否则它每帧都在终点，
    检不出"跳变发生在哪一刻"。
    """
    st = {"q": q0, "labels": []}

    def stage(label, secs, q_of_t, kp, note="", samples=None, quiet=False):
        st["labels"].append(label)
        q_des = q_of_t(0.0)
        q = st["q"]
        err = q_des - q
        n = max(int(secs * hz), 1)
        i_slip = max(1, int(slip_at * n))
        q_new = rule(kp, err, q_des)
        seq = [q] * n
        if q_new is not None:
            for i in range(i_slip, n):
                seq[i] = q_new
        st["q"] = seq[-1]
        if samples is not None:
            for i in range(n):
                tau = kp * (q_des - seq[i]) + OFFSET
                samples.append((i / hz, q_des, seq[i], _quant(tau)))
        return True

    return stage, st


# ── 两种假电机 ────────────────────────────────────────────────
# ⚠ 浮点边界：`9 × 0.05` 在 IEEE754 里 = 0.45000000000000007，**不等于** 0.45。
#   于是"恰好顶在静摩擦上"那一档会被判成"顶破了"，脱离点整整早一档，
#   凭空造出一个不存在的失败。真实电机的编码器有量化，本来就不会精确落在
#   边界上 —— 这个 epsilon 只是让假电机**符合它自己的数学模型**。
EPS = 1e-9


def smooth(fric):
    """**平滑**电机：顶破摩擦就滑到平衡位置，之后越滑越顺。

    这是上一版用的模型。它在脱离之后给出**真平台**，所以它测不出
    "把一条爬升曲线的中位数当平台"这个错 —— 留着它是为了对照。
    """
    def rule(kp, err, q_des):
        if kp <= 0 or abs(kp * err) <= fric * (1 + EPS) + EPS:
            return None
        return q_des - math.copysign(fric / kp, err)
    return rule


def stick_slip(theta_s, theta_k=None):
    """**黏滑**电机：两级摩擦。

    顶不破静摩擦 `theta_s` → 不动；
    顶破了 → 跳到 `kp·(q_des−q) = theta_k` 的新齿位上，那里动摩擦 `theta_k` 顶得住，
    于是**又卡住**，要等 kp 再涨上去顶破 `theta_s` 才跳第二次。

    就这一条规则复现了真机的全部特征：位置几个台阶、tau 一路爬升、跳变离散、
    脱离力矩和"平台中位数"差得远。
    """
    if theta_k is None:
        theta_k = theta_s * 0.605          # 由真机数据反推：0.689 → 0.417

    def rule(kp, err, q_des):
        if kp <= 0 or abs(kp * err) <= theta_s * (1 + EPS) + EPS:
            return None
        return q_des - math.copysign(theta_k / kp, err)
    return rule


def two_sided(rule_plus, rule_minus):
    return lambda kp, err, q_des: (rule_plus if err > 0 else rule_minus)(kp, err, q_des)


# ── 已经删掉的老算法，复刻一份用来钉坑 ────────────────────────
def old_plateau(steps, baseline, move_tol):
    """**复刻已经删掉的老判读**：取「脱离之后」所有档 tau 的中位数当静摩擦。

    留着它不是怀旧 —— 是让它每次都被喂一口黏滑数据，然后断言它算错。
    哪天有人把这个算法写回来，这一条会红。
    """
    mv = [s for s in steps if abs(s["q_med"] - baseline) > move_tol]
    return abs(R._median([s["tau_med"] for s in mv])) if mv else None


# ── 小工具 ────────────────────────────────────────────────────
def args(*extra):
    return J.build_parser().parse_args(
        ["--exp", "kpsweep", "--type", "DM4340", *extra])


def sweep_quiet(a, p0, stage):
    """跑一次采集，把打屏吞掉，返回 (记录, 打出来的文本)。"""
    buf = io.StringIO()
    with redirect_stdout(buf):
        rec = J.sweep(a, p0, stage)
    return rec, buf.getvalue()


def report_text(res):
    buf = io.StringIO()
    with redirect_stdout(buf):
        R.print_report(res)
    return buf.getvalue()


def events_of(res, sign):
    for r in res["sides"]:
        if r["sign"] == sign:
            return r["events"]
    return []


def intervals(evs):
    return [(round(e["tau_below"], 3), round(e["tau_above"], 3)) for e in evs]


def main() -> int:
    fails = []

    def check(cond, label, extra=""):
        print(f"    {'✓' if cond else '✗'} {label}" + (f"   {extra}" if extra else ""))
        if not cond:
            fails.append(label)

    # ── [1] 采集结构 ──────────────────────────────────────────────
    print("[1] 采集：sweep() 存下的记录要含逐帧样本、档数、基线")
    a1 = args("--dir", "both")
    stage1, _ = make_stage(stick_slip(0.689))
    rec1, _txt1 = sweep_quiet(a1, P0, stage1)
    check(rec1 is not None and len(rec1["sweeps"]) == 2, "两个方向都采到了")
    sw1 = rec1["sweeps"][0]
    check(len(sw1["steps"]) == 25, "25 档全在", f"实际 {len(sw1['steps'])}")
    check(all(len(s["samples"]) == int(a1.hold * a1.hz) for s in sw1["steps"]),
          "每档都存满了逐帧样本",
          f"每档 {len(sw1['steps'][0]['samples'])} 帧（{a1.hold:g}s × {a1.hz:g}Hz）")
    check(abs(sw1["baseline"] - P0) < 1e-6, "基线 = 实测停的位置",
          f"{sw1['baseline']:.6f} vs p0 {P0}")
    check(rec1["n_frames"] == 2 * 25 * int(a1.hold * a1.hz), "总帧数对得上",
          f"{rec1['n_frames']}")

    # ── [2] 平滑电机：下界必须精确还原已知摩擦 ────────────────────
    print("\n[2] 平滑电机：脱离力矩的**下界**必须精确还原已知摩擦")
    stage2, _ = make_stage(smooth(0.45))
    rec2, _ = sweep_quiet(args("--dir", "plus"), P0, stage2)
    res2 = R.analyze(rec2)
    ev2 = events_of(res2, 1)
    check(len(ev2) >= 5, "摩擦 0.45：脱离事件足够多", f"{len(ev2)} 次")
    los2 = [e["tau_below"] for e in ev2]
    check(all(abs(x - 0.45) < 1e-5 for x in los2),
          "摩擦 0.45：每次脱离的下界都 = 0.45",
          f"极差 {min(los2):.6f} … {max(los2):.6f}")

    # ★ 但**光测 0.45 是挡不住那个 bug 的** —— 这是变异测试逼出来的认识：
    #   下界若写成「上一个**没推动**的档的力矩」，0.45 这组照样全绿，
    #   因为脱离点在 kp=10，而 kp=9 的力矩 9×0.05 = 0.45 **碰巧就是正确答案**。
    #   真正暴露它的是摩擦**远小于一个 kp 档**的电机：
    #   顶破一次之后偏差就缩到 摩擦/kp，下一档要再顶破，位移只有
    #     0.02/(kp−1) − 0.02/kp = 0.02/(kp(kp−1))  →  kp=4 时已是 0.0017 < move_tol
    #   所以"推动"只发生在最前面几档（轴仍在蠕变，但每档太短，判不出「动了」）。
    #   下界写错的话，这几档的下界会全冻在 0 —— 这才是这个用例真正钉的东西。
    print("    摩擦 0.02（**远小于一个 kp 档** 0.05）⇒ 只有头几档位移够大：")
    stage2b, _ = make_stage(smooth(0.02))
    rec2b, _ = sweep_quiet(args("--dir", "plus"), P0, stage2b)
    res2b = R.analyze(rec2b)
    ev2b = events_of(res2b, 1)
    check(len(ev2b) >= 3, "头几档判得出「动了」", f"{len(ev2b)} 次脱离 / 25 档")
    check(ev2b[0]["tau_below"] == 0.0,
          "第一次脱离的下界是 0（kp=1 之前本来就没有力矩压着它）",
          f"[{ev2b[0]['tau_below']:.4f}, {ev2b[0]['tau_above']:.4f}]")
    los2b = [e["tau_below"] for e in ev2b[1:]]
    check(los2b and all(abs(x - 0.02) < 1e-5 for x in los2b),
          "此后每次的下界都 = 0.02（写错的话会全部退化成 0）",
          f"极差 {min(los2b):.6f} … {max(los2b):.6f}")

    # ── [3] 黏滑电机：区间逐个对上 + 档内跳变 ─────────────────────
    print("\n[3] 黏滑电机 θs=0.689 / θk=0.417 ⇒ 区间要逐个对上，且检得出档内跳变时刻")
    res3 = R.analyze(rec1)
    ev3 = events_of(res3, 1)
    check(len(ev3) == 2, "正好 2 次脱离", f"{len(ev3)} 次")
    got = intervals(ev3)
    want = [(0.650, 0.700), (0.685, 0.715)]
    check(got == want, "脱离力矩区间", f"判读 {got} vs 手算 {want}")
    s3 = [r for r in res3["sides"] if r["sign"] == 1][0]
    check(len(s3["plateaus"]) == 3,
          "轴只停过 3 处（25 档里）—— 跳着走的直接证据", f"{len(s3['plateaus'])} 处")
    w = ev3[0]["within"]
    check(w is not None, "档内跳变被检出（没有逐帧样本是做不到的）")
    if w:
        check(abs(w["t_jump"] - 0.15) < 0.02, "跳变时刻 ≈ 0.15s（假电机设在 30% 处）",
              f"{w['t_jump']:.3f}s")
        check(w["n_frames_before"] > 0, "跳变前确实有静止帧", f"{w['n_frames_before']} 帧")

    # ── [4] 老算法在黏滑数据上必须算错 ────────────────────────────
    print("\n[4] 【钉坑】老算法（取「脱离之后 tau 的中位数」）在黏滑数据上必须明显错")
    oldv = old_plateau(rec1["sweeps"][0]["steps"], P0, a1.move_tol)
    true_lo = ev3[0]["tau_below"]
    check(oldv is not None and oldv < true_lo - 0.1,
          "老算法给出的数与真实脱离力矩差了 > 0.1 N·m",
          f"老算法 {oldv:.4f} vs 真值下界 {true_lo:.4f}（差 {true_lo-oldv:.4f}）")
    check(oldv < 0.60,
          "老算法落在「单调爬升曲线的中位数」上，物理上没有意义",
          f"{oldv:.4f} —— 它既不是静摩擦也不是动摩擦，是一个爬升段的中点")
    # 同一个老算法喂平滑数据时**看着是对的** —— 正是这一点让上一版测试全绿而毫无察觉。
    # 它其实也错了，只是错得小：差的正好是传感器零偏 + 量化格。
    old_smooth = old_plateau(rec2["sweeps"][0]["steps"], P0, a1.move_tol)
    check(abs(old_smooth - 0.45) < 0.05,
          "老算法喂**平滑**数据时看着是对的（所以上一版测试全绿、没人察觉）",
          f"老算法 {old_smooth:.4f} vs 真值 0.4500 —— 差 {abs(old_smooth-0.45):.4f}，"
          f"正好是零偏 {OFFSET:+.4f} 加量化格 {TAU_RES:.4f}")

    # ── [5] 存盘往返 ──────────────────────────────────────────────
    print("\n[5] 存盘往返：_save_record 写出的文件读回来，判读结论必须一致")
    with tempfile.TemporaryDirectory() as td:
        a5 = args("--reg-dir", td)
        stage5, _ = make_stage(stick_slip(0.689))
        rec5, _ = sweep_quiet(a5, P0, stage5)
        fp = J._save_record(a5, (12.5, 10.0, 28.0), P0, rec5, {"sent": 1})
        check(fp is not None and fp.exists(), "文件写出来了",
              f"{fp.relative_to(td) if fp else '—'}")
        doc = json.loads(fp.read_text())
        check(doc["params"]["hold"] == a5.hold and doc["params"]["median_n"] == a5.median_n,
              "params 完整存下来了（判读要靠它）")
        check(doc["limit"] == [12.5, 10.0, 28.0], "limit 存下来了（算量化格要用）")
        check(doc["command"].split()[0] == sys.argv[0],
              "命令行存下来了（复跑时要靠它还原当时的口径）", doc["command"])
        check(intervals(events_of(R.analyze(doc), 1)) == want,
              "从文件读回来的判读结论与内存里一致")
        # --no-save 必须真的不写
        a5b = args("--reg-dir", td, "--no-save")
        check(J._save_record(a5b, (12.5, 10.0, 28.0), P0, rec5, {}) is None,
              "--no-save 时不写文件")

    # ── [6] 不对称摩擦 ────────────────────────────────────────────
    print("\n[6] 不对称摩擦（+0.689 / −0.45）两侧必须判出不同的脱离力矩")
    stage6, _ = make_stage(two_sided(stick_slip(0.689), stick_slip(0.45)))
    rec6, _ = sweep_quiet(args("--dir", "both"), P0, stage6)
    res6 = R.analyze(rec6)
    p6 = events_of(res6, 1)[0]["tau_below"]
    m6 = events_of(res6, -1)[0]["tau_below"]
    check(abs(p6 - 0.650) < 0.006, "+ 侧第一次脱离下界", f"{p6:.4f}")
    check(abs(m6 - 0.450) < 0.006, "− 侧第一次脱离下界", f"{m6:.4f}")
    check(p6 - m6 > 0.15, "两侧确实不同", f"差 {p6-m6:.4f} N·m")

    # ── [7] 摩擦大到扫不出来 ──────────────────────────────────────
    print("\n[7] 摩擦 5.0 N·m（> kp-max·E = 1.2）⇒ 必须说「一次脱离都没有」")
    stage7, _ = make_stage(stick_slip(5.0))
    rec7, _ = sweep_quiet(args("--dir", "plus"), P0, stage7)
    res7 = R.analyze(rec7)
    check(not events_of(res7, 1), "确实一次都没脱离")
    t7 = report_text(res7)
    check("一次脱离都没有" in t7, "打屏明说扫不出来")
    check("静摩擦（下界）" not in t7,
          "没有打印任何「静摩擦（下界）= …」的假数字（老版本的输出）")

    # ── [8] 中止要能传上去 ────────────────────────────────────────
    print("\n[8] `stage()` 报中止 ⇒ `sweep()` 必须返回 None")
    def aborting_stage(label, secs, q_of_t, kp, note="", samples=None, quiet=False):
        return False
    rec8, _ = sweep_quiet(args("--dir", "plus"), P0, aborting_stage)
    check(rec8 is None, "返回 None（调用方 `if not rec:` 能吃下）", f"实际 {rec8!r}")

    # ── [9] 真机存档数据 ──────────────────────────────────────────
    print("\n[9] 【真数据】判读 2026-09-23 那次真机运行，区间必须复现")
    real = REPO / "registers" / "01" / "20260923-195137_kpsweep.json"
    if not real.exists():
        print(f"    ⚠ 找不到 {real.relative_to(REPO)} —— 跳过（不记为失败）")
    else:
        res9 = R.analyze(json.loads(real.read_text()))
        got9 = intervals(events_of(res9, 1)) + intervals(events_of(res9, -1))
        want9 = [(0.640, 0.689), (0.676, 0.706), (0.645, 0.694), (0.564, 0.594)]
        check(got9 == want9, "四次脱离的区间逐位复现", f"{got9}")
        t9 = report_text(res9)
        check("没有逐帧样本" in t9, "明说这份老记录看不到档内细节")
        check("不成立" in t9, "明说「静摩擦 = X」在这里不成立（散布 25%）")

    # ── [10] 参数自检 ─────────────────────────────────────────────
    print("\n[10] `_steps_of` 不累加浮点；`_validate_sweep` 在碰硬件之前就死")
    s1 = J._steps_of(args("--kp-step", "0.1", "--kp-max", "1.0"))
    check(len(s1) == 11 and s1[-1] == 1.0, "0.1 步长扫到 1.0", f"{len(s1)} 档，末档 {s1[-1]}")
    s2 = J._steps_of(args("--kp-step", "0.3", "--kp-max", "3.0"))
    check(len(s2) == 11 and s2[-1] == 3.0, "0.3 步长扫到 3.0（累加会飘）",
          f"{len(s2)} 档，末档 {s2[-1]}")
    died = False
    buf = io.StringIO()
    try:
        with redirect_stdout(buf):
            J._validate_sweep(args("--kp-max", "100"))    # 100×0.05 = 5.0 ≥ 2.0
    except SystemExit as e:
        died = e.code not in (0, None)
    check(died, "kp-max 100 × e 0.05 = 5.0 ≥ tau-abort 2.0 时拒绝开跑")
    ok = True
    try:
        with redirect_stdout(buf):
            J._validate_sweep(args("--kp-max", "60", "--tau-abort", "4.0"))
    except SystemExit:
        ok = False
    check(ok, "合法组合（60×0.05=3.0 < 4.0）不被误拦")

    # ── 收尾 ──────────────────────────────────────────────────────
    print()
    if fails:
        print(f"FAIL（{len(fails)} 项）：")
        for f in fails:
            print(f"  - {f}")
        return 1
    print("PASS: 采集存逐帧、判读给出脱离力矩区间、黏滑能被认出来、"
          "老算法在黏滑数据上被证明是错的、存盘往返一致、真机存档区间逐位复现")
    return 0


if __name__ == "__main__":
    sys.exit(main())
