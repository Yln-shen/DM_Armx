// generated from rosidl_generator_cpp/resource/idl__struct.hpp.em
// with input from rebotarm_msgs:msg/JointMitCmd.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "rebotarm_msgs/msg/joint_mit_cmd.hpp"


#ifndef REBOTARM_MSGS__MSG__DETAIL__JOINT_MIT_CMD__STRUCT_HPP_
#define REBOTARM_MSGS__MSG__DETAIL__JOINT_MIT_CMD__STRUCT_HPP_

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "rosidl_runtime_cpp/bounded_vector.hpp"
#include "rosidl_runtime_cpp/message_initialization.hpp"


// Include directives for member types
// Member 'stamp'
#include "builtin_interfaces/msg/detail/time__struct.hpp"

#ifndef _WIN32
# define DEPRECATED__rebotarm_msgs__msg__JointMitCmd __attribute__((deprecated))
#else
# define DEPRECATED__rebotarm_msgs__msg__JointMitCmd __declspec(deprecated)
#endif

namespace rebotarm_msgs
{

namespace msg
{

// message struct
template<class ContainerAllocator>
struct JointMitCmd_
{
  using Type = JointMitCmd_<ContainerAllocator>;

  explicit JointMitCmd_(rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  : stamp(_init)
  {
    if (rosidl_runtime_cpp::MessageInitialization::ALL == _init ||
      rosidl_runtime_cpp::MessageInitialization::ZERO == _init)
    {
      this->pos = 0.0;
      this->vel = 0.0;
      this->kp = 0.0;
      this->kd = 0.0;
      this->tau = 0.0;
    }
  }

  explicit JointMitCmd_(const ContainerAllocator & _alloc, rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  : stamp(_alloc, _init)
  {
    if (rosidl_runtime_cpp::MessageInitialization::ALL == _init ||
      rosidl_runtime_cpp::MessageInitialization::ZERO == _init)
    {
      this->pos = 0.0;
      this->vel = 0.0;
      this->kp = 0.0;
      this->kd = 0.0;
      this->tau = 0.0;
    }
  }

  // field types and members
  using _pos_type =
    double;
  _pos_type pos;
  using _vel_type =
    double;
  _vel_type vel;
  using _kp_type =
    double;
  _kp_type kp;
  using _kd_type =
    double;
  _kd_type kd;
  using _tau_type =
    double;
  _tau_type tau;
  using _stamp_type =
    builtin_interfaces::msg::Time_<ContainerAllocator>;
  _stamp_type stamp;

  // setters for named parameter idiom
  Type & set__pos(
    const double & _arg)
  {
    this->pos = _arg;
    return *this;
  }
  Type & set__vel(
    const double & _arg)
  {
    this->vel = _arg;
    return *this;
  }
  Type & set__kp(
    const double & _arg)
  {
    this->kp = _arg;
    return *this;
  }
  Type & set__kd(
    const double & _arg)
  {
    this->kd = _arg;
    return *this;
  }
  Type & set__tau(
    const double & _arg)
  {
    this->tau = _arg;
    return *this;
  }
  Type & set__stamp(
    const builtin_interfaces::msg::Time_<ContainerAllocator> & _arg)
  {
    this->stamp = _arg;
    return *this;
  }

  // constant declarations

  // pointer types
  using RawPtr =
    rebotarm_msgs::msg::JointMitCmd_<ContainerAllocator> *;
  using ConstRawPtr =
    const rebotarm_msgs::msg::JointMitCmd_<ContainerAllocator> *;
  using SharedPtr =
    std::shared_ptr<rebotarm_msgs::msg::JointMitCmd_<ContainerAllocator>>;
  using ConstSharedPtr =
    std::shared_ptr<rebotarm_msgs::msg::JointMitCmd_<ContainerAllocator> const>;

  template<typename Deleter = std::default_delete<
      rebotarm_msgs::msg::JointMitCmd_<ContainerAllocator>>>
  using UniquePtrWithDeleter =
    std::unique_ptr<rebotarm_msgs::msg::JointMitCmd_<ContainerAllocator>, Deleter>;

  using UniquePtr = UniquePtrWithDeleter<>;

  template<typename Deleter = std::default_delete<
      rebotarm_msgs::msg::JointMitCmd_<ContainerAllocator>>>
  using ConstUniquePtrWithDeleter =
    std::unique_ptr<rebotarm_msgs::msg::JointMitCmd_<ContainerAllocator> const, Deleter>;
  using ConstUniquePtr = ConstUniquePtrWithDeleter<>;

  using WeakPtr =
    std::weak_ptr<rebotarm_msgs::msg::JointMitCmd_<ContainerAllocator>>;
  using ConstWeakPtr =
    std::weak_ptr<rebotarm_msgs::msg::JointMitCmd_<ContainerAllocator> const>;

  // pointer types similar to ROS 1, use SharedPtr / ConstSharedPtr instead
  // NOTE: Can't use 'using' here because GNU C++ can't parse attributes properly
  typedef DEPRECATED__rebotarm_msgs__msg__JointMitCmd
    std::shared_ptr<rebotarm_msgs::msg::JointMitCmd_<ContainerAllocator>>
    Ptr;
  typedef DEPRECATED__rebotarm_msgs__msg__JointMitCmd
    std::shared_ptr<rebotarm_msgs::msg::JointMitCmd_<ContainerAllocator> const>
    ConstPtr;

  // comparison operators
  bool operator==(const JointMitCmd_ & other) const
  {
    if (this->pos != other.pos) {
      return false;
    }
    if (this->vel != other.vel) {
      return false;
    }
    if (this->kp != other.kp) {
      return false;
    }
    if (this->kd != other.kd) {
      return false;
    }
    if (this->tau != other.tau) {
      return false;
    }
    if (this->stamp != other.stamp) {
      return false;
    }
    return true;
  }
  bool operator!=(const JointMitCmd_ & other) const
  {
    return !this->operator==(other);
  }
};  // struct JointMitCmd_

// alias to use template instance with default allocator
using JointMitCmd =
  rebotarm_msgs::msg::JointMitCmd_<std::allocator<void>>;

// constant definitions

}  // namespace msg

}  // namespace rebotarm_msgs

#endif  // REBOTARM_MSGS__MSG__DETAIL__JOINT_MIT_CMD__STRUCT_HPP_
