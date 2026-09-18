# 架构笔记（边读边填）

> 读 `docs/reading_guide.md` 每过一节，回来填一块。**验证标准：能不看资料画出来、讲出来。**

## 0. 数据流一页图（阶段 0 产出）

把 reBot 的链路用自己的话画在这里（命令方向 / 反馈方向，网页→rosbridge→ROS2→Controller→SDK→MotorBridge→USB-CAN→电机）：

```text
（待你补：把 DATA_FLOW_ZH.md 的链路自己重画一遍，标注 500Hz/100Hz/单位/ID）
```

## 0.5 仿真 vs 真机：接口关系（2026-09-17 核实）

结论：**“同接口”这件事发生在驱动层，不在 MuJoCo 层。**

| 层 | 真机 | 仿真 | 接口是否一致 |
| --- | --- | --- | --- |
| 上层（网页/agent/脚本） | 发 `/rebotarm/joints/jointN/cmd`、调 `/rebotarm/enable|disable|safe_home|gripper/set`、走 FollowJointTrajectory | **Fake Driver 提供完全相同的一套** | ✅ 一致（有意设计） |
| 驱动 | `reBotArmController` → SDK → MotorBridge → USB-CAN → 电机 | `Fake Driver` 维护目标并逐步逼近，不接电机 | ✅ 一致 |
| 物理/渲染 | 真实世界 | `mujoco_pkg` 各节点：**订阅** `/rebotarm/joint_states`（当目标），内部 `qfrc_applied = 偏置 + Kp·Δq + Kd·Δqd`，发布 `/rebotarm/mujoco/*` | ❌ 不是驱动，是仿真器 |

要点：
1. MuJoCo 节点**不实现** `/rebotarm/joints/jointN/cmd` 或那些 service，所以它**不能顶替真机驱动**。
2. 想做到“一份上层代码，仿真/真机不改就能切”，正确做法是让你自己的**仿真驱动实现与真机驱动相同的接口**（范本：`fake_driver.py`），MuJoCo 负责“看得见的物理世界”。
3. 落地顺序建议：Fake Driver 跑通 → MuJoCo 订阅它 → 切真机时只换驱动，上层不动。


## 1. 三种“仿真/显示”的区别

| 名称 | 是否 `mj_step` | 有无真实动力学 | 用途 | 你的最小版要不要 |
| --- | --- | --- | --- | --- |
| 网页 Three.js 模拟器 | 否 | 否 | 浏览器显示/示教 | ？（Capstone 再定） |
| MuJoCo real2sim | 否（`mj_forward`） | 否 | 真机姿态同步显示 | 要 |
| MuJoCo physics grasp | 是 | 是 | 抓取/动力学验证 | 要 |

## 2. 控制器状态机

```text
（待你补：IDLE / LOWLEVEL_STREAMING / TRAJ_RUNNING / GRAVITY_COMP 及转换条件）
```

## 3. 模块对照表（你的自写栈 ↔ 蓝本）

| 你自写模块 | 主参考 reBot | 借形 PyArmX | 我的目标 / 进度 | 已验证 |
| --- | --- | --- | --- | --- |
| 电机链路 `dm_link` | motorbridge + `hardware_config.py` | —— | 用 SDK 但读得懂协议 | ☐ |
| 真机驱动（ROS2 node） | `hardware_manager.py` + `rebotarm_controller.py` | `real_facade.py` | 7 关节 + 夹爪，可 mock | ☐ |
| Fake / mock 层 | `fake_driver.py` | `mock=True` | 真机没到也能开发 | ☐ |
| URDF + real2sim | `rebotarm_bringup/description` + `real2sim_sync.py` | `sim_facade.py` | | ☐ |
| FK / IK | SDK `kinematics/` + MuJoCo Jacobian | `ik.py` | 自写 + 交叉验证 | ☐ |
| 轨迹 / 平滑 | SDK 轨迹、MoveIt | `motion.py`(Ruckig) | | ☐ |
| 控制（POS_VEL/重力补偿） | `hardware_manager` + SDK `dynamics` | —— | 固件闭环、不叠双 PD | ☐ |
| MuJoCo physics grasp | `mujoco_physics_grasp.py` | —— | | ☐ |
| 视觉定位 | `sim_rgb_camera.py`+`sim_color_detector.py`+标定 | `cv/tags.py` | Capstone | ☐ |

## 4. 关键参数表（真机必核对，reBot 参考值）

| 项 | reBot 参考 | 实测/你的仿件 | 备注 |
| --- | --- | --- | --- |
| 关节 ID | 0x01–0x06 | | 夹爪 0x07，反馈 0x11–0x17 |
| 夹爪开度 | 0 闭合 ~ 0.09 m ≈ −5 rad | | 米 ↔ 弧度换算 |
| 控制模式 | 全 POS_VEL | | 上位机不做外环 PD |
| 电机 PID | 见 SDK yaml | | 改一次重启验证一次 |
| 反馈发布 | 100 Hz | | |

## 5. 排障日志索引（另开 `docs/troubleshooting.md` 或记在 study-log）

每条记：现象 / 你试了什么 / 根因 / 修复 / 可复现命令。**系统集成岗位的面试点就是排障故事。**

## 6. 每周一问自测

每周从零（不看资料）讲一遍：数据流、状态机、为什么 POS_VEL 不叠双 PD、real2sim 与 physics 的区别。讲不顺 = 本周没吃透。
