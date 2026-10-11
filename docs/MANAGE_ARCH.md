# ros2_control 管理层架构（arm_manage）

> 状态：**架构设计 + 实现计划**。进度：**M9.0**（`arm_motion::MotionClient` 执行件，已落地并真机验证）、
> **M9.1**（`arm_msgs` 的 5 srv + 1 msg）、**M9.2**（插件运行期使能 + 状态话题，**已真机验过**）**已完成**；**M9.3 起未实现**（`arm_manage` 节点、总 launch）。
> 与其它文档的分工：**状态与不变量**看 [AGENTS.md](../AGENTS.md)，**实测值**看 [TESTING.md](TESTING.md)，
> **踩坑**看 [LESSONS.md](LESSONS.md)，**运动接口层**（`arm_motion`，未来）看 [MOTION_ARCH.md](MOTION_ARCH.md)，
> **电机驱动层为什么**看 [DESIGN.md](DESIGN.md)。本文只讲**管理层**。
> 决策编号 `M-1`…`M-10` 供代码注释引用（与 DESIGN 的 `D1…D9`、MOTION_ARCH 的 `D-M1…D-M8` 同一用法）。

---

## 一、定位与边界

### 1.1 一句话

`arm_manage` 是**管这条链本身**的那一层：谁使能、谁能发命令、出事怎么办、以及把这两侧的
状态汇总成一份对外真相。它**不产生运动**（那是 `arm_motion` 与 MoveIt 的事）。

### 1.2 与 ros2_control 已有部件的关系（**先读这张表，别重造轮子**）

| 角色 | 是什么 | 有 ROS 接口吗 | 谁负责 |
|---|---|---|---|
| `controller_manager`（CM） | **节点**（`ros2_control_node` 里跑的就是它） | 有：`/controller_manager/switch_controller`、`load/configure/unload_controller`、`set_hardware_component_state`、`list_*` | ros2_control 已有，**不重写** |
| `ResourceManager` | **CM 内部的一个 C++ 类**，不是节点 | **没有**自己的接口（能力都经 CM 的服务暴露） | ros2_control 已有，**不重写** |
| 硬件组件插件 | `DmSystemInterface` | 无（只实现生命周期 + 导出接口） | **我们的**，本轮要**加运行期接口** |
| 控制器插件 | `joint_trajectory_controller`(JTC) | 有（action） | 上游的，**不改** |
| **`arm_manage`** | **节点** + 两个内部模块 | 新加 3 个服务 + 1 个话题 | **本层** |

⚠️ **命名纪律**：本层**不叫** ControllerManager / ResourceManager —— 那两个名字在 ros2_control 里
已经被占了，重名会让后来者以为我们在替换它们。本层的两个内部模块叫 **`ControllerGate`**（控制器侧）
与 **`HardwareGate`**（硬件侧）。

### 1.3 为什么必须自己加运行期使能接口（M-1）

`enable_on_activate` 与 `gravity_ff` 都是**硬件组件的 `<param>`**（来自 robot_description，构建/launch 期定死）：

- `dm_system_interface.cpp:234` 读 `enable_on_activate`，`:151` 用 `gravity_ff_` 决定 `mode`；
- `0x0A` 只在 `on_activate` 里写（`:421`）；
- **`write()` 在 `!enable_on_activate_` 时什么都不发**（`:649`，整个函数体就这一条早返回）；
- **`read()` 里还有三处**同一个判据：`:594`/`:617`（有电机故障时"报 ERROR 中断"还是"只告警继续"）
  与 `:636`（**只读时主动发 `0x7FF` 刷新帧**——因为电机不主动报状态）。

⇒ 引入 `armed_` 后，**这四处都要跟着改**（见 §5.1）；其中 `:636` 的分支正好就是 M-5 要的
"失能后只发刷新帧"，等于**白捡**。

`set_hardware_component_state`（CM 现成服务）**改不了已加载的参数** ⇒ **"只读 → 使能"在运行期不可切换**。
所以要"启动只读、之后不重启就使能"，只能在插件里**新增一个运行期通道**（M-1）。
（运行期 **MIT ↔ POS_VEL 切换不做**：它同样卡在参数上，你已否决，见 §十二。）

### 1.4 本层做什么 / 不做什么

