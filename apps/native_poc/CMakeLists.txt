# The WebView2 SDK is a NuGet package of headers and a static loader, fetched into third_party
# rather than vendored: it is 45 MB of which the build uses two files. Detected up here because
# both the client shell and the viewer's macro window depend on it.
set(GNLINK_WEBVIEW2_DIR "${CMAKE_SOURCE_DIR}/third_party/webview2/build/native")
if(EXISTS "${GNLINK_WEBVIEW2_DIR}/include/WebView2.h")
  set(GNLINK_HAVE_WEBVIEW2 TRUE)
else()
  set(GNLINK_HAVE_WEBVIEW2 FALSE)
  message(STATUS "WebView2 SDK not found at ${GNLINK_WEBVIEW2_DIR}; "
                 "run automation/fetch_webview2.ps1 to build the client")
endif()

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
)

target_include_directories(remote60_native_host_poc PRIVATE
  src
)

target_link_libraries(remote60_native_host_poc PRIVATE
  capture
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
  src/host_bgra_scale.cpp
  src/host_window_enum.cpp
  src/host_capture_device.cpp
  src/host_net_io.cpp
  src/host_input_inject.cpp
  src/host_args.cpp
  src/host_control_session.cpp
  src/host_encoded_sender.cpp
  src/host_capture_session.cpp
  src/host_encoder_manager.cpp
  src/host_loop_helpers.cpp
  src/host_startup_config.cpp
  src/host_startup_connect.cpp
  src/host_startup_control.cpp
  src/host_startup_graphics.cpp
  src/host_startup_capture.cpp
  src/host_shutdown.cpp
  src/host_stage_time_limit.cpp
  src/host_stage_backend.cpp
  src/host_stage_stream_active.cpp
  src/host_stage_runtime_tune.cpp
  src/host_stage_selection.cpp
  src/host_stage_geometry.cpp
  src/host_stage_watchdogs.cpp
  src/host_stage_pace.cpp
  src/host_stage_pop_frame.cpp
  src/host_stage_gate_static.cpp
  src/host_stage_encode_send.cpp
  src/host_stage_encode_send_h264.cpp
  src/host_stage_stats.cpp
  src/host_stage_stats_h264.cpp
  src/d3d_capture_readback.cpp
  src/directory_client.cpp
  src/gdi_capture_process.cpp
  src/udp_control_channel.cpp
  src/mf_h264_codec.cpp
  src/secure_input_broker.cpp
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
)

target_link_libraries(remote60_native_video_host_poc PRIVATE
  capture
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
  # The session controls live in their own window because the video is presented through a flip
  # swap chain, which erases anything drawn into the video window's own client area.
  src/client_session_toolbar.cpp
  src/input_macro.cpp
  src/native_video_client_shared_core.cpp
  src/native_video_client_tcp_control.cpp
  src/udp_control_channel.cpp
  src/mf_h264_codec.cpp
  # Reaching a host through the directory rather than by address: sign in, race the candidates,
  # keep the socket the winner answered on.
  src/directory_session_client.cpp
  src/directory_session_bootstrap.cpp
  src/directory_rendezvous.cpp
  src/directory_client.cpp
  # The macro window is a page too, so the viewer needs the same shell the client uses.
  src/macro_shell_bridge.cpp
)
set_target_properties(remote60_native_video_client_poc PROPERTIES OUTPUT_NAME "GNLinkViewer")

target_include_directories(remote60_native_video_client_poc PRIVATE
  src
)

# The viewer draws its macro window as a page, the same way the client draws its connect screen.
# Guarded because the SDK is fetched rather than vendored; without it the window falls back to
# nothing and the rest of the viewer still builds.
if(GNLINK_HAVE_WEBVIEW2)
  target_include_directories(remote60_native_video_client_poc PRIVATE
    "${GNLINK_WEBVIEW2_DIR}/include")
  target_link_libraries(remote60_native_video_client_poc PRIVATE
    "${GNLINK_WEBVIEW2_DIR}/x64/WebView2LoaderStatic.lib" ole32 shlwapi version)
  if(MSVC)
    # The macro window's messages are Korean in std::string, on their way to a UTF-8 page.
    target_compile_options(remote60_native_video_client_poc PRIVATE /utf-8)
  endif()
  add_custom_command(TARGET remote60_native_video_client_poc POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:remote60_native_video_client_poc>/ui"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/ui/macro.html"
            "$<TARGET_FILE_DIR:remote60_native_video_client_poc>/ui/macro.html"
    COMMENT "Staging GNLink macro UI"
  )
endif()

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
set(GNLINK_PAYLOAD_CLIENT_SHELL "${_gnlink_payload_dir}/GNLinkClient.exe")
set(GNLINK_PAYLOAD_CLIENT_VIEWER "${_gnlink_payload_dir}/GNLinkViewer.exe")
set(GNLINK_PAYLOAD_CLIENT_UI "${_gnlink_payload_dir}/shell.html")
set(GNLINK_PAYLOAD_MACRO_UI "${_gnlink_payload_dir}/macro.html")

