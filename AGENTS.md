# AGENTS.md —— 给下一个 AI 的接手说明

> 人看的入口是 [readme.md](readme.md)；本文是**精确状态 + 不变量 + 禁区**。
> 维护约定：**改了代码就回来改这份文件**（本文是"当前状态"的唯一真源；`docs/DESIGN.md` 只讲**为什么**）。

## 0. 硬性约束（先读这段，违反等于白干）

1. **未经用户明确批准，不许写任何代码**；不许"顺手"改无关文件、加函数、加参数、加常量、加文件。
2. **每一处最小改动都先说明、等批准、再动手**（用户会逐条回复）。改动后要**验证并自审**（checklist 见 §9）。
3. **禁止生成本仓库的测试文件**（策略）。验证一律用**进程内 heredoc 假 bus**，不落盘。
   **两处例外**（用户明确批准过）：`motor_driver_hardware/test/` 与 `arm_motion/test/`（C++ / gtest）。
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
| [arm_msgs](src/arm_msgs) | — | 本项目接口包（ament_cmake）：msg `JointMotorCmd` / `JointMotorState` / `ArmStatus`（内嵌 `HardwareStatus`）/ **`HardwareStatus`** + action `MoveToPose` + **5 个 srv**（`SetEnabled` / `HardwareEmergencyStop` 是**插件内部接口**；`ArmOn` / `SafeDisarm` / `ManageEmergencyStop` 是**管理层对外接口**） | 只定义接口、无代码；**M9.2 起 `motor_driver_hardware` 插件依赖它**（两个插件级 srv + `HardwareStatus`），M9.3 的 `arm_manage` 还会用管理层那 3 个 srv；夹爪本阶段不做。字段与语义见 `docs/MANAGE_ARCH.md §4` |
| [arm_description](src/arm_description) | — | URDF/xacro 描述（几何 verbatim 取自 reBotArm，CERN-OHL-W-2.0）+ **`config/align.yaml`（模型对齐真源）** + **`config/mit_gains.yaml`（MIT 保持增益）** + **`config/gravity_identified.yaml`（辨识出的质量/质心）** + `scripts/apply_identified_inertia.py` + **`scripts/gen_joint_limits_header.py`（构建期生成 `joint_limits.hpp`：模型坐标关节限位，给应用层当护栏；并与 URDF 的 `<limit>` 对拍、漂了就构建失败）** + 显示 launch。构建时生成**三份**动力学 URDF：`arm.urdf`（上游名义）、**`arm_identified.urdf`（辨识版、**空载**、前馈默认用）**、`arm_identified_gripper.urdf`（带夹爪；夹爪质量**未辨识**，见陷阱 #56） | 纯数据包；几何**不是我们写的**，改 mesh/URDF 要保留上游许可与来源声明 |
| [arm_bringup](src/arm_bringup) | — | ROS2 胶水层：`real_joint_states`（**只读**镜像）+ **两份控制器配置** `config/ros2_controllers.yaml`（只 position）与 `ros2_controllers_mit.yaml`（position+velocity+constraints，见陷阱 #40） **+ `arm_system.launch.py`（总 launch，M9.4；⚠️ 同日起 `real_control.launch.py` / `mock_control.launch.py` 已删，旧内容看 `git show ac1bde9:<path>`）**：一条命令起全栈、`use_mock` 贯穿 mock/真机（参数 `enable_on_activate`/`spawn_arm_controller`/`arm_controller_inactive`/`vlim`/`gravity_ff`/`gravity_ff_scale`/`mit_controllers`/`start_arm_manage`）；**`arm_controller` 默认只装载不激活**（`spawner --inactive`，把激活交给 `arm_manage` 以保证 M-3 顺序），并在 launch 里自动补 `LD_LIBRARY_PATH`（否则 `arm_manage_node` 报 `librclcpp.so` 找不到）；⚠️ **刻意不拉 `move_group.launch.py`**（它自带一套 mock 链 ⇒ 双开 = 陷阱 #35/#29） | **绝不 `enable()`**（镜像节点）；串口连续失败达 `max_fail_streak` 就 FATAL 退出；参数默认值取自 `arm_description/config/align.yaml` |
| [arm_moveit_config](src/arm_moveit_config) | — | MoveIt 配置（纯数据）：`arm.srdf`（一个规划组 arm = base_link→gripper_tcp 链；**本阶段不做夹爪**；碰撞对只关相邻链节）/ `kinematics.yaml`（KDL）/ `ompl_planning.yaml` / **`joint_limits.yaml`（必须显式给 `max_velocity`，否则 TOTP 失败，见 `docs/TESTING.md` §十四）** / `moveit_controllers.yaml`（simple controller manager → `arm_controller`）/ `move_group.launch.py` / `moveit.rviz` | 不写规划器、不碰运动学实现；模型与限位都来自 `arm_description`；**mock 与真机上 plan / plan+execute 都已跑通**（2026-10-05）；**mock 与真机只能开一套**（陷阱 #35、#29） |
| [arm_motion](src/arm_motion) | — | **运动执行件**（ament_cmake，C++ 库）：`MotionClient` —— 给 JTC 发**单点轨迹**的非阻塞客户端（限位护栏 / 非阻塞 / `async_cancel_goal` / 残差验证），2026-10-07 **M9.0** 从 `safe_park_node` 抽出、**行为逐条等价** | 不拥有线程、不 sleep、**自己不打任何日志**（只产出**事件队列**，措辞与严重级由调用方决定 —— 日志措辞是业务的）、不碰协议/串口/寄存器；将来 `ExecuteMotion` action server 也落在本包（见 `docs/MOTION_ARCH.md`） |
| [arm_manage](src/arm_manage) | — | **管理层**（ament_cmake，C++ 节点）：**唯一状态机** = 把「控制器先 inactive → 硬件使能 → 校验 → 控制器 active」（M-3）做对 + 「收臂到折叠位**再**失能」编排成一次调用。两个 Gate：`ControllerGate`（switch/list_controllers + 持有 `arm_motion::MotionClient`）、`HardwareGate`（`/arm/set_enabled`、`/arm/emergency_stop`、订阅 `/arm/hardware_status`）。2026-10-11 **M9.3** 落地；同日 **M9.5 真机分级验收通过** | 对外只有 3 个服务（`/arm_manage/arm_on`、`safe_disarm`、`emergency_stop`）+ `/arm/status`（latched）；**只编排、不自己碰电机**；park 位姿真源是 `config/arm_manage.yaml`（M-10）；⚠️ **没有插件时（mock 链）硬件步骤显式跳过**（`skipped=true`，绝不假装使能成功）—— 这样控制器侧 + MotionClient 流程仍能在 mock 上验收 |
| [arm_application](src/arm_application) | — | **应用层**（ament_cmake，C++）：`safe_park_node`（⚠️ 它自己的 `launch/` **2026-10-11 已删** —— 链路由 `arm_bringup/launch/arm_system.launch.py` 起、收臂改由管理层的 `safe_disarm` 承担）—— 收臂到**折叠位**（模型坐标；参数 `vlim`/`tol_rad`/`timeout_sec`/`state_wait_sec`，`park_pose` 走 `ros2 run` 覆盖）。走 ros2_control 的**正常轨迹通路**：本节点 → `/arm_controller/follow_joint_trajectory`(JTC) → position 接口 → 插件 | **不碰协议/串口/电机**；收完臂臂**仍使能**（硬件停在 ACTIVE、插件继续发保持帧）——真正失能靠 `ros2_control_node` 正常退出时的 `on_deactivate()`。**✅ 真机已验（2026-10-07：空程 0.0015 rad；大行程 0.68 rad 双向残差 0.025~0.031 rad；POS_VEL 与 MIT+重力前馈两条都跑过，守卫 0 次。见陷阱 #57/#58 与 `TESTING.md` §18.9）** |
| [motor_driver_hardware](src/motor_driver_hardware) | — | **C++ 侧**（`dm_hardware` 库 + `dm_system_interface` 插件）：`dm_frames`（协议）+ `dm_serial`（termios 非阻塞串口 + `SerialIo` 接口）+ `dm_bus`（收发/缓存/`sync_states`/寄存器 I/O）+ `dm_joint`（换算/软限位/只走 POS_VEL 的关节）+ **`DmSystemInterface`（ros2_control 插件：参数化、只读模式、锁定保持目标、ERR 检查 + **M9.2 运行期使能**）** | `dm_hardware` **不依赖 rclcpp**（只有插件那层依赖）；`dm_joint` **只做 mode 2**；默认 `enable_on_activate=false`（**只读**，不发控制帧，**但仍然逐台写 `0x0A`** —— 模式是 RAM，不写后面使能会用错模式的帧；那不是控制帧）；**M9.2 起自己提供运行期使能**：服务 `/arm/set_enabled`、`/arm/emergency_stop` + 状态话题 `/arm/hardware_status`（10 Hz latched，**因为状态接口只有控制器读得到**）；与 Python 那三份实现靠 `test/` 的**逐字节/逐数值对拍**保持一致 |

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
`write()`（只读模式**一个字节都不发**；`gravity_ff=false` ⇒ `write_pos_vel()`
（命令是 NaN 就发陷阱 #38 的锁定保持帧）；`gravity_ff=true` ⇒ `write_gravity_ff()` 只做 4 次调用：
`refresh_gravity_torque()` → `check_guard()` → `update_integrators()` → `send_mit_frames()`，
**每个函数只管一件事**（重构前是 163 行的一个函数；带**残差守卫**，见陷阱 #42））。
⚠️ 收臂**不在驱动层**：驱动层只做电机 I/O；`safe_park` 是 `arm_application/safe_park_node`（见 §2）。

