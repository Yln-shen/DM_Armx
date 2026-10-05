# DmArm 封装层设计 —— 实测数据

> 编号沿用全局编号（见 [`DESIGN.md`](DESIGN.md) 开头的索引表）。
> **这里只放"测到了什么"** —— 设计决策在 `DESIGN.md`，教训在 `LESSONS.md`。

> ⚠️ **台架边界，读任何数字前先记住**：
> 已测的全部是**单电机、裸电机空载**（4310 与 4340P 各一只）。
> 整臂 7 关节同时跑、POS_VEL 模式、带负载 —— **都还没测**。
> `joint.py` **至今没有跟真电机说过话**（下一步是 D3 真机 MIT 点动）。

### 2.7 实机实测基线（2026-09-19，单只 4310，裸电机空载）

硬件：适配器 `2e88:4603 HDSC CDC Device`（= `/dev/ttyACM0`），24 V 供电，用户已在 `dialout` 组。

**① 1:1 请求-响应规律（本节最重要的一条）**

> **任何一帧送达电机，无论什么类型，都会恰好产生一个完整反馈帧；什么都不发，就一帧反馈都没有。**

验证方式：`jog --mit` 三个阶段共发 697 帧，收到 697 帧，**零丢帧**（每阶段实测都是 50.0 Hz）。
推论 → 改写 D2：

- 控制帧自己就带回反馈，**不需要 reBot 那条独立的 100Hz 刷新帧**；
- 于是"发 500Hz / 收 100Hz"这个解耦**没必要**，500Hz 发就是 500Hz 收，反馈率白涨 5 倍；
- 前提是**同步收发要写对**（见下面第 ④ 条），否则就会读到上一帧的旧值。

**② 寄存器回读（14 个，全部一次成功、重试 0 次）**

| 项 | 实测值 | 说明 |
|---|---|---|
| `Gr` (0x04) | **10** | 确认是 4310（4340P 应为 40） |
| `PMAX/VMAX/TMAX` (0x15/0x16/0x17) | **12.5 / 30 / 10** | 与 SDK `Limit_Param` 表的 DM4310 完全一致 → MIT 帧的映射范围就用这组 |
| `KP_APR` (0x1A) | **54** | 达妙出厂默认，**不是** reBot 的值 → 佐证 D7「PID 先读后写可回滚」是必要的 |
| `TIMEOUT` (0x09) | **0** | ⚠️ **看门狗是关的**（D4 要写的那个） |
| `MST_ID` (0x15?) | **0** | 反馈 CAN ID 实测就是 `0x000`，不是 `0x10+SlaveID` |
| `CTRL_MODE` (0x0A) | **1** | = MIT。所以本次点动**不需要写任何寄存器** |
| `VBus` / `Tpcb` / `Tmt` | 24.21 V / 29.66 ℃ / 27.48 ℃ | 温度反馈可读 |

- 电机 ID 实测是 **`0x01`**（不是脚本默认的 `0x04`）；扫描 ID 1–8 与 `0x0A`，**只有 `0x01` 有应答**。
- **§十第 2 条的悬空问题有答案了**：`0x3C/0x3D/0x3E` 可以用**裸 int RID** 直接读（`read_motor_param(motor, 60/61/62)`），不必手工构造读取帧。
- `monitor` 5 Hz 跑 10s，丢帧率 **0%**。

**③ MIT 闭环的真实行为：稳态残差 = 静摩擦 / kp**

裸电机给正弦指令（±0.2 rad @ 0.5 Hz，`kd=0.5`），扫 kp：

| kp | 实测幅值 / 指令 | 相位滞后 | 回 p0 残差 | 摩擦/kp 上界 | 峰值力矩 |
|---|---|---|---|---|---|
| 1.0 | 38% | 明显（>60°） | 0.0794 rad | 0.145 | ~0.144 |
| 5.0 | 85~87% | 18° | 0.0252 rad | 0.029 | 0.154 |
| 10 | 93% | 18° | 0.0145 rad | 0.0145 | 0.149 |
| 15 | 96% | 0° | 0.0046 rad | 0.0097 | 0.159 |
| 25 | 98% | 0° | 0.0034 rad | 0.0058 | 0.144 |

两条读出来的结论：

1. **残差 ≤ 摩擦 / kp** —— 不是"="，是**上界**。上位机做 PD 时，误差一路收小到 `kp·err` 顶不动静摩擦，轴就**停在那一瞬间**了，所以终值可以比边界更小（粘滑落点随机）。五档逐点核对全部满足：0.0794≤0.145、0.0252≤0.029、0.0145≤0.0145、0.0046≤0.0097、0.0034≤0.0058。
   推论：**MIT 下永远有静态误差，加大 kp 只能把上界压小、压不到 0**（要压到 0 得有积分项，而那是固件的事）→ 这就是常规运动不能用 MIT 的根本原因。
   （所以别拿 `kp × 残差` 当摩擦的估计：残差被粘滑落点随机化，那个乘积在 0.069~0.145 之间乱跳，不是常数。）
2. **峰值力矩在 5 档 kp 下都是 0.144~0.159 N·m，不随 kp 变** → 它不是"kp 出多大力"，而是"这根轴有多粘"。**裸 4310 输出轴静摩擦 ≈ 0.145~0.159 N·m**（约额定 3 N·m 的 5%，TMAX=10 的 1.5%）。这是后面重力补偿和整臂标定要用的真数，比查手册有用。
   - **别用「平均跟随误差 × kp」去反推摩擦**：那个量里混着动态滞后误差，会随 kp 变大而变大（实测 0.299 → 0.412），不是常数。我第一版就是这么写错的，已删。

→ 这两条**正面支持 D3 的「常规路径全走 POS_VEL 固件闭环」决策**：POS_VEL 是电机内部闭环（有积分），稳态误差能到 0，而且不需要上位机出那点力矩去顶摩擦（4310 是 0.15、**4340P 是 0.62 N·m**，见 §2.8）。MIT 只留给"需要重力前馈"的场合（D8）。

**④ 一个把我坑了 1.6 秒的坑：`read_all()` 是非阻塞的**

首版 `jog --mit` 的 `send()` 写成：

```python
ser.write(frame)
frames = [f for f in extract_rx(ser.read_all()) if f[1] == FEEDBACK_CMD]   # ← 错！
```

`read_all()` 立刻返回当前缓冲区内容、**不等**。写下去到反馈回来有 ~1ms，这一句必然读到**上一帧的反馈**或者空。表现：前 1.6 秒（~80 帧）一切正常，然后缓冲区凑巧被抽干的某一刻返回空 → 被判定成"收不到反馈，立即停止"并失能。**电机全程完全正常**（位置不漂、ERR=1、温度 28℃ 稳），是读法错了。

修法（`dm_bringup.py`）：

- `RxBuf`：带**残留**的接收缓冲。尾部不足 16 字节的残片**留到下次拼接**，绝不丢 —— 丢了会让后面**所有字节错位**（数据里凑巧凑成 `0xAA…0x55` 的 16 字节会被误认成帧）。
- `read_frames(ser, rx, want, timeout)`：轮询 `in_waiting` → 喂 `RxBuf` → 切帧 → 不够就等到 deadline。**不直接 `ser.read(n)`**，那会吃满串口自带的 50ms 超时，把高速循环拖垮。
- `flush_rx`：发命令**之前**丢弃残留，保证随后读到的是**本条命令的应答**，而不是上一条的（否则每次读到的位置/力矩都是 20ms 前的旧值，跳变/超力矩这类安全判断就建立在过期数据上了）。
- 偶发丢帧**先重发同一帧**（MIT 位置指令幂等），连续 `--miss-tol` 次才判链路断。

回归测试：`tools/smoke_dm_frames.py` 第 [5] 节用假串口按 1/5/9/16/17 字节分块喂 3 帧，必须一帧不少；同一节还留了个"反面教材"——逐块直接 `extract_rx` 在 5 字节/块下**一帧都切不出来**（0/3），把病因钉死。

### 2.8 实机实测基线（2026-09-19，换 4340P 之后）

把电机换成 **4340P**（也是 ID `0x01`，`Gr=40`）重跑同一套。工具新增 `tools/scan_bus.py`
（扫 ID + 认型号，只发查询帧），因为后面 7 个关节要陆续上线。

**① 只读转储（`read --id 0x01 --type DM4340`）**

| 寄存器 | 值 | 说明 |
|---|---|---|
| `Gr` (0x14) | **40** | 确认 4340 系列（1:40） |
| `PMAX/VMAX/TMAX` (0x15/16/17) | **12.5 / 10 / 28** | **MIT 映射范围**，与 SDK 表 `DM4340=[12.5,10,28]` 一致 |
| `KP_ASR/KI_ASR` (0x19/0x1A) | 0.00384 / 0.002 | 速度环 |
| `KP_APR/KI_APR` (0x1B/0x1C) | 54 / 0 | 位置环，与 4310 **相同**（达妙出厂默认，非 reBot 值） |
| `TIMEOUT` (0x09) | **0** | 看门狗仍**关着** |
| `MST_ID` (0x07) | 0 | 反馈 CAN ID 实测就是 `0x000`（不是 `0x10+ID`） |
| `CTRL_MODE` (0x0A) | 1 | MIT → `jog --mit` **零寄存器写入** |
| `VBus` (0x3C) | 24.27 V | 24V 供电正常 |
| `Tpcb` / `Tmt` (0x3D/0x3E) | 29.3 / 27.1 ℃ | 冷机 |

**② 静摩擦扫描（裸电机空载，amp 0.2 rad @ 0.3 Hz，钳位上限 5 N·m）**

