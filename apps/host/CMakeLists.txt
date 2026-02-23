add_executable(remote60_host
  src/main.cpp
  src/capture_bootstrap.cpp
  src/capture_runtime.cpp
  src/audio_runtime.cpp
  src/mf_h264_encoder.cpp
  src/runtime_server.cpp
  src/host_non_runtime_paths.cpp
)

option(REMOTE60_ENABLE_WINDOW_MODE "Enable window-scoped capture/input mode for host runtime" ON)

target_link_libraries(remote60_host PRIVATE
  common
  d3d11
  runtimeobject
  windowsapp
  mfplat
  mf
  mfuuid
  wmcodecdspuuid
  ole32
  avrt
)

target_compile_definitions(remote60_host PRIVATE REMOTE60_HAS_REALTIME_MEDIA=0)
target_compile_definitions(remote60_host PRIVATE REMOTE60_ENABLE_WINDOW_MODE=$<IF:$<BOOL:${REMOTE60_ENABLE_WINDOW_MODE}>,1,0>)

find_package(LibDataChannel CONFIG QUIET)
find_package(Opus CONFIG QUIET)
if (LibDataChannel_FOUND AND Opus_FOUND)
  target_sources(remote60_host PRIVATE src/realtime_media_probe.cpp src/realtime_runtime.cpp)
  target_link_libraries(remote60_host PRIVATE LibDataChannel::LibDataChannel Opus::opus)
  target_compile_definitions(remote60_host PRIVATE REMOTE60_HAS_REALTIME_MEDIA=1)
endif()
