# 分层契约（LAYERS）—— 硬件层 / 控制层 / 运动层 / 管理层 的作用与边界

> **这是什么**：本工程分层的**顶层契约**。每层的职责、边界、依赖方向、对外接口、以及"什么代码不该在哪一层"。
> **读法**：改任何一层之前先读本文；某层内部细节看对应设计文档（`MOTION_ARCH.md` = 运动层、`MANAGE_ARCH.md` = 管理层、
> `DESIGN.md` = 电机驱动层意图）。**冲突时以本文为准**（本文晚于它们，见 §9）。
> **由来**：2026-10-11 起因"控制律放错了层"（重力补偿写在硬件插件里）暴露出来的分层问题，逐层明确后重构。
>
> ## ⚠️ 当前状态（2026-10-14）：**MIT 路径已整体砍掉**
>
> 下面这段取代了之前的"R1 已落地 / R2 进行中"。**本文曾经描述的 `effort` NaN 语义选模式、
> 运行期模式切换、控制层 `arm_controllers`、重力前馈，在 C++ 侧全部不存在了。**
>
> | 曾经的计划 | 现在 |
> |---|---|
> | `effort` 的 NaN 语义选控制模式（D-L1） | ❌ **作废**。C++ 侧**只有 POS_VEL**（`0x0A=2`，启动/使能时写一次） |
> | 运行期模式切换（一次一台写 `0x0A`） | ❌ **已删**。只有一种模式，没有可切换的东西 |
> | 控制层 `arm_controllers`（`GravityModel` + `GravityFfLaw`） | ❌ **整包已删**。重力前馈是 MIT 专属，POS_VEL 下固件积分自己抗重力 |
> | `dm_joint` 的 `set_mit` / `clamp_mit_torque` / `torque_max` | ❌ **已删**。`dm_joint` 只做 POS_VEL |
> | `arm_description/config/mit_gains.yaml`（`kp_hold`/`kd_hold`/`ki_hold`/`friction_*`） | ❌ **已删**。C++ 侧一个都不读 |
> | `ros2_controllers_mit.yaml`、launch 的 `gravity_ff`/`mit_controllers` | ❌ **已删** |
> | `effort` 命令接口 | ✅ **保留为"空插槽"**：导出，但本层**永远不读**。将来做柔顺/力控时，力矩从这里进来 |
> | `dm_frames::mit_frame`、`float_to_uint`、与 Python 的逐字节对拍 | ✅ **保留**。那是**协议**层；Python `joint.py` 仍支持 MIT，两份实现必须继续咬住 |
> | Python `motor_driver`（`joint.py`/`dm_gravity.py`/`gravity_teach.py`） | ✅ **不动**。工具层，也是"将来要把 C++ 的 MIT 加回来"时的参考实现 |
>
> **决定依据（2026-10-14 真机只读实测，未使能、未写寄存器）**：折叠位**失能 5 s 零漂移**
> （六台全部 `0.000000 rad`），`torque` 全在 **±0.02 N·m 的 12 位量化噪声**内
> ⇒ 折叠位的重力由**机械结构**承载，电机没出那个力 ⇒ 那条重力前馈在**日常姿态上是空转**。
> 而本链要的是"停得住、走得准"：**POS_VEL 的固件位置环自带积分、静态精度最好**，
> MIT 反而要宿主自己把重力补回来才能追平它。⇒ 为 MIT 付出的复杂度（模式双路、控制层整包、
> 一个 500 ms 看门狗下还要保证每周期喂帧的模式状态机）**没有对应的收益**。
>
> **代价（如实记下，别让后来者以为没有）**：**MIT 一旦砍掉，关节力矩通路就没有了**。
> 将来要做**柔顺控制 / 拖动示教 / 力控 / 碰撞回弹**，必须把它整套加回来：
> `dm_joint::set_mit` + 插件的 `effort` 分支 + 模式写入 + 一个重力（或阻抗）控制器。
> 协议层不用重写（`dm_frames` 还在，且仍与 Python 对拍），所以**代价是可逆的、但不是一个下午的量**。
> 被砍的完整实现在分支 `refactor/layers-r1-r2`（commit `8840e26`）备查。
>
> **两条当年核实过、现在仍然成立的 ros2_control 事实**（砍 MIT 之后它们不再挡路，但别重复踩）：
> 1. **控制器串联**：框架有 `ChainableControllerInterface`（`export_reference_interfaces()`），
>    但本版 `joint_trajectory_controller::JointTrajectoryController` 继承的是
>    `ControllerInterface`，而后者把 `is_chainable()`/`export_reference_interfaces()` 都做成
>    `final` 返回"不支持"。⇒ **JTC 不可串联**，要塞一个力矩控制器到它后面得继承 JTC 自己导出参考接口。
> 2. **命令接口独占且不会自动复位**：`LoanedCommandInterface` 的析构**只调 `deleter_()` 解除占用**，
>    **不改命令值**。⇒ "控制器失活 ⇒ `effort` 自动回 NaN"这个当年 D-L1 依赖的假设
>    **ros2_control 没有保证**。现在没人写 `effort`（它是空插槽），所以这条暂时不咬人；
>    **将来接了力矩控制器就必须先解决它**（否则失活后硬件会一直施加那个陈旧的力矩）。

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
   └──────────────▶ 控制层  arm_controllers                ← ⚠️ **当前是空位**（见 §2）
   │                        │ ros2_control 控制器：声索 position
   │                        ▼
   │                    硬件层  DmSystemInterface           帧 / 串口 / 缓存 / 物理钳位 / 使能 / 看门狗
   │                        ↕ CAN（达妙电机）
   │
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
| **硬件层**<br>`motor_driver_hardware` | 串口与帧 I/O；把上层给的目标**装配成 CAN 帧**（**只有 POS_VEL 一条路径**）；写一次控制模式 `0x0A=2`；使能/失能（电气动作）；反馈 → 状态接口；**物理**边界（PMAX）；500 ms 看门狗喂帧；ERR 上报 | 控制律（τ_g / 摩擦 / 积分 / 守卫策略）；轨迹插值；**模型软限位**；业务流程（收臂）；命名目标；"该不该使能"的决策 | `arm_msgs` + 自己的 `dm_*` 库 | 100 Hz，`write()` 内**零阻塞**（陷阱 #24/#25） |
| **控制层**<br>`arm_controllers` ⚠️ **空位** | **将来**放控制律：柔顺/阻抗、力矩整形、需要"按力矩算"的东西 | 硬件 I/O、使能/失能、串口、**拒绝非法目标**（运动层的活）、业务流程 | 状态接口 + 模型数据（URDF/`gravity_identified`）+ 自己的 config | 与 `update()` 同频 100 Hz；**无阻塞、无堆分配** |
| **运动层**<br>`arm_motion` | 目标校验（限位 / 关节名 / frame / 命名目标）、问规划层、拿轨迹、用 JTC 执行、残差验证、错误码、feedback、取消/抢占 | 控制律、硬件 I/O、使能（要求上层已 ARMED）、IK/四元数数学 | `arm_msgs`(ExecuteMotion)、MoveIt 的 action/service（**不链 `moveit_ros_planning_interface`**）、JTC 客户端、`/joint_states`、`joint_limits.hpp` | 事件 + 50 ms tick；**零阻塞** |
| **管理层**<br>`arm_manage` | **唯一状态机 + 顺序**：控制器 inactive → 硬件使能 → 校验 → 控制器 active；收臂（**调运动层 `park`**）→ 停控制器 → 失能；急停绕过编排；对外 3 个服务 + `/arm/status` | 自己发轨迹、控制律、直接碰硬件（只经插件服务与 CM 的切换服务） | `arm_msgs`、CM 的 list/switch、插件 srv、**运动层 ExecuteMotion** | 20 ms tick；零阻塞；对外一次只做一个 op |
| **应用层**<br>`arm_application` | 业务动作编排（抓/放/收臂）、与视觉/夹爪交互 | 控制、规划细节、直接发 JTC | `arm_manage` 的 3 个服务 + `arm_motion` 的 ExecuteMotion | 事件驱动 |
| **规划层**<br>`arm_moveit_config` + `move_group` | IK / 规划 / 时间参数化（TOTP） | **执行**（D-M2：一律 `plan_only=true`） | URDF / SRDF / kinematics | 规划请求级 |
| **工具层**<br>`motor_driver`（Python） | 标定、单机排障（`dm_bringup`）、重力手推演示（`gravity_teach`）；**允许厚防御**（工具要容错、要能对拍） | **不参与运行期**；运行期只认 C++ | vendored SDK + pyserial | 人工调用 |

