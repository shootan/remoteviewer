option(REMOTE60_NATIVE_ENCODED_EXPERIMENT "Enable native encoded-path experiment switches" OFF)
get_property(_native_encoded_experiment_flag CACHE REMOTE60_NATIVE_ENCODED_EXPERIMENT PROPERTY VALUE)
if("${_native_encoded_experiment_flag}" STREQUAL "")
  set(_native_encoded_experiment_flag "OFF")
endif()
string(TOUPPER "${_native_encoded_experiment_flag}" _native_encoded_experiment_flag_upper)
set(_native_encoded_compile_value 0)
if(_native_encoded_experiment_flag_upper STREQUAL "ON" OR
   _native_encoded_experiment_flag_upper STREQUAL "TRUE" OR
   _native_encoded_experiment_flag_upper STREQUAL "1")
  set(_native_encoded_compile_value 1)
endif()

add_executable(remote60_native_host_poc
  src/native_host_main.cpp
  ../host/src/capture_runtime.cpp
)

target_include_directories(remote60_native_host_poc PRIVATE
  src
  ../host/src
)

target_link_libraries(remote60_native_host_poc PRIVATE
  common
  ws2_32
  d3d11
  runtimeobject
  windowsapp
)

add_executable(remote60_native_client_poc
  src/native_client_main.cpp
)

target_include_directories(remote60_native_client_poc PRIVATE
  src
)

target_link_libraries(remote60_native_client_poc PRIVATE
  common
  ws2_32
)

add_executable(remote60_native_video_host_poc
  src/native_video_host_main.cpp
  src/d3d_capture_readback.cpp
  src/directory_client.cpp
  src/gdi_capture_process.cpp
  src/udp_control_channel.cpp
  src/mf_h264_codec.cpp
  src/secure_input_broker.cpp
  ../host/src/capture_backend_dxgi.cpp
)
set_target_properties(remote60_native_video_host_poc PROPERTIES OUTPUT_NAME "GNLinkStream")

add_executable(remote60_gdi_capture_worker
  src/gdi_capture_worker_main.cpp
)
set_target_properties(remote60_gdi_capture_worker PROPERTIES OUTPUT_NAME "GNLinkCapture")
target_include_directories(remote60_gdi_capture_worker PRIVATE src)
target_link_libraries(remote60_gdi_capture_worker PRIVATE
  gdi32
  user32
)

add_executable(remote60_gdi_capture_process_test
  src/gdi_capture_process.cpp
  src/gdi_capture_process_test.cpp
)
target_include_directories(remote60_gdi_capture_process_test PRIVATE src)
target_link_libraries(remote60_gdi_capture_process_test PRIVATE user32)

target_include_directories(remote60_native_video_host_poc PRIVATE
  src
  ../host/src
)

target_link_libraries(remote60_native_video_host_poc PRIVATE
  common
  ws2_32
  d3d11
  mfplat
  mf
  mfuuid
  wmcodecdspuuid
  ole32
  runtimeobject
  windowsapp
  advapi32
)
target_compile_definitions(remote60_native_video_host_poc PRIVATE
  REMOTE60_NATIVE_ENCODED_EXPERIMENT=${_native_encoded_compile_value}
)

add_executable(remote60_secure_input_service
  src/secure_input_service_main.cpp
)
set_target_properties(remote60_secure_input_service PROPERTIES OUTPUT_NAME "GNLinkInputService")
target_include_directories(remote60_secure_input_service PRIVATE src)
target_link_libraries(remote60_secure_input_service PRIVATE
  advapi32
  user32
)

add_executable(remote60_native_video_client_poc
  src/native_video_client_main.cpp
  src/client_macro_window.cpp
  src/input_macro.cpp
  src/native_video_client_shared_core.cpp
  src/native_video_client_tcp_control.cpp
  src/udp_control_channel.cpp
  src/mf_h264_codec.cpp
)
set_target_properties(remote60_native_video_client_poc PROPERTIES OUTPUT_NAME "GNLinkViewer")

