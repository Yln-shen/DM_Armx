// generated from rosidl_typesupport_introspection_c/resource/idl__type_support.c.em
// with input from rebotarm_msgs:msg/ArmStatus.idl
// generated code does not contain a copyright notice

#include <stddef.h>
#include "rebotarm_msgs/msg/detail/arm_status__rosidl_typesupport_introspection_c.h"
#include "rebotarm_msgs/msg/rosidl_typesupport_introspection_c__visibility_control.h"
#include "rosidl_typesupport_introspection_c/field_types.h"
#include "rosidl_typesupport_introspection_c/identifier.h"
#include "rosidl_typesupport_introspection_c/message_introspection.h"
#include "rebotarm_msgs/msg/detail/arm_status__functions.h"
#include "rebotarm_msgs/msg/detail/arm_status__struct.h"


// Include directives for member types
// Member `header`
#include "std_msgs/msg/header.h"
// Member `header`
#include "std_msgs/msg/detail/header__rosidl_typesupport_introspection_c.h"
// Member `mode`
// Member `state_machine`
// Member `joint_names`
// Member `error_codes`
#include "rosidl_runtime_c/string_functions.h"
// Member `per_joint_status_code`
#include "rosidl_runtime_c/primitives_sequence_functions.h"

#ifdef __cplusplus
extern "C"
{
#endif

void rebotarm_msgs__msg__ArmStatus__rosidl_typesupport_introspection_c__ArmStatus_init_function(
  void * message_memory, enum rosidl_runtime_c__message_initialization _init)
{
  // TODO(karsten1987): initializers are not yet implemented for typesupport c
  // see https://github.com/ros2/ros2/issues/397
  (void) _init;
  rebotarm_msgs__msg__ArmStatus__init(message_memory);
}

void rebotarm_msgs__msg__ArmStatus__rosidl_typesupport_introspection_c__ArmStatus_fini_function(void * message_memory)
{
  rebotarm_msgs__msg__ArmStatus__fini(message_memory);
}

size_t rebotarm_msgs__msg__ArmStatus__rosidl_typesupport_introspection_c__size_function__ArmStatus__joint_names(
  const void * untyped_member)
{
  const rosidl_runtime_c__String__Sequence * member =
    (const rosidl_runtime_c__String__Sequence *)(untyped_member);
  return member->size;
}

const void * rebotarm_msgs__msg__ArmStatus__rosidl_typesupport_introspection_c__get_const_function__ArmStatus__joint_names(
  const void * untyped_member, size_t index)
{
  const rosidl_runtime_c__String__Sequence * member =
    (const rosidl_runtime_c__String__Sequence *)(untyped_member);
  return &member->data[index];
}

void * rebotarm_msgs__msg__ArmStatus__rosidl_typesupport_introspection_c__get_function__ArmStatus__joint_names(
  void * untyped_member, size_t index)
{
  rosidl_runtime_c__String__Sequence * member =
    (rosidl_runtime_c__String__Sequence *)(untyped_member);
  return &member->data[index];
}

void rebotarm_msgs__msg__ArmStatus__rosidl_typesupport_introspection_c__fetch_function__ArmStatus__joint_names(
  const void * untyped_member, size_t index, void * untyped_value)
{
  const rosidl_runtime_c__String * item =
    ((const rosidl_runtime_c__String *)
    rebotarm_msgs__msg__ArmStatus__rosidl_typesupport_introspection_c__get_const_function__ArmStatus__joint_names(untyped_member, index));
  rosidl_runtime_c__String * value =
    (rosidl_runtime_c__String *)(untyped_value);
  *value = *item;
}

void rebotarm_msgs__msg__ArmStatus__rosidl_typesupport_introspection_c__assign_function__ArmStatus__joint_names(
  void * untyped_member, size_t index, const void * untyped_value)
{
  rosidl_runtime_c__String * item =
    ((rosidl_runtime_c__String *)
    rebotarm_msgs__msg__ArmStatus__rosidl_typesupport_introspection_c__get_function__ArmStatus__joint_names(untyped_member, index));
  const rosidl_runtime_c__String * value =
    (const rosidl_runtime_c__String *)(untyped_value);
  *item = *value;
}

bool rebotarm_msgs__msg__ArmStatus__rosidl_typesupport_introspection_c__resize_function__ArmStatus__joint_names(
  void * untyped_member, size_t size)
{
  rosidl_runtime_c__String__Sequence * member =
    (rosidl_runtime_c__String__Sequence *)(untyped_member);
  rosidl_runtime_c__String__Sequence__fini(member);
  return rosidl_runtime_c__String__Sequence__init(member, size);
}

size_t rebotarm_msgs__msg__ArmStatus__rosidl_typesupport_introspection_c__size_function__ArmStatus__per_joint_status_code(
  const void * untyped_member)
{
  const rosidl_runtime_c__uint8__Sequence * member =
    (const rosidl_runtime_c__uint8__Sequence *)(untyped_member);
  return member->size;
}

const void * rebotarm_msgs__msg__ArmStatus__rosidl_typesupport_introspection_c__get_const_function__ArmStatus__per_joint_status_code(
  const void * untyped_member, size_t index)
{
  const rosidl_runtime_c__uint8__Sequence * member =
    (const rosidl_runtime_c__uint8__Sequence *)(untyped_member);
  return &member->data[index];
}

void * rebotarm_msgs__msg__ArmStatus__rosidl_typesupport_introspection_c__get_function__ArmStatus__per_joint_status_code(
  void * untyped_member, size_t index)
{
  rosidl_runtime_c__uint8__Sequence * member =
    (rosidl_runtime_c__uint8__Sequence *)(untyped_member);
  return &member->data[index];
}

void rebotarm_msgs__msg__ArmStatus__rosidl_typesupport_introspection_c__fetch_function__ArmStatus__per_joint_status_code(
  const void * untyped_member, size_t index, void * untyped_value)
{
  const uint8_t * item =
    ((const uint8_t *)
    rebotarm_msgs__msg__ArmStatus__rosidl_typesupport_introspection_c__get_const_function__ArmStatus__per_joint_status_code(untyped_member, index));
  uint8_t * value =
    (uint8_t *)(untyped_value);
  *value = *item;
}

void rebotarm_msgs__msg__ArmStatus__rosidl_typesupport_introspection_c__assign_function__ArmStatus__per_joint_status_code(
  void * untyped_member, size_t index, const void * untyped_value)
{
  uint8_t * item =
    ((uint8_t *)
    rebotarm_msgs__msg__ArmStatus__rosidl_typesupport_introspection_c__get_function__ArmStatus__per_joint_status_code(untyped_member, index));
  const uint8_t * value =
    (const uint8_t *)(untyped_value);
  *item = *value;
}

bool rebotarm_msgs__msg__ArmStatus__rosidl_typesupport_introspection_c__resize_function__ArmStatus__per_joint_status_code(
  void * untyped_member, size_t size)
{
  rosidl_runtime_c__uint8__Sequence * member =
    (rosidl_runtime_c__uint8__Sequence *)(untyped_member);
  rosidl_runtime_c__uint8__Sequence__fini(member);
  return rosidl_runtime_c__uint8__Sequence__init(member, size);
}

size_t rebotarm_msgs__msg__ArmStatus__rosidl_typesupport_introspection_c__size_function__ArmStatus__error_codes(
  const void * untyped_member)
{
  const rosidl_runtime_c__String__Sequence * member =
    (const rosidl_runtime_c__String__Sequence *)(untyped_member);
  return member->size;
}

const void * rebotarm_msgs__msg__ArmStatus__rosidl_typesupport_introspection_c__get_const_function__ArmStatus__error_codes(
  const void * untyped_member, size_t index)
{
  const rosidl_runtime_c__String__Sequence * member =
    (const rosidl_runtime_c__String__Sequence *)(untyped_member);
  return &member->data[index];
}

void * rebotarm_msgs__msg__ArmStatus__rosidl_typesupport_introspection_c__get_function__ArmStatus__error_codes(
  void * untyped_member, size_t index)
{
  rosidl_runtime_c__String__Sequence * member =
    (rosidl_runtime_c__String__Sequence *)(untyped_member);
  return &member->data[index];
}

void rebotarm_msgs__msg__ArmStatus__rosidl_typesupport_introspection_c__fetch_function__ArmStatus__error_codes(
  const void * untyped_member, size_t index, void * untyped_value)
{
  const rosidl_runtime_c__String * item =
    ((const rosidl_runtime_c__String *)
    rebotarm_msgs__msg__ArmStatus__rosidl_typesupport_introspection_c__get_const_function__ArmStatus__error_codes(untyped_member, index));
  rosidl_runtime_c__String * value =
    (rosidl_runtime_c__String *)(untyped_value);
  *value = *item;
}

void rebotarm_msgs__msg__ArmStatus__rosidl_typesupport_introspection_c__assign_function__ArmStatus__error_codes(
  void * untyped_member, size_t index, const void * untyped_value)
{
  rosidl_runtime_c__String * item =
    ((rosidl_runtime_c__String *)
    rebotarm_msgs__msg__ArmStatus__rosidl_typesupport_introspection_c__get_function__ArmStatus__error_codes(untyped_member, index));
  const rosidl_runtime_c__String * value =
    (const rosidl_runtime_c__String *)(untyped_value);
  *item = *value;
}

bool rebotarm_msgs__msg__ArmStatus__rosidl_typesupport_introspection_c__resize_function__ArmStatus__error_codes(
  void * untyped_member, size_t size)
{
  rosidl_runtime_c__String__Sequence * member =
    (rosidl_runtime_c__String__Sequence *)(untyped_member);
  rosidl_runtime_c__String__Sequence__fini(member);
  return rosidl_runtime_c__String__Sequence__init(member, size);
}

static rosidl_typesupport_introspection_c__MessageMember rebotarm_msgs__msg__ArmStatus__rosidl_typesupport_introspection_c__ArmStatus_message_member_array[8] = {
  {
    "header",  // name
    rosidl_typesupport_introspection_c__ROS_TYPE_MESSAGE,  // type
    0,  // upper bound of string
    NULL,  // members of sub message (initialized later)
    false,  // is key
    false,  // is array
    0,  // array size
    false,  // is upper bound
    offsetof(rebotarm_msgs__msg__ArmStatus, header),  // bytes offset in struct
    NULL,  // default value
    NULL,  // size() function pointer
    NULL,  // get_const(index) function pointer
    NULL,  // get(index) function pointer
    NULL,  // fetch(index, &value) function pointer
    NULL,  // assign(index, value) function pointer
    NULL  // resize(index) function pointer
  },
  {
    "mode",  // name
    rosidl_typesupport_introspection_c__ROS_TYPE_STRING,  // type
    0,  // upper bound of string
    NULL,  // members of sub message
    false,  // is key
    false,  // is array
    0,  // array size
    false,  // is upper bound
    offsetof(rebotarm_msgs__msg__ArmStatus, mode),  // bytes offset in struct
    NULL,  // default value
    NULL,  // size() function pointer
    NULL,  // get_const(index) function pointer
    NULL,  // get(index) function pointer
    NULL,  // fetch(index, &value) function pointer
    NULL,  // assign(index, value) function pointer
    NULL  // resize(index) function pointer
  },
  {
    "enabled",  // name
    rosidl_typesupport_introspection_c__ROS_TYPE_BOOLEAN,  // type
    0,  // upper bound of string
    NULL,  // members of sub message
    false,  // is key
    false,  // is array
    0,  // array size
    false,  // is upper bound
    offsetof(rebotarm_msgs__msg__ArmStatus, enabled),  // bytes offset in struct
    NULL,  // default value
    NULL,  // size() function pointer
    NULL,  // get_const(index) function pointer
    NULL,  // get(index) function pointer
    NULL,  // fetch(index, &value) function pointer
    NULL,  // assign(index, value) function pointer
    NULL  // resize(index) function pointer
  },
  {
    "control_loop_active",  // name
    rosidl_typesupport_introspection_c__ROS_TYPE_BOOLEAN,  // type
    0,  // upper bound of string
    NULL,  // members of sub message
    false,  // is key
    false,  // is array
    0,  // array size
    false,  // is upper bound
    offsetof(rebotarm_msgs__msg__ArmStatus, control_loop_active),  // bytes offset in struct
    NULL,  // default value
    NULL,  // size() function pointer
    NULL,  // get_const(index) function pointer
    NULL,  // get(index) function pointer
    NULL,  // fetch(index, &value) function pointer
    NULL,  // assign(index, value) function pointer
    NULL  // resize(index) function pointer
  },
  {
    "state_machine",  // name
    rosidl_typesupport_introspection_c__ROS_TYPE_STRING,  // type
    0,  // upper bound of string
    NULL,  // members of sub message
    false,  // is key
    false,  // is array
    0,  // array size
    false,  // is upper bound
    offsetof(rebotarm_msgs__msg__ArmStatus, state_machine),  // bytes offset in struct
    NULL,  // default value
    NULL,  // size() function pointer
    NULL,  // get_const(index) function pointer
    NULL,  // get(index) function pointer
    NULL,  // fetch(index, &value) function pointer
    NULL,  // assign(index, value) function pointer
    NULL  // resize(index) function pointer
  },
  {
    "joint_names",  // name
    rosidl_typesupport_introspection_c__ROS_TYPE_STRING,  // type
    0,  // upper bound of string
    NULL,  // members of sub message
    false,  // is key
    true,  // is array
    0,  // array size
    false,  // is upper bound
    offsetof(rebotarm_msgs__msg__ArmStatus, joint_names),  // bytes offset in struct
    NULL,  // default value
    rebotarm_msgs__msg__ArmStatus__rosidl_typesupport_introspection_c__size_function__ArmStatus__joint_names,  // size() function pointer
    rebotarm_msgs__msg__ArmStatus__rosidl_typesupport_introspection_c__get_const_function__ArmStatus__joint_names,  // get_const(index) function pointer
    rebotarm_msgs__msg__ArmStatus__rosidl_typesupport_introspection_c__get_function__ArmStatus__joint_names,  // get(index) function pointer
    rebotarm_msgs__msg__ArmStatus__rosidl_typesupport_introspection_c__fetch_function__ArmStatus__joint_names,  // fetch(index, &value) function pointer
    rebotarm_msgs__msg__ArmStatus__rosidl_typesupport_introspection_c__assign_function__ArmStatus__joint_names,  // assign(index, value) function pointer
    rebotarm_msgs__msg__ArmStatus__rosidl_typesupport_introspection_c__resize_function__ArmStatus__joint_names  // resize(index) function pointer
  },
  {
    "per_joint_status_code",  // name
    rosidl_typesupport_introspection_c__ROS_TYPE_UINT8,  // type
    0,  // upper bound of string
    NULL,  // members of sub message
    false,  // is key
    true,  // is array
    0,  // array size
    false,  // is upper bound
    offsetof(rebotarm_msgs__msg__ArmStatus, per_joint_status_code),  // bytes offset in struct
    NULL,  // default value
    rebotarm_msgs__msg__ArmStatus__rosidl_typesupport_introspection_c__size_function__ArmStatus__per_joint_status_code,  // size() function pointer
    rebotarm_msgs__msg__ArmStatus__rosidl_typesupport_introspection_c__get_const_function__ArmStatus__per_joint_status_code,  // get_const(index) function pointer
    rebotarm_msgs__msg__ArmStatus__rosidl_typesupport_introspection_c__get_function__ArmStatus__per_joint_status_code,  // get(index) function pointer
    rebotarm_msgs__msg__ArmStatus__rosidl_typesupport_introspection_c__fetch_function__ArmStatus__per_joint_status_code,  // fetch(index, &value) function pointer
    rebotarm_msgs__msg__ArmStatus__rosidl_typesupport_introspection_c__assign_function__ArmStatus__per_joint_status_code,  // assign(index, value) function pointer
    rebotarm_msgs__msg__ArmStatus__rosidl_typesupport_introspection_c__resize_function__ArmStatus__per_joint_status_code  // resize(index) function pointer
  },
  {
    "error_codes",  // name
    rosidl_typesupport_introspection_c__ROS_TYPE_STRING,  // type
    0,  // upper bound of string
    NULL,  // members of sub message
    false,  // is key
    true,  // is array
    0,  // array size
    false,  // is upper bound
    offsetof(rebotarm_msgs__msg__ArmStatus, error_codes),  // bytes offset in struct
    NULL,  // default value
    rebotarm_msgs__msg__ArmStatus__rosidl_typesupport_introspection_c__size_function__ArmStatus__error_codes,  // size() function pointer
    rebotarm_msgs__msg__ArmStatus__rosidl_typesupport_introspection_c__get_const_function__ArmStatus__error_codes,  // get_const(index) function pointer
    rebotarm_msgs__msg__ArmStatus__rosidl_typesupport_introspection_c__get_function__ArmStatus__error_codes,  // get(index) function pointer
    rebotarm_msgs__msg__ArmStatus__rosidl_typesupport_introspection_c__fetch_function__ArmStatus__error_codes,  // fetch(index, &value) function pointer
    rebotarm_msgs__msg__ArmStatus__rosidl_typesupport_introspection_c__assign_function__ArmStatus__error_codes,  // assign(index, value) function pointer
    rebotarm_msgs__msg__ArmStatus__rosidl_typesupport_introspection_c__resize_function__ArmStatus__error_codes  // resize(index) function pointer
  }
};

static const rosidl_typesupport_introspection_c__MessageMembers rebotarm_msgs__msg__ArmStatus__rosidl_typesupport_introspection_c__ArmStatus_message_members = {
  "rebotarm_msgs__msg",  // message namespace
  "ArmStatus",  // message name
  8,  // number of fields
  sizeof(rebotarm_msgs__msg__ArmStatus),
  false,  // has_any_key_member_
  rebotarm_msgs__msg__ArmStatus__rosidl_typesupport_introspection_c__ArmStatus_message_member_array,  // message members
  rebotarm_msgs__msg__ArmStatus__rosidl_typesupport_introspection_c__ArmStatus_init_function,  // function to initialize message memory (memory has to be allocated)
  rebotarm_msgs__msg__ArmStatus__rosidl_typesupport_introspection_c__ArmStatus_fini_function  // function to terminate message instance (will not free memory)
};

// this is not const since it must be initialized on first access
// since C does not allow non-integral compile-time constants
static rosidl_message_type_support_t rebotarm_msgs__msg__ArmStatus__rosidl_typesupport_introspection_c__ArmStatus_message_type_support_handle = {
  0,
  &rebotarm_msgs__msg__ArmStatus__rosidl_typesupport_introspection_c__ArmStatus_message_members,
  get_message_typesupport_handle_function,
  &rebotarm_msgs__msg__ArmStatus__get_type_hash,
  &rebotarm_msgs__msg__ArmStatus__get_type_description,
  &rebotarm_msgs__msg__ArmStatus__get_type_description_sources,
};

ROSIDL_TYPESUPPORT_INTROSPECTION_C_EXPORT_rebotarm_msgs
const rosidl_message_type_support_t *
ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_introspection_c, rebotarm_msgs, msg, ArmStatus)() {
  rebotarm_msgs__msg__ArmStatus__rosidl_typesupport_introspection_c__ArmStatus_message_member_array[0].members_ =
    ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_introspection_c, std_msgs, msg, Header)();
  if (!rebotarm_msgs__msg__ArmStatus__rosidl_typesupport_introspection_c__ArmStatus_message_type_support_handle.typesupport_identifier) {
    rebotarm_msgs__msg__ArmStatus__rosidl_typesupport_introspection_c__ArmStatus_message_type_support_handle.typesupport_identifier =
      rosidl_typesupport_introspection_c__identifier;
  }
  return &rebotarm_msgs__msg__ArmStatus__rosidl_typesupport_introspection_c__ArmStatus_message_type_support_handle;
}
#ifdef __cplusplus
}
#endif
