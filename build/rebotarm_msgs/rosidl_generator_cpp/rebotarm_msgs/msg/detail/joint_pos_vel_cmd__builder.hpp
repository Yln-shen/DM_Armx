// generated from rosidl_generator_cpp/resource/idl__builder.hpp.em
// with input from rebotarm_msgs:msg/JointPosVelCmd.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "rebotarm_msgs/msg/joint_pos_vel_cmd.hpp"


#ifndef REBOTARM_MSGS__MSG__DETAIL__JOINT_POS_VEL_CMD__BUILDER_HPP_
#define REBOTARM_MSGS__MSG__DETAIL__JOINT_POS_VEL_CMD__BUILDER_HPP_

#include <algorithm>
#include <utility>

#include "rebotarm_msgs/msg/detail/joint_pos_vel_cmd__struct.hpp"
#include "rosidl_runtime_cpp/message_initialization.hpp"


namespace rebotarm_msgs
{

namespace msg
{

namespace builder
{

class Init_JointPosVelCmd_stamp
{
public:
  explicit Init_JointPosVelCmd_stamp(::rebotarm_msgs::msg::JointPosVelCmd & msg)
  : msg_(msg)
  {}
  ::rebotarm_msgs::msg::JointPosVelCmd stamp(::rebotarm_msgs::msg::JointPosVelCmd::_stamp_type arg)
  {
    msg_.stamp = std::move(arg);
    return std::move(msg_);
  }

private:
  ::rebotarm_msgs::msg::JointPosVelCmd msg_;
};

class Init_JointPosVelCmd_vlim
{
public:
  explicit Init_JointPosVelCmd_vlim(::rebotarm_msgs::msg::JointPosVelCmd & msg)
  : msg_(msg)
  {}
  Init_JointPosVelCmd_stamp vlim(::rebotarm_msgs::msg::JointPosVelCmd::_vlim_type arg)
  {
    msg_.vlim = std::move(arg);
    return Init_JointPosVelCmd_stamp(msg_);
  }

private:
  ::rebotarm_msgs::msg::JointPosVelCmd msg_;
};

class Init_JointPosVelCmd_pos
{
public:
  Init_JointPosVelCmd_pos()
  : msg_(::rosidl_runtime_cpp::MessageInitialization::SKIP)
  {}
  Init_JointPosVelCmd_vlim pos(::rebotarm_msgs::msg::JointPosVelCmd::_pos_type arg)
  {
    msg_.pos = std::move(arg);
    return Init_JointPosVelCmd_vlim(msg_);
  }

private:
  ::rebotarm_msgs::msg::JointPosVelCmd msg_;
};

}  // namespace builder

}  // namespace msg

template<typename MessageType>
auto build();

template<>
inline
auto build<::rebotarm_msgs::msg::JointPosVelCmd>()
{
  return rebotarm_msgs::msg::builder::Init_JointPosVelCmd_pos();
}

}  // namespace rebotarm_msgs

#endif  // REBOTARM_MSGS__MSG__DETAIL__JOINT_POS_VEL_CMD__BUILDER_HPP_
