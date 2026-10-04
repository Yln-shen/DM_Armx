from setuptools import find_packages, setup

package_name = 'motor_driver'

# 关节配置在**包内**的 config/（src/motor_driver/config/joint.yaml），随包装到
#   share/motor_driver/config/joint.yaml
# ⚠️ data_files 的源路径**必须是相对路径**：colcon 的 ament_python task 会
#    `assert not os.path.isabs(source)`，用 Path(__file__) 拼绝对路径会直接构建失败。
setup(
    name=package_name,
    version='0.0.0',
    packages=find_packages(exclude=['test']),
    data_files=[
        ('share/ament_index/resource_index/packages',
            ['resource/' + package_name]),
        ('share/' + package_name, ['package.xml']),
        ('share/' + package_name + '/config', ['config/joint.yaml']),
    ],
    install_requires=['setuptools'],
    zip_safe=True,
    maintainer='ylnn',
    maintainer_email='3432607998@qq.com',
    description='TODO: Package description',
    license='Apache-2.0',
    extras_require={
        'test': [
            'pytest',
        ],
    },
    entry_points={
        'console_scripts': [
            'dm-bringup = motor_driver.dm_bringup:main',
            'dm-dump-registers = motor_driver.dm_registers:main',
        ],
    },
)
