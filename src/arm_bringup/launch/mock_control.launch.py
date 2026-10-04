"""M2：在 **mock 硬件** 上把 ros2_control 链路跑通（**不接真机、不动电机**）。

  ros2 launch arm_bringup mock_control.launch.py
  ros2 launch arm_bringup mock_control.launch.py use_rviz:=false     # 无图形界面验证

启动的东西：
  ros2_control_node（硬件插件 = mock_components/GenericSystem，见 arm_description 的
  urdf/inc/arm.ros2_control.xacro）
  + robot_state_publisher + spawner(joint_state_broadcaster) + spawner(arm_controller) + rviz2

验收：
  ros2 control list_controllers          # joint_state_broadcaster 与 arm_controller 都 active
  ros2 topic hz /joint_states            # ~100 Hz
  ros2 action send_goal /arm_controller/follow_joint_trajectory ...   # goal 成功，状态跟着变
"""
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, RegisterEventHandler
from launch.conditions import IfCondition
from launch.event_handlers import OnProcessExit
from launch.substitutions import Command, LaunchConfiguration, PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.parameter_descriptions import ParameterValue
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    use_rviz = DeclareLaunchArgument(
        "use_rviz", default_value="true", description="是否启动 rviz2")
    use_gripper = DeclareLaunchArgument(
        "use_gripper", default_value="false", description="模型是否带夹爪几何")

    xacro_file = PathJoinSubstitution(
        [FindPackageShare("arm_description"), "urdf", "arm.urdf.xacro"])
    rviz_config = PathJoinSubstitution(
        [FindPackageShare("arm_description"), "rviz", "display.rviz"])
    controllers_file = PathJoinSubstitution(
        [FindPackageShare("arm_bringup"), "config", "ros2_controllers.yaml"])

    robot_description = ParameterValue(
        Command(["xacro ", xacro_file, " use_gripper:=", LaunchConfiguration("use_gripper")]),
        value_type=str)

    control_node = Node(
        package="controller_manager",
        executable="ros2_control_node",
        parameters=[{"robot_description": robot_description}, controllers_file],
        output="both",
    )
    robot_state_publisher = Node(
        package="robot_state_publisher",
        executable="robot_state_publisher",
        parameters=[{"robot_description": robot_description}],
        output="both",
    )
    jsb_spawner = Node(
        package="controller_manager",
        executable="spawner",
        arguments=["joint_state_broadcaster", "--controller-manager", "/controller_manager"],
        output="both",
    )
    arm_spawner = Node(
        package="controller_manager",
        executable="spawner",
        arguments=["arm_controller", "--controller-manager", "/controller_manager"],
        output="both",
    )
    rviz_node = Node(
        package="rviz2",
        executable="rviz2",
        arguments=["-d", rviz_config],
        condition=IfCondition(LaunchConfiguration("use_rviz")),
        output="both",
    )

    # 先拉起 broadcaster（提供 /joint_states），再 spawn 轨迹控制器——顺序更稳
    spawn_arm_after_jsb = RegisterEventHandler(
        OnProcessExit(target_action=jsb_spawner, on_exit=[arm_spawner]))

    return LaunchDescription([
        use_rviz,
        use_gripper,
        control_node,
        robot_state_publisher,
        jsb_spawner,
        spawn_arm_after_jsb,
        rviz_node,
    ])
