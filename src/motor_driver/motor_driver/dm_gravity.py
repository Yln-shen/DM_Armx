"""dm_gravity —— 重力项的 Python 实现（给"零重力手推示教"用）。

⚠️ **这是第二份重力模型实现，真源是 C++ 那份**
   （`motor_driver_hardware/dm_gravity.cpp`，已与 MuJoCo 逐点对拍 ≤1e-9，
     见 `test/test_dm_gravity.cpp`）。本文件必须与它**逐位一致**：sign 乘在哪一层、
   按名字映射（不按下标）、输出是**关节侧** N·m。改了任意一份都要重跑对拍。

只管三层坐标的第一段（力矩是**矢量** ⇒ 只乘 sign，不加 zero_shift）：

    模型坐标（URDF）  --sign-->  关节侧 q_ours   （再往下的 direction 由 Joint.set_mit 负责）

用法：
    gm = GravityModel(urdf, ["joint1", ..., "joint6"], [-1, -1, -1, -1, -1, +1])
    tau_ours = gm.tau_ours(q_model)      # q_model = sign * q_ours + zero_shift
"""
from __future__ import annotations

import math

import numpy as np
import pinocchio as pin


class GravityModel:
    def __init__(self, urdf_path, joint_names, sign):
        """joint_names 按**调用方自己的顺序**给，sign 与它一一对应（只能 ±1）。

        名字在模型里找不到、不是单自由度关节、或模型的自由度数不等于 len(joint_names)
        ⇒ **构造即抛**（宁可当场失败，也不要静默算出一个错的力矩）。
        最后一条尤其重要：带夹爪的 URDF 有第 7 个自由度，若放着不管，pinocchio 会
        把它当 0 位姿算 —— 重力项就错了，而且不报错。
        """
        self._urdf_path = str(urdf_path)
        names = list(joint_names)
        signs = list(sign)
        if len(names) != len(signs):
            raise ValueError(f"joint_names 有 {len(names)} 个，sign 有 {len(signs)} 个")
        for k, s in enumerate(signs):
            if s not in (-1, 1):
                raise ValueError(f"sign[{k}]={s!r} 不是 ±1（力矩是矢量，只允许翻转或保持）")

        model = pin.buildModelFromUrdf(self._urdf_path)     # 不需要 mesh（pinocchio 只解运动学/动力学）
        idx_q, idx_v = [], []
        for k, name in enumerate(names):
            jid = model.getJointId(name)
            # getJointId 找不到时返回 0（universe）⇒ 必须回查名字，不能只看返回值
            if jid <= 0 or jid >= len(model.names) or model.names[jid] != name:
                raise ValueError(f"关节 {name!r} 不在 {self._urdf_path} 里")
            j = model.joints[jid]
            if j.nq != 1 or j.nv != 1:
                raise ValueError(
                    f"关节 {name!r} 是 {j.nq} 自由度（nq={j.nq}, nv={j.nv}）—— 本模型只支持单自由度转动关节")
            idx_q.append(j.idx_q)
            idx_v.append(j.idx_v)
        if model.nq != len(names):
            raise ValueError(
                f"{self._urdf_path} 有 {model.nq} 个自由度，但只给了 {len(names)} 个关节 "
                f"⇒ 剩下的自由度会以 0 位姿参与计算，重力项会错（带夹爪的 URDF 请换用空载那份）")

        self._model = model
        self._data = model.createData()          # 建一次，之后每帧复用（pinocchio 的 Data 不轻）
        self._q = np.zeros(model.nq)             # 预分配；每帧只改列进来的那几个位置
        self._idx_q = tuple(idx_q)
        self._idx_v = tuple(idx_v)
        self._sign = tuple(int(s) for s in signs)
        self._n = len(names)

    def size(self):
        return self._n

    def urdf_path(self):
        return self._urdf_path

    def tau_ours(self, q_model):
        """q_model（**模型坐标**，长度 = size()，按构造时的顺序）→ 关节侧 N·m 的 list（已乘 sign）。

        输入里有非有限数 ⇒ 抛 —— 绝不发一个 NaN 力矩出去（真机上那是"整臂乱动"）。
        返回值是每帧新建的 6 元素 list：这点开销在 100 Hz 下可忽略，不去为了"零分配"把它做成
        调用方缓冲（Python 里那样反而更难读，而 C++ 那份才是实时路径）。
        """
        if len(q_model) != self._n:
            raise ValueError(f"q_model 长度 {len(q_model)} ≠ {self._n}")
        for k, v in enumerate(q_model):
            if not math.isfinite(v):
                raise ValueError(f"q_model[{k}]={v!r} 是非有限数，拒绝算重力项")
            self._q[self._idx_q[k]] = v
        g = pin.computeGeneralizedGravity(self._model, self._data, self._q)
        return [self._sign[k] * float(g[self._idx_v[k]]) for k in range(self._n)]
