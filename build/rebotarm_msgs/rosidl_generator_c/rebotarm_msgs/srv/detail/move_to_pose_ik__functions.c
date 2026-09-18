// generated from rosidl_generator_c/resource/idl__functions.c.em
// with input from rebotarm_msgs:srv/MoveToPoseIK.idl
// generated code does not contain a copyright notice
#include "rebotarm_msgs/srv/detail/move_to_pose_ik__functions.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "rcutils/allocator.h"

// Include directives for member types
// Member `target_pose`
#include "geometry_msgs/msg/detail/pose__functions.h"

bool
rebotarm_msgs__srv__MoveToPoseIK_Request__init(rebotarm_msgs__srv__MoveToPoseIK_Request * msg)
{
  if (!msg) {
    return false;
  }
  // target_pose
  if (!geometry_msgs__msg__Pose__init(&msg->target_pose)) {
    rebotarm_msgs__srv__MoveToPoseIK_Request__fini(msg);
    return false;
  }
  return true;
}

void
rebotarm_msgs__srv__MoveToPoseIK_Request__fini(rebotarm_msgs__srv__MoveToPoseIK_Request * msg)
{
  if (!msg) {
    return;
  }
  // target_pose
  geometry_msgs__msg__Pose__fini(&msg->target_pose);
}

bool
rebotarm_msgs__srv__MoveToPoseIK_Request__are_equal(const rebotarm_msgs__srv__MoveToPoseIK_Request * lhs, const rebotarm_msgs__srv__MoveToPoseIK_Request * rhs)
{
  if (!lhs || !rhs) {
    return false;
  }
  // target_pose
  if (!geometry_msgs__msg__Pose__are_equal(
      &(lhs->target_pose), &(rhs->target_pose)))
  {
    return false;
  }
  return true;
}

bool
rebotarm_msgs__srv__MoveToPoseIK_Request__copy(
  const rebotarm_msgs__srv__MoveToPoseIK_Request * input,
  rebotarm_msgs__srv__MoveToPoseIK_Request * output)
{
  if (!input || !output) {
    return false;
  }
  // target_pose
  if (!geometry_msgs__msg__Pose__copy(
      &(input->target_pose), &(output->target_pose)))
  {
    return false;
  }
  return true;
}

rebotarm_msgs__srv__MoveToPoseIK_Request *
rebotarm_msgs__srv__MoveToPoseIK_Request__create(void)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rebotarm_msgs__srv__MoveToPoseIK_Request * msg = (rebotarm_msgs__srv__MoveToPoseIK_Request *)allocator.allocate(sizeof(rebotarm_msgs__srv__MoveToPoseIK_Request), allocator.state);
  if (!msg) {
    return NULL;
  }
  memset(msg, 0, sizeof(rebotarm_msgs__srv__MoveToPoseIK_Request));
  bool success = rebotarm_msgs__srv__MoveToPoseIK_Request__init(msg);
  if (!success) {
    allocator.deallocate(msg, allocator.state);
    return NULL;
  }
  return msg;
}

void
rebotarm_msgs__srv__MoveToPoseIK_Request__destroy(rebotarm_msgs__srv__MoveToPoseIK_Request * msg)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  if (msg) {
    rebotarm_msgs__srv__MoveToPoseIK_Request__fini(msg);
  }
  allocator.deallocate(msg, allocator.state);
}


