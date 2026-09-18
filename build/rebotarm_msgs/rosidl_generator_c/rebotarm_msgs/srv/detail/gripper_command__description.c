// generated from rosidl_generator_c/resource/idl__description.c.em
// with input from rebotarm_msgs:srv/GripperCommand.idl
// generated code does not contain a copyright notice

#include "rebotarm_msgs/srv/detail/gripper_command__functions.h"

ROSIDL_GENERATOR_C_PUBLIC_rebotarm_msgs
const rosidl_type_hash_t *
rebotarm_msgs__srv__GripperCommand__get_type_hash(
  const rosidl_service_type_support_t * type_support)
{
  (void)type_support;
  static rosidl_type_hash_t hash = {1, {
      0x06, 0xb9, 0xdc, 0xa0, 0xac, 0x65, 0xa5, 0x5f,
      0x9a, 0x7e, 0x4f, 0x6e, 0x46, 0x2a, 0xf3, 0x46,
      0x2b, 0x94, 0xbf, 0xc7, 0x77, 0x5c, 0x59, 0xa9,
      0xd6, 0xa1, 0x14, 0xb2, 0xf6, 0x2a, 0x3c, 0x02,
    }};
  return &hash;
}

ROSIDL_GENERATOR_C_PUBLIC_rebotarm_msgs
const rosidl_type_hash_t *
rebotarm_msgs__srv__GripperCommand_Request__get_type_hash(
  const rosidl_message_type_support_t * type_support)
{
  (void)type_support;
  static rosidl_type_hash_t hash = {1, {
      0x60, 0xf5, 0x8a, 0x8e, 0x7a, 0x3a, 0x7f, 0x4d,
      0x7f, 0xb1, 0x47, 0x22, 0x88, 0x56, 0xbb, 0xf5,
      0x5e, 0xe1, 0x4b, 0xa4, 0xad, 0x14, 0x19, 0x84,
      0x44, 0xf8, 0x69, 0x96, 0x32, 0xd8, 0x35, 0x4a,
    }};
  return &hash;
}

ROSIDL_GENERATOR_C_PUBLIC_rebotarm_msgs
const rosidl_type_hash_t *
rebotarm_msgs__srv__GripperCommand_Response__get_type_hash(
  const rosidl_message_type_support_t * type_support)
{
  (void)type_support;
  static rosidl_type_hash_t hash = {1, {
      0x83, 0x60, 0xa3, 0xd9, 0x7f, 0x9f, 0xae, 0xa4,
      0x4f, 0xbe, 0xc2, 0x1d, 0xbe, 0xf2, 0xdd, 0x06,
      0x15, 0x21, 0xd2, 0xbb, 0x64, 0x7c, 0x9e, 0x2f,
      0xc4, 0x7c, 0xe5, 0xae, 0x3b, 0x41, 0x33, 0x62,
    }};
  return &hash;
}

ROSIDL_GENERATOR_C_PUBLIC_rebotarm_msgs
const rosidl_type_hash_t *
rebotarm_msgs__srv__GripperCommand_Event__get_type_hash(
  const rosidl_message_type_support_t * type_support)
{
  (void)type_support;
  static rosidl_type_hash_t hash = {1, {
      0xd9, 0x71, 0xa8, 0xa3, 0x8d, 0x38, 0x14, 0x97,
      0x17, 0x10, 0x5f, 0xc8, 0xc1, 0xe1, 0xc3, 0xcd,
      0x92, 0x3c, 0x90, 0x88, 0xe0, 0x80, 0x97, 0x8d,
      0x57, 0xe9, 0x6f, 0x04, 0xca, 0xc6, 0xb7, 0xd1,
    }};
  return &hash;
}

#include <assert.h>
#include <string.h>

// Include directives for referenced types
#include "builtin_interfaces/msg/detail/time__functions.h"
#include "service_msgs/msg/detail/service_event_info__functions.h"

