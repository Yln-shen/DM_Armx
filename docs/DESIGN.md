# DmArm 封装层设计文档（v0.11 —— 配置层落地）

> **这几份文档共用一套章节编号**（编号全局唯一 ⇒ 写一个 `§N` 时不带文件名也能找到）：

| 编号 | 在哪 |
|---|---|
| §一，§2.1~2.5，§三（D1~D9），§四~§九，§十一，§十二 | **本文件** |
| §2.6（SDK 的四个坑） | [`LESSONS.md`](LESSONS.md) |
| §2.7，§2.8，§十 | [`TESTING.md`](TESTING.md) |

**没有 `CHANGELOG.md`** —— 逐条修订记录已删（"什么时候改了哪一行" git log 里有）。
留下的是**教训**（`LESSONS.md`）和**实测数据**（`TESTING.md`），这两样 git log 不会替你记。

> v0.1 → v0.2 修订依据：达妙官方手册（`DM-J4310-2EC.md` / `DM-J4340P-2EC V1.1 .md`）、
> reBot 真机层实测（`~/reBot_Arm_Mujoco-DM/reBotArm_ros2_DM/src/rebotarmcontroller/`）、
> reBotArm SDK 配置（`~/reBotArm_control_py/config/rebotarm_dm.yaml`）、
> 官方 SDK 源码（`src/third_party/Python例程/u2can/DM_CAN.py`）+ 官方 `USAGE.md`。
> 全部结论可追溯到具体文件行号，见文中标注。

---

## 一、定位与边界

```
[ROS 2 驱动层]  ← 另开文档，本层不涉及（但要满足它的接口需求，见 1.3）
      ↓ 调用
[本封装层 DmArm / Joint]     ← 本文档的全部范围
      ↓ 调用
[DM_CAN.py 官方 SDK]  +  [自构帧的薄传输层]
      ↓
达妙 USB2CAN 适配器 (/dev/ttyACM0 @ 921600) → 电机
```

**替换对象**：本层替代 reBot 的 `reBotArm_control_py` actuator 层（`actuator/rebotarm.py`）+ `hardware_manager.py` 中的电机部分。
**可复用**：`reBotArm_control_py` 的 `kinematics/`、`dynamics/`、`trajectory/`（纯 numpy/pinocchio，与厂商无关），FK/IK/重力项可直接拿来交叉验证。

### 1.1 设计原则（v0.1 保留）

| 原则 | 说明 |
|---|---|
| 单位统一 | 关节 rad / rad·s⁻¹，夹爪 m（对外）；电机侧一律 rad，**无任何减速比折算** |
| 换算集中 | `direction`、`offset`、夹爪线性系数只在封装层做 |
| 安全内置 | 限位钳位、双层看门狗、可选的温度/电压检查、错误码解码 |
| 配置分离 | YAML + dataclass 校验 |
| 同步为主 | 默认同步阻塞，预留异步 |
| 可独立测试 | `import dm_arm` 即可用，不 import rclpy |
| **可回滚** | 写电机寄存器前必须先读并留存原值 |

### 1.2 依赖

| 依赖 | 来源 | 状态 |
|---|---|---|
| `pyserial` | pixi（`pixi.toml:16` `pyserial = ">=3.5,<4"`） | ✅ 已装（v0.7 更正：此前这里写"❌ 未装"是过期的） |
| `DM_CAN.py` | 已 vendored 在 `src/third_party/Python例程/u2can/DM_CAN.py` | ✅ |
| `numpy` | pixi（经 robostack 传递依赖；**只剩 SDK 用**，本层已完全不用 —— v0.8 `dm_frames` 去掉了 numpy） | ✅ |
| `pyyaml` | pixi | ✅ **无需显式添加**（v0.11 实测）：已作为 `ros-jazzy`/`mujoco` 的**传递依赖**存在于 `.pixi/envs/default/`（6.0.3），`import yaml` 直接可用。**没写进 `pixi.toml`** ⇒ 上游一变可能消失，所以 `arm_config.py` 把 `import yaml` 关进 `from_yaml` 一个函数 |
| **不使用 `motorbridge`** | —— | 它是 reBot 的依赖，你选了官方 SDK |

**本层自己的模块**（v0.8）：`dm_frames.py`（协议原语，纯函数）← `dm_bus.py`（MotorBus）。
**这两个都不依赖官方 SDK**（v0.8 起自持帧模板与定点映射），也不 import `numpy`。
两个都不 import `rclpy`，可脱离 ROS 独立测试（§1.1 原则）。

### 1.3 上层（ROS 驱动层）对本层的需求

ROS 侧要发的 `JointMotorCmd` 带六个 `use_*` 标志位，本层必须能表达三种组合：

| 标志位组合 | 本层方法 | 电机模式 |
|---|---|---|
| `use_pos` + `use_vlim` | `set_pos_vel(pos, vlim)` | POS_VEL（主用） |
| `use_pos/vel/kp/kd/tau` 全给 | `set_mit(kp, kd, q, dq, tau)` | MIT |
| `use_pos` + `use_vel` + `use_tau` | `set_force_pos(pos, vel, current)` | 力位混控 |

ROS 侧要发布的字段决定 `JointState` 必须携带：`position`、`velocity`、`torque`、`err`（→ `status_code`）、`temp_*`、`vbus`、`enabled`（→ `ArmStatus.enabled`）。

**名称对齐（需你确认）**：SDK 侧夹爪关节名是 `gripper`，ROS/MuJoCo 侧是 `finger_left`（MuJoCo `joint_map_kinematic.yaml` 再把 `finger_left` 映射到双指）。本层内部统一用 `gripper`，由 ROS 层做 `gripper ↔ finger_left` 的翻译。

---

---

## 二、事实基线（参考表）

> ⚠️ **本节只剩参考表**（硬件分配 / 限位 / 夹爪换算 / 寄存器表 / 错误码）。
> SDK 的四个坑搬去了 `LESSONS.md` §2.6；实测基线在 `TESTING.md` §2.7、§2.8；待验证清单在 `TESTING.md` §十。

### 2.1 硬件分配（与你实机一致，来源 `~/reBotArm_control_py/config/rebotarm_dm.yaml`）

| 关节 | SlaveID | 反馈 ID | 型号 | 减速比 | MIT kp/kd | POS_VEL: vel_kp/vel_ki/pos_kp/pos_ki | vlim |
|---|---|---|---|---|---|---|---|
| joint1 | 0x01 | 0x11 | 4340P | 1:40 | 120.0 / 8.0 | 0.0125 / 0.004 / 150.0 / 0.5 | 5.0 |
| joint2 | 0x02 | 0x12 | 4340P | 1:40 | 120.0 / 8.0 | 0.0125 / 0.004 / 150.0 / 0.5 | 5.0 |
| joint3 | 0x03 | 0x13 | 4340P | 1:40 | 120.0 / 8.0 | 0.0125 / 0.004 / 150.0 / 0.5 | 5.0 |
| joint4 | 0x04 | 0x14 | 4310 | 10:1 | 18.0 / 2.0 | 0.0008 / 0.002 / 70.0 / 1.0 | 3.0 |
| joint5 | 0x05 | 0x15 | 4310 | 10:1 | 18.0 / 2.0 | 0.0008 / 0.002 / 60.0 / 1.0 | 3.0 |
| joint6 | 0x06 | 0x16 | 4310 | 10:1 | 18.0 / 2.0 | 0.0008 / 0.002 / 60.0 / 1.0 | 3.0 |
| gripper | 0x07 | 0x17 | 4310 | 10:1 | 8.0 / 1.0 | 0.0008 / 0.002 / 50.0 / 1.0 | 3.0 |

