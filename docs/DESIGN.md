# DmArm 电机驱动层设计文档 —— 设计意图与不变量

> 本文只讲**电机驱动层**（`dm_frames`/`dm_bus`/`dm_modes`/`joint`/`arm_config`/`arm`/`dm_registers`/`dm_bringup`）的**「为什么」与不变量**：哪些约束不能破。当前状态、逐方法实现、陷阱编号不在这里。
> 2026-10-07 精简：过程记录（v0.1~v0.12 逐版修订、D2 带宽账、D4 看门狗 A/B/C 表、mermaid 类图、YAML 示例）已删，git 历史里查 `git show e72f5b9:docs/DESIGN.md`。
## 0. 文档分工（谁是真源）

| 文档 | 内容 |
|---|---|
| [AGENTS.md](../AGENTS.md) | 精确状态（逐方法）+ 单一真源表 + 陷阱清单 —— 改代码先看它 |
| [docs/TESTING.md](TESTING.md) | 实测值（映射范围 / 摩擦 / 标定 / 辨识 / 收臂验收） |
| [docs/LESSONS.md](LESSONS.md) | 教训（SDK 的四个坑 + 方法上的坑） |
| [docs/MOTION_ARCH.md](MOTION_ARCH.md) | 运动接口层（`arm_motion`）/ 应用层 / 规划层 / 视觉契约 |
| **本文** | 电机驱动层的「为什么」与不变量 |

### 0.1 全局章节编号索引表
这几份文档**共用一套章节编号**（编号全局唯一 ⇒ 写 `§N` 时不带文件名也能找到）：

| 编号 | 在哪 |
|---|---|
| §一，§2.1~§2.2，§三（D1~D9，含 D5b），§4.1~§4.4，§七~§九，§11.1~§11.2 | **本文件** |
| §2.6（SDK 的四个坑） | [`LESSONS.md`](LESSONS.md) |
| §2.7，§2.8，§十 | [`TESTING.md`](TESTING.md) |

> **没有 `CHANGELOG.md`** —— 逐条修订在 git log 里；留下的是教训（LESSONS）与实测（TESTING）。

---

## 一、定位与边界
```
[运动层 / 应用层 / 规划层]   ← 见 docs/MOTION_ARCH.md，本文不涉及
      ↓ 调用（经 ros2_control 硬件接口，或直连 Python 包）
[本层：MotorBus / Joint / DmArm / RegisterTool]      ← 本文档的全部范围
      ↓  dm_frames 自构 30B 发送帧 / 16B 收帧（控制路径不依赖厂商 SDK）
达妙 USB2CAN 适配器（UART 侧 921600，CAN 侧固定 1Mbps）→ 电机
```
本层替代 reBot 的 actuator 层与 `hardware_manager.py` 的电机部分；**不复用**它的 kinematics / dynamics / trajectory —— 路线是 MoveIt 算 IK/轨迹、ros2_control 执行。

### 1.1 设计原则

| 原则 | 说明 |
|---|---|
| 单位统一 | 关节 rad / rad·s⁻¹；电机侧一律 rad，**无任何减速比折算** |
| 换算集中 | `direction` / `offset` 只在 `Joint` 里做；寄存器单位只在 `Reg.per_unit` |
| 安全内置 | 限位钳位、双层看门狗、温度/力矩监控、故障码解码 |
| 配置分离 | YAML + dataclass 校验（`arm_config.py`）；不 import rclpy，可脱离 ROS 独立测 |
| 收发解耦 | 默认"只发不等回包"；`poll()` 非阻塞抽干 |
| **可回滚** | **写电机寄存器前必须先读并留存原值** —— `dm_registers` 的 `dump` / `restore` / `--save`（写 flash 前自动 dump 基线）就是这条原则的实现 |

