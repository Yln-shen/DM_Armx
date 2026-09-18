#[cfg(feature = "serde")]
use serde::{Deserialize, Serialize};



#[link(name = "rebotarm_msgs__rosidl_typesupport_c")]
extern "C" {
    fn rosidl_typesupport_c__get_message_type_support_handle__rebotarm_msgs__srv__SetMode_Request() -> *const std::ffi::c_void;
}

#[link(name = "rebotarm_msgs__rosidl_generator_c")]
extern "C" {
    fn rebotarm_msgs__srv__SetMode_Request__init(msg: *mut SetMode_Request) -> bool;
    fn rebotarm_msgs__srv__SetMode_Request__Sequence__init(seq: *mut rosidl_runtime_rs::Sequence<SetMode_Request>, size: usize) -> bool;
    fn rebotarm_msgs__srv__SetMode_Request__Sequence__fini(seq: *mut rosidl_runtime_rs::Sequence<SetMode_Request>);
    fn rebotarm_msgs__srv__SetMode_Request__Sequence__copy(in_seq: &rosidl_runtime_rs::Sequence<SetMode_Request>, out_seq: *mut rosidl_runtime_rs::Sequence<SetMode_Request>) -> bool;
}

// Corresponds to rebotarm_msgs__srv__SetMode_Request
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]


// This struct is not documented.
#[allow(missing_docs)]

#[allow(non_camel_case_types)]
#[repr(C)]
#[derive(Clone, Debug, PartialEq, PartialOrd)]
pub struct SetMode_Request {

    // This member is not documented.
    #[allow(missing_docs)]
    pub mode: rosidl_runtime_rs::String,

}



impl Default for SetMode_Request {
  fn default() -> Self {
    unsafe {
      let mut msg = std::mem::zeroed();
      if !rebotarm_msgs__srv__SetMode_Request__init(&mut msg as *mut _) {
        panic!("Call to rebotarm_msgs__srv__SetMode_Request__init() failed");
      }
      msg
    }
  }
}

impl rosidl_runtime_rs::SequenceAlloc for SetMode_Request {
  fn sequence_init(seq: &mut rosidl_runtime_rs::Sequence<Self>, size: usize) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { rebotarm_msgs__srv__SetMode_Request__Sequence__init(seq as *mut _, size) }
  }
  fn sequence_fini(seq: &mut rosidl_runtime_rs::Sequence<Self>) {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { rebotarm_msgs__srv__SetMode_Request__Sequence__fini(seq as *mut _) }
  }
  fn sequence_copy(in_seq: &rosidl_runtime_rs::Sequence<Self>, out_seq: &mut rosidl_runtime_rs::Sequence<Self>) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { rebotarm_msgs__srv__SetMode_Request__Sequence__copy(in_seq, out_seq as *mut _) }
  }
}

impl rosidl_runtime_rs::Message for SetMode_Request {
  type RmwMsg = Self;
  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> { msg_cow }
  fn from_rmw_message(msg: Self::RmwMsg) -> Self { msg }
}

impl rosidl_runtime_rs::RmwMessage for SetMode_Request where Self: Sized {
  const TYPE_NAME: &'static str = "rebotarm_msgs/srv/SetMode_Request";
  fn get_type_support() -> *const std::ffi::c_void {
    // SAFETY: No preconditions for this function.
    unsafe { rosidl_typesupport_c__get_message_type_support_handle__rebotarm_msgs__srv__SetMode_Request() }
  }
}


#[link(name = "rebotarm_msgs__rosidl_typesupport_c")]
extern "C" {
    fn rosidl_typesupport_c__get_message_type_support_handle__rebotarm_msgs__srv__SetMode_Response() -> *const std::ffi::c_void;
}

#[link(name = "rebotarm_msgs__rosidl_generator_c")]
extern "C" {
    fn rebotarm_msgs__srv__SetMode_Response__init(msg: *mut SetMode_Response) -> bool;
    fn rebotarm_msgs__srv__SetMode_Response__Sequence__init(seq: *mut rosidl_runtime_rs::Sequence<SetMode_Response>, size: usize) -> bool;
    fn rebotarm_msgs__srv__SetMode_Response__Sequence__fini(seq: *mut rosidl_runtime_rs::Sequence<SetMode_Response>);
    fn rebotarm_msgs__srv__SetMode_Response__Sequence__copy(in_seq: &rosidl_runtime_rs::Sequence<SetMode_Response>, out_seq: *mut rosidl_runtime_rs::Sequence<SetMode_Response>) -> bool;
}

// Corresponds to rebotarm_msgs__srv__SetMode_Response
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]


// This struct is not documented.
#[allow(missing_docs)]

#[allow(non_camel_case_types)]
#[repr(C)]
#[derive(Clone, Debug, PartialEq, PartialOrd)]
pub struct SetMode_Response {

