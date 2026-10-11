cmake_minimum_required(VERSION 3.10)
project(arm_manage)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()
if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(arm_msgs REQUIRED)
find_package(arm_motion REQUIRED)
find_package(arm_description REQUIRED)
find_package(controller_manager_msgs REQUIRED)

# ⚠️ 链接**可执行文件**时，rclcpp 的传递依赖（-lcap / -llttng-ust）在 $CONDA_PREFIX/lib 下，
#    而 pixi 激活不设 LIBRARY_PATH ⇒ 不显式加就会报「找不到 -lcap」（陷阱 #32/#49）。
if(DEFINED ENV{CONDA_PREFIX})
  link_directories($ENV{CONDA_PREFIX}/lib)
endif()

# 一个节点 = 唯一状态机（两个 Gate 是头文件里的两个小类，不单独成库）
add_executable(arm_manage_node src/manage_node.cpp)
target_include_directories(arm_manage_node PRIVATE include)
target_link_libraries(arm_manage_node
  rclcpp::rclcpp
  arm_msgs::arm_msgs
  arm_motion::arm_motion
  arm_description::arm_description
  controller_manager_msgs::controller_manager_msgs
  ${sensor_msgs_TARGETS})

install(TARGETS arm_manage_node DESTINATION lib/${PROJECT_NAME})
# 折叠位（park）+ vlim/tol/timeout/max_retries 的真源（M-10）
install(DIRECTORY config DESTINATION share/${PROJECT_NAME})
install(DIRECTORY include/ DESTINATION include)

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  set(ament_cmake_copyright_FOUND TRUE)
  set(ament_cmake_cpplint_FOUND TRUE)
  ament_lint_auto_find_test_dependencies()
endif()

ament_package()