| 做 | 不做 |
|---|---|
| 编排使能/失能/急停的**顺序**与前置条件 | 不做运动学 / 轨迹 / 规划（→ `arm_motion` / MoveIt） |
| 编排**控制器状态**与**硬件状态**的先后（这是本层唯一不可替代的价值） | 不碰协议 / 字节 / 换算（→ `dm_hardware`） |
| 汇总并发布**一份对外状态** | 不做碰撞检测 / 安全认证层 / 电压监控 |
| 单例与互斥纪律（mock / 真机 / 演示脚本只能开一套） | 不做运行期模式切换（M-1 已排除） |

---

## 二、决策索引

| # | 决策 | 详见 |
|---|---|---|
| **M-1** | 插件新增**运行期使能/失能**通道（`get_node()` 起服务与发布者），因为 `enable_on_activate` 是构建期参数 | §1.3 / §五 |
| **M-2** | 管理层 = **一个节点** + 两个内部模块 `ControllerGate` / `HardwareGate`（顺序必须在一个状态机里，不能拆进程） | §3.1 |
| **M-3** | **控制器必须先 inactive 才能碰硬件**：寄存器 I/O 会让 `update()` 掉几帧（**实测约 70 ms**，2026-10-11） | §3.2 |
| **M-4** | 使能序列在 `write()` 里按**状态机逐周期推进**（每周期最多一次寄存器 I/O），期间**不发任何控制帧** | §5.2 |
| **M-5** | 失能之后**只发 `0x7FF` 刷新帧**（回到"只读"语义），不给已失能的电机发控制帧 | §5.3 |
| **M-6** | 对外只有 **3 个服务**（`arm_on` / `safe_disarm` / `emergency_stop`），**不做状态查询服务**；插件那 2 个是**内部接口** | §4.1 |
| **M-7** | 状态用**两个话题、两个 msg、两个发布者**（插件发硬件侧，管理层发聚合），都用 latched QoS | §4.2 |
| **M-8** | `SafeDisarm` 收臂失败 ⇒ **重试 N 次，仍失败就保持使能停在原地并报错**（不拿"收臂失败"当理由扔掉臂） | §6.2 |
| **M-9** | 急停 = 取消进行中的目标 + **立即失能**；不碰寄存器、永不抛、**不做恢复** | §6.3 |
| **M-10** | 折叠位（park）真源放 `arm_manage/config/`；**现在不建**通用命名位姿表（只有它一个消费者） | §八 |

---

## 三、分层、顺序与上下文

### 3.1 结构

```
                    ┌──────────────── arm_manage（1 个节点，持有唯一状态机）────────────────┐
  服务 /arm_manage/ │  ControllerGate（控制器侧）          HardwareGate（硬件侧）          │
  arm_on            │   · switch_controller                · /arm/set_enabled             │
  safe_disarm       │   · 控制器状态查询(CM 服务)           · /arm/emergency_stop          │
  emergency_stop    │   · 取消进行中的轨迹目标               · 订阅 /arm/hardware_status    │
                    │   · MotionClient（发轨迹给 JTC）      · 前置条件检查                 │
                    └───────┬──────────────────────────────────────┬─────────────────────┘
                            │ /controller_manager/*                │ /arm/*
                            ▼                                      ▼
                     controller_manager                    DmSystemInterface（插件）
                     （控制器生命周期 + 硬件生命周期）          （串口 / 电机 / 0x0A / ERR / 温度）
                                                                    │
                                                            （另一条进程的边界：串口独占）
```

### 3.2 顺序依赖（M-3，本层的核心不变量）

```
  ① ControllerGate.deactivate()          控制器先停：没人再往 JTC 发目标
        ↓
  ② HardwareGate.enable()                插件状态机：写 0x0A（逐台，**实测整段约 70 ms**）→ 使能帧 → 真实 MIT 保持帧
        ↓                                ⚠️ 这段里 CM 的 update() 会掉几帧（实测 ~70 ms）
  ③ 校验 /joint_states 有 6 关节、ERR ∈ {0,1}
        ↓
  ④ ControllerGate.activate()            现在才允许发轨迹
```

