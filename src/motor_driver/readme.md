重力补偿
cd ~/DM_Armx

# ── 0. 自检 ──
ls -l /dev/dm_can /dev/ttyACM* 2>/dev/null      # 都不存在 ⇒ 适配器没插
ros2 node list 2>/dev/null | grep -E "controller_manager|move_group" || echo "总线干净"
# 若只有 /dev/ttyACM0：下面每条命令都加 --channel /dev/ttyACM0

# ── 1. 预检（只读：不发一帧控制帧、不使能）──
pixi run python src/motor_driver/motor_driver/gravity_teach.py --check
#   预期：通过，并打印一条 j2 下侧 0.0076 rad 的警告（那一侧就是硬限位）

# ── 2. 首次真机：小补偿，20 s 自动停 ──
#    ⚠️ 跑之前手扶住臂：① 使能后约 30 ms 完全没力矩 ② 之后它可能自己抬起来
pixi run python src/motor_driver/motor_driver/gravity_teach.py \
    --run --yes --scale 0.5 --ramp 15 --duration 20

# ── 3. 正式演示：满补偿，一直跑到你 Ctrl-C ──
pixi run python src/motor_driver/motor_driver/gravity_teach.py \
    --run --yes --scale 1.0 --ramp 20

现场调手感（不改文件）：
bash

... --kp 6          # 松手停得更硬
... --kp 2          # 推起来更轻
... --kp-wall 40    # 软墙更硬
... --kd 1.2        # 阻尼更大（更稳，但更「黏」）
... --strict-travel # 只在你想坚持"四周都要宽敞"时用