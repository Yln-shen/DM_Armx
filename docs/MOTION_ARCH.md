# 运动接口层架构（arm_motion）—— 运动封装 / 应用层 / 规划层 / 视觉契约

> 状态：**架构设计，未实现**。本文只定义分层、接口与语义；一行代码都还没写。
> 本文是"运动接口层"的设计意图真源。**状态与不变量**看 [AGENTS.md](../AGENTS.md)，
> **实测值**看 [TESTING.md](TESTING.md)，**踩坑**看 [LESSONS.md](LESSONS.md)，
> 电机驱动层的设计意图看 [DESIGN.md](DESIGN.md)。
> 本文出现的决策编号 D-M1… 供代码注释引用（与 DESIGN.md 的 D1…D9 同一用法）。

---

## 一、定位与边界

### 1.1 这一层解决什么问题

当前工程有两条通往 `arm_controller`(JTC) 的路，但**没有一条是"应用层可以复用的运动接口"**：

- MoveIt 那条：配置齐全、mock 与真机都跑通过，但**只有 RViz 手动点**，没有任何代码调用；
- `safe_park_node` 那条：直发 JTC 单点轨迹，把"限位护栏 / 超时 / 取消 / 非阻塞"四件麻烦事
  自己实现了一遍。

于是每加一个业务节点，都要把那四件事重写一遍（陷阱 #57 回调里阻塞、#60 放弃 goal 不取消
——这两个坑 `safe_park` 已经用真机代价踩过）。**`arm_motion` 存在的唯一理由就是把这四件事
收敛到一处，只实现一次。**

### 1.2 做什么

| 职责 | 说明 |
|---|---|
| 接收运动目标 | 关节空间 / 笛卡尔位姿 / 命名目标（见 §四） |
| 目标校验 | 关节限位、起始状态越界、命名目标存在性（见 §六） |
| 规划 | 调 `move_group`：IK + OMPL 规划；可选笛卡尔直线（见 §八） |
| 执行 | 唯一向 `arm_controller`(JTC) 发目标的一方（见 D-M2） |
| 执行后验证 | 实测关节残差、超时、取消、互斥 |
| 反馈 | 阶段（校验/规划/执行/验证）、进度、当前位姿 |

### 1.3 不做什么（防止范围蔓延）

- **不碰协议、串口、寄存器、电机**——那些在 `motor_driver_hardware`；
- **不做运动学 / 轨迹生成 / 时间参数化**——IK 与 TOTP 都在 MoveIt；
- **不拥有控制循环**——控制周期由 `ros2_control` 的 `update()` 驱动，本层没有任何周期性发送；
- **不做业务编排**（先抓后放、失败重试、任务队列）——那是 `arm_application`；
- **不做视觉检测 / 标定 / 位姿估计**——本层只消费"物体位姿"这个结果（见 §十）；
- **不做夹爪**——本阶段不做（AGENTS §1）。

### 1.4 依赖方向（单向，不许反向）

```
arm_application ──▶ arm_msgs (ExecuteMotion) ──▶ arm_motion ──▶ move_group
   （业务）              （接口）                  （本层）    └─▶ arm_controller (JTC)
                                                                      │
                                                     ros2_control ─────┘
                                                          │
                                            DmSystemInterface ──▶ 6 台电机
```

- `arm_motion` **不依赖** `motor_driver`（Python）、`motor_driver_hardware`（C++）、`arm_application`；
- `arm_application` **只依赖** `arm_msgs`，不链接 MoveIt（D-M8）；
- `arm_msgs` 保持纯接口包，**零实现**。

---

## 二、现状与缺口

| 层 | 现状（已核实） | 缺口 |
|---|---|---|
| ros2_control / 驱动 | `DmSystemInterface` 插件、两份控制器配置、真机三步验收通过 | 无（本层不改它） |
| **规划层** | `arm_moveit_config` 配置齐全；`moveit_controllers.yaml` 走 simple controller manager → `arm_controller`；mock 与真机 plan/execute 都跑通过 | **程序化接入为零**：没有代码调用 `move_group`，`moveit_py` 未安装 |
| **应用层** | 只有 `safe_park_node`（389 行），自带"非阻塞状态机 + 取消 + 残差验证" | **没有可复用的运动原语**；每个新业务都要重写那四件事 |
| **互斥** | 无。`safe_park` 直发 JTC，MoveIt 也在发 JTC（经 simple controller manager） | **两个发布者会互相 preempt**：JTC 的 action server 一次只接受一个 goal，新 goal 顶掉旧的 |
| 视觉 | 无 | 本轮只冻结契约（§十），不实现 |

---

## 三、分层、数据流与执行上下文

### 3.1 分层

```
┌ 应用层（业务，非阻塞状态机）────────────────────────────────────────┐
│  arm_application/                                                  │
│   · safe_park_node（已有 → 迁移为 ExecuteMotion 的 client，见 §九） │
│   · （未来）pick_place_node：拿物体位姿 → 抓 → 放                   │
└───────────────────────┬────────────────────────────────────────────┘
                        │ ExecuteMotion.action（唯一对外契约）
                        ▼
┌ arm_motion（本层）─────────────────────────────────────────────────┐
│  目标校验 → 规划(异步) → 执行(异步) → 验证 → 上报                   │
│  互斥 · 超时 · 取消 · 残差 · 命名目标表                             │
│  非阻塞状态机（定时器推进），单线程 executor，无工作线程、无锁      │
└───────┬──────────────────────────────────────┬─────────────────────┘
        │ moveit_msgs/action/MoveGroup         │ control_msgs/action/
        │        (plan_only = true)            │ FollowJointTrajectory
        │ moveit_msgs/srv/GetCartesianPath     │  ← 唯一发布者（D-M2）
        ▼                                      ▼
   move_group（只规划）                  arm_controller (JTC)
        │                                      │
        └──────── 同一套 URDF / 限位 ───────────┘
                                               ▼
                              ros2_control → DmSystemInterface → 6 电机
```