    // This member is not documented.
    #[allow(missing_docs)]
    pub success: bool,


    // This member is not documented.
    #[allow(missing_docs)]
    pub message: rosidl_runtime_rs::String,

}



impl Default for SetMode_Response {
  fn default() -> Self {
    unsafe {
      let mut msg = std::mem::zeroed();
      if !rebotarm_msgs__srv__SetMode_Response__init(&mut msg as *mut _) {
        panic!("Call to rebotarm_msgs__srv__SetMode_Response__init() failed");
      }
      msg
    }
  }
}

impl rosidl_runtime_rs::SequenceAlloc for SetMode_Response {
  fn sequence_init(seq: &mut rosidl_runtime_rs::Sequence<Self>, size: usize) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { rebotarm_msgs__srv__SetMode_Response__Sequence__init(seq as *mut _, size) }
  }
  fn sequence_fini(seq: &mut rosidl_runtime_rs::Sequence<Self>) {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { rebotarm_msgs__srv__SetMode_Response__Sequence__fini(seq as *mut _) }
  }
  fn sequence_copy(in_seq: &rosidl_runtime_rs::Sequence<Self>, out_seq: &mut rosidl_runtime_rs::Sequence<Self>) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { rebotarm_msgs__srv__SetMode_Response__Sequence__copy(in_seq, out_seq as *mut _) }
  }
}

impl rosidl_runtime_rs::Message for SetMode_Response {
  type RmwMsg = Self;
  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> { msg_cow }
  fn from_rmw_message(msg: Self::RmwMsg) -> Self { msg }
}

impl rosidl_runtime_rs::RmwMessage for SetMode_Response where Self: Sized {
  const TYPE_NAME: &'static str = "rebotarm_msgs/srv/SetMode_Response";
  fn get_type_support() -> *const std::ffi::c_void {
    // SAFETY: No preconditions for this function.
    unsafe { rosidl_typesupport_c__get_message_type_support_handle__rebotarm_msgs__srv__SetMode_Response() }
  }
}


#[link(name = "rebotarm_msgs__rosidl_typesupport_c")]
extern "C" {
    fn rosidl_typesupport_c__get_message_type_support_handle__rebotarm_msgs__srv__SetZero_Request() -> *const std::ffi::c_void;
}

#[link(name = "rebotarm_msgs__rosidl_generator_c")]
extern "C" {
    fn rebotarm_msgs__srv__SetZero_Request__init(msg: *mut SetZero_Request) -> bool;
    fn rebotarm_msgs__srv__SetZero_Request__Sequence__init(seq: *mut rosidl_runtime_rs::Sequence<SetZero_Request>, size: usize) -> bool;
    fn rebotarm_msgs__srv__SetZero_Request__Sequence__fini(seq: *mut rosidl_runtime_rs::Sequence<SetZero_Request>);
    fn rebotarm_msgs__srv__SetZero_Request__Sequence__copy(in_seq: &rosidl_runtime_rs::Sequence<SetZero_Request>, out_seq: *mut rosidl_runtime_rs::Sequence<SetZero_Request>) -> bool;
}

// Corresponds to rebotarm_msgs__srv__SetZero_Request
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]


// This struct is not documented.
#[allow(missing_docs)]

#[allow(non_camel_case_types)]
#[repr(C)]
#[derive(Clone, Debug, PartialEq, PartialOrd)]
pub struct SetZero_Request {

    // This member is not documented.
    #[allow(missing_docs)]
    pub joint_name: rosidl_runtime_rs::String,

}



impl Default for SetZero_Request {
  fn default() -> Self {
    unsafe {
      let mut msg = std::mem::zeroed();
      if !rebotarm_msgs__srv__SetZero_Request__init(&mut msg as *mut _) {
        panic!("Call to rebotarm_msgs__srv__SetZero_Request__init() failed");
      }
      msg
    }
  }
}

impl rosidl_runtime_rs::SequenceAlloc for SetZero_Request {
  fn sequence_init(seq: &mut rosidl_runtime_rs::Sequence<Self>, size: usize) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { rebotarm_msgs__srv__SetZero_Request__Sequence__init(seq as *mut _, size) }
  }
  fn sequence_fini(seq: &mut rosidl_runtime_rs::Sequence<Self>) {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { rebotarm_msgs__srv__SetZero_Request__Sequence__fini(seq as *mut _) }
  }
  fn sequence_copy(in_seq: &rosidl_runtime_rs::Sequence<Self>, out_seq: &mut rosidl_runtime_rs::Sequence<Self>) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { rebotarm_msgs__srv__SetZero_Request__Sequence__copy(in_seq, out_seq as *mut _) }
  }
}

impl rosidl_runtime_rs::Message for SetZero_Request {
  type RmwMsg = Self;
  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> { msg_cow }
  fn from_rmw_message(msg: Self::RmwMsg) -> Self { msg }
}