| kp | 幅值 | 相位滞后 | 峰值力矩 |
|---|---|---|---|
| 2（amp 0.15） | **0%**（纹丝不动） | — | 0.321（**下界**：没动，说明摩擦 > 0.32） |
| 10（amp 0.15） | 80% | 11° | 0.663 |
| 15 | 91% | 22° | 0.527 |
| 25 | 94% | 11° | 0.581 |
| 60 | 97% | 11° | 0.718 |

- **裸 4340P 输出轴静摩擦 ≈ 0.53~0.72 N·m（均值 ~0.62）**，落在 0.15 × (40/10) = **0.60** 上 —— 4310→4340P 摩擦比与减速比 4 倍精确吻合。这条交叉验证让"峰值力矩 = 该轴静摩擦"这个判据更可信了。
- 但**离散度比 4310 大**（±16% vs ±5%）。原因大概是 TMAX 从 10 涨到 28 → 力矩 1 LSB 从 4.9 变 **13.7 mN·m**（量化更粗，只占 2%），且 40:1 的粘滑更明显。所以这个数应当读成"约 0.6 N·m 量级"，不要当 1% 精度的标定值。
- 全程峰值 ≤ 0.72 N·m = TMAX 的 **2.6%**、额定 12 N·m 的 **6%** → 裸电机测试是安全的（即便机身没夹紧也拧不动什么）。
- **工程结论：4340P 要跟得好需要 kp ≈ 25~60，4310 只需 10~25。** 直接后果：reBot 的 `kp=7.0` 不能照抄到 4340P 关节。

**③ 1:1 规律复现（不是 4310 的个案）**

- `monitor --duration 5`：发 249 / 收 249，**0 丢帧**，实测 49.8 反馈/s（目标 50）
- `jog --mit` 每次：331~332 帧，零丢帧，实测 50 Hz
- `bandwidth --hz 4000`：167% 标称容量下 1:1 仍 **100.0%**（20126/20127）

→ D2 的 v0.4 更正（可以去掉独立刷新帧）**对两种型号都成立**。

**④ 带宽天花板（`bandwidth` 不加 `--enable`，纯发帧）**

| 目标 | 控制帧实测 | 占标称容量 | 1:1 |
|---|---|---|---|
| 500 Hz | 483 帧/s（97%） | 29% | 100.0% |
| 1000 Hz | 935（93%） | 51% | — |
| 2000 Hz | 1773（89%） | 93% | — |
| 4000 Hz | **3255（81%）** | **167%** | 100.0% |

- **「111% 超载」那套按帧长算的账作废**：标称 921600 对 CDC-ACM 是装饰性的，实测 154 KB/s 照跑。v0.2/0.3 里围绕"超载"的所有担心（含官方 `USAGE.md:110` 那条 1~2ms 间隔的旁证）都**不再构成风险**。
- **但帧率有天花板，而且是硬账**：冲 4000 Hz 只到 **3255 控制帧/s**（单适配器 + 这个 Python 循环）。**7 关节 × 500 Hz = 3500 → 刚好够不着**；反推每关节上限约 **465 Hz**，而这还是**纯发帧**的账，真机每轮要跑 7 个关节的控制律，实际更低。
  → **架构含义：单总线 7 关节按 500 Hz 设计是不安全的，应按 200~300 Hz/关节设计**（比例外推 7×300×46B ≈ 96.6 KB/s，不到实测天花板的 1/3），或者分两条总线/用 CAN-FD。这条要写进 D2。
- 写延迟（`ser.write` 阻塞时间）：均值 0.11~0.13 ms、p99 0.18~0.28 ms，但 **max 到过 5.4 ms** → 偶发的毫秒级抖动是真的，硬实时循环不能假设写是即时的。

---

---

## 附：看门狗（0x09）台架现状

> ⚠️ **看门狗（0x09）台架现状 —— 2026-09-23 18:4x 逐台实测，不是抄来的：**
>
> | ID | 型号 | `0x09` | 在哪儿 |
> |---|---|---|---|
> | 0x01 | 4340P | **750 ms** | **flash（跨断电验证过）** |
> | 0x02 | 4340P | 0 | — |
> | 0x03 | 4340P | 0 | — |
> | 0x04 | 4310 | 0 | — |
> | 0x05 | 4310 | 0 | — |
>
> **这一行文字本身犯过一次错，值得留着**：本块最初的版本写的是"全仓库只有 `0x01` 配了 500ms
> 看门狗"，那是**照抄旧文档没去核** —— 实测 `0x01 = 0`。`0x01` 的 500ms 是 09-20 写进 flash 的
> （当时跨断电验证过，记录没错），后来调试 ERR=13 时一次**带 `--save`** 的 `0x09 = 0`
> 把它悄悄覆盖了。**硬件就在手边，别抄文档。**
>
> **紧迫性要说准**：现在这 5 台全是**失能**状态，没有上电出力的路径，所以"没有看门狗"此刻
> **不构成现实风险**。它**只在你自己写了控制循环之后**才变成风险 —— 换句话说，
> **补看门狗（C3）应该和真机 MIT 循环（D3）一起做，不是它的前置条件。**
> （早先把这条说成"上位机一崩就会一直出力"，那个紧迫性是错的。）

> 🔴 **再往前一步（真机 MIT 循环）之前，必须先过 `## 决定点`** ——
> 那是本项目第一次不可逆的物理风险（撞限位），且 `Joint` 是薄类**不拥有循环**，循环得另写。

---

## 十、上电必须验证的清单（纸面推导 vs 实测）

图例：✅ 已实测（结果见 §2.7 / §2.8）　🟡 部分验证　⬜ 仍待验证
**注意**：已测的都只是**单电机、裸电机空载**（4310 与 4340P 各一只）。整臂 7 关节同时跑、
POS_VEL 模式、带负载 —— 都还没测。

| # | 待验证 | 状态 / 结果 | 若不符的应对 |
|---|---|---|---|
| 1 | **串口带宽是否真的够**（按帧长算 111%，超载） | ✅ **"超载"证伪**：实测跑到标称容量 **167%**（154 KB/s）仍通、1:1 不破 → 标称波特率是摆设。**但帧率有硬天花板 ≈ 3255 控制帧/s**，7×500Hz=3500 够不着 → **默认速率下调到 200~300 Hz/关节** | 见 §2.8 ④；退路 ①→④ 逐级降频，或改 SocketCAN |
| 2 | 4 号坑：`read_motor_param` 传裸 int（如 0x3C=60）能否读到 VBus | ✅ **能**，`60/61/62` 直接可用，VBus=24.27V | 自己构读帧（`__read_RID_param` 的形状） |
| 3 | 4340P 的 0x15/0x16/0x17 回读值 | ✅ **已读 = 12.5 / 10 / 28**（4310 是 12.5/30/10）。**已确认这不是"与手册矛盾"** —— 它是 MIT 映射范围，与物理峰值 40 N·m 是两回事，见 D5 | 以回读值编解码；动力学用手册值 |
| 4 | 仿件的 `direction` / `offset` | ✅ **2026-10-03 已标定**（6 轴整臂）：逐关节目视零位定 offset、手推 + **通电复核**定 direction、用户摆姿态定软限位；见 §十二 | 第七节标定 |
| 5 | 电机侧 0x09 写入后，停发帧是否真的退出使能 | ✅ **已测**（2026-09-20，`watchdog_test.py` 3/3）：请求 500ms（写入 10000 计数）时**静默 0.3s 仍使能、1.2s 触发 ERR=13 失能**；对照组 `0x09=0` 静默 1.2s 仍使能。独立扫描复验阈值 ∈ (400,800] ms。**顺带纠正：0x09 的单位是 50µs 计数不是毫秒**，见 D4 | 这是安全底线 —— 正反两面都已拿到，D4 已定稿 |
| 6 | POS_VEL 下 `vlim` 的实际效果（梯形匀速度？） | ⬜ 需切 POS_VEL 后才能测 | 手册："速度给定是梯形加减速运行下最高速度，即匀速段速度值" |
| 7 | 位置是否会绕圈（单圈编码器 + 多圈累计） | ⬜ 单次点动范围太小（±0.2 rad），没跑到多圈 | 若会绕，需在收帧侧做 2π 解缠（reBot 只在重力补偿路径做了，见 `hardware_manager.py:406-410`） |

**v0.4 新暴露、必须补测的三件事**（都是这次真机跑出来的）：

| # | 待验证 | 状态 / 为什么重要 |
|---|---|---|
| 8 | **7 关节满载下的 1:1 应答是否仍成立** | ⬜ 单电机（两种型号）都成立；7 路同时发帧时反馈会不会互相挤掉仍未测。§2.8 ④的天花板（3255 帧/s）是**单电机纯发帧**的账，7 关节下的真实上限只会更低 |
| 9 | **4340P 的静摩擦** | ✅ **已测 ≈ 0.53~0.72 N·m（均值 ~0.62）**，是 4310 的约 4 倍（与 40:1/10:1 吻合）。→ 直接后果：**reBot 的 `kp=7.0` 不能照抄到 4340P 关节**，见 D5b |
| 10 | **使能 + POS_VEL 下的 1:1** | ⬜ §2.7/§2.8 的 1:1 都是 **MIT 模式、未使能**下测的。**注意**：`bandwidth` 已意外提供了半个证据 —— 电机在 MIT 模式 + **失能**状态下，收到 POS_VEL 帧照样每帧回一条（167% 那次 100.0%）。所以"不认这个帧也回"已经成立，剩下的是"使能 + POS_VEL 闭环中的回帧行为" |