bool
rebotarm_msgs__srv__MoveToPoseIK_Request__Sequence__init(rebotarm_msgs__srv__MoveToPoseIK_Request__Sequence * array, size_t size)
{
  if (!array) {
    return false;
  }
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rebotarm_msgs__srv__MoveToPoseIK_Request * data = NULL;

  if (size) {
    if (size > SIZE_MAX / sizeof(rebotarm_msgs__srv__MoveToPoseIK_Request)) {
      return false;
    }
    data = (rebotarm_msgs__srv__MoveToPoseIK_Request *)allocator.zero_allocate(size, sizeof(rebotarm_msgs__srv__MoveToPoseIK_Request), allocator.state);
    if (!data) {
      return false;
    }
    // initialize all array elements
    size_t i;
    for (i = 0; i < size; ++i) {
      bool success = rebotarm_msgs__srv__MoveToPoseIK_Request__init(&data[i]);
      if (!success) {
        break;
      }
    }
    if (i < size) {
      // if initialization failed finalize the already initialized array elements
      for (; i > 0; --i) {
        rebotarm_msgs__srv__MoveToPoseIK_Request__fini(&data[i - 1]);
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
rebotarm_msgs__srv__MoveToPoseIK_Request__Sequence__fini(rebotarm_msgs__srv__MoveToPoseIK_Request__Sequence * array)
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
      rebotarm_msgs__srv__MoveToPoseIK_Request__fini(&array->data[i]);
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

rebotarm_msgs__srv__MoveToPoseIK_Request__Sequence *
rebotarm_msgs__srv__MoveToPoseIK_Request__Sequence__create(size_t size)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rebotarm_msgs__srv__MoveToPoseIK_Request__Sequence * array = (rebotarm_msgs__srv__MoveToPoseIK_Request__Sequence *)allocator.allocate(sizeof(rebotarm_msgs__srv__MoveToPoseIK_Request__Sequence), allocator.state);
  if (!array) {
    return NULL;
  }
  bool success = rebotarm_msgs__srv__MoveToPoseIK_Request__Sequence__init(array, size);
  if (!success) {
    allocator.deallocate(array, allocator.state);
    return NULL;
  }
  return array;
}

void
rebotarm_msgs__srv__MoveToPoseIK_Request__Sequence__destroy(rebotarm_msgs__srv__MoveToPoseIK_Request__Sequence * array)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  if (array) {
    rebotarm_msgs__srv__MoveToPoseIK_Request__Sequence__fini(array);
  }
  allocator.deallocate(array, allocator.state);
}

bool
rebotarm_msgs__srv__MoveToPoseIK_Request__Sequence__are_equal(const rebotarm_msgs__srv__MoveToPoseIK_Request__Sequence * lhs, const rebotarm_msgs__srv__MoveToPoseIK_Request__Sequence * rhs)
{
  if (!lhs || !rhs) {
    return false;
  }
  if (lhs->size != rhs->size) {
    return false;
  }
  for (size_t i = 0; i < lhs->size; ++i) {
    if (!rebotarm_msgs__srv__MoveToPoseIK_Request__are_equal(&(lhs->data[i]), &(rhs->data[i]))) {
      return false;
    }
  }
  return true;
}

bool
rebotarm_msgs__srv__MoveToPoseIK_Request__Sequence__copy(
  const rebotarm_msgs__srv__MoveToPoseIK_Request__Sequence * input,
  rebotarm_msgs__srv__MoveToPoseIK_Request__Sequence * output)
{
  if (!input || !output) {
    return false;
  }
  if (output->capacity < input->size) {
    if (input->size > SIZE_MAX / sizeof(rebotarm_msgs__srv__MoveToPoseIK_Request)) {
      return false;
    }
    const size_t allocation_size =
      input->size * sizeof(rebotarm_msgs__srv__MoveToPoseIK_Request);
    rcutils_allocator_t allocator = rcutils_get_default_allocator();
    rebotarm_msgs__srv__MoveToPoseIK_Request * data =
      (rebotarm_msgs__srv__MoveToPoseIK_Request *)allocator.reallocate(
      output->data, allocation_size, allocator.state);
    if (!data) {
      return false;
    }
    // If reallocation succeeded, memory may or may not have been moved
    // to fulfill the allocation request, invalidating output->data.
    output->data = data;
    for (size_t i = output->capacity; i < input->size; ++i) {
      if (!rebotarm_msgs__srv__MoveToPoseIK_Request__init(&output->data[i])) {
        // If initialization of any new item fails, roll back
        // all previously initialized items. Existing items
        // in output are to be left unmodified.
        for (; i-- > output->capacity; ) {
          rebotarm_msgs__srv__MoveToPoseIK_Request__fini(&output->data[i]);
        }
        return false;
      }
    }
    output->capacity = input->size;
  }
  output->size = input->size;
  for (size_t i = 0; i < input->size; ++i) {
    if (!rebotarm_msgs__srv__MoveToPoseIK_Request__copy(
        &(input->data[i]), &(output->data[i])))
    {
      return false;
    }
  }
  return true;
}


// Include directives for member types
// Member `message`
#include "rosidl_runtime_c/string_functions.h"
// Member `q_solution`
#include "rosidl_runtime_c/primitives_sequence_functions.h"

bool
rebotarm_msgs__srv__MoveToPoseIK_Response__init(rebotarm_msgs__srv__MoveToPoseIK_Response * msg)
{
  if (!msg) {
    return false;
  }
  // success
  // message
  if (!rosidl_runtime_c__String__init(&msg->message)) {
    rebotarm_msgs__srv__MoveToPoseIK_Response__fini(msg);
    return false;
  }
  // q_solution
  if (!rosidl_runtime_c__double__Sequence__init(&msg->q_solution, 0)) {
    rebotarm_msgs__srv__MoveToPoseIK_Response__fini(msg);
    return false;
  }
  return true;
}

void
rebotarm_msgs__srv__MoveToPoseIK_Response__fini(rebotarm_msgs__srv__MoveToPoseIK_Response * msg)
{
  if (!msg) {
    return;
  }
  // success
  // message
  rosidl_runtime_c__String__fini(&msg->message);
  // q_solution
  rosidl_runtime_c__double__Sequence__fini(&msg->q_solution);
}

bool
rebotarm_msgs__srv__MoveToPoseIK_Response__are_equal(const rebotarm_msgs__srv__MoveToPoseIK_Response * lhs, const rebotarm_msgs__srv__MoveToPoseIK_Response * rhs)
{
  if (!lhs || !rhs) {
    return false;
  }
  // success
  if (lhs->success != rhs->success) {
    return false;
  }
  // message
  if (!rosidl_runtime_c__String__are_equal(
      &(lhs->message), &(rhs->message)))
  {
    return false;
  }
  // q_solution
  if (!rosidl_runtime_c__double__Sequence__are_equal(
      &(lhs->q_solution), &(rhs->q_solution)))
  {
    return false;
  }
  return true;
}

bool
rebotarm_msgs__srv__MoveToPoseIK_Response__copy(
  const rebotarm_msgs__srv__MoveToPoseIK_Response * input,
  rebotarm_msgs__srv__MoveToPoseIK_Response * output)
{
  if (!input || !output) {
    return false;
  }
  // success
  output->success = input->success;
  // message
  if (!rosidl_runtime_c__String__copy(
      &(input->message), &(output->message)))
  {
    return false;
  }
  // q_solution
  if (!rosidl_runtime_c__double__Sequence__copy(
      &(input->q_solution), &(output->q_solution)))
  {
    return false;
  }
  return true;
}

rebotarm_msgs__srv__MoveToPoseIK_Response *
rebotarm_msgs__srv__MoveToPoseIK_Response__create(void)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rebotarm_msgs__srv__MoveToPoseIK_Response * msg = (rebotarm_msgs__srv__MoveToPoseIK_Response *)allocator.allocate(sizeof(rebotarm_msgs__srv__MoveToPoseIK_Response), allocator.state);
  if (!msg) {
    return NULL;
  }
  memset(msg, 0, sizeof(rebotarm_msgs__srv__MoveToPoseIK_Response));
  bool success = rebotarm_msgs__srv__MoveToPoseIK_Response__init(msg);
  if (!success) {
    allocator.deallocate(msg, allocator.state);
    return NULL;
  }
  return msg;
}

void
rebotarm_msgs__srv__MoveToPoseIK_Response__destroy(rebotarm_msgs__srv__MoveToPoseIK_Response * msg)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  if (msg) {
    rebotarm_msgs__srv__MoveToPoseIK_Response__fini(msg);
  }
  allocator.deallocate(msg, allocator.state);
}


