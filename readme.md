# DM_Armx

桌面级 **6 轴机械臂上位机**工程（ROS2 + 达妙电机，从零自搭最小版）。

- **硬件**：6 自由度本体 + 夹爪。关节 1–3 = 达妙 **4340P**，关节 4–6 = **4310**；夹爪（4310）**本阶段不做**。
- **通信**：USB-CAN 适配器（`/dev/ttyACM0`，UART 侧 921600；CAN 侧固定 1 Mbps）。
- **环境**：pixi（`robostack-jazzy`，Python 3.12），见 [pixi.toml](pixi.toml)。
- **终点目标**：真机视觉引导抓放。

## 现在到哪一步了

| 层 | 文件 | 状态 |
|---|---|---|
| 协议帧编解码（纯函数） | [dm_frames.py](src/DMmotor_driver/DMmotor_driver/dm_frames.py) | ✅ MIT / 位置速度 / 力位混控 / 使能失能 / 刷新 / 反馈解码 |
| 总线非阻塞收发 | [dm_bus.py](src/DMmotor_driver/DMmotor_driver/dm_bus.py) | ✅ 唯一发送出口 + 注册准入 + 状态缓存 |
| 控制模式常量 | [dm_modes.py](src/DMmotor_driver/DMmotor_driver/dm_modes.py) | ✅ |
| 单关节逻辑 | [joint.py](src/DMmotor_driver/DMmotor_driver/joint.py) | ✅ 换算 / 限位 / 三模式 / 状态 / 故障 / **力矩上限（MIT 钳 tau_ff、力位钳 i_des）**；**2026-10-01 三种模式都真机跑通**（MIT/POS_VEL 各做零增益抽检 + ±0.08 rad 点动去回；力位混控另测 `i_des` A/B/C） |
| 配置解析 | [arm_config.py](src/DMmotor_driver/DMmotor_driver/arm_config.py) + [config/joint.yaml](config/joint.yaml) | ✅ 6 关节 |
| 寄存器工具 | [dm_registers.py](src/DMmotor_driver/DMmotor_driver/dm_registers.py) | ✅ `list`/`dump`/`verify`/`set`/`restore`；**2026-10-01 真机全路径验证**：读 ✓、写 RAM ✓（4 个 PID + 切 `0x0A`）、**写 flash ✓**（6 台 `0x09` 存 500ms，断电后仍在）；`Reg.per_unit` 单处定义单位换算 |
| 单电机排障脚本 | [dm_bringup.py](src/DMmotor_driver/DMmotor_driver/dm_bringup.py) | ✅ `read`/`monitor`/`jog --mit`/`bandwidth`；**刻意不 import 本包**、只走厂商 SDK（排障时用来分清是封装错还是链路错）。2026-10-01 恢复并真机只读验证 |
| 整臂层 | [arm.py](src/DMmotor_driver/DMmotor_driver/arm.py) | ⚠️ **部分**：13 个公开方法 + 力矩/温度监控 + MIT 力矩钳位；真机 6 台验证（100Hz 双循环 0 超时、500Hz 单跑 499.7Hz、零误报）；**未做**：方向/零位标定、带目标的动作、ROS2 |
| ROS2 集成 | — | ❌ 未做（**当前代码不 import rclpy**） |
| 夹爪 | — | ❌ 本阶段不做 |

> ⚠️ **现在还不能真的驱动整臂**：单关节的 MIT 与 POS_VEL 都已在真机上跑通（见上表），但**没有整臂层、没有控制循环**
> （`set_mit`/`set_pos_vel` 都是"发一帧"，循环要自己写），也没有力位混控与夹爪。

## 一页架构

```
       你 / 将来的 ROS2 节点
              │  关节侧 rad
              ▼
   joint.py    Joint       换算(dir/offset) · 软限位+PMax 钳位 · NaN 拦 · 三种模式 · 故障
              │  电机侧 rad + 电机 id
              ▼
   dm_bus.py   MotorBus    注册准入 · 唯一发送出口 send_frame · poll() 收帧 · 缓存 MotorState
              ▼
   dm_frames.py            30B 适配器帧 / 8B 数据段 / 16B 反馈帧的字节级编解码（纯函数）
              ▼
   USB-CAN 适配器 (/dev/ttyACM0, 921600)
```

三条设计约定：

1. **单一真源**：协议字节只在 `dm_frames.py`；模式编码只在 `dm_modes.py`；换算只在 `Joint` 里做。
2. **收发解耦**：发是 `send_*`（写串口即返回，**不等回包**）；收是 `bus.poll()`（非阻塞抽干，更新缓存）。
   所以 `get_state()` 读的是**缓存** —— 要新鲜数据就得在循环里持续 `poll()`。
3. **本层不写寄存器**：切模式 / 改 PID / 设零位都是寄存器 I/O（一次约 150ms 且期间发不出帧），不在这几个文件里。

## 五分钟上手

```bash
cd ~/DM_Armx
PYTHONPATH=src/DMmotor_driver/DMmotor_driver pixi run python        # 或用系统 python3
```

```python
from dm_bus import MotorBus
from joint import Joint
from arm_config import load_joint_configs

bus = MotorBus("/dev/ttyACM0").open()           # 只开串口，不改电机任何配置
cfg = load_joint_configs("config/joint.yaml")["joint1"]

j = Joint(bus, cfg.slave_id, cfg.name, cfg.direction, cfg.limit,
          offset=cfg.offset, position_min=cfg.position_min,
          position_max=cfg.position_max, mode=cfg.mode)

bus.poll()                     # ① 先收一帧（只读，不使能）
st = j.get_state()             # 没收到过反馈会抛 RuntimeError
print(st.position, st.err, st.temp_mos)

j.enable()                     # ② 使能（位置类模式下没有缓存位置会被拒）
j.set_mit(kp=3.0, kd=0.5, q=st.position)    # ③ q 给"现在在哪"，不是"要去哪"
# ... 控制循环里持续 set_mit / set_pos_vel + bus.poll() + j.assert_healthy() ...
j.disable()                    # ④ 结束前失能
```

