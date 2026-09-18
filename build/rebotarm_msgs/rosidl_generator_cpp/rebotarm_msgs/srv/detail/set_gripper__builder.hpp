// generated from rosidl_generator_cpp/resource/idl__builder.hpp.em
// with input from rebotarm_msgs:srv/SetGripper.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "rebotarm_msgs/srv/set_gripper.hpp"


#ifndef REBOTARM_MSGS__SRV__DETAIL__SET_GRIPPER__BUILDER_HPP_
#define REBOTARM_MSGS__SRV__DETAIL__SET_GRIPPER__BUILDER_HPP_

#include <algorithm>
#include <utility>

#include "rebotarm_msgs/srv/detail/set_gripper__struct.hpp"
#include "rosidl_runtime_cpp/message_initialization.hpp"


namespace rebotarm_msgs
{

namespace srv
{

namespace builder
{

class Init_SetGripper_Request_max_effort
{
public:
  explicit Init_SetGripper_Request_max_effort(::rebotarm_msgs::srv::SetGripper_Request & msg)
  : msg_(msg)
  {}
  ::rebotarm_msgs::srv::SetGripper_Request max_effort(::rebotarm_msgs::srv::SetGripper_Request::_max_effort_type arg)
  {
    msg_.max_effort = std::move(arg);
    return std::move(msg_);
  }

private:
  ::rebotarm_msgs::srv::SetGripper_Request msg_;
};

class Init_SetGripper_Request_position
{
public:
  Init_SetGripper_Request_position()
  : msg_(::rosidl_runtime_cpp::MessageInitialization::SKIP)
  {}
  Init_SetGripper_Request_max_effort position(::rebotarm_msgs::srv::SetGripper_Request::_position_type arg)
  {
    msg_.position = std::move(arg);
    return Init_SetGripper_Request_max_effort(msg_);
  }

private:
  ::rebotarm_msgs::srv::SetGripper_Request msg_;
};

}  // namespace builder

}  // namespace srv

template<typename MessageType>
auto build();

template<>
inline
auto build<::rebotarm_msgs::srv::SetGripper_Request>()
{
  return rebotarm_msgs::srv::builder::Init_SetGripper_Request_position();
}

}  // namespace rebotarm_msgs


namespace rebotarm_msgs
{

namespace srv
{

namespace builder
{

class Init_SetGripper_Response_reached_position
{
public:
  explicit Init_SetGripper_Response_reached_position(::rebotarm_msgs::srv::SetGripper_Response & msg)
  : msg_(msg)
  {}
  ::rebotarm_msgs::srv::SetGripper_Response reached_position(::rebotarm_msgs::srv::SetGripper_Response::_reached_position_type arg)
  {
    msg_.reached_position = std::move(arg);
    return std::move(msg_);
  }

private:
  ::rebotarm_msgs::srv::SetGripper_Response msg_;
};

class Init_SetGripper_Response_success
{
public:
  Init_SetGripper_Response_success()
  : msg_(::rosidl_runtime_cpp::MessageInitialization::SKIP)
  {}
  Init_SetGripper_Response_reached_position success(::rebotarm_msgs::srv::SetGripper_Response::_success_type arg)
  {
    msg_.success = std::move(arg);
    return Init_SetGripper_Response_reached_position(msg_);
  }

private:
  ::rebotarm_msgs::srv::SetGripper_Response msg_;
};

}  // namespace builder

}  // namespace srv

template<typename MessageType>
auto build();

template<>
inline
auto build<::rebotarm_msgs::srv::SetGripper_Response>()
{
  return rebotarm_msgs::srv::builder::Init_SetGripper_Response_success();
}

}  // namespace rebotarm_msgs


namespace rebotarm_msgs
{

namespace srv
{

namespace builder
{

class Init_SetGripper_Event_response
{
public:
  explicit Init_SetGripper_Event_response(::rebotarm_msgs::srv::SetGripper_Event & msg)
  : msg_(msg)
  {}
  ::rebotarm_msgs::srv::SetGripper_Event response(::rebotarm_msgs::srv::SetGripper_Event::_response_type arg)
  {
    msg_.response = std::move(arg);
    return std::move(msg_);
  }

private:
  ::rebotarm_msgs::srv::SetGripper_Event msg_;
};

class Init_SetGripper_Event_request
{
public:
  explicit Init_SetGripper_Event_request(::rebotarm_msgs::srv::SetGripper_Event & msg)
  : msg_(msg)
  {}
  Init_SetGripper_Event_response request(::rebotarm_msgs::srv::SetGripper_Event::_request_type arg)
  {
    msg_.request = std::move(arg);
    return Init_SetGripper_Event_response(msg_);
  }

private:
  ::rebotarm_msgs::srv::SetGripper_Event msg_;
};

class Init_SetGripper_Event_info
{
public:
  Init_SetGripper_Event_info()
  : msg_(::rosidl_runtime_cpp::MessageInitialization::SKIP)
  {}
  Init_SetGripper_Event_request info(::rebotarm_msgs::srv::SetGripper_Event::_info_type arg)
  {
    msg_.info = std::move(arg);
    return Init_SetGripper_Event_request(msg_);
  }

private:
  ::rebotarm_msgs::srv::SetGripper_Event msg_;
};

}  // namespace builder

}  // namespace srv

template<typename MessageType>
auto build();

template<>
inline
auto build<::rebotarm_msgs::srv::SetGripper_Event>()
{
  return rebotarm_msgs::srv::builder::Init_SetGripper_Event_info();
}

}  // namespace rebotarm_msgs

#endif  // REBOTARM_MSGS__SRV__DETAIL__SET_GRIPPER__BUILDER_HPP_
