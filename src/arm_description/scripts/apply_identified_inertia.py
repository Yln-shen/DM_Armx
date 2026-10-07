#!/usr/bin/env python3
"""把辨识出来的质量/质心应用到一份 URDF 上，生成"重力模型专用"的 URDF。

    apply_identified_inertia.py <输入.urdf> <gravity_identified.yaml> <输出.urdf> [--gripper]

只改每个 <link> 的 `<inertial>` 里的 **质量与质心位置**（`<origin xyz>`，rpy 保留）：
重力项只跟 (m, m·c) 有关，转动惯量对重力**没有贡献**，所以 `<inertia>` 原样保留 —— 也正因为
如此，生成出来的 URDF **只适合做重力前馈**，不要拿它做完整动力学。

⚠️ **固定关节的子连杆会被 pinocchio 合并进父连杆**（空载时：`end_link`、`gripper_tcp` 都并进
   `link6`）⇒ 辨识出来的"link6"其实是**合并后那个体**的参数。若只改 `link6`、不管那两个子连杆，
   pinocchio 会把它们**再加一遍**（重复计入），误差顺着链传到上游各关节
   —— 2026-10-05 实测：不改就 RMS 1.03，改完 0.42。

⚠️ **带夹爪时合并关系不同 ⇒ 清零名单也必须不同**（这就是 `--gripper` 开关的由来）：
   辨识是在**空载**真机上做的（joint6 上只有 link6 本体）⇒ `link6` 的辨识值 = 空载时那个合并体。
     - 空载（默认）：`end_link` / `gripper_tcp` 都清零 —— 它们已经被并进 `link6` 的辨识值里；
     - `--gripper`：`gripper_tcp` 仍清零（它只是 MoveIt 的末端参考点，没有质量），但
       **`end_link` 保留自己的惯量**（夹爪底座，上游 CAD 名义 0.5 kg）—— 夹爪部分**没有辨识过**，
       其质量/质心由 yaml 的 `gripper:` 段给（源头是上游 CAD，**不可信**，见那段注释）。
"""
import sys
import xml.etree.ElementTree as ET

import yaml


def zero_inertial(link):
    inertial = link.find("inertial")
    if inertial is None:
        return
    mass = inertial.find("mass")
    if mass is not None:
        mass.set("value", "0.0")
    origin = inertial.find("origin")
    if origin is not None:
        origin.set("xyz", "0 0 0")
    inertia = inertial.find("inertia")
    if inertia is not None:
        for k in ("ixx", "ixy", "ixz", "iyy", "iyz", "izz"):
            inertia.set(k, "0.0")


def write_inertial(link, spec):
    """把 mass/com 写进 link 的 <inertial>（没有就建一个）。"""
    inertial = link.find("inertial")
    if inertial is None:
        inertial = ET.SubElement(link, "inertial")
    mass = inertial.find("mass")
    if mass is None:
        mass = ET.SubElement(inertial, "mass")
    mass.set("value", "%.6f" % float(spec["mass"]))
    origin = inertial.find("origin")
    if origin is None:
        origin = ET.SubElement(inertial, "origin")
        origin.set("rpy", "0 0 0")
    origin.set("xyz", " ".join("%.6f" % float(v) for v in spec["com"]))


def main():
    argv = sys.argv[1:]
    use_gripper = "--gripper" in argv
    argv = [a for a in argv if a != "--gripper"]
    if len(argv) != 3:
        print(__doc__)
        return 2
    src, cfg_path, dst = argv
    with open(cfg_path, encoding="utf-8") as f:
        cfg = yaml.safe_load(f)
    links = dict(cfg["links"])
    if use_gripper:
        # 夹爪那部分没辨识过：用 yaml 里 gripper 段的名义值（上游 CAD，不可信）
        gripper = cfg.get("gripper") or {}
        for name, spec in (gripper.get("links") or {}).items():
            links[name] = spec

    tree = ET.parse(src)
    root = tree.getroot()

    # ① 要清零的固定子连杆
    #    - 空载：所有固定子连杆（它们的贡献已并进父连杆的辨识值）
    #    - 带夹爪：只清 `gripper_tcp`（末端参考点）；`end_link` 是夹爪底座，要按自己的惯量参与
    fixed_children = {
        j.find("child").get("link")
        for j in root.findall("joint") if j.get("type") == "fixed"
    }
    to_zero = fixed_children if not use_gripper else {"gripper_tcp"}

    # ② 把（辨识值 / 夹爪名义值）写进各 link
    changed = []
    for link in root.findall("link"):
        name = link.get("name")
        if name not in links:
            continue
        if link.find("inertial") is None:
            print(f"⚠️ {name} 没有 <inertial>，新建一个", file=sys.stderr)
        write_inertial(link, links[name])
        changed.append(name)

    for link in root.findall("link"):
        if link.get("name") in to_zero:
            zero_inertial(link)

    missing = [n for n in links if n not in changed]
    if missing:
        print(f"⚠️ yaml 里有、URDF 里没改到的 link: {missing}", file=sys.stderr)
    tree.write(dst, encoding="utf-8", xml_declaration=True)
    print(f"已写入 {len(changed)} 个 link 的质量/质心: {' '.join(sorted(changed))}")
    print(f"已清零 {len(to_zero)} 个固定子连杆的惯量: {' '.join(sorted(to_zero))}")
    if use_gripper:
        print("⚠️ 带夹爪：夹爪那部分的质量/质心来自上游 CAD、**没有辨识过** —— "
              "只当占位，别信它的绝对精度（真装夹爪后要重新辨识）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
