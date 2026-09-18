// generated from rosidl_generator_cpp/resource/idl__struct.hpp.em
// with input from rebotarm_msgs:srv/MoveToPoseIK.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "rebotarm_msgs/srv/move_to_pose_ik.hpp"


#ifndef REBOTARM_MSGS__SRV__DETAIL__MOVE_TO_POSE_IK__STRUCT_HPP_
#define REBOTARM_MSGS__SRV__DETAIL__MOVE_TO_POSE_IK__STRUCT_HPP_

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "rosidl_runtime_cpp/bounded_vector.hpp"
#include "rosidl_runtime_cpp/message_initialization.hpp"


// Include directives for member types
// Member 'target_pose'
#include "geometry_msgs/msg/detail/pose__struct.hpp"

#ifndef _WIN32
# define DEPRECATED__rebotarm_msgs__srv__MoveToPoseIK_Request __attribute__((deprecated))
#else
# define DEPRECATED__rebotarm_msgs__srv__MoveToPoseIK_Request __declspec(deprecated)
#endif

namespace rebotarm_msgs
{

namespace srv
{

// message struct
template<class ContainerAllocator>
struct MoveToPoseIK_Request_
{
  using Type = MoveToPoseIK_Request_<ContainerAllocator>;

  explicit MoveToPoseIK_Request_(rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  : target_pose(_init)
  {
    (void)_init;
  }

  explicit MoveToPoseIK_Request_(const ContainerAllocator & _alloc, rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  : target_pose(_alloc, _init)
  {
    (void)_init;
  }

  // field types and members
  using _target_pose_type =
    geometry_msgs::msg::Pose_<ContainerAllocator>;
  _target_pose_type target_pose;

  // setters for named parameter idiom
  Type & set__target_pose(
    const geometry_msgs::msg::Pose_<ContainerAllocator> & _arg)
  {
    this->target_pose = _arg;
    return *this;
  }

  // constant declarations

  // pointer types
  using RawPtr =
    rebotarm_msgs::srv::MoveToPoseIK_Request_<ContainerAllocator> *;
  using ConstRawPtr =
    const rebotarm_msgs::srv::MoveToPoseIK_Request_<ContainerAllocator> *;
  using SharedPtr =
    std::shared_ptr<rebotarm_msgs::srv::MoveToPoseIK_Request_<ContainerAllocator>>;
  using ConstSharedPtr =
    std::shared_ptr<rebotarm_msgs::srv::MoveToPoseIK_Request_<ContainerAllocator> const>;

  template<typename Deleter = std::default_delete<
      rebotarm_msgs::srv::MoveToPoseIK_Request_<ContainerAllocator>>>
  using UniquePtrWithDeleter =
    std::unique_ptr<rebotarm_msgs::srv::MoveToPoseIK_Request_<ContainerAllocator>, Deleter>;

  using UniquePtr = UniquePtrWithDeleter<>;

  template<typename Deleter = std::default_delete<
      rebotarm_msgs::srv::MoveToPoseIK_Request_<ContainerAllocator>>>
  using ConstUniquePtrWithDeleter =
    std::unique_ptr<rebotarm_msgs::srv::MoveToPoseIK_Request_<ContainerAllocator> const, Deleter>;
  using ConstUniquePtr = ConstUniquePtrWithDeleter<>;

  using WeakPtr =
    std::weak_ptr<rebotarm_msgs::srv::MoveToPoseIK_Request_<ContainerAllocator>>;
  using ConstWeakPtr =
    std::weak_ptr<rebotarm_msgs::srv::MoveToPoseIK_Request_<ContainerAllocator> const>;

  // pointer types similar to ROS 1, use SharedPtr / ConstSharedPtr instead
  // NOTE: Can't use 'using' here because GNU C++ can't parse attributes properly
  typedef DEPRECATED__rebotarm_msgs__srv__MoveToPoseIK_Request
    std::shared_ptr<rebotarm_msgs::srv::MoveToPoseIK_Request_<ContainerAllocator>>
    Ptr;
  typedef DEPRECATED__rebotarm_msgs__srv__MoveToPoseIK_Request
    std::shared_ptr<rebotarm_msgs::srv::MoveToPoseIK_Request_<ContainerAllocator> const>
    ConstPtr;

  // comparison operators
  bool operator==(const MoveToPoseIK_Request_ & other) const
  {
    if (this->target_pose != other.target_pose) {
      return false;
    }
    return true;
  }
  bool operator!=(const MoveToPoseIK_Request_ & other) const
  {
    return !this->operator==(other);
  }
};  // struct MoveToPoseIK_Request_

// alias to use template instance with default allocator
using MoveToPoseIK_Request =
  rebotarm_msgs::srv::MoveToPoseIK_Request_<std::allocator<void>>;

// constant definitions

}  // namespace srv

}  // namespace rebotarm_msgs


#ifndef _WIN32
# define DEPRECATED__rebotarm_msgs__srv__MoveToPoseIK_Response __attribute__((deprecated))
#else
# define DEPRECATED__rebotarm_msgs__srv__MoveToPoseIK_Response __declspec(deprecated)
#endif

namespace rebotarm_msgs
{

namespace srv
{

// message struct
template<class ContainerAllocator>
struct MoveToPoseIK_Response_
{
  using Type = MoveToPoseIK_Response_<ContainerAllocator>;