总线：`/dev/ttyACM0`，适配器串口 **921600**（注意：这是 UART 侧，**CAN 侧固定 1Mbps**，由寄存器 0x23 `can_br` 配）。控制循环 500Hz。设备权限 `sudo chmod 666 /dev/ttyACM*`。

> 注意 reBot 的配置里**完全没有** `direction` / `offset` / `gear_ratio` 字段（两个 repo 全量 grep 0 命中）。所以 v0.1 里那套换算是纸上推演的复杂度，本层保留 `direction`/`offset` 是为了标定仿件的装配误差，默认值必须让"不标定即等于 reBot 行为"（`dir=1, offset=0`）。

### 2.2 限位与力矩（来源 URDF，**不在电机配置里**）

| 关节 | 位置限位 (rad) | URDF effort (N·m) | MoveIt 速度限 (rad/s) |
|---|---|---|---|
| joint1 | [-2.8, 2.8] | 27 | 1.0 |
| joint2 | [-3.14, 0] | 27 | 1.0 |
| joint3 | [-3.14, 0] | 27 | 1.0 |
| joint4 | [-1.87, 1.57] | 7 | 1.0 |
| joint5 | [-1.57, 1.57] | 7 | 1.0 |
| joint6 | [-3.14, 3.14] | 7 | 1.0 |
| 手指 | finger_left 限位 0.0285（URDF） | 8 | 0.08 |

**限位以 URDF 为唯一真源**（`~/reBotArm_control_py/urdf/DM/urdf/ReBot_Arm_DM.urdf`），配置里改为引用而非硬编码。

> ⚠️ **POS_VEL 模式下上位机无法限力矩**。力矩只受电机侧 0x03 `OC_Value`（过流）与 0x17 `TMAX` 约束。所以 `JointConfig.torque_max` 应注明"仅 MIT/力位模式生效"。reBot 也没有软件力矩保护——这是你相对蓝本可以补上的一处。

### 2.3 夹爪换算（线性，来源 `hardware_manager.py:14-15, 552-560, 594-599`）

```
_G_MAX_DIST_M = 0.10     # 全开 0.10 m
_G_ANGLE_OPEN = -5.0     # 全开对应 -5.0 rad

m → rad:  angle = max((distance / 0.10) * -5.0, -5.0)   # 即 angle = -50.0 * distance_m
rad → m:  distance = (pos / -5.0) * 0.10                # 即 m = -0.02 * pos
rad/s → m/s: 系数 0.10 / -5.0 = -0.02
夹持力: _G_DEFAULT_FORCE = 0.30，钳位到 [0.05, 1.5]
到位容差: 0.12 rad
```

即 **0.10 m 全开 ↔ -5.0 rad**，线性、与开度无关。这两个常数写进 `GripperConfig`。

> ⚠️ reBot 发布 `/joint_states` 里的 `finger_left` 时乘了 `0.5`（`ros_publishers.py:77-78`），于是最大发到 0.05 m，而 URDF 限位是 0.0285——**这是 reBot 的一个疑似不一致**，本层不要照抄。以米为准，缩放留给可视化层。

### 2.4 用到的寄存器（来源手册"寄存器列表及范围"）

| RID | 名称 | 读写 | 用途 |
|---|---|---|---|
| 0x00 | UV_Value | RW | 欠压保护值 |
| 0x01 | KT_Value | RW | 扭矩系数（电流↔力矩换算） |
| 0x02 | OT_Value | RW | 过温保护值，范围 [80, 200) |
| 0x03 | OC_Value | RW | 过流保护值，范围 (0, 1.0) |
| 0x04 | ACC / 0x05 DEC | RW | 梯形加减速 |
| 0x06 | MAX_SPD | RW | 最大速度 |
| 0x07 | MST_ID | RW | 反馈 ID（= 0x11~0x17） |
| 0x08 | ESC_ID | RW | 接收 ID（= 0x01~0x07） |
| **0x09** | **TIMEOUT** | RW | **通讯丢失保护时间 → 电机侧看门狗**。⚠ **单位是 50µs 计数周期**（1ms=20 计数），不是毫秒 —— 见 D4 |
| 0x0A | CTRL_MODE | RW | 控制模式，[0,4] |
| 0x15/0x16/0x17 | PMAX/VMAX/TMAX | RW | MIT 映射范围（**可回读，用它校准而不是硬编码**） |
| 0x19~0x1C | KP_ASR/KI_ASR/KP_APR/KI_APR | RW | **POS_VEL 的 PID，控制律本体** |
| 0x23 | can_br | RW | CAN 波特率代码 [0,4] |
| 0x3B | Imax | RO | 驱动板最大电流 |
| 0x3C | VBus | RO | 电源电压 → 欠压检查 |
| 0x3D/0x3E | Tpcb / Tmt | RO | 驱动板温度 / 电机温度 |
| 0x14 | Gr | RO | 实际减速比（想验证型号就读它：应为 10 或 40） |
| 0x50/0x51 | p_m / xout | RO | 电机侧位置 / **输出轴位置（本层用的就是它）** |

### 2.5 错误码（反馈帧 D[0] 高 4 位，来源手册:341-355）

| ERR | 含义 | 应对 |
|---|---|---|
| 0 | 失能 | 正常（未使能时） |
| 1 | 使能 | 正常 |
| 8 | 超压 | 检查电源，24V 版建议 ≤32V |
| 9 | 欠压 | 检查电源，建议 ≥15V |
| A | 过电流 | 检查 0x03 OC_Value 与负载 |
| B | MOS 过温 | 降载/散热 |
| C | 线圈过温 | 降载/散热 |
| **D** | **通讯丢失** | 总线/适配器问题，配合 0x09 排查 |
| E | 过载 | 机械卡阻或增益过高 |

> reBot 全程只用 `status_code == 0` 判"正常"，**没有任何位定义表**，`ArmStatus.error_codes` 更是永远发空数组。本层把这张表做出来就是净增量。

---

## 三、关键设计决策

### D1 换算只有 `dir` 与 `offset`，没有 `gear`

```python
def _joint_to_motor(self, v):  return (v - self.cfg.offset) * self.cfg.direction
def _motor_to_joint(self, v):  return v * self.cfg.direction + self.cfg.offset
```
`direction ∈ {+1, -1}`，`offset ∈ [-π, π]`。**注意 `_motor_to_joint` 必须与 `_joint_to_motor` 严格互逆**，用属性测试（`hypothesis` 或手写随机对拍）锁住这一点。

### D2 非阻塞发送 + 收发解耦（本层最核心的改动）

```
发：500 Hz 定时循环 → bus.send_pos_vel_batch({motor: (pos, vlim)})   # 只写串口，不等回包
收：100 Hz 定时循环 → bus.poll() → 解析缓冲 → 更新各 Motor 状态缓存
```
对应 reBot 的真机行为：500Hz 循环**只发不读**，反馈刷新跟着 `/joint_states` 的 100Hz 走（`hardware_manager.py:704-730`、`ros_publishers.py:61`），夹爪另有 50Hz 线程。

**串口超时必须调小**（建议 1~5 ms）。这是 D2 能成立的前提：`timeout=0.5` 会让每次读阻塞 0.5s。收帧改用 `in_waiting` 判断可读字节数，凑够完整帧才解析（SDK 的 `__extract_packets` 可复用）。

