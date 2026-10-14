# 硬件侧状态（**插件**发，10 Hz，latched QoS）。见 docs/MANAGE_ARCH.md §4.2。
#
# 为什么只能由插件发：ros2_control 的状态接口**只有控制器能读**（/joint_states 只广播
# position/velocity/effort）⇒ ERR / 温度**外部节点根本拿不到**，必须插件自己发话题。
#
# ⚠️ 用了 builtin_interfaces/Time ⇒ rosidl 的 DEPENDENCIES 里必须有 builtin_interfaces。
builtin_interfaces/Time stamp
# 运行期**真实**控制模式：逐关节一致时给名字（"MIT"/"POS_VEL"），不一致时逗号分隔逐关节列出。
# 它由 `effort` 命令接口的 NaN 语义决定（D-L1）⇒ 控制器起停会让它变，不是启动期声明值。
string   mode
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
