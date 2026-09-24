# CURRENT_STATE —— 现在是什么（2026-09-23）

> **给下一次开新对话的 agent**：先读这一页，再按需翻 [`docs/DESIGN.md`](docs/DESIGN.md)（设计）/
> [`docs/TESTING.md`](docs/TESTING.md)（实测数据）/ [`docs/LESSONS.md`](docs/LESSONS.md)（踩坑）。
> 本文件只讲**现在是什么**和**下一步做什么**，不讲为什么。

## 1. 项目是什么

达妙 DM 电机驱动的 7 自由度机械臂（6 关节 + 1 夹爪），ROS2 Jazzy，最终目标是**视觉引导抓放**。
蓝本是 Seeed 官方的 `~/reBot_Arm_Mujoco-DM`（硬件 1:1），但**从零自搭**最小 ROS2 版。
环境全在项目内：**所有命令走 `pixi run`**，不动全局环境、不擅自改 `pixi.lock`。
当前阶段：**电机封装层（阶段 1）**。仿真侧已移植并验证；驱动层的协议 / 传输 / 控制 / 配置四块已写完，
但**没有任何 ROS 节点能驱动电机**，也**从未装到臂上**（整臂本体未到货）。

## 2. 已完成

**模块**（`src/DMmotor_driver/DMmotor_driver/`）：
| 文件 | 行数 | 是什么 |
|---|---|---|
| `dm_frames.py` | 320 | 协议原语：帧构造 / 切帧 / 解码。纯函数，无 CLI，不依赖 SDK |
| `dm_bus.py` | 434 | `MotorBus`：非阻塞收发 + 状态缓存（`MotorState`）|
| `joint.py` | 520 | `Joint`：单电机控制类（dir/offset 换算、钳位、出错保护）。**MIT 可用**；`set_pos_vel` / `set_force_pos` / `switch_mode` 是预留桩 |
| `arm_config.py` | 674 | 配置类（`JointConfig` / `GripperConfig` / `SafetyConfig` / `ArmConfig`）+ 校验 |
| `config/rebotarm_b601_mixed.yaml` | 7.2K | 真数据：6 关节 + 夹爪 + 安全阈值 |
| `dm_bringup.py` | 1153 | 单电机上电验证 CLI：`read` / `monitor` / `jog` / `bandwidth` |
| `dm_registers.py` | 605 | 寄存器 `dump` / `verify` / `set` / `restore` |

**实测**（数据全部在 TESTING.md）：
- 单电机 MIT 点动 —— **4310 与 4340P 两种型号都跑通**（裸电机空载、零寄存器写入）
- 帧协议与厂商 SDK **逐字节对拍一致**（MIT 帧 5 组参数，含两端饱和值）
- 5 台电机（`0x01`~`0x05`）逐台巡检：型号 / `CTRL_MODE` / `KP_APR` / `TIMEOUT`
- 映射范围回读（`0x15` / `0x16` / `0x17`）；静摩擦量化（两种型号）
- 链路带宽天花板
- 看门狗 `0x09` 阈值与触发点（正反两面都拿到）；`--save` 跨断电验证（在 `0x01` 上）

## 3. 未完成

**模块**：
- [ ] `arm.py`（`DmArm` 整臂类）—— **没写**，且**不在 DESIGN.md §九 的优先级表里**
- [ ] `RegisterTool` —— **不存在**（`dm_registers.py` 是 CLI，不是可调用的类）
- [ ] `arm_controller.py` / `joint_controller.py`（ROS2 驱动节点）—— **0 行占位**

**实测**：
- [ ] POS_VEL 路径**从未在真机上跑过**（`jog --yes` 一次都没跑）
- [ ] 「使能 + POS_VEL」下的 1:1 应答
- [ ] 7 关节满载下的 1:1 应答（现有数据全是单电机）
- [ ] 单圈编码器会不会绕圈（点动范围太小，没跑到多圈）
- [ ] `joint6`(`0x06`) 与夹爪(`0x07`) —— **没接线**
- [ ] `direction` / `offset` 标定（现在全是默认值，含义是**未标定**）
- [ ] 装到臂上（本体未到货）