**未实现（别以为有）**：`arm_msgs` 之上的节点 · **运动接口层 `arm_motion` 的 action server / IK / 规划 / 命名目标**（⚠️ 其中**执行件 `MotionClient` 已落地**（M9.0，8 个 gtest 全过 + 端到端行为等价已验证）；分层/接口契约/异常矩阵见 [docs/MOTION_ARCH.md](docs/MOTION_ARCH.md)，决策 D-M1…D-M8）· ~~M9.5 真机分级验收~~ ✅ **已通过（2026-10-11）**：只读 → 使能（**零位移 0.00000 rad**）→ 起控制器（**M-3 顺序成立**）→ 小动作（**往返残差 0.00076 rad**）→ `safe_disarm`（已在折叠位 ⇒ **自动跳过收臂**；失能后 ERR 全 0）→ `emergency_stop` 幂等。逐条见 `docs/TESTING.md` §十九 · 夹爪 · 速度模式(3) · **电压监控**（本层读不到 `0x3C`）·
**C++ 侧的力位混控路径**（POS_VEL 与 MIT 两条都做了，够重力前馈用）·
**摩擦前馈 / 宿主侧积分 / 逐关节 kp 标定**（见 §5 "重力补偿"那段末尾的"还差什么"）·
**`motor_driver`（Python 包）里仍然没有任何测试文件**（策略；`test/` 只存在于 C++ 包：`motor_driver_hardware/test/` 与 `arm_motion/test/`，见 §7）。
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
| **模型坐标下的关节限位**（应用层护栏 / URDF / MoveIt 都该用这一套） | **算出来的，不是手写的**：`joint.yaml` 的 `position_min/max` + `align.yaml` 的 `sign/zero_shift` ⇒ URDF 的 `<limit>`（xacro 里是**硬编码字面量**）与构建期生成的 `joint_limits.hpp`。**生成器逐条对拍两者**：改了真源没同步另一份就构建失败 |
| **MIT 保持增益 + 摩擦前馈** | `src/arm_description/config/mit_gains.yaml`（2026-10-05 真机定：**kp j1~j3 25.0 / j4~j6 15.0、kd 0.8、ki 0.3；friction_c 0.378/0.406/0.525/0.112/0.082/0.092（= 恒速实测 ×0.7）、friction_v_eps 0.02**；不塞进 `joint.yaml` 是因为 `arm_config.py` 严格解析、多一个键就 TypeError） |
| **这台实机的质量 / 质心（重力模型）** | `src/arm_description/config/gravity_identified.yaml`（2026-10-05 辨识，RMS 1.323→0.418；**上游 CAD 的惯量不可信**，见陷阱 #43） |
| 设计意图（电机驱动层） | `docs/DESIGN.md`（2026-10-07 精简为"设计意图与不变量"；2026-10-11 再去重一轮。⚠️ **节号只能删内容不能改编号**） |
| 运动 / 应用 / 规划层设计 | `docs/MOTION_ARCH.md`（**已设计未实现**，决策 D-M1…D-M8） |

