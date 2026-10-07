cmake_minimum_required(VERSION 3.10)
project(arm_description)

find_package(ament_cmake REQUIRED)

# 纯数据包：几何、xacro、launch、rviz 配置、模型对齐参数、第三方参考与许可
install(DIRECTORY meshes urdf launch rviz config reference
        DESTINATION share/${PROJECT_NAME})

# ── 动力学（重力前馈）用的一份"纯展开" URDF ──
# 与运行期那份**同一个 xacro** 展开而来 ⇒ 惯性与运动学同源；只是没有 use_mock 那些分支。
# ⚠️ joint_cfg / align_cfg 刻意用**源码路径**覆盖默认的 `$(find motor_driver)/…`：
#    构建期 motor_driver 未必已经安装，用 $(find) 会引入构建顺序依赖。
set(DYN_URDF "${CMAKE_CURRENT_BINARY_DIR}/arm_dynamics.urdf")
set(_xacro_in "${CMAKE_CURRENT_SOURCE_DIR}/urdf/arm.urdf.xacro")
set(_joint_yaml "${CMAKE_CURRENT_SOURCE_DIR}/../motor_driver/config/joint.yaml")
set(_align_yaml "${CMAKE_CURRENT_SOURCE_DIR}/config/align.yaml")
set(_mit_yaml "${CMAKE_CURRENT_SOURCE_DIR}/config/mit_gains.yaml")
file(GLOB _xacro_incs "${CMAKE_CURRENT_SOURCE_DIR}/urdf/inc/*.xacro")
add_custom_command(
  OUTPUT "${DYN_URDF}"
  COMMAND xacro "${_xacro_in}"
          use_gripper:=false use_mock:=true
          joint_cfg:=${_joint_yaml} align_cfg:=${_align_yaml} mit_gains_cfg:=${_mit_yaml}
          -o "${DYN_URDF}"
  DEPENDS "${_xacro_in}" ${_xacro_incs} "${_joint_yaml}" "${_align_yaml}" "${_mit_yaml}"
  COMMENT "生成动力学用 URDF（share/${PROJECT_NAME}/urdf/arm.urdf）"
  VERBATIM)
add_custom_target(arm_dynamics_urdf ALL DEPENDS "${DYN_URDF}")
install(FILES "${DYN_URDF}" DESTINATION share/${PROJECT_NAME}/urdf RENAME arm.urdf)

# ── 同一份 URDF 的"辨识版"：把重力辨识出来的质量/质心替换进去 ──
# 上游 CAD 的惯量与这台实机不符（实测 j2/j3/j4 的重力项差 7.8× / 0.76× / 0.32×）
# ⇒ 重力前馈用这份。只改质量与质心；转动惯量对重力无贡献，原样保留。
set(_ident_yaml "${CMAKE_CURRENT_SOURCE_DIR}/config/gravity_identified.yaml")
set(_apply_script "${CMAKE_CURRENT_SOURCE_DIR}/scripts/apply_identified_inertia.py")
set(IDENT_URDF "${CMAKE_CURRENT_BINARY_DIR}/arm_identified.urdf")
add_custom_command(
  OUTPUT "${IDENT_URDF}"
  COMMAND python3 "${_apply_script}" "${DYN_URDF}" "${_ident_yaml}" "${IDENT_URDF}"
  DEPENDS "${DYN_URDF}" "${_ident_yaml}" "${_apply_script}"
  COMMENT "生成辨识版动力学 URDF（share/${PROJECT_NAME}/urdf/arm_identified.urdf）"
  VERBATIM)
add_custom_target(arm_identified_urdf ALL DEPENDS "${IDENT_URDF}")
install(FILES "${IDENT_URDF}" DESTINATION share/${PROJECT_NAME}/urdf)

