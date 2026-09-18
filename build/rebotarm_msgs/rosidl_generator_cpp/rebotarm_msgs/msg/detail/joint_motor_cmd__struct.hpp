// generated from rosidl_generator_cpp/resource/idl__struct.hpp.em
// with input from rebotarm_msgs:msg/JointMotorCmd.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "rebotarm_msgs/msg/joint_motor_cmd.hpp"


#ifndef REBOTARM_MSGS__MSG__DETAIL__JOINT_MOTOR_CMD__STRUCT_HPP_
#define REBOTARM_MSGS__MSG__DETAIL__JOINT_MOTOR_CMD__STRUCT_HPP_

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
# define DEPRECATED__rebotarm_msgs__msg__JointMotorCmd __attribute__((deprecated))
#else
# define DEPRECATED__rebotarm_msgs__msg__JointMotorCmd __declspec(deprecated)
#endif

namespace rebotarm_msgs
{

namespace msg
{

// message struct
template<class ContainerAllocator>
struct JointMotorCmd_
{
  using Type = JointMotorCmd_<ContainerAllocator>;

  explicit JointMotorCmd_(rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  : stamp(_init)
  {
    if (rosidl_runtime_cpp::MessageInitialization::ALL == _init ||
      rosidl_runtime_cpp::MessageInitialization::ZERO == _init)
    {
      this->mode = 0;
      this->use_pos = false;
      this->use_vel = false;
      this->use_kp = false;
      this->use_kd = false;
      this->use_tau = false;
      this->use_vlim = false;
      this->pos = 0.0;
      this->vel = 0.0;
      this->kp = 0.0;
      this->kd = 0.0;
      this->tau = 0.0;
      this->vlim = 0.0;
    }
  }

  explicit JointMotorCmd_(const ContainerAllocator & _alloc, rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  : stamp(_alloc, _init)
  {
    if (rosidl_runtime_cpp::MessageInitialization::ALL == _init ||
      rosidl_runtime_cpp::MessageInitialization::ZERO == _init)
    {
      this->mode = 0;
      this->use_pos = false;
      this->use_vel = false;
      this->use_kp = false;
      this->use_kd = false;
      this->use_tau = false;
      this->use_vlim = false;
      this->pos = 0.0;
      this->vel = 0.0;
      this->kp = 0.0;
      this->kd = 0.0;
      this->tau = 0.0;
      this->vlim = 0.0;
    }
  }

  // field types and members
  using _mode_type =
    uint8_t;
  _mode_type mode;
  using _use_pos_type =
    bool;
  _use_pos_type use_pos;
  using _use_vel_type =
    bool;
  _use_vel_type use_vel;
  using _use_kp_type =
    bool;
  _use_kp_type use_kp;
  using _use_kd_type =
    bool;
  _use_kd_type use_kd;
  using _use_tau_type =
    bool;
  _use_tau_type use_tau;
  using _use_vlim_type =
    bool;
  _use_vlim_type use_vlim;
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
  using _vlim_type =
    double;
  _vlim_type vlim;
  using _stamp_type =
    builtin_interfaces::msg::Time_<ContainerAllocator>;
  _stamp_type stamp;

  // setters for named parameter idiom
  Type & set__mode(
    const uint8_t & _arg)
  {
    this->mode = _arg;
    return *this;
  }
  Type & set__use_pos(
    const bool & _arg)
  {
    this->use_pos = _arg;
    return *this;
  }
  Type & set__use_vel(
    const bool & _arg)
  {
    this->use_vel = _arg;
    return *this;
  }
  Type & set__use_kp(
    const bool & _arg)
  {
    this->use_kp = _arg;
    return *this;
  }
  Type & set__use_kd(
    const bool & _arg)
  {
    this->use_kd = _arg;
    return *this;
  }
  Type & set__use_tau(
    const bool & _arg)
  {
    this->use_tau = _arg;
    return *this;
  }
  Type & set__use_vlim(
    const bool & _arg)
  {
    this->use_vlim = _arg;
    return *this;
  }
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
  static constexpr uint8_t MODE_MIT =
    0u;
  static constexpr uint8_t MODE_POS_VEL =
    1u;
  static constexpr uint8_t MODE_VEL =
    2u;

  // pointer types
  using RawPtr =
    rebotarm_msgs::msg::JointMotorCmd_<ContainerAllocator> *;
  using ConstRawPtr =
    const rebotarm_msgs::msg::JointMotorCmd_<ContainerAllocator> *;
  using SharedPtr =
    std::shared_ptr<rebotarm_msgs::msg::JointMotorCmd_<ContainerAllocator>>;
  using ConstSharedPtr =
    std::shared_ptr<rebotarm_msgs::msg::JointMotorCmd_<ContainerAllocator> const>;

  template<typename Deleter = std::default_delete<
      rebotarm_msgs::msg::JointMotorCmd_<ContainerAllocator>>>
  using UniquePtrWithDeleter =
    std::unique_ptr<rebotarm_msgs::msg::JointMotorCmd_<ContainerAllocator>, Deleter>;

  using UniquePtr = UniquePtrWithDeleter<>;

  template<typename Deleter = std::default_delete<
      rebotarm_msgs::msg::JointMotorCmd_<ContainerAllocator>>>
  using ConstUniquePtrWithDeleter =
    std::unique_ptr<rebotarm_msgs::msg::JointMotorCmd_<ContainerAllocator> const, Deleter>;
  using ConstUniquePtr = ConstUniquePtrWithDeleter<>;

  using WeakPtr =
    std::weak_ptr<rebotarm_msgs::msg::JointMotorCmd_<ContainerAllocator>>;
  using ConstWeakPtr =
    std::weak_ptr<rebotarm_msgs::msg::JointMotorCmd_<ContainerAllocator> const>;

  // pointer types similar to ROS 1, use SharedPtr / ConstSharedPtr instead
  // NOTE: Can't use 'using' here because GNU C++ can't parse attributes properly
  typedef DEPRECATED__rebotarm_msgs__msg__JointMotorCmd
    std::shared_ptr<rebotarm_msgs::msg::JointMotorCmd_<ContainerAllocator>>
    Ptr;
  typedef DEPRECATED__rebotarm_msgs__msg__JointMotorCmd
    std::shared_ptr<rebotarm_msgs::msg::JointMotorCmd_<ContainerAllocator> const>
    ConstPtr;

  // comparison operators
  bool operator==(const JointMotorCmd_ & other) const
  {
    if (this->mode != other.mode) {
      return false;
    }
    if (this->use_pos != other.use_pos) {
      return false;
    }
    if (this->use_vel != other.use_vel) {
      return false;
    }
    if (this->use_kp != other.use_kp) {
      return false;
    }
    if (this->use_kd != other.use_kd) {
      return false;
    }
    if (this->use_tau != other.use_tau) {
      return false;
    }
    if (this->use_vlim != other.use_vlim) {
      return false;
    }
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
    if (this->vlim != other.vlim) {
      return false;
    }
    if (this->stamp != other.stamp) {
      return false;
    }
    return true;
  }
  bool operator!=(const JointMotorCmd_ & other) const
  {
    return !this->operator==(other);
  }
};  // struct JointMotorCmd_

// alias to use template instance with default allocator
using JointMotorCmd =
  rebotarm_msgs::msg::JointMotorCmd_<std::allocator<void>>;

// constant definitions
#if __cplusplus < 201703L
// static constexpr member variable definitions are only needed in C++14 and below, deprecated in C++17
template<typename ContainerAllocator>
constexpr uint8_t JointMotorCmd_<ContainerAllocator>::MODE_MIT;
#endif  // __cplusplus < 201703L
#if __cplusplus < 201703L
// static constexpr member variable definitions are only needed in C++14 and below, deprecated in C++17
template<typename ContainerAllocator>
constexpr uint8_t JointMotorCmd_<ContainerAllocator>::MODE_POS_VEL;
#endif  // __cplusplus < 201703L
#if __cplusplus < 201703L
// static constexpr member variable definitions are only needed in C++14 and below, deprecated in C++17
template<typename ContainerAllocator>
constexpr uint8_t JointMotorCmd_<ContainerAllocator>::MODE_VEL;
#endif  // __cplusplus < 201703L

}  // namespace msg

}  // namespace rebotarm_msgs

#endif  // REBOTARM_MSGS__MSG__DETAIL__JOINT_MOTOR_CMD__STRUCT_HPP_