**v0.5 新暴露的一项：**

| # | 待验证 | 为什么重要 |
|---|---|---|
| 11 | **`ser.write()` 的偶发 5 ms 抖动** | 实测写延迟均值 0.11~0.13 ms、p99 0.18~0.28 ms，但 **max 到过 5.4 ms**。硬实时控制循环不能假设写是即时的 → 控制周期要留余量，或用独立发送线程（D2 的收发解耦其实正好能吸收它） |

---

## 十二、方向 / 零位 / 软限位标定实测（2026-10-03，6 轴整臂，`/dev/ttyACM0`）

**方法**：零位按**本项目自己的定义**（逐关节分别目视摆位）；位置读数走寄存器 `0x50 p_m`（float32，等价于反馈帧 `pos`）；
`direction` 先由"沿各关节自定义的 + 方向手推、看读数符号"得出，**再逐个用通电小动作目视复核**；
软限位由用户摆出两端姿态、读 `p_m` 换算成关节侧。只读阶段只发 `0x33` 读帧（不使能、不写寄存器）；通电复核全程 MIT（`0x0A=1`，**不需要写任何寄存器**）。

| 关节 | offset（零位读数） | direction | 「+」的物理含义（本项目自定义） | 软限位 `[min, max]` | 手推实测 Δ读数 |
|---|---|---|---|---|---|
| joint1 | +1.306486 | −1 | 俯视顺时针 | [−0.096419, +0.137522] | −0.3759 |
| joint2 | +1.594229 | −1 | 末端向上 | [0.0, +2.188275] | −0.3632 |
| joint3 | −0.341649 | −1 | 向上 | [0.0, +1.842548] | −0.4304 |
| joint4 | +1.675259 | −1 | 向上 | [−1.095672, 0.0] | −0.5239 |
| joint5 | +1.278440 | −1 | 从上方看与 j1 相反 | [−0.865009, +1.019013] | −0.3995 |
| joint6 | +2.010141 | **+1** | 面对电机轴看顺时针 | [−3.141593, +3.141593]（±180°） | −1.1241 |

⚠️ **j6 的 `direction` 手推时判反了**（当时报"顺时针"），通电复核发现它实际**逆时针**转 ⇒ 改成 **+1**（其软限位对称，数值不变）。
⇒ **教训：手推定出的符号必须用通电小动作目视复核**，尤其当"+"的描述依赖观察视角时。

**Phase C 通电复核：逐个关节停到软限位两端**（一次只使能一个关节、MIT、50 Hz 喂狗、收尾必失能）

| 关节 | kp | 实测停在（残差） |
|---|---|---|
| joint1 | 15 | max 端差 0.042 rad（≈ 静摩擦 0.62 ÷ kp） |
| joint2 | 30 | min 端差 0.0585 rad（重力 1.76 ÷ kp） |
| joint3 | 30 | min 0.028 / **max 0.008 rad（0.46°）** |
| joint4 | 8.2（按 `torque_max` 自适应） | max 0.0099（0.57°）/ min 0.0034（0.19°） |
| joint5 | 3.0 | min 0.0117（0.67°）/ max 0.0025（0.14°） |
| joint6 | 6.0（全程斜坡） | +180° 0.0006（0.03°）/ −180° 0.0009（0.05°） |

- **`kp` 只能把关节推到离目标"负载 ÷ kp"的地方**；**把稳态残差积进 `tau_ff` 前馈**后能贴到 0.03°~0.6°（j3/j4/j5/j6），总力矩仍受 `torque_max` 钳位。
- j4 曾因"当前位置在软限位外 0.7 rad、一帧指令 PD = 20.9 N·m > `torque_max`=3.5"被 `Joint._clamp_mit_torque()` **拒发**（保护生效、`finally` 立即失能、臂未动）⇒ 从限位外起步必须降 kp 或先手动挪进范围。
- j6 无重力负载、惯量小 ⇒ 用**全程斜坡**（无跳变）+ 角速度守卫（>3 rad/s 停）；实测角速度 0.3~0.5 rad/s。

本次查清的三件事：① `is_reg_response()` 误判（AGENTS.md 陷阱 #26）：id2 停在 +1.79 rad 时 **20/20 条反馈被丢**、缓存里没这台电机；
② 位置真源是 `0x50 p_m`（= 反馈帧 `pos`），**不是** `0x51 xout`（每台差常数 +0.072 / −0.016 / −0.031 / +0.058 / +0.170 / +0.259 rad）；
③ 多圈位置跨断电保持（断电约 8 s 再上电，Δ ≤ 0.00015 rad）。

**待办**：带目标的整臂动作、POS_VEL 下的同样复核、ROS2。

---

## 十三、模型对齐：URDF 坐标系 ↔ 真机电机读数（2026-10-04，M1b）

**要解决的问题**：URDF 的关节零点是**模型几何**定义的姿态，而本项目的零位是**用户在真机上自己摆的**
（`config/joint.yaml` 的 `offset`，且都靠近机械限位）——两者差一个零点差 δ，正方向也可能反号。
没有这一步，URDF 的 joint limit 和 ros2_control 的关节↔电机映射都无从谈起。

**定法（不靠盲扫）**：把真机摆成一个**已知姿态**，一次解出全部 6 组 `(sign, zero_shift)`：

    q_urdf = sign · q_ours + zero_shift
    zero_shift = q_target − sign · q_ours          （用当时的只读读数解）

本次用的已知姿态：**j2~j6 = 参考项目 `initial_positions.yaml`（全 0 = 模型零位）、j1 = 俯视逆时针 90°**
⇒ `q_target = (1.5708, 0, 0, 0, 0, 0)`。当时的只读读数（`q_ours`）：
j1 −0.041076 · j2 −0.191649 · j3 −0.049248 · j4 −0.157540 · j5 +0.048372 · j6 −1.782591。

| 关节 | sign | zero_shift |
|---|---|---|
| joint1 | −1 | +1.529724 |
| joint2 | −1 | −0.191649 |
| joint3 | −1 | −0.049248 |
| joint4 | −1 | −0.157540 |
| joint5 | −1 | +0.048372 |
| joint6 | **+1** | +1.782591 |

**`sign` 的物理含义**：`sign = direction` ⇒ "**模型的正方向 = 电机读数的正方向**"。
依据：2026-10-04 真机观察 —— `sign=+1` 时 j1~j5 的模型转向与真机相反、j6 相同。

**限位换算**（模型坐标）：`lower/upper = sign · (joint.yaml 的 position_min/max) + zero_shift`（取小/大排序）：

| 关节 | 我们标定（关节侧） | → 模型坐标（已写进 URDF） | 上游参考限位 |
|---|---|---|---|
| joint1 | [−0.096419, +0.137522] | [+1.392202, +1.626143] | [−2.8, 2.8] |
| joint2 | [0, +2.188275] | [−2.379924, −0.191649] | [−3.14, 0] |
| joint3 | [0, +1.842548] | [−1.891796, −0.049248] | [−3.14, 0] |
| joint4 | [−1.095672, 0] | [−0.157540, +0.938132] | [−1.87, 1.57] |
| joint5 | [−0.865009, +1.019013] | [−0.970641, +0.913381] | [−1.57, 1.57] |
| joint6 | [−π, +π] | [−1.359002, +4.924184] | [−3.14, 3.14] |

**旁证**：j1~j5 换算后**全部落在上游参考限位内部**（同一套机械设计、同一套模型轴定义）。

**工具**：`arm_bringup` 的 `real_joint_states`（**只读**，绝不 `enable()`；参数默认值取自
`arm_description/config/align.yaml`，所以重启不丢对齐）→ `robot_state_publisher` → RViz 实时镜像真机。
`roadmap`：先看方向（`sign`）再按住调常量（`zero_shift`），从根关节往梢关节调。

**这一步踩的坑（都已进 AGENTS 陷阱清单）**：
1. **设备号会变**：`/dev/ttyACM0` → `/dev/ttyACM1`，而 `channel` 写死 ⇒ 只读节点每轮读失败、**又不退出**
   ⇒ RViz 里模型**冻在最后一帧**，看起来像"调参没生效"（陷阱 #28）。
2. **`joint_state_publisher*` 会订阅 `/joint_states` 再回发**：M1a 的 `display.launch.py` 没关
   ⇒ 同一话题两个发布者打架 ⇒ 模型**发抖**（陷阱 #29）。
3. **`PYTHONPATH` 层级**：裸 import（`from arm_config import ...`）必须指到**模块目录**
   `src/motor_driver/motor_driver`；指到 `src/motor_driver` 会 `ModuleNotFoundError`。
   另外**以包方式 `from motor_driver.arm import DmArm` 也会挂**（`arm.py` 的兄弟导入是裸的，
   只有 `dm_registers.py` 自己插了 `sys.path`）。
4. **`launch_ros.actions.Node` 用 `parameters=`**，不是 `params=`（陷阱 #27）：写错时 launch 在
   **加载文件阶段**就抛，一个节点都不会起。⇒ 改完 launch 先跑 `--show-args` 验证。

**遗留**：我们的标定限位整体偏窄（j1 只有 ±0.1 rad ≈ ±5.5°）；真正跑 MoveIt 时可用范围很小，
大概率需要重新摆一套更宽的限位。

