#!/usr/bin/env python3
"""mujoco_pkg 冒烟测试：不依赖显示器，验证「模型跟手动」这条链路。

做三件事：
1. 解析包内模型/joint_map 路径（验证改名后的 share 路径与内联 URDF）；
2. 加载模型（默认 colored，与 real2sim.launch.py 默认一致；含 mesh 与夹爪双指）；
3. 合成一条 JointState 喂给 real2sim 的回调，再手动跑一次同步循环，
   确认关节目标被映射进 data.qpos 且 qpos 真的变了。

用法（在 DM_Armx 根目录）：
    pixi run bash -c "source install/setup.bash && python tools/smoke_sim.py"
    # 换模型： python tools/smoke_sim.py rebotarm_b601_stl.xml
"""
import sys

import numpy as np
import rclpy
from ament_index_python.packages import get_package_share_directory
from sensor_msgs.msg import JointState

from mujoco_pkg.real2sim_sync import MujocoReal2Sim


def main() -> int:
    share = get_package_share_directory("mujoco_pkg")
    # 默认与 real2sim.launch.py 一致：colored 模型 + kinematic joint_map（上游官方配对）
    model_file = sys.argv[1] if len(sys.argv) > 1 else "rebotarm_b601_colored.xml"
    model_path = f"{share}/models/{model_file}"
    joint_map = f"{share}/config/joint_map_kinematic.yaml"

    print(f"[1] package share : {share}")
    print(f"    canonical urdf: {MujocoReal2Sim._canonical_visual_urdf().is_file()}")
    print(f"    model          : {model_path}")

    rclpy.init(
        args=[
            "--ros-args",
            "-p", "open_viewer:=false",
            "-p", f"model_path:={model_path}",
            "-p", f"joint_map_file:={joint_map}",
        ]
    )
    node = MujocoReal2Sim()
    try:
        nmesh = int(node.model.nmesh)
        print(f"[2] model loaded  : nq={node.model.nq} nbody={node.model.nbody} nmesh={nmesh}")

        # 关节 + 夹爪各发一条命令；cmd 按关节名索引，便于和映射结果对照
        cmd = {f"joint{i}": v for i, v in enumerate([0.1, -0.2, 0.3, -0.4, 0.5, -0.6], start=1)}
        cmd["finger_left"] = 0.02
        msg = JointState()
        msg.name = list(cmd)
        msg.position = list(cmd.values())
        node._joint_state_callback(msg)
        print(f"[3] targets mapped: {len(node._target_qpos)} 个 qpos 目标")

        before = node.data.qpos.copy()
        node._sync_timer()          # 应用目标 + mj_forward
        after = node.data.qpos.copy()

        # 按每个关节自己的 qpos 地址逐一核对，而不是看 qpos[:6]（索引顺序≠关节顺序）
        ok = True
        print("    关节 -> qpos地址 / 命令值 -> 实际值")
        for name, targets in node.qpos_targets.items():
            for i, target in enumerate(targets):
                if name not in cmd:
                    continue  # 本轮没发这条命令
                want = target.clamp(cmd[name] * target.scale + target.offset)
                got = float(after[target.qpos_addr])
                hit = abs(got - want) < 1e-6
                ok &= hit
                label = name if len(targets) == 1 else f"{name}[{i}]"
                print(f"      {label:<12} addr={target.qpos_addr:<3} {want: .3f} -> {got: .3f}  {'✓' if hit else '✗'}")

        changed = not np.allclose(before, after)
        if not node._target_qpos or not changed or not ok:
            print("FAIL: 关节目标未按映射到位")
            return 1
        if nmesh == 0 and "colored" in model_file:
            print("FAIL: colored 模型一个 mesh 都没加载（外观会退化成几何体）")
            return 1
        print(f"PASS: {model_file} 可加载（{nmesh} 个 mesh）、各关节按映射跟动")
        return 0
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    sys.exit(main())
