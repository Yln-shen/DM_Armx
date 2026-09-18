// generated from rosidl_generator_c/resource/idl__struct.h.em
// with input from rebotarm_msgs:msg/JointPosVelCmd.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "rebotarm_msgs/msg/joint_pos_vel_cmd.h"


#ifndef REBOTARM_MSGS__MSG__DETAIL__JOINT_POS_VEL_CMD__STRUCT_H_
#define REBOTARM_MSGS__MSG__DETAIL__JOINT_POS_VEL_CMD__STRUCT_H_

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Constants defined in the message

// Include directives for member types
// Member 'stamp'
#include "builtin_interfaces/msg/detail/time__struct.h"

/// Struct defined in msg/JointPosVelCmd in the package rebotarm_msgs.
typedef struct rebotarm_msgs__msg__JointPosVelCmd
{
  double pos;
  double vlim;
  builtin_interfaces__msg__Time stamp;
} rebotarm_msgs__msg__JointPosVelCmd;

// Struct for a sequence of rebotarm_msgs__msg__JointPosVelCmd.
typedef struct rebotarm_msgs__msg__JointPosVelCmd__Sequence
{
  rebotarm_msgs__msg__JointPosVelCmd * data;
  /// The number of valid items in data
  size_t size;
  /// The number of allocated items in data
  size_t capacity;
} rebotarm_msgs__msg__JointPosVelCmd__Sequence;

#ifdef __cplusplus
}
#endif

#endif  // REBOTARM_MSGS__MSG__DETAIL__JOINT_POS_VEL_CMD__STRUCT_H_
