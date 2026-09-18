// generated from rosidl_typesupport_cpp/resource/idl__type_support.cpp.em
// with input from rebotarm_msgs:srv/SetGripper.idl
// generated code does not contain a copyright notice

#include "cstddef"
#include "rosidl_runtime_c/message_type_support_struct.h"
#include "rebotarm_msgs/srv/detail/set_gripper__functions.h"
#include "rebotarm_msgs/srv/detail/set_gripper__struct.hpp"
#include "rosidl_typesupport_cpp/identifier.hpp"
#include "rosidl_typesupport_cpp/message_type_support.hpp"
#include "rosidl_typesupport_c/type_support_map.h"
#include "rosidl_typesupport_cpp/message_type_support_dispatch.hpp"
#include "rosidl_typesupport_cpp/visibility_control.h"
#include "rosidl_typesupport_interface/macros.h"

namespace rebotarm_msgs
{

namespace srv
{

namespace rosidl_typesupport_cpp
{

typedef struct _SetGripper_Request_type_support_ids_t
{
  const char * typesupport_identifier[2];
} _SetGripper_Request_type_support_ids_t;

static const _SetGripper_Request_type_support_ids_t _SetGripper_Request_message_typesupport_ids = {
  {
    "rosidl_typesupport_fastrtps_cpp",  // ::rosidl_typesupport_fastrtps_cpp::typesupport_identifier,
    "rosidl_typesupport_introspection_cpp",  // ::rosidl_typesupport_introspection_cpp::typesupport_identifier,
  }
};

typedef struct _SetGripper_Request_type_support_symbol_names_t
{
  const char * symbol_name[2];
} _SetGripper_Request_type_support_symbol_names_t;

#define STRINGIFY_(s) #s
#define STRINGIFY(s) STRINGIFY_(s)

static const _SetGripper_Request_type_support_symbol_names_t _SetGripper_Request_message_typesupport_symbol_names = {
  {
    STRINGIFY(ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_fastrtps_cpp, rebotarm_msgs, srv, SetGripper_Request)),
    STRINGIFY(ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_introspection_cpp, rebotarm_msgs, srv, SetGripper_Request)),
  }
};

typedef struct _SetGripper_Request_type_support_data_t
{
  void * data[2];
} _SetGripper_Request_type_support_data_t;

static _SetGripper_Request_type_support_data_t _SetGripper_Request_message_typesupport_data = {
  {
    0,  // will store the shared library later
    0,  // will store the shared library later
  }
};

static const type_support_map_t _SetGripper_Request_message_typesupport_map = {
  2,
  "rebotarm_msgs",
  &_SetGripper_Request_message_typesupport_ids.typesupport_identifier[0],
  &_SetGripper_Request_message_typesupport_symbol_names.symbol_name[0],
  &_SetGripper_Request_message_typesupport_data.data[0],
};

static const rosidl_message_type_support_t SetGripper_Request_message_type_support_handle = {
  ::rosidl_typesupport_cpp::typesupport_identifier,
  reinterpret_cast<const type_support_map_t *>(&_SetGripper_Request_message_typesupport_map),
  ::rosidl_typesupport_cpp::get_message_typesupport_handle_function,
  &rebotarm_msgs__srv__SetGripper_Request__get_type_hash,
  &rebotarm_msgs__srv__SetGripper_Request__get_type_description,
  &rebotarm_msgs__srv__SetGripper_Request__get_type_description_sources,
};

}  // namespace rosidl_typesupport_cpp

}  // namespace srv

}  // namespace rebotarm_msgs

namespace rosidl_typesupport_cpp
{

template<>
ROSIDL_TYPESUPPORT_CPP_PUBLIC
const rosidl_message_type_support_t *
get_message_type_support_handle<rebotarm_msgs::srv::SetGripper_Request>()
{
  return &::rebotarm_msgs::srv::rosidl_typesupport_cpp::SetGripper_Request_message_type_support_handle;
}

#ifdef __cplusplus
extern "C"
{
#endif

ROSIDL_TYPESUPPORT_CPP_PUBLIC
const rosidl_message_type_support_t *
ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_cpp, rebotarm_msgs, srv, SetGripper_Request)() {
  return get_message_type_support_handle<rebotarm_msgs::srv::SetGripper_Request>();
}

#ifdef __cplusplus
}
#endif
}  // namespace rosidl_typesupport_cpp

