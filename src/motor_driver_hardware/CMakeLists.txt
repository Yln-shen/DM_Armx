cmake_minimum_required(VERSION 3.10)
project(motor_driver_hardware)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()
if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)

# 协议层：**纯函数**库 —— 不开发送、不碰串口、不判安全（与 dm_frames.py 的边界一致）
add_library(dm_frames src/dm_frames.cpp)
target_include_directories(dm_frames PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)

install(TARGETS dm_frames
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin)
install(DIRECTORY include/ DESTINATION include)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)

  ament_add_gtest(test_dm_frames test/test_dm_frames.cpp)
  target_link_libraries(test_dm_frames dm_frames)
  # 与 Python 的 dm_frames 逐字节对拍：把它的**源码目录**编译进来（测试时 import 用）
  target_compile_definitions(test_dm_frames PRIVATE
    MOTOR_DRIVER_PY_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../motor_driver/motor_driver")
endif()

ament_package()
