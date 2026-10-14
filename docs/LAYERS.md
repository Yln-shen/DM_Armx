# 分层契约（LAYERS）—— 硬件层 / 控制层 / 运动层 / 管理层 的作用与边界

> **这是什么**：本工程分层的**顶层契约**。每层的职责、边界、依赖方向、对外接口、以及"什么代码不该在哪一层"。
> **读法**：改任何一层之前先读本文；某层内部细节看对应设计文档（`MOTION_ARCH.md` = 运动层、`MANAGE_ARCH.md` = 管理层、
> `DESIGN.md` = 电机驱动层意图）。**冲突时以本文为准**（本文晚于它们，见 §9）。
> **由来**：2026-10-11 起因"控制律放错了层"（重力补偿写在硬件插件里）暴露出来的分层问题，逐层明确后重构。
> **重构序列见 §8**；本文是 R0，只有文档、还没动代码。

---

## 1. 全景：四层在通路上，管理层是**正交**的，工具层在旁路

```
应用层   arm_application            业务动作（抓/放/收臂编排）—— 只调运动层与管理层的接口
   │
   ├──────────────▶ 运动层  arm_motion／ExecuteMotion      "去哪" →（问规划层）→ 轨迹 → 执行编排
   │                     │ 调
   │                     ▼
   │                 规划层  move_group（只规划，plan_only；不算控制、不执行）
   │                     │ 轨迹
   │                     ▼
   └──────────────▶ 控制层  arm_controllers                100 Hz 控制律（跑在 controller_manager 进程内）
                           │ 命令接口：position / effort
                           ▼
                       硬件层  DmSystemInterface            帧 / 串口 / 缓存 / 物理钳位 / 使能 / 看门狗
                           ↕ CAN（达妙电机）

管理层   arm_manage          **不在命令通路上**：只改下面几层的 on/off（控制器激活、硬件使能、收臂后失能）
工具层   motor_driver(Python) 标定 / 单机排障 / 重力手推演示 —— **不在运行期链路**
```

**三条不变量**

1. **依赖单向向下**：上层 → 下层，跨层只经 `arm_msgs` 的接口。任何一层不许反向 import/订阅上层。
2. **每层只有一个"执行出口"**：硬件层 = CAN 帧；控制层 = 命令接口；运动层 = JTC 目标；管理层 = 使能/激活状态。
3. **管理层正交**：它不发命令、不算控制，只做**顺序与生命周期**；运动层要动之前，管理层必须已把链置为 `ARMED_CONTROLLER`。

---

## 2. 每层契约

| 层 | 只做 | **不做**（越界即违规） | 依赖 | 时间尺度 / 生命周期 |
|---|---|---|---|---|
| **硬件层**<br>`motor_driver_hardware` | 串口与帧 I/O；把上层给的目标**装配成 CAN 帧**；使能/失能（电气动作）；反馈 → 状态接口；**物理**边界（PMAX、`torque_max`）；500 ms 看门狗喂帧；ERR 上报 | 控制律（τ_g / 摩擦 / 积分）；轨迹插值；**模型软限位**；业务流程（收臂）；命名目标；"该不该使能"的决策 | `arm_msgs` + 自己的 `dm_*` 库 | 100 Hz，`write()` 内**零阻塞**（陷阱 #24/#25） |
| **控制层**<br>`arm_controllers` 🆕 | **控制律**：τ_g（pinocchio）、摩擦前馈、积分整形、目标轨迹与其插值；把结果写进 `position`/`effort`；**守卫的策略**（阈值、宽限、防抖） | 硬件 I/O、使能/失能、串口、**拒绝非法目标**（运动层的活）、业务流程 | 状态接口 + 模型数据（URDF/`gravity_identified`）+ 自己的 config | 与 `update()` 同频 100 Hz；**无阻塞、无堆分配** |
| **运动层**<br>`arm_motion` | 目标校验（限位 / 关节名 / frame / 命名目标）、问规划层、拿轨迹、用 JTC 执行、残差验证、错误码、feedback、取消/抢占 | 控制律、硬件 I/O、使能（要求上层已 ARMED）、IK/四元数数学 | `arm_msgs`(ExecuteMotion)、MoveIt 的 action/service（**不链 `moveit_ros_planning_interface`**）、JTC 客户端、`/joint_states`、`joint_limits.hpp` | 事件 + 50 ms tick；**零阻塞** |
| **管理层**<br>`arm_manage` | **唯一状态机 + 顺序**：控制器 inactive → 硬件使能 → 校验 → 控制器 active；收臂（**调运动层 `park`**）→ 停控制器 → 失能；急停绕过编排；对外 3 个服务 + `/arm/status` | 自己发轨迹、控制律、直接碰硬件（只经插件服务与 CM 的切换服务） | `arm_msgs`、CM 的 list/switch、插件 srv、**运动层 ExecuteMotion** | 20 ms tick；零阻塞；对外一次只做一个 op |
| **应用层**<br>`arm_application` | 业务动作编排（抓/放/收臂）、与视觉/夹爪交互 | 控制、规划细节、直接发 JTC | `arm_manage` 的 3 个服务 + `arm_motion` 的 ExecuteMotion | 事件驱动 |
| **规划层**<br>`arm_moveit_config` + `move_group` | IK / 规划 / 时间参数化（TOTP） | **执行**（D-M2：一律 `plan_only=true`） | URDF / SRDF / kinematics | 规划请求级 |
| **工具层**<br>`motor_driver`（Python） | 标定、单机排障（`dm_bringup`）、重力手推演示（`gravity_teach`）；**允许厚防御**（工具要容错、要能对拍） | **不参与运行期**；运行期只认 C++ | vendored SDK + pyserial | 人工调用 |