**依赖**：`pyserial` / `pyyaml` 都在 `pixi.toml`（pyyaml 已显式声明；`arm_config` 仍把 `import yaml` 关进 `from_yaml` 一个函数）；厂商 SDK `DM_CAN.py` 已 vendored、**只有 `dm_bringup.py` 用它**（控制路径自构帧）；`numpy` 本层**零 import**；**不使用 `motorbridge`**。
**上层需求（指针）**：三种控制组合落在 `joint.set_pos_vel` / `set_mit` / `set_force_pos`；状态字段落在 `JointState`（§4.1）；关节 ↔ 模型坐标映射见 `align.yaml` 与 C++ `dm_joint` 的 `model_to_ours()`/`ours_to_model()`（三层坐标见 AGENTS §5）；上层节点 / 轨迹 / 收臂 / 视觉**不在本层**。
**与其它模块**：厂商 SDK 只用于 `dm_bringup` 排障（分清"封装错"还是"链路错"）；C++ 侧另有一份实现（`motor_driver_hardware`），与 Python 侧**逐字节/逐数值对拍**；MoveIt 算 IK/轨迹，本层只做电机 I/O、**不实现**运动学 / 控制循环；`arm_description` 提供三份 config 与 URDF（模型限位由 `joint.yaml` + `align.yaml` 算出）；`mujoco_pkg` / `fake_driver_pkg` 已移出工作区（AGENTS §8）。
**数据流**：发（`send_hz` 500）`set_joint_positions` / `set_joint_mit_all` → `prepare_frame`（NaN 拦 → 软限位 → 换算 → PMAX）→ `bus.send_*`（写串口即返回）；收（`feedback_hz` 100）`bus.poll()` → 解析缓冲 → 更新 `MotorState` 缓存 → 监控 → 上层 `get_state()` 读缓存。推论：① `get_state()` 读的是**缓存**，要新鲜数据就得持续 `poll()`；② 发循环单跑时缓存里的位置/力矩是**旧**的；③ 监控挂在**收**循环上，只跑发循环等于没有监控。

---

## 二、事实基线：真源指针 + 两条铁律
原 §2.1 / §2.3 / §2.4 / §2.5 已删（都是从代码里抄出来的副本，违反单一真源）：

| 事实 | 唯一真源 |
|---|---|
| 关节限位 / 方向 / 零位 / 模式声明 / `torque_max` | `src/motor_driver/config/joint.yaml`（本项目自己标定） |
| 模型对齐 `sign` / `zero_shift` | `src/arm_description/config/align.yaml` |
| MIT 保持增益 + 摩擦前馈 / 质量质心 | `arm_description/config/mit_gains.yaml` / `gravity_identified.yaml` |
| 寄存器表（49 条）+ 单位换算 | `dm_registers.py` 的 `Reg` / `Reg.per_unit` |
| 故障码含义 + `ERR_OK` / 协议字节布局 / 模式编码 | `dm_frames.ERR_DECODE` / `ERR_OK`、`dm_frames.py` / `dm_modes.py` |
| 当前状态、陷阱编号 | AGENTS.md §3 / §6 |

### 2.1 硬件分配（表已删）
原表来自旧项目 `~/reBotArm_control_py/config/rebotarm_dm.yaml`（MIT kp/kd 120/8 与 18/2、POS_VEL PID 150/0.5/0.0125/0.004、vlim 5/3）—— **不是本项目真源**（真源见上表）。保留此锚点是因为 `AGENTS.md` §5 拿它对比真机工厂 PID（`0.00372/0.002/54/0`）。夹爪行一并删（本阶段不做）。

### 2.2 限位与力矩：真源不是 URDF
**限位**（原文档写的"来源 URDF、不在电机配置里"**是反的**，2026-10-04 起更正）：

- 真源是 `src/motor_driver/config/joint.yaml` 的 `position_min` / `position_max`（**关节侧**软限位）与 `arm_description/config/align.yaml` 的 `sign` / `zero_shift`。
- **模型坐标下的限位是算出来的，不是手写的**：两者 ⇒ URDF 的 `<limit>`（xacro 里是硬编码字面量）与构建期生成的 `joint_limits.hpp`（给应用层当护栏）。生成器 `arm_description/scripts/gen_joint_limits_header.py` **逐条对拍**这两者，改了真源没同步另一份就**构建失败**。源头 URDF 已移出工作区（`arm_description/reference/` 只留几何参考），旧的 URDF 参考限位已作废。实测见 `docs/TESTING.md` §十二；数值以 `joint.yaml` 为准。

**力矩**：`torque_max`（j1~j3 = 12.0、j4~j6 = 3.5，额定值）两种模式都生效 —— MIT 发帧前钳 `tau_ff`（`Joint._clamp_mit_torque`）、力位混控钳 `i_des`（`Joint._limit_i_des`）。

