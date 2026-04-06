cmake_minimum_required(VERSION 3.22.1)

project(remote60_android_direct)

add_library(remote60_android_direct SHARED
  native_bridge.cpp
  ../../../../../native_poc/src/native_video_client_shared_core.cpp
  ../../../../../native_poc/src/native_video_client_tcp_control.cpp
  ../../../../../native_poc/src/native_video_client_session.cpp
)

find_library(log-lib log)

target_include_directories(remote60_android_direct PRIVATE
  ../../../../../native_poc/src
)

target_link_libraries(remote60_android_direct
  ${log-lib}
)
