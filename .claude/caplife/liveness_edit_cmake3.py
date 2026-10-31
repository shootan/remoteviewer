p = 'C:/work/gnlink-wt/apk-decoder-liveness/apps/native_poc/CMakeLists.txt'
s = open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
anchor = 'target_link_libraries(remote60_android_decoder_session_liveness_test PRIVATE common ws2_32)' + nl
assert s.count(anchor) == 1
block = '''
# apk-decoder-liveness r2 axis 2: an IDR lost while the decoder was not ready and its recovery once it is -- the
# PRODUCT shared session (control loop + owed retry + limiter + UDP receive loop) into the APK sink, a fake host.
# TEST ONLY. The same REMOTE60_LIVENESS_SINK_DIR choice as above.
add_executable(remote60_android_decoder_idr_recovery_test
  src/android_decoder_idr_recovery_test.cpp
  src/native_video_client_shared_core.cpp
  src/native_video_client_tcp_control.cpp
  src/udp_control_channel.cpp
  src/native_video_client_session.cpp
  src/test_fake_ndk/fake_ndk.cpp
  ${REMOTE60_LIVENESS_SINK_DIR}/android_video_decoder.cpp
)
target_include_directories(remote60_android_decoder_idr_recovery_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src/test_fake_ndk
  ${REMOTE60_LIVENESS_SINK_DIR}
  src
)
target_compile_definitions(remote60_android_decoder_idr_recovery_test PRIVATE NOMINMAX)
target_link_libraries(remote60_android_decoder_idr_recovery_test PRIVATE common ws2_32)
'''.replace('\n', nl)
s = s.replace(anchor, anchor + block)
open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
