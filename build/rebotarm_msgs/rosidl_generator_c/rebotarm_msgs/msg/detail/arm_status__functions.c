// generated from rosidl_generator_c/resource/idl__functions.c.em
// with input from rebotarm_msgs:msg/ArmStatus.idl
// generated code does not contain a copyright notice
#include "rebotarm_msgs/msg/detail/arm_status__functions.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "rcutils/allocator.h"


// Include directives for member types
// Member `header`
#include "std_msgs/msg/detail/header__functions.h"
// Member `mode`
// Member `state_machine`
// Member `joint_names`
// Member `error_codes`
#include "rosidl_runtime_c/string_functions.h"
// Member `per_joint_status_code`
#include "rosidl_runtime_c/primitives_sequence_functions.h"

bool
rebotarm_msgs__msg__ArmStatus__init(rebotarm_msgs__msg__ArmStatus * msg)
{
  if (!msg) {
    return false;
  }
  // header
  if (!std_msgs__msg__Header__init(&msg->header)) {
    rebotarm_msgs__msg__ArmStatus__fini(msg);
    return false;
  }
  // mode
  if (!rosidl_runtime_c__String__init(&msg->mode)) {
    rebotarm_msgs__msg__ArmStatus__fini(msg);
    return false;
  }
  // enabled
  // control_loop_active
  // state_machine
  if (!rosidl_runtime_c__String__init(&msg->state_machine)) {
    rebotarm_msgs__msg__ArmStatus__fini(msg);
    return false;
  }
  // joint_names
  if (!rosidl_runtime_c__String__Sequence__init(&msg->joint_names, 0)) {
    rebotarm_msgs__msg__ArmStatus__fini(msg);
    return false;
  }
  // per_joint_status_code
  if (!rosidl_runtime_c__uint8__Sequence__init(&msg->per_joint_status_code, 0)) {
    rebotarm_msgs__msg__ArmStatus__fini(msg);
    return false;
  }
  // error_codes
  if (!rosidl_runtime_c__String__Sequence__init(&msg->error_codes, 0)) {
    rebotarm_msgs__msg__ArmStatus__fini(msg);
    return false;
  }
  return true;
}

void
rebotarm_msgs__msg__ArmStatus__fini(rebotarm_msgs__msg__ArmStatus * msg)
{
  if (!msg) {
    return;
  }
  // header
  std_msgs__msg__Header__fini(&msg->header);
  // mode
  rosidl_runtime_c__String__fini(&msg->mode);
  // enabled
  // control_loop_active
  // state_machine
  rosidl_runtime_c__String__fini(&msg->state_machine);
  // joint_names
  rosidl_runtime_c__String__Sequence__fini(&msg->joint_names);
  // per_joint_status_code
  rosidl_runtime_c__uint8__Sequence__fini(&msg->per_joint_status_code);
  // error_codes
  rosidl_runtime_c__String__Sequence__fini(&msg->error_codes);
}

bool
rebotarm_msgs__msg__ArmStatus__are_equal(const rebotarm_msgs__msg__ArmStatus * lhs, const rebotarm_msgs__msg__ArmStatus * rhs)
{
  if (!lhs || !rhs) {
    return false;
  }
  // header
  if (!std_msgs__msg__Header__are_equal(
      &(lhs->header), &(rhs->header)))
  {
    return false;
  }
  // mode
  if (!rosidl_runtime_c__String__are_equal(
      &(lhs->mode), &(rhs->mode)))
  {
    return false;
  }
  // enabled
  if (lhs->enabled != rhs->enabled) {
    return false;
  }
  // control_loop_active
  if (lhs->control_loop_active != rhs->control_loop_active) {
    return false;
  }
  // state_machine
  if (!rosidl_runtime_c__String__are_equal(
      &(lhs->state_machine), &(rhs->state_machine)))
  {
    return false;
  }
  // joint_names
  if (!rosidl_runtime_c__String__Sequence__are_equal(
      &(lhs->joint_names), &(rhs->joint_names)))
  {
    return false;
  }
  // per_joint_status_code
  if (!rosidl_runtime_c__uint8__Sequence__are_equal(
      &(lhs->per_joint_status_code), &(rhs->per_joint_status_code)))
  {
    return false;
  }
  // error_codes
  if (!rosidl_runtime_c__String__Sequence__are_equal(
      &(lhs->error_codes), &(rhs->error_codes)))
  {
    return false;
  }
  return true;
}

