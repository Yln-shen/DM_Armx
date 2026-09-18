// generated from rosidl_generator_cpp/resource/idl__traits.hpp.em
// with input from rebotarm_msgs:msg/ArmStatus.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "rebotarm_msgs/msg/arm_status.hpp"


#ifndef REBOTARM_MSGS__MSG__DETAIL__ARM_STATUS__TRAITS_HPP_
#define REBOTARM_MSGS__MSG__DETAIL__ARM_STATUS__TRAITS_HPP_

#include <stdint.h>

#include <sstream>
#include <string>
#include <type_traits>

#include "rebotarm_msgs/msg/detail/arm_status__struct.hpp"
#include "rosidl_runtime_cpp/traits.hpp"

// Include directives for member types
// Member 'header'
#include "std_msgs/msg/detail/header__traits.hpp"

namespace rebotarm_msgs
{

namespace msg
{

inline void to_flow_style_yaml(
  const ArmStatus & msg,
  std::ostream & out)
{
  out << "{";
  // member: header
  {
    out << "header: ";
    to_flow_style_yaml(msg.header, out);
    out << ", ";
  }

  // member: mode
  {
    out << "mode: ";
    rosidl_generator_traits::value_to_yaml(msg.mode, out);
    out << ", ";
  }

  // member: enabled
  {
    out << "enabled: ";
    rosidl_generator_traits::value_to_yaml(msg.enabled, out);
    out << ", ";
  }

  // member: control_loop_active
  {
    out << "control_loop_active: ";
    rosidl_generator_traits::value_to_yaml(msg.control_loop_active, out);
    out << ", ";
  }

  // member: state_machine
  {
    out << "state_machine: ";
    rosidl_generator_traits::value_to_yaml(msg.state_machine, out);
    out << ", ";
  }

  // member: joint_names
  {
    if (msg.joint_names.size() == 0) {
      out << "joint_names: []";
    } else {
      out << "joint_names: [";
      size_t pending_items = msg.joint_names.size();
      for (auto item : msg.joint_names) {
        rosidl_generator_traits::value_to_yaml(item, out);
        if (--pending_items > 0) {
          out << ", ";
        }
      }
      out << "]";
    }
    out << ", ";
  }

  // member: per_joint_status_code
  {
    if (msg.per_joint_status_code.size() == 0) {
      out << "per_joint_status_code: []";
    } else {
      out << "per_joint_status_code: [";
      size_t pending_items = msg.per_joint_status_code.size();
      for (auto item : msg.per_joint_status_code) {
        rosidl_generator_traits::value_to_yaml(item, out);
        if (--pending_items > 0) {
          out << ", ";
        }
      }
      out << "]";
    }
    out << ", ";
  }

  // member: error_codes
  {
    if (msg.error_codes.size() == 0) {
      out << "error_codes: []";
    } else {
      out << "error_codes: [";
      size_t pending_items = msg.error_codes.size();
      for (auto item : msg.error_codes) {
        rosidl_generator_traits::value_to_yaml(item, out);
        if (--pending_items > 0) {
          out << ", ";
        }
      }
      out << "]";
    }
  }
  out << "}";
}  // NOLINT(readability/fn_size)

inline void to_block_style_yaml(
  const ArmStatus & msg,
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

  // member: mode
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "mode: ";
    rosidl_generator_traits::value_to_yaml(msg.mode, out);
    out << "\n";
  }

  // member: enabled
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "enabled: ";
    rosidl_generator_traits::value_to_yaml(msg.enabled, out);
    out << "\n";
  }

  // member: control_loop_active
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "control_loop_active: ";
    rosidl_generator_traits::value_to_yaml(msg.control_loop_active, out);
    out << "\n";
  }

  // member: state_machine
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "state_machine: ";
    rosidl_generator_traits::value_to_yaml(msg.state_machine, out);
    out << "\n";
  }

  // member: joint_names
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    if (msg.joint_names.size() == 0) {
      out << "joint_names: []\n";
    } else {
      out << "joint_names:\n";
      for (auto item : msg.joint_names) {
        if (indentation > 0) {
          out << std::string(indentation, ' ');
        }
        out << "- ";
        rosidl_generator_traits::value_to_yaml(item, out);
        out << "\n";
      }
    }
  }

  // member: per_joint_status_code
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    if (msg.per_joint_status_code.size() == 0) {
      out << "per_joint_status_code: []\n";
    } else {
      out << "per_joint_status_code:\n";
      for (auto item : msg.per_joint_status_code) {
        if (indentation > 0) {
          out << std::string(indentation, ' ');
        }
        out << "- ";
        rosidl_generator_traits::value_to_yaml(item, out);
        out << "\n";
      }
    }
  }

  // member: error_codes
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    if (msg.error_codes.size() == 0) {
      out << "error_codes: []\n";
    } else {
      out << "error_codes:\n";
      for (auto item : msg.error_codes) {
        if (indentation > 0) {
          out << std::string(indentation, ' ');
        }
        out << "- ";
        rosidl_generator_traits::value_to_yaml(item, out);
        out << "\n";
      }
    }
  }
}  // NOLINT(readability/fn_size)

inline std::string to_yaml(const ArmStatus & msg, bool use_flow_style = false)
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
  const rebotarm_msgs::msg::ArmStatus & msg,
  std::ostream & out, size_t indentation = 0)
{
  rebotarm_msgs::msg::to_block_style_yaml(msg, out, indentation);
}

[[deprecated("use rebotarm_msgs::msg::to_yaml() instead")]]
inline std::string to_yaml(const rebotarm_msgs::msg::ArmStatus & msg)
{
  return rebotarm_msgs::msg::to_yaml(msg);
}

template<>
inline const char * data_type<rebotarm_msgs::msg::ArmStatus>()
{
  return "rebotarm_msgs::msg::ArmStatus";
}

template<>
inline const char * name<rebotarm_msgs::msg::ArmStatus>()
{
  return "rebotarm_msgs/msg/ArmStatus";
}

template<>
struct has_fixed_size<rebotarm_msgs::msg::ArmStatus>
  : std::integral_constant<bool, false> {};

template<>
struct has_bounded_size<rebotarm_msgs::msg::ArmStatus>
  : std::integral_constant<bool, false> {};

template<>
struct is_message<rebotarm_msgs::msg::ArmStatus>
  : std::true_type {};

}  // namespace rosidl_generator_traits

#endif  // REBOTARM_MSGS__MSG__DETAIL__ARM_STATUS__TRAITS_HPP_
