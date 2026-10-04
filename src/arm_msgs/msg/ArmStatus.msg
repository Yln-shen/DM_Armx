# 整臂状态
string mode                  # "MIT" / "POS_VEL" / "FORCE_POS"
bool enabled                 # 整臂是否使能
bool control_loop_active     # 控制循环是否在跑
string state_machine         # "IDLE" / "TRAJ_RUNNING" / ...
string[] joint_names         # 关节名列表
int32[] per_joint_status_code # 每个关节的状态码
string[] error_codes         # 错误码列表