#### ⚠️ v0.4 更正：1:1 应答让上面这套解耦**不再是必需的**

实测（§2.7 第 ① 条）：**发一帧就必回一帧，不发就没有**。于是在**同一个**控制循环里
`写一帧 → 阻塞等它自己那条反馈` 是可行的，而且：

- **不需要 reBot 那条独立的 100 Hz 刷新帧**（`0x7FF` 广播）—— 控制帧自己就把反馈捎回来了；
- 反馈率从 100 Hz 白涨到 500 Hz（对 `check_health()` 和力矩保护都是净收益）；
- 控制循环里读到的位置/力矩是**本帧的**，不是 20ms 前的，跳变/超力矩判断才立得住。

**但代价是把收发写对**，两个必须同时做到（§2.7 第 ④ 条）：

| 必须 | 不做的后果 |
|---|---|
| 发**之前** `flush` 掉残留帧 | 每次读到的是**上一帧**的反馈，位置/力矩恒定滞后一个周期 → 安全判断建立在过期数据上 |
| 读要**自己阻塞**到 deadline（轮询 `in_waiting`），不能用 `read_all()` | `read_all()` 非阻塞，写到读完之间反馈还没回来就返回空 → 误判"收不到反馈"。真机首测就是这么在第 1.6s 停机的 |
| 尾部**残片必须留**（`RxBuf`） | 残片一丢，后续字节全部错位，凑巧成 `0xAA…0x55` 的 16 字节会被误认成帧 |

**本层取两者并存**：

- **默认（7 关节满载）**：仍用"只发不读 + 100Hz 抽干"—— 因为 7 路的反馈不是每路都需要同样高的频率，而抽干是**非阻塞**的，不占循环时间。
  **v0.5 更正：速率从 500 Hz 下调到 200~300 Hz/关节**（依据 §2.8 ④：实测天花板 3255 控制帧/s，7×500 = 3500 够不着）。
- **单路调试 / 需要逐帧确认的场合**（`dm_bringup.py jog --mit`、将来的 `RegisterTool`）：用"发一帧等一帧"，因为此时**正确性 > 吞吐**，而且能把每条命令的应答单独归因。

#### ~~⚠️ 带宽账：按帧长算，这条链路是**超载**的~~ → **v0.5：已实测，作废**

> **结论先行（2026-09-19 实测，详见 §2.8 第 ④ 条）**：下面这套按"标称 921600 = 92,160 B/s"
> 算出来的账**是错的**。CDC-ACM 的标称波特率纯属装饰，实测压到 **167% 标称容量
> （154 KB/s）**仍然跑通、1:1 应答 100.0% 不破。**所以"超载"不成立。**
>
> **但换来了一个更硬的约束**：真正的天花板是**帧率**，实测约 **3255 控制帧/s**
> （单适配器 + 这个 Python 循环）→ **7 关节 × 500 Hz = 3500 帧/s 刚好够不着**，
> 每关节上限约 465 Hz，而这还是**纯发帧**的账。
> → **D2 的默认速率应从 500 Hz 下调到 200~300 Hz/关节**（7×300×46B ≈ 96.6 KB/s，
> 在实测天花板内有充足余量），或分总线。
>
> 下面保留原文，作为"纸面推导错在哪"的记录。

适配器协议有两套帧长，别搞混（`DM_CAN.py:83-85` 与 `:545-549`）：

| 方向 | 帧长 | 依据 |
|---|---|---|
| 发送（主机→适配器） | **30 字节** | `send_data_frame` 共 30 个元素，`[2]=0x1e=30` 即长度字段；`[13][14]`=CAN ID，`[21:29]`=8 字节数据 |
| 接收（适配器→主机） | **16 字节** | `__extract_packets` 的 `frame_length = 16`；`[3:7]`=CANID，`[7:15]`=数据，`[15]=0x55` 尾 |

921600 8N1 → 上限 **92,160 B/s**。按 reBot 的实际负载（6 关节 @500Hz 发 + 100Hz 收，夹爪独立 50Hz 线程）：

```
发送: 6 × 30 B × 500 Hz = 90,000 B/s        ← 已占 97.7%
接收: 6 × 16 B × 100 Hz =  9,600 B/s
夹爪: (30 + 16) B × 50 Hz = 2,300 B/s
合计 ≈ 101,900 B/s  ≈  链路容量的 111%      ← 超了
```

**两种可能**：(a) 达妙 USB2CAN 是 CDC-ACM 设备，固件并不真的把 921600 当速率上限（USB 全速 12Mbps 远高于此），标称值只是形式；(b) reBot 本来就贴着上限在丢帧。

**旁证（2026-09-19 补）**：达妙官方 `USAGE.md:110` 自己写着「推荐在每帧控制完后延迟 2ms 或者 1ms，usb 转 can 默认有缓冲器没有延迟也可使用，但是推荐加上延迟」——官方推荐的每帧间隔就是 1~2ms，换算单电机上限 **500~1000 Hz**。

> **v0.5 对这条例的再判读**：这条旁证当时被我用来"支持超载担心"，但实测表明它是**另一回事** ——
> 它讲的是**官方推荐值**（保守的用法建议），不是**物理上限**。实测 3255 帧/s 远超它推荐的
> 500~1000 Hz/单电机。所以它**不能再当作"6×500Hz 会超载"的依据**。
> 但它有个新用途：它和实测天花板（3255 帧/s）**数量级一致**，两者互相印证"这台适配器
> 确实在 3 kHz 量级到顶"，而不是无上限。

**实测已完成（v0.5）**：`dm_bringup.py bandwidth` 压到 4000 Hz，结论见 §2.8 第 ④ 条与上面那个框。
退路按代价递增：① 夹爪并入主循环但降到 100Hz；② 4310 组降到 250Hz；③ **整体降到 200~300Hz**（当前推荐）；④ 改 SocketCAN（`can0` @1Mbps）绕开串口瓶颈。

### D3 模式：命令驱动 + 显式切换双轨

- 常规路径：由 `JointMotorCmd` 的 `use_*` 标志位**每条命令决定**（见 1.3 表），无粘性状态 → 上层不会因为忘了切模式而发错帧。
- 专家路径：`switch_mode(joint, mode)` 显式切换，对应 `CTRL_MODE` 寄存器 0x0A。
- **切换必须逐关节做**，且切完立刻发一条保持当前位置的命令（reBot 的做法，`hardware_manager.py:436-465`），否则整组切换期间电机会处于无命令状态。

### D4 双层看门狗

| 层 | 实现 | 作用 |
|---|---|---|
| 主机侧 | `Watchdog`（软，100ms） | 检测上层控制循环卡死 |
| **电机侧** | **寄存器 0x09 `TIMEOUT`（如 500ms）** | **主机崩溃/断线时电机自己退出使能** ← v0.1 缺的就是这层 |

上电时写入 0x09，并**回读确认**。注意 0x09=0 表示关闭保护，务必非 0。

#### ★ 单位不是毫秒：一个计数周期 = 50µs（已实测）

手册（`DM-J4310-2EC.md:698`、`DM-J4340P-2EC V1.1 .md:702`）原文：

> 表示多少个计数周期后仍未检测到 CAN 命令，进行电机保护，**一个计数周期为 50us**，
> 只在电机使能时生效

