"""M6：起 MoveIt 的 move_group（**只做规划**；执行仍由 ros2_control 的 arm_controller 承担）。

  # ① 先对 mock 跑（不接真机）
  ros2 launch arm_moveit_config move_group.launch.py

  # ② 再对真机跑：另开终端先把真机那套起起来，再加 use_mock:=false
  ros2 launch arm_bringup real_control.launch.py enable_on_activate:=true spawn_arm_controller:=true vlim:=0.5
  ros2 launch arm_moveit_config move_group.launch.py use_mock:=false

⚠️ 一次只起一套：`use_mock:=true` 会自己起 controller_manager + 控制器，
   与 `real_control.launch.py` **不能同时开**（两个 CM 抢同名控制器；而且 CM 会从共享话题
   `/robot_description` 订阅到对方的 URDF —— 见 AGENTS 陷阱 #35）。

📌 参数为什么用 moveit_configs_utils：手拼 `{"ompl": {...}}`（嵌套）与扁平 `ompl.planning_plugin`
   **两种都试过**，move_group 都报 `Planning plugin name is empty or not defined in namespace 'ompl'`。
   官方 builder 会把 `planning_pipelines` / `ompl.*` / 运动学 / 控制器映射按 MoveIt 期望的结构摆好。
"""
import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.conditions import IfCondition
from launch.substitutions import Command, LaunchConfiguration, PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.parameter_descriptions import ParameterValue
from launch_ros.substitutions import FindPackageShare
from moveit_configs_utils import MoveItConfigsBuilder


def generate_launch_description():
    use_mock = DeclareLaunchArgument(
        "use_mock", default_value="true",
        description="true = 自己起 mock 硬件与控制器（先这么验规划）；false = 只起 move_group，硬件另起")
    use_rviz = DeclareLaunchArgument(
        "use_rviz", default_value="true", description="是否启动带 MotionPlanning 面板的 rviz2")
    use_gripper = DeclareLaunchArgument(
        "use_gripper", default_value="false", description="模型是否带夹爪几何（本阶段不做夹爪）")
    enable_on_activate = DeclareLaunchArgument(
        "enable_on_activate", default_value="false",
        description="只有 use_mock:=false 时才被 xacro 用上：真机是否在激活时使能")

    arm_desc = get_package_share_directory("arm_description")
    cfg = get_package_share_directory("arm_moveit_config")
    bringup = get_package_share_directory("arm_bringup")

    xacro_file = PathJoinSubstitution([FindPackageShare("arm_description"), "urdf", "arm.urdf.xacro"])
    rviz_config = PathJoinSubstitution([FindPackageShare("arm_moveit_config"), "rviz", "moveit.rviz"])

    # 带 launch 参数的那份 robot_description（use_mock 决定用 mock 还是真机插件 —— 这是关键，
    # 不能用 builder 自己读的那份：它只会用 xacro 的默认值）
    robot_description = ParameterValue(
        Command(["xacro ", xacro_file,
                 " use_gripper:=", LaunchConfiguration("use_gripper"),
                 " use_mock:=", LaunchConfiguration("use_mock"),
                 " enable_on_activate:=", LaunchConfiguration("enable_on_activate")]),
        value_type=str)

    # builder 负责 SRDF / kinematics.yaml / ompl_planning.yaml / moveit_controllers.yaml 这些
    # 约定文件名的参数（都在本包的 config/ 下，名字与 MoveIt 约定一致）
    moveit_config = (
        MoveItConfigsBuilder("dm_armx", package_name="arm_moveit_config")
        .robot_description(file_path=os.path.join(arm_desc, "urdf", "arm.urdf.xacro"))
        .robot_description_semantic(file_path=os.path.join(cfg, "config", "arm.srdf"))
        .robot_description_kinematics(file_path=os.path.join(cfg, "config", "kinematics.yaml"))
        .planning_pipelines(pipelines=["ompl"], default_planning_pipeline="ompl")
        .trajectory_execution(file_path=os.path.join(cfg, "config", "moveit_controllers.yaml"))
        .to_moveit_configs()
    )
    move_group_params = moveit_config.to_dict()
    move_group_params["robot_description"] = robot_description      # 覆写成带参数的那份

    move_group = Node(
        package="moveit_ros_move_group", executable="move_group",
        parameters=[move_group_params], output="both")

    rviz_node = Node(
        package="rviz2", executable="rviz2", arguments=["-d", rviz_config],
        parameters=[moveit_config.robot_description, moveit_config.robot_description_semantic,
                    moveit_config.robot_description_kinematics],
        condition=IfCondition(LaunchConfiguration("use_rviz")), output="both")

    # mock 那套（与 arm_bringup 的 mock_control.launch.py 等价）：只在 use_mock 为真时起
    use_mock_if = IfCondition(LaunchConfiguration("use_mock"))
    controllers_file = os.path.join(bringup, "config", "ros2_controllers.yaml")
    mock_nodes = [
        Node(package="controller_manager", executable="ros2_control_node",
             parameters=[{"robot_description": robot_description}, controllers_file],
             condition=use_mock_if, output="both"),
        Node(package="robot_state_publisher", executable="robot_state_publisher",
             parameters=[{"robot_description": robot_description}],
             condition=use_mock_if, output="both"),
        Node(package="controller_manager", executable="spawner",
             arguments=["joint_state_broadcaster", "--controller-manager", "/controller_manager"],
             condition=use_mock_if, output="both"),
        Node(package="controller_manager", executable="spawner",
             arguments=["arm_controller", "--controller-manager", "/controller_manager"],
             condition=use_mock_if, output="both"),
    ]

    return LaunchDescription(
        [use_mock, use_rviz, use_gripper, enable_on_activate, move_group, rviz_node] + mock_nodes)
