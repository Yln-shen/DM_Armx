"""M6：起 MoveIt 的 move_group（**只做规划**；执行仍由 ros2_control 的 arm_controller 承担）。

  # ① 先对 mock 跑（不接真机）：规划能出来、能在 RViz 里看到"影子"动
  ros2 launch arm_moveit_config move_group.launch.py

  # ② 再对真机跑：另开终端先把真机那套起起来，再加 use_mock:=false
  ros2 launch arm_bringup real_control.launch.py enable_on_activate:=true spawn_arm_controller:=true vlim:=0.5
  ros2 launch arm_moveit_config move_group.launch.py use_mock:=false

⚠️ 一次只起一套：`use_mock:=true` 会自己起 controller_manager + 控制器，
   与 `real_control.launch.py` **不能同时开**（两个 controller_manager 抢同一个控制器名）。
⚠️ 真机上的执行速度由 arm_controller 的 `vlim` 决定（real_control.launch.py 的参数）；
   MoveIt 算出的时间参数是"期望速度"，实际跟不上就会被拉长（慢，但不会错）。
"""
import os

import yaml
from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.conditions import IfCondition
from launch.substitutions import Command, LaunchConfiguration, PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.parameter_descriptions import ParameterValue
from launch_ros.substitutions import FindPackageShare


def load_yaml(*parts):
    with open(os.path.join(*parts), "r", encoding="utf-8") as f:
        return yaml.safe_load(f)


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

    # 配置文件路径用**纯 Python**取（不要在 launch 里 eval substitution：那个要 LaunchContext，
    # 写不对就是 TypeError，而这条路径跟 ROS 图无关，直接读文件最简单）
    arm_desc = get_package_share_directory("arm_description")
    cfg = get_package_share_directory("arm_moveit_config")
    bringup = get_package_share_directory("arm_bringup")

    xacro_file = PathJoinSubstitution([FindPackageShare("arm_description"), "urdf", "arm.urdf.xacro"])
    rviz_config = PathJoinSubstitution([FindPackageShare("arm_moveit_config"), "rviz", "moveit.rviz"])

    robot_description = ParameterValue(
        Command(["xacro ", xacro_file,
                 " use_gripper:=", LaunchConfiguration("use_gripper"),
                 " use_mock:=", LaunchConfiguration("use_mock"),
                 " enable_on_activate:=", LaunchConfiguration("enable_on_activate")]),
        value_type=str)

    with open(os.path.join(cfg, "config", "arm.srdf"), "r", encoding="utf-8") as f:
        srdf_text = f.read()

    # MoveIt 的参数按"经典结构"显式拼：SRDF → robot_description_semantic、IK →
    # robot_description_kinematics、OMPL → ompl.*、控制器映射 → 顶层键。
    # 这样不依赖 moveit_configs_utils 的 API 细节，出问题一眼能看出是哪个文件。
    move_group_params = [
        {"robot_description": robot_description},
        {"robot_description_semantic": srdf_text},
        {"robot_description_kinematics": load_yaml(cfg, "config", "kinematics.yaml")},
        {"planning_pipelines": ["ompl"], "default_planning_pipeline": "ompl"},
        {"ompl": load_yaml(cfg, "config", "ompl_planning.yaml")},
        load_yaml(cfg, "config", "moveit_controllers.yaml"),
        {"allow_trajectory_execution": True,
         "publish_planning_scene": True,
         "publish_geometry_updates": True,
         "publish_state_updates": True,
         "publish_transforms_updates": True},
    ]

    move_group = Node(
        package="moveit_ros_move_group", executable="move_group",
        parameters=move_group_params, output="both")

    rviz_node = Node(
        package="rviz2", executable="rviz2", arguments=["-d", rviz_config],
        parameters=[{"robot_description": robot_description}],
        condition=IfCondition(LaunchConfiguration("use_rviz")), output="both")

    # mock 那套（与 arm_bringup 的 mock_control.launch.py 等价）：只在 use_mock 为真时起
    use_mock_if = IfCondition(LaunchConfiguration("use_mock"))
    controllers_file = os.path.join(bringup, "config", "ros2_controllers.yaml")
    mock_nodes = [
        # ⚠️ 把 controller_manager 对 `robot_description` 的**订阅**重映射到私有话题：
        #    真机那套的 robot_state_publisher 也在发 `/robot_description`，CM 会从**共享话题**
        #    订阅到别人的 URDF（2026-10-05 真机踩过：mock 的 CM 因此加载了真机插件、开了真机串口、
        #    把电机使能了 ⇒ ERR=13 锁存）。重映射之后 CM 只用自己参数里的 description，互不干扰。
        Node(package="controller_manager", executable="ros2_control_node",
             parameters=[{"robot_description": robot_description}, controllers_file],
             remappings=[("robot_description", "dm_armx_local_description")],
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
