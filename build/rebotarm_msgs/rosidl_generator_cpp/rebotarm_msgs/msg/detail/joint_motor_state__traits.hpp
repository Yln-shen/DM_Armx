// generated from rosidl_generator_cpp/resource/idl__traits.hpp.em
// with input from rebotarm_msgs:msg/JointMotorState.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "rebotarm_msgs/msg/joint_motor_state.hpp"


#ifndef REBOTARM_MSGS__MSG__DETAIL__JOINT_MOTOR_STATE__TRAITS_HPP_
#define REBOTARM_MSGS__MSG__DETAIL__JOINT_MOTOR_STATE__TRAITS_HPP_

#include <stdint.h>

#include <sstream>
#include <string>
#include <type_traits>

#include "rebotarm_msgs/msg/detail/joint_motor_state__struct.hpp"
#include "rosidl_runtime_cpp/traits.hpp"

// Include directives for member types
// Member 'header'
#include "std_msgs/msg/detail/header__traits.hpp"

namespace rebotarm_msgs
{

namespace msg
{

inline void to_flow_style_yaml(
  const JointMotorState & msg,
  std::ostream & out)
{
  out << "{";
  // member: header
  {
    out << "header: ";
    to_flow_style_yaml(msg.header, out);
    out << ", ";
  }

  // member: joint_name
  {
    out << "joint_name: ";
    rosidl_generator_traits::value_to_yaml(msg.joint_name, out);
    out << ", ";
  }

  // member: position
  {
    out << "position: ";
    rosidl_generator_traits::value_to_yaml(msg.position, out);
    out << ", ";
  }

  // member: velocity
  {
    out << "velocity: ";
    rosidl_generator_traits::value_to_yaml(msg.velocity, out);
    out << ", ";
  }

  // member: torque
  {
    out << "torque: ";
    rosidl_generator_traits::value_to_yaml(msg.torque, out);
    out << ", ";
  }

  // member: status_code
  {
    out << "status_code: ";
    rosidl_generator_traits::value_to_yaml(msg.status_code, out);
  }
  out << "}";
}  // NOLINT(readability/fn_size)

inline void to_block_style_yaml(
  const JointMotorState & msg,
  std::ostream & out, size_t indentation = 0)
{
  // member: header
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "header:\n";
    to_block_style_yaml(msg.header, out, indentation + 2);
  }

  // member: joint_name
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "joint_name: ";
    rosidl_generator_traits::value_to_yaml(msg.joint_name, out);
    out << "\n";
  }

  // member: position
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "position: ";
    rosidl_generator_traits::value_to_yaml(msg.position, out);
    out << "\n";
  }

  // member: velocity
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "velocity: ";
    rosidl_generator_traits::value_to_yaml(msg.velocity, out);
    out << "\n";
  }

  // member: torque
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "torque: ";
    rosidl_generator_traits::value_to_yaml(msg.torque, out);
    out << "\n";
  }

  // member: status_code
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "status_code: ";
    rosidl_generator_traits::value_to_yaml(msg.status_code, out);
    out << "\n";
  }
}  // NOLINT(readability/fn_size)

inline std::string to_yaml(const JointMotorState & msg, bool use_flow_style = false)
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
  const rebotarm_msgs::msg::JointMotorState & msg,
  std::ostream & out, size_t indentation = 0)
{
  rebotarm_msgs::msg::to_block_style_yaml(msg, out, indentation);
}

[[deprecated("use rebotarm_msgs::msg::to_yaml() instead")]]
inline std::string to_yaml(const rebotarm_msgs::msg::JointMotorState & msg)
{
  return rebotarm_msgs::msg::to_yaml(msg);
}

template<>
inline const char * data_type<rebotarm_msgs::msg::JointMotorState>()
{
  return "rebotarm_msgs::msg::JointMotorState";
}

template<>
inline const char * name<rebotarm_msgs::msg::JointMotorState>()
{
  return "rebotarm_msgs/msg/JointMotorState";
}

template<>
struct has_fixed_size<rebotarm_msgs::msg::JointMotorState>
  : std::integral_constant<bool, false> {};

template<>
struct has_bounded_size<rebotarm_msgs::msg::JointMotorState>
  : std::integral_constant<bool, false> {};

template<>
struct is_message<rebotarm_msgs::msg::JointMotorState>
  : std::true_type {};

}  // namespace rosidl_generator_traits

#endif  // REBOTARM_MSGS__MSG__DETAIL__JOINT_MOTOR_STATE__TRAITS_HPP_
