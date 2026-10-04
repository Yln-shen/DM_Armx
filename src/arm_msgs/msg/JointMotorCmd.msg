# 单关节命令
string joint_name      # 关节名，如 "joint1"
float64 pos            # 目标位置（关节侧 rad）
float64 vel            # 速度上限或目标速度（rad/s）
bool use_pos           # 是否使用 pos
bool use_vel           # 是否使用 vel
bool use_vlim          # 是否使用 vel 作为速度上限