**关于控制层这个"空位"**：它**不是**待办清单上的欠账，而是**当前架构的正确形状** —— 本链的静态精度
由硬件（固件位置环）拿到，没有需要宿主每周期算的东西。留这一层的理由只有一个：
**将来做柔顺/力控时，那一坨东西有地方放**。所以：

- ❌ **不要**因为它空就往里塞"顺便算个重力补偿"（那就是被砍掉的那条路，理由见文首）；
- ❌ **不要**把它从本文里删掉 —— 删了，将来力矩控制器就没有约定的落点，容易又写回硬件插件；
- ✅ `effort` 命令接口同理保留（空插槽），它和这一层是**同一件事的两端**。

---

## 3. 各层对外接口（唯一出口）

| 层 | 对外暴露 |
|---|---|
| 硬件层 | 状态接口 `position/velocity/effort`；命令接口 `position` + **`effort`（空插槽，本层不读）**；srv `/arm/set_enabled`、`/arm/emergency_stop`；topic `/arm/hardware_status`（10 Hz latched） |
| 控制层 | ⚠️ 当前只有 JTC（`arm_controller`）：只声索 `position`。**没有任何控制器声索 `effort`** |
| 运动层 | action `/arm_motion/execute_motion`（`arm_msgs/action/ExecuteMotion`） |
| 管理层 | srv `/arm_manage/{arm_on,safe_disarm,emergency_stop}`；topic `/arm/status`（latched） |
| 应用层 | 业务自己的 action/topic（尚未定义） |