### 3.2 决策索引

| # | 决策 | 详见 |
|---|---|---|
| D-M1 | 对外契约只有 `ExecuteMotion` **一个** action（用 `target_type` 分派），不是三个 action | §4.1 |
| D-M2 | `arm_motion` 是**唯一**的 JTC 目标发布者；`move_group` 只规划不执行（`plan_only=true`） | §3.2 下方 |
| D-M2b | 关节空间目标也走 `move_group` 规划，不直发 JTC 单点轨迹 | §3.2 下方 |
| D-M3 | 单线程 executor + 定时器驱动的非阻塞状态机；依赖里**不出现** `moveit_ros_planning_interface` | §3.2 下方 |
| D-M4 | 默认**拒绝**抢占；显式 `preempt=true` 才抢占 | §7.2 |
| D-M5 | 命名目标用 YAML 文件，不用 ROS 参数 | §5.1 |
| D-M6 | 视觉只给**物体**位姿；抓取位姿由应用层算 | §10.1 |
| D-M7 | 错误码**自建**，不透传 `MoveItErrorCodes` | §4.3 |
| D-M8 | `arm_application` 只依赖 `arm_msgs`；`arm_motion` 不依赖驱动层与应用层（单向） | §1.4 |

> **状态（2026-10-07）**
> - **已确认**：**D-M2** 与 **D-M3**（用户在"接口形态"对比后拍板）。
>   二者的备选方案 —— 让 `move_group` 负责执行、用阻塞的 `MoveGroupInterface` —— **已否决**，
>   否决理由保留在下方正文里（防止后人重新引入阻塞式实现，见陷阱 #57）。
> - **同样来自用户选择**：D-M6（视觉只给物体位姿）、新建独立包 `arm_motion`、C++ 应用层 + 一个总 launch。
> - **其余为本轮建议**（D-M1 / D-M2b / D-M4 / D-M5 / D-M7 / D-M8），实现前仍可在评审时推翻。

下面三条是骨架，展开说明。

**D-M2：`arm_motion` 是唯一的 JTC 目标发布者；`move_group` 只规划不执行。**

- `MoveGroup` action 一律带 `planning_options.plan_only = true`，只取回 `planned_trajectory`；
  执行由本层自己发给 JTC。
- 理由：①**单一执行出口** ⇒ 互斥、取消、超时、残差验证只在一处；②笛卡尔路径由
  `GetCartesianPath` 拿到后同样交给 JTC，两条规划路共用同一个执行器，不会出现两个发布者；
  ③不需要 MoveIt 的控制器切换/执行监控（单控制器桌面臂用不上）。
- 代价：失去 MoveIt 的执行监控（多控制器切换、执行中重规划）。若将来需要多控制器（例如
  加夹爪控制器），需要重新评估。
- 连带影响：`moveit_controllers.yaml` 与 `trajectory_execution` 参数**在 `plan_only` 下不再被
  使用**（保留无害，但要知道它不是执行链路的一部分）。

**D-M3：实现范式 = 单线程 executor + 定时器驱动的非阻塞状态机，依赖里不出现
`moveit_ros_planning_interface`。**

- **理由（关键）**：`MoveGroupInterface::plan()` / `move()` / `computeCartesianPath()` 都是
  **阻塞调用**。放进 executor 回调里就会重演陷阱 #57（`safe_park` 第一版：`wait_for_state()`
  在回调里 `sleep` ⇒ 订阅回调永远进不来）。纯异步接口（action client + service client 的
  `future` + 定时器推进）能彻底避开这个问题，且与 `safe_park` 的成熟范式一致。
- **备选（不推荐）**：用 `MoveGroupInterface` 的便利 API，把整套流程放进**专职工作线程**，
  回调只做入队/取反馈/处理取消。代价：共享状态要加锁、取消语义要在两个线程间同步、
  且 `arm_motion` 会被迫链接 `moveit_ros_planning_interface`（编译变量变重）。
- **备选（更不推荐）**：用 `MoveGroupInterface` 但仍阻塞在回调里 ⇒ 运动期间无法响应取消
  （安全上不可接受）。

**D-M2b：关节空间目标也走 `move_group` 规划，而不是直发 JTC 单点轨迹。**

- 理由：①单一执行链，语义一致；②MoveIt 会做限位检查与 TOTP 时间参数化；③将来加碰撞
  场景/障碍物时自动生效；④顺带替我们挡住陷阱 #37（起始状态越界 ⇒ 拒绝规划）。
- 代价：比 `safe_park` 现在的单点直发慢（多一次规划往返，量级 ms~几十 ms）。桌面抓放
  对这点延迟不敏感。

### 3.3 执行上下文（每段代码跑在哪）

| 环节 | 上下文 | 约束 |
|---|---|---|
| 收目标 / 校验 | action goal 回调 | **只做常量时间的事**；重活交给状态机 |
| 规划 / 执行 | 定时器 tick（如 50 ms） | 每 tick 只推进一步、立刻返回；**不许 sleep / 不许阻塞等 future** |
| 反馈 | 状态机 tick 内发布 | 从共享状态读，不做计算 |
| 取消 | action cancel 回调 | 置原子标志 + `async_cancel_goal`；状态机下一 tick 看到并进入 CANCELLING |
| 执行完成后验证 | 状态机 tick | 等约 0.3 s 让 `/joint_states` 刷新（非阻塞） |

