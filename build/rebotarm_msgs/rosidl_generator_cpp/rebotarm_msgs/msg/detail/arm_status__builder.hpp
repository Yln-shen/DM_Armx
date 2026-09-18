// generated from rosidl_generator_cpp/resource/idl__builder.hpp.em
// with input from rebotarm_msgs:msg/ArmStatus.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "rebotarm_msgs/msg/arm_status.hpp"


#ifndef REBOTARM_MSGS__MSG__DETAIL__ARM_STATUS__BUILDER_HPP_
#define REBOTARM_MSGS__MSG__DETAIL__ARM_STATUS__BUILDER_HPP_

#include <algorithm>
#include <utility>

#include "rebotarm_msgs/msg/detail/arm_status__struct.hpp"
#include "rosidl_runtime_cpp/message_initialization.hpp"


namespace rebotarm_msgs
{

namespace msg
{

namespace builder
{

class Init_ArmStatus_error_codes
{
public:
  explicit Init_ArmStatus_error_codes(::rebotarm_msgs::msg::ArmStatus & msg)
  : msg_(msg)
  {}
  ::rebotarm_msgs::msg::ArmStatus error_codes(::rebotarm_msgs::msg::ArmStatus::_error_codes_type arg)
  {
    msg_.error_codes = std::move(arg);
    return std::move(msg_);
  }

private:
  ::rebotarm_msgs::msg::ArmStatus msg_;
};

class Init_ArmStatus_per_joint_status_code
{
public:
  explicit Init_ArmStatus_per_joint_status_code(::rebotarm_msgs::msg::ArmStatus & msg)
  : msg_(msg)
  {}
  Init_ArmStatus_error_codes per_joint_status_code(::rebotarm_msgs::msg::ArmStatus::_per_joint_status_code_type arg)
  {
    msg_.per_joint_status_code = std::move(arg);
    return Init_ArmStatus_error_codes(msg_);
  }

private:
  ::rebotarm_msgs::msg::ArmStatus msg_;
};

class Init_ArmStatus_joint_names
{
public:
  explicit Init_ArmStatus_joint_names(::rebotarm_msgs::msg::ArmStatus & msg)
  : msg_(msg)
  {}
  Init_ArmStatus_per_joint_status_code joint_names(::rebotarm_msgs::msg::ArmStatus::_joint_names_type arg)
  {
    msg_.joint_names = std::move(arg);
    return Init_ArmStatus_per_joint_status_code(msg_);
  }

private:
  ::rebotarm_msgs::msg::ArmStatus msg_;
};

class Init_ArmStatus_state_machine
{
public:
  explicit Init_ArmStatus_state_machine(::rebotarm_msgs::msg::ArmStatus & msg)
  : msg_(msg)
  {}
  Init_ArmStatus_joint_names state_machine(::rebotarm_msgs::msg::ArmStatus::_state_machine_type arg)
  {
    msg_.state_machine = std::move(arg);
    return Init_ArmStatus_joint_names(msg_);
  }

private:
  ::rebotarm_msgs::msg::ArmStatus msg_;
};

class Init_ArmStatus_control_loop_active
{
public:
  explicit Init_ArmStatus_control_loop_active(::rebotarm_msgs::msg::ArmStatus & msg)
  : msg_(msg)
  {}
  Init_ArmStatus_state_machine control_loop_active(::rebotarm_msgs::msg::ArmStatus::_control_loop_active_type arg)
  {
    msg_.control_loop_active = std::move(arg);
    return Init_ArmStatus_state_machine(msg_);
  }

private:
  ::rebotarm_msgs::msg::ArmStatus msg_;
};

class Init_ArmStatus_enabled
{
public:
  explicit Init_ArmStatus_enabled(::rebotarm_msgs::msg::ArmStatus & msg)
  : msg_(msg)
  {}
  Init_ArmStatus_control_loop_active enabled(::rebotarm_msgs::msg::ArmStatus::_enabled_type arg)
  {
    msg_.enabled = std::move(arg);
    return Init_ArmStatus_control_loop_active(msg_);
  }

private:
  ::rebotarm_msgs::msg::ArmStatus msg_;
};

class Init_ArmStatus_mode
{
public:
  explicit Init_ArmStatus_mode(::rebotarm_msgs::msg::ArmStatus & msg)
  : msg_(msg)
  {}
  Init_ArmStatus_enabled mode(::rebotarm_msgs::msg::ArmStatus::_mode_type arg)
  {
    msg_.mode = std::move(arg);
    return Init_ArmStatus_enabled(msg_);
  }

private:
  ::rebotarm_msgs::msg::ArmStatus msg_;
};

class Init_ArmStatus_header
{
public:
  Init_ArmStatus_header()
  : msg_(::rosidl_runtime_cpp::MessageInitialization::SKIP)
  {}
  Init_ArmStatus_mode header(::rebotarm_msgs::msg::ArmStatus::_header_type arg)
  {
    msg_.header = std::move(arg);
    return Init_ArmStatus_mode(msg_);
  }

private:
  ::rebotarm_msgs::msg::ArmStatus msg_;
};

}  // namespace builder

}  // namespace msg

template<typename MessageType>
auto build();

template<>
inline
auto build<::rebotarm_msgs::msg::ArmStatus>()
{
  return rebotarm_msgs::msg::builder::Init_ArmStatus_header();
}

}  // namespace rebotarm_msgs

#endif  // REBOTARM_MSGS__MSG__DETAIL__ARM_STATUS__BUILDER_HPP_