bool
rebotarm_msgs__srv__MoveToPoseIK_Response__Sequence__init(rebotarm_msgs__srv__MoveToPoseIK_Response__Sequence * array, size_t size)
{
  if (!array) {
    return false;
  }
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rebotarm_msgs__srv__MoveToPoseIK_Response * data = NULL;

  if (size) {
    if (size > SIZE_MAX / sizeof(rebotarm_msgs__srv__MoveToPoseIK_Response)) {
      return false;
    }
    data = (rebotarm_msgs__srv__MoveToPoseIK_Response *)allocator.zero_allocate(size, sizeof(rebotarm_msgs__srv__MoveToPoseIK_Response), allocator.state);
    if (!data) {
      return false;
    }
    // initialize all array elements
    size_t i;
    for (i = 0; i < size; ++i) {
      bool success = rebotarm_msgs__srv__MoveToPoseIK_Response__init(&data[i]);
      if (!success) {
        break;
      }
    }
    if (i < size) {
      // if initialization failed finalize the already initialized array elements
      for (; i > 0; --i) {
        rebotarm_msgs__srv__MoveToPoseIK_Response__fini(&data[i - 1]);
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
rebotarm_msgs__srv__MoveToPoseIK_Response__Sequence__fini(rebotarm_msgs__srv__MoveToPoseIK_Response__Sequence * array)
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
      rebotarm_msgs__srv__MoveToPoseIK_Response__fini(&array->data[i]);
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

rebotarm_msgs__srv__MoveToPoseIK_Response__Sequence *
rebotarm_msgs__srv__MoveToPoseIK_Response__Sequence__create(size_t size)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rebotarm_msgs__srv__MoveToPoseIK_Response__Sequence * array = (rebotarm_msgs__srv__MoveToPoseIK_Response__Sequence *)allocator.allocate(sizeof(rebotarm_msgs__srv__MoveToPoseIK_Response__Sequence), allocator.state);
  if (!array) {
    return NULL;
  }
  bool success = rebotarm_msgs__srv__MoveToPoseIK_Response__Sequence__init(array, size);
  if (!success) {
    allocator.deallocate(array, allocator.state);
    return NULL;
  }
  return array;
}

void
rebotarm_msgs__srv__MoveToPoseIK_Response__Sequence__destroy(rebotarm_msgs__srv__MoveToPoseIK_Response__Sequence * array)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  if (array) {
    rebotarm_msgs__srv__MoveToPoseIK_Response__Sequence__fini(array);
  }
  allocator.deallocate(array, allocator.state);
}

bool
rebotarm_msgs__srv__MoveToPoseIK_Response__Sequence__are_equal(const rebotarm_msgs__srv__MoveToPoseIK_Response__Sequence * lhs, const rebotarm_msgs__srv__MoveToPoseIK_Response__Sequence * rhs)
{
  if (!lhs || !rhs) {
    return false;
  }
  if (lhs->size != rhs->size) {
    return false;
  }
  for (size_t i = 0; i < lhs->size; ++i) {
    if (!rebotarm_msgs__srv__MoveToPoseIK_Response__are_equal(&(lhs->data[i]), &(rhs->data[i]))) {
      return false;
    }
  }
  return true;
}

bool
rebotarm_msgs__srv__MoveToPoseIK_Response__Sequence__copy(
  const rebotarm_msgs__srv__MoveToPoseIK_Response__Sequence * input,
  rebotarm_msgs__srv__MoveToPoseIK_Response__Sequence * output)
{
  if (!input || !output) {
    return false;
  }
  if (output->capacity < input->size) {
    if (input->size > SIZE_MAX / sizeof(rebotarm_msgs__srv__MoveToPoseIK_Response)) {
      return false;
    }
    const size_t allocation_size =
      input->size * sizeof(rebotarm_msgs__srv__MoveToPoseIK_Response);
    rcutils_allocator_t allocator = rcutils_get_default_allocator();
    rebotarm_msgs__srv__MoveToPoseIK_Response * data =
      (rebotarm_msgs__srv__MoveToPoseIK_Response *)allocator.reallocate(
      output->data, allocation_size, allocator.state);
    if (!data) {
      return false;
    }
    // If reallocation succeeded, memory may or may not have been moved
    // to fulfill the allocation request, invalidating output->data.
    output->data = data;
    for (size_t i = output->capacity; i < input->size; ++i) {
      if (!rebotarm_msgs__srv__MoveToPoseIK_Response__init(&output->data[i])) {
        // If initialization of any new item fails, roll back
        // all previously initialized items. Existing items
        // in output are to be left unmodified.
        for (; i-- > output->capacity; ) {
          rebotarm_msgs__srv__MoveToPoseIK_Response__fini(&output->data[i]);
        }
        return false;
      }
    }
    output->capacity = input->size;
  }
  output->size = input->size;
  for (size_t i = 0; i < input->size; ++i) {
    if (!rebotarm_msgs__srv__MoveToPoseIK_Response__copy(
        &(input->data[i]), &(output->data[i])))
    {
      return false;
    }
  }
  return true;
}


// Include directives for member types
// Member `info`
#include "service_msgs/msg/detail/service_event_info__functions.h"
// Member `request`
// Member `response`
// already included above
// #include "rebotarm_msgs/srv/detail/move_to_pose_ik__functions.h"

bool
rebotarm_msgs__srv__MoveToPoseIK_Event__init(rebotarm_msgs__srv__MoveToPoseIK_Event * msg)
{
  if (!msg) {
    return false;
  }
  // info
  if (!service_msgs__msg__ServiceEventInfo__init(&msg->info)) {
    rebotarm_msgs__srv__MoveToPoseIK_Event__fini(msg);
    return false;
  }
  // request
  if (!rebotarm_msgs__srv__MoveToPoseIK_Request__Sequence__init(&msg->request, 0)) {
    rebotarm_msgs__srv__MoveToPoseIK_Event__fini(msg);
    return false;
  }
  // response
  if (!rebotarm_msgs__srv__MoveToPoseIK_Response__Sequence__init(&msg->response, 0)) {
    rebotarm_msgs__srv__MoveToPoseIK_Event__fini(msg);
    return false;
  }
  return true;
}

void
rebotarm_msgs__srv__MoveToPoseIK_Event__fini(rebotarm_msgs__srv__MoveToPoseIK_Event * msg)
{
  if (!msg) {
    return;
  }
  // info
  service_msgs__msg__ServiceEventInfo__fini(&msg->info);
  // request
  rebotarm_msgs__srv__MoveToPoseIK_Request__Sequence__fini(&msg->request);
  // response
  rebotarm_msgs__srv__MoveToPoseIK_Response__Sequence__fini(&msg->response);
}

bool
rebotarm_msgs__srv__MoveToPoseIK_Event__are_equal(const rebotarm_msgs__srv__MoveToPoseIK_Event * lhs, const rebotarm_msgs__srv__MoveToPoseIK_Event * rhs)
{
  if (!lhs || !rhs) {
    return false;
  }
  // info
  if (!service_msgs__msg__ServiceEventInfo__are_equal(
      &(lhs->info), &(rhs->info)))
  {
    return false;
  }
  // request
  if (!rebotarm_msgs__srv__MoveToPoseIK_Request__Sequence__are_equal(
      &(lhs->request), &(rhs->request)))
  {
    return false;
  }
  // response
  if (!rebotarm_msgs__srv__MoveToPoseIK_Response__Sequence__are_equal(
      &(lhs->response), &(rhs->response)))
  {
    return false;
  }
  return true;
}

bool
rebotarm_msgs__srv__MoveToPoseIK_Event__copy(
  const rebotarm_msgs__srv__MoveToPoseIK_Event * input,
  rebotarm_msgs__srv__MoveToPoseIK_Event * output)
{
  if (!input || !output) {
    return false;
  }
  // info
  if (!service_msgs__msg__ServiceEventInfo__copy(
      &(input->info), &(output->info)))
  {
    return false;
  }
  // request
  if (!rebotarm_msgs__srv__MoveToPoseIK_Request__Sequence__copy(
      &(input->request), &(output->request)))
  {
    return false;
  }
  // response
  if (!rebotarm_msgs__srv__MoveToPoseIK_Response__Sequence__copy(
      &(input->response), &(output->response)))
  {
    return false;
  }
  return true;
}

rebotarm_msgs__srv__MoveToPoseIK_Event *
rebotarm_msgs__srv__MoveToPoseIK_Event__create(void)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rebotarm_msgs__srv__MoveToPoseIK_Event * msg = (rebotarm_msgs__srv__MoveToPoseIK_Event *)allocator.allocate(sizeof(rebotarm_msgs__srv__MoveToPoseIK_Event), allocator.state);
  if (!msg) {
    return NULL;
  }
  memset(msg, 0, sizeof(rebotarm_msgs__srv__MoveToPoseIK_Event));
  bool success = rebotarm_msgs__srv__MoveToPoseIK_Event__init(msg);
  if (!success) {
    allocator.deallocate(msg, allocator.state);
    return NULL;
  }
  return msg;
}

void
rebotarm_msgs__srv__MoveToPoseIK_Event__destroy(rebotarm_msgs__srv__MoveToPoseIK_Event * msg)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  if (msg) {
    rebotarm_msgs__srv__MoveToPoseIK_Event__fini(msg);
  }
  allocator.deallocate(msg, allocator.state);
}


