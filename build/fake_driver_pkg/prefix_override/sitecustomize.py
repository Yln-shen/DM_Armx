import sys
if sys.prefix == '/home/ylnn/DM_Armx/.pixi/envs/default':
    sys.real_prefix = sys.prefix
    sys.prefix = sys.exec_prefix = '/home/ylnn/DM_Armx/install/fake_driver_pkg'