即 **1ms = 20 个计数**。所以「写 200」得到的是 **10ms** 的看门狗，不是 200ms ——
这不是"没生效"，是生效了一个小 20 倍的阈值，会在正常控制循环里疯狂误触发。
**换算规则只在 `dm_registers.Reg.per_unit` 定义一处**，CLI 层统一说毫秒、
同时打印原始计数。这个坑是两轮测试测废后才定位的（见下）。

#### 实测记录（2026-09-20，裸 4310，`tools/watchdog_test.py` + `tools/wd_probe.py`）

**A/B/C 对照（`watchdog_test.py --yes`，3/3 符合预期）：**

| 组 | 请求阈值 | 写入的原始计数 | 静默时长 | 静默后 ERR | 判定 |
|---|---|---|---|---|---|
| A 对照 | 0（关闭） | 0 | 1.2 s | `1` **仍使能** | ← 危险坐实 |
| B 阈值下侧 | 500 ms | 10000 | 0.3 s | `1` 仍使能 | ✓ 未到点 |
| C 阈值上侧 | 500 ms | 10000 | 1.2 s | `13` **已失能** | ✓ 过点触发 |

**独立复验（`wd_probe.py` 递增静默扫描，写 10000）：** 静默 50 / 100 / 200 / 400 ms
全部存活、**800 ms 触发**。

→ 有效阈值 ∈ **(400, 800] ms**，与请求的 500ms 吻合，语义 = "距最后一条 CAN 帧超过
阈值就退出使能"。**正反两面都拿到了，D4 定稿。**

**上线形态验证（`--save` 写 flash 之后，`wd_probe.py --mode default`）：**

| 步骤 | 结果 |
|---|---|
| `set --rid 0x09 --value 500 --commit --save` | 回读 10000 计数 ✓ |
| **电机 24V 断电重启** | 再读 0x09 = **10000 计数（500 ms）** ✓ flash 持久化成立 |
| `--mode default`（**全程不写 0x09**）：使能 → 保持 119 帧 → 静默 1.2s | **ERR=13 已失能** ✓ |

最后一行才是驱动真正依赖的那条链路：驱动**不会**在运行期去写 0x09（D4 约束第 2 条
不允许），它指望的是"电机一上电就带着保护"。上面这行证明了那个指望成立。
`--mode default` 与 `--mode sweep` 的区别就在这：sweep 证的是"写入路径 + 看门狗"，
default 证的是"**没有任何人写它**时保护依然在"。

> 🔴 **上表已失效 —— 2026-09-23 复测，`0x01` 读出 `0x09 = 0`。**
>
> **记录本身没做错**（它跨了断电验证，方法是对的）。失效的原因是**之后**发生的事：
> 调 ERR=13 时写了一次 `0x09 = 0` 想解锁，**那次写带了 `--save`**，flash 就被覆盖了。
> 判据是推理出来的：这期间 `0x01` 必然断过很多次电，RAM 早被冲掉，读出 0
> ⇒ flash 里也必须是 0 ⇒ 那次写一定落了盘。
>
> **留下的教训，三条：**
> 1. **`--save` 的验证必须跨一次断电**才算数 —— `--commit` 的回读 ✓ **完全不能**证明写进了 flash
>    （回读走的是 RAM，`--save` 根本不改变回读值）。上表「回读 10000 计数 ✓」那一行
>    **单独看是无效证据**，它是和下一行「断电重启后再读 = 10000 ✓」**合起来**才成立的。
>    **上表本身符合这条规矩，是后来者没有。**
> 2. **一次"失败"的实验照样会改持久状态。** "写 0x09=0 解不开 ERR=13"被记成了一无所获，
>    但它成功地、永久地拿掉了一个安全保护。**写 flash 的操作，成功失败都要记一笔。**
> 3. **`0x09` 有两个来源会让它变**（别人写的 flash、当前 RAM），**别信文档，上电就回读**。
>    `ARCHITECTURE.md` §「上电先看」第 1 条就是这个意思。
>
> **当前实况**：`0x01 = 750 ms`（2026-09-23 重新写入并跨断电验证通过）；
> `0x02`~`0x05 = 0`（当初那 500ms 只写了 RAM，被断电冲掉了）。

> **由此得到一条工具纪律**：任何清理/收尾代码**不能无条件写 `0x09 = 0`**。
> `wd_probe.py` 原来的 `finally` 就是这么写的（当成"还原成出厂默认"），
> 在 0x09 变成 flash 持久配置之后，这个动作会**把运行中的保护悄悄拆掉** ——
> 而 flash 里还是 500ms，于是表现为"配好了、但这次上电其实没保护"，
> 下次读寄存器还一切正常，极难发现。已改为"只在真改过它时还原成进来时的值"。

一个容易误读的细节：B/C 两组 `pos` 偏移都是 `+0.0000`。这**不是**"看门狗很温柔"，
而是因为被测的是一只裸电机、空载、且本来就静止（没有重力力矩要它扛）。
这里能确认的只有**失效方式是"松开"而不是"刹住"** —— 对真臂上的关节，
"松开"就意味着它在重力下直接掉下来。**这个危险在裸电机上演示不出来，别把
`+0.0000` 当成"所以没事"。** 要看到它，得等 7 关节满载（§十 第 8 条）。

#### 三条会影响驱动设计的实测约束

1. **任何发出去的帧都会重置计时器** —— 包括读状态用的 0x7FF 刷新帧。所以看门狗
   测试**不能轮询**（每次读 ERR 都清零计时器），只能"静默固定时长 → 单点观测"。
   反过来说，**正常控制循环只要按时发帧就永远不会触发**，这才是它安全的原因。
2. **写一次寄存器要 ~150ms，期间电机侧完全静默且无法压缩** → 阈值必须显著大于 150ms，
   且**运行期绝不能做寄存器 I/O**。正确做法：上电时 `set --save` 写进 flash，
   运行期不再碰。
3. **ERR=13 是锁存的** —— `enable(0xFC)`、连发 MIT 帧、写回 `0x09=0` 都清不掉，
   **只能电机断电重启**。所以看门狗实验每触发一次就要断一次电（USB-CAN 不用动）。

**取值建议**：主机侧控制循环 200~300Hz（D2），故 500ms ≈ 100~150 个控制周期的余量 ——
既容得下偶发的调度抖动，又远短于"人跑过去拔电源"的时间。**且必须 `--save` 写 flash**，
否则断电重启后看门狗又回到关闭状态，而人不会记得每次上电都去开它。

### D5 4340P 的型号与映射范围

1. 在 `DM_Motor_Type` 上加 `DM4340P = 15`，`Limit_Param` 追加一行。取值：`Q_MAX = 12.5`（同族），`DQ_MAX ≈ 6.0`（手册空载最大 56rpm = 5.86 rad/s），`TAU_MAX = 40`（手册峰值扭矩）。
2. **但不要信任这个硬编码值**：上电后读回电机自己的 0x15/0x16/0x17（PMAX/VMAX/TMAX），它们是 RW 且反映固件当前映射范围，**以回读值为准**，和配置不一致就报警。
3. 断言 0x14 `Gr` 应为 10（4310）或 40（4340P），用来兜底"型号填错"。

> 这三条合起来把 `DM_CAN.py` 硬编码表的隐患变成了自检项，是"吃透协议"的直接产出。

**v0.5 实测补充（重要 —— 别再把两套数混起来用）**

| 用途 | 用哪套数 | 4340P 的实际值 |
|---|---|---|
| **编解码 CAN 帧**（MIT 的 p/v/t 线性映射） | **`0x15/0x16/0x17` 回读值** | 12.5 / 10 / **28** |
| **动力学模型 / 重力补偿 / URDF 力矩上限 / 选型** | **手册物理值** | 额定 **12**、峰值 **40** N·m、空载 **5.86** rad/s |

