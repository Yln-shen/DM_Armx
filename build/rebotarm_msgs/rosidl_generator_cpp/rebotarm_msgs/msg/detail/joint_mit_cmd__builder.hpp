// generated from rosidl_generator_cpp/resource/idl__builder.hpp.em
// with input from rebotarm_msgs:msg/JointMitCmd.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "rebotarm_msgs/msg/joint_mit_cmd.hpp"


#ifndef REBOTARM_MSGS__MSG__DETAIL__JOINT_MIT_CMD__BUILDER_HPP_
#define REBOTARM_MSGS__MSG__DETAIL__JOINT_MIT_CMD__BUILDER_HPP_

#include <algorithm>
#include <utility>

#include "rebotarm_msgs/msg/detail/joint_mit_cmd__struct.hpp"
#include "rosidl_runtime_cpp/message_initialization.hpp"


namespace rebotarm_msgs
{

namespace msg
{

namespace builder
{

class Init_JointMitCmd_stamp
{
public:
  explicit Init_JointMitCmd_stamp(::rebotarm_msgs::msg::JointMitCmd & msg)
  : msg_(msg)
  {}
  ::rebotarm_msgs::msg::JointMitCmd stamp(::rebotarm_msgs::msg::JointMitCmd::_stamp_type arg)
  {
    msg_.stamp = std::move(arg);
    return std::move(msg_);
  }

private:
  ::rebotarm_msgs::msg::JointMitCmd msg_;
};

class Init_JointMitCmd_tau
{
public:
  explicit Init_JointMitCmd_tau(::rebotarm_msgs::msg::JointMitCmd & msg)
  : msg_(msg)
  {}
  Init_JointMitCmd_stamp tau(::rebotarm_msgs::msg::JointMitCmd::_tau_type arg)
  {
    msg_.tau = std::move(arg);
    return Init_JointMitCmd_stamp(msg_);
  }

private:
  ::rebotarm_msgs::msg::JointMitCmd msg_;
};

class Init_JointMitCmd_kd
{
public:
  explicit Init_JointMitCmd_kd(::rebotarm_msgs::msg::JointMitCmd & msg)
  : msg_(msg)
  {}
  Init_JointMitCmd_tau kd(::rebotarm_msgs::msg::JointMitCmd::_kd_type arg)
  {
    msg_.kd = std::move(arg);
    return Init_JointMitCmd_tau(msg_);
  }

private:
  ::rebotarm_msgs::msg::JointMitCmd msg_;
};

class Init_JointMitCmd_kp
{
public:
  explicit Init_JointMitCmd_kp(::rebotarm_msgs::msg::JointMitCmd & msg)
  : msg_(msg)
  {}
  Init_JointMitCmd_kd kp(::rebotarm_msgs::msg::JointMitCmd::_kp_type arg)
  {
    msg_.kp = std::move(arg);
    return Init_JointMitCmd_kd(msg_);
  }

private:
  ::rebotarm_msgs::msg::JointMitCmd msg_;
};

class Init_JointMitCmd_vel
{
public:
  explicit Init_JointMitCmd_vel(::rebotarm_msgs::msg::JointMitCmd & msg)
  : msg_(msg)
  {}
  Init_JointMitCmd_kp vel(::rebotarm_msgs::msg::JointMitCmd::_vel_type arg)
  {
    msg_.vel = std::move(arg);
    return Init_JointMitCmd_kp(msg_);
  }

private:
  ::rebotarm_msgs::msg::JointMitCmd msg_;
};

class Init_JointMitCmd_pos
{
public:
  Init_JointMitCmd_pos()
  : msg_(::rosidl_runtime_cpp::MessageInitialization::SKIP)
  {}
  Init_JointMitCmd_vel pos(::rebotarm_msgs::msg::JointMitCmd::_pos_type arg)
  {
    msg_.pos = std::move(arg);
    return Init_JointMitCmd_vel(msg_);
  }

private:
  ::rebotarm_msgs::msg::JointMitCmd msg_;
};

}  // namespace builder

}  // namespace msg

template<typename MessageType>
auto build();

template<>
inline
auto build<::rebotarm_msgs::msg::JointMitCmd>()
{
  return rebotarm_msgs::msg::builder::Init_JointMitCmd_pos();
}

}  // namespace rebotarm_msgs

#endif  // REBOTARM_MSGS__MSG__DETAIL__JOINT_MIT_CMD__BUILDER_HPP_
