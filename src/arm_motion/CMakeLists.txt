cmake_minimum_required(VERSION 3.10)
project(arm_motion)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()
if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# 运动执行件（库）：给 JointTrajectoryController 发单点轨迹的**非阻塞**客户端。
# 2026-10-07 从 arm_application/safe_park_node.cpp 抽出（M9.0），行为逐条等价。
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_action REQUIRED)
find_package(action_msgs REQUIRED)
find_package(control_msgs REQUIRED)
find_package(trajectory_msgs REQUIRED)
find_package(sensor_msgs REQUIRED)
# 关节限位（模型坐标）：arm_description 构建期从 joint.yaml + align.yaml 生成并导出。
# 它是**护栏的唯一真源**（JTC 不校验限位，越界会顶在机械硬限位上堵转，见陷阱 #59）。
find_package(arm_description REQUIRED)
# 命名目标表解析（纯数据，无 ROS 依赖 ⇒ 加载器可脱离 ROS 单测）
find_package(yaml-cpp REQUIRED)

# ⚠️ robostack 里 rclcpp 的传递依赖（-lcap / -llttng-ust）在 $CONDA_PREFIX/lib，而 pixi 激活
#    **不设 LIBRARY_PATH** ⇒ 链接**可执行文件**（这里的 gtest）会报「找不到 -lcap」。
#    （共享库不受影响：允许未定义符号，运行时由 LD_LIBRARY_PATH 解析。）见 AGENTS 陷阱 #32。
if(DEFINED ENV{CONDA_PREFIX})
  link_directories($ENV{CONDA_PREFIX}/lib)
endif()

# `named_targets.cpp` 刻意不依赖 rclcpp（只依赖 yaml-cpp + arm_description 的限位头）
# ⇒ 它的单测不需要节点/串口；和 MotionClient 编在同一个库里只是省一个 target。
add_library(arm_motion SHARED src/motion_client.cpp src/named_targets.cpp)
target_include_directories(arm_motion PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<INSTALL_INTERFACE:include>")
target_link_libraries(arm_motion
  arm_description::arm_description
  yaml-cpp::yaml-cpp
  rclcpp::rclcpp
  rclcpp_action::rclcpp_action
  ${action_msgs_TARGETS}
  ${control_msgs_TARGETS}
  ${trajectory_msgs_TARGETS}
  ${sensor_msgs_TARGETS})

# 只 ament_export_include_directories 不够：那样下游 find_package 得到空配置，
# 既拿不到 -I 也**建立不了构建顺序**（arm_application 会先于本包编译）。⇒ export target。
install(TARGETS arm_motion EXPORT export_arm_motion
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin)
install(DIRECTORY include/ DESTINATION include)
# 命名目标表与运动参数是**数据**，装到 share/ 下供 launch 用
install(DIRECTORY config DESTINATION share/${PROJECT_NAME})

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  # 进程内假 action server + 假 /joint_states：正常到达 / 残差超差 / 执行超时后真的取消（陷阱 #60）/
  # 等不到 action server / 等不到状态 / 越界拒绝（一帧不发）。自带 main（要 rclcpp::init）。
  ament_add_gtest(test_motion_client test/test_motion_client.cpp)
  target_link_libraries(test_motion_client arm_motion)

  # 命名目标表：**纯逻辑**（写临时 YAML → 加载 → 看校验结果），不需要 rclcpp。
  # 覆盖：正常加载 / 关节名顺序错 / 越界 / 未知字段 / 类型错 / NaN / frame 错 / 缺 targets。
  # 用例需要真实文件路径 ⇒ 用 `NAMED_TARGETS_TEST_DIR` 指到源码目录（构建期写死，不落盘）。
  ament_add_gtest(test_named_targets test/test_named_targets.cpp)
  target_link_libraries(test_named_targets arm_motion)
  target_compile_definitions(test_named_targets PRIVATE
    ARM_MOTION_TEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test")
endif()

ament_export_targets(export_arm_motion HAS_LIBRARY_TARGET)
ament_export_dependencies(
  ament_cmake rclcpp rclcpp_action action_msgs control_msgs trajectory_msgs sensor_msgs arm_description yaml-cpp)
ament_package()
