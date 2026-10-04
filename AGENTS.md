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
当前做到**驱动层**（协议 / 总线 / 单关节 / 整臂 / 寄存器工具 + 6 轴标定），**没有 ROS2 上层**（代码**不 import rclpy**）。

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

**未实现（别以为有）**：`ros2_control` 硬件接口插件 · MoveIt 配置（URDF/SRDF/限位）· `arm_msgs` 之上的节点 ·
夹爪 · 速度模式(3) · 重力补偿 · 电压监控（本层读不到 `0x3C`）· **任何测试文件**。

## 4. 单一真源表（改之前想清楚该改哪个）

| 事实 | 唯一真源 |
|---|---|
| 协议字节布局 / 帧构造 / 反馈解码 | `dm_frames.py` |
| 模式编码与名字 | `dm_modes.py`（`MODE_NAMES`） |
| 故障码含义 / `ERR_OK=(0,1)` | `dm_frames.ERR_DECODE` / `ERR_OK` |
| 关节限位、方向、零位、模式声明 | `src/motor_driver/config/joint.yaml`（**本项目自己标定**；源头 URDF 已移出工作区） |
| `limit` 档位数值 | **只能回读** `0x15/0x16/0x17`；实测记录在 `docs/TESTING.md` §2.5/§2.6 |
| **力位混控 `i_des` ↔ 扭矩的换算** | `arm_config.NM_PER_I_DES`（实测值，**别按峰值比例外推**：4340P 40 / 4310 22，不是 3.2 倍关系） |
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

**2026-10-01 真机只读实测**（总线上只有一台：id=**6** 的 **4310**）：`Gr=10`、`PMAX/VMAX/TMAX=12.5/30/10` ✓、`CTRL_MODE=1(MIT)`、`ESC_ID=6`、**`MST_ID=0`**、`0x1F Data=4.0`、**`0x09 TIMEOUT=0`（看门狗关闭）**、`VBus=24.15 V`、`Tpcb=28.9 ℃`、`Tmt=26.1 ℃`；
PID 现为 `KP_ASR=0.00372 / KI_ASR=0.002 / KP_APR=54 / KI_APR=0` —— **与 `DESIGN.md §2.1` 的计划值不同**（4310 计划 0.0008/0.002/70/1.0），要跑 POS_VEL 得先写。

**2026-10-01 真机点动结果**（同一台 id=6，裸机、轴悬空，目标都是 +0.08 rad）：
- **MIT**（kp=10 / kd=1）→ 实际走到 **+0.0691 rad**，**稳态残差 0.069 rad**（≈ 摩擦/kp，MIT 的固有特性）；反向回位后残差 +0.012。峰值速度 0.579 rad/s。
- **POS_VEL**（vlim=0.5）→ 实际走到 **+0.0816 rad**，**稳态残差 0.0016 rad**（≈0.09°，固件闭环确实近似零残差）；回位后残差 +0.001。
- 切进 POS_VEL 后 `enable()` 的"保持帧"（pos=当前位置, vlim=0.1）**实测 0.6 s 内 dev=0.0000** —— 不朝零位冲 ✓。
- 两模式全程 ERR=1、Tmos 30 ℃、总线 1:1（`ratio≈1.0`）、无误触发。
- **PID 已写入并留在 RAM**：`KP_ASR=0.0008 / KI_ASR=0.002 / KP_APR=60 / KI_APR=1`（**未存 flash** ⇒ 掉电即失，回工厂值）。
- 收尾把 `0x0A` 写回 **1**，与 `config/joint.yaml` 的冷启动声明一致（`0x0A` 本来就是 RAM）。
- 证据快照：`registers/06/20261001-170644_baseline.json`、`..._170902_before_pid.json`、`..._171147_before_posvel.json`

