cmake_minimum_required(VERSION 3.10)
project(arm_moveit_config)

find_package(ament_cmake REQUIRED)

# 纯数据包：SRDF / 规划器参数 / 控制器映射 / launch / rviz
install(DIRECTORY config launch rviz
        DESTINATION share/${PROJECT_NAME})

ament_package()