bool
rebotarm_msgs__srv__MoveToPoseIK_Event__Sequence__init(rebotarm_msgs__srv__MoveToPoseIK_Event__Sequence * array, size_t size)
{
  if (!array) {
    return false;
  }
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rebotarm_msgs__srv__MoveToPoseIK_Event * data = NULL;

  if (size) {
    if (size > SIZE_MAX / sizeof(rebotarm_msgs__srv__MoveToPoseIK_Event)) {
      return false;
    }
    data = (rebotarm_msgs__srv__MoveToPoseIK_Event *)allocator.zero_allocate(size, sizeof(rebotarm_msgs__srv__MoveToPoseIK_Event), allocator.state);
    if (!data) {
      return false;
    }
    // initialize all array elements
    size_t i;
    for (i = 0; i < size; ++i) {
      bool success = rebotarm_msgs__srv__MoveToPoseIK_Event__init(&data[i]);
      if (!success) {
        break;
      }
    }
    if (i < size) {
      // if initialization failed finalize the already initialized array elements
      for (; i > 0; --i) {
        rebotarm_msgs__srv__MoveToPoseIK_Event__fini(&data[i - 1]);
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
rebotarm_msgs__srv__MoveToPoseIK_Event__Sequence__fini(rebotarm_msgs__srv__MoveToPoseIK_Event__Sequence * array)
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
      rebotarm_msgs__srv__MoveToPoseIK_Event__fini(&array->data[i]);
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

rebotarm_msgs__srv__MoveToPoseIK_Event__Sequence *
rebotarm_msgs__srv__MoveToPoseIK_Event__Sequence__create(size_t size)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rebotarm_msgs__srv__MoveToPoseIK_Event__Sequence * array = (rebotarm_msgs__srv__MoveToPoseIK_Event__Sequence *)allocator.allocate(sizeof(rebotarm_msgs__srv__MoveToPoseIK_Event__Sequence), allocator.state);
  if (!array) {
    return NULL;
  }
  bool success = rebotarm_msgs__srv__MoveToPoseIK_Event__Sequence__init(array, size);
  if (!success) {
    allocator.deallocate(array, allocator.state);
    return NULL;
  }
  return array;
}

void
rebotarm_msgs__srv__MoveToPoseIK_Event__Sequence__destroy(rebotarm_msgs__srv__MoveToPoseIK_Event__Sequence * array)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  if (array) {
    rebotarm_msgs__srv__MoveToPoseIK_Event__Sequence__fini(array);
  }
  allocator.deallocate(array, allocator.state);
}

bool
rebotarm_msgs__srv__MoveToPoseIK_Event__Sequence__are_equal(const rebotarm_msgs__srv__MoveToPoseIK_Event__Sequence * lhs, const rebotarm_msgs__srv__MoveToPoseIK_Event__Sequence * rhs)
{
  if (!lhs || !rhs) {
    return false;
  }
  if (lhs->size != rhs->size) {
    return false;
  }
  for (size_t i = 0; i < lhs->size; ++i) {
    if (!rebotarm_msgs__srv__MoveToPoseIK_Event__are_equal(&(lhs->data[i]), &(rhs->data[i]))) {
      return false;
    }
  }
  return true;
}

bool
rebotarm_msgs__srv__MoveToPoseIK_Event__Sequence__copy(
  const rebotarm_msgs__srv__MoveToPoseIK_Event__Sequence * input,
  rebotarm_msgs__srv__MoveToPoseIK_Event__Sequence * output)
{
  if (!input || !output) {
    return false;
  }
  if (output->capacity < input->size) {
    if (input->size > SIZE_MAX / sizeof(rebotarm_msgs__srv__MoveToPoseIK_Event)) {
      return false;
    }
    const size_t allocation_size =
      input->size * sizeof(rebotarm_msgs__srv__MoveToPoseIK_Event);
    rcutils_allocator_t allocator = rcutils_get_default_allocator();
    rebotarm_msgs__srv__MoveToPoseIK_Event * data =
      (rebotarm_msgs__srv__MoveToPoseIK_Event *)allocator.reallocate(
      output->data, allocation_size, allocator.state);
    if (!data) {
      return false;
    }
    // If reallocation succeeded, memory may or may not have been moved
    // to fulfill the allocation request, invalidating output->data.
    output->data = data;
    for (size_t i = output->capacity; i < input->size; ++i) {
      if (!rebotarm_msgs__srv__MoveToPoseIK_Event__init(&output->data[i])) {
        // If initialization of any new item fails, roll back
        // all previously initialized items. Existing items
        // in output are to be left unmodified.
        for (; i-- > output->capacity; ) {
          rebotarm_msgs__srv__MoveToPoseIK_Event__fini(&output->data[i]);
        }
        return false;
      }
    }
    output->capacity = input->size;
  }
  output->size = input->size;
  for (size_t i = 0; i < input->size; ++i) {
    if (!rebotarm_msgs__srv__MoveToPoseIK_Event__copy(
        &(input->data[i]), &(output->data[i])))
    {
      return false;
    }
  }
  return true;
}