impl rosidl_runtime_rs::RmwMessage for SetZero_Request where Self: Sized {
  const TYPE_NAME: &'static str = "rebotarm_msgs/srv/SetZero_Request";
  fn get_type_support() -> *const std::ffi::c_void {
    // SAFETY: No preconditions for this function.
    unsafe { rosidl_typesupport_c__get_message_type_support_handle__rebotarm_msgs__srv__SetZero_Request() }
  }
}


#[link(name = "rebotarm_msgs__rosidl_typesupport_c")]
extern "C" {
    fn rosidl_typesupport_c__get_message_type_support_handle__rebotarm_msgs__srv__SetZero_Response() -> *const std::ffi::c_void;
}

#[link(name = "rebotarm_msgs__rosidl_generator_c")]
extern "C" {
    fn rebotarm_msgs__srv__SetZero_Response__init(msg: *mut SetZero_Response) -> bool;
    fn rebotarm_msgs__srv__SetZero_Response__Sequence__init(seq: *mut rosidl_runtime_rs::Sequence<SetZero_Response>, size: usize) -> bool;
    fn rebotarm_msgs__srv__SetZero_Response__Sequence__fini(seq: *mut rosidl_runtime_rs::Sequence<SetZero_Response>);
    fn rebotarm_msgs__srv__SetZero_Response__Sequence__copy(in_seq: &rosidl_runtime_rs::Sequence<SetZero_Response>, out_seq: *mut rosidl_runtime_rs::Sequence<SetZero_Response>) -> bool;
}

// Corresponds to rebotarm_msgs__srv__SetZero_Response
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]


// This struct is not documented.
#[allow(missing_docs)]

#[allow(non_camel_case_types)]
#[repr(C)]
#[derive(Clone, Debug, PartialEq, PartialOrd)]
pub struct SetZero_Response {

    // This member is not documented.
    #[allow(missing_docs)]
    pub success: bool,


    // This member is not documented.
    #[allow(missing_docs)]
    pub message: rosidl_runtime_rs::String,

}



impl Default for SetZero_Response {
  fn default() -> Self {
    unsafe {
      let mut msg = std::mem::zeroed();
      if !rebotarm_msgs__srv__SetZero_Response__init(&mut msg as *mut _) {
        panic!("Call to rebotarm_msgs__srv__SetZero_Response__init() failed");
      }
      msg
    }
  }
}

impl rosidl_runtime_rs::SequenceAlloc for SetZero_Response {
  fn sequence_init(seq: &mut rosidl_runtime_rs::Sequence<Self>, size: usize) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { rebotarm_msgs__srv__SetZero_Response__Sequence__init(seq as *mut _, size) }
  }
  fn sequence_fini(seq: &mut rosidl_runtime_rs::Sequence<Self>) {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { rebotarm_msgs__srv__SetZero_Response__Sequence__fini(seq as *mut _) }
  }
  fn sequence_copy(in_seq: &rosidl_runtime_rs::Sequence<Self>, out_seq: &mut rosidl_runtime_rs::Sequence<Self>) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { rebotarm_msgs__srv__SetZero_Response__Sequence__copy(in_seq, out_seq as *mut _) }
  }
}

impl rosidl_runtime_rs::Message for SetZero_Response {
  type RmwMsg = Self;
  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> { msg_cow }
  fn from_rmw_message(msg: Self::RmwMsg) -> Self { msg }
}

impl rosidl_runtime_rs::RmwMessage for SetZero_Response where Self: Sized {
  const TYPE_NAME: &'static str = "rebotarm_msgs/srv/SetZero_Response";
  fn get_type_support() -> *const std::ffi::c_void {
    // SAFETY: No preconditions for this function.
    unsafe { rosidl_typesupport_c__get_message_type_support_handle__rebotarm_msgs__srv__SetZero_Response() }
  }
}


#[link(name = "rebotarm_msgs__rosidl_typesupport_c")]
extern "C" {
    fn rosidl_typesupport_c__get_message_type_support_handle__rebotarm_msgs__srv__MoveToPoseIK_Request() -> *const std::ffi::c_void;
}

#[link(name = "rebotarm_msgs__rosidl_generator_c")]
extern "C" {
    fn rebotarm_msgs__srv__MoveToPoseIK_Request__init(msg: *mut MoveToPoseIK_Request) -> bool;
    fn rebotarm_msgs__srv__MoveToPoseIK_Request__Sequence__init(seq: *mut rosidl_runtime_rs::Sequence<MoveToPoseIK_Request>, size: usize) -> bool;
    fn rebotarm_msgs__srv__MoveToPoseIK_Request__Sequence__fini(seq: *mut rosidl_runtime_rs::Sequence<MoveToPoseIK_Request>);
    fn rebotarm_msgs__srv__MoveToPoseIK_Request__Sequence__copy(in_seq: &rosidl_runtime_rs::Sequence<MoveToPoseIK_Request>, out_seq: *mut rosidl_runtime_rs::Sequence<MoveToPoseIK_Request>) -> bool;
}