## 5. 关键事实表

> **这里只放"不变量"**。实测数值与历史过程见 `docs/TESTING.md`（那是实测真源），
> 2026-10-01/03/05 那几轮的原始数据在它的 §一~§十八 里。

**CAN ID 偏移**（`JOINT` 侧）：MIT = `id` · 位置速度 = `0x100+id` · 速度 = `0x200+id`（未实现）· 力位混控 = `0x300+id` · 使能/失能 = `id`（数据 `FF*7+FC/FD`）· 刷新 = `0x7FF`（广播，数据带目标 id）

**反馈帧**：适配器→主机 16 字节（`[0]=0xAA`、`[1]=0x11`、`[15]=0x55`），数据段 8 字节 = `D[0]=ID|ERR<<4`、`D[1:3]=POS16`、`D[3:5]=VEL12`、`D[5:7]=T12`、`D[6]=T_MOS`、`D[7]=T_Rotor`（后两个**就是摄氏度**，不换算）

**模式（`0x0A`）**：1 MIT / 2 位置速度 / 3 速度 / 4 力位混控。`0x0A` 是 **RAM**，掉电复位；本层**读不到**它 ⇒ `mode` 永远是**声明**（陷阱 #8）

**ERR**：`0` 失能 / `1` 使能 / `8` 超压 / `9` 欠压 / `A` 过流 / `B` MOS 过温 / `C` 线圈过温 / **`D` 通讯丢失（锁存，只能断电清）** / `E` 过载

**`limit = (PMAX, VMAX, TMAX)`**：4340P = **12.5 / 10 / 28**；4310 = **12.5 / 30 / 10**（**只能回读** `0x15/0x16/0x17`）

**看门狗 `0x09` = 10000（500 ms，存 flash）**：实测窗口 **(400, 700] ms**，一触发就**锁存 ERR=13** ⇒ **任何回调里不许有 >100 ms 的阻塞**（陷阱 #24/#25）。发+收 100 Hz 实测 100.0 Hz / 0 超时

**关节表**：j1 4340P id1 · j2 4340P id2 · j3 4340P id3 · j4 4310 id4 · j5 4310 id5 · j6 4310 id6
（`direction`/`offset`/软限位**均已按本项目 2026-10-03 标定**：j1~j5 `direction=−1`、**j6 `direction=+1`**；
`offset` 标一次长期有效（多圈绝对值跨断电保持，Δ≤0.00015 rad）。
**位置真源 = `0x50 p_m`（== 反馈帧的 pos），≠ `0x51 xout`**）

**模型坐标下的限位**（真源 = `joint.yaml` + `align.yaml`；已写进 URDF，也由构建期生成 `joint_limits.hpp`）：
j1 `[+1.392202, +1.626143]` · j2 `[-2.379924, +0.058351]` · j3 `[-1.891796, +0.050752]` ·
j4 `[-0.157540, +0.938132]` · j5 `[-0.970641, +0.913381]` · j6 `[-1.359002, +4.924184]`
⚠️ j2/j3 下界**放宽过**（`position_min` 0.0 → −0.25 / −0.10，让真机自然停放姿态落进限位）⇒ `q_urdf=0` 离 j2 上界只差 **0.058 rad（3.3°）**。应用层护栏 / MoveIt / `park_pose` 校验都用这一套（陷阱 #37/#59）

**三层坐标（M5 起最容易看错的地方）**：

    模型坐标  --sign / zero_shift-->  q_ours  --direction / offset-->  电机 p_m
    (ros2_control 接口)              (dm_joint 的"关节侧"、软限位在这一套)   (电机读数)

- `sign`/`zero_shift` 来自 `arm_description/config/align.yaml`（M1b 实测）；`direction`/`offset` 来自
  `motor_driver/config/joint.yaml`；两者由 **xacro 变成 `<param>`** 传进插件 ⇒ 单一真源不破