target_include_directories(remote60_native_video_client_poc PRIVATE
  src
)

target_link_libraries(remote60_native_video_client_poc PRIVATE
  common
  ws2_32
  d3d11
  dxgi
  d3dcompiler
  gdi32
  user32
  shell32
  mfplat
  mf
  mfuuid
  wmcodecdspuuid
  ole32
)
target_compile_definitions(remote60_native_video_client_poc PRIVATE
  REMOTE60_NATIVE_ENCODED_EXPERIMENT=${_native_encoded_compile_value}
)

add_executable(remote60_native_video_client_shared_core_test
  src/native_video_client_shared_core.cpp
  src/native_video_client_tcp_control.cpp
  src/udp_control_channel.cpp
  src/native_video_client_session.cpp
  src/native_video_client_shared_core_test.cpp
)

target_include_directories(remote60_native_video_client_shared_core_test PRIVATE
  src
)

target_link_libraries(remote60_native_video_client_shared_core_test PRIVATE
  common
  ws2_32
)

# The thing a user installs: sign-in window plus tray, supervising the streaming host.
add_executable(remote60_host_app WIN32
  src/host_app_main.cpp
  src/directory_client.cpp
  host_app.rc
)
set_target_properties(remote60_host_app PROPERTIES OUTPUT_NAME "GNLinkHost")

target_include_directories(remote60_host_app PRIVATE
  src
)

target_link_libraries(remote60_host_app PRIVATE
  ws2_32
  comctl32
  shlwapi
)
if(MSVC)
  # Input into elevated windows and installation of the secure-desktop broker both require an
  # elevated token. Embed that contract in the product launcher so it prompts once at startup.
  target_link_options(remote60_host_app PRIVATE
    "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'"
  )
endif()
add_dependencies(remote60_host_app
  remote60_native_video_host_poc
  remote60_secure_input_service
  remote60_gdi_capture_worker
)

# One file the user can carry to another machine. It embeds the four product executables and
# installs them under %ProgramFiles%\GNLink, which is the only place the LocalSystem input
# service can live without handing SYSTEM to anyone who can write the install folder.
set(_gnlink_payload_dir "${CMAKE_CURRENT_BINARY_DIR}/installer_payload")
set(GNLINK_INSTALLER_ICON "${CMAKE_CURRENT_SOURCE_DIR}/res/gnlink.ico")
set(GNLINK_PAYLOAD_HOST_APP "${_gnlink_payload_dir}/GNLinkHost.exe")
set(GNLINK_PAYLOAD_VIDEO_HOST "${_gnlink_payload_dir}/GNLinkStream.exe")
set(GNLINK_PAYLOAD_SECURE_INPUT "${_gnlink_payload_dir}/GNLinkInputService.exe")
set(GNLINK_PAYLOAD_GDI_WORKER "${_gnlink_payload_dir}/GNLinkCapture.exe")

# The .rc reader takes these paths literally, so escape the separators for the generated script.
foreach(_var GNLINK_INSTALLER_ICON GNLINK_PAYLOAD_HOST_APP GNLINK_PAYLOAD_VIDEO_HOST
             GNLINK_PAYLOAD_SECURE_INPUT GNLINK_PAYLOAD_GDI_WORKER)
  string(REPLACE "/" "\\\\" ${_var} "${${_var}}")
endforeach()
configure_file(installer/payload.rc.in "${CMAKE_CURRENT_BINARY_DIR}/installer_payload.rc" @ONLY)