// Corresponds to rebotarm_msgs__srv__MoveToPoseIK_Request
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]


// This struct is not documented.
#[allow(missing_docs)]

#[allow(non_camel_case_types)]
#[repr(C)]
#[derive(Clone, Debug, PartialEq, PartialOrd)]
pub struct MoveToPoseIK_Request {

    // This member is not documented.
    #[allow(missing_docs)]
    pub target_pose: geometry_msgs::msg::rmw::Pose,

}



impl Default for MoveToPoseIK_Request {
  fn default() -> Self {
    unsafe {
      let mut msg = std::mem::zeroed();
      if !rebotarm_msgs__srv__MoveToPoseIK_Request__init(&mut msg as *mut _) {
        panic!("Call to rebotarm_msgs__srv__MoveToPoseIK_Request__init() failed");
      }
      msg
    }
  }
}

impl rosidl_runtime_rs::SequenceAlloc for MoveToPoseIK_Request {
  fn sequence_init(seq: &mut rosidl_runtime_rs::Sequence<Self>, size: usize) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { rebotarm_msgs__srv__MoveToPoseIK_Request__Sequence__init(seq as *mut _, size) }
  }
  fn sequence_fini(seq: &mut rosidl_runtime_rs::Sequence<Self>) {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { rebotarm_msgs__srv__MoveToPoseIK_Request__Sequence__fini(seq as *mut _) }
  }
  fn sequence_copy(in_seq: &rosidl_runtime_rs::Sequence<Self>, out_seq: &mut rosidl_runtime_rs::Sequence<Self>) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { rebotarm_msgs__srv__MoveToPoseIK_Request__Sequence__copy(in_seq, out_seq as *mut _) }
  }
}

impl rosidl_runtime_rs::Message for MoveToPoseIK_Request {
  type RmwMsg = Self;
  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> { msg_cow }
  fn from_rmw_message(msg: Self::RmwMsg) -> Self { msg }
}

impl rosidl_runtime_rs::RmwMessage for MoveToPoseIK_Request where Self: Sized {
  const TYPE_NAME: &'static str = "rebotarm_msgs/srv/MoveToPoseIK_Request";
  fn get_type_support() -> *const std::ffi::c_void {
    // SAFETY: No preconditions for this function.
    unsafe { rosidl_typesupport_c__get_message_type_support_handle__rebotarm_msgs__srv__MoveToPoseIK_Request() }
  }
}


#[link(name = "rebotarm_msgs__rosidl_typesupport_c")]
extern "C" {
    fn rosidl_typesupport_c__get_message_type_support_handle__rebotarm_msgs__srv__MoveToPoseIK_Response() -> *const std::ffi::c_void;
}

#[link(name = "rebotarm_msgs__rosidl_generator_c")]
extern "C" {
    fn rebotarm_msgs__srv__MoveToPoseIK_Response__init(msg: *mut MoveToPoseIK_Response) -> bool;
    fn rebotarm_msgs__srv__MoveToPoseIK_Response__Sequence__init(seq: *mut rosidl_runtime_rs::Sequence<MoveToPoseIK_Response>, size: usize) -> bool;
    fn rebotarm_msgs__srv__MoveToPoseIK_Response__Sequence__fini(seq: *mut rosidl_runtime_rs::Sequence<MoveToPoseIK_Response>);
    fn rebotarm_msgs__srv__MoveToPoseIK_Response__Sequence__copy(in_seq: &rosidl_runtime_rs::Sequence<MoveToPoseIK_Response>, out_seq: *mut rosidl_runtime_rs::Sequence<MoveToPoseIK_Response>) -> bool;
}

// Corresponds to rebotarm_msgs__srv__MoveToPoseIK_Response
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]


// This struct is not documented.
#[allow(missing_docs)]

#[allow(non_camel_case_types)]
#[repr(C)]
#[derive(Clone, Debug, PartialEq, PartialOrd)]
pub struct MoveToPoseIK_Response {

    // This member is not documented.
    #[allow(missing_docs)]
    pub success: bool,


    // This member is not documented.
    #[allow(missing_docs)]
    pub message: rosidl_runtime_rs::String,


    // This member is not documented.
    #[allow(missing_docs)]
    pub q_solution: rosidl_runtime_rs::Sequence<f64>,

}



impl Default for MoveToPoseIK_Response {
  fn default() -> Self {
    unsafe {
      let mut msg = std::mem::zeroed();
      if !rebotarm_msgs__srv__MoveToPoseIK_Response__init(&mut msg as *mut _) {
        panic!("Call to rebotarm_msgs__srv__MoveToPoseIK_Response__init() failed");
      }
      msg
    }
  }
}

