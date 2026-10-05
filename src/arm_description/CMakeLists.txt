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

ament_package()