- 换算函数只有一份：`dm_joint.hpp` 的 `model_to_ours()` / `ours_to_model()`（别各写一遍）
- **速度 / 力矩是矢量：只乘 `sign`（或 `direction`），不加偏移**

**单位与换算**：位置 `电机侧 = direction × 关节侧 + offset`；**本层没有减速比折算**（电机报的就是输出轴 rad）

**`Joint._clamp_mit_torque`（只在 MIT 生效）**：`torque_max` 来自 yaml（j1~j3 **12.0**、j4~j6 **3.5**）
- 预测式 `tau = kp·(q_des−q) + kd·(dq_des−dq) + tau_ff`（**全用缓存量**，滞后 1~20 ms ⇒ 只是**近似**保证）
- **PD 项自己超 `torque_max` ⇒ 拒发**（钳 `tau_ff` 救不回来）；否则把 `tau_ff` 钳到"总力矩 = ±`torque_max`"；
  **没有缓存状态也拒发**（算不出来就不装作算过了）

**`Joint._limit_i_des`（力位混控）**：`i_des_cap = torque_max / NM_PER_I_DES[型号]`（实测 40 / 22）
⇒ j1~j3 `i_des ≤ 0.300`、j4~j6 `≤ 0.159`。`current` 是**上限**不是需求 ⇒ 钳 + 限流日志
（**`enable()` 的保持帧直发 `1.0`，不经过这里**）

**`DmArm._monitor_step()`（挂在收循环 ⇒ 采样率 = `feedback_hz`）**：
- **力矩只查 POS_VEL**（阈值 j1~j3 = 15.0 / j4~j6 = 5.0，连续 `torque_monitor_count`=10 次才报）。
  三模式分工：MIT 发前钳位、力位混控限幅、**POS_VEL 只能事后监控**
- **温度所有模式都查**：MOS / 线圈各自判 `>=`，`temp_warn=80` / `temp_fault=100`℃；warn 限流键是 **(关节, 传感器)**

**重力前馈（MIT；2026-10-05 整链真机跑通）**：
- **开关**：`gravity_ff:=true` ⇒ 整链进 MIT（写 `0x0A=1`）+ 每帧按**实测姿态**算重力项；`false`（默认）一个字节都不变
- **只有 MIT 能给力矩**：POS_VEL 帧里没有力矩字段，力位混控的 `i_des` 是**上限**也不是前馈
  ⇒ 做重力前馈**必然**要整链换模式
- **模型**：默认 `arm_identified.urdf`（**空载**辨识版；`dyn_model:=arm.urdf` 切名义版；带夹爪用 `_gripper` 那份，
  但那份质量**未辨识**）。辨识 RMS **1.323 → 0.418 N·m**
- **符号链已验证**（三条独立路径）：与 MuJoCo ≤8.9e-15 · 真机带载关节符号全同 · `tau_ff` 与 effort 往返 ≈1:1
- **`effort` 是电流估计、不是力矩传感器**（用途："POS_VEL 托住时读真实保持力矩"）
- **静态精度的杠杆是 `kp`，不是积分**：误差本质是**死区** `|e| ≤ 扰动/kp`；宿主侧积分（100 Hz）破不了它，
  `ki≥1.0` 反而出**极限环**。现取 **kp=25（j1~j3）/ 15（j4~j6）、kd=0.8、ki=0.3**（真源 `mit_gains.yaml`）
- ⚠️ **`kp` 有硬上界** = `torque_max / 期望滞后`（超了**拒发** ⇒ 失能 ⇒ 掉臂）：
  j1~j3(12.0) 可到 ~80，**j4~j6(3.5) 只能到 ~25**（陷阱 #51）
- **库仑摩擦前馈** `τ_ff += friction_c·tanh(v_cmd/friction_v_eps)`：**治运动质量、不治静态精度**；
  **保持（命令 NaN）时一律给 0**（静止时摩擦方向不确定，猜错等于推着走）；符号取自**命令速度**
- ⚠️ **还差**：①逐关节 `kp`/`friction_c`（j1/j3 数据不足）②`kd` 未标定
  ③j2/j3 的**模型偏置** 0.02~0.03 rad ④只有"残差守卫 + `torque_max` 钳位"，**无安全认证层、无碰撞检测**

## 6. 陷阱清单（都是这个工程真踩过的）

**每条只留"一句处置"**（编号**不可改** —— 38 个文件按 `陷阱 #N` 引用；长故事在 `docs/LESSONS.md`、实测在 `docs/TESTING.md`）。