bool
rebotarm_msgs__msg__ArmStatus__copy(
  const rebotarm_msgs__msg__ArmStatus * input,
  rebotarm_msgs__msg__ArmStatus * output)
{
  if (!input || !output) {
    return false;
  }
  // header
  if (!std_msgs__msg__Header__copy(
      &(input->header), &(output->header)))
  {
    return false;
  }
  // mode
  if (!rosidl_runtime_c__String__copy(
      &(input->mode), &(output->mode)))
  {
    return false;
  }
  // enabled
  output->enabled = input->enabled;
  // control_loop_active
  output->control_loop_active = input->control_loop_active;
  // state_machine
  if (!rosidl_runtime_c__String__copy(
      &(input->state_machine), &(output->state_machine)))
  {
    return false;
  }
  // joint_names
  if (!rosidl_runtime_c__String__Sequence__copy(
      &(input->joint_names), &(output->joint_names)))
  {
    return false;
  }
  // per_joint_status_code
  if (!rosidl_runtime_c__uint8__Sequence__copy(
      &(input->per_joint_status_code), &(output->per_joint_status_code)))
  {
    return false;
  }
  // error_codes
  if (!rosidl_runtime_c__String__Sequence__copy(
      &(input->error_codes), &(output->error_codes)))
  {
    return false;
  }
  return true;
}

rebotarm_msgs__msg__ArmStatus *
rebotarm_msgs__msg__ArmStatus__create(void)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rebotarm_msgs__msg__ArmStatus * msg = (rebotarm_msgs__msg__ArmStatus *)allocator.allocate(sizeof(rebotarm_msgs__msg__ArmStatus), allocator.state);
  if (!msg) {
    return NULL;
  }
  memset(msg, 0, sizeof(rebotarm_msgs__msg__ArmStatus));
  bool success = rebotarm_msgs__msg__ArmStatus__init(msg);
  if (!success) {
    allocator.deallocate(msg, allocator.state);
    return NULL;
  }
  return msg;
}

void
rebotarm_msgs__msg__ArmStatus__destroy(rebotarm_msgs__msg__ArmStatus * msg)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  if (msg) {
    rebotarm_msgs__msg__ArmStatus__fini(msg);
  }
  allocator.deallocate(msg, allocator.state);
}


bool
rebotarm_msgs__msg__ArmStatus__Sequence__init(rebotarm_msgs__msg__ArmStatus__Sequence * array, size_t size)
{
  if (!array) {
    return false;
  }
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rebotarm_msgs__msg__ArmStatus * data = NULL;

  if (size) {
    if (size > SIZE_MAX / sizeof(rebotarm_msgs__msg__ArmStatus)) {
      return false;
    }
    data = (rebotarm_msgs__msg__ArmStatus *)allocator.zero_allocate(size, sizeof(rebotarm_msgs__msg__ArmStatus), allocator.state);
    if (!data) {
      return false;
    }
    // initialize all array elements
    size_t i;
    for (i = 0; i < size; ++i) {
      bool success = rebotarm_msgs__msg__ArmStatus__init(&data[i]);
      if (!success) {
        break;
      }
    }
    if (i < size) {
      // if initialization failed finalize the already initialized array elements
      for (; i > 0; --i) {
        rebotarm_msgs__msg__ArmStatus__fini(&data[i - 1]);
      }
      allocator.deallocate(data, allocator.state);
      return false;
    }
  }
  array->data = data;
  array->size = size;
  array->capacity = size;
  return true;
}