---

## 4. 控制模式（**只有 POS_VEL**）与 `effort` 空插槽

**事实**：C++ 侧的控制模式**不是可选项**。硬件层在 `on_activate` 与运行期使能序列里
**逐台写一次 `0x0A = 2`**，之后每周期只发 POS_VEL 帧。

**为什么每次都要写**：`0x0A` 是 **RAM**，掉电复位（陷阱：模式是声明不是读数）。
不写的话，后面使能时电机还在默认/旧模式，我们会用**错误模式的帧**去驱动它。
⚠️ 它不是控制帧，所以**只读模式（`enable_on_activate=false`）下也照写**。

**`effort` 命令接口为什么留着**：

- 它是 ros2_control 的**标准力矩口**。删掉之后，将来想加力矩控制器还得改 URDF/xacro（模型文件），
  而保留它只是"一个永远 NaN 的数组"；
- 它与控制层那个空位是**同一件事的两端**：有了口，将来才有地方把力矩写进来；
- ⚠️ **本层永远不读它**。没有任何控制器声索它 ⇒ 它恒为 NaN。
  如果哪天有人写了它而硬件仍然只发 POS_VEL，那是**静默无效**的 —— 这正是留着它时要防的误解，
  所以本文件与 `AGENTS.md` §0.5 都明写"空插槽"。

**被砍掉的 MIT 长什么样**（只作历史，不要照着重写）：
`effort` 有限 ⇒ 硬件发 MIT 帧（`q_des = position`、`kp/kd = kp_hold/kd_hold`、`tau_ff = effort`），
`effort` 是 NaN ⇒ 发 POS_VEL 帧；模式由 `pick_mode(effort)` 每周期判、不一致就一次一台写 `0x0A`。
它需要一整套运行期模式状态机，而它的**唯一收益**是"能在 MIT 下补重力"。

---

## 5. 安全边界的分工

