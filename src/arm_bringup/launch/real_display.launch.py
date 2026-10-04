"""M1b：真机（**只读**）→ `/joint_states` → RViz 里的模型实时镜像真机。

  ros2 launch arm_bringup real_display.launch.py
  ros2 launch arm_bringup real_display.launch.py use_rviz:=false      # 只发状态不看图

⚠️ 这个 launch **不会使能电机**（`real_joint_states` 节点只发 0x7FF 刷新帧）。
   手推真机前请先托住 j2/j3（失能状态下它们会因重力下坠）。

现场调参（不用重启）：
  ros2 param set /real_joint_states joint3.sign -1.0
  ros2 param set /real_joint_states joint2.zero_shift 1.5708
"""
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.conditions import IfCondition
from launch.substitutions import Command, LaunchConfiguration, PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.parameter_descriptions import ParameterValue
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    use_rviz = DeclareLaunchArgument(
        "use_rviz", default_value="true", description="是否启动 rviz2")
    use_gripper = DeclareLaunchArgument(
        "use_gripper", default_value="false", description="模型是否带夹爪几何")
    rate_hz = DeclareLaunchArgument(
        "rate_hz", default_value="20.0", description="只读轮询 + 发布频率（Hz）")

    xacro_file = PathJoinSubstitution(
        [FindPackageShare("arm_description"), "urdf", "arm.urdf.xacro"])
    rviz_config = PathJoinSubstitution(
        [FindPackageShare("arm_description"), "rviz", "display.rviz"])

    robot_description = ParameterValue(
        Command(["xacro ", xacro_file, " use_gripper:=", LaunchConfiguration("use_gripper")]),
        value_type=str)

    return LaunchDescription([
        use_rviz,
        use_gripper,
        rate_hz,
        Node(
            package="arm_bringup",
            executable="real-joint-states",
            parameters=[{"rate_hz": LaunchConfiguration("rate_hz")}],
            output="both",
        ),
        Node(
            package="robot_state_publisher",
            executable="robot_state_publisher",
            parameters=[{"robot_description": robot_description}],
            output="both",
        ),
        Node(
            package="rviz2",
            executable="rviz2",
            arguments=["-d", rviz_config],
            condition=IfCondition(LaunchConfiguration("use_rviz")),
            output="both",
        ),
    ])
