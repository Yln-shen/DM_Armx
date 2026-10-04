"""real_joint_states —— 只读把真机 6 个关节角映射成 `/joint_states`（M1b 模型对齐用）。

## 它做什么

按 `q_urdf = sign * q_ours + zero_shift` 把驱动层读到的关节角换算成**模型坐标**，
定时发 `sensor_msgs/JointState`，给 `robot_state_publisher` / RViz 用。
目的：手推真机时让 RViz 里的模型**实时镜像**真机，从而逐关节定出 `sign`（±1）与
`zero_shift`（模型零位 ↔ 我们的零位之差）。

## 它**不**做什么（安全边界，改之前先读）

- **绝不 `enable()`**、绝不发电机侧命令帧；每轮只发 6 条 `0x7FF` **刷新帧**（纯读）+ `poll()` 收帧。
- 退出走 `DmArm.shutdown()`（发一次失能帧 + 关串口）—— 电机本来就没使能，这只是收尾。
- 不做限位/钳位等任何加工；只做上面那一个仿射换算。

## 串口掉了不装死

连续 `max_fail_streak` 轮（默认 20，@20Hz 就是 1 秒）读失败 ⇒ 打 FATAL 说明原因并**退出**。
否则模型会"冻在最后一帧"，看起来像"调参没生效" —— **2026-10-04 真踩过**：
适配器设备号从 `ttyACM0` 变成 `ttyACM1`，节点在刷 `读状态失败`，模型却一直不动。

## 现场调参（不用重启）

参数默认值来自 `arm_description` 的 `config/align.yaml`（**单一真源**，M1b 实测值），
所以重启节点不会丢掉对齐结果；要临时试别的值：

    ros2 param set /real_joint_states joint3.sign -1.0
    ros2 param set /real_joint_states joint2.zero_shift 1.5708
    ros2 param get /real_joint_states joint2.zero_shift
"""
from __future__ import annotations

import pathlib
import sys

import rclpy
import yaml
from ament_index_python.packages import get_package_share_directory
from rclpy.node import Node
from rclpy.qos import qos_profile_sensor_data
from sensor_msgs.msg import JointState

# 驱动层是"裸 import"风格（`arm.py` 里写的是 `from arm_config import ...`），
# 所以先把 motor_driver 的**模块目录**塞进 sys.path 再 import —— 与 `dm_registers.py` 同一做法。
import motor_driver                                                          # noqa: E402
sys.path.insert(0, str(pathlib.Path(motor_driver.__file__).resolve().parent))
from arm import DmArm                                                        # noqa: E402
from arm_config import load_arm_config                                       # noqa: E402


class RealJointStates(Node):
    """只读发布 `/joint_states`（模型坐标）。**不会让电机出力。**"""

    def __init__(self) -> None:
        super().__init__("real_joint_states")

        default_cfg = str(pathlib.Path(get_package_share_directory("motor_driver"))
                          / "config" / "joint.yaml")
        self.declare_parameter("config_file", default_cfg)
        self.declare_parameter("rate_hz", 20.0)
        self.declare_parameter("publish_velocity", True)
        self.declare_parameter("max_fail_streak", 20)   # 连续读失败多少轮就退出（20 轮 @20Hz = 1 秒）

        self._cfg = load_arm_config(self.get_parameter("config_file").value)

        # 每个关节两个参数；默认值取自 arm_description 的 config/align.yaml（**单一真源**）
        align: dict = {}
        try:
            align_path = (pathlib.Path(get_package_share_directory("arm_description"))
                          / "config" / "align.yaml")
            align = yaml.safe_load(align_path.read_text(encoding="utf-8")).get("joints", {})
            self.get_logger().info(f"模型对齐默认值来自 {align_path}")
        except Exception as e:                       # noqa: BLE001 —— 读不到也要能起，退回中性值
            self.get_logger().warn(
                f"没读到模型对齐配置（{type(e).__name__}: {e}）⇒ sign=+1 / zero_shift=0"
                "（这样模型大概率与真机对不上，需要现场重调）")

        for name in self._cfg.joints:
            a = align.get(name, {})
            self.declare_parameter(f"{name}.sign", float(a.get("sign", 1.0)))
            self.declare_parameter(f"{name}.zero_shift", float(a.get("zero_shift", 0.0)))

        self._missing_prev: set[str] = set()
        self._fail_streak = 0
        self._pub = self.create_publisher(JointState, "joint_states", qos_profile_sensor_data)

        try:
            self._arm = DmArm(self._cfg).connect()
        except Exception as e:                                   # noqa: BLE001 —— 要把原因讲清楚
            self.get_logger().fatal(
                f"打开串口/建关节失败：{type(e).__name__}: {e}\n"
                f"  适配器插好了吗（{self._cfg.channel}）？在 dialout 组里吗？被别的程序占着吗？")
            raise

        rate = float(self.get_parameter("rate_hz").value)
        if rate <= 0.0:
            raise ValueError(f"rate_hz 必须 > 0，收到 {rate}")
        self.create_timer(1.0 / rate, self._tick)

        self.get_logger().info(
            f"只读模式：{self._cfg.channel} @ {rate:g} Hz，**不使能电机**。"
            "调参：ros2 param set /real_joint_states <关节>.sign|zero_shift <值>")

    def _tick(self) -> None:
        try:
            self._arm.refresh_all_states()          # 只读：6 条 0x7FF + poll 一次
            states = self._arm.get_state()          # 只含"回过包"的关节
        except Exception as e:                       # noqa: BLE001 —— 偶发抖动容忍，但**不装死**
            self._fail_streak += 1
            limit = int(self.get_parameter("max_fail_streak").value)
            self.get_logger().error(
                f"读状态失败（连续 {self._fail_streak}/{limit}）：{type(e).__name__}: {e}")
            if self._fail_streak >= limit:
                self.get_logger().fatal(
                    f"连续 {limit} 轮读不到电机（{self._cfg.channel}）——适配器被拔了？"
                    "设备号变了（ttyACM0/ACM1 会随插拔顺序变）？被别的进程占了？"
                    "节点主动退出，避免\"模型冻住却不知道\"。")
                raise SystemExit(1)
            return

        self._fail_streak = 0
        msg = JointState()
        msg.header.stamp = self.get_clock().now().to_msg()
        want_vel = bool(self.get_parameter("publish_velocity").value)
        for name, js in states.items():
            sign = float(self.get_parameter(f"{name}.sign").value)
            shift = float(self.get_parameter(f"{name}.zero_shift").value)
            msg.name.append(name)
            msg.position.append(sign * js.position + shift)
            if want_vel:
                msg.velocity.append(sign * js.velocity)   # 速度是矢量：只乘 sign，不加 zero_shift
        self._pub.publish(msg)

        missing = set(self._cfg.joints) - set(states)
        if missing != self._missing_prev:
            if missing:
                self.get_logger().warn(f"这些关节这轮没回包（不编造）：{sorted(missing)}")
            self._missing_prev = missing

    def destroy_node(self) -> bool:
        try:
            self._arm.shutdown()                    # 先失能（本就没使能）再关串口
        finally:
            return super().destroy_node()


def main(args=None) -> None:
    rclpy.init(args=args)
    node: RealJointStates | None = None
    try:
        node = RealJointStates()
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        if node is not None:
            node.destroy_node()
        if rclpy.ok():
            rclpy.shutdown()


if __name__ == "__main__":
    main()
