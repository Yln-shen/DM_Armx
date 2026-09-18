#!/usr/bin/env bash
# DM_Armx pixi 环境自检
# 用法：bash tools/check_env.sh
set -u

# 定位 pixi（非登录 shell 常不在 PATH）
PIXI="$(command -v pixi || true)"
if [ -z "$PIXI" ] && [ -x "$HOME/.pixi/bin/pixi" ]; then
  PIXI="$HOME/.pixi/bin/pixi"
fi
if [ -z "$PIXI" ]; then
  echo "✗ 未找到 pixi（PATH 或 ~/.pixi/bin/pixi）。请先安装：curl -fsSL https://pixi.sh/install.sh | bash"
  exit 1
fi
echo "✓ pixi: $($PIXI --version)"

cd "$(dirname "$0")/.." || exit 1   # 回到项目根

run_in_env() { "$PIXI" run "$@" 2>&1 | tail -n 1; }

echo "--- 系统 ---"
grep PRETTY /etc/os-release || true
echo "python(系统): $(python3 --version 2>&1)"

echo "--- pixi 环境内 ---"
echo "python: $(run_in_env python -c 'import sys;print(sys.version.split()[0])' 2>/dev/null || echo 未获取)"

# ROS2
ROS_LINE="$(run_in_env ros2 --help 2>/dev/null | head -1)"
case "$ROS_LINE" in
  *usage*|*Usage*) echo "✓ ros2 CLI 存在" ;;
  *) echo "✗ ros2 CLI 不可用：$ROS_LINE" ;;
esac
ROS_DISTRO="$(run_in_env sh -c 'echo ${ROS_DISTRO:-未设置}' 2>/dev/null)"
echo "ROS_DISTRO(env内): $ROS_DISTRO"

echo "--- Python 电机/仿真库（阶段 0/1 逐项加）---"
for mod in motorbridge mujoco pin numpy transforms3d; do
  if "$PIXI" run python -c "import $mod" >/dev/null 2>&1; then
    v="$("$PIXI" run python -c "import $mod; print(getattr($mod,'__version__','ok'))" 2>/dev/null)"
    echo "✓ $mod ($v)"
  else
    echo "✗ $mod 未安装（阶段需要时 pixi add）"
  fi
done

echo "--- 真机通信设备 ---"
if ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null | grep -q .; then
  echo "✓ 检测到: $(ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null | tr '\n' ' ')"
else
  echo "· 暂无 /dev/ttyACM* /dev/ttyUSB*（电机链路阶段需接入 USB-CAN）"
fi

echo "--- 建议下一步 ---"
echo "1. 本机若要在 pixi 内跑 reBot 作对照：核对 env 的 ros2 版本与 reBot 要求(Jazzy)是否一致"
echo "2. 阶段0 读 docs/reading_guide.md；电机阶段再 pixi add motorbridge mujoco pin"
