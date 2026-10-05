"""M5：**接真机**跑 ros2_control（走 motor_driver_hardware/DmSystemInterface）。

三步验收（默认值就是最安全的那一步）：

  ① 只读（不使能、不发控制帧，只读真机姿态）
     ros2 launch arm_bringup real_control.launch.py
  ② 使能但不动（发保持帧：用电机侧实测位置，零位移）
     ros2 launch arm_bringup real_control.launch.py enable_on_activate:=true
  ③ 可以发轨迹（起轨迹控制器；先给慢速）
     ros2 launch arm_bringup real_control.launch.py enable_on_activate:=true spawn_arm_controller:=true vlim:=0.5

⚠️ 真机注意：
  - `enable_on_activate` 默认 **false**：不显式打开就绝不会动电机；
  - 使能后**每圈都要给 6 台发帧**（500ms 看门狗，陷阱 #24/#25）——本插件在 write() 里发，别去改；
  - 控制器一停 / 节点一退，硬件进入 deactivate ⇒ **全部失能**；
  - 串口设备号会变（陷阱 #28）：真源是 motor_driver/config/joint.yaml 的 `channel`，
    xacro 会把它写进 `<param name="device">`；对不上就改 yaml（或加 udev 规则固定设备名）。
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
    enable_on_activate = DeclareLaunchArgument(
        "enable_on_activate", default_value="false",
        description="是否在激活时使能电机。默认 false = 只读（绝不发控制帧）")
    spawn_arm_controller = DeclareLaunchArgument(
        "spawn_arm_controller", default_value="false",
        description="是否起轨迹控制器（起之前只有状态广播器，最安全）")
    vlim = DeclareLaunchArgument(
        "vlim", default_value="1.0",
        description="POS_VEL 速度上限 rad/s（首轮给慢的）")
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
        Command([
            "xacro ", xacro_file,
            " use_mock:=false",
            " use_gripper:=", LaunchConfiguration("use_gripper"),
            " enable_on_activate:=", LaunchConfiguration("enable_on_activate"),
            " vlim:=", LaunchConfiguration("vlim"),
        ]),
        value_type=str)

    control_node = Node(
        package="controller_manager",
        executable="ros2_control_node",
        parameters=[{"robot_description": robot_description}, controllers_file],
        # ⚠️ 把 CM 对 `robot_description` 的**订阅**重映射到私有话题：否则它会从**共享话题**
        #    订阅到别的节点（比如 mock 那套）发的 URDF，加载错的硬件插件 —— 2026-10-05 真机踩过
        #    （mock 的 CM 因此开了真机串口、把电机使能，最后 ERR=13 锁存）。重映射后只用自己参数。
        remappings=[("robot_description", "dm_armx_local_description")],
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
        condition=IfCondition(LaunchConfiguration("spawn_arm_controller")),
    )
    rviz_node = Node(
        package="rviz2",
        executable="rviz2",
        arguments=["-d", rviz_config],
        condition=IfCondition(LaunchConfiguration("use_rviz")),
        output="both",
    )

    spawn_arm_after_jsb = RegisterEventHandler(
        OnProcessExit(target_action=jsb_spawner, on_exit=[arm_spawner]))

    return LaunchDescription([
        enable_on_activate,
        spawn_arm_controller,
        vlim,
        use_rviz,
        use_gripper,
        control_node,
        robot_state_publisher,
        jsb_spawner,
        spawn_arm_after_jsb,
        rviz_node,
    ])
