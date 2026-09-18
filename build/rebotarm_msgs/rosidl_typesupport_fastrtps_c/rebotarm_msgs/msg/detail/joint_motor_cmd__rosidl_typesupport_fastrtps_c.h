// generated from rosidl_typesupport_fastrtps_c/resource/idl__rosidl_typesupport_fastrtps_c.h.em
// with input from rebotarm_msgs:msg/JointMotorCmd.idl
// generated code does not contain a copyright notice
#ifndef REBOTARM_MSGS__MSG__DETAIL__JOINT_MOTOR_CMD__ROSIDL_TYPESUPPORT_FASTRTPS_C_H_
#define REBOTARM_MSGS__MSG__DETAIL__JOINT_MOTOR_CMD__ROSIDL_TYPESUPPORT_FASTRTPS_C_H_


#include <stddef.h>
#include "rosidl_runtime_c/message_type_support_struct.h"
#include "rosidl_typesupport_interface/macros.h"
#include "rebotarm_msgs/msg/rosidl_typesupport_fastrtps_c__visibility_control.h"
#include "rebotarm_msgs/msg/detail/joint_motor_cmd__struct.h"
#include "fastcdr/Cdr.h"

#ifdef __cplusplus
extern "C"
{
#endif

ROSIDL_TYPESUPPORT_FASTRTPS_C_PUBLIC_rebotarm_msgs
bool cdr_serialize_rebotarm_msgs__msg__JointMotorCmd(
  const rebotarm_msgs__msg__JointMotorCmd * ros_message,
  eprosima::fastcdr::Cdr & cdr);

ROSIDL_TYPESUPPORT_FASTRTPS_C_PUBLIC_rebotarm_msgs
bool cdr_deserialize_rebotarm_msgs__msg__JointMotorCmd(
  eprosima::fastcdr::Cdr &,
  rebotarm_msgs__msg__JointMotorCmd * ros_message);

ROSIDL_TYPESUPPORT_FASTRTPS_C_PUBLIC_rebotarm_msgs
size_t get_serialized_size_rebotarm_msgs__msg__JointMotorCmd(
  const void * untyped_ros_message,
  size_t current_alignment);

ROSIDL_TYPESUPPORT_FASTRTPS_C_PUBLIC_rebotarm_msgs
size_t max_serialized_size_rebotarm_msgs__msg__JointMotorCmd(
  bool & full_bounded,
  bool & is_plain,
  size_t current_alignment);

ROSIDL_TYPESUPPORT_FASTRTPS_C_PUBLIC_rebotarm_msgs
bool cdr_serialize_key_rebotarm_msgs__msg__JointMotorCmd(
  const rebotarm_msgs__msg__JointMotorCmd * ros_message,
  eprosima::fastcdr::Cdr & cdr);

ROSIDL_TYPESUPPORT_FASTRTPS_C_PUBLIC_rebotarm_msgs
size_t get_serialized_size_key_rebotarm_msgs__msg__JointMotorCmd(
  const void * untyped_ros_message,
  size_t current_alignment);

ROSIDL_TYPESUPPORT_FASTRTPS_C_PUBLIC_rebotarm_msgs
size_t max_serialized_size_key_rebotarm_msgs__msg__JointMotorCmd(
  bool & full_bounded,
  bool & is_plain,
  size_t current_alignment);

ROSIDL_TYPESUPPORT_FASTRTPS_C_PUBLIC_rebotarm_msgs
const rosidl_message_type_support_t *
ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_fastrtps_c, rebotarm_msgs, msg, JointMotorCmd)();

#ifdef __cplusplus
}
#endif

#endif  // REBOTARM_MSGS__MSG__DETAIL__JOINT_MOTOR_CMD__ROSIDL_TYPESUPPORT_FASTRTPS_C_H_
