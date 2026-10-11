# 整臂状态（**arm_manage** 发，10 Hz，latched QoS）。见 docs/MANAGE_ARCH.md §4.2。
#
# ⚠️ 与 HardwareStatus **分成两个话题、两个发布者**：插件不该知道控制器，管理层不该去读串口。
#    同一个话题两个发布者 = 两份真源，别这么干。
# ⚠️ 没有 `GetStatus` 服务：latched（TRANSIENT_LOCAL）话题让**新订阅者立刻拿到最后一份**
#    ⇒ 服务只留给"要改变状态"的动作（M-6）。
#
# 用法：`ros2 topic echo /arm/status --once`（state_machine 取值见 MANAGE_ARCH §4.3）。
# ⚠️ 2026-10-07 改版：本文过去无人发布（`enabled`/`control_loop_active` 是当初的设计），
#    现在按管理层的字段重写；老字段没有消费者，直接替换。
builtin_interfaces/Time stamp
string   state_machine     # READONLY / ARMING / ARMED / ARMED_CONTROLLER / DISARMING / FAULT
bool     hardware_armed    # 硬件侧是否使能（来自 HardwareStatus）
bool     controller_active # arm_controller 是否 active（能发轨迹）
string   mode              # **声明**模式（"MIT"/"POS_VEL"；真源 joint.yaml）
string[] joint_names
int32[]  per_joint_status_code
string[] error_codes
HardwareStatus hardware    # 内嵌：想看逐关节 ERR/温度/守卫的看它
string   message           # 最近一次编排的结果/原因（便于现场排查）
