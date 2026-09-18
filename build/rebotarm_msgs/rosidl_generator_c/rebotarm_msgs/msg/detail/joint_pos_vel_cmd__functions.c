// generated from rosidl_generator_c/resource/idl__functions.c.em
// with input from rebotarm_msgs:msg/JointPosVelCmd.idl
// generated code does not contain a copyright notice
#include "rebotarm_msgs/msg/detail/joint_pos_vel_cmd__functions.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "rcutils/allocator.h"


// Include directives for member types
// Member `stamp`
#include "builtin_interfaces/msg/detail/time__functions.h"

bool
rebotarm_msgs__msg__JointPosVelCmd__init(rebotarm_msgs__msg__JointPosVelCmd * msg)
{
  if (!msg) {
    return false;
  }
  // pos
  // vlim
  // stamp
  if (!builtin_interfaces__msg__Time__init(&msg->stamp)) {
    rebotarm_msgs__msg__JointPosVelCmd__fini(msg);
    return false;
  }
  return true;
}

void
rebotarm_msgs__msg__JointPosVelCmd__fini(rebotarm_msgs__msg__JointPosVelCmd * msg)
{
  if (!msg) {
    return;
  }
  // pos
  // vlim
  // stamp
  builtin_interfaces__msg__Time__fini(&msg->stamp);
}

bool
rebotarm_msgs__msg__JointPosVelCmd__are_equal(const rebotarm_msgs__msg__JointPosVelCmd * lhs, const rebotarm_msgs__msg__JointPosVelCmd * rhs)
{
  if (!lhs || !rhs) {
    return false;
  }
  // pos
  if (lhs->pos != rhs->pos) {
    return false;
  }
  // vlim
  if (lhs->vlim != rhs->vlim) {
    return false;
  }
  // stamp
  if (!builtin_interfaces__msg__Time__are_equal(
      &(lhs->stamp), &(rhs->stamp)))
  {
    return false;
  }
  return true;
}

bool
rebotarm_msgs__msg__JointPosVelCmd__copy(
  const rebotarm_msgs__msg__JointPosVelCmd * input,
  rebotarm_msgs__msg__JointPosVelCmd * output)
{
  if (!input || !output) {
    return false;
  }
  // pos
  output->pos = input->pos;
  // vlim
  output->vlim = input->vlim;
  // stamp
  if (!builtin_interfaces__msg__Time__copy(
      &(input->stamp), &(output->stamp)))
  {
    return false;
  }
  return true;
}

rebotarm_msgs__msg__JointPosVelCmd *
rebotarm_msgs__msg__JointPosVelCmd__create(void)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rebotarm_msgs__msg__JointPosVelCmd * msg = (rebotarm_msgs__msg__JointPosVelCmd *)allocator.allocate(sizeof(rebotarm_msgs__msg__JointPosVelCmd), allocator.state);
  if (!msg) {
    return NULL;
  }
  memset(msg, 0, sizeof(rebotarm_msgs__msg__JointPosVelCmd));
  bool success = rebotarm_msgs__msg__JointPosVelCmd__init(msg);
  if (!success) {
    allocator.deallocate(msg, allocator.state);
    return NULL;
  }
  return msg;
}

void
rebotarm_msgs__msg__JointPosVelCmd__destroy(rebotarm_msgs__msg__JointPosVelCmd * msg)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  if (msg) {
    rebotarm_msgs__msg__JointPosVelCmd__fini(msg);
  }
  allocator.deallocate(msg, allocator.state);
}