**当天已放宽的一处**：j2/j3 的下界（`joint.yaml` 的 `position_min` 0.0 → **−0.25 / −0.10**），
因为真机"自然停放姿态"是 `q_ours = −0.1916 / −0.0477`，原来落在限位**外** ——
不放宽的话 M5 接真机后第一个"保持当前位置"就会被钳成 11° / 2.7° 的运动。
放宽后换算限位变成 j2 `[−2.379924, +0.058351]`、j3 `[−1.891796, +0.050752]`，
`q_urdf = 0` 就落在里面了（离 j2 上界余量 0.058 rad ≈ 3.3°）。

**M2（mock 链路）当天验过**：`mock_components/GenericSystem` + `joint_state_broadcaster` +
`joint_trajectory_controller`（`update_rate: 100`）两个控制器都 **active**、6 个 `position` 命令接口被 claimed；
发一条两点轨迹（起点 = mock 初始姿态、终点 `(1.45, −1.10, −0.80, 0.35, 0.10, 0.10)`）⇒
`Goal successfully reached!`，且 `/joint_states` 的 position 精确变成该终点值。**全程未接真机。**

**M3（C++ 协议层）当天验过**：新包 `motor_driver_hardware` 的 `dm_frames` 与 Python 的 `dm_frames.py`
**逐字节对拍**通过 —— `colcon test --packages-select motor_driver_hardware` ⇒ `colcon test-result` 报
`6 tests, 0 errors, 0 failures`。34 个用例覆盖：6 类发送帧（含 `q=±PMAX`、`kp=500 / kd=5`、
力位混控两个无符号量的钳位与负值归零）、接收切分（干净流 / 含垃圾 / 尾部残片）、
`RxBuf` 分块喂（残片必须留到下一次）、反馈解码（含 2026-10-03 那条真实 id2 帧 `02 92 55 7f f7 fe 1c 1b`）、
`is_reg_response` 边界（真寄存器回包 vs 停在低字节 0x55 的反馈帧）。**对拍不写"期望字节"，而是直接调 Python 那份**，
所以两份实现漂了就会红（这也是 AGENTS 陷阱 #30 的机器保证）。

**M4（C++ 串口 / 总线 / 关节层）当天验过**：`colcon test --packages-select motor_driver_hardware` ⇒
`colcon test-result` 报 `21 tests, 0 errors, 0 failures`（帧 5 + 总线 8 + 关节 5，另 3 个 ctest 包装）。
- `test_dm_bus.cpp` 用 **`SerialIo` 假串口**（内存字节流）走通"发帧 → 收反馈 → 进缓存"整条链：发送计数与字节数、
  注入真实反馈后的状态与 `decode_feedback` 一致、**寄存器回包与陌生 ID 不污染缓存**、一帧拆成两次 `poll()` 不丢、
  `wait_feedback` 交出**这一台**的原始帧、`sync_states` 先丢陈旧再主动刷新（测试里给假串口装了**应答器**：
  收到刷新帧就回一条反馈，模拟真适配器）。
- `test_dm_joint.cpp` 直接调 Python 的 `joint.py` 比 `prepare_frame`/发帧（6 组不同 direction/offset/档位/边界），
  并验"没缓存位置就拒使能（一个字节都不发）"、"使能后补**用电机侧实测值**的保持帧（vlim=0.1）"、
  "故障先失能再抛（ERR=13 提示锁存）"、"同一台电机档位不同就拒构造"。
- 踩到并修掉的两处**测试自己**的问题（记下来免得下次再犯）：假串口要遵守与真 `SerialPort` 同一契约（**写要计数**）；
  `sync_states` 会先 `flush()`，所以预注入的帧会被丢掉 —— 得用应答器而不是预填。

**M5（ros2_control 插件）代码侧验过**：`colcon test --packages-select motor_driver_hardware` ⇒
`27 tests, 0 errors, 0 failures`（帧 5 + 总线 8 + 关节 5 + **插件 5** + 4 个 ctest 包装）。
插件测试（`test_dm_system_interface.cpp`）在**假串口里塞了一台模拟电机**：收到 POS_VEL 就把位置跟过去、
收到刷新就回状态、收到使能帧置 ERR=1、寄存器帧回显 RID。五条：①只读模式**一个控制帧都不发**（只发 0x7FF），
状态仍能读出来且模型侧 ≈ 0（= 真机停放姿态）；②激活时**先写 `0x0A=2`、再使能、再补保持帧**（保持帧用
**电机侧实测值** + vlim 0.1）；③命令是 NaN 时**继续发保持帧**（绝不当 0）；④**模型坐标 0 ⇒ 电机侧 1.785878**
（M1b 那天从真机读数解出来的那个数，闭环读回来也对得上）；⑤ERR=0x0D ⇒ `read()` 报 ERROR，`on_deactivate()` 全失能。
另外验了参数错误（缺 motor_id / sign=0 / vlim=0）在 `on_init` 就拒，档位与软限位矛盾在 `on_configure` 拒。

**真机三步验收（还没做，等用户操作）**：① `real_control.launch.py`（默认 `enable_on_activate=false` +
只起状态广播器）→ 用真机真实角度驱动 RViz，与模型对照；② `enable_on_activate:=true` → 只保持、零位移；
③ 再加 `spawn_arm_controller:=true vlim:=0.5` → 发几度的小轨迹。

**M5 踩到的两个环境/API 坑（已进 AGENTS 陷阱表）**：① 链接 rclcpp 的**可执行文件**报
「找不到 `-lcap` / `-llttng-ust`」—— 库在 `$CONDA_PREFIX/lib` 但 pixi 没设 `LIBRARY_PATH`，
CMake 里补 `link_directories($ENV{CONDA_PREFIX}/lib)`（共享库不受影响）；② `hardware_interface` 的
`on_init(const HardwareInfo&)` 在 jazzy 已 deprecated，换 `on_init(const HardwareComponentInterfaceParams&)`。
测试自己又犯了两次：期望值用了**未量化**的位置（真机反馈是 16 位定点，差 6e-5 rad）；
以及想验"min>max"却把 min 设成 2.0（j2 的 max 是 2.188，本来就合法）。

**M5 真机验收 ①（只读）2026-10-05 通过**：`ros2 launch arm_bringup real_control.launch.py`
（默认 `enable_on_activate=false` + 只起状态广播器）⇒ **RViz 里的机械臂与真机的方向、位置完全一致**
（j1 ≈ +1.5708、j2~j6 ≈ 0，模型坐标）。这条链整条通了：
`joint.yaml`/`align.yaml` → xacro 生成 `<param>` → C++ 插件 → 真机串口 → `/joint_states` → RViz。

为走到这一步排掉的三件事（都已进 AGENTS 陷阱表）：
1. **串口设备号又变**（#28）：适配器从 ACM1 变回 **ACM0** ⇒ 打不开串口 ⇒ 硬件没激活 ⇒ RViz 停在零位姿，
   看起来像"模型不对 / 只有 j1 差 90°"（真机 j2~j6 本来就 ≈0、只有 j1 是 +90°，所以特别容易误判）。
2. **插件被编成了静态库**（#34）：`add_library()` 默认产出 `.a`，而 pluginlib 只能 `dlopen` `.so` ⇒
   CM 报 `LibraryLoadException … Could not find library … 'dm_system_interface'`。
   **单元测试全绿也照样漏**（测试直接链静态库、从不 dlopen）⇒ 排查入口：`ls install/<pkg>/lib/`
   （看是 `.a` 还是 `.so`）与 `~/.ros/log/latest/launch.log`（launch 的 stdout/stderr 都在那儿）。
3. ROS 守护进程的**幽灵节点**（daemon 缓存）让 `ros2 node list` 显示出多套 mock，误导过一次判断 ⇒
   看进程用 `ps -eo pid,etime,args | grep ros2_control_node` 更可靠。

**M5 真机验收 ②（使能保持）2026-10-05 通过**：`enable_on_activate:=true`（只起广播器）⇒ 日志
`已使能 6 台（保持帧 = 电机侧实测位置 + vlim 0.10）`，**机械臂零位移**（保持帧用电机侧实测值，不朝零位冲）。

**M5 真机验收 ③（小动作）2026-10-05 通过**：`enable_on_activate:=true spawn_arm_controller:=true vlim:=0.5`
⇒ `list_controllers` 里 `arm_controller(joint_trajectory_controller) active` + 广播器 active；发一条 3 秒小轨迹
（j6：0 → 0.10 rad）⇒ goal 成功，`/joint_states` 实测 `[1.5594, ~0, ~0, ~0, -0.0008, 0.0992]`
—— **j6 到位、其余关节纹丝不动**，日志无拒发、无 ERROR。

⚠️ 这一步踩过一个坑：**两套 launch 抢同一个串口**（②那套没关就起③ ⇒ 看着像"发轨迹不动"，
其实是 JTC 不在、`send_goal` 没有 action server）。根因是 Linux 默认允许重复打开同一个 tty ⇒
已在 `SerialPort::open()` 设 **`TIOCEXCL`**（第二家直接 EBUSY 失败），并在报错里提示"先 Ctrl-C 旧的那套"。

**M5 结论**：`MoveIt → ros2_control` 路线上的**执行端（硬件接口）已在真机跑通**：
只读镜像 ✓ → 使能保持 ✓ → 轨迹小动作 ✓。下一步 M6：MoveIt（SRDF / kinematics / ompl）。

---

## 十四、M6（MoveIt）进行中：配置已写，mock 规划还没跑通（2026-10-05）

**已完成**：新包 `arm_moveit_config`（纯数据）：`config/arm.srdf`（只一个规划组 arm = base_link→gripper_tcp 链；
**本阶段不做夹爪**；碰撞对只关相邻链节）、`config/kinematics.yaml`（KDL）、`config/ompl_planning.yaml`（RRTConnect）、
`config/moveit_controllers.yaml`（simple controller manager → `arm_controller` 的 FollowJointTrajectory）、
`launch/move_group.launch.py`（显式拼 MoveIt 参数；`use_mock:=true` 时自己起 mock 硬件+控制器）、
`rviz/moveit.rviz`（MotionPlanning 面板）。`pixi.toml/lock` 补了 6 个 MoveIt 包（都已装上）。

