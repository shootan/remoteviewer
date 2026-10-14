# mobile-abr r3: TEST ONLY. The APK's decoder sink and client session on a real MediaCodec, as an Android
# command-line executable for an emulator (mediacodec_smoke.cpp). Not referenced by the APK's Gradle build.
# Configure with the NDK toolchain, e.g.:
#   cmake -S apps/android_direct_client/smoke -B <dir> -G Ninja -DCMAKE_TOOLCHAIN_FILE=<ndk>/build/cmake/android.toolchain.cmake
#         -DANDROID_ABI=x86_64 -DANDROID_PLATFORM=android-28 -DCMAKE_BUILD_TYPE=Release
cmake_minimum_required(VERSION 3.22.1)
project(gnlink_mediacodec_smoke CXX)
set(CMAKE_CXX_STANDARD 20)

set(APP_CPP ${CMAKE_CURRENT_SOURCE_DIR}/../app/src/main/cpp)
set(POC_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../native_poc/src)

add_executable(mediacodec_smoke
  mediacodec_smoke.cpp
  ${APP_CPP}/android_video_decoder.cpp
  ${POC_SRC}/native_video_client_shared_core.cpp
  ${POC_SRC}/native_video_client_tcp_control.cpp
  ${POC_SRC}/native_video_client_session.cpp
  ${POC_SRC}/udp_control_channel.cpp
)
target_include_directories(mediacodec_smoke PRIVATE ${APP_CPP} ${POC_SRC})
target_link_libraries(mediacodec_smoke PRIVATE android log mediandk)