两套数**本来就不同**（映射范围只需覆盖实际用量，不必等于物理极限）。v0.2 把二者当成
同一个量从而得出"SDK 表与手册矛盾"的结论，已在 [`LESSONS.md`](LESSONS.md) §2.6 坑 2 更正。

另外，**回读到 28 并不意味着电机只能出 28 N·m**（物理峰值 40），但**MIT 帧能命令的上限就是 28** ——
要 28 以上的力矩只能走 POS_VEL（固件自己闭环，不受帧映射限制）。
实测中的实际用量只有 0.7 N·m（TMAX 的 2.6%），离这个上限很远。

### D5b 重力补偿（D8）的 kp 必须逐关节实测，不能照抄 reBot

reBot 真机重力补偿路径硬编码 **`kp=7.0` / `kd=0.8`**（见 D8）。实测：

- 裸 **4310** 空载：kp=10 → 93% 幅值；kp=7 大约够用
- 裸 **4340P** 空载：kp=15 → 仅 **91%**、kp=25 → 94%、kp=60 → 97%

而残余误差上界 = 静摩擦/kp（§2.7 第 3 条），4340P 的摩擦（~0.62）是 4310（0.15）的约 4 倍，
**同样 kp 下 4340P 的残差也大约是 4 倍**。所以：

- `kp=7.0` 用在 4310 关节上勉强合适，**用在 4340P 关节（1~3 轴）上偏小**；
- 但**不等于要盲目加大 kp** —— MIT 的 kp 越大，对反馈延迟/丢帧越敏感，越容易振。
  而且重力补偿本来就是"前馈承担主要力矩、kp 只修残差"，kp 需要的量取决于前馈准不准。
- **做法**：把 kp 放进 `JointConfig` 逐关节配置，装臂后**逐关节实测标定**；
  在臂上测时注意 4340P 的摩擦 0.62 N·m 意味着**前馈力矩低于此值时该关节根本不会动**，
  所以前馈必须包含摩擦项，否则会出现"指令发了、关节不动"的死区。

### D6 状态量来源

| 量 | 来源 | 频率 |
|---|---|---|
| position / velocity / torque | 反馈帧（16/12/12 位定点，SDK 已解码） | 100 Hz |
| err | 反馈帧 D[0] 高 4 位 | 100 Hz |
| **temp_mos / temp_rotor** | **反馈帧 D[6] / D[7]（需自己解析）** | 100 Hz |
| vbus | 寄存器 0x3C（RO），慢速读 | 1 Hz 或按需 |
| enabled | 寄存器 0x0A 控制模式 / 反馈状态 | 变化时 |

### D7 PID 先读后写、可回滚

`cmd` 或标定脚本里提供：
```
dump_pid()   → 逐关节读 0x19/0x1A/0x1B/0x1C，打印并落盘（含时间戳）
apply_pid(cfg) → 逐关节 change_motor_param 写入
restore_pid(dump) → 从落盘文件回写
save()       → save_motor_param 存 flash（**只在确认后才调，flash 有写寿命**）
```
**写 RAM 与存 flash 必须分成两个动作**，调参阶段只写 RAM，改一次重启验证一次（符合 readme 的真机守则）。

### D8 重力补偿

```
进入：逐关节 disable → ensure_mode(MIT) → 立刻 send_mit(当前 q, dq=0, kp_hold, kd_hold, tau_g)
运行：500 Hz，tau = tau_g(q) + 积分项；积分上限 ±0.5，运动时 ×0.9 衰减
      速度门限：线速度 0.04 m/s、角速度 0.08 rad/s（由 pinocchio EE 雅可比算）
增益：kp=7.0 / kd=0.8（reBot 真机实测值，`hardware_manager.py:24-25`）
退出：逐关节切回 POS_VEL，切完立刻发保持位置命令
```
**不做渐变**：v0.1/我最初引用的 `transition_duration: 0.5` 来自 YAML，而 ROS 真机路径**不读这个键**。真机的安全手段是逐关节切换，不是渐变。`tau_g` 用 `reBotArm_control_py.dynamics.compute_generalized_gravity`。

**v0.4 实测补充（§2.7 第 ③ 条）——MIT 到底该用在哪：**

MIT 是上位机自己做 PD，所以**稳态残差有上界 摩擦/kp，压不到 0**（实测 5 档 kp 全部符合）。这直接划出了适用边界：

| 场合 | 用哪个模式 | 为什么 |
|---|---|---|
| 常规运动（轨迹跟踪） | **POS_VEL 固件闭环** | 电机内部有积分，稳态误差能到 0；且**不需要上位机出那 0.15 N·m 去顶摩擦** |
| 重力补偿 / 需要力矩前馈 | MIT + `tau_g` | MIT 是唯一能直接给力矩的模式；此时 kp 只负责"托住"，重力由 `t_ff` 承担，k 残差问题不突出 |

两个具体影响：

1. ~~**`kp=7.0` 是够用的**~~ → **v0.5 更正：对 4310 关节够用，对 4340P 关节偏小**。按实测上界 `残差 ≤ 摩擦/kp`，4310 在 kp=7 时残差上界 ≈ 0.15/7 ≈ **21 mrad**；4340P 是 0.62/7 ≈ **89 mrad**，大 4 倍。实测裸机跟踪：4310 在 kp=10 已 93%，4340P 要 kp=25 才 94%。**所以 kp 必须逐关节配（见 D5b），不能整个臂用一个 7.0。**
2. **摩擦这个数要进标定表**（`config/rebotarm_b601_mixed.yaml`）：**已测两种型号** —— 裸 4310 ≈ 0.145~0.159 N·m，裸 **4340P ≈ 0.53~0.72 N·m（均值 ~0.62）**；其余 5 个电机待测。它是重力补偿积分项的初值和整臂力矩阈值（4.3）的依据 —— 而且**仿件的摩擦很可能比原厂大**，这正是要逐台实测的理由。
3. **4340P 的 0.62 N·m 带出一个新风险：前馈死区**。重力前馈 `tau_g` 若低于该关节的静摩擦，关节**根本不会动**（不是慢慢动）。所以 1~3 轴的 `tau_g` 必须包含摩擦补偿项，或者靠 kp·误差 把差额补上 —— 而后者又要求 kp 足够大。这是装臂后最该先验的一件事（§十 第 9 条的直接后果）。

### D9 限位钳位的位置

在 `Joint.set_pos_vel` 入口钳位（软限位），并**同时**保留一个更保守的 `safe_margin`（如 0.02 rad），避免贴限位撞机械挡块。位置源是 URDF（2.2），配置里只做覆盖。

---

### D3 附：为什么目前是 MIT 而不是 POS_VEL

**这一轮改了一个原定的顺序**：原计划 `Joint` 先做 POS_VEL，实际**先做 MIT**。

原因是**只读实测**发现的：`CTRL_MODE`(0x0A) 在 0x01 与 0x04 上**都是 1 (MIT)**。
于是 MIT 成了唯一**零寄存器写入**就能跑的模式 —— POS_VEL 要先把 0x0A 写成 2，
而那是寄存器 I/O（`dm_registers.py --commit`），需要单独批准、单独复盘。
先做 MIT 等于把"第一次让电机转起来"和"第一次写寄存器"**解耦**，风险小得多。

