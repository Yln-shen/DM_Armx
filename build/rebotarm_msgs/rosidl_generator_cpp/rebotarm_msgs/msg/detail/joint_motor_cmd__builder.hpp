// generated from rosidl_generator_cpp/resource/idl__builder.hpp.em
// with input from rebotarm_msgs:msg/JointMotorCmd.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "rebotarm_msgs/msg/joint_motor_cmd.hpp"


#ifndef REBOTARM_MSGS__MSG__DETAIL__JOINT_MOTOR_CMD__BUILDER_HPP_
#define REBOTARM_MSGS__MSG__DETAIL__JOINT_MOTOR_CMD__BUILDER_HPP_

#include <algorithm>
#include <utility>

#include "rebotarm_msgs/msg/detail/joint_motor_cmd__struct.hpp"
#include "rosidl_runtime_cpp/message_initialization.hpp"


namespace rebotarm_msgs
{

namespace msg
{

namespace builder
{

class Init_JointMotorCmd_stamp
{
public:
  explicit Init_JointMotorCmd_stamp(::rebotarm_msgs::msg::JointMotorCmd & msg)
  : msg_(msg)
  {}
  ::rebotarm_msgs::msg::JointMotorCmd stamp(::rebotarm_msgs::msg::JointMotorCmd::_stamp_type arg)
  {
    msg_.stamp = std::move(arg);
    return std::move(msg_);
  }

private:
  ::rebotarm_msgs::msg::JointMotorCmd msg_;
};

class Init_JointMotorCmd_vlim
{
public:
  explicit Init_JointMotorCmd_vlim(::rebotarm_msgs::msg::JointMotorCmd & msg)
  : msg_(msg)
  {}
  Init_JointMotorCmd_stamp vlim(::rebotarm_msgs::msg::JointMotorCmd::_vlim_type arg)
  {
    msg_.vlim = std::move(arg);
    return Init_JointMotorCmd_stamp(msg_);
  }

private:
  ::rebotarm_msgs::msg::JointMotorCmd msg_;
};

class Init_JointMotorCmd_tau
{
public:
  explicit Init_JointMotorCmd_tau(::rebotarm_msgs::msg::JointMotorCmd & msg)
  : msg_(msg)
  {}
  Init_JointMotorCmd_vlim tau(::rebotarm_msgs::msg::JointMotorCmd::_tau_type arg)
  {
    msg_.tau = std::move(arg);
    return Init_JointMotorCmd_vlim(msg_);
  }

private:
  ::rebotarm_msgs::msg::JointMotorCmd msg_;
};

class Init_JointMotorCmd_kd
{
public:
  explicit Init_JointMotorCmd_kd(::rebotarm_msgs::msg::JointMotorCmd & msg)
  : msg_(msg)
  {}
  Init_JointMotorCmd_tau kd(::rebotarm_msgs::msg::JointMotorCmd::_kd_type arg)
  {
    msg_.kd = std::move(arg);
    return Init_JointMotorCmd_tau(msg_);
  }

private:
  ::rebotarm_msgs::msg::JointMotorCmd msg_;
};

class Init_JointMotorCmd_kp
{
public:
  explicit Init_JointMotorCmd_kp(::rebotarm_msgs::msg::JointMotorCmd & msg)
  : msg_(msg)
  {}
  Init_JointMotorCmd_kd kp(::rebotarm_msgs::msg::JointMotorCmd::_kp_type arg)
  {
    msg_.kp = std::move(arg);
    return Init_JointMotorCmd_kd(msg_);
  }

private:
  ::rebotarm_msgs::msg::JointMotorCmd msg_;
};

class Init_JointMotorCmd_vel
{
public:
  explicit Init_JointMotorCmd_vel(::rebotarm_msgs::msg::JointMotorCmd & msg)
  : msg_(msg)
  {}
  Init_JointMotorCmd_kp vel(::rebotarm_msgs::msg::JointMotorCmd::_vel_type arg)
  {
    msg_.vel = std::move(arg);
    return Init_JointMotorCmd_kp(msg_);
  }

private:
  ::rebotarm_msgs::msg::JointMotorCmd msg_;
};

class Init_JointMotorCmd_pos
{
public:
  explicit Init_JointMotorCmd_pos(::rebotarm_msgs::msg::JointMotorCmd & msg)
  : msg_(msg)
  {}
  Init_JointMotorCmd_vel pos(::rebotarm_msgs::msg::JointMotorCmd::_pos_type arg)
  {
    msg_.pos = std::move(arg);
    return Init_JointMotorCmd_vel(msg_);
  }

private:
  ::rebotarm_msgs::msg::JointMotorCmd msg_;
};

class Init_JointMotorCmd_use_vlim
{
public:
  explicit Init_JointMotorCmd_use_vlim(::rebotarm_msgs::msg::JointMotorCmd & msg)
  : msg_(msg)
  {}
  Init_JointMotorCmd_pos use_vlim(::rebotarm_msgs::msg::JointMotorCmd::_use_vlim_type arg)
  {
    msg_.use_vlim = std::move(arg);
    return Init_JointMotorCmd_pos(msg_);
  }

private:
  ::rebotarm_msgs::msg::JointMotorCmd msg_;
};

class Init_JointMotorCmd_use_tau
{
public:
  explicit Init_JointMotorCmd_use_tau(::rebotarm_msgs::msg::JointMotorCmd & msg)
  : msg_(msg)
  {}
  Init_JointMotorCmd_use_vlim use_tau(::rebotarm_msgs::msg::JointMotorCmd::_use_tau_type arg)
  {
    msg_.use_tau = std::move(arg);
    return Init_JointMotorCmd_use_vlim(msg_);
  }

private:
  ::rebotarm_msgs::msg::JointMotorCmd msg_;
};

class Init_JointMotorCmd_use_kd
{
public:
  explicit Init_JointMotorCmd_use_kd(::rebotarm_msgs::msg::JointMotorCmd & msg)
  : msg_(msg)
  {}
  Init_JointMotorCmd_use_tau use_kd(::rebotarm_msgs::msg::JointMotorCmd::_use_kd_type arg)
  {
    msg_.use_kd = std::move(arg);
    return Init_JointMotorCmd_use_tau(msg_);
  }

private:
  ::rebotarm_msgs::msg::JointMotorCmd msg_;
};

class Init_JointMotorCmd_use_kp
{
public:
  explicit Init_JointMotorCmd_use_kp(::rebotarm_msgs::msg::JointMotorCmd & msg)
  : msg_(msg)
  {}
  Init_JointMotorCmd_use_kd use_kp(::rebotarm_msgs::msg::JointMotorCmd::_use_kp_type arg)
  {
    msg_.use_kp = std::move(arg);
    return Init_JointMotorCmd_use_kd(msg_);
  }

private:
  ::rebotarm_msgs::msg::JointMotorCmd msg_;
};

class Init_JointMotorCmd_use_vel
{
public:
  explicit Init_JointMotorCmd_use_vel(::rebotarm_msgs::msg::JointMotorCmd & msg)
  : msg_(msg)
  {}
  Init_JointMotorCmd_use_kp use_vel(::rebotarm_msgs::msg::JointMotorCmd::_use_vel_type arg)
  {
    msg_.use_vel = std::move(arg);
    return Init_JointMotorCmd_use_kp(msg_);
  }

private:
  ::rebotarm_msgs::msg::JointMotorCmd msg_;
};

class Init_JointMotorCmd_use_pos
{
public:
  explicit Init_JointMotorCmd_use_pos(::rebotarm_msgs::msg::JointMotorCmd & msg)
  : msg_(msg)
  {}
  Init_JointMotorCmd_use_vel use_pos(::rebotarm_msgs::msg::JointMotorCmd::_use_pos_type arg)
  {
    msg_.use_pos = std::move(arg);
    return Init_JointMotorCmd_use_vel(msg_);
  }

private:
  ::rebotarm_msgs::msg::JointMotorCmd msg_;
};

class Init_JointMotorCmd_mode
{
public:
  Init_JointMotorCmd_mode()
  : msg_(::rosidl_runtime_cpp::MessageInitialization::SKIP)
  {}
  Init_JointMotorCmd_use_pos mode(::rebotarm_msgs::msg::JointMotorCmd::_mode_type arg)
  {
    msg_.mode = std::move(arg);
    return Init_JointMotorCmd_use_pos(msg_);
  }

private:
  ::rebotarm_msgs::msg::JointMotorCmd msg_;
};

}  // namespace builder

}  // namespace msg

template<typename MessageType>
auto build();

template<>
inline
auto build<::rebotarm_msgs::msg::JointMotorCmd>()
{
  return rebotarm_msgs::msg::builder::Init_JointMotorCmd_mode();
}

}  // namespace rebotarm_msgs

#endif  // REBOTARM_MSGS__MSG__DETAIL__JOINT_MOTOR_CMD__BUILDER_HPP_