⚠️ 全层**没有任何 `sleep` / 阻塞等待**；唯一的等待形式是"`future.wait_for(0)` 非阻塞轮询"。

---

## 四、接口契约：`arm_msgs/action/ExecuteMotion.action`

### 4.1 为什么只有一个 action（D-M1）

应用层的需求只有"去某个地方"，差别在**目标的表达方式**（关节/位姿/命名）。用一个 action
加 `target_type` 分派，比三个 action 好：互斥、超时、取消、错误码只有一套；业务层只学一个接口。

### 4.2 定义

```rosag
# ── Goal ─────────────────────────────────────────────
uint8 TARGET_JOINTS = 0    # 关节空间：MoveIt 关节约束 → 规划 → 执行
uint8 TARGET_POSE   = 1    # 笛卡尔位姿：IK + 规划（可要求直线）
uint8 TARGET_NAMED  = 2    # 命名目标：查 named_targets.yaml
uint8 target_type

# TARGET_JOINTS
string[] joint_names                  # 必须与真源顺序一致（见 §5.2 校验）
float64[] joint_positions             # 模型坐标 rad

# TARGET_POSE
geometry_msgs/PoseStamped target_pose  # frame_id 必须 "base_link"（见 §4.4）

# TARGET_NAMED
string target_name

# 通用
string  tip_link                      # 空 ⇒ 用 named_targets.yaml 的默认（gripper_tcp）
bool    linear                        # true ⇒ 笛卡尔直线（仅 TARGET_POSE 有意义）
float64 max_velocity_scaling          # (0,1]；<=0 ⇒ 用服务端默认（见 §6.3）
float64 max_acceleration_scaling      # (0,1]；<=0 ⇒ 用服务端默认
float64 timeout_sec                   # 0 ⇒ 用服务端默认
bool    preempt                       # false ⇒ 忙时拒绝（error 10）；true ⇒ 抢占（D-M4）

---
# ── Result ───────────────────────────────────────────
bool    success
uint16  error_code                    # §4.3 的错误码表
string  message                       # 人可读，含失败的具体关节/数值
float64 elapsed_sec                   # 从收到 goal 到出结果
float64 max_residual_rad              # 执行后实测关节残差（成功路径也填）

---
# ── Feedback ─────────────────────────────────────────
string phase                          # §7.1 的阶段名
float64 progress                      # 0~1（执行阶段按轨迹时间估算，其余阶段 0）
geometry_msgs/PoseStamped current_pose # 当前末端位姿（规划/执行期间可空）
```

### 4.3 错误码表（本项目自己的，不透传 `MoveItErrorCodes`）

| code | 名字 | 含义 | 是否发过帧 |
|---|---|---|---|
| 0 | OK | 成功 | 是 |
| 1 | INVALID_GOAL | 参数非法（关节数/名字不匹配、字段缺失、frame 不是 base_link） | 否 |
| 2 | JOINT_LIMIT | 关节目标越界（限位护栏，陷阱 #59） | **否** |
| 3 | IK_FAILED | 位姿不可达 / IK 无解 | 否 |
| 4 | PLANNING_FAILED | 规划失败或规划超时 | 否 |
| 5 | CARTESIAN_INCOMPLETE | 直线段完成度 < 阈值 | 否 |
| 6 | EXEC_TIMEOUT | 执行超时 | 是（已取消） |
| 7 | PREEMPTED | 被新的目标抢占 | 是（已取消） |
| 8 | RESIDUAL_TOO_LARGE | 执行完成但残差超限 | 是 |
| 9 | NAMED_TARGET_MISSING | 命名目标不存在 | 否 |
| 10 | BUSY | 已有目标在执行（未带 `preempt`） | 否 |
| 11 | HARDWARE_ERROR | JTC 报硬件错误 / 电机 ERR（由 DmSystemInterface 上报） | 是 |
| 12 | START_STATE_INVALID | 拿不到 `/joint_states`，或起始状态越界（陷阱 #37） | 否 |
| 13 | CANCELLED | 被取消请求终止 | 是（已取消） |

**为什么自建错误码（D-M7）**：`MoveItErrorCodes` 面向 MoveIt 内部概念（`PLANNING_FAILED=-1`、
`START_STATE_IN_COLLISION=-11`、`GOAL_CONSTRAINTS_VIOLATED`…），业务层不需要理解这些；
而"目标越界被我们自己拒绝"（#59 的护栏）在 MoveIt 里根本没有对应码。**但** `Result.message`
里要带上原始 `MoveItErrorCodes` 数值，便于排查。

### 4.4 坐标系与单位约定

- `target_pose.frame_id` **必须**是 `base_link`；其它 frame 一律拒绝（error 1）。
  理由：本层的 TF 依赖面越小越好，`base_link` 是 URDF 根（`arm.srdf` 的规划组基石）；
  视觉侧要做的是"把物体位姿变换到 `base_link`"，那是视觉包的职责。
- 位置单位 m、角度 rad（与 URDF/MoveIt 一致）；关节位置是**模型坐标**（AGENTS §5 三层坐标），
  与 `joint_limits.hpp`、URDF `<limit>` 同一套。
- `stamp` 不参与校验（本层不做时间对齐）；陈旧帧的时效性由视觉侧负责（§10.4）。

### 4.5 落地步骤（实现阶段才做）