> ⚠️ **不要以为 MIT 是退而求其次。** 它是唯一能**直接给力矩**的模式（`tau_ff`），
> 将来那 7 关节的重力补偿只能靠它。POS_VEL 的长处是稳态误差能到 0（固件闭环），
> 代价是**上位机无法限力矩**（详见 §2.2 的 ⚠️ 段）。两者是互补的，不是新旧关系。

---

## 四、类设计（修订后）

新增两个类：`MotorBus`（承担 D2 的非阻塞收发）与 `RegisterTool`（承担 D7）。

> **✅ v0.7 进展**：`MotorBus` 已实现（`src/DMmotor_driver/DMmotor_driver/dm_bus.py`）。
> 与下面这张类图有两处**有意的偏差**，都不是遗漏：
> 1. `send_frame(frame)` 只收已拼好的 30 字节帧，**不再单独传 motor_id** ——
>    CAN ID 已经在帧的 `[13:15]` 里了，传两个来源的 ID 就有不一致的机会。
> 2. 多了一组原类图里没列的方法：`send_enable` / `send_disable` / `send_refresh` /
>    `send_and_wait` / `flush` / `stats`。前三个是 `Joint`/`DmArm` 必然要调的下层动作
>    （类图上的 `Joint.enable()` 总得落到某条帧上）；`send_and_wait` 是 D2 明确要求的
>    "发一帧等一帧"那条路；`stats` 给 1:1 校验用。
>
> 另外 `cache` 的值类型定为 **`MotorState`（电机侧原始量）**，与 §4.1 的 `JointState`
> （关节侧、经 dir/offset 换算）**是两个类型，不要合并** —— 这一轮不实现 `JointState`。

```mermaid
classDiagram
    class MotorBus {
        -Serial serial
        -dict~int, Motor~ motors
        -dict~int, MotorState~ cache
        +open(channel, baud, timeout)
        +close()
        +send_pos_vel(motor_id, pos, vlim)
        +send_pos_vel_batch(dict)
        +send_mit(motor_id, kp, kd, q, dq, tau)
        +send_frame(motor_id, bytes)   «非阻塞，唯一出口»
        +poll()                        «解析缓冲，更新 cache»
        +get_state(motor_id)
    }
    class DmArm {
        -MotorBus bus
        -dict~str, Joint~ joints
        -ArmConfig config
        -Watchdog watchdog
        +connect() / shutdown()
        +enable_all() / disable_all() / safe_home()
        +set_joint_positions(targets, vlim)
        +set_joint_mit_all(...)
        +set_gripper(m) / get_gripper_position()
        +get_state() / refresh_all_states()
        +start/stop/get_gravity_compensation()
        +check_health() / emergency_disable()
        +run_control_loop(hz, fn)      «500Hz 发»
        +run_feedback_loop(hz, fn)     «100Hz 收»
    }
    class Joint {
        -MotorBus bus
        -Motor motor
        -JointConfig cfg
        +enable() / disable()
        +set_pos_vel(pos, vlim)
        +set_mit(kp, kd, q, dq, tau)
        +set_force_pos(pos, vel, current)
        +get_state() JointState
        +switch_mode(mode)
        -_joint_to_motor(v) / _motor_to_joint(v)
        -_clamp(pos)
    }
    class RegisterTool {
        +dump_pid(joints) dict
        +apply_pid(cfg, dry_run)
        +restore_pid(dump_file)
        +verify_mapping(joint) «读 0x14/0x15/0x16/0x17»
        +set_motor_timeout(joint, ms)
    }
    DmArm "1" *-- "1" MotorBus
    DmArm "1" *-- "1..*" Joint
    Joint "1" *-- "1" JointConfig
```

### 4.1 JointState（修订）

```python
@dataclass
class JointState:
    name: str
    position: float        # rad（输出轴，无减速比折算）
    velocity: float        # rad/s
    torque: float          # N·m
    err: int               # 反馈帧 ERR 半字节，用 2.5 的表解码
    temp_mos: float        # ℃  反馈帧 D[6]
    temp_rotor: float      # ℃  反馈帧 D[7]
    vbus: float | None     # V   寄存器 0x3C，未读则为 None
                           # ⚠️ v0.9 实况：**恒为 None** —— 本层不做寄存器 I/O（§六），
                           # 要它得等 RegisterTool 在**上电自检**阶段读一次再塞进来
    enabled: bool
    timestamp: float
```

### 4.2 JointConfig（修订）

```python
@dataclass
class JointConfig:
    name: str
    motor_type: str                    # "4340P" / "4310"，映射到 DM_Motor_Type
    slave_id: int
    master_id: int
    direction: int = 1                 # ±1，标定得出
    offset: float = 0.0                # rad，标定得出
    position_min: float | None = None  # None = 从 URDF 取
    position_max: float | None = None
    velocity_max: float = 1.0          # 来自 MoveIt 限速
    # --- POS_VEL（本机唯一实际生效的控制律）---
    pos_kp: float = 150.0
    pos_ki: float = 0.5
    vel_kp: float = 0.0125
    vel_ki: float = 0.004
    vlim: float = 5.0
    # --- MIT ---
    mit_kp: float = 120.0
    mit_kd: float = 8.0
    # --- 力矩（POS_VEL 下 torque_max 无效，靠 monitor 兜底）---
    torque_max: float = 12.0           # 仅 MIT/力位模式生效
    torque_monitor: bool = True        # POS_VEL 下额外监控（上位机唯一的力矩保护手段）
    torque_monitor_threshold: float = 15.0
    torque_monitor_count: int = 10     # 连续 N 次越限才急停，采样率 = feedback_hz
    # --- 安全 ---
    motor_timeout_ms: int = 200        # 寄存器 0x09
    def __post_init__(self): ...       # 校验 direction∈{±1}、min<max、timeout>0、按 motor_type 校验增益档位与力矩档位
```

> `__post_init__` 里建议加一条**型号-增益一致性检查**：`4340P` 的 pos_kp 应在 100~200，`4310` 应在 50~100；明显越界就报错。防止复制粘贴配置时串了档。

### 4.3 力矩阈值必须按型号分档

**不能全套关节用同一组数**——4310 的峰值只有 12.5 N·m，若阈值定 15 则永远不触发（等于没有保护）：

| 关节 | 型号 | 电机额定 | 电机峰值 | URDF effort | `torque_max`（MIT 用） | `monitor_threshold` |
|---|---|---|---|---|---|---|
| joint1-3 | 4340P | 12 | 40 | 27 | 12.0 | 15.0 |
| joint4-6 | 4310 | 3.5 | 12.5 | 7 | 3.5 | 5.0 |
| gripper | 4310 | 3.5 | 12.5 | 8 | 1.5 | 1.8 |

`__post_init__` 增加第二条一致性检查：`monitor_threshold` 必须 < 该型号峰值扭矩，否则报错（"这个阈值永远不会触发"）。

> **采样率绑定**：`torque_monitor_count` 按 `feedback_hz`（100 Hz）计数，10 次 = 100 ms 连续越限。**不要放在 500 Hz 发帧循环里数**，那样 10 次只有 20 ms，加速度峰值就会误触发。越限判定用反馈帧的 `torque` 字段（电机由电流估算，带噪声，所以需要 count 防抖）。

`GripperConfig` 在 v0.1 基础上增加 `m_per_rad = -0.02`（等价 `rad_per_m = -50.0`）、`arrive_tol_rad = 0.12`、`default_force = 0.30`、`force_range = (0.05, 1.5)`，并去掉 `open_position/close_position` 的裸值改为 `0.10 / 0.0` 配常数校验。

