"""一键拉起「关节滑块 GUI + MuJoCo real2sim」。

数据流（全在 ROS 2 话题上，无进程内直连）：
    joint_slider_gui --(默认 QoS, 30Hz)--> /rebotarm/joint_states --> real2sim_sync --> MuJoCo viewer

注意：这里 model_path / joint_map_file 的默认值必须与 real2sim.launch.py 保持一致，
改动时两个文件一起改（本 launch 只是把两个 launch 拼起来，不引入新默认）。
"""
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, IncludeLaunchDescription
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    mujoco_share = FindPackageShare("mujoco_pkg")

    model_path = LaunchConfiguration("model_path")
    joint_map_file = LaunchConfiguration("joint_map_file")
    open_viewer = LaunchConfiguration("open_viewer")
    publish_hz = LaunchConfiguration("publish_hz")

    real2sim = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(
            PathJoinSubstitution([mujoco_share, "launch", "real2sim.launch.py"])
        ),
        launch_arguments={
            "model_path": model_path,
            "joint_map_file": joint_map_file,
            "open_viewer": open_viewer,
        }.items(),
    )
    slider = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(
            PathJoinSubstitution([mujoco_share, "launch", "joint_slider_gui.launch.py"])
        ),
        launch_arguments={"publish_hz": publish_hz}.items(),
    )

    return LaunchDescription(
        [
            DeclareLaunchArgument(
                "model_path",
                default_value=PathJoinSubstitution(
                    [mujoco_share, "models", "rebotarm_b601_colored.xml"]
                ),
            ),
            DeclareLaunchArgument(
                "joint_map_file",
                default_value=PathJoinSubstitution(
                    [mujoco_share, "config", "joint_map_kinematic.yaml"]
                ),
            ),
            DeclareLaunchArgument("open_viewer", default_value="true"),
            DeclareLaunchArgument("publish_hz", default_value="30.0"),
            real2sim,
            slider,
        ]
    )
