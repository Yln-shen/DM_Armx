// generated from rosidl_generator_c/resource/idl__struct.h.em
// with input from rebotarm_msgs:srv/MoveToPoseIK.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "rebotarm_msgs/srv/move_to_pose_ik.h"


#ifndef REBOTARM_MSGS__SRV__DETAIL__MOVE_TO_POSE_IK__STRUCT_H_
#define REBOTARM_MSGS__SRV__DETAIL__MOVE_TO_POSE_IK__STRUCT_H_

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


// Constants defined in the message

// Include directives for member types
// Member 'target_pose'
#include "geometry_msgs/msg/detail/pose__struct.h"

/// Struct defined in srv/MoveToPoseIK in the package rebotarm_msgs.
typedef struct rebotarm_msgs__srv__MoveToPoseIK_Request
{
  geometry_msgs__msg__Pose target_pose;
} rebotarm_msgs__srv__MoveToPoseIK_Request;

// Struct for a sequence of rebotarm_msgs__srv__MoveToPoseIK_Request.
typedef struct rebotarm_msgs__srv__MoveToPoseIK_Request__Sequence
{
  rebotarm_msgs__srv__MoveToPoseIK_Request * data;
  /// The number of valid items in data
  size_t size;
  /// The number of allocated items in data
  size_t capacity;
} rebotarm_msgs__srv__MoveToPoseIK_Request__Sequence;

// Constants defined in the message

// Include directives for member types
// Member 'message'
#include "rosidl_runtime_c/string.h"
// Member 'q_solution'
#include "rosidl_runtime_c/primitives_sequence.h"

/// Struct defined in srv/MoveToPoseIK in the package rebotarm_msgs.
typedef struct rebotarm_msgs__srv__MoveToPoseIK_Response
{
  bool success;
  rosidl_runtime_c__String message;
  rosidl_runtime_c__double__Sequence q_solution;
} rebotarm_msgs__srv__MoveToPoseIK_Response;

// Struct for a sequence of rebotarm_msgs__srv__MoveToPoseIK_Response.
typedef struct rebotarm_msgs__srv__MoveToPoseIK_Response__Sequence
{
  rebotarm_msgs__srv__MoveToPoseIK_Response * data;
  /// The number of valid items in data
  size_t size;
  /// The number of allocated items in data
  size_t capacity;
} rebotarm_msgs__srv__MoveToPoseIK_Response__Sequence;

// Constants defined in the message

// Include directives for member types
// Member 'info'
#include "service_msgs/msg/detail/service_event_info__struct.h"

// constants for array fields with an upper bound
// request
enum
{
  rebotarm_msgs__srv__MoveToPoseIK_Event__request__MAX_SIZE = 1
};
// response
enum
{
  rebotarm_msgs__srv__MoveToPoseIK_Event__response__MAX_SIZE = 1
};

/// Struct defined in srv/MoveToPoseIK in the package rebotarm_msgs.
typedef struct rebotarm_msgs__srv__MoveToPoseIK_Event
{
  service_msgs__msg__ServiceEventInfo info;
  rebotarm_msgs__srv__MoveToPoseIK_Request__Sequence request;
  rebotarm_msgs__srv__MoveToPoseIK_Response__Sequence response;
} rebotarm_msgs__srv__MoveToPoseIK_Event;

// Struct for a sequence of rebotarm_msgs__srv__MoveToPoseIK_Event.
typedef struct rebotarm_msgs__srv__MoveToPoseIK_Event__Sequence
{
  rebotarm_msgs__srv__MoveToPoseIK_Event * data;
  /// The number of valid items in data
  size_t size;
  /// The number of allocated items in data
  size_t capacity;
} rebotarm_msgs__srv__MoveToPoseIK_Event__Sequence;

#ifdef __cplusplus
}
#endif

#endif  // REBOTARM_MSGS__SRV__DETAIL__MOVE_TO_POSE_IK__STRUCT_H_
