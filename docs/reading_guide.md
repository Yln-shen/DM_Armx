# reBot / PyArmX 精读导读

> 目的：**先建立全局模型，再拆解到字节**。reBot 是“参考答案”——读它是为了**自己复述得出来、能交叉验证**，不是照着抄文件。
> 每读完一节，在 `architecture_notes.md` 里填空，能“画出来/讲出来”才算过。

两条蓝本路径简写：
- `RB` = `~/reBot_Arm_Mujoco-DM`
- `PA` = `~/PyArmX`

---

## A. 文档层（1–2 天，先通读，别碰代码）

按顺序读，配一支笔画数据流：

| 顺序 | 文件 | 看完要能回答 |
| --- | --- | --- |
| 1 | `RB/PROJECT_ARCHITECTURE_ZH.md` | 三种“仿真”的真实区别：网页 Three.js（无动力学）/ MuJoCo real2sim（`mj_forward` 只同步姿态）/ MuJoCo physics grasp（`mj_step` 真动力学）。防抖靠哪些手段叠加？文档自己承认的缺口有哪些？ |
| 2 | `RB/DATA_FLOW_ZH.md` | 命令链路与反馈链路（网页→rosbridge→ROS2→Controller→SDK→MotorBridge→`/dev/ttyACM0`→电机）。为什么单关节命令要同步 SDK 的 `_q_target`？夹爪单位为什么是米？重力补偿切 MIT 时为什么必须“逐关节立即给保持命令”？状态机有哪几个态？ |
| 3 | `RB/USER_MANUAL_ZH.md` | 真机启停、校准、回零、急停习惯（安全红线）。 |

**产出 A**：不看文档，能画出数据流图 + 说出状态机 4 态。

---

## B. 控制器代码层（真机如何被抽象成 ROS 接口）

全在 `RB/reBotArm_ros2_DM/src/rebotarmcontroller/rebotarmcontroller/`。

| 文件 | 看什么 | 想清楚的问题 |
| --- | --- | --- |
| `rebotarm_controller.py` | ROS2 Node 主入口，`joint_state_rate=100Hz`，组合 Services/Actions | 一个 Node 怎么拆职责（发布/服务/动作）？为什么用组合而非一个巨型类？ |
| `hardware_manager.py` | 核心。找 SDK 的候选路径（`_sdk_candidates`）、`connect()`、`mode_pos_vel()`、`enable()`、POS_VEL 控制循环启动、**夹爪独立线程 + 把 SDK 控制循环的 `_has_gripper=False`** | “同一总线上 6 轴走统一控制循环、夹爪单独管”为什么更安全？什么叫“控制互斥/抢占”？ |
| `fake_driver.py` | 不接电机的“虚拟执行器”，逼近真机的接口 | 为什么最小版**一定要有**这个？→ 真机没到时天天可开发（呼应 PyArmX 的 `mock=True`） |
| `ros_services.py` / `ros_actions.py` / `ros_publishers.py` | 各暴露哪些 topic/service/action，命名规范 | 接口契约怎么定才不打架？ |
| `conversions.py` | 单位/方向/偏置转换（rad、夹爪米↔电机弧度） | 你自写时最容易踩的坑就在这（joint 方向 `direction=-1`、offset）。 |
| `rebotarm_msgs/msg|srv|action` | 消息字段设计 | 自定义消息何时值得建，何时该用标准消息？ |

**产出 B**：给你一页纸，你能画出自己最小版 ROS2 节点的接口清单。

---

## C. 电机链路 & SDK（“吃透协议”的入口）

电机协议不在这两个仓库里，在 SDK + motorbridge 里。**先克隆**：

```bash
git clone https://github.com/Seeed-Projects/reBotArm_control_py.git ~/reBotArm_control_py
```

