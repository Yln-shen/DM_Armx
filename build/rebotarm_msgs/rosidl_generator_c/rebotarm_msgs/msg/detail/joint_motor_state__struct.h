// generated from rosidl_generator_c/resource/idl__struct.h.em
// with input from rebotarm_msgs:msg/JointMotorState.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "rebotarm_msgs/msg/joint_motor_state.h"


#ifndef REBOTARM_MSGS__MSG__DETAIL__JOINT_MOTOR_STATE__STRUCT_H_
#define REBOTARM_MSGS__MSG__DETAIL__JOINT_MOTOR_STATE__STRUCT_H_

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
// Member 'joint_name'
#include "rosidl_runtime_c/string.h"

/// Struct defined in msg/JointMotorState in the package rebotarm_msgs.
typedef struct rebotarm_msgs__msg__JointMotorState
{
  std_msgs__msg__Header header;
  rosidl_runtime_c__String joint_name;
  double position;
  double velocity;
  double torque;
  uint8_t status_code;
} rebotarm_msgs__msg__JointMotorState;

// Struct for a sequence of rebotarm_msgs__msg__JointMotorState.
typedef struct rebotarm_msgs__msg__JointMotorState__Sequence
{
  rebotarm_msgs__msg__JointMotorState * data;
  /// The number of valid items in data
  size_t size;
  /// The number of allocated items in data
  size_t capacity;
} rebotarm_msgs__msg__JointMotorState__Sequence;

#ifdef __cplusplus
}
#endif

#endif  // REBOTARM_MSGS__MSG__DETAIL__JOINT_MOTOR_STATE__STRUCT_H_
