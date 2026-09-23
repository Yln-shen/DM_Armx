# DM_Armx

从零自搭的「reBot 最小 ROS2 版」桌面级 6 轴夹爪机械臂项目。

硬件为矽递 reBot Arm B601-DM 同款/仿件：6 自由度本体 + 1 夹爪，7 个达妙电机（3×4340P：关节 1-3；4×4310：关节 4-6 + 夹爪），USB-CAN 通信。

> 目标定位：**系统集成 / 项目落地方向**。彻底吃透机械臂工程链路、不深入算法。约 6 个月窗口（2026-09 起），目标终点是**真机视觉引导抓放**跑通。
> 学法：从零自写最小版，只借鉴设计；软件直接做成 **ROS2 最小子集**。

## 复刻决策（访谈结论）

| 参考 | 角色 | 理由 |
| --- | --- | --- |
| `~/reBot_Arm_Mujoco-DM`（Seeed 官方, Apache-2.0） | **主蓝本 / 参考答案** | 硬件 1:1（电机 ID/构型/夹爪一致）。链路 = USB-CAN → motorbridge + reBotArm_control_py → ROS2 驱动 → FK/IK → MuJoCo 数字孪生 → 视觉/抓放，正是“系统集成落地”完整教材。面向 Ubuntu 26.04/Jazzy/Py3.12，本机用 pixi robostack-jazzy 对齐。 |
| `~/PyArmX`（师兄 DBink） | 只“借形” | facade 解耦 + `mock=True` 干跑 + sim/real 双抽象值得抄。但真机层依赖师兄私有 `pydamiao`（Serial、6 轴、无夹爪）、差异化是 VLM/AI，**不宜作复刻对象**。 |

## 目录结构

```text
DM_Armx/
├─ readme.md              本文件：路线 + 检查清单
├─ pixi.toml / pixi.lock  pixi 环境（robostack-jazzy, Python 3.12）
├─ docs/
│  ├─ reading_guide.md    reBot / PyArmX 精读导读（先读它再动工）
│  └─ architecture_notes.md  架构笔记模板 + 模块对照表（边读边填）
├─ tools/
│  ├─ check_env.sh        pixi 环境自检
│  ├─ smoke_sim.py        冒烟测试①：模型加载 + 关节按映射跟动（直接调回调，无需显示器）
│  ├─ smoke_slider_link.py 冒烟测试②：滑块→joint_states→模型，走真 DDS（验 QoS 配对）
│  ├─ smoke_dm_frames.py  冒烟测试③：达妙 CAN 帧构造/切帧/反馈解码（无硬件，与厂商 SDK 对拍）
│  ├─ smoke_dm_bus.py     冒烟测试④：MotorBus 时序（假串口，无硬件）
│  ├─ smoke_joint.py      冒烟测试⑤：Joint 控制类（假串口，无硬件；含"不碰寄存器"的静态检查）
│  ├─ scan_bus.py         扫总线：哪些 ID 在线 + 型号判定（只发查询帧，**换线/加电机后先跑这个**）
│  └─ bus_probe.py        MotorBus 真机只读探针（连通/路由/poll 非阻塞/1:1 记账，不使能）
└─ src/
   ├─ mujoco_pkg/         MuJoCo 仿真包（移植 rebotarm_mujoco，已改名）
   │  ├─ models/          kinematic(0 mesh) / colored / stl / simple
   │  ├─ meshes/          50 个 STL（42M，colored+stl 模型实际引用的并集）
   │  ├─ config/          joint_map.yaml / joint_map_kinematic.yaml
   │  ├─ description/     内联 ReBot_Arm_DM.urdf（17KB，视觉调色用）
   │  └─ launch/          slider_real2sim(滑块+仿真) / real2sim / physics_grasp / ...
   ├─ rebotarm_msgs/      自定义 msg/srv/action（保持原名，import 直接用）
   ├─ fake_driver_pkg/    Fake Driver 虚拟执行器（移植自 rebotarmcontroller）
   ├─ DMmotor_driver/     电机封装层（阶段 1 主线）
   │  ├─ design.md        设计文档（v0.4，决策已定 + 真机实测基线；先读它）
   │  ├─ DM-J4310-2EC.md / DM-J4340P-2EC V1.1 .md   官方手册
   │  └─ DMmotor_driver/dm_bringup.py   单电机上电验证（read/monitor/jog/bandwidth，jog 另有 --mit）
   └─ third_party/Python例程/u2can/DM_CAN.py   达妙官方 SDK（vendored，选它不用 motorbridge）
```

