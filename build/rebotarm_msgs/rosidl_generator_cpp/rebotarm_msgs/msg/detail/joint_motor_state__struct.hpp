// generated from rosidl_generator_cpp/resource/idl__struct.hpp.em
// with input from rebotarm_msgs:msg/JointMotorState.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "rebotarm_msgs/msg/joint_motor_state.hpp"


#ifndef REBOTARM_MSGS__MSG__DETAIL__JOINT_MOTOR_STATE__STRUCT_HPP_
#define REBOTARM_MSGS__MSG__DETAIL__JOINT_MOTOR_STATE__STRUCT_HPP_

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "rosidl_runtime_cpp/bounded_vector.hpp"
#include "rosidl_runtime_cpp/message_initialization.hpp"


// Include directives for member types
// Member 'header'
#include "std_msgs/msg/detail/header__struct.hpp"

#ifndef _WIN32
# define DEPRECATED__rebotarm_msgs__msg__JointMotorState __attribute__((deprecated))
#else
# define DEPRECATED__rebotarm_msgs__msg__JointMotorState __declspec(deprecated)
#endif

namespace rebotarm_msgs
{

namespace msg
{

// message struct
template<class ContainerAllocator>
struct JointMotorState_
{
  using Type = JointMotorState_<ContainerAllocator>;

  explicit JointMotorState_(rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  : header(_init)
  {
    if (rosidl_runtime_cpp::MessageInitialization::ALL == _init ||
      rosidl_runtime_cpp::MessageInitialization::ZERO == _init)
    {
      this->joint_name = "";
      this->position = 0.0;
      this->velocity = 0.0;
      this->torque = 0.0;
      this->status_code = 0;
    }
  }

  explicit JointMotorState_(const ContainerAllocator & _alloc, rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  : header(_alloc, _init),
    joint_name(_alloc)
  {
    if (rosidl_runtime_cpp::MessageInitialization::ALL == _init ||
      rosidl_runtime_cpp::MessageInitialization::ZERO == _init)
    {
      this->joint_name = "";
      this->position = 0.0;
      this->velocity = 0.0;
      this->torque = 0.0;
      this->status_code = 0;
    }
  }

  // field types and members
  using _header_type =
    std_msgs::msg::Header_<ContainerAllocator>;
  _header_type header;
  using _joint_name_type =
    std::basic_string<char, std::char_traits<char>, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<char>>;
  _joint_name_type joint_name;
  using _position_type =
    double;
  _position_type position;
  using _velocity_type =
    double;
  _velocity_type velocity;
  using _torque_type =
    double;
  _torque_type torque;
  using _status_code_type =
    uint8_t;
  _status_code_type status_code;

  // setters for named parameter idiom
  Type & set__header(
    const std_msgs::msg::Header_<ContainerAllocator> & _arg)
  {
    this->header = _arg;
    return *this;
  }
  Type & set__joint_name(
    const std::basic_string<char, std::char_traits<char>, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<char>> & _arg)
  {
    this->joint_name = _arg;
    return *this;
  }
  Type & set__position(
    const double & _arg)
  {
    this->position = _arg;
    return *this;
  }
  Type & set__velocity(
    const double & _arg)
  {
    this->velocity = _arg;
    return *this;
  }
  Type & set__torque(
    const double & _arg)
  {
    this->torque = _arg;
    return *this;
  }
  Type & set__status_code(
    const uint8_t & _arg)
  {
    this->status_code = _arg;
    return *this;
  }

  // constant declarations

  // pointer types
  using RawPtr =
    rebotarm_msgs::msg::JointMotorState_<ContainerAllocator> *;
  using ConstRawPtr =
    const rebotarm_msgs::msg::JointMotorState_<ContainerAllocator> *;
  using SharedPtr =
    std::shared_ptr<rebotarm_msgs::msg::JointMotorState_<ContainerAllocator>>;
  using ConstSharedPtr =
    std::shared_ptr<rebotarm_msgs::msg::JointMotorState_<ContainerAllocator> const>;

  template<typename Deleter = std::default_delete<
      rebotarm_msgs::msg::JointMotorState_<ContainerAllocator>>>
  using UniquePtrWithDeleter =
    std::unique_ptr<rebotarm_msgs::msg::JointMotorState_<ContainerAllocator>, Deleter>;

  using UniquePtr = UniquePtrWithDeleter<>;

  template<typename Deleter = std::default_delete<
      rebotarm_msgs::msg::JointMotorState_<ContainerAllocator>>>
  using ConstUniquePtrWithDeleter =
    std::unique_ptr<rebotarm_msgs::msg::JointMotorState_<ContainerAllocator> const, Deleter>;
  using ConstUniquePtr = ConstUniquePtrWithDeleter<>;

  using WeakPtr =
    std::weak_ptr<rebotarm_msgs::msg::JointMotorState_<ContainerAllocator>>;
  using ConstWeakPtr =
    std::weak_ptr<rebotarm_msgs::msg::JointMotorState_<ContainerAllocator> const>;

  // pointer types similar to ROS 1, use SharedPtr / ConstSharedPtr instead
  // NOTE: Can't use 'using' here because GNU C++ can't parse attributes properly
  typedef DEPRECATED__rebotarm_msgs__msg__JointMotorState
    std::shared_ptr<rebotarm_msgs::msg::JointMotorState_<ContainerAllocator>>
    Ptr;
  typedef DEPRECATED__rebotarm_msgs__msg__JointMotorState
    std::shared_ptr<rebotarm_msgs::msg::JointMotorState_<ContainerAllocator> const>
    ConstPtr;

  // comparison operators
  bool operator==(const JointMotorState_ & other) const
  {
    if (this->header != other.header) {
      return false;
    }
    if (this->joint_name != other.joint_name) {
      return false;
    }
    if (this->position != other.position) {
      return false;
    }
    if (this->velocity != other.velocity) {
      return false;
    }
    if (this->torque != other.torque) {
      return false;
    }
    if (this->status_code != other.status_code) {
      return false;
    }
    return true;
  }
  bool operator!=(const JointMotorState_ & other) const
  {
    return !this->operator==(other);
  }
};  // struct JointMotorState_

// alias to use template instance with default allocator
using JointMotorState =
  rebotarm_msgs::msg::JointMotorState_<std::allocator<void>>;

// constant definitions

}  // namespace msg

}  // namespace rebotarm_msgs

#endif  // REBOTARM_MSGS__MSG__DETAIL__JOINT_MOTOR_STATE__STRUCT_HPP_