// already included above
// #include "cstddef"
// already included above
// #include "rosidl_runtime_c/message_type_support_struct.h"
// already included above
// #include "rebotarm_msgs/srv/detail/set_gripper__functions.h"
// already included above
// #include "rebotarm_msgs/srv/detail/set_gripper__struct.hpp"
// already included above
// #include "rosidl_typesupport_cpp/identifier.hpp"
// already included above
// #include "rosidl_typesupport_cpp/message_type_support.hpp"
// already included above
// #include "rosidl_typesupport_c/type_support_map.h"
// already included above
// #include "rosidl_typesupport_cpp/message_type_support_dispatch.hpp"
// already included above
// #include "rosidl_typesupport_cpp/visibility_control.h"
// already included above
// #include "rosidl_typesupport_interface/macros.h"

namespace rebotarm_msgs
{

namespace srv
{

namespace rosidl_typesupport_cpp
{

typedef struct _SetGripper_Response_type_support_ids_t
{
  const char * typesupport_identifier[2];
} _SetGripper_Response_type_support_ids_t;

static const _SetGripper_Response_type_support_ids_t _SetGripper_Response_message_typesupport_ids = {
  {
    "rosidl_typesupport_fastrtps_cpp",  // ::rosidl_typesupport_fastrtps_cpp::typesupport_identifier,
    "rosidl_typesupport_introspection_cpp",  // ::rosidl_typesupport_introspection_cpp::typesupport_identifier,
  }
};

typedef struct _SetGripper_Response_type_support_symbol_names_t
{
  const char * symbol_name[2];
} _SetGripper_Response_type_support_symbol_names_t;

#define STRINGIFY_(s) #s
#define STRINGIFY(s) STRINGIFY_(s)

static const _SetGripper_Response_type_support_symbol_names_t _SetGripper_Response_message_typesupport_symbol_names = {
  {
    STRINGIFY(ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_fastrtps_cpp, rebotarm_msgs, srv, SetGripper_Response)),
    STRINGIFY(ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_introspection_cpp, rebotarm_msgs, srv, SetGripper_Response)),
  }
};

typedef struct _SetGripper_Response_type_support_data_t
{
  void * data[2];
} _SetGripper_Response_type_support_data_t;

static _SetGripper_Response_type_support_data_t _SetGripper_Response_message_typesupport_data = {
  {
    0,  // will store the shared library later
    0,  // will store the shared library later
  }
};

static const type_support_map_t _SetGripper_Response_message_typesupport_map = {
  2,
  "rebotarm_msgs",
  &_SetGripper_Response_message_typesupport_ids.typesupport_identifier[0],
  &_SetGripper_Response_message_typesupport_symbol_names.symbol_name[0],
  &_SetGripper_Response_message_typesupport_data.data[0],
};

static const rosidl_message_type_support_t SetGripper_Response_message_type_support_handle = {
  ::rosidl_typesupport_cpp::typesupport_identifier,
  reinterpret_cast<const type_support_map_t *>(&_SetGripper_Response_message_typesupport_map),
  ::rosidl_typesupport_cpp::get_message_typesupport_handle_function,
  &rebotarm_msgs__srv__SetGripper_Response__get_type_hash,
  &rebotarm_msgs__srv__SetGripper_Response__get_type_description,
  &rebotarm_msgs__srv__SetGripper_Response__get_type_description_sources,
};

}  // namespace rosidl_typesupport_cpp

}  // namespace srv

}  // namespace rebotarm_msgs

namespace rosidl_typesupport_cpp
{

template<>
ROSIDL_TYPESUPPORT_CPP_PUBLIC
const rosidl_message_type_support_t *
get_message_type_support_handle<rebotarm_msgs::srv::SetGripper_Response>()
{
  return &::rebotarm_msgs::srv::rosidl_typesupport_cpp::SetGripper_Response_message_type_support_handle;
}

#ifdef __cplusplus
extern "C"
{
#endif

ROSIDL_TYPESUPPORT_CPP_PUBLIC
const rosidl_message_type_support_t *
ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_cpp, rebotarm_msgs, srv, SetGripper_Response)() {
  return get_message_type_support_handle<rebotarm_msgs::srv::SetGripper_Response>();
}

#ifdef __cplusplus
}
#endif
}  // namespace rosidl_typesupport_cpp

