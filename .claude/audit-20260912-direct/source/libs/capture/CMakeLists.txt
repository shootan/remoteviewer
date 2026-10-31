# Screen capture, shared by everything that streams a desktop.
#
# This lived in apps/host until the WebRTC host it belonged to was retired. It outlived that app
# because the capture itself was never the experimental part: the streaming host still uses the
# same DXGI backend and the same output-selection rules. Keeping it under apps/ after its app was
# gone would have said it belonged to one executable, which is how it came to be reached by
# relative path from a sibling directory in the first place.
add_library(capture STATIC
  src/capture_runtime.cpp
  src/capture_backend_dxgi.cpp
)

target_include_directories(capture PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(capture PUBLIC
  d3d11
  dxgi
)
