# What is genuinely shared: the version string and the input wire format.
#
# The signalling client and the RTP packetisers lived here too, until the WebRTC host and client
# they served were retired. Nothing in the shipping product -- streaming host, Windows client,
# Android client, directory -- ever referenced them.
add_library(common STATIC
  src/version.cpp
  src/input_protocol.cpp
)

target_include_directories(common PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(common PUBLIC
  winhttp
)