**还没跑通**：mock 规划链起不来，两个各自独立的原因：
1. **`ros2_control_node` 会从共享话题 `/robot_description` 订阅"别人的" URDF** ⇒ 危险！
   当时用户那套 `real_control.launch.py` 还开着，它的 robot_state_publisher 发的是真机版 URDF，
   我这个 mock CM 订阅到之后**加载了真机插件、开了真机串口、把 6 台电机使能了**，
   随后 `关节 joint1 故障 ERR=13`（锁存，只能断电清）并自动失能退出。
   ⇒ 已在**两个 launch** 里把 CM 对 `robot_description` 的订阅重映射到私有话题
   （`dm_armx_local_description`），CM 只用自己参数里的 description。教训见 AGENTS 陷阱 #35。
2. **move_group 起不来**：先是 `request_adapters` 写成了折叠字符串 ⇒
   `ParameterTypeException: expected [string_array] got [string]`（已改成 YAML 列表）；
   改完再试又变成 `Planning plugin name is empty or not defined in namespace 'ompl'` ⇒
   我拼参数的方式（把整份 yaml 塞进 `{"ompl": ...}` 嵌套 dict）没让 `ompl.planning_plugin` 生效，
   **下一步要对着 move_group 实际收到的参数查**（把 dict 形式改成扁平键 `ompl.planning_plugin` 之类，
   或改用 `moveit_configs_utils`）。

**另外**：mock 测试当时还撞上"两套 CM 抢同名控制器"（`Failed to activate controller`），
所以 **mock 与真机这两套任何时候都只能开一套**（含 `move_group.launch.py use_mock:=true` 与
`real_control.launch.py`）。

**M6 续：mock 规划仍未跑通（2026-10-05 晚）**。参数拼法的两次尝试：
1. `{"ompl": {...}}`（嵌套 dict）⇒ `move_group` 抛
   `Planning plugin name is empty or not defined in namespace 'ompl'`；
2. 改成**扁平键** `ompl.planning_plugin` / `ompl.request_adapters` / `ompl.arm.*` 之后，
   `move_group` **仍然没能进入 node list**（起来即退），原因见 `/tmp/m6.log`（已归档到本条下面）。
⇒ 下一步建议（给下一个 AI）：**直接用官方 `moveit_configs_utils.MoveItConfigsBuilder`** 生成参数
（它会把 `planning_pipelines` / `ompl.*` / `robot_description_kinematics` / 控制器映射按 MoveIt 期望的
结构摆好），别自己手拼；起的时候先只看 `ros2 node list | grep move_group`（`--show-args` 只能验语法）。

⚠️ 另记一条操作教训：`pkill -f "ros2 launch"` / `grep "[m]ove_group"` 这类**模式会匹配到"正在执行这条命令的 shell 自己"**
（命令行里就含那个字面量）⇒ 自杀了两次。要杀就用**带方括号的写法**（`move[_]group`）且别在命令行里出现裸字面量。

**M6 里程碑：move_group 跑起来了（2026-10-05 深夜）**，三个坑依次排掉（都是 Jazzy/MoveIt 2.12 与老教程不同处）：
1. 规划插件参数必须是**复数数组** `planning_plugins: [ompl_interface/OMPLPlanner]`
   —— 单数 `planning_plugin` 不报错但读出为空 ⇒ `move_group` abort "Planning plugin name is empty"；
2. 请求适配器前缀是 `default_planning_request_adapters/`（不是 `..._planner_...`），
   且可用名字以错误信息里列出的为准：CheckForStackedConstraints / CheckStartStateBounds /
   CheckStartStateCollision / ResolveConstraintFrames / ValidateWorkspaceBounds；
3. **`AddTimeOptimalParameterization` 在 Jazzy 属于 `response_adapters`**（放进 request 会报 class 不存在）。
   另外 `moveit_configs_utils` 会**无条件**读 `config/joint_limits.yaml` ⇒ 该文件必须存在。

现状：`ros2 node list` 里 move_group ×3 ✓；`plan_only` 的 MoveGroup goal 能应答 ✓，但结果 **ABORTED、轨迹为空**，
`planning_time≈0.045s`（一进去就退出）⇒ 大概率**起始状态被判自碰撞**（`arm.srdf` 目前只关了相邻链节，
是刻意的保守选择）。**下一步**：从日志里抓具体碰撞对（`Found a contact between 'linkX' and 'linkY'`），
补进 `arm.srdf` 的 `disable_collisions`（或改用 Setup Assistant 生成的列表）。

**M6 诊断结论（2026-10-05 深夜）**：`plan_only` 返回 ABORTED **不是规划失败**，而是
**响应适配器失败**：
> ⚠️ **本段已被下面的"结案"段取代**：速度限位缺失是**真因**，但那条 `Invalid max_velocity_scaling_factor 0.000000`
> 只是**无害告警**（TOTP 把 0 当 1.0），**不是**原因；"排查顺序"的三步请按结案段的对照实验来读。

```
[WARN]  time_optimal_trajectory_generation: Invalid max_velocity_scaling_factor 0.000000 → defaulting to 1.0
[ERROR] Response adapter 'AddTimeOptimalParameterization' failed to generate a trajectory.
[ERROR] PlanningResponseAdapter 'AddTimeOptimalParameterization' failed with error code FAILURE
```

⇒ OMPL 规划出了路径 ✓，**时间参数化（TOTP）那一步失败** ⇒ 整条 plan 被判失败。头号嫌疑：
**关节速度限位为 0 或缺失**（TOTP 必须要速度/加速度上限）。排查顺序：
1. `xacro src/arm_description/urdf/arm.urdf.xacro use_mock:=true | grep -A1 '<limit'` —— 看 `velocity=` 是不是 0/没写；
2. `config/joint_limits.yaml` 现在是 `has_velocity_limits: false`（本意是"用 URDF 的值"）——
   若 URDF 里根本没写或写了 0，就得在这里显式给 `has_velocity_limits: true` + `max_velocity`；
3. goal 里的 `max_velocity_scaling_factor` / `max_acceleration_scaling_factor` 传 0 会退化成 1.0（现在就是这个），
   想限速就显式传 0.1~0.5。

⚠️ 另一个坑（这次浪费了时间）：**mock 测试必须确认没有真机 CM 在跑**。我用 `grep DM_Armx` 过滤进程时
漏掉了真机 CM —— 它的 cmdline 是 `.../ros2_control_node --ros-args --params-file /tmp/launch_params_xxx`，
**不含 `DM_Armx`** ⇒ 真机 CM 一直活着，mock 的 `/joint_states` 因此是**真机姿态**，与 mock 模型不匹配。
正确做法：`ps -eo pid,args | grep ros2_control_node` 拿到 PID 后 **kill 具体数字**（别按模式杀，见下），
或直接确认 `ps -eo args | grep -c ros2_control_node` 为 0 再起 mock。

**M6 结案：mock 上规划 + 执行都跑通（2026-10-05）**。真因**不是**那条 scaling 告警，而是紧跟它的：

    [ERROR] time_optimal_trajectory_generation: No velocity limit was defined for joint joint1!
            You have to define velocity limits in the URDF or joint_limits.yaml

机制：**`joint_limits.yaml` 里只要写了某个关节的条目，它就覆盖 URDF 的限位** —— 所以
`has_velocity_limits: false` **不等于**"用 URDF 的值"（该文件原来的注释正是这么以为的），而是
"这个关节**没有**速度上限" ⇒ TOTP 必须先有速度/加速度上限才能算时间参数 ⇒ 它直接失败 ⇒ 整条 plan ABORTED。

**对照实验（4 格矩阵；每格都断言 `ps -eo comm | grep -c '^move_group$'` == 1 再信结果）**：

| 用例 | `joint_limits.yaml` | goal 的 scaling | `error_code` | `No velocity limit` 错误 |
|---|---|---|---|---|
| A | 新：`has_velocity_limits: true` + `max_velocity` 10/30 | 0.5 | **1 = SUCCESS** | 0 次 |
| B | 旧：`has_velocity_limits: false` | 0.5 | 99999 = FAILURE | 1 次 |
| D | 旧 | 0.0 | 99999 = FAILURE | 1 次 |
| C | 新 | 0.0 | **1 = SUCCESS** | 0 次 |

⇒ 唯一自变量是**速度限位**；`scaling_factor = 0` 与失败**无关**（C 用 0 照样成功）。
`max_velocity` 取 `motor_driver/config/joint.yaml` 的 `limit[1]`（就是输出轴 rad/s：j1~j3 4340P → 10、j4~j6 4310 → 30）。

**mock 上 plan + execute 也通了**（`plan_only: false`，同一个关节空间目标，`/joint_states` 实测）：

    执行前: [1.5100, -1.0000, -0.9000, 0.3000, 0.0000, 0.0000]
    目标:   [1.4500, -0.8000, -0.7000, 0.4000, 0.1000, 0.3000]
    执行后: [1.4602, -0.7947, -0.7025, 0.3933, 0.1169, 0.2862]   ← 末点落在 0.02 容差内

`error_code.val = 1`，move_group 侧无 ABORTED / 无规划错误（只有一条无关的 "No 3D sensor plugin(s) for octomap"
与 FIFO RT 调度告警）。⇒ **MoveIt → ros2_control → 硬件这条链在 mock 上端到端成立**。