// Hashes for external referenced types
#ifndef NDEBUG
static const rosidl_type_hash_t builtin_interfaces__msg__Time__EXPECTED_HASH = {1, {
    0xb1, 0x06, 0x23, 0x5e, 0x25, 0xa4, 0xc5, 0xed,
    0x35, 0x09, 0x8a, 0xa0, 0xa6, 0x1a, 0x3e, 0xe9,
    0xc9, 0xb1, 0x8d, 0x19, 0x7f, 0x39, 0x8b, 0x0e,
    0x42, 0x06, 0xce, 0xa9, 0xac, 0xf9, 0xc1, 0x97,
  }};
static const rosidl_type_hash_t service_msgs__msg__ServiceEventInfo__EXPECTED_HASH = {1, {
    0x41, 0xbc, 0xbb, 0xe0, 0x7a, 0x75, 0xc9, 0xb5,
    0x2b, 0xc9, 0x6b, 0xfd, 0x5c, 0x24, 0xd7, 0xf0,
    0xfc, 0x0a, 0x08, 0xc0, 0xcb, 0x79, 0x21, 0xb3,
    0x37, 0x3c, 0x57, 0x32, 0x34, 0x5a, 0x6f, 0x45,
  }};
#endif

static char rebotarm_msgs__srv__GripperCommand__TYPE_NAME[] = "rebotarm_msgs/srv/GripperCommand";
static char builtin_interfaces__msg__Time__TYPE_NAME[] = "builtin_interfaces/msg/Time";
static char rebotarm_msgs__srv__GripperCommand_Event__TYPE_NAME[] = "rebotarm_msgs/srv/GripperCommand_Event";
static char rebotarm_msgs__srv__GripperCommand_Request__TYPE_NAME[] = "rebotarm_msgs/srv/GripperCommand_Request";
static char rebotarm_msgs__srv__GripperCommand_Response__TYPE_NAME[] = "rebotarm_msgs/srv/GripperCommand_Response";
static char service_msgs__msg__ServiceEventInfo__TYPE_NAME[] = "service_msgs/msg/ServiceEventInfo";

// Define type names, field names, and default values
static char rebotarm_msgs__srv__GripperCommand__FIELD_NAME__request_message[] = "request_message";
static char rebotarm_msgs__srv__GripperCommand__FIELD_NAME__response_message[] = "response_message";
static char rebotarm_msgs__srv__GripperCommand__FIELD_NAME__event_message[] = "event_message";

static rosidl_runtime_c__type_description__Field rebotarm_msgs__srv__GripperCommand__FIELDS[] = {
  {
    {rebotarm_msgs__srv__GripperCommand__FIELD_NAME__request_message, 15, 15},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_NESTED_TYPE,
      0,
      0,
      {rebotarm_msgs__srv__GripperCommand_Request__TYPE_NAME, 40, 40},
    },
    {NULL, 0, 0},
  },
  {
    {rebotarm_msgs__srv__GripperCommand__FIELD_NAME__response_message, 16, 16},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_NESTED_TYPE,
      0,
      0,
      {rebotarm_msgs__srv__GripperCommand_Response__TYPE_NAME, 41, 41},
    },
    {NULL, 0, 0},
  },
  {
    {rebotarm_msgs__srv__GripperCommand__FIELD_NAME__event_message, 13, 13},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_NESTED_TYPE,
      0,
      0,
      {rebotarm_msgs__srv__GripperCommand_Event__TYPE_NAME, 38, 38},
    },
    {NULL, 0, 0},
  },
};

