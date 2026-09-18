// generated from rosidl_generator_c/resource/idl__struct.h.em
// with input from rebotarm_msgs:action/MoveToPose.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "rebotarm_msgs/action/move_to_pose.h"


#ifndef REBOTARM_MSGS__ACTION__DETAIL__MOVE_TO_POSE__STRUCT_H_
#define REBOTARM_MSGS__ACTION__DETAIL__MOVE_TO_POSE__STRUCT_H_

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

/// Struct defined in action/MoveToPose in the package rebotarm_msgs.
typedef struct rebotarm_msgs__action__MoveToPose_Goal
{
  geometry_msgs__msg__Pose target_pose;
  double duration;
} rebotarm_msgs__action__MoveToPose_Goal;

// Struct for a sequence of rebotarm_msgs__action__MoveToPose_Goal.
typedef struct rebotarm_msgs__action__MoveToPose_Goal__Sequence
{
  rebotarm_msgs__action__MoveToPose_Goal * data;
  /// The number of valid items in data
  size_t size;
  /// The number of allocated items in data
  size_t capacity;
} rebotarm_msgs__action__MoveToPose_Goal__Sequence;

// Constants defined in the message

// Include directives for member types
// Member 'message'
#include "rosidl_runtime_c/string.h"
// Member 'final_pose'
// already included above
// #include "geometry_msgs/msg/detail/pose__struct.h"

/// Struct defined in action/MoveToPose in the package rebotarm_msgs.
typedef struct rebotarm_msgs__action__MoveToPose_Result
{
  bool success;
  rosidl_runtime_c__String message;
  geometry_msgs__msg__Pose final_pose;
} rebotarm_msgs__action__MoveToPose_Result;

// Struct for a sequence of rebotarm_msgs__action__MoveToPose_Result.
typedef struct rebotarm_msgs__action__MoveToPose_Result__Sequence
{
  rebotarm_msgs__action__MoveToPose_Result * data;
  /// The number of valid items in data
  size_t size;
  /// The number of allocated items in data
  size_t capacity;
} rebotarm_msgs__action__MoveToPose_Result__Sequence;

// Constants defined in the message

// Include directives for member types
// Member 'current_pose'
// already included above
// #include "geometry_msgs/msg/detail/pose__struct.h"

/// Struct defined in action/MoveToPose in the package rebotarm_msgs.
typedef struct rebotarm_msgs__action__MoveToPose_Feedback
{
  geometry_msgs__msg__Pose current_pose;
  double progress;
  double time_elapsed;
} rebotarm_msgs__action__MoveToPose_Feedback;

// Struct for a sequence of rebotarm_msgs__action__MoveToPose_Feedback.
typedef struct rebotarm_msgs__action__MoveToPose_Feedback__Sequence
{
  rebotarm_msgs__action__MoveToPose_Feedback * data;
  /// The number of valid items in data
  size_t size;
  /// The number of allocated items in data
  size_t capacity;
} rebotarm_msgs__action__MoveToPose_Feedback__Sequence;

// Constants defined in the message

// Include directives for member types
// Member 'goal_id'
#include "unique_identifier_msgs/msg/detail/uuid__struct.h"
// Member 'goal'
#include "rebotarm_msgs/action/detail/move_to_pose__struct.h"

/// Struct defined in action/MoveToPose in the package rebotarm_msgs.
typedef struct rebotarm_msgs__action__MoveToPose_SendGoal_Request
{
  unique_identifier_msgs__msg__UUID goal_id;
  rebotarm_msgs__action__MoveToPose_Goal goal;
} rebotarm_msgs__action__MoveToPose_SendGoal_Request;

// Struct for a sequence of rebotarm_msgs__action__MoveToPose_SendGoal_Request.
typedef struct rebotarm_msgs__action__MoveToPose_SendGoal_Request__Sequence
{
  rebotarm_msgs__action__MoveToPose_SendGoal_Request * data;
  /// The number of valid items in data
  size_t size;
  /// The number of allocated items in data
  size_t capacity;
} rebotarm_msgs__action__MoveToPose_SendGoal_Request__Sequence;

// Constants defined in the message

// Include directives for member types
// Member 'stamp'
#include "builtin_interfaces/msg/detail/time__struct.h"

/// Struct defined in action/MoveToPose in the package rebotarm_msgs.
typedef struct rebotarm_msgs__action__MoveToPose_SendGoal_Response
{
  bool accepted;
  builtin_interfaces__msg__Time stamp;
} rebotarm_msgs__action__MoveToPose_SendGoal_Response;

// Struct for a sequence of rebotarm_msgs__action__MoveToPose_SendGoal_Response.
typedef struct rebotarm_msgs__action__MoveToPose_SendGoal_Response__Sequence
{
  rebotarm_msgs__action__MoveToPose_SendGoal_Response * data;
  /// The number of valid items in data
  size_t size;
  /// The number of allocated items in data
  size_t capacity;
} rebotarm_msgs__action__MoveToPose_SendGoal_Response__Sequence;

// Constants defined in the message

// Include directives for member types
// Member 'info'
#include "service_msgs/msg/detail/service_event_info__struct.h"

// constants for array fields with an upper bound
// request
enum
{
  rebotarm_msgs__action__MoveToPose_SendGoal_Event__request__MAX_SIZE = 1
};
// response
enum
{
  rebotarm_msgs__action__MoveToPose_SendGoal_Event__response__MAX_SIZE = 1
};