## Pixi 环境速查（已就绪）

环境：`conda-forge` + `robostack-jazzy`，Python 3.12.14，含 `ros-jazzy-ros-core`、`ros-jazzy-moveit-ros-planning`。

```bash
pixi shell          # 进入项目环境
pixi run <cmd>      # 在环境里跑单条命令
ros2 --help         # 验证 ROS2
```

阶段 1 需要补的 Python 电机/仿真库（**先别急着装**，见路线阶段 0/1 再逐项加）：

```bash
pixi add motorbridge mujoco pin numpy pyyaml transforms3d   # 部分来自 pip 侧
pixi add "ros-jazzy-tf-transformations" 2>/dev/null          # 视发行版可用性
```

## 6 个月路线与检查清单

总预算约 250+ h（10h+/周）。**硬件到货顺序：电机/CAN 先到，本体后到**，故仿真/建栈前置，任何环节真机没到都在 MuJoCo 里推进。

### 阶段 0　摸底与基线（第 1 周）——产出：一页架构笔记
- [ ] 按 `docs/reading_guide.md` 通读 reBot 三份中文文档，自写一页“数据流长什么样”
- [ ] 核实 pixi 内 `ros2` 发行版细节；确认 `motorbridge`/`mujoco`/`pin` 在该 env 的安装方式
- [ ] `~/reBotArm_control_py` SDK 克隆到本地（阶段 C 必读）
- [ ] 跑通 `tools/check_env.sh`
- 验证：能不看文档讲出「命令与反馈两条链路 + 三种仿真模式区别」

### 阶段 1　电机链路打通（第 2–4 周）——吃透 CAN/协议
- [ ] `/dev/ttyACM*` 权限/udev 配置；`fuser` 排占用
- [ ] motorbridge 枚举电机、读反馈（位置/速度/力矩/温度/错误码）、使能/失能、设 POS_VEL、读写 PID 寄存器
- [ ] 先单电机点动，再整臂安全回零；夹爪开合
- 验证：**7 关节自检脚本**打印各关节状态；能画出「命令到一条 CAN 帧」的字节流
- ⚠️ 真机安全守则见文末

### 阶段 2　URDF + 运动学 + 最小 ROS2 控制器（第 5–9 周）
- [ ] 本体组装 → 核对构型与 reBot `bringup/description` 是否一致
- [ ] rclpy 自写：驱动/状态节点 → `/joint_states`；FK 自写；IK 自写并用 reBot SDK / MuJoCo 交叉验证
- [ ] real2sim：真关节角实时驱动 MuJoCo 模型
- 验证：`ros2 topic echo /joint_states` 有数；MuJoCo 里模型跟手

### 阶段 3　控制闭环 + MuJoCo 物理抓取（第 10–16 周）
- [ ] 吃透 reBot 核心设计：**7 电机统一 POS_VEL、固件闭环、上位机不叠双 PD**
- [ ] 重力补偿原理与实现（Pinocchio 广义重力）；轨迹平滑（梯形/Ruckig）
- [ ] MuJoCo physics grasp（`mj_step` + PD + 偏置补偿 + 力矩限幅）
- 验证：MuJoCo 里先跑通物理抓放