复现（headless；⚠️ launch 与 action 客户端必须在**同一次 shell 调用**里，原因见下）：

    source install/setup.bash
    ros2 launch arm_moveit_config move_group.launch.py use_rviz:=false &
    # 等 launch 日志出现 "You can start planning now!"，再等 ~3 s
    ros2 action send_goal /move_action moveit_msgs/action/MoveGroup "$(cat goal.yaml)"

goal.yaml 骨架：`request.group_name: arm` + `goal_constraints[0].joint_constraints`（6 个关节各给
`position` / `tolerance_above` / `tolerance_below` / `weight`）+ `planning_options.plan_only: true|false`。

⚠️ **两条操作教训（这轮又踩了，都属于"看起来像规划问题、其实是环境问题"）**：
1. **跨沙箱调用不通 FastDDS 的数据面**：把 launch 放进后台 job、goal 客户端放到**另一次** shell 调用里，
   **发现**（`ros2 node/topic/service list`，走 daemon）全部正常，但 `ros2 topic echo` 与任何服务调用
   **收不到一个字节**（`bwrap` 给每次调用独立的 `/dev/shm`，FastDDS 的 SHM 传输断在这一层）。
   ⇒ **launch 和它的客户端放进同一次 shell 调用**（后来文件策略放开、无 `bwrap` 时则无此问题）。
2. **`kill -INT $LPID` 杀不掉 `ros2 launch` 的子进程** ⇒ 上一轮的 `move_group` 还活着，下一轮就有**两个**
   `move_group` 同时应答 action ⇒ 读到的是**被污染实例**给出的结果（这轮真的拿到过假的 `val:1`，
   而当时正在测的那份配置其实失败了）。⇒ ①用 `setsid ros2 launch ... &` 起、收尾 `kill -INT -$LPID`
   （杀整个进程组）；②**每轮先断言 `move_group` 实例数 == 1** 再信结果；③**别用 `pgrep -f "move_group"` 清场** ——
   它会匹配到你自己命令行里的 `move_group.launch.py`（这轮把自己 SIGKILL 了一次）；
   用 `ps -eo pid,comm` 匹配 comm 列才安全。

## 十五、M6 真机：MoveIt 规划 + 执行都跑通（2026-10-05）

链路：`real_control.launch.py`（真插件 `DmSystemInterface`，走 POS_VEL）→
`move_group.launch.py use_mock:=false`（**只起 move_group，不起 mock CM**）→ MoveGroup action。

**① 只读**：6/6 有状态、ERR 全 0。真机起始姿态（模型坐标，`/joint_states`）

    [1.371670, 0.002670, -0.008011, 0.010682, 0.074769, 0.099184]

⚠️ **j1 比 URDF 下界 `1.392202` 低 0.0205 rad** ⇒ 见下条。

**①b 只读复验（锁定保持改动之后，2026-10-05）**：同一条只读链 + `use_mock:=false` 的 move_group ——
插件日志照旧 `enable_on_activate=false（只读）—— 不使能、不发控制帧，只发 0x7FF 刷新帧读状态`；
`/joint_states` **Publisher count = 1**（陷阱 #29）；起始姿态 6 个关节**全在 URDF 限位内**；
`plan_only` **`error_code = 1`**；**规划前后 `/joint_states` 逐位相同**（机械臂一个字节都没收到）；0 条 ERROR/FATAL。
⇒ 锁定保持只动 `write()` 的 NaN 分支，只读路径不受影响。

**② 起始状态越界时，MoveIt 拒绝规划 —— 不是钳位**（本轮最有价值的一条）：

    [ERROR] Joint 'joint1' from the starting state is outside bounds by: [1.37167]
            should be in the range [1.3922 ], [1.62614 ].
    [ERROR] PlanningRequestAdapter 'CheckStartStateBounds' failed, because
            'Start state out of bounds.'. Aborting planning pipeline.

即使越界量 0.0205 **小于** `ompl_planning.yaml` 的 `start_state_max_bounds_error: 0.1`，它**照样失败**
（该参数并没有让适配器"容忍并就近钳位"）⇒ `error_code = 99999`、轨迹为空。
好处是它比"静默钳位"安全：只读模式下**机械臂一个字节没收到**，规划前后 `/joint_states` 逐位相同。
⇒ 教训：**真机规划前先比对"实测姿态 vs URDF 限位"**；越界就先把关节弄回限位内。
本轮处置：电机未使能（无保持力矩）⇒ **手动把 j1 推回 `1.498701`**，零电机命令。

**③ 只读 + `plan_only`**：j1 进限位后 `error_code = 1 (SUCCESS)`，
轨迹首点与真机实测**逐位相同**（没有被钳位），TOTP 正常执行。

**④ 使能但不动**（`enable_on_activate:=true`，不起轨迹控制器）：**0 条 ERROR/FATAL**。
**第一版实现（follow-me）下漂移不是零** —— j1/j3 在重力下**单调蠕动**（2 Hz 采样 20 s）：

    j1: 1.494887 → 1.492216   （Δ = −0.002670 rad = −0.15°，看不到收敛）
    j3: −0.008774 → −0.010300 （Δ = −0.001526）
    j2/j4/j5/j6: Δ = 0

原因：那时 `write()` 的保持帧是 `send_pos_vel(id, bus_->get_state(id)->pos, kHoldVlim)` ——
目标是**每圈重新读到的实测位置**（**follow-me**）：没有回复力，重力把关节压走后指令跟着走。

⇒ **当天修掉**（陷阱 #38）：`on_activate` 把"使能那一刻的电机侧位置"锁进 `JointParams::hold_pos`，
`write()` 一直发它。真机复测同一个 20 s 场景：

| | follow-me（改前） | 锁定（改后） |
|---|---|---|
| j1 | −0.002670，**持续下滑** | **−0.000381（1 LSB），8 s 后钉住、余下 12 s 不动** |
| j3 | −0.001526 | −0.000381（1 LSB） |
| j2/j4/j5/j6 | 0 | 0 |

⇒ 从"**无界蠕动**"变成"**1 LSB 收敛**"。代价：使能状态下**用手推关节会被顶回来**（follow-me 时是"推哪算哪"）；
重力负载下会持续通一点电流。回归用例：`test_dm_system_interface.cpp::HoldTargetIsLatchedNotReread`
（**旧实现下必失败**，已实测：把实现退回 HEAD 跑该用例 ⇒ 报"j2 的保持目标跟着实测值跑了"）。
（M5 记的"零漂移"宜读作"短时 ≤0.003 rad"；当时姿态/时长可能不同。）

**⑤ 小轨迹执行**（`enable_on_activate:=true spawn_arm_controller:=true vlim:=0.5`，goal scaling 0.1）：

| | j1 | j2 | j3 | j4 | j5 | j6 |
|---|---|---|---|---|---|---|
| 执行前（实测） | +1.491835 | +0.002670 | −0.008774 | +0.011445 | +0.055314 | +0.099184 |
| 目标 | 1.45 | −0.08 | −0.09 | 0.09 | −0.02 | 0.00 |
| 规划末点 | +1.468010 | −0.074069 | −0.077233 | +0.105514 | −0.031845 | +0.016218 |
| 实测末点 | +1.467802 | −0.074006 | −0.077440 | +0.104906 | −0.031663 | +0.015641 |

- `arm_controller: Goal reached, success!`、`error_code.val = 1`、硬件链 **0 条 ERROR/FATAL**、move_group 侧无 ABORTED。
- **实测末点 vs 规划末点 ≤ 0.0006 rad（0.03°）** ⇒ POS_VEL 的跟踪精度足够。
- ⚠️ 但"实测末点 ≠ 目标"（j1 差 0.018）：**规划器自己就停在容差球边缘**（goal tolerance 0.02），
  不是硬件没走到。要精确到位就把 `tolerance_±` 收紧。
- 速度匹配：`vlim=0.5` + scaling 0.1 ⇒ TOTP 峰值约 0.22 rad/s < 硬件上限 0.5 ⇒ 硬件跟得上。
  （若让轨迹比 `vlim` 快，硬件会滞后于轨迹，动作会在 action 返回之后才走完。）

**⑤b 改完保持帧之后复测**（同一个 goal、同样的 `vlim=0.5`）：仍然 `Goal reached, success!`、
`error_code = 1`、0 条 ERROR/FATAL；实测末点 vs 规划末点 ≤ **0.00084 rad（0.048°）**
（规划末点距目标 0.009~0.018，仍停在容差球边缘）。⇒ **锁定保持没有破坏轨迹通路**。

**复现（⚠️ 一次只开一套；`move_group.launch.py` 真机时必须是 `use_mock:=false`）**：

    # A：真机链。先只读，逐级放开
    ros2 launch arm_bringup real_control.launch.py use_rviz:=false
    ros2 launch arm_bringup real_control.launch.py use_rviz:=false enable_on_activate:=true
    ros2 launch arm_bringup real_control.launch.py use_rviz:=false enable_on_activate:=true spawn_arm_controller:=true vlim:=0.5
    # B：move_group（**不起 mock CM**）
    ros2 launch arm_moveit_config move_group.launch.py use_mock:=false use_rviz:=false
    # C：发目标（只规划 plan_only: true / 规划并执行 false）
    ros2 action send_goal /move_action moveit_msgs/action/MoveGroup "$(cat goal.yaml)"

## 十六、M6 真机：IK / 笛卡尔位姿目标（2026-10-05）