static rosidl_runtime_c__type_description__IndividualTypeDescription rebotarm_msgs__srv__GripperCommand__REFERENCED_TYPE_DESCRIPTIONS[] = {
  {
    {builtin_interfaces__msg__Time__TYPE_NAME, 27, 27},
    {NULL, 0, 0},
  },
  {
    {rebotarm_msgs__srv__GripperCommand_Event__TYPE_NAME, 38, 38},
    {NULL, 0, 0},
  },
  {
    {rebotarm_msgs__srv__GripperCommand_Request__TYPE_NAME, 40, 40},
    {NULL, 0, 0},
  },
  {
    {rebotarm_msgs__srv__GripperCommand_Response__TYPE_NAME, 41, 41},
    {NULL, 0, 0},
  },
  {
    {service_msgs__msg__ServiceEventInfo__TYPE_NAME, 33, 33},
    {NULL, 0, 0},
  },
};

const rosidl_runtime_c__type_description__TypeDescription *
rebotarm_msgs__srv__GripperCommand__get_type_description(
  const rosidl_service_type_support_t * type_support)
{
  (void)type_support;
  static bool constructed = false;
  static const rosidl_runtime_c__type_description__TypeDescription description = {
    {
      {rebotarm_msgs__srv__GripperCommand__TYPE_NAME, 32, 32},
      {rebotarm_msgs__srv__GripperCommand__FIELDS, 3, 3},
    },
    {rebotarm_msgs__srv__GripperCommand__REFERENCED_TYPE_DESCRIPTIONS, 5, 5},
  };
  if (!constructed) {
    assert(0 == memcmp(&builtin_interfaces__msg__Time__EXPECTED_HASH, builtin_interfaces__msg__Time__get_type_hash(NULL), sizeof(rosidl_type_hash_t)));
    description.referenced_type_descriptions.data[0].fields = builtin_interfaces__msg__Time__get_type_description(NULL)->type_description.fields;
    description.referenced_type_descriptions.data[1].fields = rebotarm_msgs__srv__GripperCommand_Event__get_type_description(NULL)->type_description.fields;
    description.referenced_type_descriptions.data[2].fields = rebotarm_msgs__srv__GripperCommand_Request__get_type_description(NULL)->type_description.fields;
    description.referenced_type_descriptions.data[3].fields = rebotarm_msgs__srv__GripperCommand_Response__get_type_description(NULL)->type_description.fields;
    assert(0 == memcmp(&service_msgs__msg__ServiceEventInfo__EXPECTED_HASH, service_msgs__msg__ServiceEventInfo__get_type_hash(NULL), sizeof(rosidl_type_hash_t)));
    description.referenced_type_descriptions.data[4].fields = service_msgs__msg__ServiceEventInfo__get_type_description(NULL)->type_description.fields;
    constructed = true;
  }
  return &description;
}
// Define type names, field names, and default values
static char rebotarm_msgs__srv__GripperCommand_Request__FIELD_NAME__position[] = "position";
static char rebotarm_msgs__srv__GripperCommand_Request__FIELD_NAME__timeout[] = "timeout";

static rosidl_runtime_c__type_description__Field rebotarm_msgs__srv__GripperCommand_Request__FIELDS[] = {
  {
    {rebotarm_msgs__srv__GripperCommand_Request__FIELD_NAME__position, 8, 8},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_DOUBLE,
      0,
      0,
      {NULL, 0, 0},
    },
    {NULL, 0, 0},
  },
  {
    {rebotarm_msgs__srv__GripperCommand_Request__FIELD_NAME__timeout, 7, 7},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_DOUBLE,
      0,
      0,
      {NULL, 0, 0},
    },
    {NULL, 0, 0},
  },
};

const rosidl_runtime_c__type_description__TypeDescription *
rebotarm_msgs__srv__GripperCommand_Request__get_type_description(
  const rosidl_message_type_support_t * type_support)
{
  (void)type_support;
  static bool constructed = false;
  static const rosidl_runtime_c__type_description__TypeDescription description = {
    {
      {rebotarm_msgs__srv__GripperCommand_Request__TYPE_NAME, 40, 40},
      {rebotarm_msgs__srv__GripperCommand_Request__FIELDS, 2, 2},
    },
    {NULL, 0, 0},
  };
  if (!constructed) {
    constructed = true;
  }
  return &description;
}
// Define type names, field names, and default values
static char rebotarm_msgs__srv__GripperCommand_Response__FIELD_NAME__success[] = "success";
static char rebotarm_msgs__srv__GripperCommand_Response__FIELD_NAME__reached_position[] = "reached_position";
static char rebotarm_msgs__srv__GripperCommand_Response__FIELD_NAME__message[] = "message";