1. 新建 `src/arm_msgs/action/ExecuteMotion.action`（内容如上）；
2. `src/arm_msgs/CMakeLists.txt` 的 `rosidl_generate_interfaces` 里加一行
   `"action/ExecuteMotion.action"`；**`DEPENDENCIES` 不用改**（只用 `geometry_msgs`，
   `float64/uint16/bool/string` 都是 builtin）；
3. `arm_msgs/action/MoveToPose.action` **本轮暂留不动**（无任何代码引用；是否删除见 §15）。

---

## 五、命名目标表

### 5.1 为什么用 YAML 而不是 ROS 参数（D-M5）

位姿是**数据**不是**参数**：改一个放置点不该要求改 launch 文件或命令行。YAML 还能带注释
（"这个点是 XYZ 标定出来的"），参数数组不能。

### 5.2 格式（`arm_motion/config/named_targets.yaml`）

```yaml
# 命名目标表 —— 常用位姿的真源
frame_id: base_link          # 仅允许 base_link
tip_link: gripper_tcp        # Goal.tip_link 为空时的默认末端
targets:
  park:                      # 折叠位（模型坐标；与 safe_park 的默认值同源）
    type: joints
    positions: [1.4724, 0.0008, -0.0233, 0.0027, 0.0069, 0.0149]
  home:
    type: joints
    positions: [0.0, 0.5, -0.5, 0.0, 0.0, 0.0]
  place_a:
    type: pose
    position: [0.20, -0.10, 0.15]    # m，base_link
    rpy: [3.14159, 0.0, 0.0]         # rad
```

**加载期校验（启动即 FATAL，不带病运行）**：

| 检查 | 依据 |
|---|---|
| `joints` 类型的 `positions` 个数 == 关节数、顺序 == 真源顺序 | `arm_limits::kJointNames` |
| `joints` 类型逐个查限位 | `arm_limits::kModelLimits`（构建期由 `joint.yaml`+`align.yaml` 生成） |
| `type` 只能是 `joints`/`pose`；缺字段/多字段拒绝 | 本表 |
| `pose` 类型不做限位校验（可达性交给 IK），但 `position` 必须是有限数 | 陷阱 #1 的同类思路（NaN 拦在门口） |

⚠️ **关节顺序必须与 `joint_limits.hpp` 一致**——`safe_park_node` 已经有同款校验
（`validate_joint_names()`），照抄它的做法，别按下标硬比。

### 5.3 `park` 的单一真源问题

`safe_park_node` 现在把折叠位硬编码在 `kDefaultParkPose`。迁移后（§九）折叠位的真源变成
`named_targets.yaml` 的 `park`，**两处不能同时留**。迁移时必须一次性切换，并做行为回归。

---

## 六、安全护栏

### 6.1 关节限位（陷阱 #59 的护栏，必须有）

`arm_controller`(JTC) **不校验限位**——真机实测：`joint2` 给 `+99 rad` 它照执行，臂顶在机械
硬限位上堵转 60 s、effort 22.5 N·m、线圈 62 ℃，**全程 err=0 无故障码**。
所以 `arm_motion` 必须在**发目标之前**自己查限位：

- 真源：`arm_description` 构建期生成的 `arm_limits::kModelLimits` / `kJointNames`
  （生成器 `scripts/gen_joint_limits_header.py`，与 URDF `<limit>` 逐条对拍）；
- 检查点：① `TARGET_JOINTS` / 命名 `joints` 目标在 VALIDATING 阶段查；② `TARGET_POSE` 与
  命名 `pose` 目标的**规划结果**在 EXECUTING 之前**逐点**查轨迹（MoveIt 的位姿目标经过 IK 后
  也是一串关节值，同样要过这道闸）。

### 6.2 起始状态越界（陷阱 #37）

真机踩过：`joint1 = 1.371670` 比 URDF 下界低 `0.0205` ⇒ MoveIt 报
`Start state out of bounds`、`error_code=99999`、轨迹为空。**放任不管的表现是"规划失败"，
无法区分"真不可达"和"只是起点越界一点"。** 所以本层在 PLANNING 之前先做：

1. 有没有 `/joint_states`（QoS 必须是 RELIABLE + TRANSIENT_LOCAL，陷阱 #58）→ 没有 ⇒ error 12；
2. 当前关节值逐关节比 `kModelLimits` → 越界 ⇒ error 12，`message` 里报出**是哪一关节、
   差多少**（这条信息是排查"为什么突然规划不了"的唯一线索）。

### 6.3 速度与加速度缩放

- `max_velocity_scaling` / `max_acceleration_scaling` 只允许 `(0, 1]`；`>1` 直接拒绝
  （error 1）——真机安全上"放大"没有正当用途；`<=0` 用服务端默认。
- 服务端默认值写在 `arm_motion/config/motion_params.yaml`，且**默认要保守**（首轮真机建议
  `0.05~0.1`）。
- ⚠️ **与 MIT/重力前馈的相互作用**（AGENTS §5、陷阱 #51）：MIT 下 `kp` 有硬上界
  `torque_max / 期望滞后`，PD 项自己超过 `torque_max` 会**拒发** ⇒ `write()` 返回 ERROR ⇒
  CM 报硬件错误 ⇒ **失能 ⇒ 掉臂**。轨迹越快、期望滞后越大。所以 `gravity_ff:=true` 时
  速度缩放要更保守，且这一条要写进 `motion_params.yaml` 的注释里。

### 6.4 位姿目标的额外粗检（可选，待定）