第一次让**末端位姿目标**上真机（§十五 走的都是关节空间目标）。做法两步：**先只读探 IK，再执行**。

**怎么探**（只读阶段 `enable_on_activate` 默认 false ⇒ 不使能、不发控制帧）：

1. 读末端当前位姿：`ros2 run tf2_ros tf2_echo base_link gripper_tcp`
2. 对"当前位姿 + 六个方向各 3 cm"逐个问 `/compute_ik`（`moveit_msgs/srv/GetPositionIK`：
   `group_name=arm`、`ik_link_name=gripper_tcp`、`avoid_collisions=true`、timeout 0.5 s），
   取第一个 `error_code.val == 1` 的当真机目标。

**① IK 探测：六个方向只有 1 个可解**

    当前末端 x=0.016255 y=0.149039 z=0.189758  q=(-0.034469, 0.046930, 0.666842, 0.742921)
    IK  +1x / -1x / +1y / -1y / -1z 各 3cm  ->  no (val = -31 = NO_IK_SOLUTION)
    IK  +1z 3cm                          ->  OK (val = 1)

⚠️ 避碰是打开的，所以这 5 个 `-31` 是 **KDL 解不出来**，不是撞了。⇒ 机械臂现在这个构型**很受限**
（和 §5 里 j1 只有 0.234 rad 行程是同一件事）—— 想让它"干点活"，得先把它摆到一个手臂伸得开的姿态。

**② 只读 `plan_only`（位姿目标）**：`error_code = 1`、10 个路点，各关节位移
`+0.0412 / -0.0272 / -0.0900 / +0.0178 / +0.0518 / -0.0116`（max **0.0900 rad = 5.2°**）；
轨迹首点与真机实测**逐位相同**（没被钳位）；规划前后 `/joint_states` 逐位相同 ⇒ 只读确实没碰电机。

**③ 执行**（`enable_on_activate:=true spawn_arm_controller:=true vlim:=0.5`，goal `scaling 0.1`）：

| | x | y | z |
|---|---|---|---|
| 执行前实测（`tf2_echo`） | 0.016 | 0.149 | 0.190 |
| 目标 | 0.016255 | 0.149039 | **0.219758** |
| 执行后实测 | 0.016 | 0.148 | **0.221** |
| 误差 | −0.3 mm | −1.0 mm | **+1.2 mm** |

⇒ 末端实际上抬 **3.1 cm**（目标 3.00 cm）。`tf2_echo` 只显示到 mm，1.2 mm 基本就是显示分辨率。

同一轮的关节（`Goal reached, success!`、`error_code.val = 1`、硬件链 **0 条 ERROR/FATAL**）：

    规划首点: +1.4590 -0.0008 -0.0675 +0.1823 -0.0042 +0.0111   ← = 规划时的实测姿态，无钳位
    规划末点: +1.4583 -0.0699 -0.1641 +0.1681 -0.0064 +0.0216
    实测末点: +1.4579 -0.0694 -0.1644 +0.1678 -0.0057 +0.0217
    跟踪差  : -0.0005 +0.0005 -0.0003 -0.0002 +0.0007 +0.0001   max|Δ| = 0.0007 rad（0.04°）

**④ 两条发现**

- **姿态容差会被"用满"**：命令 RPY `[0.011, 0.116, 1.463]` → 实测 `[0.023, 0.073, 1.466]`，
  **pitch 差 0.043 rad（2.5°）**，紧贴 `absolute_*_axis_tolerance: 0.05`（2.9°）。
  ⇒ 想要姿态准就收紧这个容差（代价：IK 更容易解不出来）。
- 位置误差（1.2 mm）远小于"关节容差 0.02 rad 折到末端"的量级 —— 因为**位置约束是个 5 mm 的球**，
  规划器只要落进球里就停。要更准就缩小球半径。

**⑤ 位姿目标的 goal 结构**（本轮探针脚本是临时件，在 `log/real/ik_probe.py`，**未入库**）：

    request:
      group_name: arm
      num_planning_attempts: 10
      allowed_planning_time: 5.0
      max_velocity_scaling_factor: 0.1
      max_acceleration_scaling_factor: 0.1
      goal_constraints:
        - position_constraints:
            - header: {frame_id: base_link}
              link_name: gripper_tcp
              constraint_region:
                primitives: [{type: 2, dimensions: [0.005]}]          # type 2 = SPHERE, r = 5 mm
                primitive_poses: [{position: {x: 0.016255, y: 0.149039, z: 0.219758}}]
              weight: 1.0
          orientation_constraints:
            - header: {frame_id: base_link}
              link_name: gripper_tcp
              orientation: {x: -0.034469, y: 0.046930, z: 0.666842, w: 0.742921}
              absolute_x_axis_tolerance: 0.05
              absolute_y_axis_tolerance: 0.05
              absolute_z_axis_tolerance: 0.05
              weight: 1.0
    planning_options:
      plan_only: true          # 执行时改 false

## 十七、M6 真机：IK 可解性强依赖构型 + "张开手臂"整链（2026-10-05）

§十六 发现当前构型 IK 六方向只有 `+z` 可解 —— 怀疑是构型太"折叠"（j2/j3 都贴在**上界**：
j2 `-0.0008` vs 上界 `0.0584`、j3 `-0.0999` vs 上界 `0.0508`）。这一节是验证与修法。

**① 先用 `/compute_fk` 预测候选（只读，不碰电机）**：对"当前姿态 + 只动 j2/j3/j4/j5 的 6 组增量"
算末端位置，避免"往负方向其实是把手插进桌面"这种事。

    当前             x=+0.0158 y=+0.1440 z=+0.1830  reach=0.1449
    A 轻 (-0.25,-0.30,+0.10)  ->  z=+0.2524  reach=0.1470
    B 中 (-0.40,-0.50,+0.20)  ->  z=+0.2939  reach=0.1526
    C 大 (-0.55,-0.70,+0.30)  ->  z=+0.3329  reach=0.1627   ← 取"伸展最大"
    D/E/F（带肩 roll ±0.25 / ±0.30）  reach≈0.151~0.161

⇒ 往负方向是**抬起来**（z 0.183 → 0.333），不会扎桌面。但伸展只从 **0.1449 → 0.1627（+12%）** ——
这臂末端离基座才 ~15 cm，"打开"的量级有限。

**② 执行 C 大**（j2 −0.55 / j3 −0.70 / j4 +0.30）：`val=1`、`Goal reached, success!`、0 条 ERROR/FATAL。
末端 `(0.0158, 0.1440, 0.1830)` → 实测 `(0.015, 0.161, 0.325)`；FK 曾预测 `(0.0178, 0.1617, 0.3329)`
⇒ 实测与预测差 **≤8 mm**（差值来自关节目标的 ±0.02 容差）。

⚠️ **轨迹速度要先和 `vlim` 对一下**：按 `scaling=0.1` 规划出的峰值速度约 **0.84 rad/s**，**超过 `vlim=0.5`**
⇒ 硬件跟不上、JTC 收尾可能判容差失败。处置是**把轨迹放慢**（`scaling` 0.1 → **0.03**，峰值降到 ~0.30 rad/s），
**不去放大 `vlim`**（`vlim` 是硬件侧的硬上限，是安全边界）。用时约 3.5 s。

**③ 塌回 —— 第一遍"跨运行接着探"失败的原因**：C 执行完、杀掉 launch（= 失能）之后，下一次只读链起来时
姿态已经变成 `[1.4724, +0.0008, -0.0233, +0.0027, ...]`：**j2 掉 0.55 / j3 掉 0.76 / j4 掉 0.63 rad**，
几十秒内自己塌回折叠位（全程无任何命令）。⇒ **张开姿态不是自由状态的稳定点**，
"跑完退出、下次接着来"的流程**不成立**（下次起来时手臂已经不在你以为的地方）。

**④ 保持使能重做：IK 可解方向 1/6 → 4/6**（同一次运行里张开 → **不杀 launch** → 探 IK）

| 方向 3 cm | 折叠位（旧） | **张开后（新）** |
|---|---|---|
| +x | no | no |
| −x | no | no |
| +y | no | **OK** |
| −y | no | **OK** |
| +z | OK | **OK** |
| −z | no | **OK** |

**⑤ 张开构型下执行位姿目标（+y 3 cm）**：

| | x | y | z |
|---|---|---|---|
| 执行前 | 0.015 | 0.162 | 0.326 |
| 目标 | 0.015116 | 0.192220 | 0.326058 |
| 执行后 | 0.012 | **0.195** | 0.329 |
| 误差 | −3.1 mm | +2.8 mm | +2.9 mm |

- 末端**前伸 3.3 cm**（目标 3.0）。三轴误差 ~3 mm，**都在那个 5 mm 位置球内**（规划器一进圈就停，
  所以误差量级由球半径决定；§十六 那次是 1.2 mm，差别只是运气）。
- 关节跟踪 **max|Δ| = 0.0005 rad（0.03°）**；13 个路点、最大关节位移 8.8°（0.1540 rad）。
- 两次 `Goal reached, success!`（张开 + 位姿）、硬件链 **0 条 ERROR/FATAL**。

**⑥ 结论 / 操作要点**

1. **IK 可解性强依赖构型**：折叠位只有 1/6 方向可解，张开后 4/6（±y、±z）。要做笛卡尔作业，
   先把手臂抬起来。
2. **张开姿态要靠"保持使能"撑着**（锁定保持，见陷阱 #38）；失能就塌回折叠位。
3. **规划出来的轨迹速度先和 `vlim` 对一下**：轨迹峰值 > `vlim` ⇒ 硬件跟不上；放慢轨迹，别放大 `vlim`。