// already included above
// #include "cstddef"
// already included above
// #include "rosidl_runtime_c/message_type_support_struct.h"
// already included above
// #include "rebotarm_msgs/srv/detail/set_gripper__functions.h"
// already included above
// #include "rebotarm_msgs/srv/detail/set_gripper__struct.hpp"
// already included above
// #include "rosidl_typesupport_cpp/identifier.hpp"
// already included above
// #include "rosidl_typesupport_cpp/message_type_support.hpp"
// already included above
// #include "rosidl_typesupport_c/type_support_map.h"
// already included above
// #include "rosidl_typesupport_cpp/message_type_support_dispatch.hpp"
// already included above
// #include "rosidl_typesupport_cpp/visibility_control.h"
// already included above
// #include "rosidl_typesupport_interface/macros.h"

namespace rebotarm_msgs
{

namespace srv
{

namespace rosidl_typesupport_cpp
{

typedef struct _SetGripper_Event_type_support_ids_t
{
  const char * typesupport_identifier[2];
} _SetGripper_Event_type_support_ids_t;

static const _SetGripper_Event_type_support_ids_t _SetGripper_Event_message_typesupport_ids = {
  {
    "rosidl_typesupport_fastrtps_cpp",  // ::rosidl_typesupport_fastrtps_cpp::typesupport_identifier,
    "rosidl_typesupport_introspection_cpp",  // ::rosidl_typesupport_introspection_cpp::typesupport_identifier,
  }
};

typedef struct _SetGripper_Event_type_support_symbol_names_t
{
  const char * symbol_name[2];
} _SetGripper_Event_type_support_symbol_names_t;

#define STRINGIFY_(s) #s
#define STRINGIFY(s) STRINGIFY_(s)

static const _SetGripper_Event_type_support_symbol_names_t _SetGripper_Event_message_typesupport_symbol_names = {
  {
    STRINGIFY(ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_fastrtps_cpp, rebotarm_msgs, srv, SetGripper_Event)),
    STRINGIFY(ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_introspection_cpp, rebotarm_msgs, srv, SetGripper_Event)),
  }
};

typedef struct _SetGripper_Event_type_support_data_t
{
  void * data[2];
} _SetGripper_Event_type_support_data_t;

static _SetGripper_Event_type_support_data_t _SetGripper_Event_message_typesupport_data = {
  {
    0,  // will store the shared library later
    0,  // will store the shared library later
  }
};

static const type_support_map_t _SetGripper_Event_message_typesupport_map = {
  2,
  "rebotarm_msgs",
  &_SetGripper_Event_message_typesupport_ids.typesupport_identifier[0],
  &_SetGripper_Event_message_typesupport_symbol_names.symbol_name[0],
  &_SetGripper_Event_message_typesupport_data.data[0],
};

static const rosidl_message_type_support_t SetGripper_Event_message_type_support_handle = {
  ::rosidl_typesupport_cpp::typesupport_identifier,
  reinterpret_cast<const type_support_map_t *>(&_SetGripper_Event_message_typesupport_map),
  ::rosidl_typesupport_cpp::get_message_typesupport_handle_function,
  &rebotarm_msgs__srv__SetGripper_Event__get_type_hash,
  &rebotarm_msgs__srv__SetGripper_Event__get_type_description,
  &rebotarm_msgs__srv__SetGripper_Event__get_type_description_sources,
};

}  // namespace rosidl_typesupport_cpp

}  // namespace srv

}  // namespace rebotarm_msgs

namespace rosidl_typesupport_cpp
{

template<>
ROSIDL_TYPESUPPORT_CPP_PUBLIC
const rosidl_message_type_support_t *
get_message_type_support_handle<rebotarm_msgs::srv::SetGripper_Event>()
{
  return &::rebotarm_msgs::srv::rosidl_typesupport_cpp::SetGripper_Event_message_type_support_handle;
}

#ifdef __cplusplus
extern "C"
{
#endif

ROSIDL_TYPESUPPORT_CPP_PUBLIC
const rosidl_message_type_support_t *
ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_cpp, rebotarm_msgs, srv, SetGripper_Event)() {
  return get_message_type_support_handle<rebotarm_msgs::srv::SetGripper_Event>();
}

#ifdef __cplusplus
}
#endif
}  // namespace rosidl_typesupport_cpp

// already included above
// #include "cstddef"
#include "rosidl_runtime_c/service_type_support_struct.h"
#include "rosidl_typesupport_cpp/service_type_support.hpp"
// already included above
// #include "rebotarm_msgs/srv/detail/set_gripper__struct.hpp"
// already included above
// #include "rosidl_typesupport_cpp/identifier.hpp"
// already included above
// #include "rosidl_typesupport_c/type_support_map.h"
#include "rosidl_typesupport_cpp/service_type_support_dispatch.hpp"
// already included above
// #include "rosidl_typesupport_cpp/visibility_control.h"
// already included above
// #include "rosidl_typesupport_interface/macros.h"