## 寄存器工具（调试 / 标定用）

```bash
# 建议先设好路径（裸 import 风格需要包目录在 sys.path）
export PYTHONPATH=src/DMmotor_driver/DMmotor_driver
DM=src/DMmotor_driver/DMmotor_driver/dm_registers.py

python3 $DM list                        # 不接硬件：列出 49 条寄存器与类型
python3 $DM verify --id 1               # 读 Gr/PMAX/VMAX/TMAX/CTRL_MODE，跟型号档位表 + joint.yaml 对拍
python3 $DM dump   --id 1 --tag baseline # 读一批存 JSON（写前的基线，也是 restore 的来源）
python3 $DM set    --id 1 --rid Data --value 4.0            # 默认只写 RAM（掉电丢）
python3 $DM set    --id 1 --rid CTRL_MODE --value 2         # 切位置速度模式（RAM！）
python3 $DM set    --id 1 --rid Data --value 4.0 --save --yes   # 这才写 flash
python3 $DM restore --id 1 --file registers/01/xxxx_baseline.json --yes
```

- **写 flash 前必须先失能**（手册），flash 寿命约 1 万次；`--save` 没有 `--yes` 会拒。
- 改 ID / CAN 波特率 / 看门狗 / 环带宽这类寄存器在**拒写清单**里，要 `--force`；RO 的一律拒。
- `verify` 会顺便检查 `0x1F`（位置类模式的阻尼因子必须非 0）。

## 真机安全守则

| # | 守则 | 为什么 |
|---|---|---|
| 1 | **先只读后出力**：先 `poll()` 看位置 / ERR / 温度，再考虑使能 | 不知道电机当前状态就出力是赌博 |
| 2 | **位置类模式（2/4）使能前必须先 poll 到位置** | 切进位置类模式时电机内部指令被**清零**，使能后没有"保持帧"就**朝零位冲**（`Joint.enable()` 会拒） |
| 3 | **MIT 从小增益起步**（kp 1~5、kd 0.1~0.5），`q` 先给当前位置 | 先确认方向对不对再加大；kp=120 是整臂标定后的值 |
| 4 | **失能 ≠ 停住**：失能只是让电机松掉，带重力负载的关节会掉下来 | 要"停住"得先想好支撑 |
| 5 | **ERR=13（通讯丢失）是锁存的**，只能**断电再上电**清除 | enable / 连发帧 / 写看门狗寄存器都清不掉 |
| 6 | `limit`(PMAX/VMAX/TMAX) **必须用回读值**：4340P = 12.5/10/28，4310 = 12.5/30/10 | 用错档位解出来的**力矩差数倍且不报错** |
| 7 | 结束顺序：**先 `disable()`，再关电源 / 拔线**；一条总线同时只能一个程序占用 | 带电离场是隐患；两进程抢串口 ⇒ 帧交错 |
| 8 | 力位混控的 `i_des` 是**电流上限**，不是力矩前馈：给 0 = 一点力都不给 | 带重力负载时会垂下去，不是"保持" |

## 目录结构（只列真实存在的）

```text
DM_Armx/
├─ readme.md                    本文件：人看的入口
├─ AGENTS.md                    给下一个 AI 接手的精确状态说明
├─ ARMWORK.md                   人机协作工作流（访谈→计划→实现→精修→归档）
├─ pixi.toml / pixi.lock        环境（robostack-jazzy + pyserial + pyyaml）
├─ config/joint.yaml            6 个关节的静态参数（安装到 share/DMmotor_driver/config/）
├─ docs/
│  ├─ DESIGN.md                 设计文档（**最全**，但含已过期的历史段）
│  ├─ TESTING.md                实测数据（映射范围 / 摩擦 / 验证清单）—— 有效真源
│  ├─ LESSONS.md                踩坑记录（SDK 四个坑 + 方法上的坑）—— 有效真源
│  ├─ PLAN_joint.md             ⚠️ 已过期的临时计划（内容已被实现取代，留作历史）
│  └─ reading_guide.md / architecture_notes.md   外部参考（reBot / PyArmX）导读
├─ tools/check_env.sh           环境自检
└─ src/DMmotor_driver/
   ├─ setup.py
   ├─ DM-J4340P-2EC V1.1 .md  /  DM-J4310-2EC.md     两份电机手册（**权威**）
   └─ DMmotor_driver/          dm_frames.py · dm_bus.py · dm_modes.py · joint.py · arm_config.py · dm_registers.py
```

## 接下来做什么

1. **真机验证寄存器工具**：逐台 `verify` 校对 limit；写 `0x0A=2` 与 PID；确认位置速度模式能跑。
2. **整臂层**：6 个 `Joint` 组装 + 发/收双循环（发 500Hz、收 100Hz）+ 急停 / 力矩监控 / 看门狗。
3. **ROS2 集成**：节点 / 话题 / URDF（限位真源）/ `ros2_control`。
4. **夹爪**（4310，`0x07`）与重力补偿。

细节看 [AGENTS.md](AGENTS.md)（精确到方法的状态与不变量）与 [docs/DESIGN.md](docs/DESIGN.md)（设计意图）。