### 阶段 4　Capstone：视觉引导抓放（第 17–24 周）
- [ ] 手眼标定；相机检测定位（物体系 → 基座系）
- [ ] 检测 → IK → 轨迹 → 夹爪抓放，先 sim 后真机
- [ ] 留 1–2 周联调 + 排障记录 + 演示打磨
- 验证：真机在摄像头引导下完成抓放；你能完整讲清排障史

## 关键参考数据（reBot 已核）

- 电机 ID：关节 1–6 = `0x01`–`0x06`，夹爪 = `0x07`；反馈 ID = `0x11`–`0x17`
- 控制模式：全部 POS_VEL；`send_pos_vel(q, vlim)`，500 Hz 控制循环，`_q_target` 须同步防覆盖
- 夹爪单位：网页/ROS 用**米**（0 闭合 ~ 0.09 m 全开 ≈ −5 rad）
- 反馈：控制器 100 Hz 发 `/joint_states`
- reBot 面向 Ubuntu 24.04/Jazzy/Py3.12——本 pixi 环境已对齐 Jazzy/Py3.12

## 真机安全守则（尽早养成）

1. 每次上电先看 `arm_status`/错误码；确认只有一个控制器占用总线
2. 全程低速、带限位测试；物理空间与人员隔离；保留急停
3. 机械限位/方向不对时**先失能再排查**，别硬顶
4. 调 PID/寄存器前备份当前配置；改一次重启验证一次
5. 断电顺序：先失能/回零，再关电源

## 当前可跑的命令（DM_Armx 根目录）

```bash
pixi run bash -c "source install/setup.bash && ros2 launch mujoco_pkg slider_real2sim.launch.py"
# ↑ 主用：弹「关节滑块」+ MuJoCo viewer 两个窗口，拖滑块模型即跟动（默认 colored 1:1 外观）

pixi run bash -c "source install/setup.bash && ros2 launch mujoco_pkg real2sim.launch.py"
# ↑ 只看仿真：没输入源，模型静止；可 open_viewer:=false 纯后台

pixi run bash -c "source install/setup.bash && python tools/smoke_sim.py"                 # 冒烟①：模型+映射
pixi run bash -c "source install/setup.bash && python tools/smoke_slider_link.py"         # 冒烟②：滑块链路(真 DDS)
pixi run python tools/smoke_dm_frames.py                                                  # 冒烟③：CAN 帧（无需硬件）
pixi run python tools/smoke_dm_bus.py                                                     # 冒烟④：MotorBus 时序（无需硬件）
pixi run python tools/smoke_joint.py                                                      # 冒烟⑤：Joint 控制类（无需硬件）
```

电机链路（阶段 1，**不需要 ROS**；改完代码不用重编译，直接跑源文件）：

