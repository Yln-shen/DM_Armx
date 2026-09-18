// generated from rosidl_generator_cpp/resource/idl__builder.hpp.em
// with input from rebotarm_msgs:msg/JointMotorState.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "rebotarm_msgs/msg/joint_motor_state.hpp"


#ifndef REBOTARM_MSGS__MSG__DETAIL__JOINT_MOTOR_STATE__BUILDER_HPP_
#define REBOTARM_MSGS__MSG__DETAIL__JOINT_MOTOR_STATE__BUILDER_HPP_

#include <algorithm>
#include <utility>

#include "rebotarm_msgs/msg/detail/joint_motor_state__struct.hpp"
#include "rosidl_runtime_cpp/message_initialization.hpp"


namespace rebotarm_msgs
{

namespace msg
{

namespace builder
{

class Init_JointMotorState_status_code
{
public:
  explicit Init_JointMotorState_status_code(::rebotarm_msgs::msg::JointMotorState & msg)
  : msg_(msg)
  {}
  ::rebotarm_msgs::msg::JointMotorState status_code(::rebotarm_msgs::msg::JointMotorState::_status_code_type arg)
  {
    msg_.status_code = std::move(arg);
    return std::move(msg_);
  }

private:
  ::rebotarm_msgs::msg::JointMotorState msg_;
};

class Init_JointMotorState_torque
{
public:
  explicit Init_JointMotorState_torque(::rebotarm_msgs::msg::JointMotorState & msg)
  : msg_(msg)
  {}
  Init_JointMotorState_status_code torque(::rebotarm_msgs::msg::JointMotorState::_torque_type arg)
  {
    msg_.torque = std::move(arg);
    return Init_JointMotorState_status_code(msg_);
  }

private:
  ::rebotarm_msgs::msg::JointMotorState msg_;
};

class Init_JointMotorState_velocity
{
public:
  explicit Init_JointMotorState_velocity(::rebotarm_msgs::msg::JointMotorState & msg)
  : msg_(msg)
  {}
  Init_JointMotorState_torque velocity(::rebotarm_msgs::msg::JointMotorState::_velocity_type arg)
  {
    msg_.velocity = std::move(arg);
    return Init_JointMotorState_torque(msg_);
  }

private:
  ::rebotarm_msgs::msg::JointMotorState msg_;
};

class Init_JointMotorState_position
{
public:
  explicit Init_JointMotorState_position(::rebotarm_msgs::msg::JointMotorState & msg)
  : msg_(msg)
  {}
  Init_JointMotorState_velocity position(::rebotarm_msgs::msg::JointMotorState::_position_type arg)
  {
    msg_.position = std::move(arg);
    return Init_JointMotorState_velocity(msg_);
  }

private:
  ::rebotarm_msgs::msg::JointMotorState msg_;
};

class Init_JointMotorState_joint_name
{
public:
  explicit Init_JointMotorState_joint_name(::rebotarm_msgs::msg::JointMotorState & msg)
  : msg_(msg)
  {}
  Init_JointMotorState_position joint_name(::rebotarm_msgs::msg::JointMotorState::_joint_name_type arg)
  {
    msg_.joint_name = std::move(arg);
    return Init_JointMotorState_position(msg_);
  }

private:
  ::rebotarm_msgs::msg::JointMotorState msg_;
};

class Init_JointMotorState_header
{
public:
  Init_JointMotorState_header()
  : msg_(::rosidl_runtime_cpp::MessageInitialization::SKIP)
  {}
  Init_JointMotorState_joint_name header(::rebotarm_msgs::msg::JointMotorState::_header_type arg)
  {
    msg_.header = std::move(arg);
    return Init_JointMotorState_joint_name(msg_);
  }

private:
  ::rebotarm_msgs::msg::JointMotorState msg_;
};

}  // namespace builder

}  // namespace msg

template<typename MessageType>
auto build();

template<>
inline
auto build<::rebotarm_msgs::msg::JointMotorState>()
{
  return rebotarm_msgs::msg::builder::Init_JointMotorState_header();
}

}  // namespace rebotarm_msgs

#endif  // REBOTARM_MSGS__MSG__DETAIL__JOINT_MOTOR_STATE__BUILDER_HPP_