# The .rc reader takes these paths literally, so escape the separators for the generated script.
foreach(_var GNLINK_INSTALLER_ICON GNLINK_PAYLOAD_HOST_APP GNLINK_PAYLOAD_VIDEO_HOST
             GNLINK_PAYLOAD_SECURE_INPUT GNLINK_PAYLOAD_GDI_WORKER
             GNLINK_PAYLOAD_CLIENT_SHELL GNLINK_PAYLOAD_CLIENT_VIEWER GNLINK_PAYLOAD_CLIENT_UI
             GNLINK_PAYLOAD_MACRO_UI)
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
    "${_gnlink_payload_dir}/GNLinkClient.exe"
    "${_gnlink_payload_dir}/GNLinkViewer.exe"
    "${_gnlink_payload_dir}/shell.html"
    "${_gnlink_payload_dir}/macro.html"
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
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "$<TARGET_FILE:remote60_client_shell>" "${_gnlink_payload_dir}/GNLinkClient.exe"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "$<TARGET_FILE:remote60_native_video_client_poc>"
          "${_gnlink_payload_dir}/GNLinkViewer.exe"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_CURRENT_SOURCE_DIR}/ui/shell.html" "${_gnlink_payload_dir}/shell.html"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_CURRENT_SOURCE_DIR}/ui/macro.html" "${_gnlink_payload_dir}/macro.html"
  DEPENDS remote60_host_app remote60_native_video_host_poc
          remote60_secure_input_service remote60_gdi_capture_worker
          remote60_client_shell remote60_native_video_client_poc
          "${CMAKE_CURRENT_SOURCE_DIR}/ui/shell.html"
          "${CMAKE_CURRENT_SOURCE_DIR}/ui/macro.html"
  COMMENT "Staging GNLink installer payload"
  VERBATIM
)
add_custom_target(remote60_installer_payload
  DEPENDS
    "${_gnlink_payload_dir}/GNLinkHost.exe"
    "${_gnlink_payload_dir}/GNLinkStream.exe"
    "${_gnlink_payload_dir}/GNLinkInputService.exe"
    "${_gnlink_payload_dir}/GNLinkCapture.exe"
    "${_gnlink_payload_dir}/GNLinkClient.exe"
    "${_gnlink_payload_dir}/GNLinkViewer.exe"
    "${_gnlink_payload_dir}/shell.html"
    "${_gnlink_payload_dir}/macro.html"
)

add_executable(remote60_installer WIN32
  installer/installer_main.cpp
  installer/installer_ui.rc
  "${CMAKE_CURRENT_BINARY_DIR}/installer_payload.rc"
)
set_target_properties(remote60_installer PROPERTIES OUTPUT_NAME "GNLinkSetup")
target_include_directories(remote60_installer PRIVATE installer src)
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

add_executable(remote60_capture_cadence_gate_test
  src/capture_cadence_gate_test.cpp
)
target_include_directories(remote60_capture_cadence_gate_test PRIVATE src)

add_executable(remote60_bind_port_candidates_test
  src/bind_port_candidates_test.cpp
)
target_include_directories(remote60_bind_port_candidates_test PRIVATE src)

add_executable(remote60_secure_input_session_test
  src/secure_input_session_test.cpp
)
target_include_directories(remote60_secure_input_session_test PRIVATE src)

add_executable(remote60_dxgi_output_selection_test
  ${CMAKE_SOURCE_DIR}/libs/capture/src/dxgi_output_selection_test.cpp
)
target_link_libraries(remote60_dxgi_output_selection_test PRIVATE capture)

add_executable(remote60_secure_input_mapping_test
  src/secure_input_mapping_test.cpp
)
target_include_directories(remote60_secure_input_mapping_test PRIVATE src)

add_executable(remote60_connect_candidates_test
  src/connect_candidates_test.cpp
)
target_include_directories(remote60_connect_candidates_test PRIVATE src)

add_executable(remote60_encode_resolution_ladder_test
  src/encode_resolution_ladder_test.cpp
)
target_include_directories(remote60_encode_resolution_ladder_test PRIVATE src)

add_executable(remote60_directory_session_client_test
  src/directory_session_client_test.cpp
  src/directory_client.cpp
)
target_include_directories(remote60_directory_session_client_test PRIVATE src)
target_link_libraries(remote60_directory_session_client_test PRIVATE common ws2_32)

add_executable(remote60_client_shell_bridge_test
  src/client_shell_bridge_test.cpp
  src/client_shell_bridge.cpp
)
target_include_directories(remote60_client_shell_bridge_test PRIVATE src)

add_executable(remote60_macro_shell_bridge_test
  src/macro_shell_bridge_test.cpp
  src/macro_shell_bridge.cpp
  src/input_macro.cpp
)
target_include_directories(remote60_macro_shell_bridge_test PRIVATE src)