| # | 陷阱 | 处置 |
|---|---|---|
| 1 | **`NaN` 穿过钳位**（`min/max` 与 NaN 比较恒 False） | 三个 `set_*` 各自 `isfinite` 拦并抛；**不失能**（NaN 是软件错，失能反而危险） |
| 2 | **切进位置类模式时电机内部指令被清零** | mode 2/4 下**无缓存位置拒使能**，并使能后立刻补保持帧 |
| 3 | **力位混控 `i_des` 是电流上限、不是力矩前馈** | `enable()` 的保持帧用 1.0；上位机按 `i_des ≤ torque_max/NM_PER_I_DES` 钳。⚠️ **别用手测上限**（手 ~0.5 N·m 就打滑） |
| 4 | **`force_pos_frame` 与 SDK 约定不同**（我们收物理量、函数内放大） | 别照抄 SDK 参数（数值已逐字节对拍） |
| 5 | **两层 `get_state()` 语义不同** | `MotorBus` 返回 `None`（"从没收到过"是正常）；**`Joint` 抛 `RuntimeError`** |
| 6 | **`send_*` 先查 `registered_ids`**（`send_frame` 查不了） | 忘注册 ⇒ `RuntimeError` |
| 7 | **`set_*` 写串口失败只抛、不失能**（用户明确要求） | 只有 `enable()` 有失能兜底；兜底也失败 ⇒ 告警 + **重抛原异常** |
| 8 | **`mode` 是声明不是读数**，错了帧被电机静默丢 | 每个 `set_*` 先查；改完 `0x0A` 必须同步 `Joint.mode` |
| 9 | **`limit` 用错档位：力矩差数倍且不报错** | `arm_config` 校验软限位落在 ±PMAX；`Joint` 构造校验注册冲突 |
| 10 | **配置在包内**（`config/joint.yaml`） | `setup.py` 的 `data_files` **只能相对路径**（colcon 会 assert） |
| 11 | **裸 import 需要包目录在 `sys.path`** | 裸跑 `PYTHONPATH=src/motor_driver/motor_driver`；按包导 `.../src/motor_driver` |
| 12 | `Joint.__init__` 参数顺序（有默认值后不能再有必填） | 签名见 §3 |
| 13 | **寄存器回包与反馈帧共用 `CMD=0x11`** | `poll`/`wait_feedback`/`_reg_io` 一律先过 `is_reg_response()`（否则垃圾状态污染缓存） |
| 14 | **存参数帧手册与 SDK 不一致** | 按**手册**发 `0x01`；**断电验证过，别改成 `0x00`** |
| 15 | **寄存器写数据 float32/uint32 由 RID 决定** | `_INT_RIDS` 与 SDK 逐条一致；**编错不报错、只静默写坏** |
| 16 | **`DmArm` 不替你切模式**（yaml 现在是 `mode: 1`） | 用 POS_VEL：先写 `0x0A=2`，**并把 yaml 的 mode 同步改成 2** |
| 17 | **`DmArm` 批量"统一增益"只适合点动**（4310/4340P 差 5 倍） | 整臂要逐关节给增益 |
| 18 | **两个循环都阻塞 ⇒ 不能同时跑** | 实测 500 Hz 单跑 499.7 Hz；与收循环同跑掉到 **482.3 Hz**（GIL）⇒ 要干净 500 Hz 就降频或分进程 |
| 19 | **`fn` 严格判 `is False`**（没写 return 不会停） | 真踩过：`lambda a: t>DUR` ⇒ 静默只跑 1 圈就"正常退出" |
| 20 | **陈旧回包骗缓存** | 排空用 `flush()` + 多轮刷新到两轮一致；**两个循环必须成对跑**。M9.2 又踩一次：见 LESSONS §十七 |
| 21 | **未标定时"保持当前位置"会变成会动的命令** | 装臂前先标定；POS_VEL 保持帧**故意绕过钳位** ⇒ 使能本身不动 |
| 22 | **MIT 力矩上限是"近似"的**（`q/dq` 来自缓存） | PD 项自己超 ⇒ 拒发；否则钳 `tau_ff`。**别当硬保证** |
| 23 | **监控挂在收循环上** | 两个循环成对跑；**排空直接用 `sync_states()`**（别自己写） |
| 24 | **使能了却不发帧 ⇒ 500 ms 看门狗打掉 ⇒ 锁存 ERR=13** | 要么只使能要控制的关节，要么每圈给**所有已使能**关节发帧 |
| 25 | **已使能关节的发帧空档不能超过 `0x09`（500 ms）** | 实测窗口 (400, 700] ms ⇒ **回调里不许有 >100 ms 阻塞**。⚠️ ROS2 集成头号坑 |
| 26 | **`is_reg_response()` 会把某些反馈帧误判成寄存器回包**（约 1.2% 位置范围） | 判据补 `D[0]≤0x0F 且 D[1]==0x00`；残留 3 个 LSB |
| 27 | **`launch_ros` 的关键字是 `parameters=` 不是 `params=`** | 改完 launch 先跑 `--show-args`（只解析不启动） |
| 28 | **USB-CAN 设备号会变** | 按适配器序列号加 udev 固定成 `/dev/dm_can`；节点已加"连续失败就 FATAL 退出" |
| 29 | **`joint_state_publisher*` 会回发 `/joint_states`** | 镜像前确认 **Publisher count: 1** |
| 30 | **协议两份实现，浮点顺序不一致就差 1 LSB** | `float_to_uint` 顺序不可改；靠 34 个逐字节对拍用例守 |
| 31 | **往 `src/` 拷第三方 CMake 工程会被当"包"** | `src/third_party/` 放空 `COLCON_IGNORE` |
| 32 | **链接 rclcpp 的可执行文件报「找不到 `-lcap`/`-llttng-ust`」** | CMake 里 `link_directories($ENV{CONDA_PREFIX}/lib)`；**运行时**同源问题见 §7 的 `LD_LIBRARY_PATH` |
| 33 | **`on_init(const HardwareInfo&)` 在 jazzy 已 deprecated** | 用 `HardwareComponentInterfaceParams` |
| 34 | **插件必须 SHARED**（静态 `.a` pluginlib 加载不了，单测看不出来） | target 用 `SHARED`；`dm_hardware` 要 PIC |
| 35 | **CM 会从共享 `/robot_description` 订到别人的 URDF**（曾把真机使能 ⇒ ERR=13 锁存） | **任何时候只开一套**（mock/真机互斥） |
| 36 | **"一次采样就要求全部就位"会偶发失败** | 重试（~8 s）；只读容忍缺席；ERR 只报一次 |
| 37 | **MoveIt 不钳位起始状态，越界直接拒绝规划** | 规划前先比对实测姿态 vs URDF 限位 |
| 38 | **"保持"曾是每圈重读 ⇒ 无回复力，重力把关节压走** | 已改成**锁定使能那一刻的位置**；回归用例 `HoldTargetIsLatchedNotReread` |
| 39 | **失能后臂会自由塌回折叠位**（张开姿态不是稳定点） | ①要停在工作姿态必须保持使能；②失能前先读姿态；③别把失能当支撑 |
| 40 | **JTC 的 `goal_time=0` ⇒ 永不 ABORT，只无限挂住** | POS_VEL 只声索 position；MIT 必须给 `stopped_velocity_tolerance` + `goal_time`。见 LESSONS §七 |
| 41 | **MIT 的 `enable()` 是零增益帧 ⇒ 使能到首帧之间臂真的在掉** | 立刻补一帧**真实** MIT 保持帧；别放宽阈值糊过去 |
| 42 | **残差守卫必须按"保持 / 跑轨迹"分开判 + 宽限防抖** | 保持：位置>0.1 rad 或速度>0.5 rad/s（连续 0.2 s、宽限 0.5 s）；跑轨迹：只查跟踪误差>0.3 rad |
| 43 | **上游 CAD 惯量与本机不符、且非均匀地错** | 必须辨识（RMS 1.323→0.418）：`gravity_identified.yaml` + `apply_identified_inertia.py` |
| 44 | **固定关节的子连杆会被 pinocchio 并进父连杆** | 写回时**把固定关节子连杆惯量清零**；排查手法：手动设参数再比 |
| 45 | **辨识/静态测量必须等积分收敛 + 取中位数** | "停 4 s + 3 次取中位数"（RMS 0.515→0.418）；判据：两关节**同时**掉 = 瞬态 |
| 46 | **`effort` 是电流估计、不是力矩传感器** | 用已知 `tau_ff` 标定，往返 ≈1:1 |
| 47 | **"失能后塌回去的姿态"常靠在机械硬限位上** ⇒ 在那里测重力无意义 | 测重力要换到**真带载**姿态（张开位） |
| 48 | **构建期 xacro 不能用 `$(find <本包>)`** | CMake 里用源码路径覆盖配置并加进 `DEPENDS` |
| 49 | **加了 pinocchio 后构建必须走 `pixi run colcon`** | 陷阱 #32 的兜底只在 `CONDA_PREFIX` 存在时生效 |
| 50 | **宿主侧积分破不了摩擦死区，大 ki 反而极限环** | 静态精度的杠杆是 **`kp`**（固件按 10+ kHz 执行）。见 LESSONS §十四 |
| 51 | **`kp` 上界 = `torque_max / 期望滞后`；超了就拒发 = 掉臂** | j1~j3(12.0) 可到 ~80；**j4~j6(3.5) 只能到 ~25** |
| 52 | **"保持当前位置"的帧也被软限位钳位 ⇒ 凭空造 PD 项；kp 大时激活 FATAL** | 加 `prepare_frame_raw()` + `bypass_soft_limits`；回归用例 `ActivationHoldFrameBypassesSoftLimits` |
| 53 | **单测证明不了物理**（写错的假设会被原样复制） | 符号/方向/量纲类假设**必须找独立物理量对照**。见 LESSONS §十六 |
| 54 | **`clamp()` 吃关节侧**；拿电机侧的值调它会钳到另一头 | 先钳位、后换算 |
| 55 | **业务流程不要塞进硬件插件** | `safe_park` 已移到应用层；澄清：停控制器**不会**让硬件 `on_deactivate()` |
| 56 | **`arm_identified.urdf` 的 `link6` 已是空载量**（再"减负载"会把 j2/j4 前馈弄错 0.8 N·m） | 加夹爪用 `--gripper` 那份；真装后要重辨 |
| 57 | **在 executor 回调里阻塞等数据 ⇒ 订阅回调永远进不来** | 整条流程必须**非阻塞状态机**。⚠️ 别信 `get_publisher_count()` 的陈旧读数 |
| 58 | **QoS 不匹配就一帧都收不到、且不报错** | `jsb` 是 RELIABLE + TRANSIENT_LOCAL；排查用 `ros2 topic info --verbose` |
| 59 | **JTC 不校验关节限位** ⇒ 越界目标顶在硬限位上堵转 | **发目标前自己查限位**：构建期 `joint_limits.hpp` + 启动即校验 `park_pose` |
| 60 | **放弃 goal 时只退出、不取消 ⇒ JTC 还挂着，臂会自己走完** | 加 `Cancelling` + `async_cancel_goal`；凡"超时/放弃"路径都要**显式取消** |
| 61 | **`ros2 service list` 会列出"只有客户端"的名字** | 用 `ros2 service info X` 看 **Services count**：**0 = 没有服务端**（只是某个节点的 client 端点）。2026-10-11 我据此误判"mock 链里有插件服务"、绕了一大圈 |
| 62 | **`pkill -f <pattern>` 会匹配到自己所在 shell 的命令行**（命令文本含该 pattern 时）⇒ **自杀** | 表现是那条命令**自己被杀**、后续步骤全没跑（假插件因此根本没起来）。先 `pgrep` 确认、或把 pattern 收紧（如 `pkill -f 'python3 .*xxx'`） |
| 63 | **状态机等异步结果必须自带超时** —— 对面服务"在但不答"时，**服务调用方的超时救不了 FSM** | 调用方最多等 `timeout+10 s` 就报错，但 FSM 会**永久停在过渡态**、之后所有操作被"忙"拒掉。`arm_manage` 已加 **3 s/步**（`kAsyncWaitTicksMax`）并如实报"没回 set_enabled（3 s）" |

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
- **插件级测试**（`test/test_dm_system_interface.cpp`，**21** 个用例）：假串口里再塞一台**模拟电机** —— 收到 POS_VEL 就把
  位置跟过去、收到刷新就回状态、收到使能帧就置 ERR=1、寄存器帧回显 RID、**收到 MIT 帧只在 `kp>0` 时才跟位置**
  （零增益帧真机出力恒 0，假电机无脑跟随会把保持目标带偏）。覆盖："只读模式一个控制帧都不发"（含 `gravity_ff=true`）、
  "激活时写 `0x0A=2` + 使能 + **锁定保持目标**"、"命令 NaN 继续保持"、"模型坐标 0 ⇒ 电机侧 1.785878"、
  "故障报 ERROR"、**`gravity_ff=true` ⇒ 写 `0x0A=1`、无 POS_VEL 帧、MIT 帧的 `t_ff == direction × 重力项`**、
  **scale 按比例作用且 >1 被拒**、**守卫的宽限/防抖/位置即时判**、**宿住积分爬升/钳位/复位**、
  **保持帧绕过软限位**、**库仑摩擦前馈**；
  **M9.2 运行期使能**（7 个新用例）：状态机逐台写 `0x0A` **期间一个控制帧都不发** + 顺序断言（最后一个寄存器写早于第一个使能帧）、
  **全臂非失能 ⇒ 拒绝使能**（含 ERR=13 的提示）、失能只发失能帧且**不碰寄存器**、失能后 `read()` 只发刷新帧（M-5）、
  幂等不发帧、急停两路（本来就失能 / **update 停了 ⇒ 如实报失败**）、**注入真节点**后服务与状态消息字段都填对。
