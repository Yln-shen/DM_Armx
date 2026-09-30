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
当前只做到**单关节层**，没有整臂、没有 ROS2（代码**不 import rclpy**）。

## 2. 代码地图（工作区**真实存在**的全部代码）

| 文件 | 行数 | 职责 | 边界 / 不变量 |
|---|---|---|---|
| [dm_frames.py](src/DMmotor_driver/DMmotor_driver/dm_frames.py) | 342 | CAN 帧编解码**纯函数**：30B 发送帧模板、8B 数据段、16B 收帧切分、反馈解码 | 不 import 本包、不打屏、不发帧、不判安全、**`_TX_TEMPLATE` 是不可推导的魔数**（改错=适配器不认帧且无报错） |
| [dm_bus.py](src/DMmotor_driver/DMmotor_driver/dm_bus.py) | 459 | 一条总线的**非阻塞**收发：唯一发送出口 `send_frame()`、`poll()` 抽干、`MotorState` 缓存、`registered_ids` 准入 | **不写寄存器、不设零位、不自动使能、不判安全**；`close()` 不负责失能 |
| [dm_modes.py](src/DMmotor_driver/DMmotor_driver/dm_modes.py) | 27 | 模式编码 `MODE_MIT=1/POS_VEL=2/FORCE_POS=4` + `MODE_NAMES` | 零 import；**3 = 速度模式，不是力位混控** |
| [joint.py](src/DMmotor_driver/DMmotor_driver/joint.py) | 217 | 单关节：换算 / 软限位+PMax / NaN 拦 / 三模式发帧 / 使能失能 / 状态 / 故障 | **不拥有控制循环**（`set_*` 只发一帧）；**不写寄存器**（`switch_mode()` 永远抛）；**不 poll** |
| [arm_config.py](src/DMmotor_driver/DMmotor_driver/arm_config.py) | 77 | `config/joint.yaml` → `JointConfig`；**纯参数自洽**校验在 `__post_init__` | 不通信、不碰运行期状态、**不 import 驱动层**（只 import `dm_modes` + yaml） |
| [config/joint.yaml](config/joint.yaml) | — | 6 关节静态参数 | `limit` **必须**是 `0x15/0x16/0x17` 回读值 |
| [setup.py](src/DMmotor_driver/setup.py) | 40 | 装 `share/DMmotor_driver/config/joint.yaml`（用 `Path(__file__).parents[2]` 定位仓库根） | `entry_points` 仍指向**已删除**的 `dm_bringup`/`dm_registers`（已知问题） |

## 3. 已实现 / 未实现（精确到方法）

**`MotorBus`（全实现）**：`__init__(port, *, baud=921600, timeout=0.003)` · `connect` · `open/close`（上下文管理器可用）·
`add_motor(id, limit, motor_type=None)` · `set_limit` · `motors()` · `registered_ids` ·
`send_frame` · `send_pos_vel` · `send_pos_vel_batch` · `send_mit` · `send_force_pos` · `send_enable` · `send_disable` · `send_refresh` ·
`poll` · `wait_feedback` · `send_and_wait` · `poll_and_wait` · `get_state` · `states` · `flush` · `stats`

**`Joint`（全实现）**：
```python
Joint(bus, motor_id, name, direction, limit, offset=0.0,
      position_min=None, position_max=None, mode=MODE_MIT)
enable() / disable()
prepare_frame(pos)                      # NaN 拦 → 软限位 → 换算 → PMax
set_mit(kp, kd, q, dq=0.0, tau=0.0)     # 仅 mode==1
set_pos_vel(pos, vlim)                  # 仅 mode==2；vlim 是幅值、负值抛
set_force_pos(pos, vel, current)        # 仅 mode==4；current ∈ [0,1]
get_state()   -> JointState             # 无反馈 ⇒ 抛 RuntimeError
assert_healthy() -> None                # 只查 ERR（温度归上层）；故障 ⇒ 先失能再抛
switch_mode(mode)                       # 永远抛 NotImplementedError（附五步指引）
joint_to_motor(v) / motor_to_joint(v) / clamp(pos) / clamp_pmax(motor_pos)
```
`JointState` 字段：`name, position, velocity, torque, err, err_text, temp_mos, temp_rotor, enabled, timestamp`

