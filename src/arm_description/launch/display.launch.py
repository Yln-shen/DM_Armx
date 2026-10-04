"""M1 的模型显示：robot_state_publisher + joint_state_publisher_gui + rviz2。

  ros2 launch arm_description display.launch.py                  # 默认不加载夹爪
  ros2 launch arm_description display.launch.py use_gripper:=true
  ros2 launch arm_description display.launch.py use_gui:=false   # 只看模型，不用滑动条
  ros2 launch arm_description display.launch.py use_rviz:=false  # 无图形界面时（只验证节点）

不接真机、不动电机：关节角由 joint_state_publisher_gui 的滑动条给。
"""
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.conditions import IfCondition
from launch.substitutions import Command, LaunchConfiguration, PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.parameter_descriptions import ParameterValue
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    use_gui = DeclareLaunchArgument(
        "use_gui", default_value="true",
        description="启动 joint_state_publisher_gui（滑动条手动摆关节）")
    use_gripper = DeclareLaunchArgument(
        "use_gripper", default_value="false",
        description="是否加载夹爪几何（本阶段不做，默认 false）")
    use_rviz = DeclareLaunchArgument(
        "use_rviz", default_value="true",
        description="是否启动 rviz2（无图形界面/只想验证节点时置 false）")

    xacro_file = PathJoinSubstitution(
        [FindPackageShare("arm_description"), "urdf", "arm.urdf.xacro"])
    rviz_config = PathJoinSubstitution(
        [FindPackageShare("arm_description"), "rviz", "display.rviz"])

    robot_description = ParameterValue(
        Command(["xacro ", xacro_file, " use_gripper:=", LaunchConfiguration("use_gripper")]),
        value_type=str)

    return LaunchDescription([
        use_gui,
        use_gripper,
        use_rviz,
        Node(
            package="robot_state_publisher",
            executable="robot_state_publisher",
            parameters=[{"robot_description": robot_description}],
            output="both",
        ),
        Node(
            package="joint_state_publisher_gui",
            executable="joint_state_publisher_gui",
            condition=IfCondition(LaunchConfiguration("use_gui")),
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
