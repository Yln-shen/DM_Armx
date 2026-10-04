# arm_description —— DM_Armx 的 URDF/xacro 描述

纯数据包（无代码）：几何 + xacro 装配 + 显示配置。

## 来源与许可（重要）

几何资产**不是我们写的**：

- 来源仓库：<https://github.com/Yang-Ci/Borot-Arm_Mujoco.git>（`reBotArm_ros2_DM/src/rebotarm_bringup/description/`）
- 许可：**CERN Open Hardware Licence v2 – Weakly Reciprocal（CERN-OHL-W-2.0）**，全文见
  [LICENSE-CERN-OHL-W-2.0](LICENSE-CERN-OHL-W-2.0)。按该许可要求，本包保留许可全文、注明来源与改动。
- 本包**自写**的部分（xacro 装配、`<ros2_control>` 段、launch、rviz、本文档）为 Apache-2.0，见 [LICENSE](LICENSE)。
  （CERN-OHL-W 是"弱互惠"：只要求被覆盖的源文件保持同许可，不与我们的软件许可冲突。）

**原样（verbatim）拷贝、未改动的文件**：

| 路径 | 说明 |
|---|---|
| `meshes/`（73M，60 个文件） | 手臂各 link + 夹爪手指的彩色 STL/DAE 与基础 STL，一字未改 |
| `reference/ReBot_Arm_DM.urdf`（457 行） | 上游原始 URDF，留作"改动前"对照（便于 diff 出我们改了什么） |

**未拷贝、有意跳过**（记录备查）：上游还有 `description/meshes_b601_gripper/`（27M，
`link1.STL`…`link6.STL`、`gripper_left/right.STL`、`base_link.STL` 等）。
核对结果：**上游 URDF 一处都没引用它**（所有 `<mesh>` 路径都在 `meshes/` 下，夹爪手指的
mesh 也在 `meshes/` 里），拷进来只是死重量，故本包不含。以后若换用该夹爪模型再补。

## 我们的改动（相对上游）

| 文件 | 改动 |
|---|---|
| `urdf/inc/arm_geometry.urdf.xacro` | ①`<mesh>` 包路径 `package://rebotarm_bringup/description/meshes/` → `package://arm_description/meshes/`；②夹爪 link/joint 移出到 `gripper.urdf.xacro`；③`effort`/`velocity` 换成本项目电机实测值（j1–j3 `12.0`/`10`，j4–j6 `3.5`/`30`，见下）；④fixed joint 去掉无意义的 `<axis>`；⑤补 `<?xml ...?>` 与注释 |
| `urdf/inc/gripper.urdf.xacro` | 夹爪（`finger_left`/`finger_right`，prismatic）包成 `<xacro:macro name="gripper_links">`，**默认不调用** |
| `urdf/arm.urdf.xacro` | 新的主文件：include 上面两个 + `<ros2_control>` 宏 + `gripper_tcp` 固定关节（照上游 `xyz="-0.105 0 0"`） |
| `urdf/inc/arm.ros2_control.xacro` | 新写：`mock_components/GenericSystem`（6 关节 position 命令 + position/velocity 状态） |

### ⚠️ joint limit 还没换成我们标定的值

`arm_geometry.urdf.xacro` 里 `joint1..joint6` 的 `lower/upper` 仍是**上游参考值**
（`[-2.8,2.8]` / `[-3.14,0]` / `[-3.14,0]` / `[-1.87,1.57]` / `[-1.57,1.57]` / `[-3.14,3.14]`）。

原因：URDF 的关节零点是**模型几何**定义的姿态，而本项目的零位是**我们自己在真机上摆的**
（`src/motor_driver/config/joint.yaml` 的 `offset`，且各关节都靠近机械限位），两者差一个
零点差 δ；正方向也可能与模型 `axis` 反号（j1 模型是俯视逆时针，我们定义的是俯视顺时针）。
这两个量必须**实测**（计划里的 M1b：把真机摆到模型零位 / 在 RViz 里对照），
换算后才能把 `joint.yaml` 的 `position_min/max` 写进 URDF。

在那之前，模型只能用于**显示与 mock 链路**（M1/M2），不能用来判断"真机现在在模型里的哪个姿态"。

### effort / velocity 取值的依据

| 关节 | 型号 | `effort`（N·m） | `velocity`（rad/s） |
|---|---|---|---|
| joint1–joint3 | 达妙 4340P | `12.0`（`joint.yaml` 的 `torque_max`） | `10`（电机 VMAX） |
| joint4–joint6 | 达妙 4310 | `3.5`（`joint.yaml` 的 `torque_max`） | `30`（电机 VMAX） |

上游写的是 `effort=27/7`、`velocity=50/200`——那是对外宣传值，`200 rad/s` 明显不现实，
故不采用。真正跑轨迹时的速度上限由 `joint_trajectory_controller` 的参数再限一层。

## 怎么用

```bash
colcon build --symlink-install
source install/setup.bash

# ① 展开检查（不启动任何东西）
xacro $(ros2 pkg prefix arm_description)/share/arm_description/urdf/arm.urdf.xacro > /tmp/arm.urdf

# ② 显示：robot_state_publisher + 滑动条 + rviz2（不接真机、不动电机）
ros2 launch arm_description display.launch.py
ros2 launch arm_description display.launch.py use_gripper:=true   # 连夹爪一起看
```

## 目录

```text
arm_description/
├─ urdf/arm.urdf.xacro              主文件（include 下面三个 + TCP + ros2_control）
├─ urdf/inc/arm_geometry.urdf.xacro 手臂几何（8 link / 7 joint / 11 material）
├─ urdf/inc/gripper.urdf.xacro      夹爪几何（宏，默认不调用）
├─ urdf/inc/arm.ros2_control.xacro  硬件接口（现在是 mock；M5 换真插件）
├─ launch/display.launch.py
├─ rviz/display.rviz
├─ meshes/                          73M（verbatim，上游）
├─ reference/ReBot_Arm_DM.urdf      上游原始 URDF（verbatim，对照用）
├─ LICENSE                          Apache-2.0（本包自写部分）
└─ LICENSE-CERN-OHL-W-2.0           上游几何的许可全文
```
