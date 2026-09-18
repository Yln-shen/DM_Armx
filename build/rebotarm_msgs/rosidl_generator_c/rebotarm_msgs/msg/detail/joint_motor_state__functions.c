// generated from rosidl_generator_c/resource/idl__functions.c.em
// with input from rebotarm_msgs:msg/JointMotorState.idl
// generated code does not contain a copyright notice
#include "rebotarm_msgs/msg/detail/joint_motor_state__functions.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "rcutils/allocator.h"


// Include directives for member types
// Member `header`
#include "std_msgs/msg/detail/header__functions.h"
// Member `joint_name`
#include "rosidl_runtime_c/string_functions.h"

bool
rebotarm_msgs__msg__JointMotorState__init(rebotarm_msgs__msg__JointMotorState * msg)
{
  if (!msg) {
    return false;
  }
  // header
  if (!std_msgs__msg__Header__init(&msg->header)) {
    rebotarm_msgs__msg__JointMotorState__fini(msg);
    return false;
  }
  // joint_name
  if (!rosidl_runtime_c__String__init(&msg->joint_name)) {
    rebotarm_msgs__msg__JointMotorState__fini(msg);
    return false;
  }
  // position
  // velocity
  // torque
  // status_code
  return true;
}

void
rebotarm_msgs__msg__JointMotorState__fini(rebotarm_msgs__msg__JointMotorState * msg)
{
  if (!msg) {
    return;
  }
  // header
  std_msgs__msg__Header__fini(&msg->header);
  // joint_name
  rosidl_runtime_c__String__fini(&msg->joint_name);
  // position
  // velocity
  // torque
  // status_code
}

bool
rebotarm_msgs__msg__JointMotorState__are_equal(const rebotarm_msgs__msg__JointMotorState * lhs, const rebotarm_msgs__msg__JointMotorState * rhs)
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
  // joint_name
  if (!rosidl_runtime_c__String__are_equal(
      &(lhs->joint_name), &(rhs->joint_name)))
  {
    return false;
  }
  // position
  if (lhs->position != rhs->position) {
    return false;
  }
  // velocity
  if (lhs->velocity != rhs->velocity) {
    return false;
  }
  // torque
  if (lhs->torque != rhs->torque) {
    return false;
  }
  // status_code
  if (lhs->status_code != rhs->status_code) {
    return false;
  }
  return true;
}

bool
rebotarm_msgs__msg__JointMotorState__copy(
  const rebotarm_msgs__msg__JointMotorState * input,
  rebotarm_msgs__msg__JointMotorState * output)
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
  // joint_name
  if (!rosidl_runtime_c__String__copy(
      &(input->joint_name), &(output->joint_name)))
  {
    return false;
  }
  // position
  output->position = input->position;
  // velocity
  output->velocity = input->velocity;
  // torque
  output->torque = input->torque;
  // status_code
  output->status_code = input->status_code;
  return true;
}

rebotarm_msgs__msg__JointMotorState *
rebotarm_msgs__msg__JointMotorState__create(void)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rebotarm_msgs__msg__JointMotorState * msg = (rebotarm_msgs__msg__JointMotorState *)allocator.allocate(sizeof(rebotarm_msgs__msg__JointMotorState), allocator.state);
  if (!msg) {
    return NULL;
  }
  memset(msg, 0, sizeof(rebotarm_msgs__msg__JointMotorState));
  bool success = rebotarm_msgs__msg__JointMotorState__init(msg);
  if (!success) {
    allocator.deallocate(msg, allocator.state);
    return NULL;
  }
  return msg;
}

void
rebotarm_msgs__msg__JointMotorState__destroy(rebotarm_msgs__msg__JointMotorState * msg)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  if (msg) {
    rebotarm_msgs__msg__JointMotorState__fini(msg);
  }
  allocator.deallocate(msg, allocator.state);
}


