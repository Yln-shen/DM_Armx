#!/usr/bin/env python3
"""静摩擦扫描的**离线判读** —— 读 `joint_jog.py --exp kpsweep` 存下的原始数据。

## 为什么判读不放在工具里

2026-09-23 第一次真机跑，工具报「静摩擦（下界）= 0.5128 N·m」。**这个数是错的**，
而且错得看不出来：那 0.5128 是一条**单调爬升曲线的中位数**，不是平台的高度。
真实的脱离力矩是 0.64~0.71 N·m（四次跳变），文档里那个 0.144 更是差了 4.8 倍。

工具当时为什么没发现：它把逐帧样本采下来、算完中位数**就丢了**，判读也只跑那一次。
**判读错了没法复跑，也没法拿存档数据回归** —— 这才是根因，不是"判读写错了一个条件"。

所以现在分成两半：

    joint_jog.py        只负责**安全地采数据 + 存盘**（这是它唯一能在真机上验证的部分）
    friction_report.py  负责**判读**（能拿存档数据反复跑，能进回归测试，不用上电）

## 判什么

**① 脱离事件**（核心）。轴被摩擦卡住，力矩一路涨、位置纹丝不动，涨到某个值"啪"地跳走。
每一次跳变给出一个**区间** `[上一个没推动的力矩, 这一档推动了的力矩]`。

    ⚠️ 这两个数都是**指令值** `kp × 偏差`，**不经过力矩回读**。
       所以它们不受回读分辨率（TMAX/2047 ≈ 0.0137 N·m）限制，只受 kp 步长限制。

**② 零偏检查**。"没推动"的那些档里，电机收到的指令力矩就是 `kp × 偏差`，
而 `tau` 读数是同一帧的传感器输出 —— 两者**应该严格相等**。
残差就是传感器偏置。这个偏置必须查，因为它会把斜率判据整个带歪：

    模型 tau_meas = ±kp·E + c  ⇒  过原点拟合的斜率 = ±1 + c·(Σx/Σx²)

真机上 c ≈ −0.0205 N·m，把它算进去：正方向预测 0.9544（实测 0.9412），
负方向预测 −1.0456（实测 −1.0367）—— **偏置解释了 76~78% 的"偏离"**。
所以老版本那个"正方向 ⚠ / 负方向 ✓"两个都是假的：一个假警、一个碰巧。

**③ kp 线性检查**。**带截距**拟合（不是过原点 —— 过原点会把零偏算成斜率）。
报告斜率**及其标准误**，并把拟合残差和量化格比：残差已经贴着量化地板时，
"斜率≠1"这个结论没有数据支撑，不许报成 ⚠。

    ⚠️ 别想着"先减掉零偏再拟合" —— 带截距的模型里，把 y 整体平移一个常数，
       斜率**一点都不会变**（偏置全被截距吸收）。那是自欺欺人。

**④ 散布**。四次脱离力矩如果各差各的，那"静摩擦 = X"这个说法就**不成立** ——
这台关节是**黏滑**的，每次脱离的力矩取决于当时的齿面状态。报一个数就是撒谎。

## 输入格式

新格式（`joint_jog.py` 存盘）带逐帧样本 → 上面四项全能判，还多一项：
**档内跳变的时刻与时长**（分档扫描每档只 0.5s，跳变过程本身有没有被采到，全看这个）。

老格式（只有每档中位数，`registers/01/20260923-195137_kpsweep.json` 那种）
→ 只能做跨档判读，脚本会**明说自己看不到了什么**，不会假装。

用法（在 DM_Armx 根目录）：

    pixi run python tools/friction_report.py registers/01/<时间戳>_kpsweep.json
    pixi run python tools/friction_report.py registers/01/*.json      # 一次判多份
"""
import argparse
import json
import math
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]

DEFAULT_MOVE_TOL = 0.002

# 判定「这一帧确实离开了原位」时，要求后面连续几帧也都离开 ——
# 单个离群帧不算，否则编码器偶尔跳一格就会被当成一次脱离
SUSTAIN_N = 3