```bash
# ⓪ 接线换了/加了电机，先问一句"现在总线上是谁"——只发查询帧，不写寄存器
pixi run python tools/scan_bus.py
#    输出每个在线的 ID + 型号判定（读 Gr/PMAX/VMAX/TMAX 对照已知表）

# ⓪b 让 MotorBus 自己去问一遍（只读，不使能）：连通 / 路由 / poll 非阻塞 / 1:1 记账
pixi run python tools/bus_probe.py
#    改过 dm_bus.py 或换了接线就跑它 —— 它验的是"封装层真的能跟真电机说上话"

# ① 先看协议：不需要硬件、不需要 pyserial。打印 30 字节发送帧 / 16 字节反馈帧的逐字段拆解
pixi run python src/DMmotor_driver/DMmotor_driver/dm_bringup.py read --dry-run
pixi run python src/DMmotor_driver/DMmotor_driver/dm_bringup.py jog --dry-run

# ② 接上硬件，先只读。**注意 --id 和 --type 都要看实机**（脚本默认是 0x04/DM4310）
pixi run python src/DMmotor_driver/DMmotor_driver/dm_bringup.py read --id 0x01 --type DM4340
pixi run python src/DMmotor_driver/DMmotor_driver/dm_bringup.py monitor --duration 10 --id 0x01 --type DM4340

# ③ 确认无误后单电机低速点动（必须 --yes）+ 链路吞吐实测
pixi run python src/DMmotor_driver/DMmotor_driver/dm_bringup.py jog --yes --amp 0.2
pixi run python src/DMmotor_driver/DMmotor_driver/dm_bringup.py bandwidth --hz 500 --duration 30

# ④ MIT 点动（**零寄存器写入**，两种型号 CTRL_MODE 都是 1=MIT，不用改任何配置）
pixi run python src/DMmotor_driver/DMmotor_driver/dm_bringup.py jog --mit --id 0x01 --dry-run
pixi run python src/DMmotor_driver/DMmotor_driver/dm_bringup.py jog --mit --id 0x01 --yes --kp 25
#                                                                    ↑ 4340P 用 25~60；4310 用 10~25

# ⑤ 想知道链路的真实上限（不是标称波特率），压到冲不动为止：
pixi run python src/DMmotor_driver/DMmotor_driver/dm_bringup.py bandwidth --hz 4000 --duration 6

# 整臂 7 关节是后续 dm-calibrate / 驱动层的事；当前只有单电机脚本
```

### MIT 点动的三个阶段（以及怎么看结果）

脚本走阶梯：`kp=0` 零刚度 → `kp=--kp` 原地保持 → 正弦。**每档都比上一档多出力**，出问题就在低档停住。

`--kp` 怎么选（实测规律，见 design.md §2.7/§2.8）：**回零残差 ≤ 静摩擦/kp**。两种型号都测过：

| `--kp` | 4310 幅值 | 4310 峰值力矩 | 4340P 幅值 | 4340P 峰值力矩 |
|---|---|---|---|---|
| 1~2 | 38% | ~0.14 N·m | **0%**（纹丝不动） | 0.32（只是下界） |
| 5 | 87% | 0.154 | — | — |
| 10 | 93% | 0.149 | 80% | 0.663 |
| 15 | 96% | 0.159 | 91% | 0.527 |
| 25 | 98% | 0.144 | 94% | 0.581 |
| 60 | — | — | 97% | 0.718 |

- **峰值力矩在该型号的所有 kp 档下都稳定在同一区间，不随 kp 变** → 这个数就是该轴静摩擦，不是"kp 出多大力"。
- **裸 4310 静摩擦 ≈ 0.15 N·m；裸 4340P ≈ 0.62 N·m（约 4 倍 = 40:1 vs 10:1 的减速比）**。
- 所以**推荐起点：4310 用 `--kp 15`，4340P 用 `--kp 25`**（4340P 摩擦大 4 倍，同 kp 下死区也大 4 倍）。
- `--tau-abort` 默认 2.0 N·m 对 4310 是摩擦的十几倍；**跑 4340P 时建议提到 5.0**（仍只有额定 12 N·m 的 42%），否则大 kp 档可能被误判为超力矩。

> 安全：`read`/`monitor` 全程只读；`jog`、`jog --mit` 和 `bandwidth --enable` 会让电机通电出力，必须显式 `--yes`，且都会「从当前位置出发 → 回当前位置 → 失能」（Ctrl-C 也走这条路）。脚本**永不调用** `set_zero_position`，**不写任何寄存器**。
>
> 四个自动保护：`ERR` 变成 0/1 以外、`|tau|` 超过 `--tau-abort`（默认 2.0 N·m）、使能瞬间位置跳变超过 `--jump-abort`（默认 0.1 rad）—— 任一触发立刻失能退出；链路连续丢 `--miss-tol`（默认 3）帧也停（丢一帧会先重发同一帧，因为 MIT 位置指令是幂等的）。
>
> ⚠️ 已知不足：低 kp 下「回 p0」可能**推不动静摩擦而停在别处**（脚本会验并告警，不会假装成功）。裸电机无害，装到臂上要先加大 kp。

