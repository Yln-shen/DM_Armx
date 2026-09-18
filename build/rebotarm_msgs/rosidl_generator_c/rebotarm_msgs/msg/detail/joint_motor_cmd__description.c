// generated from rosidl_generator_c/resource/idl__description.c.em
// with input from rebotarm_msgs:msg/JointMotorCmd.idl
// generated code does not contain a copyright notice

#include "rebotarm_msgs/msg/detail/joint_motor_cmd__functions.h"

ROSIDL_GENERATOR_C_PUBLIC_rebotarm_msgs
const rosidl_type_hash_t *
rebotarm_msgs__msg__JointMotorCmd__get_type_hash(
  const rosidl_message_type_support_t * type_support)
{
  (void)type_support;
  static rosidl_type_hash_t hash = {1, {
      0x69, 0x06, 0x34, 0x01, 0x7e, 0x4c, 0x61, 0x61,
      0x2a, 0x49, 0x06, 0xbc, 0x29, 0x0e, 0xeb, 0x06,
      0xb1, 0x2b, 0xc7, 0xdf, 0xcb, 0xb0, 0x82, 0xfe,
      0x17, 0x5c, 0x40, 0xc1, 0x58, 0xeb, 0xbf, 0xcb,
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

static char rebotarm_msgs__msg__JointMotorCmd__TYPE_NAME[] = "rebotarm_msgs/msg/JointMotorCmd";
static char builtin_interfaces__msg__Time__TYPE_NAME[] = "builtin_interfaces/msg/Time";

// Define type names, field names, and default values
static char rebotarm_msgs__msg__JointMotorCmd__FIELD_NAME__mode[] = "mode";
static char rebotarm_msgs__msg__JointMotorCmd__FIELD_NAME__use_pos[] = "use_pos";
static char rebotarm_msgs__msg__JointMotorCmd__FIELD_NAME__use_vel[] = "use_vel";
static char rebotarm_msgs__msg__JointMotorCmd__FIELD_NAME__use_kp[] = "use_kp";
static char rebotarm_msgs__msg__JointMotorCmd__FIELD_NAME__use_kd[] = "use_kd";
static char rebotarm_msgs__msg__JointMotorCmd__FIELD_NAME__use_tau[] = "use_tau";
static char rebotarm_msgs__msg__JointMotorCmd__FIELD_NAME__use_vlim[] = "use_vlim";
static char rebotarm_msgs__msg__JointMotorCmd__FIELD_NAME__pos[] = "pos";
static char rebotarm_msgs__msg__JointMotorCmd__FIELD_NAME__vel[] = "vel";
static char rebotarm_msgs__msg__JointMotorCmd__FIELD_NAME__kp[] = "kp";
static char rebotarm_msgs__msg__JointMotorCmd__FIELD_NAME__kd[] = "kd";
static char rebotarm_msgs__msg__JointMotorCmd__FIELD_NAME__tau[] = "tau";
static char rebotarm_msgs__msg__JointMotorCmd__FIELD_NAME__vlim[] = "vlim";
static char rebotarm_msgs__msg__JointMotorCmd__FIELD_NAME__stamp[] = "stamp";

static rosidl_runtime_c__type_description__Field rebotarm_msgs__msg__JointMotorCmd__FIELDS[] = {
  {
    {rebotarm_msgs__msg__JointMotorCmd__FIELD_NAME__mode, 4, 4},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_UINT8,
      0,
      0,
      {NULL, 0, 0},
    },
    {NULL, 0, 0},
  },
  {
    {rebotarm_msgs__msg__JointMotorCmd__FIELD_NAME__use_pos, 7, 7},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_BOOLEAN,
      0,
      0,
      {NULL, 0, 0},
    },
    {NULL, 0, 0},
  },
  {
    {rebotarm_msgs__msg__JointMotorCmd__FIELD_NAME__use_vel, 7, 7},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_BOOLEAN,
      0,
      0,
      {NULL, 0, 0},
    },
    {NULL, 0, 0},
  },
  {
    {rebotarm_msgs__msg__JointMotorCmd__FIELD_NAME__use_kp, 6, 6},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_BOOLEAN,
      0,
      0,
      {NULL, 0, 0},
    },
    {NULL, 0, 0},
  },
  {
    {rebotarm_msgs__msg__JointMotorCmd__FIELD_NAME__use_kd, 6, 6},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_BOOLEAN,
      0,
      0,
      {NULL, 0, 0},
    },
    {NULL, 0, 0},
  },
  {
    {rebotarm_msgs__msg__JointMotorCmd__FIELD_NAME__use_tau, 7, 7},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_BOOLEAN,
      0,
      0,
      {NULL, 0, 0},
    },
    {NULL, 0, 0},
  },
  {
    {rebotarm_msgs__msg__JointMotorCmd__FIELD_NAME__use_vlim, 8, 8},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_BOOLEAN,
      0,
      0,
      {NULL, 0, 0},
    },
    {NULL, 0, 0},
  },
  {
    {rebotarm_msgs__msg__JointMotorCmd__FIELD_NAME__pos, 3, 3},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_DOUBLE,
      0,
      0,
      {NULL, 0, 0},
    },
    {NULL, 0, 0},
  },
  {
    {rebotarm_msgs__msg__JointMotorCmd__FIELD_NAME__vel, 3, 3},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_DOUBLE,
      0,
      0,
      {NULL, 0, 0},
    },
    {NULL, 0, 0},
  },
  {
    {rebotarm_msgs__msg__JointMotorCmd__FIELD_NAME__kp, 2, 2},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_DOUBLE,
      0,
      0,
      {NULL, 0, 0},
    },
    {NULL, 0, 0},
  },
  {
    {rebotarm_msgs__msg__JointMotorCmd__FIELD_NAME__kd, 2, 2},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_DOUBLE,
      0,
      0,
      {NULL, 0, 0},
    },
    {NULL, 0, 0},
  },
  {
    {rebotarm_msgs__msg__JointMotorCmd__FIELD_NAME__tau, 3, 3},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_DOUBLE,
      0,
      0,
      {NULL, 0, 0},
    },
    {NULL, 0, 0},
  },
  {
    {rebotarm_msgs__msg__JointMotorCmd__FIELD_NAME__vlim, 4, 4},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_DOUBLE,
      0,
      0,
      {NULL, 0, 0},
    },
    {NULL, 0, 0},
  },
  {
    {rebotarm_msgs__msg__JointMotorCmd__FIELD_NAME__stamp, 5, 5},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_NESTED_TYPE,
      0,
      0,
      {builtin_interfaces__msg__Time__TYPE_NAME, 27, 27},
    },
    {NULL, 0, 0},
  },
};