- **重力模型对拍**（`test/test_dm_gravity.cpp`，3 个用例）：C++ 的 `GravityModel` 与**已跑通的 MuJoCo**
  逐点对拍（4 位姿 × 6 关节，≤1e-9）+ sign 逐关节生效 + 参数/用法错误。
  ⚠️ 它读的是**构建时生成**的 `arm_description/arm_dynamics.urdf`，所以要先构建 `arm_description`。
- ⚠️ **跑我们自己编的可执行文件（`safe_park_node`、`arm_manage_node`、gtest）都要带 `LD_LIBRARY_PATH=<pixi env>/lib`**：CMake 给它们设的 RPATH 里没有 pixi 环境 ⇒ 直接 `ros2 run` 会报 `librclcpp.so: cannot open shared object file`（`ros2 launch` 起的 `ros2_control_node` 不受影响 —— 那个可执行文件来自 pixi 环境本身）。根治同样是 `pixi.toml` 的 `[activation.env]`（待用户定）。
- **`arm_manage` 的验收（mock 链 + 假插件，M9.3 用过的手法）**：起 `mock_control.launch.py`（控制器都在、但**没有我们的插件服务**）→ 起 `arm_manage_node` → 再起一个**假插件**（两个 srv + `/arm/hardware_status`），在它的 `set_enabled` 处理函数里**记下那一刻 `arm_controller` 是否 active** ⇒ 这就是 **M-3 顺序不变量**的判据（实测：enable/disable 到达时 `ctl_active=False`）。同时断言 `/arm/status` 的迁移序列（`READONLY → ARMING → ARMED_CONTROLLER → DISARMING → READONLY`，**全绿流程里不该出现 `FAULT`**）。
- ⚠️ **跑这两个（含 pinocchio 的）测试要带 `LD_LIBRARY_PATH=<pixi env>/lib`**：干净重建后直接跑
  `./build/motor_driver_hardware/test_dm_gravity` 会报 `libpinocchio_parsers.so.4.1.0: cannot open shared
  object file`（pixi 激活只设 `LIBRARY_PATH`、**不设 `LD_LIBRARY_PATH`**，同陷阱 #49）。最省事是
  `export LD_LIBRARY_PATH=$PWD/.pixi/envs/default/lib:$LD_LIBRARY_PATH` 再跑。
