#[cfg(feature = "serde")]
use serde::{Deserialize, Serialize};



// Corresponds to rebotarm_msgs__msg__JointMotorCmd

// This struct is not documented.
#[allow(missing_docs)]

#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]
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
    pub stamp: builtin_interfaces::msg::Time,

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
    <Self as rosidl_runtime_rs::Message>::from_rmw_message(super::msg::rmw::JointMotorCmd::default())
  }
}

impl rosidl_runtime_rs::Message for JointMotorCmd {
  type RmwMsg = super::msg::rmw::JointMotorCmd;

  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> {
    match msg_cow {
      std::borrow::Cow::Owned(msg) => std::borrow::Cow::Owned(Self::RmwMsg {
        mode: msg.mode,
        use_pos: msg.use_pos,
        use_vel: msg.use_vel,
        use_kp: msg.use_kp,
        use_kd: msg.use_kd,
        use_tau: msg.use_tau,
        use_vlim: msg.use_vlim,
        pos: msg.pos,
        vel: msg.vel,
        kp: msg.kp,
        kd: msg.kd,
        tau: msg.tau,
        vlim: msg.vlim,
        stamp: builtin_interfaces::msg::Time::into_rmw_message(std::borrow::Cow::Owned(msg.stamp)).into_owned(),
      }),
      std::borrow::Cow::Borrowed(msg) => std::borrow::Cow::Owned(Self::RmwMsg {
      mode: msg.mode,
      use_pos: msg.use_pos,
      use_vel: msg.use_vel,
      use_kp: msg.use_kp,
      use_kd: msg.use_kd,
      use_tau: msg.use_tau,
      use_vlim: msg.use_vlim,
      pos: msg.pos,
      vel: msg.vel,
      kp: msg.kp,
      kd: msg.kd,
      tau: msg.tau,
      vlim: msg.vlim,
        stamp: builtin_interfaces::msg::Time::into_rmw_message(std::borrow::Cow::Borrowed(&msg.stamp)).into_owned(),
      })
    }
  }

  fn from_rmw_message(msg: Self::RmwMsg) -> Self {
    Self {
      mode: msg.mode,
      use_pos: msg.use_pos,
      use_vel: msg.use_vel,
      use_kp: msg.use_kp,
      use_kd: msg.use_kd,
      use_tau: msg.use_tau,
      use_vlim: msg.use_vlim,
      pos: msg.pos,
      vel: msg.vel,
      kp: msg.kp,
      kd: msg.kd,
      tau: msg.tau,
      vlim: msg.vlim,
      stamp: builtin_interfaces::msg::Time::from_rmw_message(msg.stamp),
    }
  }
}


// Corresponds to rebotarm_msgs__msg__JointMitCmd

// This struct is not documented.
#[allow(missing_docs)]

#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]
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
    pub stamp: builtin_interfaces::msg::Time,

}



impl Default for JointMitCmd {
  fn default() -> Self {
    <Self as rosidl_runtime_rs::Message>::from_rmw_message(super::msg::rmw::JointMitCmd::default())
  }
}

impl rosidl_runtime_rs::Message for JointMitCmd {
  type RmwMsg = super::msg::rmw::JointMitCmd;

  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> {
    match msg_cow {
      std::borrow::Cow::Owned(msg) => std::borrow::Cow::Owned(Self::RmwMsg {
        pos: msg.pos,
        vel: msg.vel,
        kp: msg.kp,
        kd: msg.kd,
        tau: msg.tau,
        stamp: builtin_interfaces::msg::Time::into_rmw_message(std::borrow::Cow::Owned(msg.stamp)).into_owned(),
      }),
      std::borrow::Cow::Borrowed(msg) => std::borrow::Cow::Owned(Self::RmwMsg {
      pos: msg.pos,
      vel: msg.vel,
      kp: msg.kp,
      kd: msg.kd,
      tau: msg.tau,
        stamp: builtin_interfaces::msg::Time::into_rmw_message(std::borrow::Cow::Borrowed(&msg.stamp)).into_owned(),
      })
    }
  }

  fn from_rmw_message(msg: Self::RmwMsg) -> Self {
    Self {
      pos: msg.pos,
      vel: msg.vel,
      kp: msg.kp,
      kd: msg.kd,
      tau: msg.tau,
      stamp: builtin_interfaces::msg::Time::from_rmw_message(msg.stamp),
    }
  }
}


// Corresponds to rebotarm_msgs__msg__JointPosVelCmd

// This struct is not documented.
#[allow(missing_docs)]

#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]
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
    pub stamp: builtin_interfaces::msg::Time,

}



impl Default for JointPosVelCmd {
  fn default() -> Self {
    <Self as rosidl_runtime_rs::Message>::from_rmw_message(super::msg::rmw::JointPosVelCmd::default())
  }
}

impl rosidl_runtime_rs::Message for JointPosVelCmd {
  type RmwMsg = super::msg::rmw::JointPosVelCmd;

  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> {
    match msg_cow {
      std::borrow::Cow::Owned(msg) => std::borrow::Cow::Owned(Self::RmwMsg {
        pos: msg.pos,
        vlim: msg.vlim,
        stamp: builtin_interfaces::msg::Time::into_rmw_message(std::borrow::Cow::Owned(msg.stamp)).into_owned(),
      }),
      std::borrow::Cow::Borrowed(msg) => std::borrow::Cow::Owned(Self::RmwMsg {
      pos: msg.pos,
      vlim: msg.vlim,
        stamp: builtin_interfaces::msg::Time::into_rmw_message(std::borrow::Cow::Borrowed(&msg.stamp)).into_owned(),
      })
    }
  }