`SafetyConfig` 增加：`motor_timeout_ms`、`temp_warn/temp_fault`（建议 80/100，手册建议线圈不超 100℃）、`vbus_min/vbus_max`（24V 版建议 ≥15V、≤32V）、`feedback_hz`。

---

## 五、数据流（修订）

```mermaid
sequenceDiagram
    participant C as 上层(ROS驱动层)
    participant A as DmArm
    participant J as Joint
    participant B as MotorBus
    participant M as 电机

    Note over A,B: 发：500 Hz
    C->>A: set_joint_positions({joint1: 0.5}, vlim=1.0)
    A->>A: check_health() → 钳位 → watchdog.feed()
    A->>J: set_pos_vel(0.5, 1.0)
    J->>J: (0.5 - offset) * dir  →  0.5 rad 电机侧
    J->>B: send_pos_vel(motor_id, 0.5, 1.0)
    B->>M: CAN 帧 (0x100+ID, float32 pos, float32 vlim)   «写串口即返回，不等回包»

    Note over A,B: 收：100 Hz
    A->>B: poll()
    B->>M: 读缓冲
    M-->>B: 反馈帧 (ERR|POS|VEL|T|T_MOS|T_Rotor)
    B->>B: 更新 cache（含温度两字节）
    C->>A: get_state()
    A-->>C: {joint1: JointState(pos=0.5, err=1, temp_rotor=42, ...)}
```

---

## 六、YAML 配置（修订示例，实际文件 `config/rebotarm_b601_mixed.yaml`）

```yaml
channel: /dev/ttyACM0
baudrate: 921600          # USB-CAN 适配器串口侧；CAN 侧固定 1Mbps
serial_timeout: 0.003     # 关键：必须远小于 1/500Hz
send_hz: 500
feedback_hz: 100
default_mode: POS_VEL
urdf_path: ../../mujoco_pkg/description/ReBot_Arm_DM.urdf   # 限位真源（v0.11 更正：third_party 下只有厂商例程）

joints:
  joint1:
    motor_type: "4340P"      # 非 DM4340！减速比 1:40
    slave_id: 0x01
    master_id: 0x11
    direction: 1             # 标定得出
    offset: 0.0              # 标定得出
    pos_kp: 150.0
    pos_ki: 0.5
    vel_kp: 0.0125
    vel_ki: 0.004
    vlim: 5.0
    mit_kp: 120.0
    mit_kd: 8.0
    torque_max: 12.0                 # 4340P 额定
    torque_monitor: true
    torque_monitor_threshold: 15.0
    torque_monitor_count: 10         # × feedback_hz(100) = 100ms 防抖
    motor_timeout_ms: 200
  # joint2/joint3 同上；joint4~joint6 为 4310 档
  #   pos_kp: 70/60/60, pos_ki: 1.0, vel_kp: 0.0008, vel_ki: 0.002, vlim: 3.0
  #   mit_kp: 18.0, mit_kd: 2.0
  #   torque_max: 3.5, torque_monitor_threshold: 5.0   ← 4310 峰值仅 12.5，别沿用 15

gripper:
  motor_type: "4310"
  slave_id: 0x07
  master_id: 0x17
  direction: 1
  offset: 0.0
  m_open: 0.10             # 全开 0.10 m
  rad_open: -5.0           # 对应 -5.0 rad
  default_force: 0.30
  force_min: 0.05
  force_max: 1.5
  arrive_tol_rad: 0.12
  torque_max: 1.5                  # 夹爪限力
  torque_monitor: true
  torque_monitor_threshold: 1.8    # 抓空/夹住时最先触发的保护

safety:
  watchdog_timeout: 0.1
  motor_timeout_ms: 200
  temp_warn: 80.0
  temp_fault: 100.0
  vbus_min: 15.0
  vbus_max: 32.0
```

---

## 七、标定工作流（新增）

你的硬件是**仿件**，装配零位/方向可能与 reBot 不同，这一步不能省。做成可重复执行的脚本，每一步都要求人工确认后才继续。

| 步 | 动作 | 安全措施 | 产出 |
|---|---|---|---|
| 0 | 只连一个电机（如 joint4/4310，力矩小） | 电机空载、固定好 | 总线通 |
| 1 | 读 0x14 `Gr`、0x15/0x16/0x17、0x19~0x1C 并落盘 | 只读，不动 | `dump_<date>.yaml` |
| 2 | `enable` → 低速点动 ±0.2 rad（`vlim=0.3`） | 全程手放在急停/电源上 | **direction** 判定 |
| 3 | 读回 0x51 `xout`，确认"指令 +0.2 rad → 反馈也 +0.2" | 若反向则 direction=-1 | direction 定值 |
| 4 | 手动把关节推到机械零位（或对准标记），记 `xout` | 先 `disable` | **offset = -xout·dir** |
| 5 | 写入 offset，重跑步 2-3 复核 | —— | offset 定值 |
| 6 | 逐关节重复 3-5 | 一次只动一个关节 | 全部 offset |
| 7 | 读 0x09，写入 500ms（=10000 计数，见 D4），**`--save` 写 flash**，回读确认 | 只写这一个 | 电机侧看门狗生效 |
| 8 | 7 关节低速联动到 URDF 零位，对比 `safe_home` | 速度 ≤0.5 rad/s | 整臂可用 |

**标定脚本必须支持 `--dry-run`**：打印将要写入的寄存器与值，不实际写。

---

## 八、上电 / 使能 / 失能 / 回零序列（含 reBot 实测延时）

```
connect:  开串口(timeout=3ms) → 建 Motor 对象 → 读 0x14/0x15/0x16/0x17 校验型号 → 断言 Gr∈{10,40}
enable:   逐关节 enable → sleep(0.05)/个
          → 逐关节 写 POS_VEL PID 寄存器(sleep 0.02) → ensure_mode(POS_VEL, timeout=1000) → sleep(0.05)/个
          → 组尾 sleep(0.2)
          → 写 0x09 = 500ms（=10000 计数，D4：单位是 50µs）→ 回读确认
          → 启动 500Hz 发帧循环 + 100Hz 收帧循环
recover:  若反馈出现 D(通讯丢失)/8/9/A/B/C/E → 按 2.5 表分类处理；8/9 检查电源，A/B/C/E 降载或急停
safe_home: 最小 jerk 轨迹 10s³-15s⁴+6s⁵，50Hz 流式下发
          max_vel=0.5, 到位阈值=0.01 rad, 超时 15s, 到位后再保持 3s
          已在 0.01 rad 内则直接返回；夹爪单独回零
set_zero: 仅用于恢复出厂零位 —— disable → sleep(0.3) → 逐关节轮询 status_code==0（200×0.05 上限）
          → set_zero_position → sleep(0.1)   ⚠️ 会改电机内部零位，标定前不要用
disable:  停循环 → 清零/保持 → 逐关节 disable → sleep(0.05)/个
shutdown: 停循环 → disable → sleep(0.5) → 逐关节 shutdown → sleep(0.1) → 关串口
```

---

## 九、实现优先级（修订）