  explicit MoveToPoseIK_Response_(rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  {
    if (rosidl_runtime_cpp::MessageInitialization::ALL == _init ||
      rosidl_runtime_cpp::MessageInitialization::ZERO == _init)
    {
      this->success = false;
      this->message = "";
    }
  }

  explicit MoveToPoseIK_Response_(const ContainerAllocator & _alloc, rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  : message(_alloc)
  {
    if (rosidl_runtime_cpp::MessageInitialization::ALL == _init ||
      rosidl_runtime_cpp::MessageInitialization::ZERO == _init)
    {
      this->success = false;
      this->message = "";
    }
  }

  // field types and members
  using _success_type =
    bool;
  _success_type success;
  using _message_type =
    std::basic_string<char, std::char_traits<char>, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<char>>;
  _message_type message;
  using _q_solution_type =
    std::vector<double, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<double>>;
  _q_solution_type q_solution;

  // setters for named parameter idiom
  Type & set__success(
    const bool & _arg)
  {
    this->success = _arg;
    return *this;
  }
  Type & set__message(
    const std::basic_string<char, std::char_traits<char>, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<char>> & _arg)
  {
    this->message = _arg;
    return *this;
  }
  Type & set__q_solution(
    const std::vector<double, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<double>> & _arg)
  {
    this->q_solution = _arg;
    return *this;
  }

  // constant declarations

  // pointer types
  using RawPtr =
    rebotarm_msgs::srv::MoveToPoseIK_Response_<ContainerAllocator> *;
  using ConstRawPtr =
    const rebotarm_msgs::srv::MoveToPoseIK_Response_<ContainerAllocator> *;
  using SharedPtr =
    std::shared_ptr<rebotarm_msgs::srv::MoveToPoseIK_Response_<ContainerAllocator>>;
  using ConstSharedPtr =
    std::shared_ptr<rebotarm_msgs::srv::MoveToPoseIK_Response_<ContainerAllocator> const>;

  template<typename Deleter = std::default_delete<
      rebotarm_msgs::srv::MoveToPoseIK_Response_<ContainerAllocator>>>
  using UniquePtrWithDeleter =
    std::unique_ptr<rebotarm_msgs::srv::MoveToPoseIK_Response_<ContainerAllocator>, Deleter>;

  using UniquePtr = UniquePtrWithDeleter<>;

  template<typename Deleter = std::default_delete<
      rebotarm_msgs::srv::MoveToPoseIK_Response_<ContainerAllocator>>>
  using ConstUniquePtrWithDeleter =
    std::unique_ptr<rebotarm_msgs::srv::MoveToPoseIK_Response_<ContainerAllocator> const, Deleter>;
  using ConstUniquePtr = ConstUniquePtrWithDeleter<>;

  using WeakPtr =
    std::weak_ptr<rebotarm_msgs::srv::MoveToPoseIK_Response_<ContainerAllocator>>;
  using ConstWeakPtr =
    std::weak_ptr<rebotarm_msgs::srv::MoveToPoseIK_Response_<ContainerAllocator> const>;

  // pointer types similar to ROS 1, use SharedPtr / ConstSharedPtr instead
  // NOTE: Can't use 'using' here because GNU C++ can't parse attributes properly
  typedef DEPRECATED__rebotarm_msgs__srv__MoveToPoseIK_Response
    std::shared_ptr<rebotarm_msgs::srv::MoveToPoseIK_Response_<ContainerAllocator>>
    Ptr;
  typedef DEPRECATED__rebotarm_msgs__srv__MoveToPoseIK_Response
    std::shared_ptr<rebotarm_msgs::srv::MoveToPoseIK_Response_<ContainerAllocator> const>
    ConstPtr;

  // comparison operators
  bool operator==(const MoveToPoseIK_Response_ & other) const
  {
    if (this->success != other.success) {
      return false;
    }
    if (this->message != other.message) {
      return false;
    }
    if (this->q_solution != other.q_solution) {
      return false;
    }
    return true;
  }
  bool operator!=(const MoveToPoseIK_Response_ & other) const
  {
    return !this->operator==(other);
  }
};  // struct MoveToPoseIK_Response_

// alias to use template instance with default allocator
using MoveToPoseIK_Response =
  rebotarm_msgs::srv::MoveToPoseIK_Response_<std::allocator<void>>;

// constant definitions

}  // namespace srv

}  // namespace rebotarm_msgs


// Include directives for member types
// Member 'info'
#include "service_msgs/msg/detail/service_event_info__struct.hpp"

#ifndef _WIN32
# define DEPRECATED__rebotarm_msgs__srv__MoveToPoseIK_Event __attribute__((deprecated))
#else
# define DEPRECATED__rebotarm_msgs__srv__MoveToPoseIK_Event __declspec(deprecated)
#endif

namespace rebotarm_msgs
{

namespace srv
{

// message struct
template<class ContainerAllocator>
struct MoveToPoseIK_Event_
{
  using Type = MoveToPoseIK_Event_<ContainerAllocator>;