`TARGET_POSE` 在送进 IK 之前，可先做工作空间粗检（例如 |p| 是否在臂长的 [0, 1.05] 倍内），
把"明显不可能"的目标在本地就拒掉，省一次 IK 往返。**本轮不定**，见 §15。

### 6.5 不做的安全措施（必须说清楚）

本层**没有**：碰撞检测（MoveIt 的 planning scene 里没有任何障碍物）、安全认证层、
力矩级保护（那在驱动层 `torque_max` 与残差守卫）、急停硬件。**别把 `arm_motion` 当安全层。**

---

## 七、执行语义

### 7.1 状态机

```
IDLE ──goal──▶ VALIDATING ──▶ PLANNING ──▶ EXECUTING ──▶ VERIFYING ──▶ DONE
                    │              │             │                        │
                    └──────────────┴─────────────┴──── cancel ──▶ CANCELLING ──▶ DONE
```

| 阶段 | 做的事 | 失败 ⇒ code |
|---|---|---|
| VALIDATING | 字段/类型/frame/关节名顺序/限位/命名目标/起始状态/是否忙 | 1, 2, 9, 10, 12 |
| PLANNING | `MoveGroup`(plan_only) 或 `GetCartesianPath`；直线完成度检查 | 3, 4, 5 |
| EXECUTING | 逐点查限位后把轨迹发给 JTC；跟踪反馈算 progress | 6, 7, 11 |
| VERIFYING | 等 ~0.3 s 状态刷新，比实测关节 vs 轨迹末点 | 8 |
| CANCELLING | `async_cancel_goal` 同时发给 `move_group`（规划中）与 JTC（执行中），等确认（超时 1 s 不再等） | 13 |

### 7.2 互斥与抢占（D-M4）

- **默认拒绝**：已有目标在执行 ⇒ 立即返回 error 10。理由：抢占会让"谁在动这个臂"变得
  不可预测，而越权抢占在安全上是最危险的行为。
- **显式抢占**：`preempt = true` 才取消当前目标（error 7 上报给**被抢占者**，新目标继续）。
  留给"需要中途改变目的地"的场景（例如未来视觉给的目标过期了、要改抓另一个）。
- 无论哪种，**同一时刻只有一个目标在跑**（单线程状态机天然保证）。
- ⚠️ **取消必须真的取消**（陷阱 #60）：真机踩过——`timeout_sec=0.3` 时节点退出后臂仍走完
  0.5566 rad，而日志写着"臂停在当前位置"（是错的）。所以放弃路径一律 `async_cancel_goal`
  并打印"取消已确认 / 未确认"。

### 7.3 残差验证

JTC 报 `SUCCEEDED` 不等于"到位"：POS_VEL 有稳态残差（真机 +0.08 rad 目标 ⇒ 残差 0.0016 rad），
MIT 下有 `扰动/kp` 的死区（`kp=25` 时 max 0.0385 rad）。所以 VERIFYING 阶段按
`max_residual_rad`（服务端参数，默认建议 `0.05`）判定，超限报 error 8 但**不失能、不停在原地
之外**——臂就在那里保持（硬件仍 ACTIVE、插件继续发保持帧）。

### 7.4 超时

- `timeout_sec` 覆盖 EXECUTING + VERIFYING（规划阶段的超时由 `move_group` 自己的规划时间
  上限决定，本层再套一层"规划无响应"保护，默认 5 s）。
- 超时 ⇒ 取消 ⇒ error 6。**不重试**（重试策略属于应用层）。

---

## 八、规划层接入

### 8.1 `move_group` 常驻

三种形态（`MoveGroupInterface` / 原生 action / 混合）**都要求 `move_group` 常驻**，它不能被
按需拉起（每次拉起要重新解析 URDF/SRDF、加载 OMPL，启动秒级）。所以：

- 总 launch 里 `move_group` 与 `ros2_control` 一起起（§十一）；
- `arm_motion` 只做客户端，`on_configure` 时等 `/move_action` 与 `/compute_cartesian_path`
  就绪（非阻塞等待 + 超时报警，不 FATAL——规划服务晚到不该让整栈起不来）。

### 8.2 用哪个接口

| 用途 | 接口 | 说明 |
|---|---|---|
| 关节目标 / 位姿目标规划 | `moveit_msgs/action/MoveGroup`，`plan_only=true` | 取回 `planned_trajectory`；`MoveItErrorCodes` 映射到 §4.3 |
| 笛卡尔直线 | `moveit_msgs/srv/GetCartesianPath` | 返回 `trajectory` + `fraction`；`fraction < 0.95` ⇒ error 5（阈值可配） |
| 执行 | `control_msgs/action/FollowJointTrajectory` | 直接发给 `/arm_controller/follow_joint_trajectory` |

**为什么不用 `MoveGroupInterface`**：见 D-M3（阻塞 API）。本层依赖里因此**不出现**
`moveit_ros_planning_interface`——接口包本来就该是轻的：`arm_msgs/CMakeLists.txt` 的
`rosidl_generate_interfaces` 里 `DEPENDENCIES` 只有 `geometry_msgs`，
`arm_motion` 需要新增的只有 `moveit_msgs` + `control_msgs` + `trajectory_msgs`。

### 8.3 规划失败怎么映射

| MoveIt 现象 | 本层 code |
|---|---|
| IK 无解 / `NO_IK_SOLUTION` | 3 |
| `START_STATE_OUT_OF_BOUNDS` / `99999` | 12（本层前置拦截，一般到不了这里） |
| 其它规划失败、无 plan 返回、规划超时 | 4 |
| `GetCartesianPath` 的 `fraction` 不足 | 5 |

