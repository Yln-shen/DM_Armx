cmake_minimum_required(VERSION 3.16)
project(arm_controllers)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()
if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
# pinocchio 算 τ_g。⚠️ 必须用 find_package（不能手写 -I）：它的 imported target 带一批**必须**的
# 宏（BOOST_MPL_CFG_NO_PREPROCESSED_HEADERS / BOOST_MPL_LIMIT_LIST_SIZE=30 …），少了就会在
# boost::variant 处报 "wrong number of template arguments (25)"。
# `pinocchio::pinocchio_parsers` 是独立的 .so（URDF 解析在里面），别只链 pinocchio::pinocchio。
find_package(pinocchio REQUIRED)

# ── 控制律核：重力模型 + 前馈律（**不依赖 rclcpp**，可脱开 ROS 单测）──
add_library(arm_control_law
  src/gravity_model.cpp
  src/gravity_ff.cpp
)
target_include_directories(arm_control_law PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(arm_control_law PUBLIC
  pinocchio::pinocchio
  pinocchio::pinocchio_parsers)

install(TARGETS arm_control_law
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin)
install(DIRECTORY include/ DESTINATION include)

# ── 测试（LAYERS §8.1：控制律只喂状态数组、只看算出的 effort；不需要串口、不需要假电机）──
if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)

  # 与**已跑通的 MuJoCo** 逐点对拍（另一条完全独立的实现路径）—— 守住 sign 那一段符号链。
  # ⚠️ 这个用例从 motor_driver_hardware 迁来（R1 删 dm_gravity 时一起删的，R2 必须补回）。
  # ⚠️ 它读的是**构建时生成**的 arm_description/arm_dynamics.urdf ⇒ 测试前先构建 arm_description。
  ament_add_gtest(test_gravity_model test/test_gravity_model.cpp)
  target_link_libraries(test_gravity_model arm_control_law)
  target_compile_definitions(test_gravity_model PRIVATE
    ARM_DYN_URDF="${CMAKE_BINARY_DIR}/../arm_description/arm_dynamics.urdf"
    ARM_DESC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../arm_description")

  # 前馈律：纯输入→输出（守卫宽限/防抖/锁存、积分门控与饱和、摩擦只在有速度参考时给、NaN 语义）
  ament_add_gtest(test_gravity_ff test/test_gravity_ff.cpp)
  target_link_libraries(test_gravity_ff arm_control_law)
  target_compile_definitions(test_gravity_ff PRIVATE
    ARM_DYN_URDF="${CMAKE_BINARY_DIR}/../arm_description/arm_dynamics.urdf")
endif()

ament_package()
