#!/usr/bin/env python3
"""从**真源**生成模型坐标下的关节限位头文件，给应用层（C++）用。

为什么需要它（2026-10-07）：
  `arm_controller`(JTC) **不校验关节限位** —— 越界目标照样执行，臂会顶在机械硬限位上
  **堵转**到超时（真机实测：joint2 给 +99 rad ⇒ 堵转 60 s、`effort` +22.5 N·m、线圈 62 ℃、
  且**全程 err=0 没有任何故障码**）。⇒ 应用层发目标前**必须自己查限位**。
  但限位原本只存在于 `joint.yaml`（关节侧）+ `align.yaml`（模型对齐）里，经 xacro 变成
  `<param>` 传给**插件**；应用层是独立包、拿不到那条链路。

真源（本脚本只读，不写回）：
  --joints  src/motor_driver/config/joint.yaml        每个关节的 position_min/max（**关节侧**）
  --align   src/arm_description/config/align.yaml      sign / zero_shift
换算（与 xacro 里的注释同一套）：
  q_urdf = sign * q_ours + zero_shift
  ⇒ 模型限位 = sort(sign * 关节侧限位 + zero_shift)

用法：
  gen_joint_limits_header.py --joints <joint.yaml> --align <align.yaml> --out <header.hpp>
                             [--verify-urdf <arm.urdf>]
"""
from __future__ import annotations

import argparse
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

try:
    import yaml
except ImportError:                                     # pragma: no cover
    sys.exit("需要 PyYAML（pixi 环境里有：import yaml）")


def load_sources(joints_path: Path, align_path: Path):
    """返回 [{name, sign, zero_shift, qmin, qmax, mlo, mhi}]，顺序按 joint.yaml。"""
    with joints_path.open(encoding="utf-8") as fh:
        joint_doc = yaml.safe_load(fh)
    with align_path.open(encoding="utf-8") as fh:
        align_doc = yaml.safe_load(fh)

    joint_cfg = joint_doc.get("joints") or {}
    align_cfg = align_doc.get("joints") or {}
    if not joint_cfg:
        sys.exit(f"{joints_path} 里没有 joints 段")
    if set(joint_cfg) != set(align_cfg):
        only_j = sorted(set(joint_cfg) - set(align_cfg))
        only_a = sorted(set(align_cfg) - set(joint_cfg))
        sys.exit(f"两个真源的关节名对不上：只在 joint.yaml={only_j}，只在 align.yaml={only_a}")

    out = []
    for name, jc in joint_cfg.items():
        ac = align_cfg[name]
        qmin, qmax = float(jc["position_min"]), float(jc["position_max"])
        sign, zs = float(ac["sign"]), float(ac["zero_shift"])
        a, b = sign * qmin + zs, sign * qmax + zs
        out.append({
            "name": name, "sign": sign, "zero_shift": zs,
            "qmin": qmin, "qmax": qmax, "mlo": min(a, b), "mhi": max(a, b),
        })
    return out


def verify_against_urdf(rows, urdf_path: Path, tol: float = 1e-6):
    """把头文件将要说的话与 URDF 实际值对一遍（同一真源，不该有差异）。"""
    root = ET.parse(urdf_path).getroot()
    urdf = {}
    for j in root.findall("joint"):
        lim = j.find("limit")
        if lim is not None and lim.get("lower") is not None:
            urdf[j.get("name")] = (float(lim.get("lower")), float(lim.get("upper")))
    bad = []
    for r in rows:
        got = urdf.get(r["name"])
        if got is None:
            bad.append(f"{r['name']}: URDF 里没有这个关节")
            continue
        if abs(got[0] - r["mlo"]) > tol or abs(got[1] - r["mhi"]) > tol:
            bad.append(
                f"{r['name']}: 换算 [{r['mlo']:+.6f},{r['mhi']:+.6f}] "
                f"≠ URDF [{got[0]:+.6f},{got[1]:+.6f}]")
    if bad:
        sys.exit("限位换算与 URDF 不一致（说明 xacro 的硬编码字面量或真源漂了）：\n  " +
                 "\n  ".join(bad))