---

## 3. 各层对外接口（唯一出口）

| 层 | 对外暴露 |
|---|---|
| 硬件层 | 状态接口 `position/velocity/effort`；命令接口 `position/effort`；srv `/arm/set_enabled`、`/arm/emergency_stop`；topic `/arm/hardware_status`（10 Hz latched） |
| 控制层 | 作为 controller：`<controller>/joint_trajectory`（JTC）或自有的 `~/setpoint` 流式口；参数在 `arm_controllers/config/*.yaml` |
| 运动层 | action `/arm_motion/execute_motion`（`arm_msgs/action/ExecuteMotion`） |
| 管理层 | srv `/arm_manage/{arm_on,safe_disarm,emergency_stop}`；topic `/arm/status`（latched） |
| 应用层 | 业务自己的 action/topic（尚未定义） |

---

## 4. 关键接口设计：`effort` 的 NaN 语义（**取代** `gravity_ff` 参数）

**决定（D-L1）**：硬件层同时导出 `position` 与 `effort` 两个命令接口，用 **`effort` 是不是 NaN** 来选控制模式：

| `effort[i]` | 硬件层发什么帧 | 触发条件 |
|---|---|---|
| **NaN** | **POS_VEL**（`position` 也是 NaN ⇒ 发"保持锁定位置"帧，陷阱 #38） | 没有控制器声索 `effort`（未加载 / 未激活 / 已失活） |
| **有限值** | **MIT**：`q_des = position[i]`、`kp/kd = 硬件参数`、`tau_ff = effort[i]` | 有控制器正在每周期写 `effort` |

**为什么不用新通信**：① 与现有"命令 NaN ⇒ 保持帧"是同一套机制（陷阱 #38），零新概念；② "控制器失活 ⇒ 自动回到 POS_VEL"是**免费**的（没人写 ⇒ 保持 NaN）；③ 模式互斥由 CM **免费**保证 —— 两个控制器不能同时声索同一个命令接口，所以"位置控制"与"力矩控制"天然互斥，不需要我们在插件里写判断。

**沿用不变的**（硬件层仍是最后防线）：`torque_max` 钳位、`enable()` 时补零增益帧与激活保持帧（陷阱 #41/#52）、500 ms 看门狗、ERR 检查。

---

## 5. 安全边界的分工（D-L3）

| 内容 | 放哪 | 理由 |
|---|---|---|
| 力矩钳位 `torque_max`、PMAX 钳位 | **硬件层** | 物理边界，必须独立于控制器是否激活 |
| `kp` 上界检查（`torque_max / 期望滞后`，陷阱 #51） | **硬件层** | 两个操作数（`kp`、`torque_max`）都在硬件层参数里 |
| 使能补帧 / 激活保持帧（#41 / #52） | **硬件层** | "使能到控制器起来"之间没有控制器在跑 |
| **守卫的策略**（阈值、宽限、防抖、按"保持/跑轨迹"分开判） | **控制层**（可配、报事件） | 它是控制律的一部分 |
| 守卫的**粗兜底**（一个很宽的硬阈值 + 固定防抖 ⇒ 直接 REPORT ERROR） | **硬件层** | 安全不能依赖控制器激活 |
| 模型软限位（应用层护栏 / MoveIt） | **运动层**（入口拒绝，陷阱 #59） | 策略问题，不是物理问题 |
| 起始状态越界（陷阱 #37） | **运动层**（规划前比对） | 同上 |