- **`arm_motion` 的 gtest**（`src/arm_motion/test/test_motion_client.cpp`，**8** 个用例）：进程内**假 action server**
  + 假 `/joint_states`（QoS 必须 RELIABLE + TRANSIENT_LOCAL），覆盖：越界/名字错 ⇒ 当场拒绝（一帧不发）、
  "全 0 目标"也越界（j1 限位是 `[1.392202, 1.626143]`）、正常到达残差≈0、JTC 报成功但没动 ⇒ 残差超差、
  **执行超时 ⇒ 真的取消并等到确认**、没有 action server / 没有 `/joint_states` ⇒ 超时报错。
  ⚠️ 跑它要**两个**环境变量：`LD_LIBRARY_PATH`（同上）**和可写的 `ROS_LOG_DIR`**
  （沙箱/只读 `$HOME` 下 rclcpp 建日志文件会抛 `spdlog_ex ... Read-only file system`）：
  ```bash
  export LD_LIBRARY_PATH=$PWD/.pixi/envs/default/lib:$LD_LIBRARY_PATH
  export ROS_LOG_DIR=$PWD/build/ros_log && mkdir -p "$ROS_LOG_DIR"
  pixi run colcon test --packages-select arm_motion
  ```
- **M9.4 总 launch 的验收（mock，一条命令起全栈）**：`ros2 launch arm_bringup arm_system.launch.py use_rviz:=false`
  ⇒ 断言 `arm_controller` 是 **inactive（装载未激活）**、jsb `active`、`/arm_manage/*` 三个服务在、`/arm/status` = `READONLY`；
  再 `ros2 service call /arm_manage/arm_on arm_msgs/srv/ArmOn "{with_controller: true}"` ⇒ `ARMED_CONTROLLER` 且控制器转 `active`；
  然后 `safe_disarm`（残差 0.000000、attempts=1）→ `emergency_stop`。**2026-10-11 全部实测通过**。
  ⚠️ 回归 FSM 超时（陷阱 #63）的手法：起一个**只注册服务、callback 睡 30 s** 的假插件 ⇒ `arm_on` 应在 **~3 s** 失败并报
  "插件没回 set_enabled（3 s）"。