namespace rebotarm_msgs
{

namespace srv
{

namespace rosidl_typesupport_cpp
{

typedef struct _SetGripper_type_support_ids_t
{
  const char * typesupport_identifier[2];
} _SetGripper_type_support_ids_t;

static const _SetGripper_type_support_ids_t _SetGripper_service_typesupport_ids = {
  {
    "rosidl_typesupport_fastrtps_cpp",  // ::rosidl_typesupport_fastrtps_cpp::typesupport_identifier,
    "rosidl_typesupport_introspection_cpp",  // ::rosidl_typesupport_introspection_cpp::typesupport_identifier,
  }
};

typedef struct _SetGripper_type_support_symbol_names_t
{
  const char * symbol_name[2];
} _SetGripper_type_support_symbol_names_t;
#define STRINGIFY_(s) #s
#define STRINGIFY(s) STRINGIFY_(s)

static const _SetGripper_type_support_symbol_names_t _SetGripper_service_typesupport_symbol_names = {
  {
    STRINGIFY(ROSIDL_TYPESUPPORT_INTERFACE__SERVICE_SYMBOL_NAME(rosidl_typesupport_fastrtps_cpp, rebotarm_msgs, srv, SetGripper)),
    STRINGIFY(ROSIDL_TYPESUPPORT_INTERFACE__SERVICE_SYMBOL_NAME(rosidl_typesupport_introspection_cpp, rebotarm_msgs, srv, SetGripper)),
  }
};

typedef struct _SetGripper_type_support_data_t
{
  void * data[2];
} _SetGripper_type_support_data_t;

static _SetGripper_type_support_data_t _SetGripper_service_typesupport_data = {
  {
    0,  // will store the shared library later
    0,  // will store the shared library later
  }
};

static const type_support_map_t _SetGripper_service_typesupport_map = {
  2,
  "rebotarm_msgs",
  &_SetGripper_service_typesupport_ids.typesupport_identifier[0],
  &_SetGripper_service_typesupport_symbol_names.symbol_name[0],
  &_SetGripper_service_typesupport_data.data[0],
};

static const rosidl_service_type_support_t SetGripper_service_type_support_handle = {
  ::rosidl_typesupport_cpp::typesupport_identifier,
  reinterpret_cast<const type_support_map_t *>(&_SetGripper_service_typesupport_map),
  ::rosidl_typesupport_cpp::get_service_typesupport_handle_function,
  ::rosidl_typesupport_cpp::get_message_type_support_handle<rebotarm_msgs::srv::SetGripper_Request>(),
  ::rosidl_typesupport_cpp::get_message_type_support_handle<rebotarm_msgs::srv::SetGripper_Response>(),
  ::rosidl_typesupport_cpp::get_message_type_support_handle<rebotarm_msgs::srv::SetGripper_Event>(),
  &::rosidl_typesupport_cpp::service_create_event_message<rebotarm_msgs::srv::SetGripper>,
  &::rosidl_typesupport_cpp::service_destroy_event_message<rebotarm_msgs::srv::SetGripper>,
  &rebotarm_msgs__srv__SetGripper__get_type_hash,
  &rebotarm_msgs__srv__SetGripper__get_type_description,
  &rebotarm_msgs__srv__SetGripper__get_type_description_sources,
};

}  // namespace rosidl_typesupport_cpp

}  // namespace srv

}  // namespace rebotarm_msgs

namespace rosidl_typesupport_cpp
{

template<>
ROSIDL_TYPESUPPORT_CPP_PUBLIC
const rosidl_service_type_support_t *
get_service_type_support_handle<rebotarm_msgs::srv::SetGripper>()
{
  return &::rebotarm_msgs::srv::rosidl_typesupport_cpp::SetGripper_service_type_support_handle;
}

}  // namespace rosidl_typesupport_cpp

#ifdef __cplusplus
extern "C"
{
#endif

ROSIDL_TYPESUPPORT_CPP_PUBLIC
const rosidl_service_type_support_t *
ROSIDL_TYPESUPPORT_INTERFACE__SERVICE_SYMBOL_NAME(rosidl_typesupport_cpp, rebotarm_msgs, srv, SetGripper)() {
  return ::rosidl_typesupport_cpp::get_service_type_support_handle<rebotarm_msgs::srv::SetGripper>();
}

#ifdef __cplusplus
}
#endif