**`arm_config`**：`JointConfig(name, motor_type, slave_id, direction, limit, offset=0.0, position_min=None, position_max=None, mode=MODE_MIT)` · `load_joint_configs(path) -> dict[str, JointConfig]`

**未实现（别以为有）**：整臂层 / 控制循环 / 急停 / 力矩监控 / 看门狗 · 寄存器工具（读 `0x15~0x17`、写 `0x0A`/`0x19~0x1C`/`0x1F`）·
`dm_bringup` CLI · ROS2 节点/话题/URDF/`ros2_control` · 夹爪 · 速度模式(3) · 重力补偿 · 温度策略 · **任何测试文件**

## 4. 单一真源表（改之前想清楚该改哪个）

| 事实 | 唯一真源 |
|---|---|
| 协议字节布局 / 帧构造 / 反馈解码 | `dm_frames.py` |
| 模式编码与名字 | `dm_modes.py`（`MODE_NAMES`） |
| 故障码含义 / `ERR_OK=(0,1)` | `dm_frames.ERR_DECODE` / `ERR_OK` |
| 关节限位、方向、零位、模式声明 | `config/joint.yaml`（源头是 URDF，**已移出工作区**） |
| `limit` 档位数值 | **只能回读** `0x15/0x16/0x17`；实测记录在 `docs/TESTING.md` §2.5/§2.6 |
| 设计意图 | `docs/DESIGN.md`（**部分历史段已过期**） |

## 5. 关键事实表

**CAN ID 偏移**（`JOINT` 侧）：MIT = `id` · 位置速度 = `0x100+id` · 速度 = `0x200+id`（未实现）· 力位混控 = `0x300+id` · 使能/失能 = `id`（数据 `FF*7+FC/FD`）· 刷新 = `0x7FF`（广播，数据带目标 id）

**反馈帧**：适配器→主机 16 字节（`[0]=0xAA`、`[1]=0x11`、`[15]=0x55`），数据段 8 字节 = `D[0]=ID|ERR<<4`、`D[1:3]=POS16`、`D[3:5]=VEL12`、`D[5:7]=T12`、`D[6]=T_MOS`、`D[7]=T_Rotor`

**模式（`0x0A`）**：1 MIT / 2 位置速度 / 3 速度 / 4 力位混控。`0x0A` 是 **RAM**，掉电复位。本层**读不到**它 ⇒ `mode` 永远是**声明**

**ERR**：`0` 失能 / `1` 使能 / `8` 超压 / `9` 欠压 / `A` 过流 / `B` MOS 过温 / `C` 线圈过温 / **`D` 通讯丢失（锁存，只能断电清）** / `E` 过载

**`limit = (PMAX, VMAX, TMAX)`**：4340P = **12.5 / 10 / 28**；4310 = **12.5 / 30 / 10**（实测回读，与 SDK 表一致）

**关节表**：j1 4340P id1 `[-2.8, 2.8]` · j2 4340P id2 `[-3.14, 0]` · j3 4340P id3 `[-3.14, 0]` · j4 4310 id4 `[-1.87, 1.57]` · j5 4310 id5 `[-1.57, 1.57]` · j6 4310 id6 `[-3.14, 3.14]`

**单位与换算**：位置 `电机侧 = direction × 关节侧 + offset`（逆换算 `关节侧 = direction × (电机侧 − offset)`）；
**速度 / 力矩是矢量：只乘 `direction`，不加 `offset`**。本层**没有减速比折算**（电机报的就是输出轴 rad）。

## 6. 陷阱清单（都是这个工程真踩过的）