static rosidl_runtime_c__type_description__Field rebotarm_msgs__srv__GripperCommand_Response__FIELDS[] = {
  {
    {rebotarm_msgs__srv__GripperCommand_Response__FIELD_NAME__success, 7, 7},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_BOOLEAN,
      0,
      0,
      {NULL, 0, 0},
    },
    {NULL, 0, 0},
  },
  {
    {rebotarm_msgs__srv__GripperCommand_Response__FIELD_NAME__reached_position, 16, 16},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_DOUBLE,
      0,
      0,
      {NULL, 0, 0},
    },
    {NULL, 0, 0},
  },
  {
    {rebotarm_msgs__srv__GripperCommand_Response__FIELD_NAME__message, 7, 7},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_STRING,
      0,
      0,
      {NULL, 0, 0},
    },
    {NULL, 0, 0},
  },
};

const rosidl_runtime_c__type_description__TypeDescription *
rebotarm_msgs__srv__GripperCommand_Response__get_type_description(
  const rosidl_message_type_support_t * type_support)
{
  (void)type_support;
  static bool constructed = false;
  static const rosidl_runtime_c__type_description__TypeDescription description = {
    {
      {rebotarm_msgs__srv__GripperCommand_Response__TYPE_NAME, 41, 41},
      {rebotarm_msgs__srv__GripperCommand_Response__FIELDS, 3, 3},
    },
    {NULL, 0, 0},
  };
  if (!constructed) {
    constructed = true;
  }
  return &description;
}
// Define type names, field names, and default values
static char rebotarm_msgs__srv__GripperCommand_Event__FIELD_NAME__info[] = "info";
static char rebotarm_msgs__srv__GripperCommand_Event__FIELD_NAME__request[] = "request";
static char rebotarm_msgs__srv__GripperCommand_Event__FIELD_NAME__response[] = "response";

static rosidl_runtime_c__type_description__Field rebotarm_msgs__srv__GripperCommand_Event__FIELDS[] = {
  {
    {rebotarm_msgs__srv__GripperCommand_Event__FIELD_NAME__info, 4, 4},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_NESTED_TYPE,
      0,
      0,
      {service_msgs__msg__ServiceEventInfo__TYPE_NAME, 33, 33},
    },
    {NULL, 0, 0},
  },
  {
    {rebotarm_msgs__srv__GripperCommand_Event__FIELD_NAME__request, 7, 7},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_NESTED_TYPE_BOUNDED_SEQUENCE,
      1,
      0,
      {rebotarm_msgs__srv__GripperCommand_Request__TYPE_NAME, 40, 40},
    },
    {NULL, 0, 0},
  },
  {
    {rebotarm_msgs__srv__GripperCommand_Event__FIELD_NAME__response, 8, 8},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_NESTED_TYPE_BOUNDED_SEQUENCE,
      1,
      0,
      {rebotarm_msgs__srv__GripperCommand_Response__TYPE_NAME, 41, 41},
    },
    {NULL, 0, 0},
  },
};

static rosidl_runtime_c__type_description__IndividualTypeDescription rebotarm_msgs__srv__GripperCommand_Event__REFERENCED_TYPE_DESCRIPTIONS[] = {
  {
    {builtin_interfaces__msg__Time__TYPE_NAME, 27, 27},
    {NULL, 0, 0},
  },
  {
    {rebotarm_msgs__srv__GripperCommand_Request__TYPE_NAME, 40, 40},
    {NULL, 0, 0},
  },
  {
    {rebotarm_msgs__srv__GripperCommand_Response__TYPE_NAME, 41, 41},
    {NULL, 0, 0},
  },
  {
    {service_msgs__msg__ServiceEventInfo__TYPE_NAME, 33, 33},
    {NULL, 0, 0},
  },
};