add_executable(remote60_punch_any_test
  src/punch_any_test.cpp
  src/directory_rendezvous.cpp
)
target_include_directories(remote60_punch_any_test PRIVATE src)
target_link_libraries(remote60_punch_any_test PRIVATE common ws2_32)

# Throwaway diagnostic, not part of the product and not staged into the installer. Answers
# whether a SYSTEM process on the Winlogon desktop can read pixels, and with which API, before
# the secure-desktop capture work commits to one. Delete once 구현계획 U1 is answered.
if(GNLINK_HAVE_WEBVIEW2)
  # The window a person opens: sign in, pick a PC, start a session. The session itself runs in
  # GNLinkViewer.exe as a child, mirroring how GNLinkHost supervises GNLinkStream.
  add_executable(remote60_client_shell WIN32
    src/client_shell_main.cpp
    src/client_shell_bridge.cpp
    src/directory_session_client.cpp
    src/directory_client.cpp
  )
  target_include_directories(remote60_client_shell PRIVATE src "${GNLINK_WEBVIEW2_DIR}/include")
  if(MSVC)
    # The interface text is Korean in ordinary std::string, and MSVC otherwise encodes narrow
    # literals in the system code page while everything downstream -- the JSON, the page, the
    # log -- is UTF-8. Without this the messages arrive as mojibake.
    target_compile_options(remote60_client_shell PRIVATE /utf-8)
  endif()
  target_link_libraries(remote60_client_shell PRIVATE
    common
    "${GNLINK_WEBVIEW2_DIR}/x64/WebView2LoaderStatic.lib"
    ws2_32 iphlpapi user32 ole32 shell32 shlwapi version gdi32
  )
  set_target_properties(remote60_client_shell PROPERTIES OUTPUT_NAME "GNLinkClient")

  # The page is loaded from disk rather than compiled in, so it can be adjusted without a
  # rebuild; copied beside the executable so a build tree behaves like an install.
  add_custom_command(TARGET remote60_client_shell POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:remote60_client_shell>/ui"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/ui/shell.html"
            "$<TARGET_FILE_DIR:remote60_client_shell>/ui/shell.html"
    COMMENT "Staging GNLink client UI"
  )

  # Proves the shell before the client is built on it: SDK links, runtime present, HTML renders,
  # and JavaScript can reach C++. Diagnostic only -- nothing ships this.
  add_executable(remote60_webview2_spike
    tools/webview2_spike.cpp
  )
  target_include_directories(remote60_webview2_spike PRIVATE "${GNLINK_WEBVIEW2_DIR}/include")
  # Static loader, so no extra DLL travels with the installer.
  target_link_libraries(remote60_webview2_spike PRIVATE
    "${GNLINK_WEBVIEW2_DIR}/x64/WebView2LoaderStatic.lib"
    user32 ole32 shell32 version
  )
  set_target_properties(remote60_webview2_spike PROPERTIES OUTPUT_NAME "WebView2Spike")
endif()

# Diagnostic only. The parsing has unit tests; this answers the other half -- whether the requests
# are shaped the way a real directory expects.
add_executable(remote60_directory_login_probe
  tools/directory_login_probe.cpp
  src/directory_session_client.cpp
  src/directory_client.cpp
)
target_include_directories(remote60_directory_login_probe PRIVATE src)
target_link_libraries(remote60_directory_login_probe PRIVATE common ws2_32 iphlpapi shell32)
set_target_properties(remote60_directory_login_probe PROPERTIES OUTPUT_NAME "DirectoryLoginProbe")

add_executable(remote60_winlogon_capture_probe
  tools/winlogon_capture_probe.cpp
)
target_link_libraries(remote60_winlogon_capture_probe PRIVATE d3d11 dxgi gdi32 advapi32 user32)
set_target_properties(remote60_winlogon_capture_probe PROPERTIES OUTPUT_NAME "WinlogonProbe")

# Also diagnostic-only. Measures how often the desktop actually changes, with no encoder or
# client in the way, so 구현계획 A1 can be settled by measurement instead of inference.
add_executable(remote60_desktop_update_rate
  tools/desktop_update_rate.cpp
)
target_link_libraries(remote60_desktop_update_rate PRIVATE d3d11 dxgi)
set_target_properties(remote60_desktop_update_rate PROPERTIES OUTPUT_NAME "DesktopUpdateRate")

add_executable(remote60_udp_fec_interleave_test
  src/native_video_client_shared_core.cpp
  src/native_video_client_tcp_control.cpp
  src/udp_control_channel.cpp
  src/native_video_client_session.cpp
  src/udp_fec_interleave_test.cpp
)
target_include_directories(remote60_udp_fec_interleave_test PRIVATE src)
target_link_libraries(remote60_udp_fec_interleave_test PRIVATE common ws2_32)

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
