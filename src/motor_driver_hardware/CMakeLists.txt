cmake_minimum_required(VERSION 3.10)
project(motor_driver_hardware)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()
if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
# 动力学（重力前馈）用 pinocchio 算重力项。它是 C++ 库、不牵 rclcpp ⇒ 可以进 dm_hardware 核。
find_package(pinocchio REQUIRED)

# ── C++ 核：协议 + 串口 + 总线 + 关节 + 重力（**不依赖 rclcpp**）──
add_library(dm_hardware
  src/dm_frames.cpp
  src/dm_serial.cpp
  src/dm_bus.cpp
  src/dm_joint.cpp
  src/dm_gravity.cpp
)
target_include_directories(dm_hardware PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
# PUBLIC：静态库的使用者（插件 .so 与测试）也要能解析 pinocchio 的符号。
#   pinocchio::parsers 是独立的 .so（URDF 解析在里面），别只链 pinocchio::pinocchio。
target_link_libraries(dm_hardware PUBLIC
  pinocchio::pinocchio
  pinocchio::pinocchio_parsers)

install(TARGETS dm_hardware
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin)
# 这份静态库要被插件 .so 链进去 ⇒ 必须编成位置无关代码（否则插件链接时会报重定位错误）
set_property(TARGET dm_hardware PROPERTY POSITION_INDEPENDENT_CODE ON)
install(DIRECTORY include/ DESTINATION include)

# ── ros2_control 插件（这一层才依赖 ROS）──
find_package(hardware_interface REQUIRED)
find_package(pluginlib REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_lifecycle REQUIRED)

# ⚠️ robostack 环境里 rclcpp 的传递依赖（-lcap / -llttng-ust / -llttng-ust-common）在
#    $CONDA_PREFIX/lib 下，而 pixi 的激活**没有设置 LIBRARY_PATH** ⇒ 链接可执行文件（不是 .so）
#    时会报「找不到 -lcap」。这里显式把环境的 lib 目录加进链接搜索路径。
#    （共享库不受影响：允许未定义符号，运行时由 LD_LIBRARY_PATH 解析。）
if(DEFINED ENV{CONDA_PREFIX})
  link_directories($ENV{CONDA_PREFIX}/lib)
endif()

# ⚠️ 必须是 **SHARED**：pluginlib 只能 dlopen 共享库。写成默认（静态 .a）会编译链接全过、
#    单元测试也全绿（测试是直接链静态库，从不 dlopen），但真机 controller_manager 加载时报
#    「Could not find library corresponding to plugin motor_driver_hardware/DmSystemInterface」
#    —— 2026-10-05 真机踩过（AGENTS 陷阱 #34）。
add_library(dm_system_interface SHARED src/dm_system_interface.cpp)
target_link_libraries(dm_system_interface PUBLIC
  dm_hardware
  hardware_interface::hardware_interface
  pluginlib::pluginlib
  rclcpp::rclcpp
  rclcpp_lifecycle::rclcpp_lifecycle)

# 插件描述文件装进 share/<pkg> 并注册到 hardware_interface 的 pluginlib 导出组
pluginlib_export_plugin_description_file(hardware_interface motor_driver_hardware.xml)
install(TARGETS dm_system_interface
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin)

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

  # 插件：假串口 + 模拟电机，走通"只读/使能/保持/命令换算/故障"（自带 main，要 rclcpp::init）
  ament_add_gtest(test_dm_system_interface test/test_dm_system_interface.cpp)
  target_link_libraries(test_dm_system_interface dm_system_interface)

  # 重力项：与已跑通的 MuJoCo 逐点对拍（守住 sign 那一段符号链）
  #   ARM_DYN_URDF 指向 **arm_description 构建时生成**的那份纯 URDF
  #   （install/ 里那份是它的符号链接）⇒ 测试前 arm_description 得先构建过。
  ament_add_gtest(test_dm_gravity test/test_dm_gravity.cpp)
  target_link_libraries(test_dm_gravity dm_hardware)
  target_compile_definitions(test_dm_gravity PRIVATE
    ARM_DYN_URDF="${CMAKE_BINARY_DIR}/../arm_description/arm_dynamics.urdf"
    ARM_DESC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../arm_description")
endif()

ament_package()