# ── 带夹爪的辨识版：给"以后真装上夹爪"那条链用（`gravity_ff` 的 `dyn_model:=` 切）──
# 空载那份（arm_identified.urdf）的 `link6 = 0.7718` 是**空载**辨识值，夹爪当时不在台上；
# 这份把 `use_gripper:=true` 的几何一并展开，夹爪那几个 link 的质量/质心由
# gravity_identified.yaml 的 `gripper:` 段给（上游 CAD 名义值，**没辨识过**，只当占位）。
# ⚠️ `--gripper` 还让 `end_link`（夹爪底座）**保留**自己的惯量 —— 带夹爪时它不再被并进 link6。
set(DYN_URDF_GRIPPER "${CMAKE_CURRENT_BINARY_DIR}/arm_dynamics_gripper.urdf")
add_custom_command(
  OUTPUT "${DYN_URDF_GRIPPER}"
  COMMAND xacro "${_xacro_in}"
          use_gripper:=true use_mock:=true
          joint_cfg:=${_joint_yaml} align_cfg:=${_align_yaml} mit_gains_cfg:=${_mit_yaml}
          -o "${DYN_URDF_GRIPPER}"
  DEPENDS "${_xacro_in}" ${_xacro_incs} "${_joint_yaml}" "${_align_yaml}" "${_mit_yaml}"
  COMMENT "生成带夹爪的动力学 URDF（中间产物）"
  VERBATIM)
set(IDENT_URDF_GRIPPER "${CMAKE_CURRENT_BINARY_DIR}/arm_identified_gripper.urdf")
add_custom_command(
  OUTPUT "${IDENT_URDF_GRIPPER}"
  COMMAND python3 "${_apply_script}" "${DYN_URDF_GRIPPER}" "${_ident_yaml}"
          "${IDENT_URDF_GRIPPER}" --gripper
  DEPENDS "${DYN_URDF_GRIPPER}" "${_ident_yaml}" "${_apply_script}"
  COMMENT "生成带夹爪的辨识版 URDF（share/${PROJECT_NAME}/urdf/arm_identified_gripper.urdf）"
  VERBATIM)
add_custom_target(arm_identified_gripper_urdf ALL DEPENDS "${IDENT_URDF_GRIPPER}")
install(FILES "${IDENT_URDF_GRIPPER}" DESTINATION share/${PROJECT_NAME}/urdf)

# ── 给**应用层**用的关节限位头文件（模型坐标）──
# 为什么要有它：`arm_controller`(JTC) **不校验关节限位** —— 越界目标照执行，臂会顶在
# 机械硬限位上堵转到超时（真机实测 joint2 +99 rad ⇒ 堵转 60 s、effort 22.5 N·m、线圈 62 ℃，
# 且全程 err=0 无故障码）。应用层发目标前必须自己查限位，而限位只在
# joint.yaml（关节侧）+ align.yaml（模型对齐）里 ⇒ 构建期算成常量给他们。
# ⚠️ 依赖 ${DYN_URDF} 是为了**顺带对拍**：PRIVATE 的产物若与 URDF 的 <limit> 不一致就构建失败，
#    这样"xacro 里硬编码的字面量"与"真源算出来的值"漂了会当场暴露。
set(_limits_hpp "${CMAKE_CURRENT_BINARY_DIR}/include/arm_description/joint_limits.hpp")
set(_limits_script "${CMAKE_CURRENT_SOURCE_DIR}/scripts/gen_joint_limits_header.py")
add_custom_command(
  OUTPUT "${_limits_hpp}"
  COMMAND python3 "${_limits_script}"
          --joints "${_joint_yaml}" --align "${_align_yaml}"
          --out "${_limits_hpp}" --verify-urdf "${DYN_URDF}"
  DEPENDS "${_limits_script}" "${_joint_yaml}" "${_align_yaml}" "${DYN_URDF}"
  COMMENT "生成关节限位头文件（模型坐标，share/${PROJECT_NAME}/include/…）"
  VERBATIM)
add_custom_target(arm_joint_limits_header ALL DEPENDS "${_limits_hpp}")

# 导出给应用层包用：ament 标准布局 ⇒ install/<pkg>/include/arm_description/joint_limits.hpp
# ⚠️ 只 ament_export_include_directories 不够：那样下游 find_package 得到的是空配置，
#    既拿不到 -I 也**建立不了构建顺序**（arm_application 会先于本包编译 ⇒ 找不到头文件）。
#    做成 INTERFACE 库并 export target，下游 target_link_libraries 一次拿到两者。
add_library(${PROJECT_NAME} INTERFACE)
target_include_directories(${PROJECT_NAME} INTERFACE
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>"
  "$<INSTALL_INTERFACE:include>")
install(DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/include/"
        DESTINATION include)
install(TARGETS ${PROJECT_NAME} EXPORT export_${PROJECT_NAME})
ament_export_targets(export_${PROJECT_NAME} HAS_LIBRARY_TARGET)
ament_export_dependencies(ament_cmake)

ament_package()