| 内容 | 放哪 | 理由 |
|---|---|---|
| PMAX 钳位 | **硬件层** | 物理边界，必须独立于控制器是否激活 |
| 使能补帧 / 激活保持帧（陷阱 #41 / #52） | **硬件层** | "使能到控制器起来"之间没有控制器在跑，而且**位置类模式切进来时电机内部指令会被清零**（陷阱 #2） |
| 500 ms 看门狗喂帧（陷阱 #24/#25） | **硬件层** | 已使能关节的喂帧空档不许超过 `0x09`；任何回调里不许 >100 ms 阻塞 |
| ERR 上报（`read()` 里 ERR∉{0,1} ⇒ REPORT ERROR） | **硬件层** | 硬件唯一能自己看见的故障信号 |
| 模型软限位（应用层护栏 / MoveIt） | **运动层**（入口拒绝，陷阱 #59） | 策略问题，不是物理问题 |
| 起始状态越界（陷阱 #37） | **运动层**（规划前比对） | 同上 |
| 力矩钳位 `torque_max`、`kp` 上界检查（陷阱 #51） | ❌ **不存在了**（随 MIT 一起删） | 只有 MIT 会施加力矩；POS_VEL 的力矩由固件位置环自己限 |
| 残差守卫（阈值/宽限/防抖，陷阱 #42） | ❌ **当前没有落地** | 它当年是控制层的活；控制层现在是空位 ⇒ **这段没有残差保护**。要么接受（POS_VEL 下跑飞的表现是"跟不上"，由 JTC 的容差与 `MotionClient` 的残差验证兜住），要么把它做成运动层/管理层的检查并写清归属 |

---

## 6. 数据与参数归属（单一真源）

| 数据 | 归属包 | 为什么 |
|---|---|---|
| URDF/xacro、`align.yaml`、`gravity_identified.yaml`、生成的 `joint_limits.hpp` | `arm_description` | **模型数据**（几何/质量/坐标系/限位），谁都要读 |
| `vlim` / PMAX / `device` / `baud` / `enable_on_activate` | `motor_driver_hardware`（经 xacro `<param>`） | **"这台电机怎么接"**；使能补帧与物理钳位要用 |
| `direction` / `offset` / 软限位 | `motor_driver/config/joint.yaml`（经 xacro `<param>`） | 本项目自己标定的真源（**Python 与 C++ 都从它推**） |
| `sign` / `zero_shift` | `arm_description/config/align.yaml`（经 xacro `<param>`） | 模型对齐真源 |
| 折叠位 / 命名目标 | `arm_manage/config/arm_manage.yaml`（`park`）+ `arm_motion/config/named_targets.yaml`（其余命名点） | `park` 是管理层的动作目标，只存一份、运动层按名字取 |
| 控制器清单与类型 | `arm_bringup/config/ros2_controllers.yaml`（**只剩这一份**） | 装配信息 |
| `kp_hold`/`kd_hold`/`ki_hold`/`friction_*` | ❌ **MIT 砍掉后 C++ 侧不再读**。`arm_description/config/mit_gains.yaml` **已删**。⚠️ `motor_driver/config/teach.yaml` 里仍有引用那**文件名**的注释（`gravity_teach` 工具用），加回 MIT 时要一并恢复 | — |
| τ_g 缩放 / 摩擦 / 积分 / 守卫阈值 | ❌ 无归属（控制层为空位）。当年计划放 `arm_controllers/config/` | — |

---

## 7. 越界清单（现在的实际状态）

| # | 现状 | 目标 / 说明 |
|---|---|---|
| 1 | 控制律**不在**硬件层 ✅（R1 消除，且 MIT 砍掉后连控制层也不需要它） | 已完成。`dm_hardware` 不依赖 pinocchio，只有帧装配 |
| 2 | 控制模式**不是参数** ✅（固定 POS_VEL；`gravity_ff` 参数已删） | 已完成 |
| 3 | ~~增益由插件读~~ → **C++ 侧已不读任何增益** ✅ | `mit_gains.yaml` 已删 |
| 4 | 限位校验散在三处（`MotionClient` / 硬件层 / 管理层） | **未收敛**：收敛到**运动层入口**；硬件层只钳 PMAX |
| 5 | 管理层自己发轨迹（`ControllerGate` 持有 `MotionClient`） | `safe_disarm` 调 **`ExecuteMotion(named=park)`**（D-L4）—— **未做** |
| 6 | 应用层 `safe_park_node` 直发 JTC | 改成 `ExecuteMotion` 的 client（D-M2 单一执行出口）—— **未做** |
| 7 | Python 与 C++ 两套语义并存、边界模糊 | 明确 Python = 工具层（D-L5）✅ 本文已定；对拍是**测试手段**，不是两个运行期实现 |
| 8 | **MIT 砍掉之后**：`effort` 空插槽没有任何消费者；控制层是空位 | **这是有意的**（见 §2 末），不是待清理的垃圾。别"顺手删掉"或"顺手填上" |
| 9 | **残差守卫没有归属**（§5 末） | 要么明确"不需要"（POS_VEL 下由 JTC 容差 + `MotionClient` 残差验证兜）并写进本文，要么指定归属 —— **待定** |

