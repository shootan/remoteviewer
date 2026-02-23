add_library(common STATIC
  src/version.cpp
  src/signaling_protocol.cpp
  src/rtc_signaling_client.cpp
  src/ws_rtc_signaling_client.cpp
  src/h264_rtp_packetizer.cpp
  src/opus_rtp_packetizer.cpp
  src/input_protocol.cpp
)

target_include_directories(common PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(common PUBLIC
  winhttp
)
