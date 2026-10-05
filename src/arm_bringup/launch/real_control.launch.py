"""M5：**接真机**跑 ros2_control（走 motor_driver_hardware/DmSystemInterface）。

三步验收（默认值就是最安全的那一步）：

  ① 只读（不使能、不发控制帧，只读真机姿态）
     ros2 launch arm_bringup real_control.launch.py
  ② 使能但不动（发保持帧：用电机侧实测位置，零位移）
     ros2 launch arm_bringup real_control.launch.py enable_on_activate:=true
  ③ 可以发轨迹（起轨迹控制器；先给慢速）
     ros2 launch arm_bringup real_control.launch.py enable_on_activate:=true spawn_arm_controller:=true vlim:=0.5

  ④ 需要 velocity 命令接口（MIT 速度前馈）时额外给 `mit_controllers:=true`：
     它会换成 config/ros2_controllers_mit.yaml（多声索 velocity + 显式 constraints）。
     ⚠️ 别把 velocity 加到默认那份里 —— 声索它会改变 JTC 的到达判据，真机会卡死。

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
from launch.substitutions import Command, LaunchConfiguration, PathJoinSubstitution, PythonExpression
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
    mit_controllers = DeclareLaunchArgument(
        "mit_controllers", default_value="false",
        description="控制器是否声索 velocity 命令接口（MIT 速度前馈要用）。"
                    "默认 false = 只声索 position（POS_VEL 路径，最稳）")
    gravity_ff = DeclareLaunchArgument(
        "gravity_ff", default_value="false",
        description="重力前馈总开关。true ⇒ 这条链的关节进 MIT + 每帧发重力前馈，"
                    "并**自动**改用 MIT 版控制器配置（多声索 velocity）。默认 false")
    gravity_ff_scale = DeclareLaunchArgument(
        "gravity_ff_scale", default_value="1.0",
        description="重力前馈缩放 0~1（**分级上电用**）：第一次上真机先从 0.2 开始，"
                    "确认方向对、位移比纯 PD 更小，再 0.5 → 1.0")

    xacro_file = PathJoinSubstitution(
        [FindPackageShare("arm_description"), "urdf", "arm.urdf.xacro"])
    rviz_config = PathJoinSubstitution(
        [FindPackageShare("arm_description"), "rviz", "display.rviz"])
    # 两套控制器配置：POS_VEL（只声索 position）与 MIT（多声索 velocity + 显式 constraints）。
    # ⚠️ 声索 velocity 会改变 JTC 的到达判据（真机曾因此卡死），所以默认不声索 —— 见那份 yaml。
    # gravity_ff=true 时必须用 MIT 那份：要用速度前馈就得声索它。
    controllers_file = PathJoinSubstitution([
        FindPackageShare("arm_bringup"), "config",
        PythonExpression([
            "'ros2_controllers_mit.yaml' if ('", LaunchConfiguration("mit_controllers"),
            "' == 'true' or '", LaunchConfiguration("gravity_ff"), "' == 'true')",
            " else 'ros2_controllers.yaml'",
        ]),
    ])

    robot_description = ParameterValue(
        Command([
            "xacro ", xacro_file,
            " use_mock:=false",
            " use_gripper:=", LaunchConfiguration("use_gripper"),
            " enable_on_activate:=", LaunchConfiguration("enable_on_activate"),
            " vlim:=", LaunchConfiguration("vlim"),
            " gravity_ff:=", LaunchConfiguration("gravity_ff"),
            " gravity_ff_scale:=", LaunchConfiguration("gravity_ff_scale"),
        ]),
        value_type=str)

    control_node = Node(
        package="controller_manager",
        executable="ros2_control_node",
        parameters=[{"robot_description": robot_description}, controllers_file],
        # ⚠️ 这里**不要**把 `robot_description` 重映射走：CM 只从话题取描述、**不会回退到自己参数**
        #    （试过 ⇒ 它一直打 "Waiting for data on 'robot_description' topic" ⇒ 硬件根本起不来，
        #      RViz 里就是"残缺的模型"）。防"订阅到别人的 URDF"靠两条纪律与保护：
        #    ①任何时候只开一套（mock / 真机互斥）；②串口设了 TIOCEXCL，抢不到口的第二套会在
        #    on_activate 直接 EBUSY 失败（不会偷偷使能电机）。见 AGENTS 陷阱 #35。
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
        mit_controllers,
        gravity_ff,
        gravity_ff_scale,
        control_node,
        robot_state_publisher,
        jsb_spawner,
        spawn_arm_after_jsb,
        rviz_node,
    ])
