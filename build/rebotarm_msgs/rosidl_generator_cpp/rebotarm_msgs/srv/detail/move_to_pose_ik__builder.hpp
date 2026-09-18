// generated from rosidl_generator_cpp/resource/idl__builder.hpp.em
// with input from rebotarm_msgs:srv/MoveToPoseIK.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "rebotarm_msgs/srv/move_to_pose_ik.hpp"


#ifndef REBOTARM_MSGS__SRV__DETAIL__MOVE_TO_POSE_IK__BUILDER_HPP_
#define REBOTARM_MSGS__SRV__DETAIL__MOVE_TO_POSE_IK__BUILDER_HPP_

#include <algorithm>
#include <utility>

#include "rebotarm_msgs/srv/detail/move_to_pose_ik__struct.hpp"
#include "rosidl_runtime_cpp/message_initialization.hpp"


namespace rebotarm_msgs
{

namespace srv
{

namespace builder
{

class Init_MoveToPoseIK_Request_target_pose
{
public:
  Init_MoveToPoseIK_Request_target_pose()
  : msg_(::rosidl_runtime_cpp::MessageInitialization::SKIP)
  {}
  ::rebotarm_msgs::srv::MoveToPoseIK_Request target_pose(::rebotarm_msgs::srv::MoveToPoseIK_Request::_target_pose_type arg)
  {
    msg_.target_pose = std::move(arg);
    return std::move(msg_);
  }

private:
  ::rebotarm_msgs::srv::MoveToPoseIK_Request msg_;
};

}  // namespace builder

}  // namespace srv

template<typename MessageType>
auto build();

template<>
inline
auto build<::rebotarm_msgs::srv::MoveToPoseIK_Request>()
{
  return rebotarm_msgs::srv::builder::Init_MoveToPoseIK_Request_target_pose();
}

}  // namespace rebotarm_msgs


namespace rebotarm_msgs
{

namespace srv
{

namespace builder
{

class Init_MoveToPoseIK_Response_q_solution
{
public:
  explicit Init_MoveToPoseIK_Response_q_solution(::rebotarm_msgs::srv::MoveToPoseIK_Response & msg)
  : msg_(msg)
  {}
  ::rebotarm_msgs::srv::MoveToPoseIK_Response q_solution(::rebotarm_msgs::srv::MoveToPoseIK_Response::_q_solution_type arg)
  {
    msg_.q_solution = std::move(arg);
    return std::move(msg_);
  }

private:
  ::rebotarm_msgs::srv::MoveToPoseIK_Response msg_;
};

class Init_MoveToPoseIK_Response_message
{
public:
  explicit Init_MoveToPoseIK_Response_message(::rebotarm_msgs::srv::MoveToPoseIK_Response & msg)
  : msg_(msg)
  {}
  Init_MoveToPoseIK_Response_q_solution message(::rebotarm_msgs::srv::MoveToPoseIK_Response::_message_type arg)
  {
    msg_.message = std::move(arg);
    return Init_MoveToPoseIK_Response_q_solution(msg_);
  }

private:
  ::rebotarm_msgs::srv::MoveToPoseIK_Response msg_;
};

class Init_MoveToPoseIK_Response_success
{
public:
  Init_MoveToPoseIK_Response_success()
  : msg_(::rosidl_runtime_cpp::MessageInitialization::SKIP)
  {}
  Init_MoveToPoseIK_Response_message success(::rebotarm_msgs::srv::MoveToPoseIK_Response::_success_type arg)
  {
    msg_.success = std::move(arg);
    return Init_MoveToPoseIK_Response_message(msg_);
  }

private:
  ::rebotarm_msgs::srv::MoveToPoseIK_Response msg_;
};

}  // namespace builder

}  // namespace srv

template<typename MessageType>
auto build();

template<>
inline
auto build<::rebotarm_msgs::srv::MoveToPoseIK_Response>()
{
  return rebotarm_msgs::srv::builder::Init_MoveToPoseIK_Response_success();
}

}  // namespace rebotarm_msgs


namespace rebotarm_msgs
{

namespace srv
{

namespace builder
{

class Init_MoveToPoseIK_Event_response
{
public:
  explicit Init_MoveToPoseIK_Event_response(::rebotarm_msgs::srv::MoveToPoseIK_Event & msg)
  : msg_(msg)
  {}
  ::rebotarm_msgs::srv::MoveToPoseIK_Event response(::rebotarm_msgs::srv::MoveToPoseIK_Event::_response_type arg)
  {
    msg_.response = std::move(arg);
    return std::move(msg_);
  }

private:
  ::rebotarm_msgs::srv::MoveToPoseIK_Event msg_;
};

class Init_MoveToPoseIK_Event_request
{
public:
  explicit Init_MoveToPoseIK_Event_request(::rebotarm_msgs::srv::MoveToPoseIK_Event & msg)
  : msg_(msg)
  {}
  Init_MoveToPoseIK_Event_response request(::rebotarm_msgs::srv::MoveToPoseIK_Event::_request_type arg)
  {
    msg_.request = std::move(arg);
    return Init_MoveToPoseIK_Event_response(msg_);
  }

private:
  ::rebotarm_msgs::srv::MoveToPoseIK_Event msg_;
};

class Init_MoveToPoseIK_Event_info
{
public:
  Init_MoveToPoseIK_Event_info()
  : msg_(::rosidl_runtime_cpp::MessageInitialization::SKIP)
  {}
  Init_MoveToPoseIK_Event_request info(::rebotarm_msgs::srv::MoveToPoseIK_Event::_info_type arg)
  {
    msg_.info = std::move(arg);
    return Init_MoveToPoseIK_Event_request(msg_);
  }

private:
  ::rebotarm_msgs::srv::MoveToPoseIK_Event msg_;
};

}  // namespace builder

}  // namespace srv

template<typename MessageType>
auto build();

template<>
inline
auto build<::rebotarm_msgs::srv::MoveToPoseIK_Event>()
{
  return rebotarm_msgs::srv::builder::Init_MoveToPoseIK_Event_info();
}

}  // namespace rebotarm_msgs

#endif  // REBOTARM_MSGS__SRV__DETAIL__MOVE_TO_POSE_IK__BUILDER_HPP_
