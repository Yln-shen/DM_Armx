// generated from rosidl_generator_cpp/resource/idl__struct.hpp.em
// with input from rebotarm_msgs:msg/ArmStatus.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "rebotarm_msgs/msg/arm_status.hpp"


#ifndef REBOTARM_MSGS__MSG__DETAIL__ARM_STATUS__STRUCT_HPP_
#define REBOTARM_MSGS__MSG__DETAIL__ARM_STATUS__STRUCT_HPP_

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
# define DEPRECATED__rebotarm_msgs__msg__ArmStatus __attribute__((deprecated))
#else
# define DEPRECATED__rebotarm_msgs__msg__ArmStatus __declspec(deprecated)
#endif

namespace rebotarm_msgs
{

namespace msg
{

// message struct
template<class ContainerAllocator>
struct ArmStatus_
{
  using Type = ArmStatus_<ContainerAllocator>;

  explicit ArmStatus_(rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  : header(_init)
  {
    if (rosidl_runtime_cpp::MessageInitialization::ALL == _init ||
      rosidl_runtime_cpp::MessageInitialization::ZERO == _init)
    {
      this->mode = "";
      this->enabled = false;
      this->control_loop_active = false;
      this->state_machine = "";
    }
  }

  explicit ArmStatus_(const ContainerAllocator & _alloc, rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  : header(_alloc, _init),
    mode(_alloc),
    state_machine(_alloc)
  {
    if (rosidl_runtime_cpp::MessageInitialization::ALL == _init ||
      rosidl_runtime_cpp::MessageInitialization::ZERO == _init)
    {
      this->mode = "";
      this->enabled = false;
      this->control_loop_active = false;
      this->state_machine = "";
    }
  }

  // field types and members
  using _header_type =
    std_msgs::msg::Header_<ContainerAllocator>;
  _header_type header;
  using _mode_type =
    std::basic_string<char, std::char_traits<char>, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<char>>;
  _mode_type mode;
  using _enabled_type =
    bool;
  _enabled_type enabled;
  using _control_loop_active_type =
    bool;
  _control_loop_active_type control_loop_active;
  using _state_machine_type =
    std::basic_string<char, std::char_traits<char>, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<char>>;
  _state_machine_type state_machine;
  using _joint_names_type =
    std::vector<std::basic_string<char, std::char_traits<char>, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<char>>, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<std::basic_string<char, std::char_traits<char>, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<char>>>>;
  _joint_names_type joint_names;
  using _per_joint_status_code_type =
    std::vector<uint8_t, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<uint8_t>>;
  _per_joint_status_code_type per_joint_status_code;
  using _error_codes_type =
    std::vector<std::basic_string<char, std::char_traits<char>, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<char>>, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<std::basic_string<char, std::char_traits<char>, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<char>>>>;
  _error_codes_type error_codes;

  // setters for named parameter idiom
  Type & set__header(
    const std_msgs::msg::Header_<ContainerAllocator> & _arg)
  {
    this->header = _arg;
    return *this;
  }
  Type & set__mode(
    const std::basic_string<char, std::char_traits<char>, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<char>> & _arg)
  {
    this->mode = _arg;
    return *this;
  }
  Type & set__enabled(
    const bool & _arg)
  {
    this->enabled = _arg;
    return *this;
  }
  Type & set__control_loop_active(
    const bool & _arg)
  {
    this->control_loop_active = _arg;
    return *this;
  }
  Type & set__state_machine(
    const std::basic_string<char, std::char_traits<char>, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<char>> & _arg)
  {
    this->state_machine = _arg;
    return *this;
  }
  Type & set__joint_names(
    const std::vector<std::basic_string<char, std::char_traits<char>, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<char>>, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<std::basic_string<char, std::char_traits<char>, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<char>>>> & _arg)
  {
    this->joint_names = _arg;
    return *this;
  }
  Type & set__per_joint_status_code(
    const std::vector<uint8_t, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<uint8_t>> & _arg)
  {
    this->per_joint_status_code = _arg;
    return *this;
  }
  Type & set__error_codes(
    const std::vector<std::basic_string<char, std::char_traits<char>, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<char>>, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<std::basic_string<char, std::char_traits<char>, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<char>>>> & _arg)
  {
    this->error_codes = _arg;
    return *this;
  }

  // constant declarations

  // pointer types
  using RawPtr =
    rebotarm_msgs::msg::ArmStatus_<ContainerAllocator> *;
  using ConstRawPtr =
    const rebotarm_msgs::msg::ArmStatus_<ContainerAllocator> *;
  using SharedPtr =
    std::shared_ptr<rebotarm_msgs::msg::ArmStatus_<ContainerAllocator>>;
  using ConstSharedPtr =
    std::shared_ptr<rebotarm_msgs::msg::ArmStatus_<ContainerAllocator> const>;

  template<typename Deleter = std::default_delete<
      rebotarm_msgs::msg::ArmStatus_<ContainerAllocator>>>
  using UniquePtrWithDeleter =
    std::unique_ptr<rebotarm_msgs::msg::ArmStatus_<ContainerAllocator>, Deleter>;

  using UniquePtr = UniquePtrWithDeleter<>;

  template<typename Deleter = std::default_delete<
      rebotarm_msgs::msg::ArmStatus_<ContainerAllocator>>>
  using ConstUniquePtrWithDeleter =
    std::unique_ptr<rebotarm_msgs::msg::ArmStatus_<ContainerAllocator> const, Deleter>;
  using ConstUniquePtr = ConstUniquePtrWithDeleter<>;

  using WeakPtr =
    std::weak_ptr<rebotarm_msgs::msg::ArmStatus_<ContainerAllocator>>;
  using ConstWeakPtr =
    std::weak_ptr<rebotarm_msgs::msg::ArmStatus_<ContainerAllocator> const>;

  // pointer types similar to ROS 1, use SharedPtr / ConstSharedPtr instead
  // NOTE: Can't use 'using' here because GNU C++ can't parse attributes properly
  typedef DEPRECATED__rebotarm_msgs__msg__ArmStatus
    std::shared_ptr<rebotarm_msgs::msg::ArmStatus_<ContainerAllocator>>
    Ptr;
  typedef DEPRECATED__rebotarm_msgs__msg__ArmStatus
    std::shared_ptr<rebotarm_msgs::msg::ArmStatus_<ContainerAllocator> const>
    ConstPtr;

  // comparison operators
  bool operator==(const ArmStatus_ & other) const
  {
    if (this->header != other.header) {
      return false;
    }
    if (this->mode != other.mode) {
      return false;
    }
    if (this->enabled != other.enabled) {
      return false;
    }
    if (this->control_loop_active != other.control_loop_active) {
      return false;
    }
    if (this->state_machine != other.state_machine) {
      return false;
    }
    if (this->joint_names != other.joint_names) {
      return false;
    }
    if (this->per_joint_status_code != other.per_joint_status_code) {
      return false;
    }
    if (this->error_codes != other.error_codes) {
      return false;
    }
    return true;
  }
  bool operator!=(const ArmStatus_ & other) const
  {
    return !this->operator==(other);
  }
};  // struct ArmStatus_

// alias to use template instance with default allocator
using ArmStatus =
  rebotarm_msgs::msg::ArmStatus_<std::allocator<void>>;

// constant definitions

}  // namespace msg

}  // namespace rebotarm_msgs

#endif  // REBOTARM_MSGS__MSG__DETAIL__ARM_STATUS__STRUCT_HPP_