const rosidl_runtime_c__type_description__TypeDescription *
rebotarm_msgs__srv__GripperCommand_Event__get_type_description(
  const rosidl_message_type_support_t * type_support)
{
  (void)type_support;
  static bool constructed = false;
  static const rosidl_runtime_c__type_description__TypeDescription description = {
    {
      {rebotarm_msgs__srv__GripperCommand_Event__TYPE_NAME, 38, 38},
      {rebotarm_msgs__srv__GripperCommand_Event__FIELDS, 3, 3},
    },
    {rebotarm_msgs__srv__GripperCommand_Event__REFERENCED_TYPE_DESCRIPTIONS, 4, 4},
  };
  if (!constructed) {
    assert(0 == memcmp(&builtin_interfaces__msg__Time__EXPECTED_HASH, builtin_interfaces__msg__Time__get_type_hash(NULL), sizeof(rosidl_type_hash_t)));
    description.referenced_type_descriptions.data[0].fields = builtin_interfaces__msg__Time__get_type_description(NULL)->type_description.fields;
    description.referenced_type_descriptions.data[1].fields = rebotarm_msgs__srv__GripperCommand_Request__get_type_description(NULL)->type_description.fields;
    description.referenced_type_descriptions.data[2].fields = rebotarm_msgs__srv__GripperCommand_Response__get_type_description(NULL)->type_description.fields;
    assert(0 == memcmp(&service_msgs__msg__ServiceEventInfo__EXPECTED_HASH, service_msgs__msg__ServiceEventInfo__get_type_hash(NULL), sizeof(rosidl_type_hash_t)));
    description.referenced_type_descriptions.data[3].fields = service_msgs__msg__ServiceEventInfo__get_type_description(NULL)->type_description.fields;
    constructed = true;
  }
  return &description;
}

static char toplevel_type_raw_source[] =
  "float64 position\n"
  "float64 timeout\n"
  "---\n"
  "bool success\n"
  "float64 reached_position\n"
  "string message";

static char srv_encoding[] = "srv";
static char implicit_encoding[] = "implicit";

// Define all individual source functions

const rosidl_runtime_c__type_description__TypeSource *
rebotarm_msgs__srv__GripperCommand__get_individual_type_description_source(
  const rosidl_service_type_support_t * type_support)
{
  (void)type_support;
  static const rosidl_runtime_c__type_description__TypeSource source = {
    {rebotarm_msgs__srv__GripperCommand__TYPE_NAME, 32, 32},
    {srv_encoding, 3, 3},
    {toplevel_type_raw_source, 90, 90},
  };
  return &source;
}

const rosidl_runtime_c__type_description__TypeSource *
rebotarm_msgs__srv__GripperCommand_Request__get_individual_type_description_source(
  const rosidl_message_type_support_t * type_support)
{
  (void)type_support;
  static const rosidl_runtime_c__type_description__TypeSource source = {
    {rebotarm_msgs__srv__GripperCommand_Request__TYPE_NAME, 40, 40},
    {implicit_encoding, 8, 8},
    {NULL, 0, 0},
  };
  return &source;
}

const rosidl_runtime_c__type_description__TypeSource *
rebotarm_msgs__srv__GripperCommand_Response__get_individual_type_description_source(
  const rosidl_message_type_support_t * type_support)
{
  (void)type_support;
  static const rosidl_runtime_c__type_description__TypeSource source = {
    {rebotarm_msgs__srv__GripperCommand_Response__TYPE_NAME, 41, 41},
    {implicit_encoding, 8, 8},
    {NULL, 0, 0},
  };
  return &source;
}