def _median(xs):
    """中位数。黏滑附近是"卡住 → 啪一下跳 → 再卡住"，均值会被那一下拽偏。"""
    s = sorted(xs)
    n = len(s)
    if n == 0:
        return float("nan")
    return s[n // 2] if n % 2 else 0.5 * (s[n // 2 - 1] + s[n // 2])


def _lsq(xs, ys):
    """带截距的最小二乘，返回 (斜率 a, 截距 b)。

    **故意不像老版本那样过原点**：过原点拟合会把一个恒定偏置全部算进斜率里，
    于是"传感器有零偏"看起来和"kp 不是线性放大器"一模一样。
    带截距拟合能把这两件事分开 —— a 管线性，b 管零偏。
    """
    n = len(xs)
    if n < 2:
        return float("nan"), float("nan")
    sx, sy = sum(xs), sum(ys)
    sxx = sum(x * x for x in xs)
    sxy = sum(x * y for x, y in zip(xs, ys))
    den = n * sxx - sx * sx
    if abs(den) < 1e-15:
        return float("nan"), float("nan")
    a = (n * sxy - sx * sy) / den
    return a, (sy - a * sx) / n


# ───────────────────────── 归一化 ─────────────────────────
def _steps_of(sw):
    """把一次扫描的档位序列取出来，新老两种格式都吃。

    返回 `(steps, has_frames)`。`has_frames=False` 时每个 step 的 `samples` 是 None。
    """
    if "steps" in sw:
        return sw["steps"], True
    out = []
    for r in sw.get("rows", []):
        out.append({"kp": r["kp"], "q_des": r["q_des"],
                    "q_med": r["q"], "tau_med": r["tau"], "samples": None})
    return out, False


def _rest(st, n_tail):
    """一档末尾的静止位置 / 力矩读数。

    有逐帧样本就用**末尾 n_tail 帧的中位数**（判读时才算，不再依赖工具当时的口径）；
    没有就退回工具存下的中位数。
    """
    s = st.get("samples")
    if s:
        tail = s[-min(n_tail, len(s)):]
        return _median([x[2] for x in tail]), _median([x[3] for x in tail])
    return st["q_med"], st["tau_med"]


def _jump_within(st, move_tol, n_tail):
    """档内的跳变时刻与时长。没有逐帧样本就返回 None。"""
    s = st.get("samples")
    if not s or len(s) < SUSTAIN_N:
        return None
    ts = [x[0] for x in s]
    qs = [x[2] for x in s]
    q0 = _median(qs[:min(n_tail, len(qs))])
    jump_i = None
    for i in range(len(qs)):
        if abs(qs[i] - q0) > move_tol and all(
                abs(qs[j] - q0) > move_tol
                for j in range(i, min(i + SUSTAIN_N, len(qs)))):
            jump_i = i
            break
    if jump_i is None:
        return None
    q_end = _median(qs[-min(n_tail, len(qs)):])
    # 跳完之后什么时候重新停稳（进入 q_end 的一个 move_tol 以内）
    rest_i = len(qs) - 1
    for j in range(jump_i, len(qs)):
        if abs(qs[j] - q_end) <= move_tol:
            rest_i = j
            break
    return {
        "t_jump": round(ts[jump_i], 4),
        "rise_s": round(ts[rest_i] - ts[jump_i], 4),
        "creep_rad": round(max(abs(q - q0) for q in qs[:jump_i]), 6) if jump_i else 0.0,
        "n_frames_before": jump_i,
    }


# ───────────────────────── 判读 ─────────────────────────
def analyze_side(sw, *, move_tol=DEFAULT_MOVE_TOL, n_tail=20, tau_res=28.0 / 2047):
    """判读一个方向。返回结构化的结果，**不打印**（打印在 `print_side`）。"""
    steps, has_frames = _steps_of(sw)
    base = sw["baseline"]
    sign = sw["sign"]

    for st in steps:
        st["q_rest"], st["tau_rest"] = _rest(st, n_tail)

    # ① 轴停过的位置（黏滑最直接的证据：力矩在涨、位置不动）
    plat = []
    for st in steps:
        if plat and abs(st["q_rest"] - plat[-1]["q"]) <= move_tol:
            plat[-1]["kp_to"] = st["kp"]
            plat[-1]["n"] += 1
        else:
            plat.append({"q": st["q_rest"], "kp_from": st["kp"],
                         "kp_to": st["kp"], "n": 1})

    # ② 脱离事件
    #
    #    下界 = **进入这一档时，把轴按在那儿的指令力矩**（上一档结束时的
    #    `kp × 偏差`，偏差按轴**实际停的位置**算）。这个定义对两种情况都成立：
    #      · 上一档没推动 → 就是上一档的 `kp × 偏差`
    #      · 上一档也在动 → 是它停下来那一刻的 `kp × 偏差`
    #    ⚠️ 不能写成「上一个没推动的档的力矩」：低摩擦的电机**每一档都在动**，
    #       那样下界会永远是 0，区间变成 [0, T] 毫无信息。这是本脚本的一个真 bug，
    #       `smoke_joint_sweep.py` 里「每档都在动」那组就是钉它的。
    events = []
    micro = []               # move_tol 以内的微移：脱离前的蠕变
    q_hold = base            # 进入这一档时轴停在哪
    t_hold = 0.0             # 进入这一档时，把它按在那儿的指令力矩
    prev_kp = None
    for st in steps:
        kp = st["kp"]
        err_hold = st["q_des"] - q_hold          # ★ 跳变**前**的偏差
        t_cmd = kp * err_hold
        d = st["q_rest"] - q_hold
        moved = abs(d) > move_tol
        if not moved and abs(d) > 0:
            micro.append({"kp": kp, "dq": d})
        if moved and kp > 0:
            events.append({
                "kp_from": prev_kp, "kp_to": kp,
                "err_before": err_hold,
                "tau_below": abs(t_hold),
                "tau_above": abs(t_cmd),
                "dq": d, "q_from": q_hold, "q_to": st["q_rest"],
                "within": _jump_within(st, move_tol, n_tail) if has_frames else None,
            })
        # 这一档结束时，把它按在**新位置**上的力矩（下一档的下界）
        t_hold = kp * (st["q_des"] - st["q_rest"])
        prev_kp = kp
        q_hold = st["q_rest"]

    # ③ 零偏检查：只取「没推动」的档 —— 此时 tau 读数应当严格等于 kp×偏差
    zero_move = []
    q_h = base
    for st in steps:
        if st["kp"] > 0 and abs(st["q_rest"] - q_h) <= move_tol:
            zero_move.append({"kp": st["kp"],
                              "tau_cmd": st["kp"] * (st["q_des"] - q_h),
                              "tau_meas": st["tau_rest"]})
        q_h = st["q_rest"]
    resid = [z["tau_meas"] - z["tau_cmd"] for z in zero_move]
    tau_at_kp0 = [st["tau_rest"] for st in steps if st["kp"] == 0.0]

    # ④ kp 线性：**带截距**拟合，并给出斜率的**标准误**。
    #
    #    为什么必须给标准误：力矩回读的量化格是 TAU_RES ≈ 0.0137 N·m。残差如果
    #    本来就落在量化格之内，那"斜率不等于 1"这个结论**根本没有数据支撑** ——
    #    它只是量化噪声。报一个 `1.0208 ⚠` 而不给不确定度，就是在制造假警觉，
    #    而假警报和漏报一样有害（教人忽略警告）。
    #
    #    ⚠️ 注意：**不能**靠"先减掉零偏再拟合"来修斜率 —— 模型里带截距的话，
    #    把 y 整体平移一个常数，斜率一点都不会变（偏置全被截距吸收）。
    #    唯一诚实的做法是把不确定度摆出来。
    fit = {}
    if len(zero_move) >= 3:
        xs = [z["tau_cmd"] for z in zero_move]
        ys = [z["tau_meas"] for z in zero_move]
        n = len(xs)
        a, b = _lsq(xs, ys)
        xbar = sum(xs) / n
        sxx = sum((x - xbar) ** 2 for x in xs)
        rr = [y - (a * x + b) for x, y in zip(xs, ys)]
        sd = math.sqrt(sum(r * r for r in rr) / max(n - 2, 1))
        se = sd / math.sqrt(sxx) if sxx > 0 else float("nan")
        fit = {"n": n, "slope": a, "intercept": b, "sd_resid": sd, "se_slope": se,
               "dev": abs(abs(a) - 1.0),
               # 残差散布 vs 量化格：小于一格说明拟合已经贴着量化地板了
               "at_quant_floor": sd <= tau_res / math.sqrt(12) * 2}

    return {
        "sign": sign, "baseline": base, "q_des": sw["q_des"],
        "has_frames": has_frames, "n_steps": len(steps),
        "plateaus": plat, "events": events, "micro": micro,
        "zero_offset_median": _median(resid) if resid else float("nan"),
        "zero_offset_range": (min(resid), max(resid)) if resid else (float("nan"),) * 2,
        "tau_at_kp0": tau_at_kp0,
        "fit": fit,
    }


def analyze(rec, *, move_tol=None, n_tail=None):
    """判读一整份存盘记录。返回结构化结果，**不打印**。"""
    p = rec.get("params", {})
    move_tol = DEFAULT_MOVE_TOL if move_tol is None else move_tol
    n_tail = p.get("median_n", 20) if n_tail is None else n_tail
    tau_res = rec.get("limit", [0, 0, 28.0])[2] / 2047.0
    sides = [analyze_side(sw, move_tol=move_tol, n_tail=n_tail, tau_res=tau_res)
             for sw in rec.get("sweeps", [])]
    return {"record": rec, "sides": sides,
            "move_tol": move_tol, "n_tail": n_tail, "tau_res": tau_res}


# ───────────────────────── 打印 ─────────────────────────
def print_side(r, tau_res):
    print(f"\n── 方向 {r['sign']:+d}（基准 {r['baseline']: .4f}，"
          f"q_des 钉在 {r['q_des']: .4f}）──")

    print(f" · 轴停过的位置（{r['n_steps']} 档里 {len(r['plateaus'])} 处 —— "
          "力矩一直在涨而位置纹丝不动，这就是黏滑）：")
    for g in r["plateaus"]:
        print(f"     q={g['q']: .4f}   kp {g['kp_from']:g}–{g['kp_to']:g}   {g['n']} 档")
    if r["micro"]:
        tot = sum(abs(m["dq"]) for m in r["micro"])
        where = "、".join(f"kp={m['kp']:g}({m['dq']:+.4f})" for m in r["micro"])
        print(f"     另有 {len(r['micro'])} 次 move_tol 以内的**微移**（合计 {tot:.4f} rad）："
              f"{where}")
        print("     —— 脱离之前轴在慢慢爬，这是黏滑的特征之一，不是噪声")

    if not r["events"]:
        print(" · **一次脱离都没有**：kp 加到最大，轴始终没动。"
              "\n   ⇒ 这个方向的静摩擦 **> 本次扫到的最大指令力矩**。"
              "把 --kp-max 调大再来（记得别让它碰到 --tau-abort）。")
    else:
        print(f" · 脱离事件 {len(r['events'])} 次"
              "（脱离力矩 = kp × **跳变前**的偏差，是指令值）：")
        for i, e in enumerate(r["events"], 1):
            kf = f"{e['kp_from']:g}" if e["kp_from"] is not None else "—"
            print(f"     #{i}  kp {kf}→{e['kp_to']:g}   起始偏差 {e['err_before']:+.4f}"
                  f"   跳 {e['dq']:+.4f} rad")
            print(f"         脱离力矩 ∈ [{e['tau_below']:.4f}, {e['tau_above']:.4f}] N·m"
                  f"   （区间宽 {e['tau_above']-e['tau_below']:.4f}"
                  "，由 kp 步长决定，**与力矩回读分辨率无关**）")
            if e["within"]:
                w = e["within"]
                print(f"         档内：第 {w['t_jump']:.3f}s 起跳"
                      f"（前 {w['n_frames_before']} 帧静止），"
                      f"用时 {w['rise_s']:.3f}s 停稳")
            elif not r["has_frames"]:
                print("         档内：**看不到** —— 这份记录没有逐帧样本")

    if math.isnan(r["zero_offset_median"]):
        print(" · 零偏检查：没有「没推动」的档可用（全程都在动）")
    else:
        lo, hi = r["zero_offset_range"]
        print(f" · 零偏检查（只取没推动的档：此时 tau 读数应当严格等于 kp×偏差）：")
        print(f"     残差 = tau读数 − kp×偏差：中位数 {r['zero_offset_median']:+.4f} N·m"
              f"（极差 {lo:+.4f} … {hi:+.4f}）")
        if r["tau_at_kp0"]:
            k0 = _median(r["tau_at_kp0"])
            ok = abs(k0 - r["zero_offset_median"]) < 2 * tau_res
            print(f"     kp=0 处读数 {k0:+.4f} —— "
                  + ("与残差一致 ⇒ **是恒定偏置**，不是非线性"
                     if ok else
                     "与残差不一致 ⇒ 分布不像恒定偏置，值得查（可能有温漂或迟滞）"))

    ft = r["fit"]
    if ft.get("n", 0) >= 3 and not math.isnan(ft["slope"]):
        n_sig = ft["dev"] / ft["se_slope"] if ft["se_slope"] > 0 else float("inf")
        print(f" · kp 线性检查（{ft['n']} 个「没推动」的点，**带截距**拟合）：")
        print(f"     斜率 {ft['slope']:+.4f} ± {ft['se_slope']:.4f}（应为 ±1.000）   "
              f"截距 {ft['intercept']:+.4f} N·m")
        print(f"     拟合残差标准差 {ft['sd_resid']:.5f} N·m"
              f"（量化格 {tau_res:.5f}）"
              + ("   ← 已经贴着量化地板" if ft["at_quant_floor"] else ""))
        if n_sig < 2:
            print(f"     偏离 1.000 只有 {n_sig:.1f} 个标准误 ⇒ **数据与「kp 完全线性」"
                  "无法区分**，不要在这一点上做文章")
        else:
            print(f"     ⚠ 偏离 1.000 有 {n_sig:.1f} 个标准误 —— "
                  "超出量化噪声能解释的范围，值得查（先怀疑回读的零偏不是常数）")
    elif ft.get("n", 0):
        print(f" · kp 线性检查：只有 {ft['n']} 个点，拟合不可靠 —— "
              "把 --kp-step 调小（例如 0.25）多留几个点")
    else:
        print(" · kp 线性检查：一档都没落在「没推动」上 —— 摩擦太小，"
              "把 --kp-step 调小（例如 0.25）才看得到那条直线")


def print_report(res):
    rec = res["record"]
    print("═" * 72)
    print(f"记录：{rec.get('saved_at', '?')}   {rec.get('motor_type', '?')} "
          f"0x{rec.get('motor_id', 0):02X}   port {rec.get('port', '?')}")
    print(f"命令：{rec.get('command', '?')}")
    p = rec.get("params", {})
    print(f"参数：e={p.get('e', '?')}  kp 0→{p.get('kp_max', '?')} "
          f"步长 {p.get('kp_step', '?')}  每档 {p.get('hold', '?')}s  "
          f"方向 {p.get('dir', '?')}")
    print(f"力矩回读分辨率：{res['tau_res']:.5f} N·m"
          "（= TMAX/2047 —— 脱离力矩不经过它，不受这个限制）")
    if res["sides"] and not res["sides"][0]["has_frames"]:
        print("⚠️ **这份记录没有逐帧样本**（老格式）：档内的跳变时刻、跳变时长、"
              "跳变前的蠕变都看不到。")
        print("   只能做跨档判读。新版本 `joint_jog.py` 会存逐帧数据。")

    for r in res["sides"]:
        print_side(r, res["tau_res"])

    ev = [e for r in res["sides"] for e in r["events"]]
    if len(res["sides"]) == 2:
        print("\n── 两方向对比 ──")
        for r in res["sides"]:
            sp = ("、".join(f"[{e['tau_below']:.3f}, {e['tau_above']:.3f}]"
                            for e in r["events"]) or "无")
            print(f"   方向 {r['sign']:+d}：{len(r['events'])} 次   {sp} N·m")
    if len(ev) >= 2:
        lo = min(e["tau_below"] for e in ev)
        hi = max(e["tau_above"] for e in ev)
        spread = hi - lo
        mid = sorted((e["tau_below"] + e["tau_above"]) / 2 for e in ev)
        print(f"\n · {len(ev)} 次脱离的力矩散布 {lo:.3f} – {hi:.3f} N·m"
              f"（极差 {spread:.3f}，是最小值的 {100*spread/lo:.0f}%）")
        if spread > 0.25 * lo:
            print("   ⇒ **「静摩擦 = X」这个说法在这里不成立。** 各次脱离力矩差得太多，"
                  "\n      说明它是**黏滑**：每次脱离的力矩取决于当时的齿面状态。"
                  "\n      报一个数就是撒谎 —— 要报就报这一串。")
        else:
            print("   ⇒ 各次脱离力矩接近，可以用一个数描述。")
    elif len(ev) == 1:
        print("\n · 只测到 1 次脱离，**不能**据此说静摩擦是多少 ——"
              "至少要有两次独立脱离才看得出它是不是常数。")
    elif res["sides"]:
        print("\n · 一次脱离都没测到 —— 静摩擦大于本次扫过的最大指令力矩，"
              "调大 --kp-max 再来。")
    print()


def _load(path):
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def build_parser():
    p = argparse.ArgumentParser(
        description="静摩擦扫描的离线判读（读 joint_jog.py --exp kpsweep 存下的 JSON）",
        formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("files", nargs="+", help="存盘记录（可给多份）")
    p.add_argument("--move-tol", type=float, default=None,
                   help=f"判定「轴动了」的位置阈值 rad，默认 {DEFAULT_MOVE_TOL}"
                        "（与采集时一致；在编码器量化噪声之上、在偏差 E 之下）")
    p.add_argument("--median-n", type=int, default=None,
                   help="每档取末尾多少帧算静止位置，默认用记录里的 params.median_n")
    return p


def main() -> int:
    a = build_parser().parse_args()
    bad = 0
    for fp in a.files:
        try:
            rec = _load(fp)
        except Exception as e:
            print(f"[错误] 读不了 {fp}：{e}", file=sys.stderr)
            bad += 1
            continue
        if "sweeps" not in rec:
            print(f"[错误] {fp} 里没有 sweeps —— 这不是 kpsweep 的存盘记录",
                  file=sys.stderr)
            bad += 1
            continue
        print(f"\n### {fp}")
        print_report(analyze(rec, move_tol=a.move_tol, n_tail=a.median_n))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