> ⚠️ **POS_VEL 下上位机限不了力矩** —— 位置环在固件里，主机没有任何力矩通道，只能**事后监控**反馈帧的 `torque`（`DmArm._monitor_step()`，见 §4.3）⇒ `torque_monitor_threshold` 是**必填、没有开关**。（`arm.py` / `arm_config.py` / `dm_bringup.py` 按 §2.2 引用本节）

---

## 三、关键设计决策

### D1 换算只有 `dir` 与 `offset`，没有 `gear`
`joint_to_motor(v) = direction * v + offset`（关节侧 → 电机侧）；`motor_to_joint(v) = direction * (v - offset)`（逆）。`direction ∈ {+1,−1}`、`offset ∈ [-π,π]`，**两者必须严格互逆**。**速度 / 力矩是矢量：只乘 `direction`，不加 `offset`**；本层**没有减速比折算**（电机报的就是输出轴 rad）。实现在一处：`joint.py` 的两个换算函数。

### D2 非阻塞发送 + 收发解耦（本层最核心的改动）
```
发：send_hz 循环 → bus.send_pos_vel_batch({motor: (pos, vlim)})   # 只写串口，不等回包
收：feedback_hz 循环 → bus.poll() → 解析缓冲 → 更新各 Motor 状态缓存
```
**串口超时必须调小**（`serial_timeout: 0.003`）—— 这是 D2 能成立的前提：`timeout=0.5` 会让每次读阻塞 0.5s。

**"发一帧等一帧"（单路调试 / 标定 / 寄存器 I/O）必须同时做到三件事**，缺一条就出错：

| 必须 | 不做的后果 |
|---|---|
| 发**之前** `flush` 掉残留帧 | 读到的是**上一帧**反馈，位置/力矩恒定滞后一个周期 ⇒ 安全判断建立在过期数据上 |
| 读要**自己阻塞**到 deadline（轮询 `in_waiting`），**不能**用 `read_all()` | `read_all()` 非阻塞，反馈还没回来就返回空 ⇒ 误判"收不到反馈" |
| 尾部**残片必须留**（`RxBuf`） | 残片一丢，后续字节全部错位，凑巧成 `0xAA…0x55` 的 16 字节会被误认成帧 |

**1:1 应答**（发一帧就必回一帧，不发就没有）实测成立 ⇒ 同一条循环里"写一帧 → 等自己那条反馈"可行，反馈率能追上发帧率。本层**两者并存**：默认"只发不读 + 抽干"（抽干非阻塞、不占循环时间），需要逐帧归因时用"发一帧等一帧"（**正确性 > 吞吐**）。带宽：标称 921600 **不是物理上限**（实测 154 KB/s 仍 1:1 不破），真正的天花板是**帧率**（约 3255 控制帧/s，`dm_bringup.py bandwidth` 量）—— 多关节高频率按这个数算（实测见 `docs/TESTING.md` §2.8 ④）。

### D3 模式：命令驱动 + 显式切换双轨
常规路径由 `JointMotorCmd` 的 `use_*` 标志位**每条命令决定**（无粘性状态 ⇒ 不会因为忘了切模式而发错帧）；专家路径要写寄存器 `0x0A` —— **本层不做**：`Joint.switch_mode()` **永远抛**（附五步指引）。

- ⚠️ **`mode` 是声明、不是读数**（本层读不到 `0x0A`）。声明错 ⇒ 帧被电机**静默丢掉**、不报错 ⇒ 每个 `set_*` 先检查声明模式；切完 `0x0A` 必须**同步改 yaml 与 `Joint.mode`**。
- ⚠️ 编码：**1 MIT / 2 位置速度 / 3 速度 / 4 力位混控** —— **力位混控是 4 不是 3**（按 3 配，电机进速度模式认 `0x200+ID`，你发的 `0x300+ID` 它根本不看 ⇒ 静默不动）。`0x0A` 是 **RAM**，掉电复位。
- **切换必须逐关节做**，且切完**立刻发一条保持当前位置的命令**，否则整组切换期间电机会处于无命令状态。

### D4 双层看门狗

| 层 | 实现 | 作用 |
|---|---|---|
| 主机侧 | 软看门狗（100ms 级） | 检测上层控制循环卡死 |
| **电机侧** | **寄存器 `0x09` `TIMEOUT`**（6 台已统一 10000 计数 = 500ms，存 flash） | **主机崩溃/断线时电机自己退出使能** |