impl rosidl_runtime_rs::SequenceAlloc for MoveToPoseIK_Response {
  fn sequence_init(seq: &mut rosidl_runtime_rs::Sequence<Self>, size: usize) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { rebotarm_msgs__srv__MoveToPoseIK_Response__Sequence__init(seq as *mut _, size) }
  }
  fn sequence_fini(seq: &mut rosidl_runtime_rs::Sequence<Self>) {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { rebotarm_msgs__srv__MoveToPoseIK_Response__Sequence__fini(seq as *mut _) }
  }
  fn sequence_copy(in_seq: &rosidl_runtime_rs::Sequence<Self>, out_seq: &mut rosidl_runtime_rs::Sequence<Self>) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { rebotarm_msgs__srv__MoveToPoseIK_Response__Sequence__copy(in_seq, out_seq as *mut _) }
  }
}

impl rosidl_runtime_rs::Message for MoveToPoseIK_Response {
  type RmwMsg = Self;
  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> { msg_cow }
  fn from_rmw_message(msg: Self::RmwMsg) -> Self { msg }
}

impl rosidl_runtime_rs::RmwMessage for MoveToPoseIK_Response where Self: Sized {
  const TYPE_NAME: &'static str = "rebotarm_msgs/srv/MoveToPoseIK_Response";
  fn get_type_support() -> *const std::ffi::c_void {
    // SAFETY: No preconditions for this function.
    unsafe { rosidl_typesupport_c__get_message_type_support_handle__rebotarm_msgs__srv__MoveToPoseIK_Response() }
  }
}


#[link(name = "rebotarm_msgs__rosidl_typesupport_c")]
extern "C" {
    fn rosidl_typesupport_c__get_message_type_support_handle__rebotarm_msgs__srv__SetGripper_Request() -> *const std::ffi::c_void;
}

#[link(name = "rebotarm_msgs__rosidl_generator_c")]
extern "C" {
    fn rebotarm_msgs__srv__SetGripper_Request__init(msg: *mut SetGripper_Request) -> bool;
    fn rebotarm_msgs__srv__SetGripper_Request__Sequence__init(seq: *mut rosidl_runtime_rs::Sequence<SetGripper_Request>, size: usize) -> bool;
    fn rebotarm_msgs__srv__SetGripper_Request__Sequence__fini(seq: *mut rosidl_runtime_rs::Sequence<SetGripper_Request>);
    fn rebotarm_msgs__srv__SetGripper_Request__Sequence__copy(in_seq: &rosidl_runtime_rs::Sequence<SetGripper_Request>, out_seq: *mut rosidl_runtime_rs::Sequence<SetGripper_Request>) -> bool;
}

// Corresponds to rebotarm_msgs__srv__SetGripper_Request
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]


// This struct is not documented.
#[allow(missing_docs)]

#[allow(non_camel_case_types)]
#[repr(C)]
#[derive(Clone, Debug, PartialEq, PartialOrd)]
pub struct SetGripper_Request {

    // This member is not documented.
    #[allow(missing_docs)]
    pub position: f64,


    // This member is not documented.
    #[allow(missing_docs)]
    pub max_effort: f64,

}



impl Default for SetGripper_Request {
  fn default() -> Self {
    unsafe {
      let mut msg = std::mem::zeroed();
      if !rebotarm_msgs__srv__SetGripper_Request__init(&mut msg as *mut _) {
        panic!("Call to rebotarm_msgs__srv__SetGripper_Request__init() failed");
      }
      msg
    }
  }
}

impl rosidl_runtime_rs::SequenceAlloc for SetGripper_Request {
  fn sequence_init(seq: &mut rosidl_runtime_rs::Sequence<Self>, size: usize) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { rebotarm_msgs__srv__SetGripper_Request__Sequence__init(seq as *mut _, size) }
  }
  fn sequence_fini(seq: &mut rosidl_runtime_rs::Sequence<Self>) {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { rebotarm_msgs__srv__SetGripper_Request__Sequence__fini(seq as *mut _) }
  }
  fn sequence_copy(in_seq: &rosidl_runtime_rs::Sequence<Self>, out_seq: &mut rosidl_runtime_rs::Sequence<Self>) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { rebotarm_msgs__srv__SetGripper_Request__Sequence__copy(in_seq, out_seq as *mut _) }
  }
}

impl rosidl_runtime_rs::Message for SetGripper_Request {
  type RmwMsg = Self;
  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> { msg_cow }
  fn from_rmw_message(msg: Self::RmwMsg) -> Self { msg }
}

impl rosidl_runtime_rs::RmwMessage for SetGripper_Request where Self: Sized {
  const TYPE_NAME: &'static str = "rebotarm_msgs/srv/SetGripper_Request";
  fn get_type_support() -> *const std::ffi::c_void {
    // SAFETY: No preconditions for this function.
    unsafe { rosidl_typesupport_c__get_message_type_support_handle__rebotarm_msgs__srv__SetGripper_Request() }
  }
}


