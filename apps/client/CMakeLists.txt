add_executable(remote60_client
  src/main.cpp
  src/realtime_runtime_client.cpp
)

target_link_libraries(remote60_client PRIVATE common)

target_compile_definitions(remote60_client PRIVATE REMOTE60_HAS_REALTIME_MEDIA=0)
find_package(LibDataChannel CONFIG QUIET)
if (LibDataChannel_FOUND)
  target_link_libraries(remote60_client PRIVATE LibDataChannel::LibDataChannel)
  target_compile_definitions(remote60_client PRIVATE REMOTE60_HAS_REALTIME_MEDIA=1)
endif()