`message` 里带上原始数值（`error_code` 原样、`fraction` 原样）——**别丢**，这是排查的唯一线索。

### 8.4 规划场景（本轮不做）

不往 planning scene 里加任何障碍物/附着物体（原语级接口不需要）；`TARGET_POSE` 的可达性
完全由 IK 决定。将来做"抓取时把物体当障碍物/附着体"时，本层接口需要扩展（Goal 里加
"任务场景"字段），**那是视觉+抓取规划阶段的事**。

---

## 九、应用层设计

### 9.1 业务节点的标准形状

一个业务节点 = 一个非阻塞状态机 + 一个 `ExecuteMotion` action client：

```
main(): rclcpp::spin(node)
  ├ 定时器 tick(50ms) 推进一步
  ├ action client: send_goal_async → get_result_async（非阻塞 future）
  └ 任何阻塞操作都拆成"等 future"的 tick
```

**不许**在回调里 `sleep` / `spin_until_future_complete`（陷阱 #57）。

### 9.2 `safe_park_node` 的迁移路径

`safe_park` 现在**绕过 MoveIt** 直发 JTC 单点轨迹，并且**已真机验收通过**
（空程残差 0.0015 rad、大行程 0.025~0.031 rad）。迁移要**行为不变**，分三步：

| 步 | 动作 | 验收 |
|---|---|---|
| 1 | 加一个参数 `motion_backend:=direct\|arm_motion`，保持默认 `direct`（现状） | 真机回归：与迁移前逐项一致（同一 `park_pose`、同一 `vlim`、残差同量级） |
| 2 | 切 `arm_motion`，`park_pose` 改从 `named_targets.yaml` 的 `park` 读 | mock 先过；再真机跑，与第 1 步的日志逐项对拍 |
| 3 | 删掉 `direct` 分支与硬编码 `kDefaultParkPose` | `named_targets.yaml` 成为折叠位唯一真源（§5.3） |

⚠️ 第 3 步是**删代码**，要单独确认。⚠️ 迁移后 `safe_park` 的"臂仍使能"语义**不能变**
（收臂不负责失能，见 AGENTS §2 陷阱 #55 的澄清）。

### 9.3 未来的 `pick_place_node`（本轮只定位，不设计）

它属于应用层，职责：收物体位姿（§十）→ 生成抓取位姿（含接近方向）→ 编排
"预抓取点 → 直线下降 → （夹爪闭合）→ 直线抬起 → 放置点 → 下降 → （夹爪张开）→ 抬起"。
**每一步都是一次 `ExecuteMotion`**（直线段用 `linear=true`）。夹爪动作不在本层。

关键约束（给未来的自己）：编排必须能中断在任何一步，且中断后臂的状态是**已知且安全**的
（停在原地保持使能）。这条决定了 §7.2 的"默认拒绝抢占"不能改成"默认抢占"。

---

## 十、视觉接口契约（本轮冻结，不实现）

### 10.1 边界

**视觉只回答"东西在哪、什么姿态"；"怎么下爪"由应用层算**（D-M6）。理由：抓取位姿依赖末端
几何与夹爪开口，那些属于机械臂侧；视觉不该随夹爪变化而改。

### 10.2 消息（两个新 msg）

```rosag
# arm_msgs/msg/DetectedObject.msg —— 单个物体
string id                              # 稳定标识（跨帧跟踪用；本轮检测不做跟踪，先填 "0"）
geometry_msgs/PoseStamped pose         # 物体在 base_link 下的 6D 位姿（位置 = 几何中心）
geometry_msgs/Vector3 size             # 物体尺寸（m）；立方体三边相等
float64 confidence                     # 0~1
builtin_interfaces/Time first_seen      # 首次检出时刻（为将来"稳定 N 帧才抓"留字段）
```

```rosag
# arm_msgs/msg/DetectedObjectArray.msg —— 一帧的全部检出
DetectedObject[] objects
```

- 话题：`/vision/detected_objects`（`DetectedObjectArray`），QoS 建议
  `KeepLast(1).reliable()`（检测频率低、丢一帧无所谓，可靠性优先）；
- 坐标系：`pose.header.frame_id` 必须 `base_link`（与 §4.4 一致）；
- 无检出 ⇒ 发**空数组**，而不是不发（否则消费者无法区分"没有"和"节点挂了"）；
- ⚠️ 用了 `builtin_interfaces/Time` ⇒ 实现时要在 `rosidl_generate_interfaces` 的
  `DEPENDENCIES` 里加 `builtin_interfaces`（`find_package` 那行已经有了，只是没进 DEPENDENCIES）。
  `ExecuteMotion.action` 没有这个问题（只用 primitives + `geometry_msgs`）。

### 10.3 已知的硬件前提（记录，不构成设计约束）

- 相机 = **单目 RGB 固定支架**（已定）。单目给不出深度 ⇒ 深度信息来自**标定过的假设**
  （物体落在已知工作平面上）或已知尺寸的几何约束。**这些都在视觉侧解决**；
- `size` 字段是"视觉声称的尺寸"，抓取侧要能容忍它与实物不一致（可作为可信度的一部分）。

### 10.4 时效性

`DetectedObject` 带时间戳。消费者（应用层）负责判"这帧是不是太旧"（建议 0.5 s 量级，
具体值等真机测出检测周期再定）。**本层不做时间对齐、不做插值**。

### 10.5 本轮明确不做

检测算法（OpenCV/HSV 阈值/轮廓过滤）、相机标定、手眼标定、跟踪、抓取位姿生成
（`grasp_planner`）、多物体分拣策略。

