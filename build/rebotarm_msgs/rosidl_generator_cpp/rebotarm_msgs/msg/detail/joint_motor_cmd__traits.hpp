// generated from rosidl_generator_cpp/resource/idl__traits.hpp.em
// with input from rebotarm_msgs:msg/JointMotorCmd.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "rebotarm_msgs/msg/joint_motor_cmd.hpp"


#ifndef REBOTARM_MSGS__MSG__DETAIL__JOINT_MOTOR_CMD__TRAITS_HPP_
#define REBOTARM_MSGS__MSG__DETAIL__JOINT_MOTOR_CMD__TRAITS_HPP_

#include <stdint.h>

#include <sstream>
#include <string>
#include <type_traits>

#include "rebotarm_msgs/msg/detail/joint_motor_cmd__struct.hpp"
#include "rosidl_runtime_cpp/traits.hpp"

// Include directives for member types
// Member 'stamp'
#include "builtin_interfaces/msg/detail/time__traits.hpp"

namespace rebotarm_msgs
{

namespace msg
{

inline void to_flow_style_yaml(
  const JointMotorCmd & msg,
  std::ostream & out)
{
  out << "{";
  // member: mode
  {
    out << "mode: ";
    rosidl_generator_traits::value_to_yaml(msg.mode, out);
    out << ", ";
  }

  // member: use_pos
  {
    out << "use_pos: ";
    rosidl_generator_traits::value_to_yaml(msg.use_pos, out);
    out << ", ";
  }

  // member: use_vel
  {
    out << "use_vel: ";
    rosidl_generator_traits::value_to_yaml(msg.use_vel, out);
    out << ", ";
  }

  // member: use_kp
  {
    out << "use_kp: ";
    rosidl_generator_traits::value_to_yaml(msg.use_kp, out);
    out << ", ";
  }

  // member: use_kd
  {
    out << "use_kd: ";
    rosidl_generator_traits::value_to_yaml(msg.use_kd, out);
    out << ", ";
  }

  // member: use_tau
  {
    out << "use_tau: ";
    rosidl_generator_traits::value_to_yaml(msg.use_tau, out);
    out << ", ";
  }

  // member: use_vlim
  {
    out << "use_vlim: ";
    rosidl_generator_traits::value_to_yaml(msg.use_vlim, out);
    out << ", ";
  }

  // member: pos
  {
    out << "pos: ";
    rosidl_generator_traits::value_to_yaml(msg.pos, out);
    out << ", ";
  }

  // member: vel
  {
    out << "vel: ";
    rosidl_generator_traits::value_to_yaml(msg.vel, out);
    out << ", ";
  }

  // member: kp
  {
    out << "kp: ";
    rosidl_generator_traits::value_to_yaml(msg.kp, out);
    out << ", ";
  }

  // member: kd
  {
    out << "kd: ";
    rosidl_generator_traits::value_to_yaml(msg.kd, out);
    out << ", ";
  }

  // member: tau
  {
    out << "tau: ";
    rosidl_generator_traits::value_to_yaml(msg.tau, out);
    out << ", ";
  }

  // member: vlim
  {
    out << "vlim: ";
    rosidl_generator_traits::value_to_yaml(msg.vlim, out);
    out << ", ";
  }

  // member: stamp
  {
    out << "stamp: ";
    to_flow_style_yaml(msg.stamp, out);
  }
  out << "}";
}  // NOLINT(readability/fn_size)

inline void to_block_style_yaml(
  const JointMotorCmd & msg,
  std::ostream & out, size_t indentation = 0)
{
  // member: mode
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "mode: ";
    rosidl_generator_traits::value_to_yaml(msg.mode, out);
    out << "\n";
  }

  // member: use_pos
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "use_pos: ";
    rosidl_generator_traits::value_to_yaml(msg.use_pos, out);
    out << "\n";
  }

  // member: use_vel
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "use_vel: ";
    rosidl_generator_traits::value_to_yaml(msg.use_vel, out);
    out << "\n";
  }

  // member: use_kp
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "use_kp: ";
    rosidl_generator_traits::value_to_yaml(msg.use_kp, out);
    out << "\n";
  }

  // member: use_kd
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "use_kd: ";
    rosidl_generator_traits::value_to_yaml(msg.use_kd, out);
    out << "\n";
  }

  // member: use_tau
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "use_tau: ";
    rosidl_generator_traits::value_to_yaml(msg.use_tau, out);
    out << "\n";
  }

  // member: use_vlim
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "use_vlim: ";
    rosidl_generator_traits::value_to_yaml(msg.use_vlim, out);
    out << "\n";
  }

  // member: pos
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "pos: ";
    rosidl_generator_traits::value_to_yaml(msg.pos, out);
    out << "\n";
  }

  // member: vel
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "vel: ";
    rosidl_generator_traits::value_to_yaml(msg.vel, out);
    out << "\n";
  }

  // member: kp
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "kp: ";
    rosidl_generator_traits::value_to_yaml(msg.kp, out);
    out << "\n";
  }

  // member: kd
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "kd: ";
    rosidl_generator_traits::value_to_yaml(msg.kd, out);
    out << "\n";
  }

  // member: tau
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "tau: ";
    rosidl_generator_traits::value_to_yaml(msg.tau, out);
    out << "\n";
  }

  // member: vlim
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "vlim: ";
    rosidl_generator_traits::value_to_yaml(msg.vlim, out);
    out << "\n";
  }

  // member: stamp
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "stamp:\n";
    to_block_style_yaml(msg.stamp, out, indentation + 2);
  }
}  // NOLINT(readability/fn_size)

inline std::string to_yaml(const JointMotorCmd & msg, bool use_flow_style = false)
{
  std::ostringstream out;
  if (use_flow_style) {
    to_flow_style_yaml(msg, out);
  } else {
    to_block_style_yaml(msg, out);
  }
  return out.str();
}

}  // namespace msg

}  // namespace rebotarm_msgs

namespace rosidl_generator_traits
{

[[deprecated("use rebotarm_msgs::msg::to_block_style_yaml() instead")]]
inline void to_yaml(
  const rebotarm_msgs::msg::JointMotorCmd & msg,
  std::ostream & out, size_t indentation = 0)
{
  rebotarm_msgs::msg::to_block_style_yaml(msg, out, indentation);
}

[[deprecated("use rebotarm_msgs::msg::to_yaml() instead")]]
inline std::string to_yaml(const rebotarm_msgs::msg::JointMotorCmd & msg)
{
  return rebotarm_msgs::msg::to_yaml(msg);
}

template<>
inline const char * data_type<rebotarm_msgs::msg::JointMotorCmd>()
{
  return "rebotarm_msgs::msg::JointMotorCmd";
}

template<>
inline const char * name<rebotarm_msgs::msg::JointMotorCmd>()
{
  return "rebotarm_msgs/msg/JointMotorCmd";
}

template<>
struct has_fixed_size<rebotarm_msgs::msg::JointMotorCmd>
  : std::integral_constant<bool, has_fixed_size<builtin_interfaces::msg::Time>::value> {};

template<>
struct has_bounded_size<rebotarm_msgs::msg::JointMotorCmd>
  : std::integral_constant<bool, has_bounded_size<builtin_interfaces::msg::Time>::value> {};

template<>
struct is_message<rebotarm_msgs::msg::JointMotorCmd>
  : std::true_type {};

}  // namespace rosidl_generator_traits

#endif  // REBOTARM_MSGS__MSG__DETAIL__JOINT_MOTOR_CMD__TRAITS_HPP_