#[link(name = "rebotarm_msgs__rosidl_typesupport_c")]
extern "C" {
    fn rosidl_typesupport_c__get_message_type_support_handle__rebotarm_msgs__srv__SetGripper_Response() -> *const std::ffi::c_void;
}

#[link(name = "rebotarm_msgs__rosidl_generator_c")]
extern "C" {
    fn rebotarm_msgs__srv__SetGripper_Response__init(msg: *mut SetGripper_Response) -> bool;
    fn rebotarm_msgs__srv__SetGripper_Response__Sequence__init(seq: *mut rosidl_runtime_rs::Sequence<SetGripper_Response>, size: usize) -> bool;
    fn rebotarm_msgs__srv__SetGripper_Response__Sequence__fini(seq: *mut rosidl_runtime_rs::Sequence<SetGripper_Response>);
    fn rebotarm_msgs__srv__SetGripper_Response__Sequence__copy(in_seq: &rosidl_runtime_rs::Sequence<SetGripper_Response>, out_seq: *mut rosidl_runtime_rs::Sequence<SetGripper_Response>) -> bool;
}

// Corresponds to rebotarm_msgs__srv__SetGripper_Response
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]


// This struct is not documented.
#[allow(missing_docs)]

#[allow(non_camel_case_types)]
#[repr(C)]
#[derive(Clone, Debug, PartialEq, PartialOrd)]
pub struct SetGripper_Response {

    // This member is not documented.
    #[allow(missing_docs)]
    pub success: bool,


    // This member is not documented.
    #[allow(missing_docs)]
    pub reached_position: f64,

}



impl Default for SetGripper_Response {
  fn default() -> Self {
    unsafe {
      let mut msg = std::mem::zeroed();
      if !rebotarm_msgs__srv__SetGripper_Response__init(&mut msg as *mut _) {
        panic!("Call to rebotarm_msgs__srv__SetGripper_Response__init() failed");
      }
      msg
    }
  }
}

impl rosidl_runtime_rs::SequenceAlloc for SetGripper_Response {
  fn sequence_init(seq: &mut rosidl_runtime_rs::Sequence<Self>, size: usize) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { rebotarm_msgs__srv__SetGripper_Response__Sequence__init(seq as *mut _, size) }
  }
  fn sequence_fini(seq: &mut rosidl_runtime_rs::Sequence<Self>) {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { rebotarm_msgs__srv__SetGripper_Response__Sequence__fini(seq as *mut _) }
  }
  fn sequence_copy(in_seq: &rosidl_runtime_rs::Sequence<Self>, out_seq: &mut rosidl_runtime_rs::Sequence<Self>) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { rebotarm_msgs__srv__SetGripper_Response__Sequence__copy(in_seq, out_seq as *mut _) }
  }
}

impl rosidl_runtime_rs::Message for SetGripper_Response {
  type RmwMsg = Self;
  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> { msg_cow }
  fn from_rmw_message(msg: Self::RmwMsg) -> Self { msg }
}

impl rosidl_runtime_rs::RmwMessage for SetGripper_Response where Self: Sized {
  const TYPE_NAME: &'static str = "rebotarm_msgs/srv/SetGripper_Response";
  fn get_type_support() -> *const std::ffi::c_void {
    // SAFETY: No preconditions for this function.
    unsafe { rosidl_typesupport_c__get_message_type_support_handle__rebotarm_msgs__srv__SetGripper_Response() }
  }
}


#[link(name = "rebotarm_msgs__rosidl_typesupport_c")]
extern "C" {
    fn rosidl_typesupport_c__get_message_type_support_handle__rebotarm_msgs__srv__GripperCommand_Request() -> *const std::ffi::c_void;
}

#[link(name = "rebotarm_msgs__rosidl_generator_c")]
extern "C" {
    fn rebotarm_msgs__srv__GripperCommand_Request__init(msg: *mut GripperCommand_Request) -> bool;
    fn rebotarm_msgs__srv__GripperCommand_Request__Sequence__init(seq: *mut rosidl_runtime_rs::Sequence<GripperCommand_Request>, size: usize) -> bool;
    fn rebotarm_msgs__srv__GripperCommand_Request__Sequence__fini(seq: *mut rosidl_runtime_rs::Sequence<GripperCommand_Request>);
    fn rebotarm_msgs__srv__GripperCommand_Request__Sequence__copy(in_seq: &rosidl_runtime_rs::Sequence<GripperCommand_Request>, out_seq: *mut rosidl_runtime_rs::Sequence<GripperCommand_Request>) -> bool;
}

// Corresponds to rebotarm_msgs__srv__GripperCommand_Request
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]


// This struct is not documented.
#[allow(missing_docs)]

