p = 'C:/work/gnlink-wt/apk-decoder-liveness/apps/native_poc/CMakeLists.txt'
s = open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
anchor = 'target_compile_definitions(remote60_android_decoder_decode_stats_test PRIVATE NOMINMAX REMOTE60_DECODER_TEST_HOOKS)' + nl
assert s.count(anchor) == 1
block = '''
# apk-decoder-liveness r1 (Codex codex_apk_decoder_liveness_design_r1.md (3)): the APK sink against the stand-in NDK
# whose codec is held (create / configure / start / stop) longer than the control retransmit budget -- RX, control
# and UI calls must not wait for it. REMOTE60_LIVENESS_SINK_DIR picks the sink source: the product (default), or a
# copy of an older / mutated sink for the negative runs (a separate build directory). TEST ONLY.
set(REMOTE60_LIVENESS_SINK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../android_direct_client/app/src/main/cpp" CACHE PATH
    "android_video_decoder.cpp/.hpp the liveness tests build (negative runs point it at another copy)")
add_executable(remote60_android_decoder_liveness_test
  src/android_decoder_liveness_test.cpp
  src/test_fake_ndk/fake_ndk.cpp
  ${REMOTE60_LIVENESS_SINK_DIR}/android_video_decoder.cpp
)
target_include_directories(remote60_android_decoder_liveness_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src/test_fake_ndk
  ${REMOTE60_LIVENESS_SINK_DIR}
  src
)
target_compile_definitions(remote60_android_decoder_liveness_test PRIVATE NOMINMAX)
'''.replace('\n', nl)
s = s.replace(anchor, anchor + block)
open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
