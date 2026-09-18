// generated from rosidl_generator_c/resource/idl__struct.h.em
// with input from rebotarm_msgs:srv/SetMode.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "rebotarm_msgs/srv/set_mode.h"


#ifndef REBOTARM_MSGS__SRV__DETAIL__SET_MODE__STRUCT_H_
#define REBOTARM_MSGS__SRV__DETAIL__SET_MODE__STRUCT_H_

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


// Constants defined in the message

// Include directives for member types
// Member 'mode'
#include "rosidl_runtime_c/string.h"

/// Struct defined in srv/SetMode in the package rebotarm_msgs.
typedef struct rebotarm_msgs__srv__SetMode_Request
{
  rosidl_runtime_c__String mode;
} rebotarm_msgs__srv__SetMode_Request;

// Struct for a sequence of rebotarm_msgs__srv__SetMode_Request.
typedef struct rebotarm_msgs__srv__SetMode_Request__Sequence
{
  rebotarm_msgs__srv__SetMode_Request * data;
  /// The number of valid items in data
  size_t size;
  /// The number of allocated items in data
  size_t capacity;
} rebotarm_msgs__srv__SetMode_Request__Sequence;

// Constants defined in the message

// Include directives for member types
// Member 'message'
// already included above
// #include "rosidl_runtime_c/string.h"

/// Struct defined in srv/SetMode in the package rebotarm_msgs.
typedef struct rebotarm_msgs__srv__SetMode_Response
{
  bool success;
  rosidl_runtime_c__String message;
} rebotarm_msgs__srv__SetMode_Response;

// Struct for a sequence of rebotarm_msgs__srv__SetMode_Response.
typedef struct rebotarm_msgs__srv__SetMode_Response__Sequence
{
  rebotarm_msgs__srv__SetMode_Response * data;
  /// The number of valid items in data
  size_t size;
  /// The number of allocated items in data
  size_t capacity;
} rebotarm_msgs__srv__SetMode_Response__Sequence;

// Constants defined in the message

// Include directives for member types
// Member 'info'
#include "service_msgs/msg/detail/service_event_info__struct.h"

// constants for array fields with an upper bound
// request
enum
{
  rebotarm_msgs__srv__SetMode_Event__request__MAX_SIZE = 1
};
// response
enum
{
  rebotarm_msgs__srv__SetMode_Event__response__MAX_SIZE = 1
};

/// Struct defined in srv/SetMode in the package rebotarm_msgs.
typedef struct rebotarm_msgs__srv__SetMode_Event
{
  service_msgs__msg__ServiceEventInfo info;
  rebotarm_msgs__srv__SetMode_Request__Sequence request;
  rebotarm_msgs__srv__SetMode_Response__Sequence response;
} rebotarm_msgs__srv__SetMode_Event;

// Struct for a sequence of rebotarm_msgs__srv__SetMode_Event.
typedef struct rebotarm_msgs__srv__SetMode_Event__Sequence
{
  rebotarm_msgs__srv__SetMode_Event * data;
  /// The number of valid items in data
  size_t size;
  /// The number of allocated items in data
  size_t capacity;
} rebotarm_msgs__srv__SetMode_Event__Sequence;

#ifdef __cplusplus
}
#endif

#endif  // REBOTARM_MSGS__SRV__DETAIL__SET_MODE__STRUCT_H_
