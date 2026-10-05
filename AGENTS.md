# AGENTS.md —— 给下一个 AI 的接手说明

> 人看的入口是 [readme.md](readme.md)；本文是**精确状态 + 不变量 + 禁区**。
> 维护约定：**改了代码就回来改这份文件**（它比 `docs/DESIGN.md` 新，因为 DESIGN 含大量历史段）。

## 0. 硬性约束（先读这段，违反等于白干）

1. **未经用户明确批准，不许写任何代码**；不许"顺手"改无关文件、加函数、加参数、加常量、加文件。
2. **每一处最小改动都先说明、等批准、再动手**（用户会逐条回复）。改动后要**验证并自审**（checklist 见 §9）。
3. **禁止生成本仓库的测试文件**（策略）。验证一律用**进程内 heredoc 假 bus**，不落盘。
4. **不许做多余的事**：不装依赖、不跑 `pixi install`、不启动服务、不删文件（删/移动前须给出解析后的**绝对路径**并二次确认）。
5. 改文件前先用 read 工具读它（文件策略要求）；改完把关键证据（命令 + 输出）贴给用户。
6. 注释/文档风格参照 `~/elc25e`（用户的风格）：简洁中文、不堆框线 emoji。**不要**照抄 `dm_bus.py` 那种长篇 docstring 风格。
7. 协作流程见 [ARMWORK.md](ARMWORK.md)：访谈 → 计划 → 实现 → 精修 → 归档，**每阶段要用户确认**。

## 1. 这是什么（一句话）

桌面级 6 轴机械臂上位机工程：USB-CAN → 达妙电机。关节 1–3 = **4340P**，4–6 = **4310**，夹爪（4310）本阶段不做。
当前：**驱动层**（协议 / 总线 / 单关节 / 整臂 / 寄存器工具 + 6 轴标定）已就绪；ROS2 侧已有
**模型描述 + 模型对齐 + 只读镜像 + mock 控制链路 + C++ 硬件接口插件**。
（`motor_driver` 这个 Python 包**仍然不 import rclpy** —— ROS 的东西都在 `arm_*` 与 `motor_driver_hardware` 里。）

**路线（不变量）**：上层 **MoveIt 算 IK / 轨迹** → **ros2_control 下发执行**；本包只做电机驱动、标定与
`ros2_control` 硬件接口，**不实现**运动学 / 轨迹生成 / 控制循环。

## 2. 代码地图（工作区**真实存在**的全部代码）

