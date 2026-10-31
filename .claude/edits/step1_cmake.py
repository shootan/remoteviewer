import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit

CMAKE = r'D:\remote\remote\apps\native_poc\CMakeLists.txt'

edit(CMAKE, [
(
"""# Viewer split refactor T2: the PC-side target-selection gate (begin / ack / frame admission / reveal race).
""",
"""# Windows receive-path integration test (Windows NACK wiring / recovery state machine): the real
# VideoReceiver::run_udp -> assembler (in-order hold) -> FrameGate -> MF H264Decoder chain on a real
# UDP socket, against an in-process fake host that encodes real H.264 with the product's
# H264Encoder, chunks AUs with the host's wire geometry, drops / reorders / paces chunks, answers or
# ignores NACKs and honours or withholds keyframe requests. The two UI-thread entry points the path
# calls are stubbed in the test; no window, swap chain or capture backend is involved, so unlike the
# GNLinkStream+GNLinkViewer e2e it does not depend on the RDP state of the machine.
add_executable(remote60_viewer_udp_recovery_test
  src/viewer_video_receiver.cpp
  src/viewer_video_receiver_frame.cpp
  src/viewer_frame_gate.cpp
  src/viewer_selection_gate.cpp
  src/viewer_log.cpp
  src/input_macro.cpp
  src/native_video_client_shared_core.cpp
  src/native_video_client_tcp_control.cpp
  src/udp_control_channel.cpp
  src/mf_h264_codec.cpp
  src/viewer_udp_recovery_test.cpp
)
target_include_directories(remote60_viewer_udp_recovery_test PRIVATE src)
target_link_libraries(remote60_viewer_udp_recovery_test PRIVATE
  common
  ws2_32
  d3d11
  dxgi
  gdi32
  user32
  mfplat
  mf
  mfuuid
  wmcodecdspuuid
  ole32
)

# Viewer split refactor T2: the PC-side target-selection gate (begin / ack / frame admission / reveal race).
"""),
])
