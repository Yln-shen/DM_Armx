#[cfg(feature = "serde")]
use serde::{Deserialize, Serialize};


#[link(name = "rebotarm_msgs__rosidl_typesupport_c")]
extern "C" {
    fn rosidl_typesupport_c__get_message_type_support_handle__rebotarm_msgs__msg__JointMotorCmd() -> *const std::ffi::c_void;
}

#[link(name = "rebotarm_msgs__rosidl_generator_c")]
extern "C" {
    fn rebotarm_msgs__msg__JointMotorCmd__init(msg: *mut JointMotorCmd) -> bool;
    fn rebotarm_msgs__msg__JointMotorCmd__Sequence__init(seq: *mut rosidl_runtime_rs::Sequence<JointMotorCmd>, size: usize) -> bool;
    fn rebotarm_msgs__msg__JointMotorCmd__Sequence__fini(seq: *mut rosidl_runtime_rs::Sequence<JointMotorCmd>);
    fn rebotarm_msgs__msg__JointMotorCmd__Sequence__copy(in_seq: &rosidl_runtime_rs::Sequence<JointMotorCmd>, out_seq: *mut rosidl_runtime_rs::Sequence<JointMotorCmd>) -> bool;
}

// Corresponds to rebotarm_msgs__msg__JointMotorCmd
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]


// This struct is not documented.
#[allow(missing_docs)]

#[repr(C)]
#[derive(Clone, Debug, PartialEq, PartialOrd)]
pub struct JointMotorCmd {

    // This member is not documented.
    #[allow(missing_docs)]
    pub mode: u8,


    // This member is not documented.
    #[allow(missing_docs)]
    pub use_pos: bool,


    // This member is not documented.
    #[allow(missing_docs)]
    pub use_vel: bool,


    // This member is not documented.
    #[allow(missing_docs)]
    pub use_kp: bool,


    // This member is not documented.
    #[allow(missing_docs)]
    pub use_kd: bool,


    // This member is not documented.
    #[allow(missing_docs)]
    pub use_tau: bool,


    // This member is not documented.
    #[allow(missing_docs)]
    pub use_vlim: bool,


    // This member is not documented.
    #[allow(missing_docs)]
    pub pos: f64,


    // This member is not documented.
    #[allow(missing_docs)]
    pub vel: f64,


    // This member is not documented.
    #[allow(missing_docs)]
    pub kp: f64,


    // This member is not documented.
    #[allow(missing_docs)]
    pub kd: f64,


    // This member is not documented.
    #[allow(missing_docs)]
    pub tau: f64,


    // This member is not documented.
    #[allow(missing_docs)]
    pub vlim: f64,


    // This member is not documented.
    #[allow(missing_docs)]
    pub stamp: builtin_interfaces::msg::rmw::Time,

}

impl JointMotorCmd {

    // This constant is not documented.
    #[allow(missing_docs)]
    pub const MODE_MIT: u8 = 0;


    // This constant is not documented.
    #[allow(missing_docs)]
    pub const MODE_POS_VEL: u8 = 1;


    // This constant is not documented.
    #[allow(missing_docs)]
    pub const MODE_VEL: u8 = 2;

}


impl Default for JointMotorCmd {
  fn default() -> Self {
    unsafe {
      let mut msg = std::mem::zeroed();
      if !rebotarm_msgs__msg__JointMotorCmd__init(&mut msg as *mut _) {
        panic!("Call to rebotarm_msgs__msg__JointMotorCmd__init() failed");
      }
      msg
    }
  }
}

impl rosidl_runtime_rs::SequenceAlloc for JointMotorCmd {
  fn sequence_init(seq: &mut rosidl_runtime_rs::Sequence<Self>, size: usize) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { rebotarm_msgs__msg__JointMotorCmd__Sequence__init(seq as *mut _, size) }
  }
  fn sequence_fini(seq: &mut rosidl_runtime_rs::Sequence<Self>) {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { rebotarm_msgs__msg__JointMotorCmd__Sequence__fini(seq as *mut _) }
  }
  fn sequence_copy(in_seq: &rosidl_runtime_rs::Sequence<Self>, out_seq: &mut rosidl_runtime_rs::Sequence<Self>) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { rebotarm_msgs__msg__JointMotorCmd__Sequence__copy(in_seq, out_seq as *mut _) }
  }
}

