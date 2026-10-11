# 硬件侧状态（**插件**发，10 Hz，latched QoS）。见 docs/MANAGE_ARCH.md §4.2。
#
# 为什么只能由插件发：ros2_control 的状态接口**只有控制器能读**（/joint_states 只广播
# position/velocity/effort）⇒ ERR / 温度 / 守卫状态**外部节点根本拿不到**，必须插件自己发话题。
#
# ⚠️ 用了 builtin_interfaces/Time ⇒ rosidl 的 DEPENDENCIES 里必须有 builtin_interfaces。
builtin_interfaces/Time stamp
string   mode             # **声明**值（"MIT"/"POS_VEL"）；真源是 joint.yaml，本层读不到 0x0A（陷阱 #8）
bool     gravity_ff
float64  gravity_ff_scale
bool     guard_tripped    # 重力前馈的残差守卫是否**已锁存**（一旦触发就把 tau_ff 全置 0）
string[] joint_names
int32[]  err              # 逐关节 ERR（0=失能 / 1=使能 / 其它见 dm_frames.ERR_DECODE）
string[] err_text
float64[] temp_mos        # ℃，反馈帧 D[6]
float64[] temp_rotor      # ℃，反馈帧 D[7]
bool[]   armed            # 逐关节是否使能（ERR==1；注意 ERR==0 是"失能"不是"正常"）
uint64   tx_calls         # 串口统计（dm_serial::SerialStats）：写调用次数
uint64   rx_calls         # 读调用次数
uint64   tx_bytes
uint64   rx_bytes         # ⚠️ **它停滞 = 串口写不出去或电机不回包**（最有用的诊断量）