## 当前状态（2026-09-19）

- pixi 环境就绪（Python 3.12 + ROS2 Jazzy + mujoco 3.12.0 + numpy + yaml + pyserial + tkinter 8.6）；`pin` 待后续再加
- **仿真已移植并验证**：`src/mujoco_pkg`（改名自 rebotarm_mujoco）+ `src/rebotarm_msgs` + `src/fake_driver_pkg`，`colcon build` 通过
- **外观模型已就位**：`meshes/` 自带 50 个 STL（42M，colored+stl 模型引用的并集，不依赖 `rebotarm_bringup`）；默认模型 `rebotarm_b601_colored.xml` + `joint_map_kinematic.yaml`（**与上游 reBot 所有 launch 的配对一致**）
- **「手动 → 模型跟动」链路已通**：`slider_real2sim.launch.py` 一键拉起滑块 GUI + real2sim；滑块 30Hz 持续发布 `/rebotarm/joint_states`
- **电机封装层设计定稿**：`src/DMmotor_driver/design.md` **v0.5**，10+3+6+5 条修订全部有依据；三处最大修正 = **删掉 gear_ratio**（0x51 已是输出轴 rad）、**不能用 `control_Pos_Vel`**（内含 `sleep(0.001)`+阻塞 `recv()`）、**补上电机侧看门狗 0x09 与温度/错误码**（SDK 把 D[6]/D[7] 丢了）
- **电机 bring-up 脚本已落地**：`dm_bringup.py` 四个子命令（read/monitor/jog/bandwidth）+ `jog --mit`；`tools/scan_bus.py` 扫总线/认型号；`--dry-run` 不需要硬件也不需要 pyserial
- **🎉 两种型号真机都跑通**（`jog --mit`，裸电机空载，零寄存器写入）—— 4310 和 4340P。实测结论：
  - **1:1 请求-响应**：任何帧送达电机必**恰好回一帧**，不发就没有。→ reBot 那条独立 100Hz 刷新帧**可以整个去掉**，白拿 500Hz 反馈。**在 4340P 上复现，且压到标称容量 167% 时仍 100.0%** → 不是单只电机的个案
  - 电机 ID 两只都是 **`0x01`**（脚本默认的 `0x04` 不是）；反馈 CAN ID = `0x000`（MST_ID=0）
  - `CTRL_MODE=1` 两只都是 MIT → **点动全程没写任何寄存器，也不需要断电重启**
  - `KP_APR=54`（两只都是，达妙出厂值 **不是** reBot 的 → D7「先读后写可回滚」是必要的）、`TIMEOUT=0`（**看门狗当前是关的**）、VBus=24.27V
  - **MIT 稳态残差 ≤ 静摩擦/kp**（5 档 kp 逐点验证）→ 这是「常规运动必须走 POS_VEL 固件闭环」的实测依据
  - **静摩擦实测：裸 4310 ≈ 0.145~0.159 N·m；裸 4340P ≈ 0.53~0.72 N·m**（后者恰是前者的 4 倍 = 减速比 40:1 vs 10:1，交叉验证成立）
  - **4340P 寄存器**：`Gr=40`、`PMAX/VMAX/TMAX=12.5/10/28`。⚠️ **这组数不是"与手册 40 N·m 矛盾"** —— 手册那组（额定 12 / 峰值 40 N·m、空载 5.86 rad/s）是**物理能力**，寄存器这组是 **MIT 帧的映射范围**，两者本来就该不同（design.md v0.5 第 20 条更正了这个我早先写错的判断）
  - **链路真实上限 ≈ 3255 控制帧/s**（单适配器 + Python 循环），而 **7 关节 × 500 Hz = 3500 刚好够不着** → **架构上应按 200~300 Hz/关节设计**（design.md D2 已据此下调）
  - **CDC-ACM 的 921600 是摆设**：实测压到 **167% 标称容量（154 KB/s）**照跑不误 → design.md 里那套"111% 超载"的算法作废
