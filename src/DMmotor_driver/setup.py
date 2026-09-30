from pathlib import Path

from setuptools import find_packages, setup

package_name = 'DMmotor_driver'

# 关节配置在**仓库根**的 config/（不在包内），而 setup.py 在 src/DMmotor_driver/ 下 ——
# 所以 parents[2] 才是仓库根。写死 'config/joint.yaml' 会错指到 src/DMmotor_driver/config/。
_repo_root = Path(__file__).resolve().parents[2]

setup(
    name=package_name,
    version='0.0.0',
    packages=find_packages(exclude=['test']),
    data_files=[
        ('share/ament_index/resource_index/packages',
            ['resource/' + package_name]),
        ('share/' + package_name, ['package.xml']),
        # 安装后路径：share/DMmotor_driver/config/joint.yaml
        ('share/' + package_name + '/config',
            [str(_repo_root / 'config' / 'joint.yaml')]),
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
            'dm-bringup = DMmotor_driver.dm_bringup:main',
            'dm-dump-registers = DMmotor_driver.dm_registers:main',
        ],
    },
)
