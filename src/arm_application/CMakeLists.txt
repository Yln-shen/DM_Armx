cmake_minimum_required(VERSION 3.10)
project(arm_application)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()
if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# 应用层：业务流程节点（收臂等）。**不碰协议、不碰串口** —— 那些在 motor_driver_hardware。
# 它只通过 ros2_control 的正常通路（JointTrajectoryController 的 action）驱动机械臂。
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_action REQUIRED)
find_package(action_msgs REQUIRED)
find_package(control_msgs REQUIRED)
find_package(trajectory_msgs REQUIRED)
find_package(sensor_msgs REQUIRED)
# 关节限位（模型坐标）—— 由 arm_description 在构建期从
# joint.yaml + align.yaml 生成并导出（JTC **不校验限位**，见 gen_joint_limits_header.py）
# arm_description 提供 INTERFACE 库（只有头文件 + 限位常量）
find_package(arm_description REQUIRED)

add_executable(safe_park_node src/safe_park_node.cpp)
target_link_libraries(safe_park_node
  arm_description::arm_description
  rclcpp::rclcpp
  rclcpp_action::rclcpp_action
  ${action_msgs_TARGETS}
  ${control_msgs_TARGETS}
  ${trajectory_msgs_TARGETS}
  ${sensor_msgs_TARGETS})
install(TARGETS safe_park_node DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY launch DESTINATION share/${PROJECT_NAME})

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  # 下面两行是 ament 模板自带的 lint 跳过（本仓库暂无版权头/cpplint 约定）
  set(ament_cmake_copyright_FOUND TRUE)
  set(ament_cmake_cpplint_FOUND TRUE)
  ament_lint_auto_find_test_dependencies()
endif()

ament_package()