void
rebotarm_msgs__msg__ArmStatus__Sequence__fini(rebotarm_msgs__msg__ArmStatus__Sequence * array)
{
  if (!array) {
    return;
  }
  rcutils_allocator_t allocator = rcutils_get_default_allocator();

  if (array->data) {
    // ensure that data and capacity values are consistent
    assert(array->capacity > 0);
    // finalize all array elements
    for (size_t i = 0; i < array->capacity; ++i) {
      rebotarm_msgs__msg__ArmStatus__fini(&array->data[i]);
    }
    allocator.deallocate(array->data, allocator.state);
    array->data = NULL;
    array->size = 0;
    array->capacity = 0;
  } else {
    // ensure that data, size, and capacity values are consistent
    assert(0 == array->size);
    assert(0 == array->capacity);
  }
}

rebotarm_msgs__msg__ArmStatus__Sequence *
rebotarm_msgs__msg__ArmStatus__Sequence__create(size_t size)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rebotarm_msgs__msg__ArmStatus__Sequence * array = (rebotarm_msgs__msg__ArmStatus__Sequence *)allocator.allocate(sizeof(rebotarm_msgs__msg__ArmStatus__Sequence), allocator.state);
  if (!array) {
    return NULL;
  }
  bool success = rebotarm_msgs__msg__ArmStatus__Sequence__init(array, size);
  if (!success) {
    allocator.deallocate(array, allocator.state);
    return NULL;
  }
  return array;
}

void
rebotarm_msgs__msg__ArmStatus__Sequence__destroy(rebotarm_msgs__msg__ArmStatus__Sequence * array)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  if (array) {
    rebotarm_msgs__msg__ArmStatus__Sequence__fini(array);
  }
  allocator.deallocate(array, allocator.state);
}

bool
rebotarm_msgs__msg__ArmStatus__Sequence__are_equal(const rebotarm_msgs__msg__ArmStatus__Sequence * lhs, const rebotarm_msgs__msg__ArmStatus__Sequence * rhs)
{
  if (!lhs || !rhs) {
    return false;
  }
  if (lhs->size != rhs->size) {
    return false;
  }
  for (size_t i = 0; i < lhs->size; ++i) {
    if (!rebotarm_msgs__msg__ArmStatus__are_equal(&(lhs->data[i]), &(rhs->data[i]))) {
      return false;
    }
  }
  return true;
}

bool
rebotarm_msgs__msg__ArmStatus__Sequence__copy(
  const rebotarm_msgs__msg__ArmStatus__Sequence * input,
  rebotarm_msgs__msg__ArmStatus__Sequence * output)
{
  if (!input || !output) {
    return false;
  }
  if (output->capacity < input->size) {
    if (input->size > SIZE_MAX / sizeof(rebotarm_msgs__msg__ArmStatus)) {
      return false;
    }
    const size_t allocation_size =
      input->size * sizeof(rebotarm_msgs__msg__ArmStatus);
    rcutils_allocator_t allocator = rcutils_get_default_allocator();
    rebotarm_msgs__msg__ArmStatus * data =
      (rebotarm_msgs__msg__ArmStatus *)allocator.reallocate(
      output->data, allocation_size, allocator.state);
    if (!data) {
      return false;
    }
    // If reallocation succeeded, memory may or may not have been moved
    // to fulfill the allocation request, invalidating output->data.
    output->data = data;
    for (size_t i = output->capacity; i < input->size; ++i) {
      if (!rebotarm_msgs__msg__ArmStatus__init(&output->data[i])) {
        // If initialization of any new item fails, roll back
        // all previously initialized items. Existing items
        // in output are to be left unmodified.
        for (; i-- > output->capacity; ) {
          rebotarm_msgs__msg__ArmStatus__fini(&output->data[i]);
        }
        return false;
      }
    }
    output->capacity = input->size;
  }
  output->size = input->size;
  for (size_t i = 0; i < input->size; ++i) {
    if (!rebotarm_msgs__msg__ArmStatus__copy(
        &(input->data[i]), &(output->data[i])))
    {
      return false;
    }
  }
  return true;
}