static rosidl_runtime_c__type_description__IndividualTypeDescription rebotarm_msgs__msg__JointMotorCmd__REFERENCED_TYPE_DESCRIPTIONS[] = {
  {
    {builtin_interfaces__msg__Time__TYPE_NAME, 27, 27},
    {NULL, 0, 0},
  },
};

const rosidl_runtime_c__type_description__TypeDescription *
rebotarm_msgs__msg__JointMotorCmd__get_type_description(
  const rosidl_message_type_support_t * type_support)
{
  (void)type_support;
  static bool constructed = false;
  static const rosidl_runtime_c__type_description__TypeDescription description = {
    {
      {rebotarm_msgs__msg__JointMotorCmd__TYPE_NAME, 31, 31},
      {rebotarm_msgs__msg__JointMotorCmd__FIELDS, 14, 14},
    },
    {rebotarm_msgs__msg__JointMotorCmd__REFERENCED_TYPE_DESCRIPTIONS, 1, 1},
  };
  if (!constructed) {
    assert(0 == memcmp(&builtin_interfaces__msg__Time__EXPECTED_HASH, builtin_interfaces__msg__Time__get_type_hash(NULL), sizeof(rosidl_type_hash_t)));
    description.referenced_type_descriptions.data[0].fields = builtin_interfaces__msg__Time__get_type_description(NULL)->type_description.fields;
    constructed = true;
  }
  return &description;
}

static char toplevel_type_raw_source[] =
  "uint8 MODE_MIT     = 0\n"
  "uint8 MODE_POS_VEL = 1\n"
  "uint8 MODE_VEL     = 2\n"
  "uint8 mode\n"
  "\n"
  "bool use_pos\n"
  "bool use_vel\n"
  "bool use_kp\n"
  "bool use_kd\n"
  "bool use_tau\n"
  "bool use_vlim\n"
  "\n"
  "float64 pos\n"
  "float64 vel\n"
  "float64 kp\n"
  "float64 kd\n"
  "float64 tau\n"
  "float64 vlim\n"
  "\n"
  "builtin_interfaces/Time stamp";

static char msg_encoding[] = "msg";

// Define all individual source functions

const rosidl_runtime_c__type_description__TypeSource *
rebotarm_msgs__msg__JointMotorCmd__get_individual_type_description_source(
  const rosidl_message_type_support_t * type_support)
{
  (void)type_support;
  static const rosidl_runtime_c__type_description__TypeSource source = {
    {rebotarm_msgs__msg__JointMotorCmd__TYPE_NAME, 31, 31},
    {msg_encoding, 3, 3},
    {toplevel_type_raw_source, 261, 261},
  };
  return &source;
}

const rosidl_runtime_c__type_description__TypeSource__Sequence *
rebotarm_msgs__msg__JointMotorCmd__get_type_description_sources(
  const rosidl_message_type_support_t * type_support)
{
  (void)type_support;
  static rosidl_runtime_c__type_description__TypeSource sources[2];
  static const rosidl_runtime_c__type_description__TypeSource__Sequence source_sequence = {sources, 2, 2};
  static bool constructed = false;
  if (!constructed) {
    sources[0] = *rebotarm_msgs__msg__JointMotorCmd__get_individual_type_description_source(NULL),
    sources[1] = *builtin_interfaces__msg__Time__get_individual_type_description_source(NULL);
    constructed = true;
  }
  return &source_sequence;
}
