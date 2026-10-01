"""arm_config —— 配置解析层（读 config/joint.yaml）：整臂全局参数 + 每个关节的静态参数。

只管两件事：**读 yaml** 与**纯参数自洽校验**。
不做通信、不做寄存器 I/O、不碰运行期状态、不 import rclpy。
运行期状态与"参数 + 外部状态"的校验（比如 bus 上注册冲突）在 `joint.py` 的 `Joint` 里。
"""
from dataclasses import dataclass
from pathlib import Path

import yaml

from dm_modes import MODE_MIT, MODE_NAMES


@dataclass
class JointConfig:
    """一个关节的静态参数（构造 `Joint` 的输入）。

    `limit` = (PMAX, VMAX, TMAX) 是电机的 **MIT 映射范围**，必须用
    0x15/0x16/0x17 的**回读值**填：4310 与 4340P 档位不同，用错档位解出来的
    力矩差数倍**且不报错**。
    """

    name: str
    motor_type: str
    slave_id: int
    direction: int
    limit: tuple[float, float, float]
    offset: float = 0.0
    position_min: float | None = None
    position_max: float | None = None
    mode: int = MODE_MIT

    def __post_init__(self):
        # 纯参数自洽校验：只用本 dataclass 自己的字段，不依赖任何外部状态
        self.limit = tuple(float(x) for x in self.limit)

        if self.direction not in (1, -1):
            raise ValueError(f"{self.name}: direction 只能是 +1 或 -1，收到 {self.direction!r}")
        if self.mode not in MODE_NAMES:
            allowed = "/".join(f"{k}({v})" for k, v in sorted(MODE_NAMES.items()))
            raise ValueError(f"{self.name}: mode 只能是 {allowed}，收到 {self.mode!r}")
        if len(self.limit) != 3 or not all(x > 0 for x in self.limit):
            raise ValueError(
                f"{self.name}: limit 必须是 (PMAX, VMAX, TMAX) 三个正数，收到 {self.limit!r}"
            )
        # 软限位自己得先自洽：反了的话两道判断会交替命中，命令被钉死在两点集上
        if (self.position_min is not None and self.position_max is not None
                and self.position_min > self.position_max):
            raise ValueError(
                f"{self.name}: 软限位反了：position_min={self.position_min} > "
                f"position_max={self.position_max}"
            )
        # 软限位（关节侧）换算到电机侧后必须落在 ±PMAX 内，否则那条限位是虚的
        p_max = self.limit[0]
        for label, val in (("position_min", self.position_min),
                           ("position_max", self.position_max)):
            if val is None:
                continue
            q_motor = self.direction * float(val) + self.offset
            if not (-p_max <= q_motor <= p_max):
                raise ValueError(
                    f"{self.name}: {label}={val}（关节侧）换算到电机侧是 {q_motor:+.4f} rad，"
                    f"超出 PMAX=±{p_max}。检查 limit 是不是用错档位了"
                    f"（4340P 回读 12.5/10/28，4310 回读 12.5/30/10）"
                )


def load_joint_configs(path: str | Path) -> dict[str, JointConfig]:
    """读 yaml → `{关节名: JointConfig}`。

    参数名就是 yaml 里 `joints:` 下的键（`joint1`、`joint2` …）。
    不做"宽容解析"：多写一个键会由 dataclass 的 `**cfg` 当场抛 TypeError，
    少写一个键同理 —— 拼错的名字就该当场发现。
    """
    data = yaml.safe_load(Path(path).read_text(encoding="utf-8"))
    return {name: JointConfig(name=name, **cfg) for name, cfg in data["joints"].items()}


@dataclass
class ArmConfig:
    """整臂的全局参数 + 每个关节的静态参数（构造 `DmArm` 的输入）。

    `serial_timeout` 必须**远小于** 1/send_hz：它是 pyserial 的读超时，
    大了会把"发一帧等一帧"那条路拖死（见 `dm_bus.DEFAULT_TIMEOUT` 的注释）。
    """

    channel: str
    joints: dict[str, JointConfig]
    baud: int = 921600
    serial_timeout: float = 0.003
    send_hz: float = 500.0
    feedback_hz: float = 100.0

    def __post_init__(self):
        # 纯参数自洽校验（只用本 dataclass 自己的字段，不依赖外部状态）
        if not self.channel:
            raise ValueError("channel 不能为空（USB-CAN 适配器的串口，如 /dev/ttyACM0）")
        if self.baud <= 0:
            raise ValueError(f"baud 必须为正，收到 {self.baud!r}")
        if self.serial_timeout <= 0:
            raise ValueError(f"serial_timeout 必须为正，收到 {self.serial_timeout!r}")
        if self.send_hz <= 0 or self.feedback_hz <= 0:
            raise ValueError(f"send_hz/feedback_hz 必须为正，收到 {self.send_hz!r}/{self.feedback_hz!r}")
        if not self.joints:
            raise ValueError("joints 不能为空（至少一个关节）")
        seen, dup = set(), []
        for cfg in self.joints.values():
            if cfg.slave_id in seen:
                dup.append(cfg.slave_id)
            seen.add(cfg.slave_id)
        if dup:
            raise ValueError(f"slave_id 重复：{', '.join(f'0x{i:02X}' for i in dup)} —— "
                             f"同一 ID 出现两个关节，回包分不清是谁的")


# yaml 里的全局字段（`joints:` 之外的键）。没写就用 dataclass 的默认值。
_GLOBAL_KEYS = ("channel", "baud", "serial_timeout", "send_hz", "feedback_hz")


def load_arm_config(path: str | Path) -> ArmConfig:
    """读整份 yaml：全局字段 + `joints:` 表。

    ⚠️ 这份文件会被读两遍（`load_joint_configs` 也读一次）—— 配置只有几百字节，
    换来"关节解析只有一份实现"，值得。
    """
    data = yaml.safe_load(Path(path).read_text(encoding="utf-8"))
    globals_ = {k: data[k] for k in _GLOBAL_KEYS if k in data}
    return ArmConfig(joints=load_joint_configs(path), **globals_)