**2026-10-01 真机力位混控（mode 4）实测**（单台 id=6 / 4310，裸机平放、无负载；全程只使能这一台）：
- 做法：`dm_registers` 写 `0x0A=4`（RAM）→ **进程内**把 `joint.mode` 改成 4（`switch_mode()` 指引的那一步，
  **不改仓库 yaml**）→ 手写 100 Hz 发/收节拍循环（含 ERR 与"位移 >0.5 rad"守卫）；收尾 `0x0A` 写回 1、PID 还原工厂值
- `enable()` 的保持帧（`i_des=1.0` + 实测位置）⇒ 漂移 **0.00000 rad** ⇒ **使能本身不动** ✓
- **`i_des` 语义实测（陷阱 #3 结案）**：目标 +0.08 rad、vel=0.5
  - `i_des=0.0` ⇒ 150 圈/1.5 s **完全不动**（末位 +1.7977、残差 +0.0800、|tau| **0.0073**）
  - `i_des=0.2` ⇒ 到位 **+1.8774**、残差 **+0.0003**、|tau| 0.169
  - `i_des=1.0` ⇒ 到位 +1.8774、残差 **+0.0003**、|tau| 0.173 ⇒ **与 0.2 完全一致**
    ⇒ 电流上限只在环路"想要更多电流"时（负载/加速）才起作用：**空载自由轴上 0.2 与 1.0 无差别**
- **PID 对比**：工厂值（0.00372/0.002/54/0）与 DESIGN 计划值（0.0008/0.002/70/1.0）在这台空载上几乎无差别
  （残差 +0.0003 / −0.0005，回位残差稳定在 −0.0004，|tau| 峰值 0.15~0.19）
- 全程 **100.0 Hz、0 超时、ERR 恒 1、温度 34/31 ℃**；收尾 6 台 ERR 全 0
- ⚠️ 结束时 j6 比开始时偏 **+0.0008 rad**（0.046°：稳态残差 −0.0004 + 最后一帧后的自然停靠）
- 证据：`registers/06/20261001-233044_before_mode4.json`、`..._233104_after_mode4.json`
  （另有 `..._233028_before_mode4.json` 是首次尝试崩溃前的快照，当时 `0x0A` 还是 1）
- **`i_des` 封顶的带载验证：摩擦阈值扫描**（同日 深夜，无负载、无手扶；用**电机自身静摩擦**当已知负载）：
  | `i_des` | 0.0 | 0.005 | **0.006** | **0.007** | 0.008 | 0.01 | ≥0.02 |
  |---|---|---|---|---|---|---|---|
  | 位移（目标 ±0.3 rad，方向交替） | 0.0000 | 0.0004 | **0.0023 没动** | **0.5638 动了** | 0.5615 动了 | 0.3002 动了 | 动了 |
  | 中位 /\|tau\|/ | 0.0024 | 0.1050 | 0.1099 | 0.1441 | 0.1441 | 0.1099 | 0.11~0.12 |
  ⇒ **阈值尖锐落在 `i_des ∈ (0.006, 0.007]`**：以下**一点都推不动**，以上立刻走完。
  阈值处的可用扭矩 = 静摩擦 **0.145 N·m**（与独立测得的摩擦值吻合）⇒ **可用扭矩确实被 `i_des` 线性缩放**。
  两条独立路径给出 **k ≈ 18~22 N·m / 每单位 `i_des`**（阈值法 0.145/0.0065≈22；封顶读数法 0.105/0.005≈21、0.110/0.006≈18）。
  ⚠️ 推论：`i_des ≥ ~0.5` 时封顶扭矩已超过 4310 峰值 **12.5 N·m** ⇒ 再往上"限制因素"变成电机/TMAX，不是 `i_des`。
  ⚠️ **方法学教训**：前两轮"用手扭住轴"想测上限**测不出来** —— 手在 ~0.5 N·m 就打滑（每次 0.6 s 滑掉 0.12 rad、误差停在 0.18 rad），
  而 `i_des=0.05` 封顶就有 ~1 N·m ⇒ **手永远先滑**。带载验证要用"摩擦阈值法"或机械硬限位，别用手。
  → 这就是下面 `NM_PER_I_DES` 的来源（取上界：4340P 40、4310 22 N·m/单位）