/// Struct defined in action/MoveToPose in the package rebotarm_msgs.
typedef struct rebotarm_msgs__action__MoveToPose_SendGoal_Event
{
  service_msgs__msg__ServiceEventInfo info;
  rebotarm_msgs__action__MoveToPose_SendGoal_Request__Sequence request;
  rebotarm_msgs__action__MoveToPose_SendGoal_Response__Sequence response;
} rebotarm_msgs__action__MoveToPose_SendGoal_Event;

// Struct for a sequence of rebotarm_msgs__action__MoveToPose_SendGoal_Event.
typedef struct rebotarm_msgs__action__MoveToPose_SendGoal_Event__Sequence
{
  rebotarm_msgs__action__MoveToPose_SendGoal_Event * data;
  /// The number of valid items in data
  size_t size;
  /// The number of allocated items in data
  size_t capacity;
} rebotarm_msgs__action__MoveToPose_SendGoal_Event__Sequence;

// Constants defined in the message

// Include directives for member types
// Member 'goal_id'
// already included above
// #include "unique_identifier_msgs/msg/detail/uuid__struct.h"

/// Struct defined in action/MoveToPose in the package rebotarm_msgs.
typedef struct rebotarm_msgs__action__MoveToPose_GetResult_Request
{
  unique_identifier_msgs__msg__UUID goal_id;
} rebotarm_msgs__action__MoveToPose_GetResult_Request;

// Struct for a sequence of rebotarm_msgs__action__MoveToPose_GetResult_Request.
typedef struct rebotarm_msgs__action__MoveToPose_GetResult_Request__Sequence
{
  rebotarm_msgs__action__MoveToPose_GetResult_Request * data;
  /// The number of valid items in data
  size_t size;
  /// The number of allocated items in data
  size_t capacity;
} rebotarm_msgs__action__MoveToPose_GetResult_Request__Sequence;

// Constants defined in the message

// Include directives for member types
// Member 'result'
// already included above
// #include "rebotarm_msgs/action/detail/move_to_pose__struct.h"

/// Struct defined in action/MoveToPose in the package rebotarm_msgs.
typedef struct rebotarm_msgs__action__MoveToPose_GetResult_Response
{
  int8_t status;
  rebotarm_msgs__action__MoveToPose_Result result;
} rebotarm_msgs__action__MoveToPose_GetResult_Response;

// Struct for a sequence of rebotarm_msgs__action__MoveToPose_GetResult_Response.
typedef struct rebotarm_msgs__action__MoveToPose_GetResult_Response__Sequence
{
  rebotarm_msgs__action__MoveToPose_GetResult_Response * data;
  /// The number of valid items in data
  size_t size;
  /// The number of allocated items in data
  size_t capacity;
} rebotarm_msgs__action__MoveToPose_GetResult_Response__Sequence;

// Constants defined in the message

// Include directives for member types
// Member 'info'
// already included above
// #include "service_msgs/msg/detail/service_event_info__struct.h"

// constants for array fields with an upper bound
// request
enum
{
  rebotarm_msgs__action__MoveToPose_GetResult_Event__request__MAX_SIZE = 1
};
// response
enum
{
  rebotarm_msgs__action__MoveToPose_GetResult_Event__response__MAX_SIZE = 1
};

/// Struct defined in action/MoveToPose in the package rebotarm_msgs.
typedef struct rebotarm_msgs__action__MoveToPose_GetResult_Event
{
  service_msgs__msg__ServiceEventInfo info;
  rebotarm_msgs__action__MoveToPose_GetResult_Request__Sequence request;
  rebotarm_msgs__action__MoveToPose_GetResult_Response__Sequence response;
} rebotarm_msgs__action__MoveToPose_GetResult_Event;

// Struct for a sequence of rebotarm_msgs__action__MoveToPose_GetResult_Event.
typedef struct rebotarm_msgs__action__MoveToPose_GetResult_Event__Sequence
{
  rebotarm_msgs__action__MoveToPose_GetResult_Event * data;
  /// The number of valid items in data
  size_t size;
  /// The number of allocated items in data
  size_t capacity;
} rebotarm_msgs__action__MoveToPose_GetResult_Event__Sequence;

// Constants defined in the message

// Include directives for member types
// Member 'goal_id'
// already included above
// #include "unique_identifier_msgs/msg/detail/uuid__struct.h"
// Member 'feedback'
// already included above
// #include "rebotarm_msgs/action/detail/move_to_pose__struct.h"

/// Struct defined in action/MoveToPose in the package rebotarm_msgs.
typedef struct rebotarm_msgs__action__MoveToPose_FeedbackMessage
{
  unique_identifier_msgs__msg__UUID goal_id;
  rebotarm_msgs__action__MoveToPose_Feedback feedback;
} rebotarm_msgs__action__MoveToPose_FeedbackMessage;

// Struct for a sequence of rebotarm_msgs__action__MoveToPose_FeedbackMessage.
typedef struct rebotarm_msgs__action__MoveToPose_FeedbackMessage__Sequence
{
  rebotarm_msgs__action__MoveToPose_FeedbackMessage * data;
  /// The number of valid items in data
  size_t size;
  /// The number of allocated items in data
  size_t capacity;
} rebotarm_msgs__action__MoveToPose_FeedbackMessage__Sequence;

#ifdef __cplusplus
}
#endif

#endif  // REBOTARM_MSGS__ACTION__DETAIL__MOVE_TO_POSE__STRUCT_H_