> ⚠️ **`0x09` 的单位不是毫秒：一个计数周期 = 50µs**（1ms = 20 计数）。写 200 得到的是 **10ms** 看门狗，不是 200ms。换算规则**只在 `dm_registers.Reg.per_unit` 定义一处**，CLI 层统一说毫秒、同时打印原始计数。
> ⚠️ **`0x09 = 0` 是关闭保护**，务必非 0，且必须 `--save` 写 flash，否则断电重启后保护又回到关闭状态。取值理由：主机侧循环 200~300Hz（D2），500ms ≈ 100~150 个控制周期的余量 —— 容得下调度抖动，又远短于"人跑过去拔电源"的时间。

**三条会影响驱动设计的实测约束**（这是"运行期绝不能做寄存器 I/O"的完整理由）：

1. **任何发出去的帧都会重置计时器** —— 包括读状态用的 `0x7FF` 刷新帧。所以看门狗测试**不能轮询**（每次读 ERR 都清零计时器），只能"静默固定时长 → 单点观测"。反过来说，正常控制循环只要按时发帧就永远不会触发 —— 这才是它安全的原因。
2. **写一次寄存器要 ~150ms，期间电机侧完全静默且无法压缩** ⇒ 阈值必须显著大于 150ms，且**运行期绝不能做寄存器 I/O**。正确做法：上电时 `set --save` 写进 flash，运行期不再碰。
3. **ERR=13（通讯丢失）是锁存的** —— `enable(0xFC)`、连发 MIT 帧、写回 `0x09=0` 都清不掉，**只能电机断电重启**。

> 原 A/B/C 实测对照与 `--save` 跨断电复测表已删 —— `LESSONS.md` §一「尺子错，不是东西错」指向的正是那张表里的两行（"回读 ✓" 与 "断电重启后仍 ✓"）；原始记录见 git 历史与 `docs/TESTING.md` 的看门狗附录。

### D5 4340P 的型号与映射范围
**两套数，别混用**：

| 用途 | 用哪套数 | 4340P | 4310 |
|---|---|---|---|
| **编解码 CAN 帧**（MIT 的 p/v/t 线性映射） | **`0x15/0x16/0x17` 回读值**（= `joint.yaml` 的 `limit`） | 12.5 / 10 / **28** | 12.5 / 30 / 10 |
| **动力学模型 / 重力补偿 / 选型** | **手册物理值** | 额定 12、峰值 **40** N·m | 额定 3.5、峰值 12.5 |

两套数**本来就不同**（映射范围只需覆盖实际用量，不必等于物理极限）—— 当成同一个量就会得出"SDK 表与手册矛盾"的错误结论（`LESSONS.md` §2.6 坑 2 更正过）。另外：**不要信任硬编码**，上电后回读 `0x15~0x17` 与配置对拍、`0x14 Gr` 应为 10/40 兜底"型号填错"（`≤PMAX` 自洽校验已在 `arm_config.JointConfig.__post_init__`）；回读到 28 不等于只能出 28 N·m，但 **MIT 帧能命令的上限就是 28**。

### D5b kp 必须逐关节配（已落地，锚点）
**结论已落地，不再展开历史**：`arm_description/config/mit_gains.yaml` —— `kp_hold` j1~j3 = 25.0 / j4~j6 = 15.0、`kd_hold` = 0.8、`ki_hold` = 0.3、`friction_c` 逐关节。三条不能忘的理由：① 4340P 与 4310 的 kp 量级差 5 倍以上（静摩擦 0.62 vs 0.145 N·m）；② **kp 有硬上界 = `torque_max / 轨迹期望滞后`** —— PD 项自己超 `torque_max` 就**拒发** ⇒ `write()` 返回 ERROR ⇒ 失能 ⇒ **掉臂**（j4~j6 只能到 ~25）；③ 静态精度只能靠 `kp`，**宿主侧积分破不了摩擦死区**（大 ki 反而出极限环）。

### D6 状态量来源
`position` / `velocity` / `torque` 来自反馈帧（16/12/12 位定点）；`err` / `err_text` 来自 `D[0]` 高 4 位经 `ERR_DECODE`；`temp_mos` / `temp_rotor` 来自 **`D[6]` / `D[7]`（自己解析）**；`enabled` = `ERR == 1`（`ERR == 0` 是"失能"，不是"正常"）；频率都是 `feedback_hz`（100）。**`vbus` 本层读不到**（`0x3C` 是寄存器 I/O）⇒ `JointState` 里没有这个字段。