#[allow(non_camel_case_types)]
#[repr(C)]
#[derive(Clone, Debug, PartialEq, PartialOrd)]
pub struct GripperCommand_Request {

    // This member is not documented.
    #[allow(missing_docs)]
    pub position: f64,


    // This member is not documented.
    #[allow(missing_docs)]
    pub timeout: f64,

}



impl Default for GripperCommand_Request {
  fn default() -> Self {
    unsafe {
      let mut msg = std::mem::zeroed();
      if !rebotarm_msgs__srv__GripperCommand_Request__init(&mut msg as *mut _) {
        panic!("Call to rebotarm_msgs__srv__GripperCommand_Request__init() failed");
      }
      msg
    }
  }
}

impl rosidl_runtime_rs::SequenceAlloc for GripperCommand_Request {
  fn sequence_init(seq: &mut rosidl_runtime_rs::Sequence<Self>, size: usize) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { rebotarm_msgs__srv__GripperCommand_Request__Sequence__init(seq as *mut _, size) }
  }
  fn sequence_fini(seq: &mut rosidl_runtime_rs::Sequence<Self>) {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { rebotarm_msgs__srv__GripperCommand_Request__Sequence__fini(seq as *mut _) }
  }
  fn sequence_copy(in_seq: &rosidl_runtime_rs::Sequence<Self>, out_seq: &mut rosidl_runtime_rs::Sequence<Self>) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { rebotarm_msgs__srv__GripperCommand_Request__Sequence__copy(in_seq, out_seq as *mut _) }
  }
}

impl rosidl_runtime_rs::Message for GripperCommand_Request {
  type RmwMsg = Self;
  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> { msg_cow }
  fn from_rmw_message(msg: Self::RmwMsg) -> Self { msg }
}

impl rosidl_runtime_rs::RmwMessage for GripperCommand_Request where Self: Sized {
  const TYPE_NAME: &'static str = "rebotarm_msgs/srv/GripperCommand_Request";
  fn get_type_support() -> *const std::ffi::c_void {
    // SAFETY: No preconditions for this function.
    unsafe { rosidl_typesupport_c__get_message_type_support_handle__rebotarm_msgs__srv__GripperCommand_Request() }
  }
}


#[link(name = "rebotarm_msgs__rosidl_typesupport_c")]
extern "C" {
    fn rosidl_typesupport_c__get_message_type_support_handle__rebotarm_msgs__srv__GripperCommand_Response() -> *const std::ffi::c_void;
}

#[link(name = "rebotarm_msgs__rosidl_generator_c")]
extern "C" {
    fn rebotarm_msgs__srv__GripperCommand_Response__init(msg: *mut GripperCommand_Response) -> bool;
    fn rebotarm_msgs__srv__GripperCommand_Response__Sequence__init(seq: *mut rosidl_runtime_rs::Sequence<GripperCommand_Response>, size: usize) -> bool;
    fn rebotarm_msgs__srv__GripperCommand_Response__Sequence__fini(seq: *mut rosidl_runtime_rs::Sequence<GripperCommand_Response>);
    fn rebotarm_msgs__srv__GripperCommand_Response__Sequence__copy(in_seq: &rosidl_runtime_rs::Sequence<GripperCommand_Response>, out_seq: *mut rosidl_runtime_rs::Sequence<GripperCommand_Response>) -> bool;
}

// Corresponds to rebotarm_msgs__srv__GripperCommand_Response
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]


// This struct is not documented.
#[allow(missing_docs)]

#[allow(non_camel_case_types)]
#[repr(C)]
#[derive(Clone, Debug, PartialEq, PartialOrd)]
pub struct GripperCommand_Response {

    // This member is not documented.
    #[allow(missing_docs)]
    pub success: bool,


    // This member is not documented.
    #[allow(missing_docs)]
    pub reached_position: f64,


    // This member is not documented.
    #[allow(missing_docs)]
    pub message: rosidl_runtime_rs::String,

}



impl Default for GripperCommand_Response {
  fn default() -> Self {
    unsafe {
      let mut msg = std::mem::zeroed();
      if !rebotarm_msgs__srv__GripperCommand_Response__init(&mut msg as *mut _) {
        panic!("Call to rebotarm_msgs__srv__GripperCommand_Response__init() failed");
      }
      msg
    }
  }
}

impl rosidl_runtime_rs::SequenceAlloc for GripperCommand_Response {
  fn sequence_init(seq: &mut rosidl_runtime_rs::Sequence<Self>, size: usize) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { rebotarm_msgs__srv__GripperCommand_Response__Sequence__init(seq as *mut _, size) }
  }
  fn sequence_fini(seq: &mut rosidl_runtime_rs::Sequence<Self>) {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { rebotarm_msgs__srv__GripperCommand_Response__Sequence__fini(seq as *mut _) }
  }
  fn sequence_copy(in_seq: &rosidl_runtime_rs::Sequence<Self>, out_seq: &mut rosidl_runtime_rs::Sequence<Self>) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { rebotarm_msgs__srv__GripperCommand_Response__Sequence__copy(in_seq, out_seq as *mut _) }
  }
}

