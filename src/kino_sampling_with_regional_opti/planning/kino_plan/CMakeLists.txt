cmake_minimum_required(VERSION 2.8.3)
project(kino_plan)

add_compile_options(-std=c++11)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_BUILD_TYPE "Release")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -Wall -g -fPIC")

find_package(catkin REQUIRED COMPONENTS
  roscpp
  rospy
  std_msgs
  occ_grid
  poly_traj_utils
  visualization_utils
  self_msgs_and_srvs
  poly_opt
  r3_plan
)

find_package(Eigen3 REQUIRED)
find_package(PCL 1.7 REQUIRED)

catkin_package(
  INCLUDE_DIRS include
  LIBRARIES kino_plan
  CATKIN_DEPENDS occ_grid poly_traj_utils
#  DEPENDS system_lib
)

include_directories(
  include
  ${catkin_INCLUDE_DIRS}
  ${PCL_INCLUDE_DIRS}
  ${Eigen3_INCLUDE_DIRS}
)

add_library(${PROJECT_NAME}
  src/bi_krrt.cpp
  src/krrtplanner.cpp
  src/kdtree.c
  src/raycast.cpp
  src/bias_sampler.cpp
  src/bvp_solver.cpp
)

target_link_libraries(${PROJECT_NAME}
  ${catkin_LIBRARIES}
)