## 4. 当前卡在哪

**下一步是写 `arm.py`，但它现在写不下去** —— 运动要靠 POS_VEL，而：
1. `joint.py` 的 `set_pos_vel` 是 `NotImplementedError`
2. 因为现场 5 台电机全是 `CTRL_MODE=1`（MIT）；切 POS_VEL 要**写寄存器** `0x0A = 2`
3. 写寄存器只能走 CLI 形态的 `dm_registers.py`，或走**尚不存在**的 `RegisterTool`
4. `MotorBus` / `Joint` **明确禁止寄存器 I/O**，所以 DESIGN.md §八 的 `connect()` 也还落不了地

**上机前置条件**：
- 🔴 台架上的电机若停在 **ERR=13**，**必须先断一次电**才能继续（见第 5 节）
- 🔴 真机 MIT 循环是本项目**第一次不可逆的物理风险**（撞限位），开工前必须先过 `## 决定点`

## 5. 关键约束

| 约束 | 说明 |
|---|---|
| **写 flash 只有一处** | `dm_registers.py` 的 `save_motor_param`（`--commit --save`）。别处一律不写 |
| **ERR=13 是锁存的** | 只能**断电**清；`enable` / 发帧 / 写 `0x09=0` 都清不掉 |
| **写寄存器 ~150 ms 静默** | 只能在上电 / 标定阶段做，**绝不进控制循环** |
| **`set_zero_position`(0xFE) 全项目从未被调用** | 也不该被调用 |
| **现场的 `CTRL_MODE` 全是 1（MIT）** | 要走 POS_VEL 必须先写 `0x0A = 2` |

（另有 `dm_bringup.py` 的硬边界：不写寄存器、不切控制模式、不设零位；动电机要 `--yes`。）

## 6. 关键数据

| 项 | 值 |
|---|---|
| **型号 / ID** | joint1–3 = **4340P**（`0x01`–`0x03`）；joint4–6 = **4310**（`0x04`–`0x06`）；夹爪 = **4310**（`0x07`）|
| **映射范围**（PMAX/VMAX/TMAX）| 4340P = `12.5 / 10 / 28`；4310 = `12.5 / 30 / 10`。**必须回读**，用错档位力矩差 2.8×/4× 且**不报错** |
| **静摩擦** | 裸 4340P ≈ **0.62 N·m**（0.53~0.72）；裸 4310 ≈ **0.15 N·m** |
| **带宽天花板** | ≈ **3255 控制帧/s**（单适配器 + Python）→ 设计速率取 **200~300 Hz/关节** |
| **看门狗 `0x09` 现状** | `0x01` = **750 ms**（flash，跨断电验证过）；`0x02`–`0x05` = **0**（保护是关的）|
| **串口 / 帧长** | 921600 8N1（CAN 侧 1 Mbps）；TX **30 B** / RX **16 B** |
| **VBus** | 24.27 V |

## 7. 下一步

按 [`docs/DESIGN.md`](docs/DESIGN.md) §九 优先级表：
1. **P0 剩余**：`RegisterTool.dump_pid` / `verify_mapping`（只读，无风险）
2. **P1**：`RegisterTool.apply_pid` / `restore_pid`（带 dry-run）
3. **P1**：电机侧看门狗 `0x09` —— 和真机 MIT 循环**一起做**，不是它的前置
4. **P1**：标定脚本、`JointState` 全字段（含温度）、错误码解码表
5. **P2**：`check_health` / `emergency_disable`、重力补偿、`set_force_pos`
6. 之后才是 `arm.py` / `DmArm` 与 ROS2 驱动节点

**回归测试现状**：`tools/` 下的测试脚本（`smoke_dm_frames.py` / `smoke_dm_bus.py` / `smoke_joint.py` 等）
**已从工作区删除**，而**文档里还在引用它们**；要跑可从 `git show HEAD:tools/<名字>.py` 取回。
