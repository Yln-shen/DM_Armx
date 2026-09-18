from setuptools import find_packages, setup

package_name = "fake_driver_pkg"

setup(
    name=package_name,
    version="0.1.0",
    packages=find_packages(exclude=["test"]),
    data_files=[
        ("share/ament_index/resource_index/packages", [f"resource/{package_name}"]),
        (f"share/{package_name}", ["package.xml"]),
    ],
    install_requires=["setuptools"],
    zip_safe=True,
    maintainer="ylnn",
    maintainer_email="3432607998@qq.com",
    description="Fake Driver（虚拟机械臂执行器）。",
    license="Apache-2.0",
    extras_require={"test": ["pytest"]},
    entry_points={
        "console_scripts": [
            # 上游可执行名是 FakeReBotArmDriver，这里命名更直白
            "fake_driver = fake_driver_pkg.fake_driver:main",
        ],
    },
)