---

## 十八、重力补偿（MIT + 前馈）与系统辨识（2026-10-05）

> 这是本工程从"只有 POS_VEL 位置控制"走到"带实机辨识重力模型的 MIT 前馈链"的那一轮。
> 结论与坑的**摘要**在 `AGENTS.md` §5「重力补偿」与陷阱 #40~#49；详细叙述在 `docs/LESSONS.md`。
> 这一节只放**实测值与证据**。

### 18.1 为什么必须先辨识：上游 CAD 惯量不可信

在张开位（**真正靠电机扛重力**的姿态）用 POS_VEL 托住，读电机反馈的保持力矩（`effort` 状态接口），
与 `arm_description` 里上游 CAD 的惯量算出来的重力项对比：

| 关节 | 模型 tau_g | 实测保持力矩 | 比值 |
|---|---|---|---|
| j1 | −0.000 | +0.403 | 模型≈0（j1 轴竖直）⇒ 这 0.40 是摩擦 |
| j2 | −0.227 | **−1.771** | **7.8×** |
| j3 | −6.939 | **−5.272** | **0.76×** |
| j4 | −1.992 | **−0.647** | **0.32×** |
| j5 | −0.002 | −0.012 | ≈0 |
| j6 | +0.000 | −0.081 | ≈0 |

**符号全对、量级非均匀地错** ⇒ 不是标定/单位问题，是每根连杆的惯量都不对。
张开位姿态（模型坐标）：`[1.4407, −0.5570, −0.7031, 0.3140, −0.0198, −0.0114]`。
当时 `Goal reached, success!`、`val=1`、末端误差全在 0.02 rad 内。

### 18.2 `effort` 的尺度标定（做之前别假设 1:1）

方法不需要额外代码：`gravity_ff=true` 时插件发的 `tau_ff` **就是模型算出来的已知量**，
读回 effort 与之相比即可。

| scale | 守卫触发 | 3 s 后位置 | j2 比值 | j3 比值 | j4 比值 |
|---|---|---|---|---|---|
| 0.2 | 0 | 逐位不变 | 1.06 | 0.97 | 0.88 |
| 0.5 | 0 | 逐位不变 | 1.01 | 0.98 | 0.85 |

⇒ **往返 ≈1:1** ⇒ `2·t_max/4095` 的解码是对的（此前怀疑差 2 倍，排除）。
量化台阶实测是 `t_max/4095` 的整数倍。

### 18.3 辨识数据采集

工具（临时，在 gitignored 的 `log/real/`）：`ident_collect.sh` + `_median.py` + `ident_fit.py`。

- POS_VEL + 轨迹控制器自动走 **40 个静止姿态**（随机采样，j2∈[−0.6,−0.1]、j3∈[−0.75,−0.1]、
  j4∈[0.05,0.45]、j5∈[−0.35,0.35]、j6∈[−0.3,0.3]、j1∈[1.43,1.53]），**40/40 全部成功**（无碰撞、无不可达）。
- 每个姿态：**停 4 s**（等固件积分收敛）+ **连采 3 次取中位数**。

⚠️ **第一版是"停 2 s + 采一次"，数据被瞬态污染**。怎么发现的：**同一姿态重复采 5 次**（每次从不同方向靠近）：

| 关节 | 5 次实测力矩 (N·m) | 极差 |
|---|---|---|
| j2 | −1.867 −1.798 −1.812 −1.785 **−0.526** | 1.340 |
| j3 | −5.915 −5.887 **−3.179** −5.682 −5.285 | 2.735 |
| j4 | −0.857 −0.730 −0.901 −0.794 −0.799 | 0.171 |

第 3 次是**整块离群**：j2/j3 **同时**掉了 1.3/2.5 N·m。
**判据**：摩擦只该作用在单个关节上、量级 0.62（4340P 静摩擦），两个关节同时掉 ⇒ 是采到了瞬态，不是摩擦。
剔除后 j3 的极差 = 0.630 ≈ 静摩擦 0.62 ✓。改成中位数采样后，辨识 RMS 从 0.515 → **0.418**。

### 18.4 辨识结果

重力项对每连杆的 (质量, 一阶矩 m·c) 是**线性**的 ⇒ 最小二乘，24 个待识别参数。
⚠️ 回归矩阵是**自己造的**：`pinocchio 4` 的 `computeStaticRegressor` 返回的形状是 `(3, 24)`（6 关节该是 6 行），
不对，没用它。造法：用"只有某一根连杆带单位质量"的辅助模型算出的重力项就是对应的一列。

- **秩只有 10/24** ⇒ 参数不唯一。裸最小二乘（最小范数）给出 `m=0`、`质心 2.2 m` 的**非物理解**，
  而拟合精度与物理解**完全一样**（都是 RMS 0.4177）。
- **向名义值做岭回归**（λ=0.01）⇒ 精度不变、参数回到物理区间（质量 0.16~1.33 kg、|质心| ≤ 0.15 m）。

| | 整体 RMS | j1 | j2 | j3 | j4 | j5 | j6 |
|---|---|---|---|---|---|---|---|
| 上游名义 | 1.323 | 0.428 | 0.965 | 2.826 | 1.176 | 0.096 | 0.096 |
| **辨识后** | **0.418** | 0.428 | 0.434 | 0.808 | **0.070** | 0.094 | 0.094 |
| 留出 8 个姿态 | 0.411 | 0.425 | 0.443 | 0.784 | 0.085 | 0.078 | 0.080 |

- **无过拟合**（留出 ≈ 训练）。
- 远端三个（4310）已打到**摩擦地板**（0.145 静摩擦 ⇒ 均匀分布 RMS 0.084）；
- j1 的 0.428 **全是**摩擦+走线（j1 轴竖直、重力恒 0）⇒ 任何重力模型都消不掉；
- j3 的 0.808 高于它的地板 0.36 ⇒ j3 上有非重力的、随构型变化的载荷（走线）。

辨识出的参数（质心相比名义"模长接近、方向转了约 90°"，说明上游 CAD 的连杆坐标系约定也不一致）：

| link | mass (识 / 名) | com (识) |
|---|---|---|
| link1 | 0.1613 / 0.1613 | (0.0001, −0.0006, 0.0236) |
| link2 | 1.3266 / 1.3266 | (0.0271, 0.1330, −0.0308) |
| link3 | 0.7795 / 0.8353 | (−0.0057, 0.1493, −0.0332) |
| link4 | 0.4299 / 0.5200 | (−0.0256, −0.1053, −0.0367) |
| link5 | 0.2903 / 0.3830 | (−0.1263, 0.0119, −0.0133) |
| link6 | 0.7718 / 0.8663 | (−0.0364, −0.0031, 0.0199) |

⚠️ **写回 URDF 时踩的坑**：`end_link`、`gripper_tcp` 都是**固定关节的子连杆**，pinocchio 会把它们**合并进 `link6`**
⇒ 辨识出的"link6"其实是**三者的合并体**。第一版只改 `link6`、没管子连杆 ⇒ pinocchio 又把它们**加了一遍**
（重复计入）⇒ 生成出来的 URDF **反而更差**（RMS 1.03），而且误差顺着链传到 j2/j3/j4，**看起来像辨识方法错了**。
修法：脚本把固定关节子连杆的惯量清零。修后 0.4177，且与回归预测差 **4.6e-16**。
**排查手法**：把辨识参数**手动设进 pinocchio 模型**再比 —— 若模型对、URDF 不对，就是这一层。

### 18.5 真机端到端验证

`gravity_ff:=true gravity_ff_scale:=1.0` + 轨迹控制器，用 MoveIt 把机械臂送到张开位：

- **守卫触发 0 次**、`val=1`、`Goal reached`
- 到位：j2 **−0.549**（目标 −0.55）· j4 **0.302**（目标 0.30）· j3 −0.566（目标 −0.70，差 0.134
  —— 前馈缺失时 kp=7 下 j3 会差 ~0.7 rad，说明前馈确实在扛）
- 张开位实测保持力矩 vs 模型：

| | j1 | j2 | j3 | j4 | j5 | j6 | 整体 RMS |
|---|---|---|---|---|---|---|---|
| 实测 effort | −0.157 | −1.265 | −5.026 | −0.862 | +0.061 | +0.027 | — |
| **辨识版** tau_g | +0.000 | −1.342 | −4.278 | −0.765 | +0.001 | +0.029 | **0.317** |
| 名义版 tau_g | +0.000 | −0.054 | −7.146 | −1.967 | −0.000 | +0.000 | 1.096 |

⇒ **辨识版准 3.5 倍**。

### 18.6 还差什么（别把它当已完成品）

1. **静态精度**：MIT+前馈做到 j3 差 **0.134 rad**，而 POS_VEL 是 **0.0016 rad**。
   物理下限是"扰动/kp"，要追平必须加**宿主侧积分项**。
2. `kp_hold` / `kd_hold` 还是**占位值** 7.0/0.8，没按关节标定（DESIGN §D5b：4340P 与 4310 差 5 倍以上）。
3. **摩擦前馈**没做（j3 还有 ~0.8 N·m 不可重复扰动）。
4. **安全**：MIT 路径目前只有"残差守卫 + `torque_max` 钳位 + 500 ms 看门狗"，
   **没有**安全认证层、**没有**碰撞检测、**没有**速度/功率限制（见 `AGENTS.md` §5 的说明）。
5. 辨识只覆盖了**影响重力的参数**（质量 + 质心）；转动惯量没辨识
   ⇒ `arm_identified.urdf` **只适合做重力前馈**，别拿它做完整动力学。