impl rosidl_runtime_rs::Message for JointMotorCmd {
  type RmwMsg = Self;
  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> { msg_cow }
  fn from_rmw_message(msg: Self::RmwMsg) -> Self { msg }
}

impl rosidl_runtime_rs::RmwMessage for JointMotorCmd where Self: Sized {
  const TYPE_NAME: &'static str = "rebotarm_msgs/msg/JointMotorCmd";
  fn get_type_support() -> *const std::ffi::c_void {
    // SAFETY: No preconditions for this function.
    unsafe { rosidl_typesupport_c__get_message_type_support_handle__rebotarm_msgs__msg__JointMotorCmd() }
  }
}


#[link(name = "rebotarm_msgs__rosidl_typesupport_c")]
extern "C" {
    fn rosidl_typesupport_c__get_message_type_support_handle__rebotarm_msgs__msg__JointMitCmd() -> *const std::ffi::c_void;
}

#[link(name = "rebotarm_msgs__rosidl_generator_c")]
extern "C" {
    fn rebotarm_msgs__msg__JointMitCmd__init(msg: *mut JointMitCmd) -> bool;
    fn rebotarm_msgs__msg__JointMitCmd__Sequence__init(seq: *mut rosidl_runtime_rs::Sequence<JointMitCmd>, size: usize) -> bool;
    fn rebotarm_msgs__msg__JointMitCmd__Sequence__fini(seq: *mut rosidl_runtime_rs::Sequence<JointMitCmd>);
    fn rebotarm_msgs__msg__JointMitCmd__Sequence__copy(in_seq: &rosidl_runtime_rs::Sequence<JointMitCmd>, out_seq: *mut rosidl_runtime_rs::Sequence<JointMitCmd>) -> bool;
}

// Corresponds to rebotarm_msgs__msg__JointMitCmd
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]


// This struct is not documented.
#[allow(missing_docs)]

#[repr(C)]
#[derive(Clone, Debug, PartialEq, PartialOrd)]
pub struct JointMitCmd {

    // This member is not documented.
    #[allow(missing_docs)]
    pub pos: f64,


    // This member is not documented.
    #[allow(missing_docs)]
    pub vel: f64,


    // This member is not documented.
    #[allow(missing_docs)]
    pub kp: f64,


    // This member is not documented.
    #[allow(missing_docs)]
    pub kd: f64,


    // This member is not documented.
    #[allow(missing_docs)]
    pub tau: f64,


    // This member is not documented.
    #[allow(missing_docs)]
    pub stamp: builtin_interfaces::msg::rmw::Time,

}



impl Default for JointMitCmd {
  fn default() -> Self {
    unsafe {
      let mut msg = std::mem::zeroed();
      if !rebotarm_msgs__msg__JointMitCmd__init(&mut msg as *mut _) {
        panic!("Call to rebotarm_msgs__msg__JointMitCmd__init() failed");
      }
      msg
    }
  }
}

impl rosidl_runtime_rs::SequenceAlloc for JointMitCmd {
  fn sequence_init(seq: &mut rosidl_runtime_rs::Sequence<Self>, size: usize) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { rebotarm_msgs__msg__JointMitCmd__Sequence__init(seq as *mut _, size) }
  }
  fn sequence_fini(seq: &mut rosidl_runtime_rs::Sequence<Self>) {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { rebotarm_msgs__msg__JointMitCmd__Sequence__fini(seq as *mut _) }
  }
  fn sequence_copy(in_seq: &rosidl_runtime_rs::Sequence<Self>, out_seq: &mut rosidl_runtime_rs::Sequence<Self>) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { rebotarm_msgs__msg__JointMitCmd__Sequence__copy(in_seq, out_seq as *mut _) }
  }
}

