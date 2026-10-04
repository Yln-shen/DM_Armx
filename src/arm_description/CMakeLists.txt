cmake_minimum_required(VERSION 3.10)
project(arm_description)

find_package(ament_cmake REQUIRED)

# 纯数据包：几何、xacro、launch、rviz 配置、第三方参考与许可
install(DIRECTORY meshes urdf launch rviz reference
        DESTINATION share/${PROJECT_NAME})

ament_package()