---

## 8. 重构序列（每步单独验收）

| 步 | 内容 | 验收方式 | 真机风险 |
|---|---|---|---|
| **R0** | 本文（`docs/LAYERS.md`） | 逐条评审 | 无 |
| **R1** | ~~硬件层瘦身：加 `effort` 命令口（NaN 语义）+ 删控制律~~ **↩️ 已被下面的 R1' 取代一部分** | — | — |
| **R2** | ~~新建 `arm_controllers` + 重力补偿控制器~~ **❌ 已作废并回滚**（整包删除） | 备查快照：分支 `refactor/layers-r1-r2`（`8840e26`） | — |
| **R1'** | ✅ **已完成（2026-10-14）**：**砍掉整个 MIT 路径** —— `dm_joint` 只做 POS_VEL、插件固定 `0x0A=2`、删 `dm_frame_plan`/`arm_controllers`/`mit_gains.yaml`/`ros2_controllers_mit.yaml`、`effort` 留空插槽；**保留**协议层的 MIT 帧与 Python 对拍 | 干净重建 9 包；`motor_driver_hardware` 26 + `arm_motion` 9 + `arm_msgs` 4 = **39 tests 全绿**；xacro 展开核对接口集与参数 | 低（mock/静态验证；⚠️ **未做真机验收**） |
| **R3** | 运动层 `ExecuteMotion`（原 M8.0→M8.3） | mock 链 + `move_group`（⚠️ 别用 `use_mock:=true`，会双开） | 低 |
| **R4** | 管理层收敛：`safe_disarm` → `ExecuteMotion(named=park)`；删 `ControllerGate` 的 `MotionClient` | 重跑 M9.3 的 mock 断言 | 中 |
| **R5** | 应用层迁移到 ExecuteMotion；删直发路径 | mock + 真机 | 中 |
| **R6** | 真机分级验收（照 `TESTING.md` §十九 的手法）+ 文档收敛 | 真机（需人在场） | **有** |

**下一步该做什么**：R3（运动层）。理由：R1' 之后硬件层与控制层都不欠账，
真机链是"**POS_VEL + JTC + 固件位置环**"，能跑；而 §7 的 #4/#5/#6 三条（执行出口没收敛）
是**结构上还没做完**的部分，它们都在运动层及其上下游。

### 8.1 测试策略（D-L6，原则未变、条数已变）

**原则：按「这段逻辑属于哪一层」来选测法**，而不是把一切塞进插件级端到端。

| 测什么 | 在哪测 | 怎么测 |
|---|---|---|
| 协议帧（含 **MIT 帧**） | `motor_driver_hardware/test/test_dm_frames.cpp` ✅ | 与 Python `dm_frames` **逐字节对拍**（C++ 造帧 + `popen` 调 Python）。⚠️ **MIT 帧仍在这批用例里** —— 协议只有一个真源，Python 侧仍支持 MIT |
| 总线收发 / 缓存 / 寄存器 I/O | `test_dm_bus.cpp` ✅ | 假串口（内存字节流）+ 一个"收到刷新帧就回反馈"的应答器 |
| 单关节换算 / 发帧 / 保持帧 | `test_dm_joint.cpp` ✅ | 与 Python `joint.py` 对拍（**POS_VEL 路径**；MIT 那批随 `set_mit` 一起删了） |
| 插件接线（真的按顺序读写串口） | `test_iface_wiring.cpp`，**4 条** ✅ | 假串口退化成**记录器**。实际保留：① 只读模式一个控制帧都不发（含被写了 `effort`）；② 激活 ⇒ 写 `0x0A=2` **早于**使能帧 + 补 POS_VEL 保持帧 + **一个 MIT 帧都不该有**；③ 命令 NaN ⇒ 保持帧指向**锁定**位置（#38 回归）；④ ERR ⇒ REPORT ERROR（只读时容忍） |
| 端到端 | mock 链（总 launch + `arm_manage` 三服务）→ 真机 R6 | 已有的脚本化验收 |

