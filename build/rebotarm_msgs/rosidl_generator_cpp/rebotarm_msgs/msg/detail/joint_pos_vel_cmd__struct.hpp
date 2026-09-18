// generated from rosidl_generator_cpp/resource/idl__struct.hpp.em
// with input from rebotarm_msgs:msg/JointPosVelCmd.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "rebotarm_msgs/msg/joint_pos_vel_cmd.hpp"


#ifndef REBOTARM_MSGS__MSG__DETAIL__JOINT_POS_VEL_CMD__STRUCT_HPP_
#define REBOTARM_MSGS__MSG__DETAIL__JOINT_POS_VEL_CMD__STRUCT_HPP_

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
# define DEPRECATED__rebotarm_msgs__msg__JointPosVelCmd __attribute__((deprecated))
#else
# define DEPRECATED__rebotarm_msgs__msg__JointPosVelCmd __declspec(deprecated)
#endif

namespace rebotarm_msgs
{

namespace msg
{

// message struct
template<class ContainerAllocator>
struct JointPosVelCmd_
{
  using Type = JointPosVelCmd_<ContainerAllocator>;

  explicit JointPosVelCmd_(rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  : stamp(_init)
  {
    if (rosidl_runtime_cpp::MessageInitialization::ALL == _init ||
      rosidl_runtime_cpp::MessageInitialization::ZERO == _init)
    {
      this->pos = 0.0;
      this->vlim = 0.0;
    }
  }

  explicit JointPosVelCmd_(const ContainerAllocator & _alloc, rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  : stamp(_alloc, _init)
  {
    if (rosidl_runtime_cpp::MessageInitialization::ALL == _init ||
      rosidl_runtime_cpp::MessageInitialization::ZERO == _init)
    {
      this->pos = 0.0;
      this->vlim = 0.0;
    }
  }

  // field types and members
  using _pos_type =
    double;
  _pos_type pos;
  using _vlim_type =
    double;
  _vlim_type vlim;
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
  Type & set__vlim(
    const double & _arg)
  {
    this->vlim = _arg;
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
    rebotarm_msgs::msg::JointPosVelCmd_<ContainerAllocator> *;
  using ConstRawPtr =
    const rebotarm_msgs::msg::JointPosVelCmd_<ContainerAllocator> *;
  using SharedPtr =
    std::shared_ptr<rebotarm_msgs::msg::JointPosVelCmd_<ContainerAllocator>>;
  using ConstSharedPtr =
    std::shared_ptr<rebotarm_msgs::msg::JointPosVelCmd_<ContainerAllocator> const>;

  template<typename Deleter = std::default_delete<
      rebotarm_msgs::msg::JointPosVelCmd_<ContainerAllocator>>>
  using UniquePtrWithDeleter =
    std::unique_ptr<rebotarm_msgs::msg::JointPosVelCmd_<ContainerAllocator>, Deleter>;

  using UniquePtr = UniquePtrWithDeleter<>;

  template<typename Deleter = std::default_delete<
      rebotarm_msgs::msg::JointPosVelCmd_<ContainerAllocator>>>
  using ConstUniquePtrWithDeleter =
    std::unique_ptr<rebotarm_msgs::msg::JointPosVelCmd_<ContainerAllocator> const, Deleter>;
  using ConstUniquePtr = ConstUniquePtrWithDeleter<>;

  using WeakPtr =
    std::weak_ptr<rebotarm_msgs::msg::JointPosVelCmd_<ContainerAllocator>>;
  using ConstWeakPtr =
    std::weak_ptr<rebotarm_msgs::msg::JointPosVelCmd_<ContainerAllocator> const>;

  // pointer types similar to ROS 1, use SharedPtr / ConstSharedPtr instead
  // NOTE: Can't use 'using' here because GNU C++ can't parse attributes properly
  typedef DEPRECATED__rebotarm_msgs__msg__JointPosVelCmd
    std::shared_ptr<rebotarm_msgs::msg::JointPosVelCmd_<ContainerAllocator>>
    Ptr;
  typedef DEPRECATED__rebotarm_msgs__msg__JointPosVelCmd
    std::shared_ptr<rebotarm_msgs::msg::JointPosVelCmd_<ContainerAllocator> const>
    ConstPtr;

  // comparison operators
  bool operator==(const JointPosVelCmd_ & other) const
  {
    if (this->pos != other.pos) {
      return false;
    }
    if (this->vlim != other.vlim) {
      return false;
    }
    if (this->stamp != other.stamp) {
      return false;
    }
    return true;
  }
  bool operator!=(const JointPosVelCmd_ & other) const
  {
    return !this->operator==(other);
  }
};  // struct JointPosVelCmd_

// alias to use template instance with default allocator
using JointPosVelCmd =
  rebotarm_msgs::msg::JointPosVelCmd_<std::allocator<void>>;

// constant definitions

}  // namespace msg

}  // namespace rebotarm_msgs

#endif  // REBOTARM_MSGS__MSG__DETAIL__JOINT_POS_VEL_CMD__STRUCT_HPP_
