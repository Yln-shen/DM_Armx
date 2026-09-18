# generated from rosidl_cmake/cmake/rosidl_cmake_aggregate_target-extras.cmake.in

# Create a convenience aggregate target rebotarm_msgs::rebotarm_msgs
# that links all generated interface targets, so downstream packages can use
# a single modern CMake target name instead of ${rebotarm_msgs_TARGETS}.
if(rebotarm_msgs_TARGETS AND NOT TARGET rebotarm_msgs::rebotarm_msgs)
  add_library(rebotarm_msgs::rebotarm_msgs INTERFACE IMPORTED)
  set_target_properties(rebotarm_msgs::rebotarm_msgs PROPERTIES
    INTERFACE_LINK_LIBRARIES "${rebotarm_msgs_TARGETS}")
endif()
