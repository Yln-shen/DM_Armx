from glob import glob
from setuptools import find_packages, setup

package_name = "mujoco_pkg"

# 说明：
# - 上游 rebotarm_mujoco 的 setup.py 从兄弟目录 ../rebotarm_bringup/description/meshes 里
#   glob 整个 73M 目录。本包改为自带 meshes/，只搬 colored + stl 两个模型真正引用的
#   50 个文件（42M），不依赖 rebotarm_bringup。
mesh_files = glob("meshes/*.STL") + glob("meshes/*.stl")

setup(
    name=package_name,
    version="0.1.0",
    packages=find_packages(exclude=["test"]),
    data_files=(
        [
            ("share/ament_index/resource_index/packages", [f"resource/{package_name}"]),
            (f"share/{package_name}", ["package.xml"]),
        ]
        # 逐个子目录收集；空 glob 不生成条目（否则 --symlink-install 会因空目录报错）
        + [
            (f"share/{package_name}/{sub}", files)
            for sub, files in (
                ("launch", glob("launch/*.launch.py")),
                ("config", glob("config/*.yaml")),
                ("models", glob("models/*.xml")),
                ("description", glob("description/*.urdf")),
                ("meshes", mesh_files),
            )
            if files
        ]
    ),
    install_requires=["setuptools"],
    zip_safe=True,
    maintainer="ylnn",
    maintainer_email="3432607998@qq.com",
    description="MuJoCo 仿真包（移植自 rebotarm_mujoco）。",
    license="Apache-2.0",
    extras_require={"test": ["pytest"]},
    entry_points={
        "console_scripts": [
            "real2sim_sync = mujoco_pkg.real2sim_sync:main",
            "joint_slider_gui = mujoco_pkg.joint_slider_gui:main",
            "mujoco_torque_control = mujoco_pkg.mujoco_torque_control:main",
            "mujoco_physics_grasp = mujoco_pkg.mujoco_physics_grasp:main",
            "sim_task_server = mujoco_pkg.sim_task_server:main",
            "sim_rgb_camera = mujoco_pkg.sim_rgb_camera:main",
            "sim_color_detector = mujoco_pkg.sim_color_detector:main",
        ],
    },
)