**2026-10-01 真机整臂实测**（6 台**裸电机平放桌面、无负载、未组装**；`/dev/ttyACM0`；⚠️ 各 ID 与物理位置的对应关系**未确认**，用户说"地址顺序可能不对"）：
- **电机侧看门狗 `0x09`：6 台已统一为 `10000` 计数 = 500 ms，并已存 flash**（2026-10-01）。
  过程：先只动 id1（`set --force --save --yes`：写前自动 dump 基线 → 写 10000 → 读回一致 → 存 flash 收到回包）
  → **断电重上电后 id1 仍是 10000 ⇒ flash 写入路径 + 手册的 `0xAA`/`0x01` 字节都验证通过** → 再把 id2~id6 同样写入
  → **第二次断电重上电复核：6 台（含 4310 的 id4~id6）全部仍是 10000（500 ms）⇒ 持久性彻底闭环**。
  同两次复核里 `0x0A` 都回到 **1（MIT）**（RAM，掉电复位，与 yaml 声明一致）。
  历史值：改动前 id1=15000（750ms）、id2~id6=0 —— 那个不一致触发过 ERR=13 锁存（见陷阱 #24）。
- **看门狗实测（2026-10-01，只使能 id6，零增益、无负载）**：每 **400 ms** 发一帧 × 6 轮（2.4 s）
  ⇒ ERR 始终 1（**正常喂狗不误触发**）；改成每 **700 ms** 一帧 ⇒ **第 2 轮 ERR=13、使能=False**
  ⇒ **电机自己把输出关了**（fail-safe）；再静默 1 s 仍为 13 ⇒ **锁存，只能断电清**。
  实测超时窗口 **(400 ms, 700 ms]**，与 `0x09=10000`（500 ms）一致。
- **只读**：6/6 应答、ERR 全 0、`check_health()` 通过、`unknown_ids=[]`（总线上无陌生设备）
- **使能/失能**：`enable_all()` 1.8 ms → 6/6 ERR=1；`disable_all()` → 6/6 ERR=0
- **100Hz 发(6 帧/圈) + 100Hz 收，20 秒**：发 2001 圈 / 收 2002 圈，**都是 100.0 Hz、0 次超时**，ratio 1.00，**位置漂移 0.00000 rad（6 台全部）**，温度 28~32 ℃
- **500Hz 发单跑 3 秒**：**499.7 Hz**、1 次超时（write_avg 0.107 ms / p99 0.236 ms）⇒ DESIGN 的 500 Hz 目标本身可达
- **500Hz 发 + 100Hz 收同跑 3 秒**：**482.3 Hz、35 次超时（2.4%）**；收侧排空 **8681/8682** 帧、`pending_bytes` 全程 0 ⇒ **收侧吃得下 6 倍回包**，代价是两个 Python 线程的 GIL 争用
- 零增益使能全程**零位移**；收尾 `disable_all()` + 排空后 ERR=0 且位置与开始时逐位一致
- ⚠️ 未做：方向/零位标定（`offset` 全 0.0）、任何带目标的动作测试

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
- **过温路验证（2026-10-01）**：⚠️ 裸机**没法安全升到 80/100 ℃**（升温要持续电流 ⇒ 要负载，而手在 ~0.5 N·m 就打滑），
  所以改用**阈值注入**：把 `temp_warn` / `temp_fault` 临时降到现场温度（31~36 ℃）以下，让**真实读数**去走**真实代码路径**。
  - 真机 `temp_warn=25`：连调 3 次 → **12 条** warn（6 台 × 2 传感器各一条，之后每个键都被限流）✓ 不抛
  - 真机 `temp_fault=30`：**一次抛出 12 条**（6 台 × MOS/线圈，全部收集齐）✓
  - 真机循环路：只使能 j6 → `run_feedback_loop(100Hz)` ⇒ **第 1 圈 0.7 ms 就抛**，异常路径自动急停
    ⇒ 收尾 **6 台 ERR=0、使能=False、位置未变**、`check_health()` 通过 ✓（这条也顺便证明"收循环异常必先急停"）
  - 离线假总线隔离用例：只 MOS 超 / 只线圈超（**两传感器确实独立**）、恰好等于阈值（`>=` 生效）、
    同一传感器同时满足 warn/fault 时只报 fault、无反馈的关节**跳过而不是编造**、
    **MIT 下力矩大也不报**（只有 POS_VEL 做事后监控）、POS_VEL 力矩防抖第 **10** 次才抛、
    力矩与温度故障**能一起收集**（先让计数爬到 9 再推高温度 ⇒ 一次抛两条）
  - ⚠️ **仍未验证**：电机**物理上**真的到 80/100 ℃ 时的行为 —— 但那与 34 ℃ 走的是同一条 `>=` 比较，
    差别只在数字；**温度读数是活的**这一点有旁证（本次会话从 28~32 ℃ 随负载升到 36/34 ℃）

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
- 真机**预测性验证（2026-10-01，单台 j6/4310）**：用上一步测出的静摩擦阈值 `i_des≈0.0065` 预测，
  4/4 命中 ——
  `torque_max=0.12`（cap 0.00545 < 阈值）⇒ **位移 +0.0038 不动**，且 `|tau| 0.1294 ≈ 上限 0.12`（**独立复现 k≈23.7**）；
  `torque_max=0.20`（cap 0.00909 > 阈值）⇒ 动 −0.3006；真值 3.5 + 请求 0.1 ⇒ 不钳、动；
  真值 3.5 + 请求 1.0 ⇒ 钳到 0.159、动 ✓

