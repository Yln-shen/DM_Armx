#!/usr/bin/env python3
"""把辨识出来的质量/质心应用到一份 URDF 上，生成"重力模型专用"的 URDF。

    apply_identified_inertia.py <输入.urdf> <gravity_identified.yaml> <输出.urdf>

只改每个 <link> 的 `<inertial>` 里的 **质量与质心位置**（`<origin xyz>`，rpy 保留）：
重力项只跟 (m, m·c) 有关，转动惯量对重力**没有贡献**，所以 `<inertia>` 原样保留 —— 也正因为
如此，生成出来的 URDF **只适合做重力前馈**，不要拿它做完整动力学。

⚠️ **固定关节的子连杆会被 pinocchio 合并进父连杆**（本工程：`end_link`、`gripper_tcp` 都并进
   `link6`）⇒ 辨识出来的"link6"其实是**合并后那个体**的参数。若只改 `link6`、不管那两个子连杆，
   pinocchio 会把它们**再加一遍**（重复计入），误差顺着链传到上游各关节
   —— 2026-10-05 实测：不改就 RMS 1.03，改完 0.42。
   所以这里**把固定关节子连杆的惯量清零**，让每个 pinocchio body 的惯量严格等于辨识值。
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


def main():
    if len(sys.argv) != 4:
        print(__doc__)
        return 2
    src, cfg_path, dst = sys.argv[1:4]
    with open(cfg_path, encoding="utf-8") as f:
        cfg = yaml.safe_load(f)
    links = cfg["links"]

    tree = ET.parse(src)
    root = tree.getroot()

    # ① 固定关节的子连杆：惯量清零（它们的贡献已经被合并进父连杆的辨识值里了）
    fixed_children = {
        j.find("child").get("link")
        for j in root.findall("joint") if j.get("type") == "fixed"
    }

    # ② 把辨识值写进各 link
    changed = []
    for link in root.findall("link"):
        name = link.get("name")
        if name not in links:
            continue
        spec = links[name]
        inertial = link.find("inertial")
        if inertial is None:
            print(f"⚠️ {name} 没有 <inertial>，跳过", file=sys.stderr)
            continue
        mass = inertial.find("mass")
        if mass is None:
            mass = ET.SubElement(inertial, "mass")
        mass.set("value", "%.6f" % float(spec["mass"]))
        origin = inertial.find("origin")
        if origin is None:
            origin = ET.SubElement(inertial, "origin")
            origin.set("rpy", "0 0 0")
        origin.set("xyz", " ".join("%.6f" % float(v) for v in spec["com"]))
        changed.append(name)

    for link in root.findall("link"):
        if link.get("name") in fixed_children:
            zero_inertial(link)

    missing = [n for n in links if n not in changed]
    if missing:
        print(f"⚠️ yaml 里有、URDF 里没改到的 link: {missing}", file=sys.stderr)
    tree.write(dst, encoding="utf-8", xml_declaration=True)
    print(f"已写入 {len(changed)} 个 link 的辨识质量/质心: {' '.join(changed)}")
    print(f"已清零 {len(fixed_children)} 个固定子连杆的惯量: {' '.join(sorted(fixed_children))}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
