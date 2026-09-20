from setuptools import find_packages, setup

package_name = 'DMmotor_driver'

setup(
    name=package_name,
    version='0.0.0',
    packages=find_packages(exclude=['test']),
    data_files=[
        ('share/ament_index/resource_index/packages',
            ['resource/' + package_name]),
        ('share/' + package_name, ['package.xml']),
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