### D7 寄存器先读后写、可回滚
**写 RAM 与存 flash 必须分成两个动作**：调参阶段只写 RAM；`--save` 只在确认后才调（flash 有写寿命，且写前必须先失能）。工具形态（`RegisterTool` + CLI）：`dump`（先读、落盘基线）、`set`（写 RAM，写前自动 dump 基线）、`set --save --yes`（写 flash）、`restore`（从基线回写，默认只打印计划）、`verify`（读回对拍）。⚠️ **写 flash 的验证必须跨一次断电**才算数 —— `--commit` 的回读**不能**证明写进了 flash（回读走的是 RAM），而且一次"失败"的实验照样会改持久状态。只读的 `--dry-run` 不需要硬件、不需要 `pyserial` ⇒ 插电机前就能看"协议长什么样"。

### D8 重力补偿：只有 MIT 能给力矩
进入：逐关节 disable → 确保模式是 MIT → 立刻 `send_mit(当前 q, dq=0, kp_hold, kd_hold, tau_g)`；运行：`tau = tau_g(q) + 摩擦前馈 + 可选积分项`（数值与守卫见 `mit_gains.yaml` 与 AGENTS §5）；退出：逐关节切回 POS_VEL，切完立刻发保持位置命令。

| 场合 | 用哪个模式 | 为什么 |
|---|---|---|
| 常规运动（轨迹跟踪） | **POS_VEL 固件闭环** | 电机内部有积分，稳态误差能到 0；不需要上位机出死区力矩去顶摩擦 |
| 重力补偿 / 需要力矩前馈 | **MIT + `tau_g`** | MIT 是**唯一**能直接给力矩的模式 |

- **不做渐变**：最初引用的 `transition_duration: 0.5` 来自一个真机路径**根本不读**的 YAML 键；真机的安全手段是**逐关节切换**，不是渐变。**不要重新引入渐变。**
- 模型来自 `gravity_identified.yaml` / `arm_identified.urdf`（上游 CAD 惯量对这台实机**非均匀地错**，必须辨识）；`tau_g` 由 C++ 的 `GravityModel`（pinocchio）算。

### D9 限位钳位的位置：先钳位、后换算
`prepare_frame(pos)` 的顺序是固定的（`joint.py`）：`NaN 拦（math.isfinite）→ 软限位 clamp(pos)【关节侧】→ joint_to_motor 换算 → clamp_pmax【电机侧】`。

- ⚠️ **`clamp()` 吃的是关节侧**；拿电机侧的值去调它会把它钳到限位的**另一头**（真踩过：j4 电机侧目标 `1.675259` 被钳成 `1.095672` = 命令它**朝反方向**转 0.58 rad）。凡"先换算还是先钳位"，**先钳位**。
- ⚠️ **"保持当前位置"的帧必须绕过软限位**：保持帧的语义是"待在你现在的位置"，被软限位钳位就会凭空造出 PD 项（kp 大时激活直接拒发 ⇒ FATAL ⇒ 整条链起不来）。C++ 侧实现为 `Joint::prepare_frame_raw()` + `set_mit(..., bypass_soft_limits=true)`：保持帧 / 命令为 NaN 时一律绕过软限位（PMAX 仍钳），并打一条 WARN。POS_VEL 的保持帧早就绕过钳位。原文档提到的 `safe_margin`（0.02 rad）**从未实现**，不要当成既有能力。

---

## 四、类边界与不变量
本层四个入口：`MotorBus`（一条总线的非阻塞收发 + 寄存器 I/O）、`Joint`（单关节换算 / 限位 / 三模式）、`DmArm`（整臂批量 + 监控 + 两个阻塞循环）、`RegisterTool`（寄存器工具）。**逐个方法的状态看 `AGENTS.md` §3**；这里只留边界（原 mermaid 类图已删，它与实现严重不符）。

