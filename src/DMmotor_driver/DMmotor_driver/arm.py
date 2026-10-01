"""arm.py —— DmArm：整臂（把配置里的每个关节组装成一个 `Joint`，提供批量操作）。

## 它是什么

    连接/关闭、整体使能失能、批量发目标、批量读状态、批量刷新。

## 它不是什么（边界，改之前先读）

- **不拥有控制循环的节拍**：`set_joint_positions()` 只是"这一圈把 6 帧发出去"，
  不是"走到位"。循环（`run_control_loop` / `run_feedback_loop`）本轮**还没实现**，
  所以现在要么调用方自己写循环，要么用 `refresh_all_states()` 手动收帧。
- **不做寄存器 I/O**：不写 `0x0A`（控制模式）、不写 `0x09`（电机侧看门狗）、不配 PID。
- **不碰运动学**：关节角 ↔ 末端位姿不在这一层。
- **不重复限位**：软限位与 PMax 钳位仍在 `Joint` 里做。

## 用法

    cfg = load_arm_config("config/joint.yaml")
    arm = DmArm(cfg).connect()          # 开串口 + 建 6 个关节（不写任何寄存器）
    arm.refresh_all_states()            # 先只读收一帧
    arm.enable_all()
    arm.set_joint_positions({"joint1": 0.5}, vlim=0.5)
    ...
    arm.shutdown()                      # 先失能，再关串口
"""
import sys
import time

from arm_config import ArmConfig
from dm_bus import MotorBus
from dm_modes import MODE_POS_VEL
from joint import Joint, JointState

# 温度 warn 的打印间隔（秒）：warn 不是故障，但 100Hz 下每圈打一条会刷屏
MONITOR_WARN_INTERVAL = 5.0