---

## 十一、启动拓扑

### 11.1 一个总 launch

现状是**三份独立 launch**：`arm_bringup/real_control.launch.py`（真机 ros2_control）、
`arm_bringup/mock_control.launch.py`（mock）、`arm_moveit_config/move_group.launch.py`
（`use_mock:=true` 时**自带** mock 的 CM + 控制器）。拼装时必须避开两个坑：

- **mock 与真机互斥**（陷阱 #35）：真机那套开着时起 mock，mock 的 CM 会从共享话题
  `/robot_description` 订阅到**真机的 URDF** ⇒ 加载 `DmSystemInterface`、开真机串口、
  把 6 台使能了（真机踩过：`joint1 ERR=13` 锁存）。所以总 launch 用**一个** `use_mock`
  贯穿，绝不允许两条分支同时为真；
- **只会起一个 RViz**：`real_control.launch.py` 起 `display.rviz`，`move_group.launch.py`
  起 `moveit.rviz`，都默认 `use_rviz:=true` ⇒ 总 launch 必须只让一个为真。

拟新增 `arm_bringup/launch/arm_system.launch.py`：

```
arm_system.launch.py
  use_mock=true  : move_group.launch.py(use_mock:=true, use_rviz)   # 自带 CM+控制器
  use_mock=false : real_control.launch.py(use_rviz:=false, ...)      # 真机硬件
                   move_group.launch.py(use_mock:=false, use_rviz)
  总是           : arm_motion 的 motion_server 节点
```

### 11.2 参数表（总 launch）

| 参数 | 默认 | 说明 |
|---|---|---|
| `use_mock` | `true` | 贯穿两个 launch；**mock/真机互斥开关** |
| `enable_on_activate` | `false` | 真机使能开关；默认只读（现有语义不变） |
| `spawn_arm_controller` | `true` | 总 launch 需要 JTC 存在（`arm_motion` 要发目标） |
| `gravity_ff` / `gravity_ff_scale` | `false` / `1.0` | 透传给 `real_control.launch.py` |
| `mit_controllers` | `false` | 同上 |
| `vlim` | `1.0` | POS_VEL 速度上限（透传） |
| `use_rviz` | `true` | 只作用在**一个** launch 上 |
| `plan_only` | `false` | `arm_motion` 只规划不执行（只读硬件下做规划验证用，见 §11.3） |
| `motion_params` / `named_targets` | 包内默认 | 可覆盖 |

### 11.3 `plan_only` 与只读模式

默认 `enable_on_activate:=false` 时硬件只读：JTC 会接受轨迹但电机不动 ⇒ 残差巨大 ⇒
误报 error 8。所以：**只读硬件下用 `plan_only:=true`**（走到 PLANNING 结束，返回轨迹信息、
不产生任何输出）；或干脆不给目标。这条要写进 `arm_system.launch.py` 的头部注释。

---

## 十二、异常矩阵

| # | 失败模式 | 现象 | 处置 | 臂的最终状态 |
|---|---|---|---|---|
| 1 | 参数非法 | 关节数/名字不匹配、frame 不是 `base_link` | error 1，VALIDATING 拒绝 | 不动，一帧未发，仍使能 |
| 2 | 关节目标越界 | 比如 joint2 给 +99 rad | error 2，**发帧前**拒（陷阱 #59） | 不动，仍使能 |
| 3 | 起始状态越界 | j1 比下界低 0.02 | error 12，前置拦截（陷阱 #37） | 不动，仍使能 |
| 4 | 没有 `/joint_states` | QoS 不匹配（陷阱 #58）或广播器没起 | error 12，`message` 提示查 QoS | 不动 |
| 5 | 命名目标不存在 | 打错名字 | error 9 | 不动 |
| 6 | IK 无解 | 位姿在臂长之外 | error 3 | 不动 |
| 7 | 规划失败/无解 | OMPL 超时 | error 4，带原始 `MoveItErrorCodes` | 不动 |
| 8 | 直线完成度不足 | 目标绕不过去，`fraction=0.7` | error 5，**不执行**（执行半截更危险） | 不动 |
| 9 | 服务忙 | 已有目标在跑且未 `preempt` | error 10 | 前一个目标不受影响 |
| 10 | 执行超时 | 轨迹卡住 | 取消 ⇒ error 6 | 停在取消那一刻，仍使能 |
| 11 | 被抢占 | `preempt=true` 的新目标到达 | 旧目标 error 7（先取消再发新的） | 停在取消那一刻，仍使能 |
| 12 | 硬件报错 | CM 报硬件错误 / 电机 ERR=13 等 | error 11，透传原因 | 由驱动层决定（可能已失能 ⇒ 塌臂，陷阱 #39） |
| 13 | 执行完成但残差大 | POS_VEL 稳态残差 / MIT 死区 | error 8，`max_residual_rad` 回填 | 停在原处保持使能 |
| 14 | 取消确认超时 | `move_group`/JTC 1 s 内不回 | 上报"取消未确认"，**不再等**（陷阱 #60） | 可能仍在动；日志必须说清楚 |
| 15 | `move_group` 没起 | 规划服务一直不在 | PLANNING 阶段 5 s 超时 ⇒ error 4 | 不动 |
| 16 | 反馈丢失 | `/joint_states` 断流（真机串口掉线，陷阱 #28） | VERIFYING 超时 ⇒ error 8/6 | 停在原处（驱动层的 `max_fail_streak` 会 FATAL 退出） |