bool
rebotarm_msgs__msg__JointPosVelCmd__Sequence__init(rebotarm_msgs__msg__JointPosVelCmd__Sequence * array, size_t size)
{
  if (!array) {
    return false;
  }
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rebotarm_msgs__msg__JointPosVelCmd * data = NULL;

  if (size) {
    if (size > SIZE_MAX / sizeof(rebotarm_msgs__msg__JointPosVelCmd)) {
      return false;
    }
    data = (rebotarm_msgs__msg__JointPosVelCmd *)allocator.zero_allocate(size, sizeof(rebotarm_msgs__msg__JointPosVelCmd), allocator.state);
    if (!data) {
      return false;
    }
    // initialize all array elements
    size_t i;
    for (i = 0; i < size; ++i) {
      bool success = rebotarm_msgs__msg__JointPosVelCmd__init(&data[i]);
      if (!success) {
        break;
      }
    }
    if (i < size) {
      // if initialization failed finalize the already initialized array elements
      for (; i > 0; --i) {
        rebotarm_msgs__msg__JointPosVelCmd__fini(&data[i - 1]);
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
rebotarm_msgs__msg__JointPosVelCmd__Sequence__fini(rebotarm_msgs__msg__JointPosVelCmd__Sequence * array)
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
      rebotarm_msgs__msg__JointPosVelCmd__fini(&array->data[i]);
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

rebotarm_msgs__msg__JointPosVelCmd__Sequence *
rebotarm_msgs__msg__JointPosVelCmd__Sequence__create(size_t size)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rebotarm_msgs__msg__JointPosVelCmd__Sequence * array = (rebotarm_msgs__msg__JointPosVelCmd__Sequence *)allocator.allocate(sizeof(rebotarm_msgs__msg__JointPosVelCmd__Sequence), allocator.state);
  if (!array) {
    return NULL;
  }
  bool success = rebotarm_msgs__msg__JointPosVelCmd__Sequence__init(array, size);
  if (!success) {
    allocator.deallocate(array, allocator.state);
    return NULL;
  }
  return array;
}

void
rebotarm_msgs__msg__JointPosVelCmd__Sequence__destroy(rebotarm_msgs__msg__JointPosVelCmd__Sequence * array)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  if (array) {
    rebotarm_msgs__msg__JointPosVelCmd__Sequence__fini(array);
  }
  allocator.deallocate(array, allocator.state);
}

bool
rebotarm_msgs__msg__JointPosVelCmd__Sequence__are_equal(const rebotarm_msgs__msg__JointPosVelCmd__Sequence * lhs, const rebotarm_msgs__msg__JointPosVelCmd__Sequence * rhs)
{
  if (!lhs || !rhs) {
    return false;
  }
  if (lhs->size != rhs->size) {
    return false;
  }
  for (size_t i = 0; i < lhs->size; ++i) {
    if (!rebotarm_msgs__msg__JointPosVelCmd__are_equal(&(lhs->data[i]), &(rhs->data[i]))) {
      return false;
    }
  }
  return true;
}

bool
rebotarm_msgs__msg__JointPosVelCmd__Sequence__copy(
  const rebotarm_msgs__msg__JointPosVelCmd__Sequence * input,
  rebotarm_msgs__msg__JointPosVelCmd__Sequence * output)
{
  if (!input || !output) {
    return false;
  }
  if (output->capacity < input->size) {
    if (input->size > SIZE_MAX / sizeof(rebotarm_msgs__msg__JointPosVelCmd)) {
      return false;
    }
    const size_t allocation_size =
      input->size * sizeof(rebotarm_msgs__msg__JointPosVelCmd);
    rcutils_allocator_t allocator = rcutils_get_default_allocator();
    rebotarm_msgs__msg__JointPosVelCmd * data =
      (rebotarm_msgs__msg__JointPosVelCmd *)allocator.reallocate(
      output->data, allocation_size, allocator.state);
    if (!data) {
      return false;
    }
    // If reallocation succeeded, memory may or may not have been moved
    // to fulfill the allocation request, invalidating output->data.
    output->data = data;
    for (size_t i = output->capacity; i < input->size; ++i) {
      if (!rebotarm_msgs__msg__JointPosVelCmd__init(&output->data[i])) {
        // If initialization of any new item fails, roll back
        // all previously initialized items. Existing items
        // in output are to be left unmodified.
        for (; i-- > output->capacity; ) {
          rebotarm_msgs__msg__JointPosVelCmd__fini(&output->data[i]);
        }
        return false;
      }
    }
    output->capacity = input->size;
  }
  output->size = input->size;
  for (size_t i = 0; i < input->size; ++i) {
    if (!rebotarm_msgs__msg__JointPosVelCmd__copy(
        &(input->data[i]), &(output->data[i])))
    {
      return false;
    }
  }
  return true;
}