impl rosidl_runtime_rs::Message for JointMitCmd {
  type RmwMsg = Self;
  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> { msg_cow }
  fn from_rmw_message(msg: Self::RmwMsg) -> Self { msg }
}

impl rosidl_runtime_rs::RmwMessage for JointMitCmd where Self: Sized {
  const TYPE_NAME: &'static str = "rebotarm_msgs/msg/JointMitCmd";
  fn get_type_support() -> *const std::ffi::c_void {
    // SAFETY: No preconditions for this function.
    unsafe { rosidl_typesupport_c__get_message_type_support_handle__rebotarm_msgs__msg__JointMitCmd() }
  }
}


#[link(name = "rebotarm_msgs__rosidl_typesupport_c")]
extern "C" {
    fn rosidl_typesupport_c__get_message_type_support_handle__rebotarm_msgs__msg__JointPosVelCmd() -> *const std::ffi::c_void;
}

#[link(name = "rebotarm_msgs__rosidl_generator_c")]
extern "C" {
    fn rebotarm_msgs__msg__JointPosVelCmd__init(msg: *mut JointPosVelCmd) -> bool;
    fn rebotarm_msgs__msg__JointPosVelCmd__Sequence__init(seq: *mut rosidl_runtime_rs::Sequence<JointPosVelCmd>, size: usize) -> bool;
    fn rebotarm_msgs__msg__JointPosVelCmd__Sequence__fini(seq: *mut rosidl_runtime_rs::Sequence<JointPosVelCmd>);
    fn rebotarm_msgs__msg__JointPosVelCmd__Sequence__copy(in_seq: &rosidl_runtime_rs::Sequence<JointPosVelCmd>, out_seq: *mut rosidl_runtime_rs::Sequence<JointPosVelCmd>) -> bool;
}

// Corresponds to rebotarm_msgs__msg__JointPosVelCmd
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]


// This struct is not documented.
#[allow(missing_docs)]

#[repr(C)]
#[derive(Clone, Debug, PartialEq, PartialOrd)]
pub struct JointPosVelCmd {

    // This member is not documented.
    #[allow(missing_docs)]
    pub pos: f64,


    // This member is not documented.
    #[allow(missing_docs)]
    pub vlim: f64,


    // This member is not documented.
    #[allow(missing_docs)]
    pub stamp: builtin_interfaces::msg::rmw::Time,

}



impl Default for JointPosVelCmd {
  fn default() -> Self {
    unsafe {
      let mut msg = std::mem::zeroed();
      if !rebotarm_msgs__msg__JointPosVelCmd__init(&mut msg as *mut _) {
        panic!("Call to rebotarm_msgs__msg__JointPosVelCmd__init() failed");
      }
      msg
    }
  }
}

impl rosidl_runtime_rs::SequenceAlloc for JointPosVelCmd {
  fn sequence_init(seq: &mut rosidl_runtime_rs::Sequence<Self>, size: usize) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { rebotarm_msgs__msg__JointPosVelCmd__Sequence__init(seq as *mut _, size) }
  }
  fn sequence_fini(seq: &mut rosidl_runtime_rs::Sequence<Self>) {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { rebotarm_msgs__msg__JointPosVelCmd__Sequence__fini(seq as *mut _) }
  }
  fn sequence_copy(in_seq: &rosidl_runtime_rs::Sequence<Self>, out_seq: &mut rosidl_runtime_rs::Sequence<Self>) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { rebotarm_msgs__msg__JointPosVelCmd__Sequence__copy(in_seq, out_seq as *mut _) }
  }
}

impl rosidl_runtime_rs::Message for JointPosVelCmd {
  type RmwMsg = Self;
  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> { msg_cow }
  fn from_rmw_message(msg: Self::RmwMsg) -> Self { msg }
}