- **`safe_park_node` 的端到端行为等价**（M9.0 抽 `MotionClient` 时用的手法，不落盘）：进程内 Python 起
  **假 JTC action server + 假 `/joint_states`**，用 `subprocess` 跑真实可执行文件，断言**轨迹逐字段**
  （关节名 / 1 个点 / 位置=折叠位 / 末端速度 0 / `duration == max(1, travel/vlim)`）、业务日志逐条、退出码 0。
  这比"只看编译过"强得多，且不新增文件。

- ⚠️ **全量 `colcon test` 还剩 2 处**历史**失败（2026-10-11 定性；用户决定**先不改**）**：
  `arm_application` 的 `uncrustify`（5 处格式分歧）+ `motor_driver` 的 `pytest`
  （`pluggy ... Plugin 'launch_testing' for hook 'pytest_pycollect_makemodule'`，**插件环境错**）。
  - 原来还有 `flake8`(36) / `pep257`(3)：它们检查的正是 `arm_application/launch/safe_park.launch.py`，
    **该文件 2026-10-11 随 M9.4 删除 ⇒ 那 39 个失败自动消失**（注意：**不是「修好了」**）。
    ⚠️ 删文件后 `colcon test-result` 仍会把**陈旧的 xunit 文件**算进去 ⇒ 先
    `rm -rf build/<pkg>/test_results` 再测，否则看到的是缓存（我踩过一次）。
  - `uncrustify` 那条**不是新引入的**（A/B：HEAD 版本 15 处分歧 → 现在 5 处）；`pytest` 与代码无关。
  - ⇒ **看 `colcon test` 红之前先看是哪一条**：`arm_motion` / `motor_driver_hardware` / `arm_msgs` 都干净。

**真机上电顺序**（每一步都要先只读）：`bus.poll()` 看状态 → `poll()` 拿到位置 → `enable()` → 小增益 `set_mit(kp≈1~5, q=当前位置)` → 确认方向 → 加大 → `disable()` → 关电源。

## 8. 文档真源与过期清单

| 文档 | 状态 |
|---|---|
| `readme.md` | ✅ **2026-09-30 重写**（旧版描述了大量已移出工作区的文件） |
| 本文 `AGENTS.md` | ✅ 与代码同步（改代码请回来改） |
| `docs/TESTING.md`、`docs/LESSONS.md` | ✅ 有效真源（实测值 / 踩坑） |
| `docs/DESIGN.md` | ✅ 电机驱动层的**设计意图与不变量**（2026-10-07：873→272 行；2026-10-11：再去重一轮 29.3→25.9 KB）。⚠️ 节号被代码引用（~25 处），**只能删内容不能改编号**（§0.1 是编号索引表） |
| `docs/MOTION_ARCH.md` | 🆕 **运动接口层 `arm_motion` 的架构设计**：分层 / `ExecuteMotion` 接口契约 / 应用层 / 规划层 / 视觉契约。⚠️ **未实现，只是设计**（决策编号 D-M1…D-M8） |
| `docs/reading_guide.md`、`docs/architecture_notes.md` | 外部参考（reBot / PyArmX）导读，与当前代码无关 |

**已移出工作区、但 git 历史里可取回**（`git show <commit>:<path>`）：旧版 `dm_registers.py`（605 行）、
旧版 `arm_config.py`（674 行）、`tools/*.py`、`src/mujoco_pkg/`、`src/rebotarm_msgs/`、`src/fake_driver_pkg/`、
`ARCHITECTURE.md`、`CURRENT_STATE.md`、旧 `config/rebotarm_b601_mixed.yaml`。
⚠️ 现在的 `arm_config.py`（200 行）**覆盖**了历史同名那份 674 行的；`dm_registers.py`、`arm.py` 是新写的。

## 9. 接手后的第一件事

1. 读 [readme.md](readme.md) → 本文 → `docs/TESTING.md`（实测值）→ `docs/DESIGN.md`（电机驱动层的设计意图与不变量）
   → `docs/MOTION_ARCH.md`（**要动运动/应用层时先读它**：接口契约、异常矩阵、规划层接入）。
2. **问用户三件事**：这轮要做哪一层（寄存器工具 / 整臂 / ROS2 / 夹爪）？是否允许新增文件/函数？改动范围到哪为止？
3. 每处改动：**先读文件 → 说明改动清单 → 等批准 → 改 → 验证（贴证据）→ 自审**（错误处理 / 未定义行为 / 冗余 / 安全 / 真机影响）。
4. 自审 checklist（用户会问）：
   - 错误处理完整吗？（哪个异常该失能、哪个不该）
   - 有没有未定义行为？（NaN、越界、空值、None 语义）
   - 有没有冗余/死代码？（未使用的 import、重复的真相、未使用的常量）
   - 安全边界有没有被绕过？（限位、模式声明、注册准入、失能时机）
   - 真机上会怎样？（带电离场、重力负载、朝零位冲）
