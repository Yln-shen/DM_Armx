// generated from rosidl_typesupport_introspection_c/resource/idl__type_support.c.em
// with input from rebotarm_msgs:msg/JointMitCmd.idl
// generated code does not contain a copyright notice

#include <stddef.h>
#include "rebotarm_msgs/msg/detail/joint_mit_cmd__rosidl_typesupport_introspection_c.h"
#include "rebotarm_msgs/msg/rosidl_typesupport_introspection_c__visibility_control.h"
#include "rosidl_typesupport_introspection_c/field_types.h"
#include "rosidl_typesupport_introspection_c/identifier.h"
#include "rosidl_typesupport_introspection_c/message_introspection.h"
#include "rebotarm_msgs/msg/detail/joint_mit_cmd__functions.h"
#include "rebotarm_msgs/msg/detail/joint_mit_cmd__struct.h"


// Include directives for member types
// Member `stamp`
#include "builtin_interfaces/msg/time.h"
// Member `stamp`
#include "builtin_interfaces/msg/detail/time__rosidl_typesupport_introspection_c.h"

#ifdef __cplusplus
extern "C"
{
#endif

void rebotarm_msgs__msg__JointMitCmd__rosidl_typesupport_introspection_c__JointMitCmd_init_function(
  void * message_memory, enum rosidl_runtime_c__message_initialization _init)
{
  // TODO(karsten1987): initializers are not yet implemented for typesupport c
  // see https://github.com/ros2/ros2/issues/397
  (void) _init;
  rebotarm_msgs__msg__JointMitCmd__init(message_memory);
}

void rebotarm_msgs__msg__JointMitCmd__rosidl_typesupport_introspection_c__JointMitCmd_fini_function(void * message_memory)
{
  rebotarm_msgs__msg__JointMitCmd__fini(message_memory);
}

static rosidl_typesupport_introspection_c__MessageMember rebotarm_msgs__msg__JointMitCmd__rosidl_typesupport_introspection_c__JointMitCmd_message_member_array[6] = {
  {
    "pos",  // name
    rosidl_typesupport_introspection_c__ROS_TYPE_DOUBLE,  // type
    0,  // upper bound of string
    NULL,  // members of sub message
    false,  // is key
    false,  // is array
    0,  // array size
    false,  // is upper bound
    offsetof(rebotarm_msgs__msg__JointMitCmd, pos),  // bytes offset in struct
    NULL,  // default value
    NULL,  // size() function pointer
    NULL,  // get_const(index) function pointer
    NULL,  // get(index) function pointer
    NULL,  // fetch(index, &value) function pointer
    NULL,  // assign(index, value) function pointer
    NULL  // resize(index) function pointer
  },
  {
    "vel",  // name
    rosidl_typesupport_introspection_c__ROS_TYPE_DOUBLE,  // type
    0,  // upper bound of string
    NULL,  // members of sub message
    false,  // is key
    false,  // is array
    0,  // array size
    false,  // is upper bound
    offsetof(rebotarm_msgs__msg__JointMitCmd, vel),  // bytes offset in struct
    NULL,  // default value
    NULL,  // size() function pointer
    NULL,  // get_const(index) function pointer
    NULL,  // get(index) function pointer
    NULL,  // fetch(index, &value) function pointer
    NULL,  // assign(index, value) function pointer
    NULL  // resize(index) function pointer
  },
  {
    "kp",  // name
    rosidl_typesupport_introspection_c__ROS_TYPE_DOUBLE,  // type
    0,  // upper bound of string
    NULL,  // members of sub message
    false,  // is key
    false,  // is array
    0,  // array size
    false,  // is upper bound
    offsetof(rebotarm_msgs__msg__JointMitCmd, kp),  // bytes offset in struct
    NULL,  // default value
    NULL,  // size() function pointer
    NULL,  // get_const(index) function pointer
    NULL,  // get(index) function pointer
    NULL,  // fetch(index, &value) function pointer
    NULL,  // assign(index, value) function pointer
    NULL  // resize(index) function pointer
  },
  {
    "kd",  // name
    rosidl_typesupport_introspection_c__ROS_TYPE_DOUBLE,  // type
    0,  // upper bound of string
    NULL,  // members of sub message
    false,  // is key
    false,  // is array
    0,  // array size
    false,  // is upper bound
    offsetof(rebotarm_msgs__msg__JointMitCmd, kd),  // bytes offset in struct
    NULL,  // default value
    NULL,  // size() function pointer
    NULL,  // get_const(index) function pointer
    NULL,  // get(index) function pointer
    NULL,  // fetch(index, &value) function pointer
    NULL,  // assign(index, value) function pointer
    NULL  // resize(index) function pointer
  },
  {
    "tau",  // name
    rosidl_typesupport_introspection_c__ROS_TYPE_DOUBLE,  // type
    0,  // upper bound of string
    NULL,  // members of sub message
    false,  // is key
    false,  // is array
    0,  // array size
    false,  // is upper bound
    offsetof(rebotarm_msgs__msg__JointMitCmd, tau),  // bytes offset in struct
    NULL,  // default value
    NULL,  // size() function pointer
    NULL,  // get_const(index) function pointer
    NULL,  // get(index) function pointer
    NULL,  // fetch(index, &value) function pointer
    NULL,  // assign(index, value) function pointer
    NULL  // resize(index) function pointer
  },
  {
    "stamp",  // name
    rosidl_typesupport_introspection_c__ROS_TYPE_MESSAGE,  // type
    0,  // upper bound of string
    NULL,  // members of sub message (initialized later)
    false,  // is key
    false,  // is array
    0,  // array size
    false,  // is upper bound
    offsetof(rebotarm_msgs__msg__JointMitCmd, stamp),  // bytes offset in struct
    NULL,  // default value
    NULL,  // size() function pointer
    NULL,  // get_const(index) function pointer
    NULL,  // get(index) function pointer
    NULL,  // fetch(index, &value) function pointer
    NULL,  // assign(index, value) function pointer
    NULL  // resize(index) function pointer
  }
};

static const rosidl_typesupport_introspection_c__MessageMembers rebotarm_msgs__msg__JointMitCmd__rosidl_typesupport_introspection_c__JointMitCmd_message_members = {
  "rebotarm_msgs__msg",  // message namespace
  "JointMitCmd",  // message name
  6,  // number of fields
  sizeof(rebotarm_msgs__msg__JointMitCmd),
  false,  // has_any_key_member_
  rebotarm_msgs__msg__JointMitCmd__rosidl_typesupport_introspection_c__JointMitCmd_message_member_array,  // message members
  rebotarm_msgs__msg__JointMitCmd__rosidl_typesupport_introspection_c__JointMitCmd_init_function,  // function to initialize message memory (memory has to be allocated)
  rebotarm_msgs__msg__JointMitCmd__rosidl_typesupport_introspection_c__JointMitCmd_fini_function  // function to terminate message instance (will not free memory)
};

// this is not const since it must be initialized on first access
// since C does not allow non-integral compile-time constants
static rosidl_message_type_support_t rebotarm_msgs__msg__JointMitCmd__rosidl_typesupport_introspection_c__JointMitCmd_message_type_support_handle = {
  0,
  &rebotarm_msgs__msg__JointMitCmd__rosidl_typesupport_introspection_c__JointMitCmd_message_members,
  get_message_typesupport_handle_function,
  &rebotarm_msgs__msg__JointMitCmd__get_type_hash,
  &rebotarm_msgs__msg__JointMitCmd__get_type_description,
  &rebotarm_msgs__msg__JointMitCmd__get_type_description_sources,
};

ROSIDL_TYPESUPPORT_INTROSPECTION_C_EXPORT_rebotarm_msgs
const rosidl_message_type_support_t *
ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_introspection_c, rebotarm_msgs, msg, JointMitCmd)() {
  rebotarm_msgs__msg__JointMitCmd__rosidl_typesupport_introspection_c__JointMitCmd_message_member_array[5].members_ =
    ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_introspection_c, builtin_interfaces, msg, Time)();
  if (!rebotarm_msgs__msg__JointMitCmd__rosidl_typesupport_introspection_c__JointMitCmd_message_type_support_handle.typesupport_identifier) {
    rebotarm_msgs__msg__JointMitCmd__rosidl_typesupport_introspection_c__JointMitCmd_message_type_support_handle.typesupport_identifier =
      rosidl_typesupport_introspection_c__identifier;
  }
  return &rebotarm_msgs__msg__JointMitCmd__rosidl_typesupport_introspection_c__JointMitCmd_message_type_support_handle;
}
#ifdef __cplusplus
}
#endif
