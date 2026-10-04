cmake_minimum_required(VERSION 3.10)
project(motor_driver_hardware)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()
if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)

# C++ 核：协议 + 串口 + 总线（纯 C++，**不依赖 rclcpp** —— 依赖 ROS 的只有 M5 的插件那一层）
add_library(dm_hardware
  src/dm_frames.cpp
  src/dm_serial.cpp
  src/dm_bus.cpp
  src/dm_joint.cpp
)
target_include_directories(dm_hardware PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)

install(TARGETS dm_hardware
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin)
install(DIRECTORY include/ DESTINATION include)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)

  ament_add_gtest(test_dm_frames test/test_dm_frames.cpp)
  target_link_libraries(test_dm_frames dm_hardware)
  # 与 Python 的 dm_frames 逐字节对拍：把它的**源码目录**编译进来（测试时 import 用）
  target_compile_definitions(test_dm_frames PRIVATE
    MOTOR_DRIVER_PY_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../motor_driver/motor_driver")

  # 总线层：假串口（内存字节流）走通收发管线
  ament_add_gtest(test_dm_bus test/test_dm_bus.cpp)
  target_link_libraries(test_dm_bus dm_hardware)

  # 关节层：与 Python 的 joint.py 对拍换算/发帧/保持帧
  ament_add_gtest(test_dm_joint test/test_dm_joint.cpp)
  target_link_libraries(test_dm_joint dm_hardware)
  target_compile_definitions(test_dm_joint PRIVATE
    MOTOR_DRIVER_PY_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../motor_driver/motor_driver")
endif()

ament_package()
