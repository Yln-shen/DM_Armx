// generated from rosidl_generator_c/resource/idl__description.c.em
// with input from rebotarm_msgs:msg/JointPosVelCmd.idl
// generated code does not contain a copyright notice

#include "rebotarm_msgs/msg/detail/joint_pos_vel_cmd__functions.h"

ROSIDL_GENERATOR_C_PUBLIC_rebotarm_msgs
const rosidl_type_hash_t *
rebotarm_msgs__msg__JointPosVelCmd__get_type_hash(
  const rosidl_message_type_support_t * type_support)
{
  (void)type_support;
  static rosidl_type_hash_t hash = {1, {
      0x7b, 0x63, 0xdc, 0xba, 0xcf, 0xee, 0x01, 0x62,
      0x74, 0x84, 0x96, 0x1d, 0x09, 0x75, 0x04, 0x4e,
      0xd6, 0x99, 0x0b, 0xda, 0xbf, 0xfb, 0x87, 0x80,
      0x58, 0x3f, 0x36, 0xd3, 0x7c, 0xfe, 0xa6, 0x9f,
    }};
  return &hash;
}

#include <assert.h>
#include <string.h>

// Include directives for referenced types
#include "builtin_interfaces/msg/detail/time__functions.h"

// Hashes for external referenced types
#ifndef NDEBUG
static const rosidl_type_hash_t builtin_interfaces__msg__Time__EXPECTED_HASH = {1, {
    0xb1, 0x06, 0x23, 0x5e, 0x25, 0xa4, 0xc5, 0xed,
    0x35, 0x09, 0x8a, 0xa0, 0xa6, 0x1a, 0x3e, 0xe9,
    0xc9, 0xb1, 0x8d, 0x19, 0x7f, 0x39, 0x8b, 0x0e,
    0x42, 0x06, 0xce, 0xa9, 0xac, 0xf9, 0xc1, 0x97,
  }};
#endif

static char rebotarm_msgs__msg__JointPosVelCmd__TYPE_NAME[] = "rebotarm_msgs/msg/JointPosVelCmd";
static char builtin_interfaces__msg__Time__TYPE_NAME[] = "builtin_interfaces/msg/Time";

// Define type names, field names, and default values
static char rebotarm_msgs__msg__JointPosVelCmd__FIELD_NAME__pos[] = "pos";
static char rebotarm_msgs__msg__JointPosVelCmd__FIELD_NAME__vlim[] = "vlim";
static char rebotarm_msgs__msg__JointPosVelCmd__FIELD_NAME__stamp[] = "stamp";

static rosidl_runtime_c__type_description__Field rebotarm_msgs__msg__JointPosVelCmd__FIELDS[] = {
  {
    {rebotarm_msgs__msg__JointPosVelCmd__FIELD_NAME__pos, 3, 3},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_DOUBLE,
      0,
      0,
      {NULL, 0, 0},
    },
    {NULL, 0, 0},
  },
  {
    {rebotarm_msgs__msg__JointPosVelCmd__FIELD_NAME__vlim, 4, 4},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_DOUBLE,
      0,
      0,
      {NULL, 0, 0},
    },
    {NULL, 0, 0},
  },
  {
    {rebotarm_msgs__msg__JointPosVelCmd__FIELD_NAME__stamp, 5, 5},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_NESTED_TYPE,
      0,
      0,
      {builtin_interfaces__msg__Time__TYPE_NAME, 27, 27},
    },
    {NULL, 0, 0},
  },
};

static rosidl_runtime_c__type_description__IndividualTypeDescription rebotarm_msgs__msg__JointPosVelCmd__REFERENCED_TYPE_DESCRIPTIONS[] = {
  {
    {builtin_interfaces__msg__Time__TYPE_NAME, 27, 27},
    {NULL, 0, 0},
  },
};

const rosidl_runtime_c__type_description__TypeDescription *
rebotarm_msgs__msg__JointPosVelCmd__get_type_description(
  const rosidl_message_type_support_t * type_support)
{
  (void)type_support;
  static bool constructed = false;
  static const rosidl_runtime_c__type_description__TypeDescription description = {
    {
      {rebotarm_msgs__msg__JointPosVelCmd__TYPE_NAME, 32, 32},
      {rebotarm_msgs__msg__JointPosVelCmd__FIELDS, 3, 3},
    },
    {rebotarm_msgs__msg__JointPosVelCmd__REFERENCED_TYPE_DESCRIPTIONS, 1, 1},
  };
  if (!constructed) {
    assert(0 == memcmp(&builtin_interfaces__msg__Time__EXPECTED_HASH, builtin_interfaces__msg__Time__get_type_hash(NULL), sizeof(rosidl_type_hash_t)));
    description.referenced_type_descriptions.data[0].fields = builtin_interfaces__msg__Time__get_type_description(NULL)->type_description.fields;
    constructed = true;
  }
  return &description;
}

static char toplevel_type_raw_source[] =
  "float64 pos\n"
  "float64 vlim\n"
  "builtin_interfaces/Time stamp";

static char msg_encoding[] = "msg";

// Define all individual source functions

const rosidl_runtime_c__type_description__TypeSource *
rebotarm_msgs__msg__JointPosVelCmd__get_individual_type_description_source(
  const rosidl_message_type_support_t * type_support)
{
  (void)type_support;
  static const rosidl_runtime_c__type_description__TypeSource source = {
    {rebotarm_msgs__msg__JointPosVelCmd__TYPE_NAME, 32, 32},
    {msg_encoding, 3, 3},
    {toplevel_type_raw_source, 55, 55},
  };
  return &source;
}

const rosidl_runtime_c__type_description__TypeSource__Sequence *
rebotarm_msgs__msg__JointPosVelCmd__get_type_description_sources(
  const rosidl_message_type_support_t * type_support)
{
  (void)type_support;
  static rosidl_runtime_c__type_description__TypeSource sources[2];
  static const rosidl_runtime_c__type_description__TypeSource__Sequence source_sequence = {sources, 2, 2};
  static bool constructed = false;
  if (!constructed) {
    sources[0] = *rebotarm_msgs__msg__JointPosVelCmd__get_individual_type_description_source(NULL),
    sources[1] = *builtin_interfaces__msg__Time__get_individual_type_description_source(NULL);
    constructed = true;
  }
  return &source_sequence;
}
