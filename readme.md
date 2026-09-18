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
│  └─ smoke_slider_link.py 冒烟测试②：滑块→joint_states→模型，走真 DDS（验 QoS 配对）
└─ src/
   ├─ mujoco_pkg/         MuJoCo 仿真包（移植 rebotarm_mujoco，已改名）
   │  ├─ models/          kinematic(0 mesh) / colored / stl / simple
   │  ├─ meshes/          50 个 STL（42M，colored+stl 模型实际引用的并集）
   │  ├─ config/          joint_map.yaml / joint_map_kinematic.yaml
   │  ├─ description/     内联 ReBot_Arm_DM.urdf（17KB，视觉调色用）
   │  └─ launch/          slider_real2sim(滑块+仿真) / real2sim / physics_grasp / ...
   ├─ rebotarm_msgs/      自定义 msg/srv/action（保持原名，import 直接用）
   └─ fake_driver_pkg/    Fake Driver 虚拟执行器（移植自 rebotarmcontroller）
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
```

## 当前状态（2026-09-18）

- pixi 环境就绪（Python 3.12 + ROS2 Jazzy + mujoco 3.12.0 + control_msgs + tkinter 8.6）；`motorbridge`/`pin` 待电机阶段再加
- **仿真已移植并验证**：`src/mujoco_pkg`（改名自 rebotarm_mujoco）+ `src/rebotarm_msgs` + `src/fake_driver_pkg`，`colcon build` 通过
- **外观模型已就位**：`meshes/` 自带 50 个 STL（42M，colored+stl 模型引用的并集，不依赖 `rebotarm_bringup`）；默认模型 `rebotarm_b601_colored.xml` + `joint_map_kinematic.yaml`（**与上游 reBot 所有 launch 的配对一致**）
- **「手动 → 模型跟动」链路已通**：`slider_real2sim.launch.py` 一键拉起滑块 GUI + real2sim；滑块 30Hz 持续发布 `/rebotarm/joint_states`
- 验证结果：
  - 模型：colored `nq=29 nmesh=47`、stl `nmesh=10`；6 关节按 qpos_addr 21-26 逐一到位；夹爪 `finger_left`→双指（右指 scale=-1）
  - 链路：**QoS 配对已实测**——滑块用默认 QoS（RELIABLE/depth10/volatile）发布，real2sim 用 `qos_profile_sensor_data`（BEST_EFFORT）订阅，两者兼容（DDS 规则：offer ≥ request），8 个 qpos 目标全到位
  - 组合 launch 实拉：两节点同时起，无报错
- 待办（仿真侧）：
  - [ ] 带显示器亲自跑一次滑块：`ros2 launch mujoco_pkg slider_real2sim.launch.py`，确认拖动跟手、夹爪开合方向对
  - [ ] 接 **Fake Driver** 当输入源（比滑块更接近真机：走 `/rebotarm/joints/jointN/cmd` + `enable/safe_home` 等服务）
  - [ ] 想让模型更轻可临时覆盖：`model_path:=.../rebotarm_b601_kinematic.xml`（0 mesh，无外观）
  - [ ] 核对模型关节限位 vs 你实机限位（**joint3 范围 `[-3.14, 0]`，三个模型与 URDF 完全一致**，非移植问题；但正方向到底是不是 0，需上真机确认）
- 硬件：电机/CAN 部分到货，本体后到
- 蓝本 repo 已就位：`~/reBot_Arm_Mujoco-DM`、`~/PyArmX`；SDK `~/reBotArm_control_py` 待克隆
