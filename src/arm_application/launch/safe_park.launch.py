"""收臂（safe_park）—— 应用层节点。

  ros2 launch arm_application safe_park.launch.py
  ros2 launch arm_application safe_park.launch.py vlim:=0.1 tol_rad:=0.03

**前置**（本节点只发轨迹，不碰电机/串口）：
  ros2 launch arm_bringup real_control.launch.py use_rviz:=false \
      enable_on_activate:=true spawn_arm_controller:=true vlim:=0.5

它做的事：把 6 轴送回**折叠位**（模型坐标，内置 `[1.4724, 0.0008, −0.0233, 0.0027, 0.0069, 0.0149]`），
走 `arm_controller` 的 `FollowJointTrajectory`。

想换目标姿态（不常用）就直接 `ros2 run`，别走这个 launch —— 因为 `park_pose` 是
`std::vector<double>`，用 launch 传空串会把类型搞坏：
  ros2 run arm_application safe_park_node --ros-args -p park_pose:="[1.47,0.0,-0.02,0.0,0.0,0.0]"

⚠️ 收完臂 **臂仍然使能**（硬件停在 ACTIVE，插件继续发保持帧托着它）——
这是刻意的：应用层节点没有直接命令电机的通道。要真正失能，让**上面那个 `ros2_control_node`
正常退出**（Ctrl-C 那个 launch），它会在 `on_deactivate()` 里失能全部电机。
"""
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.conditions import IfCondition
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    vlim = DeclareLaunchArgument(
        "vlim", default_value="0.15",
        description="收臂轨迹速度上限 rad/s。**默认很慢**，首轮上真机别加大")
    tol_rad = DeclareLaunchArgument(
        "tol_rad", default_value="0.05",
        description="到位判据 rad：跑完轨迹后再核一遍实测位置，最大残差超过它就判失败")
    timeout_sec = DeclareLaunchArgument(
        "timeout_sec", default_value="60.0", description="轨迹执行超时（秒）")
    state_wait_sec = DeclareLaunchArgument(
        "state_wait_sec", default_value="10.0", description="等 /joint_states 的超时（秒）")
    use_rviz = DeclareLaunchArgument(
        "use_rviz", default_value="false",
        description="是否顺带起 rviz2（默认不起——收臂时看日志更清楚）")

    safe_park_node = Node(
        package="arm_application",
        executable="safe_park_node",
        name="safe_park_node",
        output="both",
        # ⚠️ 刻意**不**往这里放 park_pose：节点把它声明成 std::vector<double>，
        #    用 launch 传空串（或传错格式）会让参数类型不符、节点直接起不来。
        #    它留空时节点用内置折叠位；要改就 `ros2 run` + `-p park_pose:=[...]`。
        parameters=[{
            "vlim": LaunchConfiguration("vlim"),
            "tol_rad": LaunchConfiguration("tol_rad"),
            "timeout_sec": LaunchConfiguration("timeout_sec"),
            "state_wait_sec": LaunchConfiguration("state_wait_sec"),
        }],
    )
    rviz_node = Node(
        package="rviz2",
        executable="rviz2",
        arguments=["-d", PathJoinSubstitution([
            FindPackageShare("arm_description"), "rviz", "display.rviz"])],
        condition=IfCondition(LaunchConfiguration("use_rviz")),
        output="both",
    )

    return LaunchDescription([
        vlim, tol_rad, timeout_sec, state_wait_sec, use_rviz,
        safe_park_node,
        rviz_node,
    ])