| 位置 | 看什么 |
| --- | --- |
| `~/reBotArm_control_py/config/rebotarm_dm.yaml` | 电机 ID、波特率、关节限位、POS_VEL PID 参数表（吃透协议的“字典”） |
| `~/reBotArm_control_py/reBotArm_control_py/actuator/` | `RebotArm` / `JointGroup`：使能、模式切换、POS_VEL 控制循环实现 |
| `.../controllers/` | `RebotArmEndPose`：轨迹、IK、重力补偿入口 |
| `.../kinematics/` `.../dynamics/` | FK/IK 与 `compute_generalized_gravity`（重力补偿 = 动力学，不是 PID） |
| motorbridge（pip 包源码） | DM Serial Transport：**一条命令如何变成 CAN 帧**、寄存器读写、反馈轮询。这是“吃透协议”真正的门槛 |
| 达妙电机协议手册 | 帧结构 / 主从 ID / ControlMode（MIT、POS_VEL、POS 等）/ 寄存器表 |

**产出 C**：能写出「上位机一条位置命令 → 电机执行」的字节级流程 + 知道去哪个寄存器改 PID/查错误码。**边界**：会用 SDK 但看得懂协议即可，不必从零重写驱动（你已定的深度）。

---

## D. MuJoCo 仿真层

`RB/reBotArm_ros2_DM/src/rebotarm_mujoco/rebotarm_mujoco/`：

| 文件 | 看什么 |
| --- | --- |
| `real2sim_sync.py` | 订阅 `/joint_states` → YAML 映射 ROS 关节名到 MuJoCo `qpos` → `mj_forward`。默认 60Hz、`smoothing_alpha`、`stale_timeout` |
| `mujoco_physics_grasp.py` | **这才是真接触动力学**：500Hz 回调算 `tau=qfrc_bias+Kp·Δq+Kd·Δqd`，写入 `qfrc_applied`，`mj_step`；夹爪高刚度、独立力矩限幅 |
| `models/*.xml`（`rebotarm_b601_stl.xml` / `kinematic.xml`） | 质量/惯量/STL/简化碰撞体；`joint_map_kinematic.yaml` 一个 ROS 夹爪关节→两个 MuJoCo 滑动关节 |
| `sim_task_server.py` | 用 MuJoCo Jacobian 求 IK、`MoveToPose`、轨迹录制回放 |
| `sim_rgb_camera.py` / `sim_color_detector.py` | `mujoco.Renderer` 离屏渲染 → RGB → 颜色检测（你 Capstone 视觉抓放的雏形） |
| `RB/MUJOCO_ANTI_PENETRATION_ZH.md` | 防穿透接触参数说明 |

注意仿真边界：Python timer 非硬实时、real2sim 默认不滤波、MuJoCo 版本未锁定。**仿真跑通 ≠ 真机能跑**，真机验证前先低速、先看方向与限位。

---

## E. PyArmX：只“借形”，读这几处即可

| 文件 | 借什么 |
| --- | --- |
| `PA/src/pyarmx/real_facade.py` | `mock=True` 干跑 + facade 拆分 + `JointCfg(direction/offset)` 表达。**顺带看穿它的局限**：依赖私有 `pydamiao`、走 Serial、6 轴无夹爪 → 印证它不是你的复刻对象 |
| `PA/src/pyarmx/sim_facade.py` | sim/real 同接口的抽象形状 |
| `PA/src/pyarmx/ik.py` | 运动学求解器的清爽写法（对照 reBot 的重实现） |
| `PA/src/pyarmx/motion.py` + `examples/sim/*ruckig*` | 轨迹平滑（Ruckig）怎么用 |
| `PA/examples/facade/sim2real.py` | “仿真摆好→真机复现”的最小流程 |

**不借**：`pydamiao`、`rose`、`vlm/*`、ZMQ 架构（私有栈 + 算法向，与你的目标错位）。

---

## 阅读打卡顺序建议

A 全读 → B 先 `rebotarm_controller.py` + `hardware_manager.py` + `fake_driver.py` → 补 C（SDK/motorbridge 边用边读）→ D 按“real2sim → physics grasp”的顺序 → E 穿插在看 B/D 之间建立“抽象品味”。

每个阶段读完，在 `architecture_notes.md` 填一节。**讲不出来 = 没读完。**