def render(rows, joints_path: Path, align_path: Path) -> str:
    names = ", ".join(f'"{r["name"]}"' for r in rows)
    # ⚠️ 这些行以 `// 注释` 结尾 ⇒ **闭合括号必须另起一行**，否则会被吃进注释里
    #    （第一版写成 `... // joint6}};` ⇒ g++ 报 expected '}' —— 找了半天）
    lim = "\n".join(f"  {{{r['mlo']:+.9f}, {r['mhi']:+.9f}}},   // {r['name']}"
                    for r in rows)
    ours = "\n".join(f"  {{{r['qmin']:+.9f}, {r['qmax']:+.9f}}},   // {r['name']}（关节侧，"
                     f"sign={r['sign']:+.0f}, zero_shift={r['zero_shift']:+.6f}）"
                     for r in rows)
    n = len(rows)
    # 生成用**普通字符串 + replace**：这段代码里 `{` 很多（std::array 的初始化器），
    # 用 f-string 得写四层转义、极易写错（第一版就写错成单括号了）。
    tpl = """// **自动生成 —— 不要手改**（改真源后重新构建即可）
//
// 生成器：src/arm_description/scripts/gen_joint_limits_header.py
// 真源：  @JOINTS@
//         @ALIGN@
// 换算：  q_urdf = sign * q_ours + zero_shift ⇒ 模型限位 = sort(sign*q + zero_shift)
//
// 为什么要有这个文件：`arm_controller`(JTC) **不校验关节限位** —— 越界目标照执行，
// 臂会顶在机械硬限位上**堵转**到超时（真机实测 joint2 +99 rad ⇒ 堵转 60 s、
// effort 22.5 N·m、线圈 62 ℃，且全程 err=0 无故障码）。
// ⇒ 应用层**发目标前必须自己查限位**，而限位只有 joint.yaml + align.yaml 两处真源。
#pragma once

#include <array>
#include <cstddef>

namespace arm_limits {

// 关节顺序与 joint.yaml / align.yaml / URDF 一致
inline constexpr std::array<const char *, @N@> kJointNames = {{
  @NAMES@}};

// **模型坐标**（ros2_control / URDF / MoveIt 用的那一套）的关节限位
inline constexpr std::array<std::array<double, 2>, @N@> kModelLimits = {{
@LIM@
}};

// 上面换回**关节侧**（软限位；电机的 direction/offset 那一层在插件里）—— 供排查换算用
inline constexpr std::array<std::array<double, 2>, @N@> kJointSideLimits = {{
@OURS@
}};

// 检查一组**模型坐标**目标是否全部落在限位内。
// 返回 true = 合法；false 时把第一个越界的关节写进 bad_index / bad_name。
inline bool PoseWithinLimits(const double * q, std::size_t n,
                             std::size_t * bad_index = nullptr,
                             const char ** bad_name = nullptr)
{
  if (n != kJointNames.size()) {
    if (bad_index) {*bad_index = n;}
    if (bad_name) {*bad_name = nullptr;}
    return false;
  }
  for (std::size_t i = 0; i < n; ++i) {
    if (!(q[i] >= kModelLimits[i][0] && q[i] <= kModelLimits[i][1])) {
      if (bad_index) {*bad_index = i;}
      if (bad_name) {*bad_name = kJointNames[i];}
      return false;
    }
  }
  return true;
}

}  // namespace arm_limits
"""
    return (tpl.replace("@JOINTS@", str(joints_path))
               .replace("@ALIGN@", str(align_path))
               .replace("@N@", str(n))
               .replace("@NAMES@", names)
               .replace("@LIM@", lim)
               .replace("@OURS@", ours))


def main() -> int:
    ap = argparse.ArgumentParser(description="生成模型坐标关节限位头文件")
    ap.add_argument("--joints", required=True, type=Path)
    ap.add_argument("--align", required=True, type=Path)
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--verify-urdf", type=Path, default=None,
                    help="可选：与这份 URDF 的 <limit> 逐条对拍，不一致就失败")
    args = ap.parse_args()

    rows = load_sources(args.joints, args.align)
    if args.verify_urdf is not None:
        verify_against_urdf(rows, args.verify_urdf)

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(render(rows, args.joints, args.align), encoding="utf-8")
    print(f"生成 {args.out}（{len(rows)} 关节）" +
          ("，已与 URDF 对拍一致" if args.verify_urdf is not None else ""))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
