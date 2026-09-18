// generated from rosidl_generator_c/resource/idl__struct.h.em
// with input from rebotarm_msgs:msg/ArmStatus.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "rebotarm_msgs/msg/arm_status.h"


#ifndef REBOTARM_MSGS__MSG__DETAIL__ARM_STATUS__STRUCT_H_
#define REBOTARM_MSGS__MSG__DETAIL__ARM_STATUS__STRUCT_H_

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Constants defined in the message

// Include directives for member types
// Member 'header'
#include "std_msgs/msg/detail/header__struct.h"
// Member 'mode'
// Member 'state_machine'
// Member 'joint_names'
// Member 'error_codes'
#include "rosidl_runtime_c/string.h"
// Member 'per_joint_status_code'
#include "rosidl_runtime_c/primitives_sequence.h"

/// Struct defined in msg/ArmStatus in the package rebotarm_msgs.
typedef struct rebotarm_msgs__msg__ArmStatus
{
  std_msgs__msg__Header header;
  rosidl_runtime_c__String mode;
  bool enabled;
  bool control_loop_active;
  rosidl_runtime_c__String state_machine;
  rosidl_runtime_c__String__Sequence joint_names;
  rosidl_runtime_c__uint8__Sequence per_joint_status_code;
  rosidl_runtime_c__String__Sequence error_codes;
} rebotarm_msgs__msg__ArmStatus;

// Struct for a sequence of rebotarm_msgs__msg__ArmStatus.
typedef struct rebotarm_msgs__msg__ArmStatus__Sequence
{
  rebotarm_msgs__msg__ArmStatus * data;
  /// The number of valid items in data
  size_t size;
  /// The number of allocated items in data
  size_t capacity;
} rebotarm_msgs__msg__ArmStatus__Sequence;

#ifdef __cplusplus
}
#endif

#endif  // REBOTARM_MSGS__MSG__DETAIL__ARM_STATUS__STRUCT_H_