| # | 陷阱 | 处置 |
|---|---|---|
| 1 | **`NaN` 会穿过钳位**（`min/max` 与 NaN 比较恒 False） | `prepare_frame()` 入口 + 三个 `set_*` 各自 `math.isfinite` 拦；抛 `ValueError` 且**不失能**（NaN 是软件错，失能反而危险） |
| 2 | **切进位置类模式时电机内部指令被清零** | `enable()` 在 mode 2/4 下**没有缓存位置就拒绝使能**，并使能后**立刻补"保持帧"** |
| 3 | **力位混控的 `i_des` 是电流上限、不是力矩前馈** | 给 0 = 一点力都不给（负载下会垂）；`enable()` 的保持帧用 `1.0`（真保持，但**依赖未标定 PID**） |
| 4 | **`force_pos_frame` 与 SDK 约定不同** | 我们收**物理量**（rad/s、0~1 标幺）并在函数内放大；SDK `control_pos_force` 收**已放大**的整数。别把 SDK 参数直接抄过来（数值已逐字节对拍一致） |
| 5 | **两层 `get_state()` 语义不同** | `MotorBus.get_state()` 返回 `None`（"从没收到过"是正常）；**`Joint.get_state()` 抛 `RuntimeError`** |
| 6 | **所有 `send_*` 先查 `registered_ids`**（`_require_registered`）；`send_frame` **查不了**（它不收 `motor_id`，CAN ID 已在帧里） | 忘注册会 `RuntimeError`；`_limit()` 也从 `KeyError` 改成了 `RuntimeError` |
| 7 | **`set_*` 写串口失败只抛、不失能**（用户明确要求） | 只有 `enable()` 有失能兜底；且兜底失能**也失败**时 → stderr 告警 + **重抛原异常** |
| 8 | `mode` 是**声明不是读数**，声明错 → 帧被电机**静默丢掉** | 每个 `set_*` 先 `_require_mode` 式检查；切完 `0x0A` 必须同步改 `Joint.mode`（`switch_mode()` 只给步骤，不会替你改） |
| 9 | **`limit` 用错档位：力矩差数倍且不报错** | `arm_config` 加载时校验软限位换算是否落在 ±PMAX；`Joint` 构造时校验 bus 上注册冲突 |
| 10 | **配置在仓库根** `config/joint.yaml`，不在包内 | `setup.py` 用 `Path(__file__).resolve().parents[2]`；安装后路径 `share/DMmotor_driver/config/joint.yaml` |
| 11 | **裸 import 风格**（`from dm_bus import ...`）需要包目录在 `sys.path` | 运行：`PYTHONPATH=src/DMmotor_driver/DMmotor_driver pixi run python`；`dm_bus` 对 `dm_frames` 有 `sys.path` 兜底 |
| 12 | `Joint.__init__` 参数顺序（有默认值的 `offset` 之后不能再有必填项） | 当前签名：`(bus, motor_id, name, direction, limit, offset=0.0, ...)` |

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
另外两招：
- **真 `MotorBus` 不开串口**也能测注册/冲突/准入（构造 `MotorBus("任意路径")` 不碰串口）。
- **帧级对拍**：把参数喂给厂商 SDK `src/third_party/Python例程/u2can/DM_CAN.py` 的同名函数，比较 8 字节数据段。

**真机上电顺序**（每一步都要先只读）：`bus.poll()` 看状态 → `poll()` 拿到位置 → `enable()` → 小增益 `set_mit(kp≈1~5, q=当前位置)` → 确认方向 → 加大 → `disable()` → 关电源。

## 8. 文档真源与过期清单

| 文档 | 状态 |
|---|---|
| `readme.md` | ✅ **2026-09-30 重写**（旧版描述了大量已移出工作区的文件） |
| 本文 `AGENTS.md` | ✅ 与代码同步（改代码请回来改它） |
| `docs/TESTING.md`、`docs/LESSONS.md` | ✅ 有效真源（实测值 / 踩坑） |
| `docs/DESIGN.md` | ⚠️ 设计意图有效，但 v0.12 与"当日工作区现状"等段落**已过期** |
| `docs/PLAN_joint.md` | ❌ **过期**（上一轮的临时计划，其"不实现 force_pos"等结论已被推翻） |
| `docs/reading_guide.md`、`docs/architecture_notes.md` | 外部参考（reBot / PyArmX）导读，与当前代码无关 |

**已移出工作区、但在 `git HEAD` 里可取回**（`git show HEAD:<path>`）：
`dm_bringup.py`、`dm_registers.py`（旧版 674 行的 `arm_config.py`）、`tools/*.py`（`smoke_*.py`/`scan_bus.py`/`bus_probe.py`）、
`src/mujoco_pkg/`、`src/rebotarm_msgs/`、`src/fake_driver_pkg/`、`ARCHITECTURE.md`、`CURRENT_STATE.md`、旧 `config/rebotarm_b601_mixed.yaml`。
⚠️ 现在这个 `arm_config.py`（77 行）**覆盖**了 git HEAD 里同名的那份 674 行版本。

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