const rosidl_runtime_c__type_description__TypeSource *
rebotarm_msgs__srv__GripperCommand_Event__get_individual_type_description_source(
  const rosidl_message_type_support_t * type_support)
{
  (void)type_support;
  static const rosidl_runtime_c__type_description__TypeSource source = {
    {rebotarm_msgs__srv__GripperCommand_Event__TYPE_NAME, 38, 38},
    {implicit_encoding, 8, 8},
    {NULL, 0, 0},
  };
  return &source;
}

const rosidl_runtime_c__type_description__TypeSource__Sequence *
rebotarm_msgs__srv__GripperCommand__get_type_description_sources(
  const rosidl_service_type_support_t * type_support)
{
  (void)type_support;
  static rosidl_runtime_c__type_description__TypeSource sources[6];
  static const rosidl_runtime_c__type_description__TypeSource__Sequence source_sequence = {sources, 6, 6};
  static bool constructed = false;
  if (!constructed) {
    sources[0] = *rebotarm_msgs__srv__GripperCommand__get_individual_type_description_source(NULL),
    sources[1] = *builtin_interfaces__msg__Time__get_individual_type_description_source(NULL);
    sources[2] = *rebotarm_msgs__srv__GripperCommand_Event__get_individual_type_description_source(NULL);
    sources[3] = *rebotarm_msgs__srv__GripperCommand_Request__get_individual_type_description_source(NULL);
    sources[4] = *rebotarm_msgs__srv__GripperCommand_Response__get_individual_type_description_source(NULL);
    sources[5] = *service_msgs__msg__ServiceEventInfo__get_individual_type_description_source(NULL);
    constructed = true;
  }
  return &source_sequence;
}

const rosidl_runtime_c__type_description__TypeSource__Sequence *
rebotarm_msgs__srv__GripperCommand_Request__get_type_description_sources(
  const rosidl_message_type_support_t * type_support)
{
  (void)type_support;
  static rosidl_runtime_c__type_description__TypeSource sources[1];
  static const rosidl_runtime_c__type_description__TypeSource__Sequence source_sequence = {sources, 1, 1};
  static bool constructed = false;
  if (!constructed) {
    sources[0] = *rebotarm_msgs__srv__GripperCommand_Request__get_individual_type_description_source(NULL),
    constructed = true;
  }
  return &source_sequence;
}

const rosidl_runtime_c__type_description__TypeSource__Sequence *
rebotarm_msgs__srv__GripperCommand_Response__get_type_description_sources(
  const rosidl_message_type_support_t * type_support)
{
  (void)type_support;
  static rosidl_runtime_c__type_description__TypeSource sources[1];
  static const rosidl_runtime_c__type_description__TypeSource__Sequence source_sequence = {sources, 1, 1};
  static bool constructed = false;
  if (!constructed) {
    sources[0] = *rebotarm_msgs__srv__GripperCommand_Response__get_individual_type_description_source(NULL),
    constructed = true;
  }
  return &source_sequence;
}

const rosidl_runtime_c__type_description__TypeSource__Sequence *
rebotarm_msgs__srv__GripperCommand_Event__get_type_description_sources(
  const rosidl_message_type_support_t * type_support)
{
  (void)type_support;
  static rosidl_runtime_c__type_description__TypeSource sources[5];
  static const rosidl_runtime_c__type_description__TypeSource__Sequence source_sequence = {sources, 5, 5};
  static bool constructed = false;
  if (!constructed) {
    sources[0] = *rebotarm_msgs__srv__GripperCommand_Event__get_individual_type_description_source(NULL),
    sources[1] = *builtin_interfaces__msg__Time__get_individual_type_description_source(NULL);
    sources[2] = *rebotarm_msgs__srv__GripperCommand_Request__get_individual_type_description_source(NULL);
    sources[3] = *rebotarm_msgs__srv__GripperCommand_Response__get_individual_type_description_source(NULL);
    sources[4] = *service_msgs__msg__ServiceEventInfo__get_individual_type_description_source(NULL);
    constructed = true;
  }
  return &source_sequence;
}