| 类 | 边界（不变量） |
|---|---|
| `dm_frames` / `MotorBus` | 纯函数不打屏、不发帧、不判安全，`_TX_TEMPLATE` 是不可推导的魔数（改错 = 适配器不认帧且无报错）；`send_frame` 是**唯一发送出口**，**不写寄存器**（只发寄存器帧，写什么由 `dm_registers` 决定），不设零位、不自动使能、不判安全，`close()` 不负责失能 |
| `Joint` | **不拥有控制循环**（`set_*` 只发一帧）；**不写寄存器**（`switch_mode()` 永远抛）；**不 poll**；`get_state()` 无反馈就抛（`MotorBus.get_state()` 返回 `None` 才是对的） |
| `DmArm` | **不拥有线程**：两个循环都阻塞、**不能同时跑**；不做寄存器 I/O、不碰运动学、不重复限位 |
| `RegisterTool` / `dm_bringup` | 工具只管寄存器 I/O（**不使能、不发控制帧、不判安全**，默认不碰 flash）；`dm_bringup` **刻意不 import 本包任何模块**（只依赖 vendored SDK + pyserial）—— 排障时能分清"我们的封装错"还是"链路本身错"，默认只读、永不写寄存器/零位、使能必须 `--yes`、退出（含 Ctrl-C）必失能 |

### 4.1 JointState —— 实际 10 个字段
`name` / `position`（rad，关节侧）/ `velocity`（rad/s）/ `torque`（N·m，电机由电流估算、带噪声）/ `err`（`D[0]` 高 4 位）/ `err_text`（由 `dm_frames.ERR_DECODE` 解出，直接透传）/ `temp_mos`（**int**，℃，`D[6]`）/ `temp_rotor`（**int**，℃，`D[7]`）/ `enabled`（`ERR == 1`；`ERR == 0` 是"失能"，不是"正常"）/ `timestamp`（`time.monotonic()`）。
**没有 `vbus`**：本层不做寄存器 I/O ⇒ 读不到 `0x3C`，恒 `None` 就是死字段。温度**照带但不做阈值判断** —— 阈值策略在 `DmArm`（见 §4.4），本层不是安全策略的拥有者。

### 4.2 JointConfig —— 实际 13 个字段
必填：`name` / `motor_type`（`"4340P"` / `"4310"`，映射到峰值表 `_PEAK_TORQUE`）/ `slave_id` / `direction` / `limit`（`(PMAX, VMAX, TMAX)`，**必须用 `0x15/0x16/0x17` 回读值**）/ `torque_monitor_threshold`（POS_VEL 事后监控阈值，必填，见 §2.2）/ `torque_max`（力矩上限 = 额定值，MIT 与力位混控都生效）。
带默认：`offset=0.0` / `torque_monitor_count=10`（连续 N 次越限才算故障，时基 = `feedback_hz`）/ `position_min=None` / `position_max=None`（关节侧软限位，`None` = 不软钳）/ `mode=MODE_MIT`（**声明，不是读数**）。
`__post_init__` 是**纯参数自洽**校验（不通信、不碰运行期状态）：`direction ∈ {±1}`、`mode ∈ MODE_NAMES`、`limit` 三个正数、软限位不乱序、**软限位换算到电机侧必须落在 ±PMAX 内**（否则那条限位是虚的）、`torque_monitor_threshold < 该型号峰值`（否则**永远不会触发**）、声明 mode 4 时型号必须在 `NM_PER_I_DES` 里。`ArmConfig` 另收全局字段 `channel / baud / serial_timeout / send_hz / feedback_hz / temp_warn / temp_fault`，并校验 `slave_id` 不重复、`0 < temp_warn < temp_fault`。**没有** `master_id` / `velocity_max` / `vlim` / `pos_*` / `vel_*` / `mit_kp` / `mit_kd` / `motor_timeout_ms` —— 那些是历史设计。

### 4.3 力矩阈值必须按型号分档
**不能全套关节用同一组数** —— 4310 的峰值只有 12.5 N·m，若阈值定 15 则**永远不触发**（等于没有保护）：

| 关节 | 型号 | 电机额定 | 电机峰值 | `torque_max`（发帧前钳位） | `torque_monitor_threshold`（事后监控） |
|---|---|---|---|---|---|
| j1~j3 | 4340P | 12 | 40 | 12.0 | 15.0 |
| j4~j6 | 4310 | 3.5 | 12.5 | 3.5 | 5.0 |

