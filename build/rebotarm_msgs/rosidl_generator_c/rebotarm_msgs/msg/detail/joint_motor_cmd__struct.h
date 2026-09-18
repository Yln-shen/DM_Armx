// generated from rosidl_generator_c/resource/idl__struct.h.em
// with input from rebotarm_msgs:msg/JointMotorCmd.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "rebotarm_msgs/msg/joint_motor_cmd.h"


#ifndef REBOTARM_MSGS__MSG__DETAIL__JOINT_MOTOR_CMD__STRUCT_H_
#define REBOTARM_MSGS__MSG__DETAIL__JOINT_MOTOR_CMD__STRUCT_H_

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Constants defined in the message

/// Constant 'MODE_MIT'.
enum
{
  rebotarm_msgs__msg__JointMotorCmd__MODE_MIT = 0
};

/// Constant 'MODE_POS_VEL'.
enum
{
  rebotarm_msgs__msg__JointMotorCmd__MODE_POS_VEL = 1
};

/// Constant 'MODE_VEL'.
enum
{
  rebotarm_msgs__msg__JointMotorCmd__MODE_VEL = 2
};

// Include directives for member types
// Member 'stamp'
#include "builtin_interfaces/msg/detail/time__struct.h"

/// Struct defined in msg/JointMotorCmd in the package rebotarm_msgs.
typedef struct rebotarm_msgs__msg__JointMotorCmd
{
  uint8_t mode;
  bool use_pos;
  bool use_vel;
  bool use_kp;
  bool use_kd;
  bool use_tau;
  bool use_vlim;
  double pos;
  double vel;
  double kp;
  double kd;
  double tau;
  double vlim;
  builtin_interfaces__msg__Time stamp;
} rebotarm_msgs__msg__JointMotorCmd;

// Struct for a sequence of rebotarm_msgs__msg__JointMotorCmd.
typedef struct rebotarm_msgs__msg__JointMotorCmd__Sequence
{
  rebotarm_msgs__msg__JointMotorCmd * data;
  /// The number of valid items in data
  size_t size;
  /// The number of allocated items in data
  size_t capacity;
} rebotarm_msgs__msg__JointMotorCmd__Sequence;

#ifdef __cplusplus
}
#endif

#endif  // REBOTARM_MSGS__MSG__DETAIL__JOINT_MOTOR_CMD__STRUCT_H_
