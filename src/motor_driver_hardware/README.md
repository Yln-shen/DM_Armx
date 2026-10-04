# motor_driver_hardware —— DM_Armx 的 C++ 侧

当前只有**协议层**（帧编解码）；串口 / 总线层、`ros2_control` 硬件接口在后面几步加（见根目录 `AGENTS.md` 的路线）。

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
├─ include/motor_driver_hardware/dm_frames.hpp   接口 + 常量（档位/命令字/帧长）
├─ src/dm_frames.cpp                             实现（运算顺序与 Python 严格一致）
└─ test/test_dm_frames.cpp                        与 Python 逐字节对拍 + 边界用例
```