**为什么不能颠倒**：②的寄存器 I/O 会让 `update()` 空档约 70 ms；此时若 JTC 还 active，
它会看到"更新中断"，行为取决于它的容差判据（[陷阱 #40](AGENTS.md)：JTC 的 `goal_time=0` 时
超时分支走不到 ⇒ 会无限挂住而不是干脆 ABORT）。

### 3.3 执行上下文（每段代码跑在哪）

| 环节 | 上下文 | 约束 |
|---|---|---|
| 管理层服务回调 | executor 回调 | ⚠️ **做不到「只做常量时间的事」**（见下）：rclcpp 的 srv **不能延迟回复** —— 回调返回时响应就发出去了 ⇒ 选 A 方案（响应语义真实）就必须**在回调里等状态机完成**。实现 = **MultiThreadedExecutor** + 服务与 tick 分属**不同回调组**（否则回调等 tick、而 tick 进不来 = 陷阱 #57） |
| 管理层 tick | 定时器（20 ms） | 每 tick 推进一步、立刻返回；**不许 sleep / 不许阻塞等 future**（[陷阱 #57](AGENTS.md)）。**回调组必须与服务分开**，否则服务回调一阻塞就把 tick 饿死 |
| 发轨迹给 JTC | tick 内 `async_send_goal` | 非阻塞 future（`MotionClient`） |
| 等待到位 | tick 内 `future.wait_for(0)` | 非阻塞轮询；超时走 `async_cancel_goal`（[陷阱 #60](AGENTS.md)） |
| 插件使能状态机 | `write()` 里推进 | 每周期最多一次寄存器 I/O；**期间不发控制帧** |
| 插件状态发布 | `read()` 里每 10 周期 | 100 Hz ⇒ **10 Hz**；**不要**放 `write()` |

---

## 四、接口契约

### 4.1 服务（5 个：插件 2 个内部 + 管理层 3 个对外）

```rosag
# ── 插件级（内部接口：正常只由 arm_manage 调用）──
# arm_msgs/srv/SetEnabled.srv
bool enable
---
bool success
bool now_armed
string message            # 拒绝时说明原因（例如"当前有电机处于使能态" / "硬件组件未 active"）
```

```rosag
# arm_msgs/srv/HardwareEmergencyStop.srv   ← 名字带 Hardware 以免与管理层的同名
---
bool success              # 只要"尽力失能"这一轮没抛就是 true
string[] disabled
string[] failed           # 失能失败的关节 + 原因
string message            # ERR=13（通讯丢失，锁存）等要明确写"需要断电重启，本服务不做恢复"
```

```rosag
# ── 管理层级（对外接口）──
# arm_msgs/srv/ArmOn.srv
bool with_controller      # true ⇒ 顺带激活 arm_controller（可发轨迹）；false ⇒ 只使能
float64 timeout_sec       # 0 ⇒ 用参数默认
---
bool success
string state              # 见 §4.3 的 state_machine 取值
string message
```

```rosag
# arm_msgs/srv/SafeDisarm.srv
float64 vlim              # 0 ⇒ 参数默认（保守）
float64 tol_rad           # 0 ⇒ 参数默认
float64 timeout_sec       # 0 ⇒ 参数默认
uint8   max_retries       # 0 ⇒ 参数默认；重试仍失败 ⇒ **保持使能**
---
bool success
bool disarmed             # 只有真的失能了才是 true
float64 max_residual      # 最好一次尝试的实测残差
uint8   attempts
string message            # 失败时明确写"臂仍使能、停在 X"
```

```rosag
# arm_msgs/srv/ManageEmergencyStop.srv
---
bool success
bool disarmed
string[] failed
string message            # 含"臂将塌回折叠位"与"锁存故障需断电"两类信息
```

**为什么不做 `GetStatus` 服务**（M-6）：状态用 **latched 话题**发布（§4.2），新订阅者立刻拿到最后一份，
不需要查询服务；服务只留给"要改变状态"的动作。

### 4.2 话题与消息（两个话题、两个 msg、两个发布者）

```rosag
# arm_msgs/msg/HardwareStatus.msg —— **插件发**（10 Hz）
builtin_interfaces/Time stamp
string   mode              # 声明值（"MIT"/"POS_VEL"）；真源是 joint.yaml，本层读不到 0x0A
bool     gravity_ff
float64  gravity_ff_scale
bool     guard_tripped     # 重力前馈的残差守卫是否已锁存
string[] joint_names
int32[]  err
string[] err_text
float64[] temp_mos
float64[] temp_rotor
bool[]   armed             # 逐关节是否使能（ERR==1）
uint64   rx_frames
uint64   tx_frames
```

```rosag
# arm_msgs/msg/ArmStatus.msg —— **arm_manage 发**（10 Hz；扩展现有 8 个字段）
builtin_interfaces/Time stamp
string   state_machine     # §4.3
bool     hardware_armed    # 硬件侧是否使能（来自 HardwareStatus）
bool     controller_active # arm_controller 是否 active
string   mode              # 声明模式
string[] joint_names
int32[]  per_joint_status_code
string[] error_codes
HardwareStatus hardware    # 内嵌：想看细节的看它
string   message           # 最近一次编排的结果/原因（便于现场排查）
```

QoS（两个都）：`KeepLast(1).reliable().transient_local()` —— **latched**；
⚠️ 不匹配就静默零帧，排查入口 `ros2 topic info <topic> --verbose`（[陷阱 #58](AGENTS.md)）。

### 4.3 `state_machine` 取值（管理层的状态机）

| 值 | 含义 |
|---|---|
| `READONLY` | 硬件 active 但未使能（默认启动态）；只发刷新帧 |
| `ARMING` / `DISARMING` | 过渡态（状态机推进中，服务未返回） |
| `ARMED` | 已使能、控制器未激活（能动但没人能发轨迹） |
| `ARMED_CONTROLLER` | 已使能 + `arm_controller` active（**可发轨迹**） |
| `FAULT` | 出错（含锁存故障）；`message` 说明，需人工处理 |

### 4.4 坐标、单位与命名

- 关节位置用**模型坐标** rad（JTC 与 `joint_limits.hpp` 同一套，见 [AGENTS §5](AGENTS.md) 三层坐标）。
- 服务/话题名一律**绝对名**（`/arm/...`、`/arm_manage/...`）：因为 `get_node()` 返回的节点归属
  （是 CM 的节点还是组件自己的节点）**需要实现时确认**，绝对名在两种情况下都对。

---

## 五、插件改动：运行期使能状态机（M-1 / M-4 / M-5）

### 5.1 语义变化：引入 `armed_`（**这是关键的一处改动**）

现在 `write()` 与 `read()` 用 `enable_on_activate_` 判断"发不发控制帧 / 故障致不致命"。
改动后统一看 `armed_`：

```
armed_  初值 = enable_on_activate_        （on_activate 时设定）
                ⇒ **未调用新服务时行为与现在逐字节相同**（这是"默认行为不变"的依据）
之后     armed_ 由 SetEnabled / EmergencyStop 切换

write() 开头：
  ①  if (enable_req_ != 0) { advance_enable_fsm(); return OK; }   // FSM 必须**在**只读早返回之前
  ②  if (!armed_) { return OK; }                                  // 原 :649（什么都不发）
  ③  ... 原逻辑（write_pos_vel / write_gravity_ff）不变

read() 里三处判据也要从 enable_on_activate_ 改成 armed_：
  · :594 / :617  「有电机故障 ⇒ 报 ERROR 中断」还是「只告警继续」
                 ⇒ **armed 时必须中断**（使能状态下带着故障继续发帧是危险的）；
                   未 armed 时容忍（否则一台故障把整条只读链拖死，本来就看不见别的关节）
  · :636         「未 armed ⇒ 逐台发 0x7FF 刷新帧」← **这一条正是 M-5**，不用新写
```

⚠️ 若把 ① 放到 ② 之后，`enable_on_activate:=false` 启动时服务**永远不会完成**（静默卡死）——
这类"看起来只是顺序"的错，真机上表现为"调用使能服务成功但臂没反应"。

### 5.2 状态机（每周期最多一次寄存器 I/O，期间不发控制帧）

```
新成员：
  enum class EnablePhase { Idle, WriteMode, SendEnable, SendHold, Disabling };
  EnablePhase phase_ = Idle;  size_t step_ = 0;
  std::atomic<int> enable_req_{0};      // 0=无 1=请求使能 2=请求失能
  bool armed_ = false;

请求使能（enable_req_=1）：
  前置：**所有关节都处于失能态**（err==0），否则由服务回调直接拒绝
  ① WriteMode  ：逐台写 0x0A（1 台/周期；**实测整段 ~70 ms**，即每台 ~12 ms ——
                 早先文档写的『每台 150 ms』是 Python 寄存器工具**自己轮询节拍**的开销，不是这条链的往返延迟）
  ② SendEnable ：逐台发使能帧
  ③ SendHold   ：逐台补**真实 MIT 保持帧**，并把 hold_pos 锁在那一刻
                 ⚠️ 陷阱 #41：MIT 的 enable 是零增益帧（出力恒 0），这一段窗口臂真的在掉
  ④ armed_=true；phase_=Idle；回填服务响应

请求失能（enable_req_=2）：
  一次发 6 条失能帧（无寄存器 I/O）⇒ 一周期完成；armed_=false
```

**三条禁忌**（都是这条链上真踩过的）：

1. **FSM 期间不发控制帧** —— 否则边写寄存器边发帧（帧交错；且违反 DESIGN D4"运行期不做寄存器 I/O"的初衷）。
2. **`on_activate` 的现有序列不动** —— 它已经是"写 0x0A + 可选使能 + 锁保持目标 + 立刻补真实 MIT 帧"；
   本 FSM 只是它的**运行期版本**，不是替代。
3. **前置条件由管理层保证、插件只查"全臂失能"** —— 插件读不到控制器状态；`arm_controller` 是否
   inactive 由 `arm_manage` 负责（M-3）。插件能查的是"— 有没有电机还使能着"，那个必须查
   （否则会在旧电机还在被喂帧时开始写寄存器）。

### 5.3 失能之后的行为（M-5）

`armed_ = false` 后只发 `0x7FF` 刷新帧：`/joint_states` 继续更新（RViz 不冻），
但**不给已失能的电机发控制帧**（与 `enable_on_activate:=false` 的只读语义完全一致）。
⚠️ 这段**不需要新写**：`read():636` 的分支本来就是这个行为，只要把判据换成 `armed_`。

### 5.4 状态发布

在 `read()` 里每 10 个周期（100 Hz ⇒ 10 Hz）发一次 `/arm/hardware_status`。
⚠️ **不要放 `write()`**：那是发帧的关键路径。发布本身在 `read()` 里做是有先例的
（现有代码已经在那里 `RCLCPP_WARN_ONCE`），但要注意别做重活（固定大小消息、复用消息对象）。

### 5.5 服务与发布者的创建位置

在 `on_configure`（节点在 `on_init` 之后注入，早于 `on_configure` 更安全）：

```cpp
auto node = get_node();     // hardware_component_interface.hpp:580
set_enabled_srv_ = node->create_service<arm_msgs::srv::SetEnabled>("/arm/set_enabled", ...);
estop_srv_       = node->create_service<arm_msgs::srv::HardwareEmergencyStop>("/arm/emergency_stop", ...);
status_pub_      = node->create_publisher<arm_msgs::msg::HardwareStatus>("/arm/hardware_status", qos);
```

⚠️ 实现时**先确认节点归属**（`ros2 node list` + `ros2 service list`）：绝对名不受影响，但要知道
服务挂在谁的命名空间下，便于排查。

---

## 六、管理层编排

### 6.1 `ArmOn`（分级上电一步化）

```
① 幂等：若已在目标状态 ⇒ 直接成功返回（state 回填当前值）
② 若 with_controller：ControllerGate.deactivate("arm_controller")（若它 active）
③ HardwareGate.enable()  → 调 /arm/set_enabled{enable:true}，等状态机走完（**实测 ~70 ms**）
④ 校验：/joint_states 有 6 个关节、ERR ∈ {0,1}
⑤ 若 with_controller：ControllerGate.activate("arm_controller")
⑥ state = ARMED / ARMED_CONTROLLER；发一次 /arm/status
失败：回滚（已使能的失能）→ state=FAULT → 报清楚
```

### 6.2 `SafeDisarm`（正常失能：先回折叠位，再失能）

```
① 前置：硬件未使能 ⇒ 直接成功返回（无害），message 说明"本来就没使能，未做动作"
② 已在 park？(逐关节 |q−park| ≤ tol) ⇒ 跳过运动，直接 ⑥
③ 查限位护栏（陷阱 #59：JTC 不校验，越界会堵转）
④ MotionClient：给 JTC 发**单点轨迹**（vlim 保守，duration = max(1s, 最大行程/vlim)）
⑤ 非阻塞等到位；超时 ⇒ **async_cancel_goal**（陷阱 #60：不取消臂会自己走完）
   残差验证：max|q−park| ≤ tol
   ③~⑤ 失败 ⇒ **重试**，最多 max_retries 次
⑥ 停 arm_controller（deactivate）
⑦ HardwareGate.disable() → /arm/set_enabled{enable:false}
成功：success=true, disarmed=true, max_residual 回填
失败：**success=false, disarmed=false**, message="臂仍使能、停在 X"（M-8）
```

### 6.3 `EmergencyStop`

```
① 取消一切进行中的目标 —— ⚠️ **实现时偏离**：`arm_motion::MotionClient` 没有公开的 `cancel()`（M9.0 定稿的 API 里没有），所以改用「**立即失能 + 停控制器**」达到同样的物理效果：硬件一失能就没有力矩，而 JTC 在控制器 deactivate 时会中止它的 goal。将来若给 MotionClient 加 `cancel()`，这里再调它
② HardwareGate.estop() → /arm/emergency_stop（插件：立即失能、不碰寄存器、永不抛）
③ 停控制器（best effort）
④ 报："臂将塌回折叠位" + 失能失败的关节 + 锁存故障要断电（陷阱 #39）
```

**为什么急停与服务分开**（M-9）：急停通常正是因为**已经出事了**，此时再走一段轨迹反而危险。
两个服务语义相反、都必须存在。

### 6.4 单例与互斥纪律

启动时（`on_configure` 之后、开始服务之前）：

| 检查 | 做法 | 失败处理 |
|---|---|---|
| 是否已有别的 CM/硬件在跑 | `list_hardware_components`：发现 `DmArmSystem` 处于 `ACTIVE` 且不是自己 | **FATAL 退出**（[陷阱 #35](AGENTS.md)：mock 订到真机 URDF ⇒ 真机被使能 ⇒ `joint1 ERR=13` 锁存） |
| 控制器是否被别人管 | `list_controllers` | 只告警（可能是有意为之） |
| 串口独占 | 由插件负责（TIOCEXCL）；管理层只报错信息 | — |

---

## 七、park 位姿（M-10）

- **真源**：`arm_manage/config/arm_manage.yaml` 的 `park_pose`（模型坐标 6 个数）+ `vlim` / `tol_rad` /
  `timeout_sec` / `max_retries` / `status_hz`。
- **接管 `safe_park_node` 的硬编码**：现在的 `kDefaultParkPose = {1.4724, 0.0008, -0.0233, 0.0027, 0.0069, 0.0149}`
  要**一次性**迁到这里，`safe_park_node` 不再留一份（见 §九 的迁移）。
- **为什么现在不建通用命名位姿表**：盘完用途后，折叠位**只有一个消费者**（安全失能前的收臂）。
  未来的"抓放观察位"是 **`home`**（不是折叠位），那时再升级成
  `arm_motion/config/named_targets.yaml` + 通用的 `TARGET_NAMED`（MOTION_ARCH §5.2 的规划）。
  ⚠️ 到时 `arm_manage` 要改为读同一份表，**不能出现两份 park**。

---

## 八、文件清单

**新建**

| 路径 | 内容 |
|---|---|
| `src/arm_motion/`（新包，ament_cmake，**只有库**） | `include/arm_motion/motion_client.hpp` + `src/motion_client.cpp`：从 `safe_park_node.cpp` 抽出的 JTC 客户端（限位护栏 / 非阻塞状态机 / `async_cancel_goal` / 残差验证 / QoS）。**将来 arm_motion 的 action server 也在这里** |
| `src/arm_manage/`（新包） | `src/manage_node.cpp`、`include/arm_manage/{controller_gate,hardware_gate}.hpp`、`config/arm_manage.yaml` |
| `src/arm_msgs/srv/` | `SetEnabled.srv`、`HardwareEmergencyStop.srv`、`ArmOn.srv`、`SafeDisarm.srv`、`ManageEmergencyStop.srv`（现在这个目录是**空的**） |
| `src/arm_msgs/msg/HardwareStatus.msg` | 插件发的硬件侧状态 |
| `src/arm_bringup/launch/arm_system.launch.py` | 总 launch（取代三份） |

**修改**

| 路径 | 改动 | 风险 |
|---|---|---|
| `motor_driver_hardware/src/dm_system_interface.cpp` + `include/.../dm_system_interface.hpp` | **4 处 `enable_on_activate_` 判据 → `armed_`**（`write():649`、`read():594/:617/:636`）+ 使能 FSM + 状态发布 + 2 个服务（`get_node()`） | ⚠️ **已真机验收件**：默认行为必须**一个字节不变**（`armed_` 初值 = `enable_on_activate_`） |
| `arm_application/src/safe_park_node.cpp` | 改为调管理层的 `SafeDisarm`（或退化为薄壳） | ⚠️ 已验收（残差 0.0015~0.031 rad），要**行为等价** |
| `arm_msgs/CMakeLists.txt`、`msg/ArmStatus.msg` | 注册新接口；扩展聚合状态（现在**无人发布**，改动便宜） | 低 |
| `AGENTS.md` §2/§3/§4/§8、`readme.md`、`MOTION_ARCH.md §5.2` | 按维护约定同步 | — |

---

## 九、里程碑与验证

**mock 优先**；真机只在 M9.5。

| # | 内容 | 验收标准 |
|---|---|---|
| **M9.0** | 抽 `MotionClient` 到 `arm_motion`；`safe_park_node` 改用它（行为不变） | ① 假 action server 单测：正常到达 / 超时 / 取消确认 / 残差超差；② 与旧 `safe_park` 同参数下**发出的轨迹逐字段一致**；③ 真机回归残差与迁移前同量级 |
| **M9.1** | `arm_msgs` 加 5 srv + 1 msg；扩展 `ArmStatus` | ✅ **已落地（2026-10-11）**：10 个接口 `ros2 interface show` 全对、`colcon build` 通过；**无任何包依赖 `arm_msgs`** ⇒ 替换 `ArmStatus` 的老字段是安全的 |
| **M9.2** | 插件：`armed_` + 使能 FSM + 状态发布 + 2 服务 | ✅ **已落地（2026-10-11）**：21 个插件单测全过（14 原有 + 7 新）；真机上**只读启动**下 `/arm/set_enabled`/`/arm/emergency_stop` 注册成功、`/arm/hardware_status` **10.0 Hz** 内容正确；`SetEnabled(true)` ⇒ `success=True`、`err` 全 1、**`/joint_states` 位移为零**（实测算术序列 **70 ms**）；`SetEnabled(false)` ⇒ 全 ERR=0、幂等复测返回「已经是失能态（幂等）」 |
| **M9.3** | `arm_manage` 节点（两个 Gate + 3 服务 + 聚合状态） | ✅ **已落地（2026-10-11）**：mock 链 + 假插件，**25 项断言全绿** —— 含 **M-3 顺序不变量**（插件收到 enable/disable 的那一刻 `arm_controller` 已 inactive）、`safe_disarm` 真收臂（残差 0.000000、attempts=1、2.5 s）、**已在 park ⇒ 跳过运动**（attempts=0、0.1 s）、幂等、急停、以及 `/arm/status` 迁移 `READONLY→ARMING→ARMED_CONTROLLER→DISARMING→READONLY`（**无假 FAULT**） |
| **M9.4** | 总 launch 取代三份 | ✅ **已落地（2026-10-11）**：`arm_bringup/launch/arm_system.launch.py`。mock 上实测：一条命令起来后 `arm_controller` **inactive（装载未激活）**、`/arm_manage/*` 在、`/arm/status`=READONLY → `arm_on` ⇒ `ARMED_CONTROLLER`（控制器转 active）→ `safe_disarm`（残差 0.000000、attempts=1）→ `emergency_stop`。三处实现决定：① **`arm_controller` 用 `spawner --inactive`**（激活交给 `arm_manage`，保证 M-3 顺序）；② **不拉 `move_group.launch.py`**（它自带一套 mock 链 ⇒ 双开，陷阱 #35/#29）；③ launch 里自动补 `LD_LIBRARY_PATH`（我们自己的可执行文件 RPATH 缺 pixi 环境）。✅ 旧的 `mock_control`/`real_control`/`safe_park` 三份**同日已删除**（连同 `arm_application/CMakeLists.txt` 的 `install(DIRECTORY launch ...)` 行、以及 `install/` 空间里的陈旧副本 —— 不清的话「删了的 launch 还能跑」）；要回看旧内容：`git show ac1bde9:<path>` |
| **M9.5** | 真机分级验收 | ✅ **已通过（2026-10-11）**：只读 → `arm_on(false)`（**零位移 0.00000 rad**、`err` 全 1）→ `arm_on(true)`（控制器转 active，**M-3 顺序成立**）→ j1 ±0.05 rad 小动作（**往返残差 0.00076 rad**）→ `safe_disarm`（**臂已在折叠位 ⇒ 自动跳过收臂**，`max_residual 0.009118`、`attempts=0`；失能后 `err` 全 0）→ `emergency_stop`（幂等，如实报「已失能全部 6 台」）。逐条见 `TESTING.md` §十九。✅ **补测已过（同日）**：`gravity_ff:=true scale:=0.2` 走管理层（MIT、使能零位移 0.00038 rad、MIT 静差 0.0367 rad 符合 摩擦/kp）+ 急停打断正在跑的轨迹（位置冻结、JTC `ABORTED`/`error_code=-1`）。⚠️ 仍未测：带载姿态下真的收臂 | 只读 → `arm_on(with_controller=false)` → **零位移保持** → `arm_on(with_controller=true)` → 小动作 → `safe_disarm`（残差对拍）→ `emergency_stop`（确认塌且无异常）；每步记 ERR/温度 |

---

## 十、异常矩阵

| # | 失败模式 | 处置 | 臂的最终状态 |
|---|---|---|---|
| 1 | `SetEnabled(true)` 时仍有电机使能 | 拒绝（插件查全臂 err==0） | 不变 |
| 2 | 硬件组件未 active 时请求使能 | 拒绝并说明"先 set_hardware_component_state=active" | 不变 |
| 3 | 写 `0x0A` 失败（某台） | 状态机中止 ⇒ 已写成的那些**回滚**（写回原值）⇒ 报哪一台 | 未使能 |
| 4 | 使能帧后没补成保持帧 | 不可能（同一周期内发）——但若串口写失败 ⇒ 报错并**立刻全部失能** | 失能（臂塌） |
| 5 | `ArmOn` 中 `/joint_states` 缺关节 | 报错 + 回滚失能 | 失能 |
| 6 | `SafeDisarm` 收臂超时 | 取消 → 重试；仍失败 ⇒ **保持使能**、停在原地、报错 | 使能，位置未知但可控 |
| 7 | `SafeDisarm` 残差超差 | 同上 | 同上 |
| 8 | `SafeDisarm` 时某关节 ERR≠0/1 | 拒绝收臂（先报故障），不失能 | 使能（可能已由插件报 ERROR） |
| 9 | 急停时串口写失败 | 仍返回（`failed` 里列出来）+ 提示直接断电 | 未知（最坏仍使能） |
| 10 | ERR=13（锁存） | 报"需要断电重启，本层不做恢复" | 电机自己已关输出 |
| 11 | 已有另一套在跑 | 启动即 FATAL | — |
| 12 | 控制器 activate 失败 | 回滚到 `ARMED`（硬件仍使能、停在原地）并报错 | 使能 |

---

## 十一、不做什么

1. **运行期 MIT ↔ POS_VEL 切换**（卡在 robot_description 参数上，已否决）；
2. `arm_motion` 的 action server / IK / 规划 / 直线段（M9.0 只抽执行件）；
3. 电压监控（本层读不到 `0x3C`）、碰撞检测、安全认证层；
4. 通用命名位姿表（只有 park 一个消费者，M-10）；
5. 夹爪、视觉；
6. 重摆"故障恢复"：ERR=13 只能断电，本层**只报告不假装恢复**。

---

## 十二、未决问题（实现前定）

| # | 问题 | 倾向 |
|---|---|---|
| 1 | `max_retries` / `vlim` / `tol_rad` / `timeout_sec` 的默认值 | `max_retries=2`、`vlim=0.15`（与 safe_park 同）、`tol_rad=0.05`、`timeout_sec=60` |
| 2 | `ArmOn` 是否也需要 `SafeDisarm` 的反向（`ArmOff` 只失能不收臂） | 不需要：`SafeDisarm` 已含"已在 park 就跳过运动"，等价 |
| 3 | 状态话题的 `status_hz` | 10 Hz |
| 4 | 管理层的 tick 周期 | 20 ms（服务流程不是硬实时） |
| 5 | `get_node()` 返回节点的归属与命名空间 | 实现时先确认（绝对名不受影响） |
| 6 | `HardwareEmergencyStop.srv` 的名字（避免与管理层的 `ManageEmergencyStop` 混） | 保持两个名字，文档/日志里明确区分 |
| 7 | C++ 单测是否要把"使能 FSM 走进度"做成逐周期断言 | 是（假串口的模拟电机已能驱动 `write()`） |