- **踩了一个坑并修掉（已加回归测试）**：`ser.read_all()` 是**非阻塞**的，`write(); read_all()` 必然读到上一帧或空 → 首测在第 1.6s 误判"收不到反馈"停机（电机其实全程正常）。修法 = `RxBuf` 带残留缓冲 + 自己阻塞等到 deadline + 发前 flush。`smoke_dm_frames.py` 新增第 [5] 节：用假串口按 1/5/9/16/17 字节分块喂帧必须一帧不少，并留了个"逐块切帧在 5 字节/块下一帧都切不出"的反面教材
- **`bandwidth` 的报告已修**：控制帧/刷新帧分开计数（原来混在一起 → 报出"116%"这种假结论）；RX 计数原用非阻塞 `read_all()` 会**少数**
- 验证结果：
  - 模型：colored `nq=29 nmesh=47`、stl `nmesh=10`；6 关节按 qpos_addr 21-26 逐一到位；夹爪 `finger_left`→双指（右指 scale=-1）
  - 链路：**QoS 配对已实测**——滑块用默认 QoS（RELIABLE/depth10/volatile）发布，real2sim 用 `qos_profile_sensor_data`（BEST_EFFORT）订阅，两者兼容（DDS 规则：offer ≥ request），8 个 qpos 目标全到位
  - 帧协议：**`smoke_dm_frames.py` 全绿**——30B TX 帧、16B RX 切帧（4 种脏数据场景与 SDK `__extract_packets` 一致）、反馈解码（与 SDK `__process_packet` 一致，且温度 D[6]/D[7] 只有我们解出来）、**MIT 帧与 SDK `controlMIT` 逐字节一致（5 组参数含两端饱和值）**、接收缓冲抗拆帧
- 待办：
  - [ ] 带显示器亲自跑一次滑块：`ros2 launch mujoco_pkg slider_real2sim.launch.py`，确认拖动跟手、夹爪开合方向对
  - [ ] 接 **Fake Driver** 当输入源（比滑块更接近真机：走 `/rebotarm/joints/jointN/cmd` + `enable/safe_home` 等服务）
  - [x] ~~接 4340P 后测回读值与静摩擦~~ → **已测**：`Gr=40`、`PMAX/VMAX/TMAX=12.5/10/28`、静摩擦 ≈ **0.62 N·m**
  - [x] ~~`bandwidth --hz 500`~~ → **已测**，并一路压到 4000 Hz 摸到天花板（3255 帧/s）
  - [ ] `jog --yes`（**POS_VEL 路径**，需先切 `CTRL_MODE=2`）—— 还没跑过，会实测掉：POS_VEL 下 1:1 是否仍成立、`vlim` 的实际效果、固件闭环的稳态误差
  - [ ] 实测**电机侧看门狗**：写 0x09=200ms 后拔线，确认真的退出使能（安全底线；当前 `TIMEOUT=0`，保护是关的）
  - [ ] 按 design.md D5b **逐关节标定重力补偿的 kp**（reBot 硬编码的 7.0 对 4340P 关节偏小，不能照抄）
  - [ ] 实现 `MotorBus`/`Joint`/`DmArm`/`RegisterTool`（design.md §9 优先级表）；补 `package.xml` 依赖、`setup.py` entry_points、`config/rebotarm_b601_mixed.yaml`
  - [ ] 核对模型关节限位 vs 你实机限位（**joint3 范围 `[-3.14, 0]`，三个模型与 URDF 完全一致**，非移植问题；但正方向到底是不是 0，需上真机确认）
- 硬件：电机/CAN 部分到货（已可单电机点动），本体后到
- 蓝本 repo 已就位：`~/reBot_Arm_Mujoco-DM`、`~/PyArmX`；SDK `~/reBotArm_control_py` 待克隆
