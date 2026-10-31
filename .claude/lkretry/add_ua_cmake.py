p = r'C:\work\gnlink-wt\fix-capture-lifecycle\apps\native_poc\CMakeLists.txt'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')
anchor = '# mouse-xbutton r1: the back / forward buttons through the product\'s window procedure, the wire, a\n'
assert s.count(anchor) == 1
block = '''# capture-lifecycle r1 (UA-1 / UA-2): an isolated test-build host capturing this desktop; the shared session
# (ClientSessionController, the Android app's engine) asks WGC and crosses a shown-as-secure desktop twice on
# one connection, then the PC viewer (the product receive chain) starts a new session -- the host's default,
# DXGI -- and crosses it twice too. Lifecycle only: the secure desktop is the test build's stand-in name.
add_executable(remote60_host_capture_lifecycle_ua_e2e_test
  src/host_capture_lifecycle_ua_e2e_test.cpp
  src/native_video_client_session.cpp
  src/viewer_video_receiver.cpp
  src/viewer_video_receiver_frame.cpp
  src/viewer_window_proc.cpp
  src/viewer_input_forward.cpp
  src/viewer_control_client.cpp
  src/viewer_present.cpp
  src/viewer_picker.cpp
  src/viewer_picker_state.cpp
  src/viewer_selection_gate.cpp
  src/viewer_overlay_draw.cpp
  src/viewer_restore.cpp
  src/viewer_restore_status.cpp
  src/viewer_restore_target.cpp
  src/viewer_gdi_util.cpp
  src/viewer_layout.cpp
  src/viewer_log.cpp
  src/viewer_unlock.cpp
  src/client_session_toolbar.cpp
  src/input_macro.cpp
  src/viewer_session_watchdog.cpp
  src/viewer_cursor_overlay.cpp
  src/viewer_frame_gate.cpp
  src/sealed_unlock.cpp
  src/mf_h264_codec.cpp
  src/native_video_client_shared_core.cpp
  src/native_video_client_tcp_control.cpp
  src/udp_control_channel.cpp
)
target_include_directories(remote60_host_capture_lifecycle_ua_e2e_test PRIVATE src)
target_link_libraries(remote60_host_capture_lifecycle_ua_e2e_test PRIVATE
  remote60_clip_image remote60_test_scratch_dir
  common user32 gdi32 shell32 ole32 shlwapi version ws2_32 imm32 bcrypt ncrypt advapi32 crypt32 winmm
  d3d11 dxgi d3dcompiler mfplat mf mfuuid wmcodecdspuuid)
if(MSVC)
  target_compile_options(remote60_host_capture_lifecycle_ua_e2e_test PRIVATE /utf-8)
endif()
add_dependencies(remote60_host_capture_lifecycle_ua_e2e_test remote60_native_video_host_migration_seam)

'''
s = s.replace(anchor, block + anchor)
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