| 优先级 | 模块 | 说明 |
|---|---|---|
| ~~**P0**~~ | ~~`MotorBus` 非阻塞收发~~ | ✅ **已完成（v0.7）**：`dm_bus.py` + `dm_frames.py`（协议原语下沉）+ `tools/smoke_dm_bus.py`。含串口 timeout=3ms、非阻塞 `poll()`、原子 `send_and_wait()`、按电机注册映射范围 |
| ~~**P0**~~ | ~~`JointConfig` / `ArmConfig`~~ | ✅ **已完成（v0.11）**：`arm_config.py`（类与校验，零 yaml 依赖）+ `config/rebotarm_b601_mixed.yaml`（真数据）。含 PID 字段、型号↔增益/力矩档位校验、URDF 限位解析、`check_mapping_range` 回读对拍。单测 19 组、变异 18/18。**偏离 §4.2 之处**（夹爪 PID 字段、`torque_*` 不写进 YAML 而按型号派生、`friction` 改区间、删 `gear_ratio`/`pmax`/`vmax`/`tmax`） |
| ~~**P0**~~ | ~~`Joint` 换算 + 钳位~~ | ✅ **已完成（v0.9）**：`joint.py` + `tools/smoke_joint.py`（55 项）。含 dir/offset 换算、PMAX 硬钳位 + 软限位、静默饱和告警、`assert_healthy()` 先失能再抛。**MIT 可用**；`set_pos_vel` / `set_force_pos` / `switch_mode` 是预留桩（见 §44-53）。⚠️ 尚未与真机联调 |
| **P0** | `RegisterTool.dump_pid` / `verify_mapping` | D7 的"先读"，无风险 |
| **P1** | `RegisterTool.apply_pid` / `restore_pid` | D7 的"后写"，带 dry-run |
| **P1** | 电机侧看门狗（0x09） | D4。**现状：只有 `0x01` 有（750ms，2026-09-23 重写 flash 并跨断电验证）；`0x02`~`0x05` 全为 0**，那 4 台当初只写了 RAM，已被断电冲掉。**注意"未做"不等于"现在有风险"**：这 5 台全是失能状态，没有出力路径；看门狗只在**你自己写了控制循环之后**才是兜底 —— 所以这项**和 D3 一起做**，不是它的前置。另：`--save` 是不可逆的持久化改动，逐台单独确认；触发过一次 ERR=13 后**必须断电**才能继续 |
| **P1** | 标定脚本（第七节） | 仿件必需 |
| **P1** | `JointState` 全字段（含温度解析） | D6 |
| **P1** | 错误码解码表 | 2.5 |
| **P2** | `check_health` / `emergency_disable` | 主机侧看门狗 + 分级检查 |
| **P2** | 重力补偿 | D8，依赖 pinocchio |
| **P2** | `set_force_pos` | 夹爪限力 |
| P3 | 异步接口、多总线 | 预留 |

---

---

## 十一、已确定的决策（2026-09-19）

| 项 | 决定 |
|---|---|
| 夹爪关节名 | 本层内部用 **`gripper`**，`gripper ↔ finger_left` 的翻译放在 ROS 层 |
| 力矩保护 | **软限 + 监控双轨**：`torque_max`（仅 MIT/力位生效）+ `torque_monitor`/`_threshold`/`_count`（POS_VEL 下生效）。阈值**按型号分档**，见 4.3 |
| 标定脚本 | **并入本包**（entry_points，不单独成包） |
| bring-up 脚本 | **要**，且**刻意不依赖封装层**（直连 `DM_CAN.py`），先验证协议与硬件，其结论反过来校正封装层设计 |

### 11.1 entry_points（本包对外命令）

```python
entry_points={
    'console_scripts': [
        'dm-bringup = DMmotor_driver.dm_bringup:main',     # 单电机上电验证（先做）
        'dm-calibrate = DMmotor_driver.dm_calibrate:main', # 第七节标定流程
        'dm-dump-registers = DMmotor_driver.dm_registers:main',  # PID/映射范围读写（先读后写）
    ],
}
```

### 11.2 bring-up 脚本的范围（`dm_bringup.py`）——**已实现（2026-09-19）**

代码：`DMmotor_driver/dm_bringup.py`。刻意站在封装层**之外**，只依赖 `DM_CAN.py` + `pyserial`，用来把第十节里最关键的几条实测掉：

| 子命令 | 作用 | 风险 |
|---|---|---|
| `read`（默认） | 读 0x14 `Gr`（验型号）、0x15/0x16/0x17（定 MIT 映射表，**并用回读值覆盖 `Limit_Param`**）、0x19~0x1C（存 PID 现状）、0x09/0x07/0x0A、0x3C/0x3D/0x3E（**顺便测裸 int RID 能否读到**） | 只读 |
| `monitor` | 按 feedback_hz 刷新状态（含 D[6]/D[7] 温度，自己解帧），统计丢帧率 | 只读 |
| `jog` | 单电机低速点动正弦 ±0.2 rad，打印"命令 → 30 字节发送帧 → 16 字节反馈帧" | 需 `--yes` |
| `bandwidth` | 按 `--hz` 硬发 N 秒，量实际速率/字节数/**单帧写延迟**；`--enable` 才是完整闭环 | 默认只发不动；`--enable` 需 `--yes` |

默认目标 **joint4（0x04/0x14，4310）**——力矩最小的一档。

落地时的四条补充约定：

1. **全程不写任何寄存器**（含 `CTRL_MODE`），**永不调用 `set_zero_position`(0xFE)** —— 零位/方向是 `dm-calibrate` 的事。`read` 若发现 `CTRL_MODE != 2` 只提示、不自动改。
2. **`--dry-run` 不需要 `pyserial`、不需要硬件**，只打印帧结构。所以「协议长什么样」这件事在你插上电机之前就能看。
3. `jog` 刻意走**厂商 API**（`control_Pos_Vel`）而不是自己构帧 —— 先证明厂商这条路本身是通的，自己那套非阻塞收发是 `MotorBus` 的活。代价是走这条路拿不到温度（`recv()` 把 D[6]/D[7] 吃了），所以 `jog` 的温度只在使能前采样一次，循环里明确不留温度列。
4. 退出路径（含 Ctrl-C）统一 `finally`：回 p0 → `disable`；失败也会打印「请直接断电」。

配套冒烟测试：`tools/smoke_dm_frames.py`（**不需要硬件**）。它把帧构造/切帧/反馈解码三项与**厂商 SDK 自己的实现逐字节对拍**——因为这三处都是"写错了只会表现为电机不动、读数全错"的协议层代码，自己验自己没有意义。

---

## 十二、和其他模块的关系（修订）

| 模块 | 关系 |
|---|---|
| `DM_CAN.py`（vendored） | **被调用**，但收发路径要绕过它的阻塞便利方法（D2） |
| reBot 的 `motorbridge` | **不使用** |
| `reBotArm_control_py` 的 kinematics/dynamics | **复用**（FK/IK/重力项），仅 actuator 层被本层替代 |
| ROS 2 驱动层 | 调用本层；接口需求见 1.3，**另开文档** |
| MuJoCo 仿真 | 无关，但单位/关节名要对齐（rad、`gripper`↔`finger_left`） |
| `Fake Driver` | 同接口的另一实现，用于仿真；本层是它的真机对应物 |

---

> **一句话**：`MotorBus`（非阻塞收发，500Hz 发 / 100Hz 收）+ `Joint`（无减速比折算的 `dir`/`offset` 换算 + 钳位）+ `DmArm`（批量 + 双层看门狗）+ `RegisterTool`（先读后写可回滚）+ 标定流程。
> 三处最大的修正：**删掉 gear_ratio**、**不能用 `control_Pos_Vel`**、**补上电机侧看门狗与温度/错误码**。