**单位与换算**：位置 `电机侧 = direction × 关节侧 + offset`（逆换算 `关节侧 = direction × (电机侧 − offset)`）；
**速度 / 力矩是矢量：只乘 `direction`，不加 `offset`**。本层**没有减速比折算**（电机报的就是输出轴 rad）。

**2026-10-03 真机（6 轴整臂，`/dev/ttyACM0`）**：
- **`MST_ID(0x07)` 已统一为 `0x11~0x16`**（用户改的；`ESC_ID` 仍 1~6）。本层认电机只看反馈数据段 `D[0] & 0x0F`，**不看接收帧里的 CAN ID** ⇒ 改 MST_ID 对 `dm_bus`/`Joint`/`dm_registers` 无影响；`dm_bringup` 的 `--fb-id` 默认正是 `0x10+id`。
- **位置真源 = 寄存器 `0x50 p_m`（float32）== 反馈帧的 `pos`**；**≠ `0x51 xout`**（真机实测每台差一个常数：+0.072 / −0.016 / −0.031 / +0.058 / +0.170 / +0.259 rad；交替读数证明它不随时间漂）⇒ 标定与换算一律按 `p_m`（或反馈帧）。
- **多圈绝对位置跨断电保持**：6 台断电约 8 s 再上电，位置 Δ ≤ **0.00015 rad** ⇒ `offset` 标一次长期有效，不必每次上电回零。
- **本项目的零位/方向/软限位已标定**（`config/joint.yaml`）：offset 逐关节目视零位读数；direction 手推**加通电复核**（j1~j5 = −1、**j6 = +1**）；软限位由用户摆姿态端点换算（j1/j2/j3/j4 只在零位一侧，j5/j6 两侧）。⚠️ 这是**本项目自定义**的零位与方向，与 DESIGN.md 引用的参考项目不同。
- **`kp` 只能把关节推到离目标"负载/kp"的地方**：真机 j1 `kp=15` 差 0.042 rad（≈ 4340P 静摩擦 0.62 ÷ kp）、j3 `kp=30` 差 0.06 rad（重力 1.76 ÷ kp）。**把残差积进 `tau_ff` 前馈**后能贴到 **0.03°~0.6°**（j3/j4/j5/j6 实测），总力矩仍受 `torque_max` 钳位。

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