impl rosidl_runtime_rs::Message for GripperCommand_Response {
  type RmwMsg = Self;
  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> { msg_cow }
  fn from_rmw_message(msg: Self::RmwMsg) -> Self { msg }
}

impl rosidl_runtime_rs::RmwMessage for GripperCommand_Response where Self: Sized {
  const TYPE_NAME: &'static str = "rebotarm_msgs/srv/GripperCommand_Response";
  fn get_type_support() -> *const std::ffi::c_void {
    // SAFETY: No preconditions for this function.
    unsafe { rosidl_typesupport_c__get_message_type_support_handle__rebotarm_msgs__srv__GripperCommand_Response() }
  }
}






#[link(name = "rebotarm_msgs__rosidl_typesupport_c")]
extern "C" {
    fn rosidl_typesupport_c__get_service_type_support_handle__rebotarm_msgs__srv__SetMode() -> *const std::ffi::c_void;
}

// Corresponds to rebotarm_msgs__srv__SetMode
#[allow(missing_docs, non_camel_case_types)]
pub struct SetMode;

impl rosidl_runtime_rs::Service for SetMode {
    type Request = SetMode_Request;
    type Response = SetMode_Response;

    fn get_type_support() -> *const std::ffi::c_void {
        // SAFETY: No preconditions for this function.
        unsafe { rosidl_typesupport_c__get_service_type_support_handle__rebotarm_msgs__srv__SetMode() }
    }
}




#[link(name = "rebotarm_msgs__rosidl_typesupport_c")]
extern "C" {
    fn rosidl_typesupport_c__get_service_type_support_handle__rebotarm_msgs__srv__SetZero() -> *const std::ffi::c_void;
}

// Corresponds to rebotarm_msgs__srv__SetZero
#[allow(missing_docs, non_camel_case_types)]
pub struct SetZero;

impl rosidl_runtime_rs::Service for SetZero {
    type Request = SetZero_Request;
    type Response = SetZero_Response;

    fn get_type_support() -> *const std::ffi::c_void {
        // SAFETY: No preconditions for this function.
        unsafe { rosidl_typesupport_c__get_service_type_support_handle__rebotarm_msgs__srv__SetZero() }
    }
}




#[link(name = "rebotarm_msgs__rosidl_typesupport_c")]
extern "C" {
    fn rosidl_typesupport_c__get_service_type_support_handle__rebotarm_msgs__srv__MoveToPoseIK() -> *const std::ffi::c_void;
}

// Corresponds to rebotarm_msgs__srv__MoveToPoseIK
#[allow(missing_docs, non_camel_case_types)]
pub struct MoveToPoseIK;

impl rosidl_runtime_rs::Service for MoveToPoseIK {
    type Request = MoveToPoseIK_Request;
    type Response = MoveToPoseIK_Response;

    fn get_type_support() -> *const std::ffi::c_void {
        // SAFETY: No preconditions for this function.
        unsafe { rosidl_typesupport_c__get_service_type_support_handle__rebotarm_msgs__srv__MoveToPoseIK() }
    }
}




#[link(name = "rebotarm_msgs__rosidl_typesupport_c")]
extern "C" {
    fn rosidl_typesupport_c__get_service_type_support_handle__rebotarm_msgs__srv__SetGripper() -> *const std::ffi::c_void;
}

// Corresponds to rebotarm_msgs__srv__SetGripper
#[allow(missing_docs, non_camel_case_types)]
pub struct SetGripper;

impl rosidl_runtime_rs::Service for SetGripper {
    type Request = SetGripper_Request;
    type Response = SetGripper_Response;

    fn get_type_support() -> *const std::ffi::c_void {
        // SAFETY: No preconditions for this function.
        unsafe { rosidl_typesupport_c__get_service_type_support_handle__rebotarm_msgs__srv__SetGripper() }
    }
}




#[link(name = "rebotarm_msgs__rosidl_typesupport_c")]
extern "C" {
    fn rosidl_typesupport_c__get_service_type_support_handle__rebotarm_msgs__srv__GripperCommand() -> *const std::ffi::c_void;
}

// Corresponds to rebotarm_msgs__srv__GripperCommand
#[allow(missing_docs, non_camel_case_types)]
pub struct GripperCommand;

impl rosidl_runtime_rs::Service for GripperCommand {
    type Request = GripperCommand_Request;
    type Response = GripperCommand_Response;

    fn get_type_support() -> *const std::ffi::c_void {
        // SAFETY: No preconditions for this function.
        unsafe { rosidl_typesupport_c__get_service_type_support_handle__rebotarm_msgs__srv__GripperCommand() }
    }
}