**共同约定**：任何失败路径都**不失能、不停之外的地方**（臂保持在当前姿态、硬件仍 ACTIVE、
插件继续发保持帧）。唯一会真失能的是驱动层自己的故障路径（`on_deactivate` / ERR 处理）。

---

## 十三、里程碑与验收

**mock 优先**：M8.1~M8.5 全部在 mock 上验收，真机只在 M8.6 出现。

| # | 内容 | 验收标准（可复现） |
|---|---|---|
| M8.1 | `arm_msgs` 加 `ExecuteMotion.action`；新建 `arm_motion` 包骨架 + `named_targets.yaml` + `motion_params.yaml`；server 节点能起、能收 goal、能回 error | `ros2 interface show` 正确；`colcon build` 通过；启动期非法 yaml 会 FATAL |
| M8.2 | VALIDATING + 关节目标（`plan_only`） | mock 下发越界目标 ⇒ error 2；合法目标 ⇒ 拿到非空轨迹 |
| M8.3 | EXECUTING + VERIFYING（关节目标闭环） | mock 下残差 < 阈值；故意给不可达目标 ⇒ error 3/4 |
| M8.4 | 位姿目标 + 直线段 | `linear=false/true` 两条都通；构造一个绕不过去的直线 ⇒ error 5 |
| M8.5 | 互斥 / 取消 / 超时 / 残差 / 抢占（**回归重点**） | 忙时第二个目标 ⇒ error 10；超时后**臂不再自己走完**（陷阱 #60 的回归：对比取消确认前后轨迹推进量） |
| M8.6 | 真机验收（只读 → 使能 → 慢速轨迹） | 沿用现有三步：只读校验 → 保持不动（零位移）→ 慢速关节目标 / 小位姿目标 / 直线段；全程 err=1、无堵转、残差在 §7.3 量级 |
| M8.7 | `safe_park` 迁移（§9.2 三步） | 每步都有真机日志对拍；迁移后残差与迁移前同量级（0.0015~0.031 rad） |
| M8.8 | 视觉契约落地（等检测就绪） | 假发布者按 `DetectedObjectArray` 发位姿 ⇒ 应用层能直接喂给 `ExecuteMotion` |

**每次真机验收前的固定动作**（沿用现有守则）：只读确认姿态与 ERR ⇒ 检查起始状态在限位内
⇒ 慢速 ⇒ 逐项记录残差与 effort。

---

## 十四、不做什么（本轮与近期都不做）

1. **夹爪**（含 URDF 接上、控制器、质量辨识）；
2. **视觉检测实现**（OpenCV/HSV/轮廓、相机标定、手眼标定、跟踪）；
3. **视觉伺服**（look-then-move 之外的一切闭环；延迟耦合风险高、收益低）；
4. **碰撞检测 / planning scene 障碍物 / 附着物体**；
5. **安全认证层**（本层不是安全层，见 §6.5）；
6. **多控制器切换 / MoveIt 执行监控**（D-M2 的代价，显式接受）；
7. **速度模式(3) 与力位混控的 C++ 路径**（AGENTS §3 已列未实现）；
8. **多臂 / 多机 / 任务队列 / 抓取位姿生成**；
9. **重试策略**（属于应用层，本层只上报失败）。

---

## 十五、未决问题（需要后续拍板）

| # | 问题 | 现状 / 倾向 |
|---|---|---|
| 1 | `arm_msgs/action/MoveToPose.action`（已存在、无引用）怎么处理 | 本轮**暂留不动**；将来与 `ExecuteMotion` 功能重叠，要么删（需二次确认绝对路径）、要么明确定义为"仅位姿目标的简化接口" |
| 2 | 直线段的完成度阈值 | 文档用 0.95，需按桌面场景实测调整 |
| 3 | 残差阈值 `max_residual_rad` | 倾向 0.05（与 `safe_park` 的 `tol_rad` 一致），MIT 下要按 `kp` 重估（`kp=25` 实测 max 0.0385） |
| 4 | `TARGET_POSE` 的工作空间粗检（§6.4） | 未定；先靠 IK 反馈 |
| 5 | 是否需要"特权抢占"通道（例如未来急停/换目标专用的话题） | 未定；`preempt=true` 目前够用 |
| 6 | `DetectedObject` 的跟踪字段（`id`/`first_seen`）是否真会被用上 | 先留着，成本极低 |
| 7 | 检测频率 vs 时效阈值（§10.4） | 等真机测出检测周期再定 |
| 8 | 是否需要 `plan_only` 之外的"预演"（例如返回轨迹给外部工具） | 未定 |
| 9 | `arm_motion` 是否需要 `dry_run` 硬件互锁（检测 `enable_on_activate` 是否为真） | 未定；倾向只靠 `plan_only` 参数与文档纪律 |

---

## 附：本文引入的新事实与单一真源

| 事实 | 唯一真源 |
|---|---|
| 运动接口契约（目标类型/字段/错误码） | `arm_msgs/action/ExecuteMotion.action`（本文 §4 为其设计说明） |
| 命名目标位姿 | `arm_motion/config/named_targets.yaml` |
| 速度/加速度缩放默认值、超时、残差阈值、直线完成度阈值 | `arm_motion/config/motion_params.yaml` |
| 关节限位（模型坐标） | `arm_description` 构建期生成的 `arm_limits`（真源 `joint.yaml` + `align.yaml`）——**本层只是消费者** |
| 执行出口 | `arm_controller`(JTC)；`arm_motion` 是唯一发布者（D-M2） |
| 视觉输出 | `/vision/detected_objects`（`DetectedObjectArray`），本轮只冻结消息定义 |