  fn from_rmw_message(msg: Self::RmwMsg) -> Self {
    Self {
      pos: msg.pos,
      vlim: msg.vlim,
      stamp: builtin_interfaces::msg::Time::from_rmw_message(msg.stamp),
    }
  }
}


// Corresponds to rebotarm_msgs__msg__JointMotorState

// This struct is not documented.
#[allow(missing_docs)]

#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]
#[derive(Clone, Debug, PartialEq, PartialOrd)]
pub struct JointMotorState {

    // This member is not documented.
    #[allow(missing_docs)]
    pub header: std_msgs::msg::Header,


    // This member is not documented.
    #[allow(missing_docs)]
    pub joint_name: std::string::String,


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
    <Self as rosidl_runtime_rs::Message>::from_rmw_message(super::msg::rmw::JointMotorState::default())
  }
}

impl rosidl_runtime_rs::Message for JointMotorState {
  type RmwMsg = super::msg::rmw::JointMotorState;

  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> {
    match msg_cow {
      std::borrow::Cow::Owned(msg) => std::borrow::Cow::Owned(Self::RmwMsg {
        header: std_msgs::msg::Header::into_rmw_message(std::borrow::Cow::Owned(msg.header)).into_owned(),
        joint_name: msg.joint_name.as_str().into(),
        position: msg.position,
        velocity: msg.velocity,
        torque: msg.torque,
        status_code: msg.status_code,
      }),
      std::borrow::Cow::Borrowed(msg) => std::borrow::Cow::Owned(Self::RmwMsg {
        header: std_msgs::msg::Header::into_rmw_message(std::borrow::Cow::Borrowed(&msg.header)).into_owned(),
        joint_name: msg.joint_name.as_str().into(),
      position: msg.position,
      velocity: msg.velocity,
      torque: msg.torque,
      status_code: msg.status_code,
      })
    }
  }

  fn from_rmw_message(msg: Self::RmwMsg) -> Self {
    Self {
      header: std_msgs::msg::Header::from_rmw_message(msg.header),
      joint_name: msg.joint_name.to_string(),
      position: msg.position,
      velocity: msg.velocity,
      torque: msg.torque,
      status_code: msg.status_code,
    }
  }
}


// Corresponds to rebotarm_msgs__msg__ArmStatus

// This struct is not documented.
#[allow(missing_docs)]

#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]
#[derive(Clone, Debug, PartialEq, PartialOrd)]
pub struct ArmStatus {

    // This member is not documented.
    #[allow(missing_docs)]
    pub header: std_msgs::msg::Header,


    // This member is not documented.
    #[allow(missing_docs)]
    pub mode: std::string::String,


    // This member is not documented.
    #[allow(missing_docs)]
    pub enabled: bool,


    // This member is not documented.
    #[allow(missing_docs)]
    pub control_loop_active: bool,


    // This member is not documented.
    #[allow(missing_docs)]
    pub state_machine: std::string::String,


    // This member is not documented.
    #[allow(missing_docs)]
    pub joint_names: Vec<std::string::String>,


    // This member is not documented.
    #[allow(missing_docs)]
    pub per_joint_status_code: Vec<u8>,


    // This member is not documented.
    #[allow(missing_docs)]
    pub error_codes: Vec<std::string::String>,

}



impl Default for ArmStatus {
  fn default() -> Self {
    <Self as rosidl_runtime_rs::Message>::from_rmw_message(super::msg::rmw::ArmStatus::default())
  }
}

impl rosidl_runtime_rs::Message for ArmStatus {
  type RmwMsg = super::msg::rmw::ArmStatus;

  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> {
    match msg_cow {
      std::borrow::Cow::Owned(msg) => std::borrow::Cow::Owned(Self::RmwMsg {
        header: std_msgs::msg::Header::into_rmw_message(std::borrow::Cow::Owned(msg.header)).into_owned(),
        mode: msg.mode.as_str().into(),
        enabled: msg.enabled,
        control_loop_active: msg.control_loop_active,
        state_machine: msg.state_machine.as_str().into(),
        joint_names: msg.joint_names
          .into_iter()
          .map(|elem| elem.as_str().into())
          .collect(),
        per_joint_status_code: msg.per_joint_status_code.as_slice().into(),
        error_codes: msg.error_codes
          .into_iter()
          .map(|elem| elem.as_str().into())
          .collect(),
      }),
      std::borrow::Cow::Borrowed(msg) => std::borrow::Cow::Owned(Self::RmwMsg {
        header: std_msgs::msg::Header::into_rmw_message(std::borrow::Cow::Borrowed(&msg.header)).into_owned(),
        mode: msg.mode.as_str().into(),
      enabled: msg.enabled,
      control_loop_active: msg.control_loop_active,
        state_machine: msg.state_machine.as_str().into(),
        joint_names: msg.joint_names
          .iter()
          .map(|elem| elem.as_str().into())
          .collect(),
        per_joint_status_code: msg.per_joint_status_code.as_slice().into(),
        error_codes: msg.error_codes
          .iter()
          .map(|elem| elem.as_str().into())
          .collect(),
      })
    }
  }

  fn from_rmw_message(msg: Self::RmwMsg) -> Self {
    Self {
      header: std_msgs::msg::Header::from_rmw_message(msg.header),
      mode: msg.mode.to_string(),
      enabled: msg.enabled,
      control_loop_active: msg.control_loop_active,
      state_machine: msg.state_machine.to_string(),
      joint_names: msg.joint_names
          .into_iter()
          .map(|elem| elem.to_string())
          .collect(),
      per_joint_status_code: msg.per_joint_status_code.into(),
      error_codes: msg.error_codes
          .into_iter()
          .map(|elem| elem.to_string())
          .collect(),
    }
  }
}