---

## 6. 数据与参数归属（单一真源）

| 数据 | 归属包 | 为什么 |
|---|---|---|
| URDF/xacro、`align.yaml`、`gravity_identified.yaml`、生成的 `joint_limits.hpp` | `arm_description` | **模型数据**（几何/质量/坐标系/限位），谁都要读 |
| `kp_hold` / `kd_hold` / `vlim` / `torque_max` / PMAX / `device` | `motor_driver_hardware`（经 xacro `<param>`） | **"这台电机怎么调/怎么接"**，且使能补帧与钳位要用（D-L2） |
| τ_g 缩放、`friction_c` / `friction_v_eps`、`ki`、守卫阈值 | `arm_controllers/config/` 🆕 | **控制调参**，属于控制层 |
| 折叠位 / 命名目标 | `arm_manage/config/arm_manage.yaml`（`park`）+ `arm_motion/config/named_targets.yaml`（其余命名点） | `park` 是管理层的动作目标，只存一份、运动层按名字取 |
| 控制器清单与类型 | `arm_bringup/config/ros2_controllers*.yaml` | 装配信息 |

---

## 7. 当前违规清单（重构要拆掉的东西）

| # | 现状 | 目标 |
|---|---|---|
| 1 | 控制律在硬件层：`write_gravity_ff()` 的 `refresh_gravity_torque()` → `check_guard()` → `update_integrators()` → `send_mit_frames()`（只有最后一个是 I/O） | τ_g/摩擦/积分/守卫策略搬到 `arm_controllers`；硬件层只装配帧 |
| 2 | "模式"是插件参数 `gravity_ff`（整链二选一） | 改成"**哪个控制器活着**"（D-L1 的 NaN 语义） |
| 3 | 控制增益 `mit_gains.yaml` 由插件读 | 移到控制层 config（D-L2：kp/kd 留硬件，τ/摩擦/ki 归控制层） |
| 4 | 限位校验散在三处（`MotionClient` / 插件 / 管理层） | 收敛到**运动层入口**；硬件层只钳 PMAX |
| 5 | 管理层自己发轨迹（`ControllerGate` 持有 `MotionClient`） | `safe_disarm` 调 **`ExecuteMotion(named=park)`**（D-L4） |
| 6 | 应用层 `safe_park_node` 直发 JTC | 改成 `ExecuteMotion` 的 client（D-M2 单一执行出口） |
| 7 | Python 与 C++ 两套控制语义并存、边界模糊 | 明确 Python = 工具层（D-L5）；对拍是**测试手段**，不是两个运行期实现 |
| 8 | 例外：Python 侧 `arm.py` 的监控/双循环、`Joint._clamp_mit_torque` 等 | 保留（工具/参考实现），但**不再作为运行期语义的定义者** |

---

## 8. 重构序列（每步单独验收）

| 步 | 内容 | 验收方式 | 真机风险 |
|---|---|---|---|
| **R0** | 本文（`docs/LAYERS.md`） | 逐条评审 | 无 |
| **R1** | 硬件层瘦身：加 `effort` 命令口（NaN 语义）；删 τ_g/积分/守卫策略/模型限位/`gravity_ff*` 参数与 `dm_gravity` | 改写 21 个插件 gtest（mock 串口）；`test_dm_gravity` 用例迁 R2 | 低（mock 为主） |
| **R2** | 新建 `arm_controllers` + `GravityCompensationController`（τ_g+摩擦+ki → `effort`） | gtest 与 MuJoCo 对拍（用例从 `test_dm_gravity` 迁来） | 低 |
| **R3** | 运动层 `ExecuteMotion`（原 M8.0→M8.3） | mock 链 + `move_group`（⚠️ 别用 `use_mock:=true`，会双开） | 低 |
| **R4** | 管理层收敛：`safe_disarm` → `ExecuteMotion(named=park)`；删 `ControllerGate` 的 `MotionClient` | 重跑 M9.3 的 25 项 mock 断言 | 中 |
| **R5** | 应用层迁移到 ExecuteMotion；删直发路径 | mock + 真机 | 中 |
| **R6** | 真机分级验收（照 `TESTING.md` §十九 的手法）+ 文档收敛 | 真机（需人在场） | **有** |