impl rosidl_runtime_rs::RmwMessage for JointPosVelCmd where Self: Sized {
  const TYPE_NAME: &'static str = "rebotarm_msgs/msg/JointPosVelCmd";
  fn get_type_support() -> *const std::ffi::c_void {
    // SAFETY: No preconditions for this function.
    unsafe { rosidl_typesupport_c__get_message_type_support_handle__rebotarm_msgs__msg__JointPosVelCmd() }
  }
}


#[link(name = "rebotarm_msgs__rosidl_typesupport_c")]
extern "C" {
    fn rosidl_typesupport_c__get_message_type_support_handle__rebotarm_msgs__msg__JointMotorState() -> *const std::ffi::c_void;
}

#[link(name = "rebotarm_msgs__rosidl_generator_c")]
extern "C" {
    fn rebotarm_msgs__msg__JointMotorState__init(msg: *mut JointMotorState) -> bool;
    fn rebotarm_msgs__msg__JointMotorState__Sequence__init(seq: *mut rosidl_runtime_rs::Sequence<JointMotorState>, size: usize) -> bool;
    fn rebotarm_msgs__msg__JointMotorState__Sequence__fini(seq: *mut rosidl_runtime_rs::Sequence<JointMotorState>);
    fn rebotarm_msgs__msg__JointMotorState__Sequence__copy(in_seq: &rosidl_runtime_rs::Sequence<JointMotorState>, out_seq: *mut rosidl_runtime_rs::Sequence<JointMotorState>) -> bool;
}

// Corresponds to rebotarm_msgs__msg__JointMotorState
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]


// This struct is not documented.
#[allow(missing_docs)]

#[repr(C)]
#[derive(Clone, Debug, PartialEq, PartialOrd)]
pub struct JointMotorState {

    // This member is not documented.
    #[allow(missing_docs)]
    pub header: std_msgs::msg::rmw::Header,


    // This member is not documented.
    #[allow(missing_docs)]
    pub joint_name: rosidl_runtime_rs::String,


    // This member is not documented.
    #[allow(missing_docs)]
    pub position: f64,


    // This member is not documented.
    #[allow(missing_docs)]
    pub velocity: f64,


    // This member is not documented.
    #[allow(missing_docs)]
    pub torque: f64,


    // This member is not documented.
    #[allow(missing_docs)]
    pub status_code: u8,

}



impl Default for JointMotorState {
  fn default() -> Self {
    unsafe {
      let mut msg = std::mem::zeroed();
      if !rebotarm_msgs__msg__JointMotorState__init(&mut msg as *mut _) {
        panic!("Call to rebotarm_msgs__msg__JointMotorState__init() failed");
      }
      msg
    }
  }
}

impl rosidl_runtime_rs::SequenceAlloc for JointMotorState {
  fn sequence_init(seq: &mut rosidl_runtime_rs::Sequence<Self>, size: usize) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { rebotarm_msgs__msg__JointMotorState__Sequence__init(seq as *mut _, size) }
  }
  fn sequence_fini(seq: &mut rosidl_runtime_rs::Sequence<Self>) {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { rebotarm_msgs__msg__JointMotorState__Sequence__fini(seq as *mut _) }
  }
  fn sequence_copy(in_seq: &rosidl_runtime_rs::Sequence<Self>, out_seq: &mut rosidl_runtime_rs::Sequence<Self>) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { rebotarm_msgs__msg__JointMotorState__Sequence__copy(in_seq, out_seq as *mut _) }
  }
}

impl rosidl_runtime_rs::Message for JointMotorState {
  type RmwMsg = Self;
  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> { msg_cow }
  fn from_rmw_message(msg: Self::RmwMsg) -> Self { msg }
}

impl rosidl_runtime_rs::RmwMessage for JointMotorState where Self: Sized {
  const TYPE_NAME: &'static str = "rebotarm_msgs/msg/JointMotorState";
  fn get_type_support() -> *const std::ffi::c_void {
    // SAFETY: No preconditions for this function.
    unsafe { rosidl_typesupport_c__get_message_type_support_handle__rebotarm_msgs__msg__JointMotorState() }
  }
}