- 一致性检查：`torque_monitor_threshold` 必须 < 该型号峰值，否则报错（"这个阈值永远不会触发"）。**时基绑定**：`torque_monitor_count` 按 `feedback_hz`（100 Hz）计数，10 次 = 100 ms 连续越限；⚠️ **不要放在发帧循环里数** —— 那样 10 次只有 20 ms，加/减速峰值就会误触发。越限判定用反馈帧的 `torque`（电流估算、带噪声，所以要 count 防抖），取绝对值（正反向都算）。
- **三模式分工**（`torque_max` 不是 POS_VEL 的替代品）：MIT 发之前钳 `tau_ff`、力位混控钳 `i_des`（`torque_max / NM_PER_I_DES[型号]`）、**POS_VEL 只能事后监控**。
- ⚠️ 力位混控的 `i_des` 是**电流上限、不是力矩前馈**（给 0 = 一点力都不给；`enable()` 的保持帧用 1.0）；`NM_PER_I_DES` 是**实测值**（4340P 40 / 4310 22），**别按峰值比例外推**。

### 4.4 温度阈值

| 项 | 约定 |
|---|---|
| 阈值与校验 | `temp_warn = 80` / `temp_fault = 100` ℃（`joint.yaml` 全局段；手册建议线圈不超 100℃）；`0 < temp_warn < temp_fault`，违反即加载失败 |
| 传感器与判据 | **MOS（`D[6]`）与线圈（`D[7]`）各自独立判**，共用同一对阈值，都用 **`>=`** 比较；**同一传感器同时满足时只报 fault**（`if fault … elif warn`） |
| 覆盖范围 | **所有模式都查温度**（不像力矩只查 POS_VEL）—— 与出力模式无关，堵转时 MOS/线圈都可能烧 |
| warn / fault | warn 打 stderr，**限流键是 `(关节, 传感器)`** ⇒ 6 关节 × 2 传感器最多 12 条 / 间隔（不是"全局 1 条/5s"）；fault 收集齐后抛 `RuntimeError`，收循环的异常路径自动 `emergency_disable()` |
| 采样率与验证 | 挂在**收循环**上 ⇒ 采样率 = `feedback_hz`（100 Hz），没有反馈的关节直接跳过（不编造温度）；真机注入验证过（12 条 warn + 一次抛 12 条），⚠️ 电机物理上真到 80/100℃ **仍未验证**（与 34℃ 走同一条 `>=`，差别只在数字） |

---

## 七、标定工作流（§7）
硬件是**仿件**，装配零位/方向与参考项目不同 ⇒ 不能省，且**每一步都要人工确认后才继续**。

```text
0  只连一个电机（joint4/4310 力矩最小，空载固定好）      → 总线通
1  读 0x14 Gr / 0x15~0x17 / 0x19~0x1C 并落盘（只读）     → 基线 JSON
2  enable → 低速点动 ±0.2 rad（vlim=0.3，手放急停上）    → direction 判定
3  读回 0x50 p_m（不是 0x51 xout）确认"指令 +0.2 → 反馈也 +0.2"；反向则 direction=-1
4  disable 后把关节推到机械零位（或对准标记），记 p_m     → offset 定值
5  写入 offset，重跑步 2-3 复核；逐关节重复 3-5（一次只动一个关节）→ 全部 offset
6  读 0x09，写 500ms（= 10000 计数，见 D4），--save 写 flash，回读确认 → 看门狗生效
7  6 关节低速联动到目标位姿（≤0.5 rad/s），与姿态来源对拍  → 整臂可用
```
- ⚠️ **direction 的手推定符号必须再用通电小动作复核一次** —— 真踩过：j6 手推判反（当时报"顺时针"），通电才发现实际反向，`direction` 从 −1 改成 **+1**。手推是必要但不充分的证据。
- ⚠️ **offset 标一次长期有效**：多圈绝对位置跨断电保持（实测 6 台断电 8 s 后 Δ ≤ 0.00015 rad），不必每次上电回零；标定前不要用 `set_zero_position`(0xFE)（改电机内部零位，不可逆）；标定脚本必须支持 `--dry-run`。
- 实测依据与逐关节数值见 `docs/TESTING.md` §十二；最终数值以 `joint.yaml` 为准。

---

