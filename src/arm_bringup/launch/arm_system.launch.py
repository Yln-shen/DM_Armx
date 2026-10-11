"""**总启动**：一条命令起全栈（mock 或真机），取代原来分散的 mock_control / real_control / safe_park。

  # ① 默认：**mock 链 + 只读**（不接真机、不动电机，最安全；起 RViz 看模型）
  ros2 launch arm_bringup arm_system.launch.py

  # ② mock 上把管理层也跑起来（`arm_on` 会显式跳过硬件步骤，因为 mock 没有我们的插件服务）
  ros2 launch arm_bringup arm_system.launch.py use_rviz:=false
  ros2 service call /arm_manage/arm_on arm_msgs/srv/ArmOn "{with_controller: true}"

  # ③ 真机：**先只读**（绝不发控制帧）—— 这一步跑通了再谈使能
  ros2 launch arm_bringup arm_system.launch.py use_mock:=false

  # ④ 真机 + 使能（保持不动；`arm_on` 之后臂被托住）
  ros2 launch arm_bringup arm_system.launch.py use_mock:=false
  ros2 service call /arm_manage/arm_on arm_msgs/srv/ArmOn "{with_controller: true}"

  # ⑤ 真机 + 重力前馈（整链 MIT；先小 scale 分级上电）
  ros2 launch arm_bringup arm_system.launch.py use_mock:=false gravity_ff:=true gravity_ff_scale:=0.2

**它起什么**：`ros2_control_node`（硬件 = mock 或 `DmSystemInterface`）→ `robot_state_publisher` →
`joint_state_broadcaster` → `arm_controller`（**默认只装载、不激活**，见下）→ `arm_manage_node`（管理层）→ 一个 RViz。

**关键设计（都是踩过的坑）**
- ⚠️ **`arm_controller` 默认 `--inactive`（装载但不激活）**：M-3 要求"硬件使能时控制器必须先 inactive"。
  交给 `arm_manage` 在 `arm_on` 里按顺序激活，就不会出现"JTC 正 active 时硬件掉几帧"那种竞态
  （见 `docs/MANAGE_ARCH.md` §3.2）。要老行为就 `arm_controller_inactive:=false`。
- ⚠️ **只起一个 RViz**：本文件是唯一起 RViz 的地方。**不要把 `move_group.launch.py` 拉进来** ——
  它自带一套 mock 链（`use_mock:=true` 时还会再起一个 `ros2_control_node` + spawner）
  ⇒ 就成了"两套链同时跑"（[陷阱 #35](../AGENTS.md)：mock 订到真机 URDF 会把真机使能 ⇒ ERR=13 锁存）。
  MoveIt 要单独起：`ros2 launch arm_moveit_config move_group.launch.py`（**先确认本 launch 没在跑**）。
- ⚠️ **mock / 真机互斥**：本文件本身就只起一套；再开第二套时，真机那套会在 `on_activate` 撞
  串口的 `TIOCEXCL` 直接 EBUSY 失败（不会偷偷使能电机）。**同一时刻只开一套**这条纪律还是要人守。
- ⚠️ **`enable_on_activate` 默认 false**：不显式打开就绝不会使能电机。真机第一次务必留 false。
- ⚠️ **`LD_LIBRARY_PATH` 在本 launch 里自动补齐**（`$CONDA_PREFIX/lib`）：我们自己编的可执行文件
  （`arm_manage_node` 这类）的 RPATH 里没有 pixi 环境，不补就会 `librclcpp.so: cannot open shared object file`
  （[陷阱 #32](../AGENTS.md) 的运行时版）。不在 pixi 里跑时这一段自动跳过。
"""
import os

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, RegisterEventHandler, SetEnvironmentVariable
from launch.conditions import IfCondition
from launch.event_handlers import OnProcessExit
from launch.substitutions import Command, LaunchConfiguration, PathJoinSubstitution, PythonExpression
from launch_ros.actions import Node
from launch_ros.parameter_descriptions import ParameterValue
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    # ── 参数 ──────────────────────────────────────────────────────────────
    use_mock = DeclareLaunchArgument(
        "use_mock", default_value="true",
        description="true = mock_components（不接真机、不动电机）；false = 真机 DmSystemInterface")
    use_rviz = DeclareLaunchArgument(
        "use_rviz", default_value="true", description="是否启动 rviz2（本 launch 是唯一该起的地方）")
    start_arm_manage = DeclareLaunchArgument(
        "start_arm_manage", default_value="true",
        description="是否起管理层 arm_manage_node（3 个服务 + /arm/status）")
    enable_on_activate = DeclareLaunchArgument(
        "enable_on_activate", default_value="false",
        description="硬件激活时是否使能电机。**默认 false = 只读**（绝不发控制帧）。"
                    "真机第一次务必留 false；要让管理层管使能就保持 false + 调 /arm_manage/arm_on")
    spawn_arm_controller = DeclareLaunchArgument(
        "spawn_arm_controller", default_value="true",
        description="是否装载 arm_controller（轨迹控制器）")
    arm_controller_inactive = DeclareLaunchArgument(
        "arm_controller_inactive", default_value="true",
        description="装载后**不激活**（默认）。M-3 要求「硬件使能时控制器先 inactive」，"
                    "由 arm_manage 的 arm_on 按顺序激活；=false 则回到「spawner 直接激活」的老行为")
    vlim = DeclareLaunchArgument(
        "vlim", default_value="1.0", description="POS_VEL 速度上限 rad/s")
    use_gripper = DeclareLaunchArgument(
        "use_gripper", default_value="false", description="模型是否带夹爪几何")
    mit_controllers = DeclareLaunchArgument(
        "mit_controllers", default_value="false",
        description="控制器是否声索 velocity 命令接口（MIT 速度前馈要用）。默认只声索 position")
    gravity_ff = DeclareLaunchArgument(
        "gravity_ff", default_value="false",
        description="重力前馈总开关：true ⇒ 整链进 MIT + 每帧重力前馈，并自动改用 MIT 版控制器配置")
    gravity_ff_scale = DeclareLaunchArgument(
        "gravity_ff_scale", default_value="1.0",
        description="重力前馈缩放 0~1（**分级上电用**：第一次上真机从 0.2 开始）")

    # ── 运行时补 LD_LIBRARY_PATH（我们自己的可执行文件需要；见文件头说明）──
    conda_prefix = os.environ.get("CONDA_PREFIX", "")
    env_actions = []
    if conda_prefix:
        lib = os.path.join(conda_prefix, "lib")
        old = os.environ.get("LD_LIBRARY_PATH", "")
        env_actions.append(SetEnvironmentVariable(
            "LD_LIBRARY_PATH", (lib + ":" + old) if old else lib))

    # ── 选模型与控制器配置 ────────────────────────────────────────────────
    xacro_file = PathJoinSubstitution(
        [FindPackageShare("arm_description"), "urdf", "arm.urdf.xacro"])
    rviz_config = PathJoinSubstitution(
        [FindPackageShare("arm_description"), "rviz", "display.rviz"])
    # 两套控制器配置：POS_VEL（只声索 position，默认）与 MIT（多声索 velocity + 显式 constraints）。
    # ⚠️ 声明 velocity 会改变 JTC 的到达判据（真机曾卡死），所以默认不声明；gravity_ff=true 时必须用 MIT 那份。
    controllers_file = PathJoinSubstitution([
        FindPackageShare("arm_bringup"), "config",
        PythonExpression([
            "'ros2_controllers_mit.yaml' if ('", LaunchConfiguration("mit_controllers"),
            "' == 'true' or '", LaunchConfiguration("gravity_ff"), "' == 'true')",
            " else 'ros2_controllers.yaml'",
        ]),
    ])
    arm_manage_params = PathJoinSubstitution(
        [FindPackageShare("arm_manage"), "config", "arm_manage.yaml"])

    # use_mock 贯穿到 xacro（mock: mock_components；真机: DmSystemInterface + 那一堆 <param>）
    robot_description = ParameterValue(
        Command([
            "xacro ", xacro_file,
            " use_mock:=", LaunchConfiguration("use_mock"),
            " use_gripper:=", LaunchConfiguration("use_gripper"),
            " enable_on_activate:=", LaunchConfiguration("enable_on_activate"),
            " vlim:=", LaunchConfiguration("vlim"),
            " gravity_ff:=", LaunchConfiguration("gravity_ff"),
            " gravity_ff_scale:=", LaunchConfiguration("gravity_ff_scale"),
        ]),
        value_type=str)

    # ── 节点 ──────────────────────────────────────────────────────────────
    control_node = Node(
        package="controller_manager",
        executable="ros2_control_node",
        parameters=[{"robot_description": robot_description}, controllers_file],
        # ⚠️ 不要把 robot_description 重映射走：CM 只从话题取描述、**不会回退到自己的参数**
        #    （试过 ⇒ 一直 "Waiting for data on 'robot_description' topic" ⇒ 硬件起不来，
        #     RViz 里看着像"模型残缺"）。防"订阅到别人的 URDF"靠：只开一套 + 串口 TIOCEXCL（陷阱 #35）。
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
    # 默认 `--inactive`：只装载 + 配置，不激活（把激活交给 arm_manage 的 arm_on，保证 M-3 顺序）。
    # 两个 spawner 各带一个条件，比用 PythonExpression 拼参数列表清楚。
    spawn_ctl = LaunchConfiguration("spawn_arm_controller")
    inactive = LaunchConfiguration("arm_controller_inactive")
    arm_spawner_inactive = Node(
        package="controller_manager",
        executable="spawner",
        arguments=["arm_controller", "--controller-manager", "/controller_manager", "--inactive"],
        output="both",
        condition=IfCondition(PythonExpression(
            ["'", spawn_ctl, "' == 'true' and '", inactive, "' == 'true'"])),
    )
    arm_spawner_active = Node(
        package="controller_manager",
        executable="spawner",
        arguments=["arm_controller", "--controller-manager", "/controller_manager"],
        output="both",
        condition=IfCondition(PythonExpression(
            ["'", spawn_ctl, "' == 'true' and '", inactive, "' == 'false'"])),
    )
    # 管理层：3 个服务（arm_on / safe_disarm / emergency_stop）+ /arm/status。
    # ⚠️ 它在 mock 链上照样能起（`HardwareGate` 探测不到插件服务时会**显式跳过**硬件步骤），
    #    所以 mock 里也能把"控制器侧 + 运动流程"整条演练一遍。
    arm_manage_node = Node(
        package="arm_manage",
        executable="arm_manage_node",
        name="arm_manage",
        parameters=[arm_manage_params],
        output="both",
        condition=IfCondition(LaunchConfiguration("start_arm_manage")),
    )
    rviz_node = Node(
        package="rviz2",
        executable="rviz2",
        arguments=["-d", rviz_config],
        condition=IfCondition(LaunchConfiguration("use_rviz")),
        output="both",
    )

    # 先拉起 broadcaster（提供 /joint_states），再装 arm_controller —— 顺序更稳
    spawn_arm_after_jsb = RegisterEventHandler(
        OnProcessExit(target_action=jsb_spawner, on_exit=[arm_spawner_inactive, arm_spawner_active]))

    return LaunchDescription([
        use_mock, use_rviz, start_arm_manage, enable_on_activate,
        spawn_arm_controller, arm_controller_inactive, vlim, use_gripper,
        mit_controllers, gravity_ff, gravity_ff_scale,
        *env_actions,
        control_node,
        robot_state_publisher,
        jsb_spawner,
        spawn_arm_after_jsb,
        arm_manage_node,
        rviz_node,
    ])