class DmArm:
    """6 个 `Joint` 的组装。**自己开串口**（见 `connect()`）。"""

    def __init__(self, config: ArmConfig):
        self.config = config
        self.bus: MotorBus | None = None
        self.joints: dict[str, Joint] = {}
        # 监控状态：连续越限计数（按关节名）、warn 上次打印时间（按 关节/传感器）
        self._torque_over_count: dict[str, int] = {}
        self._warn_last: dict[str, float] = {}

    # ───────────────────────── 生命周期 ─────────────────────────
    def connect(self) -> "DmArm":
        """开串口 + 按配置建每个关节。**不写任何寄存器**（不切模式、不配看门狗、不设零位）。

        `Joint` 的构造会把 `limit` 注册到 bus 上（发帧与解反馈都要用它），
        所以这一步之后就"能发帧"了 —— 但**还没使能**，电机不出力。
        """
        if self.bus is not None:
            raise RuntimeError("DmArm 已经连接过了；要重连先 shutdown()")
        self.bus = MotorBus(self.config.channel, baud=self.config.baud,
                            timeout=self.config.serial_timeout).open()
        self.joints = {
            name: Joint(self.bus, cfg.slave_id, cfg.name, cfg.direction, cfg.limit,
                        offset=cfg.offset, position_min=cfg.position_min,
                        position_max=cfg.position_max, mode=cfg.mode)
            for name, cfg in self.config.joints.items()
        }
        return self

    def shutdown(self) -> None:
        """**先失能、再关串口**（守则：不让电机带电离场）。没连接过就直接返回。

        失能失败也会把串口关掉（`finally`），异常照抛 —— 那时的信息是
        "哪个关节没失能成功"，比"串口还开着"更重要。
        """
        if self.bus is None:
            return
        try:
            self.disable_all()
        finally:
            self.bus.close()
            self.bus = None
            self.joints = {}

    # ───────────────────────── 使能 / 失能 ─────────────────────────
    def enable_all(self) -> None:
        """逐个使能。**任一个失败 → 把已经使能的先失能，再抛**（不留"一半带力"）。

        ⚠️ 位置类模式（2/4）下，没 `poll()` 过、缓存里没有位置的关节会**拒绝使能**
        （手册：切进位置类模式时电机内部指令被清零，没有"保持帧"就会朝零位冲）。
        所以正常顺序是：先 `refresh_all_states()`，再 `enable_all()`。
        """
        self._require_connected()
        done: list[str] = []
        try:
            for name, joint in self.joints.items():
                joint.enable()
                done.append(name)
        except Exception:
            for name in done:
                try:
                    self.joints[name].disable()
                except Exception as e:                     # noqa: BLE001 —— 回滚要尽力
                    print(f"[DmArm] ‼ 回滚失能失败 {name}：{e} —— "
                          f"电机可能仍在通电出力，请直接断电", file=sys.stderr)
            raise

    def disable_all(self) -> None:
        """逐个失能：**全部尝试**（不在第一个失败处停下），最后有失败就抛。

        "全部尝试"是有意的：一个关节失能失败，不该拦住其他 5 个被放掉。
        """
        self._require_connected()
        failed: list[tuple[str, Exception]] = []
        for name, joint in self.joints.items():
            try:
                joint.disable()
            except Exception as e:                         # noqa: BLE001
                failed.append((name, e))
        if failed:
            raise RuntimeError(
                f"有 {len(failed)} 个关节失能失败：" +
                "、".join(f"{n}({e})" for n, e in failed) +
                " —— **它们可能仍在通电出力，请直接断电**"
            ) from failed[0][1]

    # ───────────────────────── 批量控制 ─────────────────────────
    def set_joint_positions(self, targets, vlim) -> None:
        """POS_VEL：`targets = {关节名: 关节侧 rad}`，`vlim` 是标量或 `{关节名: vlim}`。

        ⚠️ 这是"这一圈把帧发出去"，**不是"走到位"**：要让关节一直朝目标走，
        就得在循环里反复调（或等 `run_control_loop` 落地）。
        ⚠️ **不在 `targets` 里的关节不动**（保持上一帧的目标）。
        """
        self._require_connected()
        unknown = set(targets) - set(self.joints)
        if unknown:
            raise KeyError(f"不认识的关节名 {sorted(unknown)}；当前有 {sorted(self.joints)}")
        for name, q in targets.items():
            v = vlim[name] if isinstance(vlim, dict) else vlim
            self.joints[name].set_pos_vel(q, v)

    def set_joint_mit_all(self, targets, kp, kd, dq=0.0, tau=0.0) -> None:
        """MIT：`targets = {关节名: 关节侧 q}`，`kp`/`kd`/`dq`/`tau` 对所有关节相同。

        ⚠️ MIT 是**上位机自己闭环**：电机只按最后一帧出力
        `kp·(q_des−q) + kd·(dq_des−dq) + tau`，发一帧 ≠ 走到位，要持续发。
        ⚠️ 需要**按型号给增益**：4310（j4~j6）与 4340P（j1~j3）的 kp 量级差 5 倍以上，
        给同一组值必然一个太软、一个太猛 —— 所以这个"统一增益"的接口只适合点动调试，
        整臂控制请按关节给不同增益（下一轮）。
        """
        self._require_connected()
        unknown = set(targets) - set(self.joints)
        if unknown:
            raise KeyError(f"不认识的关节名 {sorted(unknown)}；当前有 {sorted(self.joints)}")
        for name, q in targets.items():
            self.joints[name].set_mit(kp, kd, q, dq, tau)

    # ───────────────────────── 状态 ─────────────────────────
    def get_state(self) -> dict[str, JointState]:
        """`{关节名: JointState}` —— **只含已经收到过反馈的关节**，缺的不编也不抛。

        多关节刚上电时"只有一部分回过包"是常态（轮询是异步的），
        所以这里用 `bus.get_state()` 先判空，再交给 `Joint.get_state()` 换算 ——
        而不是让 `Joint.get_state()` 抛（它抛是因为"要状态就是要用"）。
        """
        self._require_connected()
        out: dict[str, JointState] = {}
        for name, joint in self.joints.items():
            if self.bus.get_state(joint.motor_id) is None:
                continue                      # 这台还没回过包
            out[name] = joint.get_state()
        return out

    def refresh_all_states(self) -> int:
        """给每个关节发一条 `0x7FF` 刷新帧，然后 `poll()` 一次；返回本轮收到的反馈帧数。

        ⚠️ 回包是**异步**的：6 条帧发完立刻 poll，可能只收到一部分。要拿全就多调几次，
        或等下一轮的收帧循环（100Hz）。它**只读**：不改电机任何状态。
        """
        self._require_connected()
        for joint in self.joints.values():
            self.bus.send_refresh(joint.motor_id)
        return self.bus.poll()

    def sync_states(self, timeout: float = 2.0, *, quiet: float = 0.05) -> dict[str, JointState]:
        """**排空陈旧回包**，然后取一份**新**状态。返回 `{关节名: JointState}`。

        为什么要它：发循环单跑（没人 `poll()`）时，回包会堆在适配器/OS 缓冲里，
        `get_state()` / `check_health()` 读到的是**旧帧** —— 真机实测 `disable_all()` 明明成功，
        缓存却连续两轮还报 `ERR=1`（3 秒 500Hz 压测留下约 9000 条积压）。
        ⇒ **压测或单跑发循环之后，先 `sync_states()` 再信状态**；两个循环成对跑时不需要它。

        做法（只读）：`flush()` → 反复 `poll()` 到「连续 `quiet` 秒没有新帧」→ 再发一轮刷新帧并 poll。
        `timeout` 是整体上限，超时就直接返回当前状态（**不抛**）。
        """
        self._require_connected()
        self.bus.flush()
        deadline = time.monotonic() + timeout
        last_new = time.monotonic()
        while time.monotonic() < deadline:
            if self.bus.poll():
                last_new = time.monotonic()
            elif time.monotonic() - last_new >= quiet:
                break                       # 静了：积压吐完
            else:
                time.sleep(0.002)
        self.refresh_all_states()           # 最后问一次，拿"现在"的状态
        time.sleep(quiet)
        self.bus.poll()
        return self.get_state()

    # ───────────────────────── 健康检查 ─────────────────────────
    def check_health(self) -> None:
        """逐个关节查 ERR（`Joint.assert_healthy()`），**把问题收集齐再一次性抛**。

        每个关节的语义与 `Joint.assert_healthy()` 完全一致：ERR 不在 {0,1} → **先失能那一台**
        再报错；缓存里没有反馈 → 报错（不编一个零值）。所以故障关节在这一步**已经被失能**了。

        ⚠️ 这里**只查 ERR**：力矩与温度由收循环里的 `_monitor_step()` 查（采样率 = `feedback_hz`）。
        ⚠️ 它**不**自动整臂急停：一台关节故障就让整臂一起松掉，在重力负载下更危险 ——
        要不要 `emergency_disable()` 由调用方决定。
        """
        self._require_connected()
        problems: list[tuple[str, Exception]] = []
        for name, joint in self.joints.items():
            try:
                joint.assert_healthy()
            except Exception as e:                         # noqa: BLE001 —— 要收集齐，不能中断
                problems.append((name, e))
        if problems:
            detail = "\n".join(f"  · {n}：{str(e).splitlines()[0]}" for n, e in problems)
            raise RuntimeError(
                f"健康检查发现 {len(problems)} 个关节有问题（它们**已被各自失能**）：\n{detail}\n"
                f"  要不要整臂急停由你决定：arm.emergency_disable()\n"
                f"  ⚠️ 失能 ≠ 停住：带重力负载的关节会掉下来"
            )

    def _monitor_step(self) -> None:
        """一次监控：**力矩（仅 POS_VEL）+ 温度（所有模式）**。收循环每圈调，采样率 = feedback_hz。

        为什么力矩只在 POS_VEL 查：MIT 是"发之前钳位"、力位混控是 `i_des` 电流限幅 ——
        那两种模式主机能在**发之前**限力矩；**POS_VEL 的位置环在固件里，主机没有任何力矩通道**，
        只能事后看反馈帧的 `torque`（DESIGN §2.2）。也正因为它是电机由电流估算的（带噪声），
        必须连续 `torque_monitor_count` 次越限才算故障，否则加/减速峰值就会误触发。

        - warn（温度 ≥ `temp_warn`）：限流打 stderr（每 `MONITOR_WARN_INTERVAL` 秒至多一条）
        - fault（温度 ≥ `temp_fault`，或 POS_VEL 下力矩连续越限）：**收集齐后抛 RuntimeError**，
          由 `run_feedback_loop` 的异常路径自动 `emergency_disable()`

        ⚠️ 它挂在**收循环**上：只跑 `run_control_loop`（发）就**没有监控**。
        ⚠️ 没有反馈的关节直接跳过（不编造温度/力矩）；力矩取绝对值（正反向都算越限）。
        """
        faults: list[str] = []
        warns: list[tuple[str, str]] = []
        now = time.monotonic()
        for name, joint in self.joints.items():
            m = self.bus.get_state(joint.motor_id)
            if m is None:
                continue                          # 这台还没回过包
            cfg = self.config.joints[name]
            # 力矩：只有 POS_VEL 需要"事后监控"
            if joint.mode == MODE_POS_VEL and abs(m.tau) >= cfg.torque_monitor_threshold:
                n = self._torque_over_count.get(name, 0) + 1
                self._torque_over_count[name] = n
                if n >= cfg.torque_monitor_count:
                    faults.append(
                        f"{name}(0x{joint.motor_id:02X}): 力矩 |{m.tau:.3f}| N·m 连续 {n} 次 ≥ "
                        f"阈值 {cfg.torque_monitor_threshold}（约 "
                        f"{n / self.config.feedback_hz * 1000:.0f} ms）"
                    )
            else:
                self._torque_over_count[name] = 0
            # 温度：所有模式都查；MOS 与线圈各自独立判，共用同一对阈值
            for label, val in (("MOS", m.temp_mos), ("线圈", m.temp_rotor)):
                if val >= self.config.temp_fault:
                    faults.append(f"{name}(0x{joint.motor_id:02X}): {label}温度 {val}℃ ≥ "
                                  f"temp_fault {self.config.temp_fault}")
                elif val >= self.config.temp_warn:
                    warns.append((f"{name}/{label}",
                                  f"{name} {label}温度 {val}℃ ≥ temp_warn "
                                  f"{self.config.temp_warn}"))
        if faults:
            raise RuntimeError("监控发现故障（收循环会先急停再抛）：\n  · " + "\n  · ".join(faults))
        for key, text in warns:
            last = self._warn_last.get(key)
            if last is None or now - last >= MONITOR_WARN_INTERVAL:
                self._warn_last[key] = now
                print(f"[DmArm] ⚠ {text}", file=sys.stderr)

    def emergency_disable(self) -> None:
        """**尽力**让每个关节失能，**永不抛**。

        它是在"已经出错"的路径上被调用的（急停、上层崩溃、`check_health` 之后），所以：
        单个关节失能失败只打 stderr，既不中断其余关节的失能，也不顶掉调用方原本的异常。
        与 `Joint._emergency_disable` 同语义。没连接过就直接返回（无副作用）。
        """
        if self.bus is None:
            return
        failed: list[tuple[str, Exception]] = []
        for name, joint in self.joints.items():
            try:
                joint.disable()
            except Exception as e:                         # noqa: BLE001 —— 急停路径要吞
                failed.append((name, e))
        if failed:
            print(f"[DmArm] ‼ {len(failed)} 个关节失能失败：" +
                  "、".join(f"{n}({e})" for n, e in failed) +
                  " —— 电机可能仍在通电出力，请直接断电", file=sys.stderr)

    # ───────────────────────── 控制循环 ─────────────────────────
    def run_control_loop(self, hz: float, fn) -> dict:
        """以 `hz` 发帧的**阻塞**循环：每圈 `check_health()` → `fn(self)`（发目标在 `fn` 里）。

        退出与失能策略：

        | 怎么退出 | 失能吗 | 为什么 |
        |---|---|---|
        | `fn` 返回 `False` | **不失能** | "到了"：调用方可能还想保持姿态、或接着用同一目标 |
        | 抛异常（含 `check_health` 抓到故障） | **先急停再抛** | MIT 下电机会一直保持最后一帧的力矩 |
        | Ctrl-C | **先急停，然后正常返回** | 人手停的，不该再带电；但不抛出来打断调用方 |

        `fn` 的返回值**严格判 `False`**（`is False`）：写成没有 `return` 的普通函数（返回 `None`）
        就是"一直跑"，要停就 `return False` 或 Ctrl-C。

        某一圈超时（`fn` 用时超过周期）→ **重置节拍基准 + 计数**，**不补跑** ——
        补跑会突然连发好几帧，比慢一点更危险。

        返回 `{"iters", "overruns", "elapsed", "hz_actual", "interrupted"}`。

        ⚠️ 阻塞：不能和 `run_feedback_loop` 同时跑（要并行得自己开线程）。
        ⚠️ `check_health()` 看的是**缓存**，所以收帧得靠 `run_feedback_loop` 或调用方 `poll()`。
        """
        def each():
            self.check_health()
            return fn(self)
        return self._paced_loop(hz, each)

    def run_feedback_loop(self, hz: float, fn) -> dict:
        """以 `hz` 收帧的**阻塞**循环：每圈 `bus.poll()` → **监控** → `fn(self)`。

        `bus.poll()` 是 bus 缓存的**唯一驱动** —— 不跑它，`get_state()` / `check_health()`
        看到的永远是旧状态；**力矩与温度监控也挂在这里**（采样率 = `feedback_hz`，
        DESIGN §4.3 要求力矩计数按反馈率走，**不能**按 500Hz 发帧率数）。

        与 `run_control_loop` 只差：每圈先 `poll()` + 监控，而不是 `check_health()`
        （ERR 检查在发那一侧），且它**只收不发**。
        ⚠️ 监控判故障会**抛出** → 本循环的异常路径先 `emergency_disable()` 再抛。
        """
        def each():
            self.bus.poll()
            self._monitor_step()
            return fn(self)
        return self._paced_loop(hz, each)

    def _paced_loop(self, hz: float, each) -> dict:
        """两个控制循环共用的**节拍 + 退出 + 失能**骨架（改这里 = 改两个循环）。

        `each()` 每圈调一次；返回**严格 `False`**（`is False`）就正常退出 ——
        写成没有 `return` 的函数（返回 `None`）就是"一直跑"，所以写成
        `return 超时` 这种"完成了吗"式布尔会**静默只跑 1 圈**（真机踩过）。
        退出时会往 stderr 打一行 `iters/hz` 汇总，便于一眼看出"是不是只跑了 1 圈"。

        | 怎么退出 | 失能吗 | 为什么 |
        |---|---|---|
        | `each()` 返回 `False` | **不失能** | "到了"：调用方可能还想保持姿态、或接着用同一目标 |
        | 抛异常（含 `check_health` 抓到故障） | **先急停再抛** | MIT 下电机会一直保持最后一帧的力矩 |
        | Ctrl-C | **先急停，然后正常返回** | 人手停的，不该再带电；但不抛出来打断调用方 |

        某一圈超时（`each()` 用时超过周期）→ **重置节拍基准 + 计数**，**不补跑** ——
        补跑会突然连发好几帧，比慢一点更危险。

        返回 `{"iters", "overruns", "elapsed", "hz_actual", "interrupted"}`。
        `hz_actual = iters/elapsed`，**短循环会偏高**（退出那一圈没有 sleep）。
        """
        self._require_connected()
        if not hz > 0:                      # 顺手挡住 NaN（NaN > 0 为 False）
            raise ValueError(f"hz 必须是正数，收到 {hz!r}")
        period = 1.0 / hz
        iters = overruns = 0
        interrupted = False
        t0 = next_t = time.monotonic()
        try:
            while True:
                stop = each() is False
                iters += 1
                if stop:
                    el = time.monotonic() - t0
                    print(f"[DmArm] fn 返回 False → 循环正常退出（iters={iters}, "
                          f"{(iters / el) if el > 0 else 0:.1f} Hz）；**没有自动失能**",
                          file=sys.stderr)
                    break
                next_t += period
                slack = next_t - time.monotonic()
                if slack > 0:
                    time.sleep(slack)
                else:
                    overruns += 1
                    next_t = time.monotonic() + period   # 落后就重置基准，不补跑
        except KeyboardInterrupt:
            interrupted = True
            print("\n[DmArm] 收到 Ctrl-C，循环停止", file=sys.stderr)
        except BaseException:
            self.emergency_disable()
            raise
        elapsed = time.monotonic() - t0
        if interrupted:
            self.emergency_disable()
        return {"iters": iters, "overruns": overruns, "elapsed": elapsed,
                "hz_actual": (iters / elapsed) if elapsed > 0 else 0.0,
                "interrupted": interrupted}

    # ───────────────────────── 内部 ─────────────────────────
    def _require_connected(self) -> None:
        if self.bus is None:
            raise RuntimeError("DmArm 还没 connect()：先 `DmArm(config).connect()`")
