// generated from rosidl_typesupport_fastrtps_c/resource/idl__rosidl_typesupport_fastrtps_c.h.em
// with input from rebotarm_msgs:msg/JointPosVelCmd.idl
// generated code does not contain a copyright notice
#ifndef REBOTARM_MSGS__MSG__DETAIL__JOINT_POS_VEL_CMD__ROSIDL_TYPESUPPORT_FASTRTPS_C_H_
#define REBOTARM_MSGS__MSG__DETAIL__JOINT_POS_VEL_CMD__ROSIDL_TYPESUPPORT_FASTRTPS_C_H_


#include <stddef.h>
#include "rosidl_runtime_c/message_type_support_struct.h"
#include "rosidl_typesupport_interface/macros.h"
#include "rebotarm_msgs/msg/rosidl_typesupport_fastrtps_c__visibility_control.h"
#include "rebotarm_msgs/msg/detail/joint_pos_vel_cmd__struct.h"
#include "fastcdr/Cdr.h"

#ifdef __cplusplus
extern "C"
{
#endif

ROSIDL_TYPESUPPORT_FASTRTPS_C_PUBLIC_rebotarm_msgs
bool cdr_serialize_rebotarm_msgs__msg__JointPosVelCmd(
  const rebotarm_msgs__msg__JointPosVelCmd * ros_message,
  eprosima::fastcdr::Cdr & cdr);

ROSIDL_TYPESUPPORT_FASTRTPS_C_PUBLIC_rebotarm_msgs
bool cdr_deserialize_rebotarm_msgs__msg__JointPosVelCmd(
  eprosima::fastcdr::Cdr &,
  rebotarm_msgs__msg__JointPosVelCmd * ros_message);

ROSIDL_TYPESUPPORT_FASTRTPS_C_PUBLIC_rebotarm_msgs
size_t get_serialized_size_rebotarm_msgs__msg__JointPosVelCmd(
  const void * untyped_ros_message,
  size_t current_alignment);

ROSIDL_TYPESUPPORT_FASTRTPS_C_PUBLIC_rebotarm_msgs
size_t max_serialized_size_rebotarm_msgs__msg__JointPosVelCmd(
  bool & full_bounded,
  bool & is_plain,
  size_t current_alignment);

ROSIDL_TYPESUPPORT_FASTRTPS_C_PUBLIC_rebotarm_msgs
bool cdr_serialize_key_rebotarm_msgs__msg__JointPosVelCmd(
  const rebotarm_msgs__msg__JointPosVelCmd * ros_message,
  eprosima::fastcdr::Cdr & cdr);

ROSIDL_TYPESUPPORT_FASTRTPS_C_PUBLIC_rebotarm_msgs
size_t get_serialized_size_key_rebotarm_msgs__msg__JointPosVelCmd(
  const void * untyped_ros_message,
  size_t current_alignment);

ROSIDL_TYPESUPPORT_FASTRTPS_C_PUBLIC_rebotarm_msgs
size_t max_serialized_size_key_rebotarm_msgs__msg__JointPosVelCmd(
  bool & full_bounded,
  bool & is_plain,
  size_t current_alignment);

ROSIDL_TYPESUPPORT_FASTRTPS_C_PUBLIC_rebotarm_msgs
const rosidl_message_type_support_t *
ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_fastrtps_c, rebotarm_msgs, msg, JointPosVelCmd)();

#ifdef __cplusplus
}
#endif

#endif  // REBOTARM_MSGS__MSG__DETAIL__JOINT_POS_VEL_CMD__ROSIDL_TYPESUPPORT_FASTRTPS_C_H_