  explicit MoveToPoseIK_Event_(rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  : info(_init)
  {
    (void)_init;
  }

  explicit MoveToPoseIK_Event_(const ContainerAllocator & _alloc, rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  : info(_alloc, _init)
  {
    (void)_init;
  }

  // field types and members
  using _info_type =
    service_msgs::msg::ServiceEventInfo_<ContainerAllocator>;
  _info_type info;
  using _request_type =
    rosidl_runtime_cpp::BoundedVector<rebotarm_msgs::srv::MoveToPoseIK_Request_<ContainerAllocator>, 1, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<rebotarm_msgs::srv::MoveToPoseIK_Request_<ContainerAllocator>>>;
  _request_type request;
  using _response_type =
    rosidl_runtime_cpp::BoundedVector<rebotarm_msgs::srv::MoveToPoseIK_Response_<ContainerAllocator>, 1, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<rebotarm_msgs::srv::MoveToPoseIK_Response_<ContainerAllocator>>>;
  _response_type response;

  // setters for named parameter idiom
  Type & set__info(
    const service_msgs::msg::ServiceEventInfo_<ContainerAllocator> & _arg)
  {
    this->info = _arg;
    return *this;
  }
  Type & set__request(
    const rosidl_runtime_cpp::BoundedVector<rebotarm_msgs::srv::MoveToPoseIK_Request_<ContainerAllocator>, 1, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<rebotarm_msgs::srv::MoveToPoseIK_Request_<ContainerAllocator>>> & _arg)
  {
    this->request = _arg;
    return *this;
  }
  Type & set__response(
    const rosidl_runtime_cpp::BoundedVector<rebotarm_msgs::srv::MoveToPoseIK_Response_<ContainerAllocator>, 1, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<rebotarm_msgs::srv::MoveToPoseIK_Response_<ContainerAllocator>>> & _arg)
  {
    this->response = _arg;
    return *this;
  }

  // constant declarations

  // pointer types
  using RawPtr =
    rebotarm_msgs::srv::MoveToPoseIK_Event_<ContainerAllocator> *;
  using ConstRawPtr =
    const rebotarm_msgs::srv::MoveToPoseIK_Event_<ContainerAllocator> *;
  using SharedPtr =
    std::shared_ptr<rebotarm_msgs::srv::MoveToPoseIK_Event_<ContainerAllocator>>;
  using ConstSharedPtr =
    std::shared_ptr<rebotarm_msgs::srv::MoveToPoseIK_Event_<ContainerAllocator> const>;

  template<typename Deleter = std::default_delete<
      rebotarm_msgs::srv::MoveToPoseIK_Event_<ContainerAllocator>>>
  using UniquePtrWithDeleter =
    std::unique_ptr<rebotarm_msgs::srv::MoveToPoseIK_Event_<ContainerAllocator>, Deleter>;

  using UniquePtr = UniquePtrWithDeleter<>;

  template<typename Deleter = std::default_delete<
      rebotarm_msgs::srv::MoveToPoseIK_Event_<ContainerAllocator>>>
  using ConstUniquePtrWithDeleter =
    std::unique_ptr<rebotarm_msgs::srv::MoveToPoseIK_Event_<ContainerAllocator> const, Deleter>;
  using ConstUniquePtr = ConstUniquePtrWithDeleter<>;

  using WeakPtr =
    std::weak_ptr<rebotarm_msgs::srv::MoveToPoseIK_Event_<ContainerAllocator>>;
  using ConstWeakPtr =
    std::weak_ptr<rebotarm_msgs::srv::MoveToPoseIK_Event_<ContainerAllocator> const>;

  // pointer types similar to ROS 1, use SharedPtr / ConstSharedPtr instead
  // NOTE: Can't use 'using' here because GNU C++ can't parse attributes properly
  typedef DEPRECATED__rebotarm_msgs__srv__MoveToPoseIK_Event
    std::shared_ptr<rebotarm_msgs::srv::MoveToPoseIK_Event_<ContainerAllocator>>
    Ptr;
  typedef DEPRECATED__rebotarm_msgs__srv__MoveToPoseIK_Event
    std::shared_ptr<rebotarm_msgs::srv::MoveToPoseIK_Event_<ContainerAllocator> const>
    ConstPtr;

  // comparison operators
  bool operator==(const MoveToPoseIK_Event_ & other) const
  {
    if (this->info != other.info) {
      return false;
    }
    if (this->request != other.request) {
      return false;
    }
    if (this->response != other.response) {
      return false;
    }
    return true;
  }
  bool operator!=(const MoveToPoseIK_Event_ & other) const
  {
    return !this->operator==(other);
  }
};  // struct MoveToPoseIK_Event_

// alias to use template instance with default allocator
using MoveToPoseIK_Event =
  rebotarm_msgs::srv::MoveToPoseIK_Event_<std::allocator<void>>;

// constant definitions

}  // namespace srv

}  // namespace rebotarm_msgs

namespace rebotarm_msgs
{

namespace srv
{

struct MoveToPoseIK
{
  using Request = rebotarm_msgs::srv::MoveToPoseIK_Request;
  using Response = rebotarm_msgs::srv::MoveToPoseIK_Response;
  using Event = rebotarm_msgs::srv::MoveToPoseIK_Event;
};

}  // namespace srv

}  // namespace rebotarm_msgs

#endif  // REBOTARM_MSGS__SRV__DETAIL__MOVE_TO_POSE_IK__STRUCT_HPP_
