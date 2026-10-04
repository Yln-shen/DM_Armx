from setuptools import find_packages, setup

package_name = 'arm_bringup'

# 与 motor_driver 同样的教训：data_files 的源路径**必须相对**（colcon 的 ament_python
# task 会 assert 拒绝绝对路径），所以这里全部写相对包目录的路径。
setup(
    name=package_name,
    version='0.0.0',
    packages=find_packages(exclude=['test']),
    data_files=[
        ('share/ament_index/resource_index/packages',
            ['resource/' + package_name]),
        ('share/' + package_name, ['package.xml']),
        ('share/' + package_name + '/launch', ['launch/real_display.launch.py']),
    ],
    install_requires=['setuptools'],
    zip_safe=True,
    maintainer='ylnn',
    maintainer_email='3432607998@qq.com',
    description='DM_Armx 的 ROS2 胶水层（只读 /joint_states，模型对齐用）',
    license='Apache-2.0',
    entry_points={
        'console_scripts': [
            'real-joint-states = arm_bringup.real_joint_states:main',
        ],
    },
)