#[link(name = "rebotarm_msgs__rosidl_typesupport_c")]
extern "C" {
    fn rosidl_typesupport_c__get_message_type_support_handle__rebotarm_msgs__msg__ArmStatus() -> *const std::ffi::c_void;
}

#[link(name = "rebotarm_msgs__rosidl_generator_c")]
extern "C" {
    fn rebotarm_msgs__msg__ArmStatus__init(msg: *mut ArmStatus) -> bool;
    fn rebotarm_msgs__msg__ArmStatus__Sequence__init(seq: *mut rosidl_runtime_rs::Sequence<ArmStatus>, size: usize) -> bool;
    fn rebotarm_msgs__msg__ArmStatus__Sequence__fini(seq: *mut rosidl_runtime_rs::Sequence<ArmStatus>);
    fn rebotarm_msgs__msg__ArmStatus__Sequence__copy(in_seq: &rosidl_runtime_rs::Sequence<ArmStatus>, out_seq: *mut rosidl_runtime_rs::Sequence<ArmStatus>) -> bool;
}

// Corresponds to rebotarm_msgs__msg__ArmStatus
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]


// This struct is not documented.
#[allow(missing_docs)]

#[repr(C)]
#[derive(Clone, Debug, PartialEq, PartialOrd)]
pub struct ArmStatus {

    // This member is not documented.
    #[allow(missing_docs)]
    pub header: std_msgs::msg::rmw::Header,


    // This member is not documented.
    #[allow(missing_docs)]
    pub mode: rosidl_runtime_rs::String,


    // This member is not documented.
    #[allow(missing_docs)]
    pub enabled: bool,


    // This member is not documented.
    #[allow(missing_docs)]
    pub control_loop_active: bool,


    // This member is not documented.
    #[allow(missing_docs)]
    pub state_machine: rosidl_runtime_rs::String,


    // This member is not documented.
    #[allow(missing_docs)]
    pub joint_names: rosidl_runtime_rs::Sequence<rosidl_runtime_rs::String>,


    // This member is not documented.
    #[allow(missing_docs)]
    pub per_joint_status_code: rosidl_runtime_rs::Sequence<u8>,


    // This member is not documented.
    #[allow(missing_docs)]
    pub error_codes: rosidl_runtime_rs::Sequence<rosidl_runtime_rs::String>,

}



impl Default for ArmStatus {
  fn default() -> Self {
    unsafe {
      let mut msg = std::mem::zeroed();
      if !rebotarm_msgs__msg__ArmStatus__init(&mut msg as *mut _) {
        panic!("Call to rebotarm_msgs__msg__ArmStatus__init() failed");
      }
      msg
    }
  }
}

impl rosidl_runtime_rs::SequenceAlloc for ArmStatus {
  fn sequence_init(seq: &mut rosidl_runtime_rs::Sequence<Self>, size: usize) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { rebotarm_msgs__msg__ArmStatus__Sequence__init(seq as *mut _, size) }
  }
  fn sequence_fini(seq: &mut rosidl_runtime_rs::Sequence<Self>) {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { rebotarm_msgs__msg__ArmStatus__Sequence__fini(seq as *mut _) }
  }
  fn sequence_copy(in_seq: &rosidl_runtime_rs::Sequence<Self>, out_seq: &mut rosidl_runtime_rs::Sequence<Self>) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { rebotarm_msgs__msg__ArmStatus__Sequence__copy(in_seq, out_seq as *mut _) }
  }
}

impl rosidl_runtime_rs::Message for ArmStatus {
  type RmwMsg = Self;
  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> { msg_cow }
  fn from_rmw_message(msg: Self::RmwMsg) -> Self { msg }
}

impl rosidl_runtime_rs::RmwMessage for ArmStatus where Self: Sized {
  const TYPE_NAME: &'static str = "rebotarm_msgs/msg/ArmStatus";
  fn get_type_support() -> *const std::ffi::c_void {
    // SAFETY: No preconditions for this function.
    unsafe { rosidl_typesupport_c__get_message_type_support_handle__rebotarm_msgs__msg__ArmStatus() }
  }
}