## 八、上电 / 使能 / 失能的不变量（§8）
```
connect:  开串口（timeout=3ms）→ 建关节（把 limit 注册到 bus，冲突则拒）—— **不写任何寄存器**
enable:   逐关节 enable；位置类模式（2/4）下**没有缓存位置就拒绝使能**，并使能后**立刻补"保持帧"**
          （切进位置类模式时电机内部指令被清零 ⇒ 没有保持帧会朝零位冲）
          MIT 下 enable 发的是**零增益零前馈**（出力恒 0）⇒ 激活后必须立刻补一帧真实保持帧
运行:     按时发帧（喂看门狗）；**不做任何寄存器 I/O**（一次约 150ms 静默）；已使能关节的发帧空档不许超过 0x09
disable:  先 disable，再关电源 / 拔线（失能 ≠ 停住：带重力负载的关节会掉下来）
shutdown: 停循环 → **先失能** → 再关串口（`close()` 自己不管失能）
```
- 收臂 / 回零 / 轨迹**不是驱动层的事**（不要塞进硬件插件）：走 ros2_control 的正常轨迹通路，见 `arm_application/safe_park_node` 与 `docs/MOTION_ARCH.md`。
- 真机上电顺序（每步先只读）：`poll()` 看状态 → 拿到位置 → `enable()` → 小增益 `set_mit(kp≈1~5, q=当前位置)` → 确认方向 → 加大 → `disable()` → 关电源。详见 `AGENTS.md` §5。

---

## 九、实现边界：本层不做什么（§9）
- 本层**不做**运动学 / 轨迹生成 / 控制循环：上层（MoveIt 算 IK/轨迹 → ros2_control 执行）负责，控制循环由 `ros2_control` 的 update 驱动，**不自搭**。
- POS_VEL 下上位机**限不了力矩**，只能事后监控（`DmArm` 的力矩/温度监控，见 §2.2 / §4.3）—— 所以监控循环必须与发帧循环成对运行。需要"回零 / 收臂"这类业务流程时走应用层节点（经 JTC），驱动层只提供"发一帧"的能力。架构分层（运动接口层 `arm_motion` / 应用层 / 规划层 / 视觉契约）见 `docs/MOTION_ARCH.md`；精确状态与陷阱见 `AGENTS.md` §3 / §6；实测见 `docs/TESTING.md`。

---

## 十一、已确定的决策

| 项 | 决定 |
|---|---|
| 力矩保护 | **软限 + 监控双轨**：`torque_max`（MIT / 力位，发帧前生效）+ `torque_monitor_*`（POS_VEL 事后监控）。阈值**按型号分档**，见 §4.3 |
| bring-up 脚本 | **要**，且**刻意不依赖封装层**（直连 `DM_CAN.py`）：先验证协议与硬件，结论反过来校正封装层设计 |
| 标定脚本 | **不单独成包**；流程落成 `joint.yaml` 的静态参数 + `dm_registers` 的读写工具（无独立 `dm-calibrate` 命令） |
| 夹爪 | 本阶段不做（换算、`GripperConfig`、关节命名翻译一并删） |

### 11.1 entry_points（本包对外命令）
真源是 `src/motor_driver/setup.py`，只有两条：`dm-bringup = motor_driver.dm_bringup:main`（单电机排障 read/monitor/jog/bandwidth）与 `dm-dump-registers = motor_driver.dm_registers:main`（寄存器 list/dump/verify/set/restore）。早先规划里的 `dm-calibrate` **不存在**。

### 11.2 bring-up 脚本的范围（`dm_bringup.py`）

| 子命令 | 作用 | 风险 |
|---|---|---|
| `read`（默认） | 读 `Gr` / `0x15~0x17`（定映射表）/ `0x19~0x1C`（PID 现状）/ `0x09` / `0x07` / `0x0A` / `0x3C~0x3E` | 只读 |
| `monitor` | 按 feedback_hz 刷新状态（含 `D[6]/D[7]` 温度），统计丢帧率 | 只读 |
| `jog` | 单电机低速点动，打印"命令 → 30B 发送帧 → 16B 反馈帧" | 需 `--yes` |
| `bandwidth` | 按 `--hz` 硬发 N 秒，量实际速率 / 字节数 / 单帧写延迟 | 默认只发不动；`--enable` 需 `--yes` |

五条边界（不变量）：① **全程不写任何寄存器**（含 `CTRL_MODE`），**永不调用 `set_zero_position`(0xFE)**，`read` 发现模式与声明不符只提示、不自动改；② `--dry-run` 不需要 `pyserial`、不需要硬件，只打印帧结构 ⇒ 插电机前就能看"协议长什么样"；③ `jog` 刻意走**厂商 API**（先证明厂商这条路本身是通的），自己那套非阻塞收发是 `MotorBus` 的活；④ 退出路径（含 Ctrl-C）统一 `finally`：回 p0 → `disable`（失败也会打印"请直接断电"）；⑤ 使能时 `--hz` **硬限制 ≥5Hz**（否则会撞 500ms 看门狗）。
