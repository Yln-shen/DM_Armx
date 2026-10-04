# motor_driver_hardware —— DM_Armx 的 C++ 侧

当前有 **协议层 + 串口层 + 总线层 + 关节层**（一个 `dm_hardware` 库，**不依赖 rclcpp**）；
`ros2_control` 的 `SystemInterface` 插件是下一步（M5，见根目录 `AGENTS.md` 的路线）。

## 四层分别管什么

| 层 | 文件 | 管什么 | 不管什么 |
|---|---|---|---|
| 协议 | `dm_frames` | 30B 发送帧（MIT / 位置速度 / 力位混控 / 使能失能 / 刷新 / 寄存器）、16B 收帧切分、反馈与寄存器回包解码 | 不发、不碰串口、不判安全 |
| 串口 | `dm_serial` | termios 8N1 @921600、**非阻塞读**、写满、`stats` | 不解析帧（`SerialIo` 是接口，测试可塞内存假串口） |
| 总线 | `dm_bus` | 唯一发送出口、收帧分类（反馈 / 寄存器回包 / 陌生 ID）、状态缓存、`wait_feedback`、`sync_states`、寄存器 I/O | 不写寄存器（只发帧）、不自动使能、不判安全、不拥有线程 |
| 关节 | `dm_joint` | 换算（direction/offset）、软限位、PMAX、NaN 拦、`set_pos_vel`、使能（**带保持帧**）、状态、ERR 检查 | 不拥有控制循环、不 poll、不写寄存器、**只做 POS_VEL** |

## 这一层是什么 / 不是什么

**是**（纯函数库 `dm_frames`）：

- 30 字节适配器**发送帧**的构造：MIT / 位置速度 / 力位混控 / 使能失能 / 刷新 / 寄存器读·写·存参
- 16 字节**接收帧**的切分（`extract_rx` / `RxBuf`）与解码（反馈帧 / 寄存器回包）

**不是**（边界，改之前先读）：

- **不开发送**：只返回字节；串口在后面的 `dm_serial`
- **不碰串口 / 不 sleep / 不打屏**（官方例程 `damiao.h` 里有 `std::cout` 与 `usleep`，这里都不要）
- **不判安全**：不钳软限位、不看 ERR、不决定使能 —— 那是 `joint` / `arm` 那一层的活
- **不碰运动学**

## ⚠️ 两份实现必须逐字节一致

`dm_frames` 现在有**两份**：Python 的 `motor_driver/dm_frames.py`（驱动层）与本包的 C++ 版（硬件接口层）。
任何一处改错都会让"电机收到不一样的帧"却**不报错**。所以本包的对拍测试**不写"我期望的字节"**，
而是直接调 Python 那份造同样的帧、逐字节比：

```bash
cd /home/ylnn/DM_Armx
colcon build --symlink-install
colcon test --packages-select motor_driver_hardware --event-handlers console_direct+
colcon test-result --verbose          # 期望 0 failures
```

对拍覆盖：6 类发送帧 × 多组参数 + 边界（`q=±PMAX`、`kp=500/kd=5`、`tau=±TMAX`、
力位混控两个无符号量的钳位）+ 接收切分（干净流 / 含垃圾 / 残片）+ 反馈解码（含 2026-10-03
那条真实 id2 帧 `02 92 55 7f f7 fe 1c 1b`）+ `RxBuf` 分块喂 + `is_reg_response` 边界。

**改 Python 的 `dm_frames.py` 之后，一定要重跑这个测试**（反过来也一样）。

## 已修的一个真机坑（Python 侧陷阱 #26）

官方 `damiao.h` 判寄存器回包只看 `data[2] ∈ {0x33,0x55}`。而**反馈帧的 `D[2]` 就是 POS16 的低字节** ——
电机停在低字节恰为 `0x33/0x55/0xAA` 的位置时（约占位置范围 1.2%），它**每一条反馈都会被误当成寄存器回包丢掉**
（真机实测：id2 停在 +1.79 rad 时 20/20 条反馈全被丢，那台电机在缓存里彻底隐身）。
本包的 `is_reg_response` 补了 `D[0] ≤ 0x0F && D[1] == 0x00`（真寄存器回包的 `D[0:2]` 恒为"目标 ID 小端"）。

## 来源

协议字节布局改编自达妙官方 `C++例程/u2can/include/damiao.h`
（已 vendored 到仓库的 `src/third_party/C++例程/u2can/`，便于以后核对与排障），
并按 Python 侧的语义补齐了寄存器帧、回包分类与 `RxBuf`。

⚠️ 那份例程的 `CMakeLists.txt` 里有 `project (dm_Linux_Drive)` ⇒ colcon 会把它当**包**（全量构建时
多出一个包、还会连累别的包）。所以 `src/third_party/` 放了一个空的 **`COLCON_IGNORE`** 忽略整个目录 ——
它是**参考代码，不参与构建**，别删那个文件（见 AGENTS 陷阱 #31）。

## 文件

```text
motor_driver_hardware/
├─ include/motor_driver_hardware/
│  ├─ dm_frames.hpp    协议：帧构造 + 收帧切分 + 解码
│  ├─ dm_serial.hpp    SerialIo 接口 + SerialPort（termios）
│  ├─ dm_bus.hpp       MotorBus：收发、缓存、sync_states、寄存器 I/O
│  └─ dm_joint.hpp     JointConfig + Joint：换算/限位/使能/状态
├─ src/                对应四个 .cpp
└─ test/
   ├─ test_dm_frames.cpp   与 Python dm_frames 逐字节对拍（34 用例）
   ├─ test_dm_bus.cpp      假串口走通收发管线（8 用例）
   └─ test_dm_joint.cpp    与 Python joint.py 对拍换算/发帧/保持帧（5 用例）
```

**为什么 C++ 侧只有 POS_VEL**：ros2_control 这条链的命令接口是 `position`，背后就是 POS_VEL；
MIT / 力位混控在 Python 侧已经跑通且够用，搬过来只会变成没人测的死代码。要加是后续的小事。