# Stage the freshly built executables where the .rc expects them. Depending on the staged copies
# (not the targets) is what makes the installer relink after any product change.
add_custom_command(
  OUTPUT
    "${_gnlink_payload_dir}/GNLinkHost.exe"
    "${_gnlink_payload_dir}/GNLinkStream.exe"
    "${_gnlink_payload_dir}/GNLinkInputService.exe"
    "${_gnlink_payload_dir}/GNLinkCapture.exe"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${_gnlink_payload_dir}"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "$<TARGET_FILE:remote60_host_app>" "${_gnlink_payload_dir}/GNLinkHost.exe"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "$<TARGET_FILE:remote60_native_video_host_poc>"
          "${_gnlink_payload_dir}/GNLinkStream.exe"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "$<TARGET_FILE:remote60_secure_input_service>"
          "${_gnlink_payload_dir}/GNLinkInputService.exe"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "$<TARGET_FILE:remote60_gdi_capture_worker>"
          "${_gnlink_payload_dir}/GNLinkCapture.exe"
  DEPENDS remote60_host_app remote60_native_video_host_poc
          remote60_secure_input_service remote60_gdi_capture_worker
  COMMENT "Staging GNLink installer payload"
  VERBATIM
)
add_custom_target(remote60_installer_payload
  DEPENDS
    "${_gnlink_payload_dir}/GNLinkHost.exe"
    "${_gnlink_payload_dir}/GNLinkStream.exe"
    "${_gnlink_payload_dir}/GNLinkInputService.exe"
    "${_gnlink_payload_dir}/GNLinkCapture.exe"
)

add_executable(remote60_installer WIN32
  installer/installer_main.cpp
  installer/installer_ui.rc
  "${CMAKE_CURRENT_BINARY_DIR}/installer_payload.rc"
)
set_target_properties(remote60_installer PROPERTIES OUTPUT_NAME "GNLinkSetup")
target_include_directories(remote60_installer PRIVATE installer)
# Deliberately links no project library: the installer must stay dependency-free so it runs on a
# machine that has nothing installed yet.
target_link_libraries(remote60_installer PRIVATE
  advapi32
  shell32
  ole32
  uuid
  user32
)
add_dependencies(remote60_installer remote60_installer_payload)
if(MSVC)
  target_link_options(remote60_installer PRIVATE
    "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'"
  )
endif()

add_executable(remote60_udp_control_e2e_test
  src/native_video_client_shared_core.cpp
  src/native_video_client_tcp_control.cpp
  src/udp_control_channel.cpp
  src/native_video_client_session.cpp
  src/udp_control_e2e_test.cpp
)

target_include_directories(remote60_udp_control_e2e_test PRIVATE
  src
)

target_link_libraries(remote60_udp_control_e2e_test PRIVATE
  common
  ws2_32
)

# Ring policy and buffer pool of the capture readback pipeline; no GPU involved.
add_executable(remote60_capture_readback_test
  src/d3d_capture_readback.cpp
  src/capture_readback_test.cpp
)
target_include_directories(remote60_capture_readback_test PRIVATE src)
target_link_libraries(remote60_capture_readback_test PRIVATE d3d11)

add_executable(remote60_input_macro_test
  src/input_macro.cpp
  src/input_macro_test.cpp
)

target_include_directories(remote60_input_macro_test PRIVATE
  src
)

add_executable(remote60_mf_h264_codec_test
  src/mf_h264_codec.cpp
  src/mf_h264_codec_test.cpp
)
target_include_directories(remote60_mf_h264_codec_test PRIVATE src)
target_link_libraries(remote60_mf_h264_codec_test PRIVATE
  d3d11
  mfplat
  mf
  mfuuid
  wmcodecdspuuid
  ole32
)

add_executable(remote60_video_playout_clock_test
  src/video_playout_clock_test.cpp
)
target_include_directories(remote60_video_playout_clock_test PRIVATE src)

add_executable(remote60_udp_control_channel_test
  src/udp_control_channel.cpp
  src/udp_control_channel_test.cpp
)

target_include_directories(remote60_udp_control_channel_test PRIVATE
  src
)

target_link_libraries(remote60_udp_control_channel_test PRIVATE
  ws2_32
)