bool
rebotarm_msgs__msg__JointMotorState__Sequence__init(rebotarm_msgs__msg__JointMotorState__Sequence * array, size_t size)
{
  if (!array) {
    return false;
  }
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rebotarm_msgs__msg__JointMotorState * data = NULL;

  if (size) {
    if (size > SIZE_MAX / sizeof(rebotarm_msgs__msg__JointMotorState)) {
      return false;
    }
    data = (rebotarm_msgs__msg__JointMotorState *)allocator.zero_allocate(size, sizeof(rebotarm_msgs__msg__JointMotorState), allocator.state);
    if (!data) {
      return false;
    }
    // initialize all array elements
    size_t i;
    for (i = 0; i < size; ++i) {
      bool success = rebotarm_msgs__msg__JointMotorState__init(&data[i]);
      if (!success) {
        break;
      }
    }
    if (i < size) {
      // if initialization failed finalize the already initialized array elements
      for (; i > 0; --i) {
        rebotarm_msgs__msg__JointMotorState__fini(&data[i - 1]);
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
rebotarm_msgs__msg__JointMotorState__Sequence__fini(rebotarm_msgs__msg__JointMotorState__Sequence * array)
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
      rebotarm_msgs__msg__JointMotorState__fini(&array->data[i]);
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

rebotarm_msgs__msg__JointMotorState__Sequence *
rebotarm_msgs__msg__JointMotorState__Sequence__create(size_t size)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rebotarm_msgs__msg__JointMotorState__Sequence * array = (rebotarm_msgs__msg__JointMotorState__Sequence *)allocator.allocate(sizeof(rebotarm_msgs__msg__JointMotorState__Sequence), allocator.state);
  if (!array) {
    return NULL;
  }
  bool success = rebotarm_msgs__msg__JointMotorState__Sequence__init(array, size);
  if (!success) {
    allocator.deallocate(array, allocator.state);
    return NULL;
  }
  return array;
}

void
rebotarm_msgs__msg__JointMotorState__Sequence__destroy(rebotarm_msgs__msg__JointMotorState__Sequence * array)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  if (array) {
    rebotarm_msgs__msg__JointMotorState__Sequence__fini(array);
  }
  allocator.deallocate(array, allocator.state);
}

bool
rebotarm_msgs__msg__JointMotorState__Sequence__are_equal(const rebotarm_msgs__msg__JointMotorState__Sequence * lhs, const rebotarm_msgs__msg__JointMotorState__Sequence * rhs)
{
  if (!lhs || !rhs) {
    return false;
  }
  if (lhs->size != rhs->size) {
    return false;
  }
  for (size_t i = 0; i < lhs->size; ++i) {
    if (!rebotarm_msgs__msg__JointMotorState__are_equal(&(lhs->data[i]), &(rhs->data[i]))) {
      return false;
    }
  }
  return true;
}

bool
rebotarm_msgs__msg__JointMotorState__Sequence__copy(
  const rebotarm_msgs__msg__JointMotorState__Sequence * input,
  rebotarm_msgs__msg__JointMotorState__Sequence * output)
{
  if (!input || !output) {
    return false;
  }
  if (output->capacity < input->size) {
    if (input->size > SIZE_MAX / sizeof(rebotarm_msgs__msg__JointMotorState)) {
      return false;
    }
    const size_t allocation_size =
      input->size * sizeof(rebotarm_msgs__msg__JointMotorState);
    rcutils_allocator_t allocator = rcutils_get_default_allocator();
    rebotarm_msgs__msg__JointMotorState * data =
      (rebotarm_msgs__msg__JointMotorState *)allocator.reallocate(
      output->data, allocation_size, allocator.state);
    if (!data) {
      return false;
    }
    // If reallocation succeeded, memory may or may not have been moved
    // to fulfill the allocation request, invalidating output->data.
    output->data = data;
    for (size_t i = output->capacity; i < input->size; ++i) {
      if (!rebotarm_msgs__msg__JointMotorState__init(&output->data[i])) {
        // If initialization of any new item fails, roll back
        // all previously initialized items. Existing items
        // in output are to be left unmodified.
        for (; i-- > output->capacity; ) {
          rebotarm_msgs__msg__JointMotorState__fini(&output->data[i]);
        }
        return false;
      }
    }
    output->capacity = input->size;
  }
  output->size = input->size;
  for (size_t i = 0; i < input->size; ++i) {
    if (!rebotarm_msgs__msg__JointMotorState__copy(
        &(input->data[i]), &(output->data[i])))
    {
      return false;
    }
  }
  return true;
}