| 文件 | 行数 | 职责 | 边界 / 不变量 |
|---|---|---|---|
| [dm_frames.py](src/motor_driver/motor_driver/dm_frames.py) | 435 | CAN 帧编解码**纯函数**：30B 发送帧模板、8B 数据段、16B 收帧切分、反馈解码、**寄存器读/写/存参数帧 + 回包分类解码** | 不 import 本包、不打屏、不发帧、不判安全、**`_TX_TEMPLATE` 是不可推导的魔数**（改错=适配器不认帧且无报错） |
| [dm_bus.py](src/motor_driver/motor_driver/dm_bus.py) | 531 | 一条总线的**非阻塞**收发：唯一发送出口 `send_frame()`、`poll()` 抽干、`MotorState` 缓存、`registered_ids` 准入、**寄存器 I/O（`read/write_register`/`save_params`）** | **不写寄存器** —— 它只发寄存器帧，写什么由 `dm_registers.py` 决定；不设零位、不自动使能、不判安全；`close()` 不负责失能 |
| [dm_modes.py](src/motor_driver/motor_driver/dm_modes.py) | 27 | 模式编码 `MODE_MIT=1/POS_VEL=2/FORCE_POS=4` + `MODE_NAMES` | 零 import；**3 = 速度模式，不是力位混控** |
| [joint.py](src/motor_driver/motor_driver/joint.py) | 298 | 单关节：换算 / 软限位+PMax / NaN 拦 / 三模式发帧 / 使能失能 / 状态 / 故障 / **力矩上限：MIT 钳 `tau_ff`（`_clamp_mit_torque`）+ 力位混控钳 `i_des`（`_limit_i_des`）** | **不拥有控制循环**（`set_*` 只发一帧）；**不写寄存器**（`switch_mode()` 永远抛）；**不 poll**。**三种模式都已真机跑通**（MIT/POS_VEL 2026-10-01 上午；**力位混控 2026-10-01 深夜**）|
| [arm_config.py](src/motor_driver/motor_driver/arm_config.py) | 200 | `config/joint.yaml` → `JointConfig`（+ `torque_monitor_threshold`/`torque_monitor_count`/**`torque_max`**）/ `ArmConfig`（+ `temp_warn`/`temp_fault`）+ `load_joint_configs` / `load_arm_config`；**纯参数自洽**校验在 `__post_init__`（力矩阈值与 `torque_max` 都必须 < 型号峰值 `_PEAK_TORQUE`；**声明 mode 4 还要求型号在 `NM_PER_I_DES` 里**） | 不通信、不碰运行期状态、**不 import 驱动层**（只 import `dm_modes` + yaml） |
| [arm.py](src/motor_driver/motor_driver/arm.py) | 417 | `DmArm`：连接/关闭、使能失能、批量 POS_VEL / MIT、状态（`get_state`/`refresh_all_states`/`sync_states`）、健康检查与急停、**力矩+温度监控**、发/收两个阻塞循环（节拍与退出策略集中在 `_paced_loop`） | **不拥有线程**：两个循环都阻塞、**不能同时跑**（调用方开线程）；不做寄存器 I/O、不碰运动学、不重复限位。**循环与监控已在真机 6 台跑通**（100Hz 双循环 0 超时 / 500Hz 单跑 499.7Hz / 监控零误报） |
| [dm_registers.py](src/motor_driver/motor_driver/dm_registers.py) | 492 | 寄存器工具：49 条寄存器表（`Reg` 带 **`per_unit`**：换算只在这一处）+ `RegisterTool` + CLI `list/dump/verify/set/restore` | 只管寄存器 I/O：**不使能、不发控制帧、不判安全**；默认不碰 flash。**读 + 写 RAM + `--save` 写 flash 都已真机验证**（2026-10-01：写 `0x09` 后**断电重上电仍在** ⇒ 手册的 `0xAA`/`0x01` 字节是对的） |
| [dm_bringup.py](src/motor_driver/motor_driver/dm_bringup.py) | 1169 | **单电机排障脚本**（2026-10-01 从 git 历史恢复，见 §8）：`read` / `monitor` / `jog [--mit]` / `bandwidth` 四条子命令，会把每条 CAN 帧摊开讲 | **刻意不 import 本包任何模块** —— 只依赖 vendored SDK + pyserial（价值就在这：排障时能分清"是我们的封装错"还是"链路本身错"）。默认**只读**、永不写寄存器/零位、使能必须 `--yes`、退出（含 Ctrl-C）必失能；**使能时 `--hz` 硬限制 ≥5Hz**（否则会撞 500ms 看门狗） |
| [config/joint.yaml](src/motor_driver/config/joint.yaml) | — | 6 关节静态参数（**已按本项目标定**：offset / direction / 软限位） | `limit` **必须**是 `0x15/0x16/0x17` 回读值 |
| [setup.py](src/motor_driver/setup.py) | 36 | 装 `share/motor_driver/config/joint.yaml`；**`data_files` 的源路径只能写相对路径**（colcon 的 ament_python task 会 assert 拒绝绝对路径） | `entry_points` 的 `dm-dump-registers` / `dm-bringup` 指向 `motor_driver.*` |
| [arm_msgs](src/arm_msgs) | — | 本项目接口包（ament_cmake）：msg `JointMotorCmd` / `JointMotorState` / `ArmStatus` + action `MoveToPose` | 只定义接口、无代码；夹爪本阶段不做 |
| [arm_description](src/arm_description) | — | URDF/xacro 描述（几何 verbatim 取自 reBotArm，CERN-OHL-W-2.0）+ **`config/align.yaml`（模型对齐真源）** + **`config/mit_gains.yaml`（MIT 保持增益）** + **`config/gravity_identified.yaml`（辨识出的质量/质心）** + `scripts/apply_identified_inertia.py` + 显示 launch。构建时生成两份动力学 URDF：`arm.urdf`（上游名义）与 **`arm_identified.urdf`（辨识版，前馈默认用）** | 纯数据包；几何**不是我们写的**，改 mesh/URDF 要保留上游许可与来源声明 |
| [arm_bringup](src/arm_bringup) | — | ROS2 胶水层：`real_joint_states`（**只读**镜像）+ **`real_control.launch.py`**（真机 ros2_control，参数 `enable_on_activate` / `spawn_arm_controller` / `vlim` / **`gravity_ff`** / **`gravity_ff_scale`** / `mit_controllers`）+ **两份控制器配置** `config/ros2_controllers.yaml`（只 position）与 `ros2_controllers_mit.yaml`（position+velocity+constraints，见陷阱 #40） | **绝不 `enable()`**（镜像节点）；串口连续失败达 `max_fail_streak` 就 FATAL 退出；参数默认值取自 `arm_description/config/align.yaml` |
| [arm_moveit_config](src/arm_moveit_config) | — | MoveIt 配置（纯数据）：`arm.srdf`（一个规划组 arm = base_link→gripper_tcp 链；**本阶段不做夹爪**；碰撞对只关相邻链节）/ `kinematics.yaml`（KDL）/ `ompl_planning.yaml` / **`joint_limits.yaml`（必须显式给 `max_velocity`，否则 TOTP 失败，见 `docs/TESTING.md` §十四）** / `moveit_controllers.yaml`（simple controller manager → `arm_controller`）/ `move_group.launch.py` / `moveit.rviz` | 不写规划器、不碰运动学实现；模型与限位都来自 `arm_description`；**mock 与真机上 plan / plan+execute 都已跑通**（2026-10-05）；**mock 与真机只能开一套**（陷阱 #35、#29） |
| [motor_driver_hardware](src/motor_driver_hardware) | — | **C++ 侧**（`dm_hardware` 库 + `dm_system_interface` 插件）：`dm_frames`（协议）+ `dm_serial`（termios 非阻塞串口 + `SerialIo` 接口）+ `dm_bus`（收发/缓存/`sync_states`/寄存器 I/O）+ `dm_joint`（换算/软限位/只走 POS_VEL 的关节）+ **`DmSystemInterface`（ros2_control 插件：参数化、只读模式、锁定保持目标、ERR 检查）** | `dm_hardware` **不依赖 rclcpp**（只有插件那层依赖）；`dm_joint` **只做 mode 2**；默认 `enable_on_activate=false`（**只读**，不发控制帧）；与 Python 那三份实现靠 `test/` 的**逐字节/逐数值对拍**保持一致 |

## 3. 已实现 / 未实现（精确到方法）

**`MotorBus`（全实现）**：`__init__(port, *, baud=921600, timeout=0.003)` · `connect` · `open/close`（上下文管理器可用）·
`add_motor(id, limit, motor_type=None)` · `set_limit` · `motors()` · `registered_ids` ·
`send_frame` · `send_pos_vel` · `send_pos_vel_batch` · `send_mit` · `send_force_pos` · `send_enable` · `send_disable` · `send_refresh` ·
`poll` · `wait_feedback` · `send_and_wait` · `poll_and_wait` · `get_state` · `states` · `flush` · `stats` ·
`read_register(motor_id, rid, timeout=0.05)` · `write_register(motor_id, rid, raw4, timeout=0.05)` · `save_params(motor_id, timeout=0.5)`（**这三个不查注册** —— 注册检查是给控制帧的，寄存器 I/O 不解反馈帧，且 `limit` 正是它要读的东西；`raw4` 是 4 字节，类型由 rid 决定，见陷阱 #15）

**`Joint`（全实现）**：
```python
Joint(bus, motor_id, name, direction, limit, offset=0.0,
      position_min=None, position_max=None, mode=MODE_MIT,
      torque_max=None, nm_per_unit=None)
enable() / disable()
prepare_frame(pos)                      # NaN 拦 → 软限位 → 换算 → PMax
set_mit(kp, kd, q, dq=0.0, tau=0.0)     # 仅 mode==1；torque_max 不是 None 时先钳 tau_ff
_clamp_mit_torque(...)                  # 私有：PD 项自己超 → 拒发；否则把 tau_ff 钳到 ±torque_max
_limit_i_des(current)                   # 私有：力位混控钳 i_des 到 torque_max/nm_per_unit（实测换算）
set_pos_vel(pos, vlim)                  # 仅 mode==2；vlim 是幅值、负值抛
set_force_pos(pos, vel, current)        # 仅 mode==4；current ∈ [0,1] 电流上限，会被 _limit_i_des 钳
get_state()   -> JointState             # 无反馈 ⇒ 抛 RuntimeError
assert_healthy() -> None                # 只查 ERR（温度归上层）；故障 ⇒ 先失能再抛
switch_mode(mode)                       # 永远抛 NotImplementedError（附五步指引）
joint_to_motor(v) / motor_to_joint(v) / clamp(pos) / clamp_pmax(motor_pos)
```
`JointState` 字段：`name, position, velocity, torque, err, err_text, temp_mos, temp_rotor, enabled, timestamp`

**`arm_config`**：`_PEAK_TORQUE`（峰值表）· `NM_PER_I_DES`（**实测** i_des↔N·m：4340P 40 / 4310 22）· `JointConfig(..., torque_monitor_threshold, torque_max, torque_monitor_count=10)` · `load_joint_configs(path)` · `ArmConfig(channel, joints, baud, serial_timeout, send_hz, feedback_hz, temp_warn=80, temp_fault=100)` · `load_arm_config(path)`

**`DmArm`（13 个公开方法 + 2 个私有）**：`__init__(config)` · `connect()`（开串口 + 建关节，**不写寄存器**）· `shutdown()`（**先失能再关串口**）·
`enable_all()`（任一个失败 → **回滚已使能的**再抛）· `disable_all()`（**全部尝试**，最后抛汇总）·
`set_joint_positions(targets, vlim)`（POS_VEL，vlim 支持标量或 dict）· `set_joint_mit_all(targets, kp, kd, dq=0, tau=0)` ·
`get_state()`（**只返回有反馈的关节**）· `refresh_all_states()`（6 条 0x7FF + 一次 poll，**不等回包**）·
`sync_states(timeout=2.0, quiet=0.05)`（**排空陈旧回包**再取状态，对付陷阱 #20）·
`check_health()`（逐个 `Joint.assert_healthy()`，**收集齐再一次抛**；只查 ERR）·
`emergency_disable()`（**永不抛**：逐关节尽力失能，失败只打 stderr）·
`run_control_loop(hz, fn)`（**阻塞**发循环：每圈 `check_health()` → `fn(self)`）·
`run_feedback_loop(hz, fn)`（**阻塞**收循环：每圈 `bus.poll()` → **`_monitor_step()`** → `fn(self)`）；
私有：`_monitor_step()`（力矩+温度监控，见下）、`_require_connected()`；
两个循环都返回 `{"iters","overruns","elapsed","hz_actual","interrupted"}`

**`dm_registers`（工具）**：`RegisterTool(bus, motor_id, motor_type=None)` · `read` / `write` / `check_writable` / `dump` / `verify`；
CLI `list` / `dump` / `verify` / `set` / `restore`。`set` 默认只写 RAM，`--save --yes` 才写 flash 且**写前自动 dump 基线**；
`restore` 默认只打印计划。发送前 `is_reg_response` 把寄存器回包与反馈帧分开。

**`motor_driver_hardware`（C++ 侧，`dm_hardware` 库；**不依赖 rclcpp**）**：
`dm_frames`：30B 发送帧（MIT / 位置速度 / 力位混控 / 使能失能 / 刷新 / 寄存器读·写·存参）·
16B 收帧切分（`extract_rx` / `RxBuf`，残片保留）· `decode_feedback` · `decode_reg_response` · `is_reg_response`（**带陷阱 #26 的修正判据**）；
`dm_serial`：`SerialIo` 接口（测试注入假串口用）+ `SerialPort`（termios 8N1、**非阻塞读**、写满、`stats`）；
`dm_bus`：`open/close` · `add_motor`/`set_limit`/`limit`/`registered_ids` · `send_frame`（唯一出口）+ `send_pos_vel`/`send_enable`/`send_disable`/`send_refresh` ·
`poll` · `wait_feedback` · `flush` · `get_state` · `unknown_ids` · `sync_states` ·
`read_register`/`write_register`/`save_params`；
`dm_joint`：`JointConfig`（name/motor_id/direction/offset/`Limit`/软限位/mode）→ `Joint`：
`joint_to_motor`/`motor_to_joint`/`clamp`/`clamp_pmax`/`prepare_frame`（NaN→软限位→换算→PMAX）·
`set_pos_vel`（**仅 mode 2**）· `enable`（**无缓存位置拒使能 + 立刻补保持帧**）· `disable` ·
`get_state`（无反馈抛）· `assert_healthy`（只查 ERR，故障先失能再抛）；
`dm_joint` 的 MIT 侧：**`set_mit`**（仅 mode 1；`torque_max` 不是空时先按**预测总力矩**钳 `tau_ff`，
PD 项自己超则拒发）· `enable` 在 mode 1 下发的是**零增益零前馈**（出力恒 0，见陷阱 #41）；
`dm_gravity`：**`GravityModel`**（pinocchio 的 `computeGeneralizedGravity`）—— 构造时按**名字**映射到
pinocchio 关节、预分配 `Data` 与缓冲（`tau_ours()` **零堆分配**，在 100 Hz 的 `write()` 里调）；
`DmSystemInterface`（插件，`SystemInterface`）：参数**全部来自 <param>**（device/baud/enable_on_activate/vlim +
**gravity_ff** / **gravity_ff_scale** / **urdf_path** + 每关节 motor_id/motor_type/direction/offset/p_max/v_max/t_max/
position_min/position_max/**sign/zero_shift**/**torque_max**/**kp_hold**/**kd_hold**/可选 PID）·
生命周期 `on_init`（解析校验）→ `on_configure`（建串口/总线/关节；`gravity_ff` 时建 `GravityModel`，失败即 FATAL）→
`on_activate`（开串口 + `sync_states` + 写 `0x0A=1/2` + 可选写 PID + 可选使能 + **锁定保持目标** +
`gravity_ff` 时**立刻补发真实 MIT 保持帧**）→ `on_deactivate`（**全部失能**）；
`read()`（poll + 填 position/velocity/**effort** + **ERR 非 0/1 报 ERROR**）·
`write()`（只读模式**一个字节都不发**；`gravity_ff=false` ⇒ 走 `set_pos_vel`，命令是 NaN 就发陷阱 #38 的锁定保持帧；
`gravity_ff=true` ⇒ 走 `write_gravity_ff()`：实测姿态算重力项 → 每关节 `set_mit(kp_hold, kd_hold, 命令, sign·速度命令, tau)`，
带**残差守卫**，见陷阱 #42）。

**未实现（别以为有）**：`arm_msgs` 之上的节点 · 夹爪 · 速度模式(3) · **电压监控**（本层读不到 `0x3C`）·
**C++ 侧的力位混控路径**（POS_VEL 与 MIT 两条都做了，够重力前馈用）·
**摩擦前馈 / 宿主侧积分 / 逐关节 kp 标定**（见 §5 "重力补偿"那段末尾的"还差什么"）·
**`motor_driver`（Python 包）里仍然没有任何测试文件**（策略；C++ 包的 `test/` 是唯一例外，见 §7）。
真机上的 **ros2_control 三步验收**、**MoveIt 规划 + 执行**、**MIT + 重力前馈整链**都**已通过**（2026-10-05）。

## 4. 单一真源表（改之前想清楚该改哪个）

| 事实 | 唯一真源 |
|---|---|
| 协议字节布局 / 帧构造 / 反馈解码 | `dm_frames.py` |
| 模式编码与名字 | `dm_modes.py`（`MODE_NAMES`） |
| 故障码含义 / `ERR_OK=(0,1)` | `dm_frames.ERR_DECODE` / `ERR_OK` |
| 关节限位、方向、零位、模式声明 | `src/motor_driver/config/joint.yaml`（**本项目自己标定**；源头 URDF 已移出工作区） |
| `limit` 档位数值 | **只能回读** `0x15/0x16/0x17`；实测记录在 `docs/TESTING.md` §2.5/§2.6 |
| **力位混控 `i_des` ↔ 扭矩的换算** | `arm_config.NM_PER_I_DES`（实测值，**别按峰值比例外推**：4340P 40 / 4310 22，不是 3.2 倍关系） |
| **模型对齐 `sign` / `zero_shift`**（`q_urdf = sign·q_ours + zero_shift`） | `src/arm_description/config/align.yaml`（2026-10-04 M1b 实测；URDF 限位与 M5 硬件接口都从它推） |
| **MIT 保持增益 `kp_hold` / `kd_hold` / `ki_hold`** | `src/arm_description/config/mit_gains.yaml`（2026-10-05 真机分级定到 **j1~j3 25.0 / j4~j6 15.0**、kd 0.8、ki 0.3；不塞进 `joint.yaml` 是因为 `arm_config.py` 严格解析、多一个键就 TypeError） |
| **这台实机的质量 / 质心（重力模型）** | `src/arm_description/config/gravity_identified.yaml`（2026-10-05 辨识，RMS 1.323→0.418；**上游 CAD 的惯量不可信**，见陷阱 #43） |
| 设计意图 | `docs/DESIGN.md`（**部分历史段已过期**） |

## 5. 关键事实表

**CAN ID 偏移**（`JOINT` 侧）：MIT = `id` · 位置速度 = `0x100+id` · 速度 = `0x200+id`（未实现）· 力位混控 = `0x300+id` · 使能/失能 = `id`（数据 `FF*7+FC/FD`）· 刷新 = `0x7FF`（广播，数据带目标 id）

**反馈帧**：适配器→主机 16 字节（`[0]=0xAA`、`[1]=0x11`、`[15]=0x55`），数据段 8 字节 = `D[0]=ID|ERR<<4`、`D[1:3]=POS16`、`D[3:5]=VEL12`、`D[5:7]=T12`、`D[6]=T_MOS`、`D[7]=T_Rotor`

**模式（`0x0A`）**：1 MIT / 2 位置速度 / 3 速度 / 4 力位混控。`0x0A` 是 **RAM**，掉电复位。本层**读不到**它 ⇒ `mode` 永远是**声明**

**ERR**：`0` 失能 / `1` 使能 / `8` 超压 / `9` 欠压 / `A` 过流 / `B` MOS 过温 / `C` 线圈过温 / **`D` 通讯丢失（锁存，只能断电清）** / `E` 过载

**`limit = (PMAX, VMAX, TMAX)`**：4340P = **12.5 / 10 / 28**；4310 = **12.5 / 30 / 10**（实测回读，与 SDK 表一致）

**关节表**：j1 4340P id1 · j2 4340P id2 · j3 4340P id3 · j4 4310 id4 · j5 4310 id5 · j6 4310 id6
（`direction` / `offset` / 软限位**均已按本项目 2026-10-03 标定**：j1~j5 `direction=−1`、**j6 `direction=+1`**；
数值**以 `src/motor_driver/config/joint.yaml` 为准**，实测依据见 `docs/TESTING.md` §十二。旧的 URDF 参考限位已作废。）

**模型坐标下的限位**（2026-10-04 M1b 换算，已写进 URDF；`sign/zero_shift` 见 `arm_description/config/align.yaml`）：
j1 `[+1.392202, +1.626143]` · j2 `[-2.379924, +0.058351]` · j3 `[-1.891796, +0.050752]` ·
j4 `[-0.157540, +0.938132]` · j5 `[-0.970641, +0.913381]` · j6 `[-1.359002, +4.924184]`。
（2026-10-04 当天**放宽了 j2/j3 的下界**：`joint.yaml` 的 `position_min` 0.0 → **−0.25 / −0.10**，
好让真机"自然停放姿态"（`q_ours = −0.1916 / −0.0477`）落在限位内 —— 否则第一个"保持当前位置"就会被钳成 11° / 2.7° 的运动。
⚠️ 代价：`q_urdf = 0` 现在离 j2 上界只差 **0.058 rad（3.3°）**、离 j3 上界差 0.051 rad。）

**2026-10-01 真机只读 + 点动**（单台 id=6 / 4310，裸机、轴悬空）：`limit=12.5/30/10` ✓、`CTRL_MODE=1(MIT)`、
`MST_ID=0`、看门狗 `0x09=0`、`VBus=24.15 V`；**PID 是工厂值**（`0.00372/0.002/54/0`，与 `DESIGN.md §2.1` 的计划值不同）⇒ 要跑 POS_VEL 得先写。
目标 +0.08 rad：**MIT**（kp=10/kd=1）走到 +0.0691 ⇒ **残差 0.069 rad**（≈摩擦/kp，MIT 固有）；**POS_VEL**（vlim=0.5）走到 +0.0816 ⇒ **残差 0.0016 rad**。
两模式全程 ERR=1、30 ℃、总线 1:1。完整数据见 `docs/TESTING.md`。

**2026-10-01 真机力位混控（mode 4）实测**（单台 id=6 / 4310，裸机、无负载）：`enable()` 保持帧漂移 **0.00000 rad**（使能本身不动）；
**`i_des` 是电流上限**（=0 完全不动；空载上 0.2 与 1.0 结果一致）；**带载用"摩擦阈值扫描"验证**（阈值 `i_des ∈ (0.006, 0.007]`、
`k ≈ 18~22 N·m/单位` = `NM_PER_I_DES` 的来源）。⚠️ **别用手测上限**（手在 ~0.5 N·m 就打滑，试过两轮测出来的只是手）。
完整数据（PID 对比、位移表、快照路径）见 `docs/TESTING.md`。

**2026-10-01 真机整臂实测**（6 台**裸电机平放桌面、无负载、未组装**；`/dev/ttyACM0`）：
- **电机侧看门狗 `0x09`：6 台已统一为 `10000` 计数 = 500 ms 并已存 flash**（**断电重上电复核仍是 10000** ⇒
  flash 路径 + 手册的 `0xAA`/`0x01` 字节都验证过）。⚠️ 改动前 id1=15000、id2~id6=0，那个不一致触发过 ERR=13 锁存。
- **看门狗实测**：400 ms 喂狗 2.4 s **不触发**；700 ms 一帧 ⇒ 第 2 轮 **ERR=13 且电机自己关输出**（fail-safe）、
  **锁存只能断电清** ⇒ 实测超时窗口 **(400, 700] ms**，与 `0x09=10000` 一致。
- **只读** 6/6 应答 ERR 全 0、`unknown_ids=[]`；**使能/失能** 1.8 ms → 6/6。
- **100 Hz 发+收 20 s**：都是 **100.0 Hz、0 超时**、**位置漂移 0.00000 rad**、28~32 ℃；
  **500 Hz 单跑 3 s → 499.7 Hz**（write p99 0.236 ms）；**500 Hz 发 + 100 Hz 收同跑 → 482.3 Hz、2.4% 超时**
  （两个 Python 线程 GIL 争用）⇒ 要干净 500 Hz 就降到 ~400 Hz 或把收侧分进程。
- 完整证据（逐项命令与输出）见 `docs/TESTING.md`。

**力矩 / 温度监控（`DmArm._monitor_step()`，挂在**收循环**上 ⇒ 采样率 = `feedback_hz`）**：
- **力矩只查 POS_VEL**：阈值来自 yaml 的 `torque_monitor_threshold`（j1~j3=**15.0**、j4~j6=**5.0**），
  连续 `torque_monitor_count` 次（默认 **10** ⇒ 100Hz 下 **100ms** 防抖）越限才算故障。
  三模式分工：MIT 发之前钳位（**已实现**，见下）、力位混控 `i_des` 限幅（已实现）、**POS_VEL 只能事后监控**
- **温度所有模式都查**：`temp_mos` 与 `temp_rotor` 各自独立判（`>=`），共用 `temp_warn=80` / `temp_fault=100`℃；
  **同一传感器同时满足 warn 与 fault 时只报 fault**（`if/elif`）。
  warn 限流打 stderr —— ⚠️ 限流键是 **(关节, 传感器) 组合**：6 台 × 2 传感器 ⇒ 最多 **12 条 / 5s**（不是 1 条/5s）；
  fault 先**把全部越限项收集齐**，再一次性抛 → 收循环先急停再抛
- 真机误报体检（2026-10-01，15 秒双循环 100Hz）：最大 |力矩| **0.012~0.048 N·m**（阈值 5/15）、
  温度 **30~33 ℃**（warn 80）⇒ **零误报、零漂移**
- **真机注入验证（力矩路，2026-10-01，单台 j4/4310）**：写 `0x0A=2`（RAM）+ 把阈值临时设成 **0.005**（低于实测噪声）
  ⇒ 收循环 **92 ms** 报 `力矩 |-0.022| N·m 连续 10 次 ≥ 阈值 0.005（约 100 ms）` → **自动急停 6 台**（ERR 全 0）
  → 位置 **+2.7815 未变**（POS_VEL 保持帧确实不经过钳位/换算）→ 收尾 `0x0A` 写回 **1** 并读回确认
- **过温路验证（2026-10-01）**：⚠️ 裸机**没法安全升到 80/100 ℃**（升温要持续电流 ⇒ 要负载，而手在 ~0.5 N·m 就打滑）⇒ 用**阈值注入**：
  把 `temp_warn`/`temp_fault` 降到现场温度以下，让**真实读数**走**真实代码路径**。真机 `temp_warn=25` ⇒ **12 条** warn 不抛、
  `temp_fault=30` ⇒ **一次抛 12 条**；只使能 j6 跑收循环 ⇒ **第 1 圈 0.7 ms 就抛**且自动急停（6 台 ERR=0、位置未变）。
  离线用例覆盖：两传感器独立、`>=` 生效、warn/fault 互斥、无反馈跳过、**MIT 下力矩大也不报**、防抖第 10 次、两类故障一起收集。
  ⚠️ **仍未验证**：电机物理上真到 80/100 ℃ —— 但那与 34 ℃ 走同一条 `>=` 比较，差别只在数字。

**MIT 力矩钳位（`Joint._clamp_mit_torque`，只在 MIT 生效）**：
- `torque_max` 来自 yaml（j1~j3=**12.0**、j4~j6=**3.5**，DESIGN §4.3 额定值）；`torque_max=None`（单关节直用）时不钳
- 预测式 `tau = kp·(q_des−q) + kd·(dq_des−dq) + tau_ff`（**全部电机侧量**，用缓存里的 `pos/vel`）：
  - **PD 项自己超** `torque_max` ⇒ **拒发抛异常**（钳 `tau_ff` 救不回来）
  - 否则把 `tau_ff` 钳到"总力矩 = ±`torque_max`"，并限流打一条 stderr（`CLAMP_WARN_INTERVAL`=5s）
  - **没有缓存状态就拒发**（算不出来就不装作算过了）
- ⚠️ 用的是缓存 `q/vel`（滞后 1~20ms）⇒ **近似**钳位：慢速可忽略，5 rad/s 时 kp=10 约差 1 N·m
- 真机验证（2026-10-01，单台 j6/4310）：拒发路 `kp=10 / Δq=0.5` → PD=5.0>3.5 抛异常，**总线 `sent` 一个字节没增**；
  钳位路注入 `torque_max=0.05` → 日志 `tau_ff 被钳：+0.500 → +0.010`，0.5 秒持续发帧**零位移**（0.05 N·m < 摩擦 0.145）
- 力位混控用**另一条路**（见下）：没有力矩通道，就把 `torque_max` 换算成 `i_des` 电流上限

**力位混控的力矩上限（`Joint._limit_i_des`，2026-10-01 加）**：
- 换算：`i_des_cap = torque_max / nm_per_unit`，`nm_per_unit` 来自 `arm_config.NM_PER_I_DES`
  （**实测上界**：4340P = 40、4310 = 22 N·m/单位；方法 = 摩擦阈值扫描，见下）
- `current` 是**上限**不是需求 ⇒ **钳 + 限流日志（5s）**，不像 MIT 那样拒发（钳它是单调安全的）
- 没有 `torque_max` / `nm_per_unit`（单关节直用）⇒ 原样放行；**`enable()` 的保持帧直发 `i_des=1.0`，不经过这里** ⇒ 使能仍是真保持
- 于是 yaml 的 `torque_max` 在两种模式下都生效（DESIGN §4.2 原本就这么写）：
  j1~j3 `12.0 / 40` ⇒ `i_des ≤ 0.300`；j4~j6 `3.5 / 22` ⇒ `i_des ≤ 0.159`
- ⚠️ 声明 mode 4 时，型号**必须**在 `NM_PER_I_DES` 里，否则 `arm_config` 加载即拒（算不出上限就别用）
- 真机**预测性验证（2026-10-01，单台 j6/4310）**：用阈值 `i_des≈0.0065` 预测 **4/4 命中**（不动/动/不钳/钳到 0.159）

**单位与换算**：位置 `电机侧 = direction × 关节侧 + offset`（逆换算 `关节侧 = direction × (电机侧 − offset)`）；
**速度 / 力矩是矢量：只乘 `direction`，不加 `offset`**。本层**没有减速比折算**（电机报的就是输出轴 rad）。

**三层坐标（M5 起，最容易看错的地方）**：ros2_control / URDF / MoveIt 用的是**模型坐标**，
插件在里面再套一层模型对齐：

    模型坐标  --sign / zero_shift-->  q_ours  --direction / offset-->  电机 p_m
    (ros2_control 接口)              (dm_joint 的"关节侧"、软限位在这一套)   (电机读数)

- `sign` / `zero_shift` 来自 `arm_description/config/align.yaml`（M1b 实测），
  `direction` / `offset` 来自 `motor_driver/config/joint.yaml`；两者由 **xacro 变成 `<param>`** 传进插件 ⇒ 单一真源不破。
- 换算函数就一份：`dm_joint.hpp` 的 `model_to_ours()` / `ours_to_model()`（插件与测试共用，别各写一遍）。
- 速度 / 力矩过这层只乘 `sign`（不加 `zero_shift`）。

**2026-10-03 真机（6 轴整臂，`/dev/ttyACM0`）**：
- **`MST_ID(0x07)` 已统一为 `0x11~0x16`**（用户改的；`ESC_ID` 仍 1~6）。本层认电机只看反馈数据段 `D[0] & 0x0F`，**不看接收帧里的 CAN ID** ⇒ 改 MST_ID 对 `dm_bus`/`Joint`/`dm_registers` 无影响；`dm_bringup` 的 `--fb-id` 默认正是 `0x10+id`。
- **位置真源 = 寄存器 `0x50 p_m`（float32）== 反馈帧的 `pos`**；**≠ `0x51 xout`**（真机实测每台差一个常数：+0.072 / −0.016 / −0.031 / +0.058 / +0.170 / +0.259 rad；交替读数证明它不随时间漂）⇒ 标定与换算一律按 `p_m`（或反馈帧）。
- **多圈绝对位置跨断电保持**：6 台断电约 8 s 再上电，位置 Δ ≤ **0.00015 rad** ⇒ `offset` 标一次长期有效，不必每次上电回零。
- **本项目的零位/方向/软限位已标定**（`config/joint.yaml`）：offset 逐关节目视零位读数；direction 手推**加通电复核**（j1~j5 = −1、**j6 = +1**）；软限位由用户摆姿态端点换算（j1/j2/j3/j4 只在零位一侧，j5/j6 两侧）。⚠️ 这是**本项目自定义**的零位与方向，与 DESIGN.md 引用的参考项目不同。
- **`kp` 只能把关节推到离目标"负载/kp"的地方**：真机 j1 `kp=15` 差 0.042 rad（≈ 4340P 静摩擦 0.62 ÷ kp）、**j2** `kp=30` 差 0.06 rad（重力 1.76 ÷ kp）。**把残差积进 `tau_ff` 前馈**后能贴到 **0.03°~0.6°**（j3/j4/j5/j6 实测），总力矩仍受 `torque_max` 钳位。
  ⚠️ 归属更正（2026-10-05）：那条 `1.76 ÷ kp` 一直是 **joint2**（`docs/TESTING.md` §十二 的表里就是这么写的），
  本文早先误记成 j3。**今天独立佐证**：张开位用 POS_VEL 托住、读电机反馈的保持力矩 ⇒ **j2 = −1.771 N·m**，
  与 1.76 吻合；而同一姿态下 j3 是 **−5.27 N·m**。

**重力补偿（MIT + 前馈，2026-10-05 整链真机跑通）**：
- **开关**：`gravity_ff:=true` ⇒ 整条链的关节进 MIT（写 `0x0A=1`）+ 每帧按**实测姿态**算重力项当前馈；
  `false`（默认）⇒ 一个字节都不变（仍走 POS_VEL）。`gravity_ff_scale`（0~1）用于分级上电。
- **只有 MIT 能给力矩**：POS_VEL 帧里没有力矩字段，力位混控的 `i_des` 是电流**上限**也不是前馈
  ⇒ ros2_control 链上做重力前馈**必然**要整链换模式，不是"只换保持帧"。
- **模型来源**：默认用**辨识版** `urdf_path=.../arm_identified.urdf`（`dyn_model:=arm.urdf` 可切回名义版）。
  辨识精度：整体 RMS **1.323 → 0.418 N·m**；张开位实测保持力矩 vs 模型 RMS **0.317（辨识版）vs 1.096（名义版）**。
- **符号链已验证**（三条独立路径）：与 MuJoCo 逐位一致（≤8.9e-15）；真机带载关节 j2/j3/j4 模型与实测**符号全同**；
  已知 `tau_ff` 标定 effort 往返 ≈1:1。
- **`effort` 状态接口**（模型坐标 N·m）：来自电机反馈的**电流估计**，不是力矩传感器。
  用途是"POS_VEL 托住时读真实保持力矩"与 ROS 侧监控；**它的存在让重力模型能被数据校核**。
- **静态精度：杠杆是 `kp`，不是积分**（2026-10-05 分层坐实）。误差的本质是**死区**（稳态
  `kp·e = 扰动` ⇒ `|e| ≤ 扰动/kp`；j3 在 kp=7 时 0.62/7 = 0.089，与实测 0.106 吻合）⇒ 实测 max 残差
  kp=7 **0.106** → kp=15 **0.059** → kp=25 **0.0385** rad（单调，2.75×）。
  ⚠️ **宿主侧积分破不了这个死区**：它 100 Hz 爬、越过静摩擦就"跳一格"、摩擦反向、再爬 ⇒
  ki≥1.0 时出现**极限环**（实测 j3 抖动 0.107 rad）。`kp` 是**固件在电机里按 10+ kHz 执行**的，
  只有它能做亚静摩擦的连续蠕动。现取 **kp=25（j1~j3）/ 15（j4~j6）、kd=0.8、ki=0.3**。
- ⚠️ **`kp` 有硬上界 = `torque_max / 轨迹期望滞后`**：`clamp_mit_torque` 在 **PD 项自己超过 `torque_max`**
  时**拒发** ⇒ `write()` 返回 ERROR ⇒ CM 报硬件错误 ⇒ **失能 ⇒ 机械臂掉下来**。
  j1~j3（12.0）可到 ~80；**j4~j6（3.5）只能到 ~25**（0.14 rad 滞后）⇒ 50/100 在现有额定值下不可用。
- ⚠️ 改善**次线性**（kp 7→25 是 3.6 倍、误差只降 2.3 倍；`kp·e` 从 0.74 涨到 0.96 N·m）⇒ 疑似**传动柔度**
  （编码器在电机侧、看不到减速器扭转），再往上边际收益递减。
- ⚠️ **还差**：①逐关节 `kp`（j3 残差最大、而它 `torque_max=12` 有余量）；②`kd` 没标定；
  ③摩擦前馈；④安全：只有"残差守卫 + `torque_max` 钳位"，**没有**安全认证层、没有碰撞检测。

## 6. 陷阱清单（都是这个工程真踩过的）

| # | 陷阱 | 处置 |
|---|---|---|
| 1 | **`NaN` 会穿过钳位**（`min/max` 与 NaN 比较恒 False） | `prepare_frame()` 入口 + 三个 `set_*` 各自 `math.isfinite` 拦；抛 `ValueError` 且**不失能**（NaN 是软件错，失能反而危险） |
| 2 | **切进位置类模式时电机内部指令被清零** | `enable()` 在 mode 2/4 下**没有缓存位置就拒绝使能**，并使能后**立刻补"保持帧"** |
| 3 | **力位混控的 `i_des` 是电流上限、不是力矩前馈** | 给 0 = 一点力都不给（负载下会垂）；`enable()` 的保持帧用 `1.0`。**2026-10-01 真机结案**：`i_des=0.0` ⇒ 完全不动（\|tau\| 0.007）；空载自由轴上 `0.05~1.0` 结果一致（5% 就够驱动）⇒ 上限只在环路真要更多电流（负载/加速）时才起作用；**带载用摩擦阈值扫描验证**（见 §5）：阈值 `i_des ∈ (0.006, 0.007]`、`k ≈ 18~22 N·m/单位`。⚠️ **别用手测上限**：手在 ~0.5 N·m 就打滑（试过两轮，测出来的只是手）。**上位机侧的上限已实现**：`i_des ≤ torque_max / NM_PER_I_DES[型号]`（见 §5） |
| 4 | **`force_pos_frame` 与 SDK 约定不同** | 我们收**物理量**（rad/s、0~1 标幺）并在函数内放大；SDK `control_pos_force` 收**已放大**的整数。别把 SDK 参数直接抄过来（数值已逐字节对拍一致） |
| 5 | **两层 `get_state()` 语义不同** | `MotorBus.get_state()` 返回 `None`（"从没收到过"是正常）；**`Joint.get_state()` 抛 `RuntimeError`** |
| 6 | **所有 `send_*` 先查 `registered_ids`**（`_require_registered`）；`send_frame` **查不了**（它不收 `motor_id`，CAN ID 已在帧里） | 忘注册会 `RuntimeError`；`_limit()` 也从 `KeyError` 改成了 `RuntimeError` |
| 7 | **`set_*` 写串口失败只抛、不失能**（用户明确要求） | 只有 `enable()` 有失能兜底；且兜底失能**也失败**时 → stderr 告警 + **重抛原异常** |
| 8 | `mode` 是**声明不是读数**，声明错 → 帧被电机**静默丢掉** | 每个 `set_*` 先 `_require_mode` 式检查；切完 `0x0A` 必须同步改 `Joint.mode`（`switch_mode()` 只给步骤，不会替你改） |
| 9 | **`limit` 用错档位：力矩差数倍且不报错** | `arm_config` 加载时校验软限位换算是否落在 ±PMAX；`Joint` 构造时校验 bus 上注册冲突 |
| 10 | **配置在包内** `src/motor_driver/config/joint.yaml`（2026-10-04 从仓库根移入） | `setup.py` 的 `data_files` **源路径只能相对**：colcon 的 ament_python task 会 `assert not os.path.isabs(source)`，用 `Path(__file__)` 拼绝对路径会直接构建失败；安装后路径 `share/motor_driver/config/joint.yaml` |
| 11 | **裸 import 风格**（`from dm_bus import ...`）需要包目录在 `sys.path` | 裸跑：`PYTHONPATH=src/motor_driver/motor_driver`；**按包导入**：`PYTHONPATH=src/motor_driver`（`src/motor_driver/` 是 ROS 包目录，python 包在它**里面**）；`dm_bus` 对 `dm_frames` 有兜底；`dm_registers` 自己把包目录插进 `sys.path`，所以裸跑/`-m`/console script 都能用 |
| 12 | `Joint.__init__` 参数顺序（有默认值的 `offset` 之后不能再有必填项） | 当前签名：`(bus, motor_id, name, direction, limit, offset=0.0, ...)` |
| 13 | **寄存器回包与反馈帧共用 `CMD=0x11`**（SDK `DM_CAN.py:373` 靠 `data[2] ∈ {0x33, 0x55}` 区分） | `poll()` / `wait_feedback()` / `_reg_io()` **一律先过 `is_reg_response()`**；否则寄存器回包会被当反馈帧解出**垃圾 `MotorState`** 并污染缓存（已修） |
| 14 | **存储参数帧手册与 SDK 不一致**：手册 `0xAA`+`0x01`，SDK `save_motor_param` 发 `0x00` | `save_params_frame()` 按**手册**发 `0x01` —— **2026-10-01 断电验证通过**（写 `0x09=10000` 后断电重上电仍是 10000）⇒ **别改成 `0x00`** |
| 15 | **寄存器写数据是 float32 还是 uint32 由 RID 决定**（SDK `is_in_ranges`：7~10 / 13~16 / 35~36 为 uint32） | `dm_registers._INT_RIDS` 与之逐条一致；**编错不报错、只会静默写坏**。手册把 `0x25 Boot_ver` 写成 uint32 而 SDK 按 float（唯一冲突处，只读不写） |
| 16 | **`DmArm` 不替你切模式**：`set_joint_positions` 要求 `mode==2`、`set_joint_mit_all` 要求 `mode==1`，而 `config/joint.yaml` 现在是 `mode: 1` ⇒ **`set_joint_positions` 会当场拒**（假总线实测） | 要用 POS_VEL：先用 `dm_registers` 写 `0x0A=2`，**并把 yaml 的 mode 同步改成 2**（否则声明与实际不符，帧被静默丢掉） |
| 17 | **`DmArm` 的批量"统一增益"只适合点动**：4310(j4~j6) 与 4340P(j1~j3) 的 kp 量级差 5 倍以上 | `set_joint_mit_all(targets, kp, kd, ...)` 对所有关节给同一组增益；整臂控制要按关节给不同增益（下一轮） |
| 18 | **两个循环都是阻塞的** ⇒ 不能同时跑（设计里的"500Hz 发 + 100Hz 收"要并行得调用方自己开线程） | 真机实测：**500Hz 单跑 499.7Hz / 1 超时**；**与收循环同跑掉到 482.3Hz / 35 超时（2.4%）**（两个 Python 线程 GIL 争用）⇒ 要干净的 500Hz 就降到 ~400Hz，或把收侧放到另一个进程 |
| 19 | **`fn` 的返回值严格判 `is False`**：没写 `return` 的函数（返回 `None`）**不会**让循环停 | 要停就 `return False`，或 Ctrl-C（Ctrl-C 会**先急停**再返回统计）。⚠️ **2026-10-01 真机测试真的踩了**：写成 `lambda a: t > DUR`（"完成了吗"式布尔）→ 第一次就返回 `False` → **静默只跑 1 圈就"正常退出"**（不失能、不报错、`iters=1`）。正确写法只有 `if 超时: return False`（隐式 `None` = 继续） |
| 20 | **陈旧回包会骗缓存**：发循环单跑（没人 `poll()`）时回包全堆在适配器/OS 缓冲里；之后 `get_state()`/`check_health()` 读到的是**旧帧**。真机实测：3 秒 500Hz 压测（9000 帧）后 `disable_all()` 明明成功，缓存却连续两轮显示 `ERR=1`（实际已 `ERR=0`），各轮还涌入 32 / 256 / 224 条积压 | 排空要 `bus.flush()` + **多轮**刷新直到连续两轮一致（实测 3 轮才收敛）。⇒ **两个循环必须成对跑**；压测/单跑发循环之后，**先排空再信状态** |
| 21 | **未标定（`offset=0.0`）时把"当前位置"当目标是会动的命令**：`prepare_frame()` 会先做**关节侧软限位钳位**，而电机原始读数未必落在软限位内 | 真机实测 joint4 原始 `pos=+2.7815`、软限位 `[-1.87, 1.57]` ⇒ 用户发"保持当前位置"会被钳成 `1.57`，等于**命令它转 −1.2 rad（≈−69°）**。⚠️ 但 `enable()` 在 POS_VEL 下的保持帧**故意绕过钳位/换算**（直接发电机侧实测值）⇒ **使能本身不会动**，两者别混。装臂前必须先标定 |
| 22 | **MIT 的力矩上限是"近似"的**：`torque_max` 只能按 `kp·(q_des−q)+kd·(dq_des−dq)+tau_ff` **预测**，而 `q/dq` 来自缓存（滞后 1~20ms） | 已实现（`Joint._clamp_mit_torque`）：PD 项自己超 ⇒ 拒发；否则钳 `tau_ff`。慢速时预测很准，快速运动时（5 rad/s、kp=10）可能差 ~1 N·m ⇒ **别把它当硬保证**，POS_VEL 侧还有事后监控兜底 |
| 23 | **监控挂在收循环上** ⇒ 只跑 `run_control_loop`（发）就**完全没有力矩/温度监控** | 两个循环必须成对跑（或自己在 `fn` 里调 `_monitor_step`）。另外：**自己写排空循环极易写错** —— 真机实测 `flush → refresh → sleep`（sleep 之后不 poll）会让新到的回包在下一轮被 `flush` 掉，于是永远读到旧的 `ERR=1`。**直接用 `sync_states()`**（它 poll 到安静之后才取状态），真机 **102 ms** 给出真相 |
| 24 | **使能了却不发帧的关节会被"电机侧看门狗"打掉**：`enable_all()` 后只给一部分关节发目标，其余关节收不到 CAN 帧 ⇒ 超时 ⇒ **锁存 ERR=13（通讯丢失），只能断电重上电** | 真机实测（2026-10-01）：当时 id1 的 `0x09=15000`（750ms）、其余为 0，`enable_all()` + 只给 id6 发帧 1.5 秒 ⇒ **id1 锁存 ERR=13**。⇒ **要么只使能你要控制的关节，要么每圈给所有已使能关节发帧**。⚠️ 现在 **6 台都装上了 500ms 看门狗**（`0x09=10000`，已存 flash）⇒ 这条规矩**普遍适用**；正常控制循环每台 2~10ms 一帧，余量 50~250 倍。**保护本身已实测**：400ms 喂狗 2.4s 不触发；700ms 静默 ⇒ 电机自己关输出并锁存（这就是"主机崩了电机松力"那层，代价是必须断电才能恢复） |
| 25 | **已使能关节的"发帧空档"不能超过 `0x09`（现在 500ms）**：看门狗是在保护你，但它**只认喂狗帧、且一触发就锁存**。所以控制循环里任何 >500ms 的阻塞都会让**全臂掉力并锁存 ERR=13**（`sleep`、慢计算、阻塞式服务调用/日志、串口重连、调试器断点、GC/调度卡顿） | 实测（2026-10-01）：400ms 喂狗 2.4s 不触发；700ms ⇒ 触发 + 锁存 ⇒ 超时窗口 (400, 700]。⇒ **`fn` 里不许有 >100ms 的阻塞**（留 5 倍余量）；要慢操作就搬到别的线程/进程，或先 `disable_all()` 再做。⚠️ **ROS2 集成时头号坑**：回调里一个阻塞调用就能打掉整条臂，而且恢复要断电。`dm_bringup` 的 `jog` / `bandwidth --enable` 已在代码里硬限制 `--hz ≥ 5`（`MIN_HZ_WITH_ENABLE`）|
| 26 | **`is_reg_response()` 靠 `D[2] ∈ {0x33,0x55,0xAA}` 分辨寄存器回包，可反馈帧的 `D[2]` 是 POS16 低字节** ⇒ 电机停在低字节恰为 `33/55/AA` 的位置时（约占位置范围 1.2%，而且停住就一直中招），它**每一条反馈都被当寄存器回包丢掉** ⇒ 该电机在缓存里**彻底隐身**（`get_state()` 抛、力矩/温度监控全瞎） | 真机实测（2026-10-03）：id2 停在 +1.79 rad（低字节 0x55）⇒ **20/20 条反馈被丢**、`unknown_ids` 为空但缓存里没有 id2。修法：判据补 `D[0]≤0x0F 且 D[1]==0x00`（真寄存器回包实测恒为"目标 ID 小端"）。残留（已写进注释）：失能 + `pos∈[-12.5,-12.40)` 的 3 个 LSB 仍会误判 |
| 27 | **`launch_ros.actions.Node` 的关键字是 `parameters=`，不是 `params=`**（`params` 是 rclcpp/C++ 的写法） | 2026-10-04 真踩：`arm_description/launch/display.launch.py` 写成 `params=[{"robot_description": ...}]` ⇒ `ros2 launch` 在**加载文件阶段**就抛 `TypeError: Action.__init__() got an unexpected keyword argument 'params'`，**一个节点都不会启动**（报错里还跟着一条误导性的 `InvalidFrontendLaunchFileError: The launch file may have a syntax error`）。⇒ **改完 launch 文件先跑 `ros2 launch <pkg> <file> --show-args`**：它只解析不启动，能立刻暴露这类构造错误 |
| 28 | **USB-CAN 适配器的设备号会变**（`/dev/ttyACM0` ↔ `/dev/ttyACM1`，随插拔顺序），而 `config/joint.yaml` 的 `channel` 是写死的 | 2026-10-04 真踩：设备从 ttyACM0 变成 ttyACM1 ⇒ 只读节点每轮读串口都失败（当时只打 ERROR、不退出）⇒ **RViz 里模型冻在最后一帧**，看起来像"调参没生效"，白折腾半天。⇒ ①`channel` 要跟着改（2026-10-05 又变回 ACM0，已改回）；②节点已加"连续 `max_fail_streak` 轮失败就 FATAL 退出"；③根治是按适配器序列号加 udev 规则固定成 `/dev/dm_can` |
| 29 | **`joint_state_publisher*` 会订阅 `/joint_states` 再回发** ⇒ 与"真机镜像"节点同时开着，两个发布者打架 | 2026-10-04 真踩：M1a 的 `display.launch.py` 没关，`/joint_states` 有 2 个发布者（一个 RELIABLE、一个 BEST_EFFORT），RViz 里的机械臂**发抖**。⇒ 镜像前先 `ros2 topic info /joint_states` 确认 **Publisher count: 1**；`ros2 node list` 里出现**两个 `/robot_state_publisher`** 是同一问题的征兆 |
| 30 | **协议有两份实现（Python `dm_frames.py` + C++ `dm_frames`），浮点运算顺序不一致就差 1 个 LSB** | `float_to_uint` 必须"先钳位 → 先减、后除、再乘 → **向零截断**"（Python `int()` 与 C++ `static_cast` 都是向零）；把乘法换个位置（例如 `x * 4095 / 500`）就可能差 1 LSB。`pos_vel`/`force_pos` 的 float32 也必须走同样的 `double → float` 一轮转换。⇒ **不靠人眼看**：`colcon test --packages-select motor_driver_hardware` 的 34 个逐字节对拍用例就是这条的机器保证 |
| 31 | **往 `src/` 里拷第三方 CMake 工程，会被 colcon 当成"包"** | 2026-10-04 真踩：把达妙官方 C++ 例程拷进 `src/third_party/C++例程/u2can/`，它的 `CMakeLists.txt` 里是 `project (dm_Linux_Drive)`（**`project` 与 `(` 之间有空格**，grep `project(` 抓不到）⇒ colcon 把它识别成 plain cmake 包：`colcon list` 多出一个 `dm_Linux_Drive`，全量构建报 `1 package aborted: motor_driver`（连累了无关的包）。⇒ 修法是标准做法：在 `src/third_party/` 放一个**空的 `COLCON_IGNORE`**（colcon 跳过该目录及其全部子目录）。⚠️ 之前拷 Python 例程没暴露这个问题，只是因为它们**没有 CMakeLists.txt** |
| 32 | **在 pixi/robostack 环境里链接 rclcpp 的**可执行文件**会报「找不到 `-lcap` / `-llttng-ust`」** | 2026-10-04 真踩（第一次写 C++ ROS 可执行文件时才暴露）：`libcap.so` / `liblttng-ust*.so` 就在 `$CONDA_PREFIX/lib` 下，但 **pixi 的激活不设置 `LIBRARY_PATH`** ⇒ 链接器不搜那个目录。共享库（我们的插件 `.so`）不受影响 —— 允许未定义符号、运行时由 `LD_LIBRARY_PATH` 解析；只有**可执行文件**（gtest、`ros2_control_node`）会失败。⇒ 在 CMake 里 `link_directories($ENV{CONDA_PREFIX}/lib)`（见 `motor_driver_hardware/CMakeLists.txt`）。根治办法是在 `pixi.toml` 的 `[activation.env]` 里设 `LIBRARY_PATH`，但那要改环境配置（待用户定） |
| 33 | **`hardware_interface` 的 `on_init(const HardwareInfo&)` 在 jazzy 已 `[[deprecated]]`** | 新签名是 `on_init(const HardwareComponentInterfaceParams&)` ⇒ `params.hardware_info` 就是那份 HardwareInfo（另有个 `executor` 弱指针）。写老签名能编译，但**每个构建都出一条 deprecated 警告**；换新签名后单元测试也更好构造（默认构造 + 填 `hardware_info` 即可，见 `test/test_dm_system_interface.cpp`） |
| 34 | **插件必须编成 SHARED；默认的静态 `.a` pluginlib 加载不了，而且单元测试看不出来** | 2026-10-05 真机踩：`dm_system_interface` 用了 `add_library()` 默认（**静态**）⇒ 编译链接全过、28 个单测全绿（**测试是直接链静态库，从不 dlopen**），但真机 `controller_manager` 报 `LibraryLoadException … Could not find library corresponding to plugin motor_driver_hardware/DmSystemInterface. Make sure that the library 'dm_system_interface' actually exists.` ⇒ 硬件组件没加载 ⇒ 没有任何状态 ⇒ **RViz 看起来像"模型不对/只有 j1 错"**（真机 j2~j6 本来就 ≈0，只有 j1 是 +90°，所以误判成了对齐问题）。⇒ ①插件 target 用 `SHARED`；②它链的静态核（`dm_hardware`）要 `POSITION_INDEPENDENT_CODE ON`；③**排查入口**：`ls install/<pkg>/lib/` 看是 `.a` 还是 `.so`，以及 **`~/.ros/log/latest/launch.log`（launch 的 stdout/stderr 都在那儿）**。⚠️ 教训：单测覆盖不到"能不能被 dlopen"这一层 |
| 35 | **`ros2_control_node` 会从**共享话题** `/robot_description` 订阅别人的 URDF** | 2026-10-05 真机踩（很危险）：用户那套真机 `real_control.launch.py` 还开着，我起 `arm_moveit_config` 的 mock 链 ⇒ mock 的 CM 从 `/robot_description` 订阅到**真机版** URDF ⇒ `Loaded hardware 'ArmSystem' from plugin motor_driver_hardware/DmSystemInterface` ⇒ 开了真机串口、**把 6 台电机使能了**（`enable_on_activate=true` 也是从别人的 URDF 里来的）⇒ 随后 `关节 joint1 故障 ERR=13`（**锁存，只能断电清**）并在 error 里失能退出。⇒ ①**别用"重映射订阅"这招**：CM 只从话题取描述、**不回退自己的参数** ⇒ 重映射后它永远卡在 `Waiting for data on 'robot_description' topic to finish initialization`（硬件起不来、RViz 里是"残缺的模型"），2026-10-05 当场回退；②真正兜底是两条：**任何时候只开一套**（mock / 真机互斥）（mock 与真机、以及 `move_group.launch.py use_mock:=true`）；③TIOCEXCL 只能挡住"两个进程同时开串口"，**挡不住"订阅错 URDF"** —— 两件事都要防 |
| 36 | **激活硬件时"一次采样就要求所有电机就位"会偶发失败** | 2026-10-05 真机踩：`on_activate` 里只做一次 2 秒 `sync_states`，然后要求 6 台全有反馈 ⇒ 冷启动/刚上电时总有一两台回得慢 ⇒ FATAL `电机 2（joint2）一开始就没有反馈` ⇒ 硬件没激活 ⇒ 没有 `/joint_states` ⇒ RViz 里"残缺的模型"（而 Python 侧一问 6 台全部正常、ERR 全 0 ⇒ 是**时序**不是硬件）。⇒ ①激活时**重试**（最多约 5~8 秒：每轮 0.5s `sync_states` + 0.3s 等）直到全部就位；②**只读模式容忍缺席**（告警 + 跳过，能看几台是几台），"6 台必须全在"只在 `enable_on_activate=true` 时严格；③只读模式下某台 ERR 故障也**只报一次、不中断**（否则一台故障把整条只读链拖死，反而看不见其它关节） |
| 37 | **MoveIt 的 `CheckStartStateBounds` 不会替你"就近钳位"起始状态，越界就**拒绝规划**** | 2026-10-05 真机踩：真机 j1 = `1.371670`，比 URDF 下界 `1.392202` 低 **0.0205** —— 即使这个量**小于** `ompl_planning.yaml` 的 `start_state_max_bounds_error: 0.1`，适配器照样报 `Start state out of bounds. Aborting planning pipeline.` ⇒ `error_code = 99999`、轨迹为空。⚠️ 好在"拒绝"比"静默钳位"安全（只读模式下机械臂一个字节没收到，规划前后 `/joint_states` 逐位相同）。⇒ **真机规划前先比对"实测姿态 vs URDF 限位"**（模型坐标下的限位见 §5）；越界就先把关节弄回限位内（本轮是手动推回去，零电机命令） |
| 38 | **"使能保持"曾是 follow-me 式（每圈重读实测位置）⇒ 没有回复力，重力能把关节慢慢压走**；**已改成"锁定使能那一刻的位置"** | 2026-10-05 真机实测（`enable_on_activate:=true`、不起轨迹控制器）：j1/j3 在重力下**缓慢蠕动**，20 s 里 j1 −0.00267 rad（0.15°）、j3 −0.00153，j2/j4/j5/j6 为 0。⇒ 它保证的是"**不跳变、不朝零位冲、每圈喂狗**"，**不保证不动**。⚠️ 轨迹**执行期间与之后**不受影响（`hw_commands_` 不再是 NaN，保持的是最后一条命令）。**当天已修**：`on_activate` 把"使能那一刻的电机侧位置"锁进 `JointParams::hold_pos`，`write()` 一直发它 ⇒ 变成**有回复力**的保持。真机复测同一 20 s 场景：j1/j3 各只走 **1 LSB（0.000381 rad）就钉住**（之后 12 s 不动），改前 j1 是 −0.002670 且持续下滑。⚠️ 代价：使能状态下**用手推关节会被顶回来**（follow-me 时是"推哪算哪"）；重力负载下会持续通一点电流。回归用例 `test_dm_system_interface.cpp::HoldTargetIsLatchedNotReread`（**旧实现下必失败**，已实测） |
| 39 | **失能状态下机械臂会自由塌回"折叠位"** —— 张开姿态**不是自由状态的稳定点**；"跑完退出、下次接着来"的流程**不成立** | 2026-10-05 真机实测：把手臂张开到末端 z=0.325 m（j2 −0.55 / j3 −0.70 / j4 +0.30）之后**失能**，几十秒到一两分钟就自己塌回折叠位 —— j2 掉 **0.55**、j3 掉 **0.76**、j4 掉 **0.63** rad，全程**没有任何命令**（下一次只读链起来时姿态已经是 `[1.4724, +0.0008, -0.0233, +0.0027, ...]`）。⇒ ①**要停在工作姿态必须保持使能**（靠陷阱 #38 的锁定保持撑着）；②**每一轮动手前先读实测姿态**（本工程一直这么做，现在知道为什么非做不可）；③张开姿态只在**同一次运行内**有效，跨运行做笛卡尔试验必须"重新张开 → 立刻探/算 → 执行"一条龙；④别把"失能"当支撑用 |
| 40 | **JTC 的 `constraints.goal_time` 默认 0 ⇒ 永远不 ABORT，只会"无限挂住"**（表现 `val=-6`、机械臂停在半路）；声索 velocity 命令接口还会**多一条速度容差判据** | 声索 velocity ⇒ `state_error_.velocities` 非空 ⇒ 到达判据里多一条"末速度 ≤ `stopped_velocity_tolerance`（默认 0.01）"，而 `goal_time=0` ⇒ 超时 ABORT 分支**走不到**。真机 A/B/C：`[position velocity]` 无 constraints ⇒ **val=-6**；`[position]` ⇒ val=1（0.84 s）；`[position velocity]`+constraints ⇒ val=1（**0.808 s**）。⇒ **POS_VEL 路径只声索 position**；MIT 路径声索时必须显式给 `stopped_velocity_tolerance: 0.05` + `goal_time: 1.0`。本工程两份配置 `ros2_controllers.yaml` / `ros2_controllers_mit.yaml`。**详见 `docs/LESSONS.md` §七** |
| 41 | **MIT 下 `enable()` 发的是零增益保持帧（出力恒 0）⇒ 使能到第一次 `write()` 之间机械臂真的在自由下落** | 真机实测 j4 在这段窗口掉了 **0.104 rad**，直接把位置守卫顶爆。⇒ `on_activate` 里锁完保持目标后**立刻补一帧真实 MIT 保持帧**；**别用放宽阈值糊过去**。**详见 §八** |
| 42 | **重力前馈的残差守卫必须按"保持 / 跑轨迹"分开判**（我在这踩了两次），上电瞬间还要宽限+防抖 | 保持（命令 NaN）⇒ 位置偏离保持点 > 0.1 rad 或 速度 > 0.5 rad/s（且连续 0.2 s、宽限 0.5 s）；跑轨迹 ⇒ **只查跟踪误差** > 0.3 rad。⚠️ 误触发的表现是"前馈被关掉、机械臂只走一半"，**看起来像模型不对**。**详见 §九** |
| 43 | **上游 CAD 的惯量与这台实机不符**，而且**非均匀地错** ⇒ 直接做前馈有害 | 张开位实测：j2 模型 −0.23 vs 实测 **−1.77**（7.8×）、j3 −6.94 vs **−5.27**（0.76×）、j4 −1.99 vs **−0.65**（0.32×）。**符号全对、量级全错** ⇒ 必须做惯性辨识。结果 **RMS 1.323 → 0.418 N·m**。落地：`arm_description/config/gravity_identified.yaml` + `scripts/apply_identified_inertia.py`。**详见 §十** |
| 44 | **固定关节的子连杆会被 pinocchio 合并进父连杆** ⇒ 辨识出的是"合并体"的参数 | `end_link`/`gripper_tcp` 并进 `link6`；只改 `link6` 会让 pinocchio **再加一遍** ⇒ 生成物反而更差（RMS 1.03），误差还传到 j2/j3/j4。⇒ 写回时**把固定关节子连杆惯量清零**。排查手法：**把参数手动设进模型再比**。**详见 §十** |
| 45 | **辨识/静态测量必须等固件积分收敛，并多次采样取中位数** | 第一版"停 2 s + 采一次"混进瞬态（同姿态重复采时 j2/j3 **同时**掉 1.3/2.5 N·m —— 摩擦只该作用在单个关节、量级 0.62 ⇒ 判据就是"两个关节同时掉=瞬态"）。改成"停 4 s + 3 次取中位数"后 RMS 0.515→**0.418**。**详见 §十** |
| 46 | **`effort` 是电机的电流估计、不是力矩传感器**，尺度必须标定 | 标定法：`gravity_ff=true` 时发的 `tau_ff` 是已知量，读回 effort 比它 ⇒ j2 1.01~1.06 / j3 0.97~0.98 / j4 0.85~0.88 ⇒ **往返 ≈1:1**（排除"差 2 倍"）。价值：POS_VEL 托住时固态积分把误差积到 ~0，**电机报的就是真实保持力矩**。**详见 §十一** |
| 47 | **"失能后塌回去的那个姿态"往往靠在机械硬限位上** ⇒ 在那里测重力毫无意义 | 停放姿态 j3=+0.068（上限 **+0.0508**）、j2=+0.0008（上限 +0.0584）—— 就在限位上，重力由结构承担。当时四轮分级上电全部"纹丝不动"，**误以为前馈算对了**；其实模型说 j3 需要 −7.27 N·m，kp=7 时本该偏 1.04 rad。⇒ 测重力必须换到真正带载的姿态（张开位）。**详见 §十二** |
| 48 | **构建期用 xacro 生成 URDF 时，不能用 `$(find <本包>)` 引用本包配置** | 构建期本包还没安装 ⇒ `PackageNotFoundError: package 'arm_description' not found` ⇒ 整个包构建失败。⇒ CMake 里**用源码路径覆盖** `joint_cfg`/`align_cfg`/`mit_gains_cfg` 并加进 `DEPENDS`。**详见 §十三** |
| 50 | **宿主侧积分（100 Hz）破不了摩擦死区，大 ki 反而出极限环** | 静态误差本质是**死区**（`\|e\| ≤ 扰动/kp`）。积分爬到刚过静摩擦 ⇒ 关节跳一格 ⇒ 摩擦反向 ⇒ 再爬。真机 ki 分级（kp=7）：ki=0/0.3/1.0/3.0 时三次采样跳动 max = **0 / 0.010 / 0.107 / 0.074 rad**。⇒ 静态精度的杠杆是 **`kp`**（固件在电机里按 10+ kHz 执行），积分只适合补**系统性**误差（j2 在 ki=1.0 时 0.034→**0.0026** 且跳动为 0）。**详见 `docs/LESSONS.md` §十四** |
| 51 | **`kp` 的上界由 `torque_max / 期望滞后` 决定 —— 超了就"拒发"，而拒发 = 掉臂** | `clamp_mit_torque` 在 **PD 项自己超过 `torque_max`** 时拒发 ⇒ `write()` 返回 ERROR ⇒ CM 报硬件错误 ⇒ 失能（陷阱 #39：失能就塌）。真机 kp=15/25 各一轮**拒发 0 次**；但 j4~j6 的 `torque_max=3.5` 在 kp=25 时只留 0.14 rad 余量，kp=50 会在 0.07 rad 滞后时掉臂。⇒ **要更高 kp 必须先抬 `torque_max`（安全决策）或只抬有余量的关节**。**详见 §十四** |
| 52 | **"保持当前位置"的帧也被软限位钳位 ⇒ 凭空造出 PD 项；kp 大时激活直接 FATAL** | 真机 2026-10-05：激活时 j4 停在软限位外 **0.176 rad** ⇒ kp=25 下 PD=**4.39** > torque_max 3.5 ⇒ **拒发 ⇒ FATAL ⇒ 整条链起不来**（kp=7 时 7×0.176=1.23 侥幸过关，问题一直被掩盖）。⚠️ POS_VEL 的保持帧**早就故意绕过钳位**（陷阱 #21），**MIT 的保持帧漏了这一步**。⇒ 加 `Joint::prepare_frame_raw()` + `set_mit(..., bypass_soft_limits)`：**保持帧（激活那一刻 + 命令为 NaN 时）一律绕过软限位**（PMAX 仍钳），并打一条"落在软限位之外"的 WARN。回归用例 `ActivationHoldFrameBypassesSoftLimits`（**旧实现下必失败**，已实测）。**详见 `docs/LESSONS.md` §十五** |
| 49 | **加了 pinocchio 之后，构建必须走 `pixi run colcon ...`** | pinocchio 进了 `dm_hardware` 的 PUBLIC 链接后 `.so` 链接线变了 ⇒ 没有 `CONDA_PREFIX` 的普通 shell 会报 `找不到 -lcap / -llttng-ust`（陷阱 #32 的兜底只在 `CONDA_PREFIX` 存在时生效）。**详见 §十三** |

## 7. 怎么验证（没有硬件时）

**假 bus 进程内跑**（不落盘，符合"禁止测试文件"策略）。最小骨架：

```python
from dm_bus import MotorState
class FakeBus:                       # 只实现 Joint 用到的那几个方法
    def __init__(self): self.calls=[]; self.st=None; self._m={}
    def get_state(self,i): return self.st
    def send_enable(self,i): self.calls.append(("enable",i))
    def send_disable(self,i): self.calls.append(("disable",i))
    def send_mit(self,i,*a): self.calls.append(("mit",i)+a)
    def send_pos_vel(self,i,p,v): self.calls.append(("pos_vel",i,p,v))
    def send_force_pos(self,i,p,v,c): self.calls.append(("force_pos",i,p,v,c))
    def motors(self): return self._m
    def add_motor(self,i,l): self._m[i]=tuple(float(x) for x in l)
```
另外三招：
- **真 `MotorBus` 不开串口**也能测注册/冲突/准入（构造 `MotorBus("任意路径")` 不碰串口）。
- **帧级对拍**：把参数喂给厂商 SDK `src/third_party/Python例程/u2can/DM_CAN.py` 的同名函数，比较 8 字节数据段。
- **假串口测寄存器 I/O**：给 `bus.ser` 直接塞一个带 `in_waiting` / `read(n)` / `write(frame)` 的对象，
  在 `write()` 里按手册回 16 字节（`[0]=0xAA`、`[1]=0x11`、`[7:15]=8 字节数据`、`[15]=0x55`）——
  不用硬件就能端到端跑 `read/write/dump/verify` 和整个 CLI（把 `dm_registers._open` 换成返回这个假 bus）。
- **C++ ↔ Python 协议对拍**（本仓库**唯一**的测试文件，在 `motor_driver_hardware/test/`）：
  `colcon test --packages-select motor_driver_hardware` —— C++ 造帧，同时用 `popen` 直接调 Python 的
  `dm_frames` 造同样的帧，**逐字节比**（34 个用例：6 类发送帧 + 边界 + 收帧切分 + `RxBuf` 分块 +
  反馈解码 + `is_reg_response` 边界）。**改了任一份 `dm_frames` 都要重跑**，否则两份实现会悄悄漂开。
- **假串口 / 假总线**（C++ 侧，在 `motor_driver_hardware/test/`）：`SerialIo` 是接口 ⇒ 测试塞一个
  **内存字节流**当串口，就能不接硬件走通"发帧 → 收反馈 → 进缓存"整条链；`test_dm_bus.cpp` 还带一个
  **应答器**（收到刷新帧就回一条反馈），所以 `sync_states()` 那种"先丢旧的、再主动问"的流程也能测。
  `test_dm_joint.cpp` 则直接调 Python 的 `joint.py` 比 `prepare_frame`/发帧结果。
- **插件级测试**（`test/test_dm_system_interface.cpp`，11 个用例）：假串口里再塞一台**模拟电机** —— 收到 POS_VEL 就把
  位置跟过去、收到刷新就回状态、收到使能帧就置 ERR=1、寄存器帧回显 RID、**收到 MIT 帧只在 `kp>0` 时才跟位置**
  （零增益帧真机出力恒 0，假电机无脑跟随会把保持目标带偏）。覆盖："只读模式一个控制帧都不发"（含 `gravity_ff=true`）、
  "激活时写 `0x0A=2` + 使能 + **锁定保持目标**"、"命令 NaN 继续保持"、"模型坐标 0 ⇒ 电机侧 1.785878"、
  "故障报 ERROR"、**`gravity_ff=true` ⇒ 写 `0x0A=1`、无 POS_VEL 帧、MIT 帧的 `t_ff == direction × 重力项`**、
  **scale 按比例作用且 >1 被拒**、**守卫的宽限/防抖/位置即时判**。
- **重力模型对拍**（`test/test_dm_gravity.cpp`，3 个用例）：C++ 的 `GravityModel` 与**已跑通的 MuJoCo**
  逐点对拍（4 位姿 × 6 关节，≤1e-9）+ sign 逐关节生效 + 参数/用法错误。
  ⚠️ 它读的是**构建时生成**的 `arm_description/arm_dynamics.urdf`，所以要先构建 `arm_description`。

**真机上电顺序**（每一步都要先只读）：`bus.poll()` 看状态 → `poll()` 拿到位置 → `enable()` → 小增益 `set_mit(kp≈1~5, q=当前位置)` → 确认方向 → 加大 → `disable()` → 关电源。

## 8. 文档真源与过期清单

| 文档 | 状态 |
|---|---|
| `readme.md` | ✅ **2026-09-30 重写**（旧版描述了大量已移出工作区的文件） |
| 本文 `AGENTS.md` | ✅ 与代码同步（改代码请回来改它） |
| `docs/TESTING.md`、`docs/LESSONS.md` | ✅ 有效真源（实测值 / 踩坑） |
| `docs/DESIGN.md` | ⚠️ 设计意图有效，但 v0.12 与"当日工作区现状"等段落**已过期** |
| `docs/reading_guide.md`、`docs/architecture_notes.md` | 外部参考（reBot / PyArmX）导读，与当前代码无关 |

**已移出工作区、但在 git 历史里可取回**（`git show <commit>:<path>`，用 `HEAD~1` 或 `f17aec1`）：
旧版 `dm_registers.py`（605 行）、旧版 `arm_config.py`（674 行）、`tools/*.py`（`smoke_*.py`/`scan_bus.py`/`bus_probe.py`）、**（`dm_bringup.py` 已于 2026-10-01 恢复回包内）**、
`src/mujoco_pkg/`、`src/rebotarm_msgs/`、`src/fake_driver_pkg/`、`ARCHITECTURE.md`、`CURRENT_STATE.md`、旧 `config/rebotarm_b601_mixed.yaml`。
⚠️ 现在这个 `arm_config.py`（200 行）**覆盖**了历史里同名的那份 674 行版本；`dm_registers.py`、`arm.py` 也都是新写的（不是恢复的）。

## 9. 接手后的第一件事

1. 读 [readme.md](readme.md) → 本文 → `docs/TESTING.md`（实测值）→ `docs/DESIGN.md`（设计意图，跳过历史段）。
2. **问用户三件事**：这轮要做哪一层（寄存器工具 / 整臂 / ROS2 / 夹爪）？是否允许新增文件/函数？改动范围到哪为止？
3. 每处改动：**先读文件 → 说明改动清单 → 等批准 → 改 → 验证（贴证据）→ 自审**（错误处理 / 未定义行为 / 冗余 / 安全 / 真机影响）。
4. 自审 checklist（用户会问）：
   - 错误处理完整吗？（哪个异常该失能、哪个不该）
   - 有没有未定义行为？（NaN、越界、空值、None 语义）
   - 有没有冗余/死代码？（未使用的 import、重复的真相、未使用的常量）
   - 安全边界有没有被绕过？（限位、模式声明、注册准入、失能时机）
   - 真机上会怎样？（带电离场、重力负载、朝零位冲）