**已删除的测法**（不要按旧文档去找）：`test_dm_system_interface.cpp`（21 条）及其**模拟电机**（跟位置、置 ERR=1）、
`test_frame_plan.cpp`（10 条纯决策）、`arm_controllers/test/`（14 条含 MuJoCo 对拍）。
前两者随 R1 拆解、后者随 MIT 一起删。⚠️ **接线测试里仍留一台"最小假电机"**：它只回答刷新帧与寄存器帧
（否则 `on_activate` 的 `sync_states` 要等满 8 秒），**不跟位置、不置 ERR**。

**明确放弃的覆盖**（只有真机能测，记在 `TESTING.md` 的真机验收里）：500 ms 看门狗、串口错误路径、ERR=13 锁存、TIOCEXCL 抢口。

---

## 9. 与既有文档的关系 / 被本文取代的条目

| 文档 | 关系 |
|---|---|
| `MOTION_ARCH.md` | 运动层/应用层/规划层的**细化**（D-M1…D-M8 继续有效；D-M2 单一执行出口由本文 §7 #6 落实） |
| `MANAGE_ARCH.md` | 管理层的**细化**；⚠️ 其中"`ControllerGate` 持有 `MotionClient` 并在 `safe_disarm` 里自己发轨迹"**被 D-L4 取代**（改为调运动层） |
| `DESIGN.md` | 电机驱动层的**设计意图**；⚠️ 其中 §D8"只有 MIT 能给力矩"作为**物理事实**仍成立，但**本链不再用 MIT**（见文首） |
| `AGENTS.md` | 当前状态与陷阱（**§0.5 就是这次砍 MIT 的记录**）；本文的改动会**简化**若干陷阱的归属说明，但**不改变** #24/#25/#38/#41/#52 这些硬件层不变量 |

## 10. 已定决策

| # | 决策 | 状态 |
|---|---|---|
| **D-L1** | `effort` 的 **NaN 语义**选模式（NaN ⇒ POS_VEL；有限 ⇒ MIT） | ❌ **作废**（2026-10-14 砍 MIT）。且当年依赖的前提"失活 ⇒ 自动回 NaN"**ros2_control 并不保证**（见文首事实 2） |
| **D-L2** | `kp_hold`/`kd_hold` 留在硬件层；τ_g / 摩擦 / `ki` 归控制层 | ❌ **作废**（两者都不存在了） |
| **D-L3** | 守卫**策略**在控制层；硬件层保留"超时/ERR ⇒ REPORT ERROR"的物理兜底 | 🔄 **一半仍有效**：ERR ⇒ REPORT ERROR 在硬件层（§5）；"守卫策略在控制层"因为控制层是空位而**暂时无归属**（§7 #9，待定） |
| **D-L4** | 管理层的收臂改为调 **`ExecuteMotion(named=park)`**（不再自持 `MotionClient`） | ✅ **有效**、尚未实现（R4） |
| **D-L5** | Python `motor_driver` 只属**工具层**，不定义运行期语义 | ✅ **有效** |
| **D-L6** | 测试按层选测法（纯逻辑单测 + 控制层单测 + 插件只留几条接线用例） | ✅ **原则有效**；具体条数已变（§8.1） |
| **D-L7** | **控制模式只有 POS_VEL**：固定写 `0x0A=2`，不引入运行期模式切换 | ✅ 用户确认（2026-10-14） |
| **D-L8** | `effort` 命令接口**保留为空插槽**（本层不读），控制层留作**空位** —— 两者是将来做柔顺/力控的落点 | ✅ 用户确认（2026-10-14） |
| **D-L9** | **协议层的 MIT 保留**（`dm_frames::mit_frame` + 与 Python 的逐字节对拍）；砍掉的只是 C++ 的**控制路径** | ✅ 用户确认（2026-10-14） |