### 8.1 测试策略（R1/R2 的测法，D-L6）

**原则：按「这段逻辑属于哪一层」来选测法**，而不是把一切塞进插件级端到端。

| 测什么 | 在哪测 | 怎么测 |
|---|---|---|
| 帧装配决策（POS_VEL / MIT / 保持帧）、钳位（PMAX、`torque_max`、`kp` 上界）、使能 FSM 步进、状态快照 | `motor_driver_hardware/test/test_frame_plan.cpp` | **纯输入→输出**：抽成 `dm_hardware` 里的纯函数/小类，**无 rclcpp、无串口、无假电机**；用例小、毫秒级 |
| 控制律（τ_g / 摩擦 / 积分 / 守卫策略） | `arm_controllers/test/`（R2） | 只喂状态数组、只看算出的 `effort`；**不需要串口，也不需要假电机** |
| 接线（插件真的按上面的决策去读写串口） | `motor_driver_hardware/test/test_iface_wiring.cpp`，**约 6 条** | 假串口退化成**记录器**（只记 `write()` 收到了什么）。保留：① 只读模式一个控制帧都不发；② 激活 ⇒ 写 `0x0A`+锁定保持；③ 命令 NaN ⇒ 保持帧；④ `read()` 填状态接口；⑤ ERR ⇒ REPORT ERROR；⑥ 使能服务路径 |
| 端到端 | mock 链（总 launch + `arm_manage` 三服务）→ 真机 R6 | 已有的脚本化验收 |

**要删掉的东西**：`test/test_dm_system_interface.cpp` 整个文件，以及里面那台**模拟电机**（跟位置 / 使能置 ERR=1 / `kp>0` 才跟随）—— 它存在的唯一理由是喂「控制律」那批用例，而控制律搬去控制层了。

**明确放弃的覆盖**（只有真机能测，记在 `TESTING.md` 的真机验收里）：500 ms 看门狗、串口错误路径、ERR=13 锁存、TIOCEXCL 抢口。

---

## 9. 与既有文档的关系 / 被本文取代的条目

| 文档 | 关系 |
|---|---|
| `MOTION_ARCH.md` | 运动层/应用层/规划层的**细化**（D-M1…D-M8 继续有效；D-M2 单一执行出口由本文 §7 #6 落实） |
| `MANAGE_ARCH.md` | 管理层的**细化**；⚠️ 其中"`ControllerGate` 持有 `MotionClient` 并在 `safe_disarm` 里自己发轨迹"**被 D-L4 取代**（改为调运动层） |
| `DESIGN.md` | 电机驱动层的**设计意图**；§D8"只有 MIT 能给力矩"仍成立，但"谁给力矩"由硬件层改为控制层 |
| `AGENTS.md` | 当前状态与陷阱；本文的改动会**简化**陷阱 #51/#54/#59 的归属说明（见 §5），但**不改变** #24/#25/#38/#41/#52 这些硬件层不变量 |

## 10. 已定决策

| # | 决策 | 状态 |
|---|---|---|
| **D-L1** | `effort` 的 **NaN 语义**选模式（NaN ⇒ POS_VEL；有限 ⇒ MIT），不引入新的模式通信 | ✅ 用户确认 |
| **D-L2** | `kp_hold`/`kd_hold` **留在硬件层**参数；τ_g / 摩擦 / `ki` 归控制层 | ✅ 用户确认 |
| **D-L3** | 守卫**策略**在控制层；硬件层保留"超时/ERR ⇒ REPORT ERROR"的**物理兜底** | ✅ 用户确认 |
| **D-L4** | 管理层的收臂改为调 **`ExecuteMotion(named=park)`**（不再自持 `MotionClient`） | ✅ 用户确认 |
| **D-L5** | Python `motor_driver` 只属**工具层**，不定义运行期语义 | ✅ 用户确认 |
| **D-L6** | 测试按层选测法：纯逻辑单测 + 控制层单测 + **插件只留约 6 条接线用例**；删 `test_dm_system_interface.cpp` 与那台模拟电机 | ✅ 用户确认（§8.1） |
