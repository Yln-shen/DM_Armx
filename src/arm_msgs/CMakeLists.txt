cmake_minimum_required(VERSION 3.10)
project(arm_msgs)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# find dependencies
find_package(ament_cmake REQUIRED)
find_package(rosidl_default_generators REQUIRED)
find_package(std_msgs REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(builtin_interfaces REQUIRED)

rosidl_generate_interfaces(${PROJECT_NAME}
  "msg/JointMotorCmd.msg"
  "msg/JointMotorState.msg"
  "msg/HardwareStatus.msg"
  "msg/ArmStatus.msg"
  "action/MoveToPose.action"
  # 运动接口层的**唯一**对外契约（D-M1，设计见 docs/MOTION_ARCH.md §4）。
  # ⚠️ 只用 geometry_msgs + builtin，所以 DEPENDENCIES 不用改。
  "action/ExecuteMotion.action"
  # ── 管理层（arm_manage）的对外接口；插件级两个是**内部接口**，正常只由 arm_manage 调用 ──
  # 见 docs/MANAGE_ARCH.md §4.1：M-6（对外只有 3 个服务、不做状态查询服务）
  "srv/SetEnabled.srv"              # 插件：运行期使能/失能（M-1，因为 enable_on_activate 是构建期参数）
  "srv/HardwareEmergencyStop.srv"   # 插件：立即失能，不碰寄存器、不恢复（M-9）
  "srv/ArmOn.srv"                   # 管理层：分级上电一步化（含控制器顺序，M-3）
  "srv/SafeDisarm.srv"              # 管理层：先收臂到折叠位再失能（M-8 失败⇒保持使能）
  "srv/ManageEmergencyStop.srv"     # 管理层：急停 = 取消目标 + 立即失能（与 SafeDisarm 语义相反）
  # ⚠️ builtin_interfaces 是 HardwareStatus/ArmStatus 的 stamp 需要的（find_package 早就有，
  #    但之前不在 DEPENDENCIES 里 ⇒ 用 builtin_interfaces/Time 就必须补）
  DEPENDENCIES geometry_msgs builtin_interfaces
)

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  # the following line skips the linter which checks for copyrights
  # comment the line when a copyright and license is added to all source files
  set(ament_cmake_copyright_FOUND TRUE)
  # the following line skips cpplint (only works in a git repo)
  # comment the line when this package is in a git repo and when
  # a copyright and license is added to all source files
  set(ament_cmake_cpplint_FOUND TRUE)
  ament_lint_auto_find_test_dependencies()
endif()

ament_package()
