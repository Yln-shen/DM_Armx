# 单关节状态
string joint_name
float64 position       # rad（关节侧）
float64 velocity       # rad/s
float64 torque         # N·m
int32 err              # 错误码
string err_text        # 错误码文本
float64 temp_mos       # ℃
float64 temp_rotor     # ℃
bool enabled           # 是否使能