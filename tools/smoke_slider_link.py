#!/usr/bin/env python3
"""验证「滑块 GUI → real2sim」这条真·DDS 链路（不起 tkinter 界面）。

为什么要单独测：`joint_slider_gui` 用**默认 QoS**（RELIABLE, depth 10, volatile）发布
`/rebotarm/joint_states`，而 `real2sim_sync` 用 `qos_profile_sensor_data`（BEST_EFFORT）订阅。
两者能否配上属于 DDS 的 QoS 兼容性规则，**直接调 `_joint_state_callback` 是测不出来的**
（smoke_sim.py 走的就是直接调回调那条路）。本脚本用与滑块**完全相同的 QoS 组合**发真消息，
再看 real2sim 是否收到并写进目标、模型是否跟着走。

用法（在 DM_Armx 根目录）：
    pixi run bash -c "source install/setup.bash && python tools/smoke_slider_link.py"
"""
import sys
import time

import numpy as np
import rclpy
from ament_index_python.packages import get_package_share_directory
from sensor_msgs.msg import JointState

from mujoco_pkg.real2sim_sync import MujocoReal2Sim

# 取值都落在各关节限位内（joint2/joint3 只有负半轴、夹爪 0~0.05），避免 clamp 干扰比对
CMD = dict(zip([f"joint{i}" for i in range(1, 7)], [0.2, -0.3, -0.4, 0.3, 0.4, -0.5]))
CMD["finger_left"] = 0.03


def main() -> int:
    share = get_package_share_directory("mujoco_pkg")

    rclpy.init(
        args=[
            "--ros-args",
            "-p", "open_viewer:=false",
            "-p", f"model_path:={share}/models/rebotarm_b601_colored.xml",
            "-p", f"joint_map_file:={share}/config/joint_map_kinematic.yaml",
        ]
    )
    node = MujocoReal2Sim()
    # 独立的发布者节点，复刻 joint_slider_gui 的发布方式：默认 QoS + 同名话题 + 7 个关节
    pub_node = rclpy.create_node("slider_stub")
    pub = pub_node.create_publisher(JointState, "/rebotarm/joint_states", 10)

    msg = JointState()
    msg.name = list(CMD)
    msg.position = [float(v) for v in CMD.values()]

    try:
        # [1] 真实 DDS 往返：默认 QoS 的发布者 -> sensor_data QoS 的订阅者
        deadline = time.monotonic() + 5.0
        while time.monotonic() < deadline and node._last_msg_monotonic is None:
            msg.header.stamp = pub_node.get_clock().now().to_msg()
            pub.publish(msg)
            rclpy.spin_once(pub_node, timeout_sec=0.0)
            rclpy.spin_once(node, timeout_sec=0.05)

        if node._last_msg_monotonic is None:
            print("FAIL: real2sim 5 秒内没收到默认 QoS 发布者的消息（QoS 未配上？）")
            return 1
        print("[1] QoS 配对 OK：默认 QoS 发布者 -> sensor_data 订阅者，消息已到达")
        print(f"    收到 {len(node._target_qpos)} 个 qpos 目标")

        # [2] 收到之后，同步一次，模型是否真的走到命令位置
        before = node.data.qpos.copy()
        node._sync_timer()
        after = node.data.qpos.copy()

        ok = True
        print("    关节 -> qpos地址 / 命令值 -> 实际值")
        for name, targets in node.qpos_targets.items():
            for i, target in enumerate(targets):
                if name not in CMD:
                    continue
                want = target.clamp(CMD[name] * target.scale + target.offset)
                got = float(after[target.qpos_addr])
                hit = abs(got - want) < 1e-6
                ok &= hit
                label = name if len(targets) == 1 else f"{name}[{i}]"
                print(
                    f"      {label:<12} addr={target.qpos_addr:<3} "
                    f"{want: .3f} -> {got: .3f}  {'✓' if hit else '✗'}"
                )

        if not ok or np.allclose(before, after):
            print("FAIL: 模型未按滑块命令跟动")
            return 1
        print("PASS: 滑块 -> /rebotarm/joint_states -> real2sim -> 模型跟动，全链路通")
        return 0
    finally:
        pub_node.destroy_node()
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    sys.exit(main())
