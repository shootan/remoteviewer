cmake_minimum_required(VERSION 3.22.1)

project(remote60_android_direct)

add_library(remote60_android_direct SHARED
  native_bridge.cpp
)

find_library(log-lib log)

target_link_libraries(remote60_android_direct
  ${log-lib}
)
