# The WebView2 SDK is a NuGet package of headers and a static loader, fetched into third_party
add_executable(remote60_recovery_process_test src/recovery_process_test.cpp src/udp_control_channel.cpp)
target_include_directories(remote60_recovery_process_test PRIVATE src)
target_link_libraries(remote60_recovery_process_test PRIVATE ws2_32)
add_executable(remote60_winhttp_recovery_test src/winhttp_recovery_test.cpp src/winhttp_transport.cpp)
target_include_directories(remote60_winhttp_recovery_test PRIVATE src)
target_link_libraries(remote60_winhttp_recovery_test PRIVATE winhttp)

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
  src/host_unlock_relay.cpp
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
  src/host_stage_encode_send_h264_au.cpp
  src/host_stage_stats.cpp
  src/host_stage_stats_h264.cpp
  src/d3d_capture_readback.cpp
  src/directory_client.cpp
  src/update_endpoint.cpp
  src/url_origin.cpp
  src/winhttp_transport.cpp
  src/gdi_capture_process.cpp
  src/udp_control_channel.cpp
  src/mf_h264_codec.cpp
  src/secure_input_broker.cpp
)
set_target_properties(remote60_native_video_host_poc PROPERTIES OUTPUT_NAME "GNLinkStream")

# Reuse this worktree's compiled production objects for the entry/unwind fixture. The only
# replacement TU is main; all shutdown/control/watchdog implementations are the product objects.
# These object paths belong to the Visual Studio generator, so do not assume them on Ninja.
if(CMAKE_GENERATOR MATCHES "Visual Studio")
  get_target_property(_host_failure_sources remote60_native_video_host_poc SOURCES)
  set(_host_failure_objects)
  foreach(_source IN LISTS _host_failure_sources)
    if(_source MATCHES "\\.cpp$" AND NOT _source MATCHES "native_video_host_main.cpp$")
      get_filename_component(_object_name "${_source}" NAME_WE)
      list(APPEND _host_failure_objects "${CMAKE_CURRENT_BINARY_DIR}/remote60_native_video_host_poc.dir/$<CONFIG>/${_object_name}.obj")
    endif()
  endforeach()
  add_executable(remote60_host_runtime_failure_test src/host_runtime_failure_test.cpp)
  target_include_directories(remote60_host_runtime_failure_test PRIVATE src "${CMAKE_SOURCE_DIR}/libs/capture/src")
  target_link_libraries(remote60_host_runtime_failure_test PRIVATE ${_host_failure_objects})
  target_link_libraries(remote60_host_runtime_failure_test PRIVATE $<TARGET_PROPERTY:remote60_native_video_host_poc,LINK_LIBRARIES>)
  target_compile_definitions(remote60_host_runtime_failure_test PRIVATE $<TARGET_PROPERTY:remote60_native_video_host_poc,COMPILE_DEFINITIONS>)
  add_dependencies(remote60_host_runtime_failure_test remote60_native_video_host_poc)
endif()

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
  remote60_sealed_unlock
)

add_executable(remote60_native_video_client_poc
  src/native_video_client_main.cpp
  src/viewer_startup.cpp
  src/viewer_shutdown.cpp
  src/viewer_present.cpp
  src/viewer_picker_state.cpp
  src/viewer_selection_gate.cpp
  src/viewer_frame_gate.cpp
  src/viewer_control_client.cpp
  src/viewer_unlock.cpp
  src/viewer_video_receiver.cpp
  src/viewer_video_receiver_frame.cpp
  src/viewer_session_watchdog.cpp
  src/viewer_window_proc.cpp
  src/viewer_cursor_overlay.cpp
  src/viewer_overlay_draw.cpp
  src/viewer_picker.cpp
  src/viewer_input_forward.cpp
  src/viewer_layout.cpp
  src/viewer_gdi_util.cpp
  src/viewer_args.cpp
  src/viewer_log.cpp
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
  src/winhttp_transport.cpp
  src/directory_session_bootstrap.cpp
  src/directory_rendezvous.cpp
  src/directory_client.cpp
  src/update_endpoint.cpp
  src/url_origin.cpp
  # The macro window is a page too, so the viewer needs the same shell the client uses.
  src/macro_shell_bridge.cpp
)
# Korean literals reach the user from this target: the macro window's messages, and the picker's
# header and status lines. The flag used to sit inside the WebView2 conditional, which made the
# encoding of user-visible text depend on whether an unrelated SDK was found -- the same shape as
# the Host bug where /utf-8 was simply absent (history #523).
if(MSVC)
  target_compile_options(remote60_native_video_client_poc PRIVATE /utf-8)
endif()

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
  remote60_sealed_unlock
  crypt32
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

# The log uploader's credential lifecycle (P10): token replaced / identity changed / 401 pause /
# bounded retry / permanent discard, against an in-process fake directory on 127.0.0.1.
add_executable(remote60_log_upload_test
  src/log_upload.cpp
  src/directory_client.cpp
  src/update_endpoint.cpp
  src/url_origin.cpp
  src/winhttp_transport.cpp
  src/log_upload_test.cpp
)
target_include_directories(remote60_log_upload_test PRIVATE src)
target_link_libraries(remote60_log_upload_test PRIVATE common ws2_32 iphlpapi shell32)

# The uploader's SHUTDOWN, in child processes, because the failure is an exit rather than an
# assertion: a worker thread left joinable inside a function-local static is std::terminate() on
# the way out. Includes a negative control (no guard -> the child must not exit 0) and a source
# check that the product's two entry points actually declare the guard -- the test above calls
# log_upload_stop() nine times and never did prove anything ships that call.
add_executable(remote60_log_upload_shutdown_test
  src/log_upload.cpp
  src/directory_client.cpp
  src/update_endpoint.cpp
  src/url_origin.cpp
  src/winhttp_transport.cpp
  src/log_upload_shutdown_test.cpp
)
target_include_directories(remote60_log_upload_shutdown_test PRIVATE src)
target_compile_definitions(remote60_log_upload_shutdown_test PRIVATE
  "REMOTE60_MAIN_SRC_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/src\""
)
target_link_libraries(remote60_log_upload_shutdown_test PRIVATE common ws2_32 iphlpapi shell32)

# The thing a user installs: sign-in window plus tray, supervising the streaming host.
add_executable(remote60_host_app WIN32
  src/host_app_main.cpp
  src/directory_client.cpp
  src/update_endpoint.cpp
  src/url_origin.cpp
  src/winhttp_transport.cpp
  src/log_upload.cpp
  # The tray's "Check for updates" entry point and everything it needs to answer honestly, plus
  # the handoff that decides when this process may get out of the updater's way.
  src/update_check.cpp
  src/update_endpoint.cpp
  src/url_origin.cpp
  src/update_handoff.cpp
  src/update_credential_channel.cpp
  src/update_handoff_wait.cpp
  src/update_job_guard.cpp
  src/update_http.cpp
  src/update_manifest.cpp
  src/payload_name.cpp
  src/update_signature.cpp
  host_app.rc
)
set_target_properties(remote60_host_app PROPERTIES OUTPUT_NAME "GNLinkHost")
# The Korean the user reads comes from wide literals in host_app_main.cpp, and that file is UTF-8.
# Without /utf-8 MSVC reads it in the system code page (949 here) and stores what it misread --
# which is exactly what C4819 has been warning about on this target for a long time. The client
# and a dozen test targets already carry this flag; the Host, the one with the message boxes the
# user actually reads, did not.
target_compile_options(remote60_host_app PRIVATE /utf-8)

target_include_directories(remote60_host_app PRIVATE
  src
)

target_link_libraries(remote60_host_app PRIVATE
  ws2_32
  comctl32
  shlwapi
  bcrypt
  winhttp
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
set(GNLINK_PAYLOAD_UPDATER "${_gnlink_payload_dir}/GNLinkUpdater.exe")

# The .rc reader takes these paths literally, so escape the separators for the generated script.
foreach(_var GNLINK_INSTALLER_ICON GNLINK_PAYLOAD_HOST_APP GNLINK_PAYLOAD_VIDEO_HOST
             GNLINK_PAYLOAD_SECURE_INPUT GNLINK_PAYLOAD_GDI_WORKER
             GNLINK_PAYLOAD_CLIENT_SHELL GNLINK_PAYLOAD_CLIENT_VIEWER GNLINK_PAYLOAD_CLIENT_UI
             GNLINK_PAYLOAD_MACRO_UI GNLINK_PAYLOAD_UPDATER)
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
    "${_gnlink_payload_dir}/GNLinkUpdater.exe"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${_gnlink_payload_dir}"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "$<TARGET_FILE:remote60_updater>" "${_gnlink_payload_dir}/GNLinkUpdater.exe"
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
          remote60_client_shell remote60_native_video_client_poc remote60_updater
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
    "${_gnlink_payload_dir}/GNLinkUpdater.exe"
)

add_executable(remote60_installer WIN32
  installer/installer_main.cpp
  # Registration is shared with the updater so the two cannot drift, and so the version written
  # into DisplayVersion is an argument rather than a constant compiled into whichever binary
  # happens to be doing the registering.
  src/install_registration.cpp
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

add_executable(remote60_host_abr_test
  src/host_abr_test.cpp
)
target_include_directories(remote60_host_abr_test PRIVATE src)

add_executable(remote60_host_frame_gate_test
  src/host_frame_gate_test.cpp
)
target_include_directories(remote60_host_frame_gate_test PRIVATE src)

add_executable(remote60_host_kick_test
  src/host_kick_test.cpp
)
target_include_directories(remote60_host_kick_test PRIVATE src)

# P11 on the real MF H264Encoder: the flush epoch rides the accepted-input FIFO, a pre-flush AU
# surfacing after the flush is reproduced and then gated (the new epoch's first send is its IDR),
# the gate's rules and bounds on fabricated AUs, and the hold as observed on this backend.
add_executable(remote60_host_encode_epoch_test
  src/mf_h264_codec.cpp
  src/host_encode_epoch_test.cpp
)
target_include_directories(remote60_host_encode_epoch_test PRIVATE src)
# [4] drives the product's own EncoderState::TryProvenanceResync (host_encoder_manager.hpp), which
# reaches the capture backend headers through host_capture_session.hpp.
target_link_libraries(remote60_host_encode_epoch_test PRIVATE
  capture
  d3d11
  mfplat
  mf
  mfuuid
  wmcodecdspuuid
  ole32
)

add_executable(remote60_host_main_loop_mailbox_test
  src/host_main_loop_mailbox_test.cpp
)
target_include_directories(remote60_host_main_loop_mailbox_test PRIVATE src)

# The observe-endpoint contract, against the vectors the JS and Kotlin suites also read. All
# three runtimes had this rule wrong in the same way once, which is what a shared file is for.
add_executable(remote60_observe_vectors_test
  src/observe_vectors_test.cpp
  src/directory_client.cpp
  src/update_endpoint.cpp
  src/url_origin.cpp
  src/winhttp_transport.cpp
)
target_include_directories(remote60_observe_vectors_test PRIVATE src)
target_link_libraries(remote60_observe_vectors_test PRIVATE common ws2_32 iphlpapi shell32)
target_compile_definitions(remote60_observe_vectors_test PRIVATE
  "REMOTE60_OBSERVE_VECTORS_PATH=\"${CMAKE_CURRENT_SOURCE_DIR}/../shared/observe_endpoint_vectors.txt\""
)
if(MSVC)
  target_compile_options(remote60_observe_vectors_test PRIVATE /utf-8)
endif()

# The preview retry policy: how many times an unresponsive window is asked before the host stops
# asking, and when it starts again. No window and no GDI -- time is a parameter, so the sixty
# second cooldown is a number here rather than a minute of test runtime.
add_executable(remote60_host_thumbnail_budget_test
  src/host_thumbnail_budget_test.cpp
)
target_include_directories(remote60_host_thumbnail_budget_test PRIVATE src)
if(MSVC)
  target_compile_options(remote60_host_thumbnail_budget_test PRIVATE /utf-8)
endif()

# The version-comparison contract, checked against the vectors the JS and Kotlin suites also
# read. The vectors path is baked in so the test can be run from any working directory.
add_executable(remote60_version_compare_test
  src/version_compare_test.cpp
)
target_include_directories(remote60_version_compare_test PRIVATE src)
target_compile_definitions(remote60_version_compare_test PRIVATE
  "REMOTE60_VERSION_VECTORS_PATH=\"${CMAKE_CURRENT_SOURCE_DIR}/../shared/version_compare_vectors.txt\""
)
if(MSVC)
  target_compile_options(remote60_version_compare_test PRIVATE /utf-8)
endif()

# Manifest parsing plus real CNG signature verification, and the ordering between them.
# Checks a REAL release manifest with the verifier the product ships, using the compiled-in key.
# Not a test: a tool the release procedure runs against the document about to be published. The
# suite above proves the shipped key accepts a fixture signed by that key, which is a different
# claim from "the product accepts this document".
add_executable(remote60_verify_release
  src/update_manifest.cpp
  src/update_http.cpp
  src/payload_name.cpp
  src/update_signature.cpp
  tools/verify_release_manifest.cpp
)
target_include_directories(remote60_verify_release PRIVATE src)
target_link_libraries(remote60_verify_release PRIVATE bcrypt winhttp)
if(MSVC)
  target_compile_options(remote60_verify_release PRIVATE /utf-8)
endif()

# The stop request against REAL processes, through the production functions.
#
# Links update_process_targets.cpp on purpose -- every other suite deliberately does not, and the
# result was that request_process_stop() had one caller in the product and none in any test. The
# branch that failed in the field had never been executed.
# The registration path's COM dependency, with the real shell APIs. The updater had no apartment
# at all, so CLSID_ShellLink was refused and every update rolled back after a successful swap --
# reported only as "registration failed". Links go to a directory beside the test binary, never the
# machine's Start menu.
add_executable(remote60_update_registration_com_test
  src/update_registration_wiring.cpp
  src/install_registration.cpp
  src/update_registration_com_test.cpp
)
target_include_directories(remote60_update_registration_com_test PRIVATE src)
target_link_libraries(remote60_update_registration_com_test PRIVATE ole32 shell32 advapi32 uuid)
if(MSVC)
  target_compile_options(remote60_update_registration_com_test PRIVATE /utf-8)
endif()

add_executable(remote60_update_stop_process_test
  src/update_effects.cpp
  src/update_process_targets.cpp
  src/update_process_identity.cpp
  src/update_relaunch_plan.cpp
  src/update_state_machine.cpp
  src/update_manifest.cpp
  src/update_http.cpp
  src/payload_name.cpp
  src/update_signature.cpp
  src/update_stop_process_test.cpp
)
target_include_directories(remote60_update_stop_process_test PRIVATE src)
target_compile_definitions(remote60_update_stop_process_test PRIVATE
  "REMOTE60_HOST_APP_SRC=\"${CMAKE_CURRENT_SOURCE_DIR}/src/host_app_main.cpp\""
)
target_link_libraries(remote60_update_stop_process_test PRIVATE bcrypt winhttp user32 advapi32)
if(MSVC)
  target_compile_options(remote60_update_stop_process_test PRIVATE /utf-8)
endif()

add_executable(remote60_update_manifest_test
  src/update_manifest.cpp
  src/update_http.cpp
  src/payload_name.cpp
  src/update_signature.cpp
  src/update_manifest_test.cpp
)
target_include_directories(remote60_update_manifest_test PRIVATE src)
target_link_libraries(remote60_update_manifest_test PRIVATE bcrypt winhttp)
target_compile_definitions(remote60_update_manifest_test PRIVATE
  "REMOTE60_UPDATE_VECTORS_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/../shared/update_manifest\""
)
if(MSVC)
  target_compile_options(remote60_update_manifest_test PRIVATE /utf-8)
endif()

# The updater's ordering and failure rules, against a fake seam -- no real process termination
# or file replacement is attached yet, which is the point.
add_executable(remote60_update_state_machine_test
  src/update_state_machine.cpp
  src/update_manifest.cpp
  src/update_http.cpp
  src/payload_name.cpp
  src/update_signature.cpp
  src/update_state_machine_test.cpp
)
target_include_directories(remote60_update_state_machine_test PRIVATE src)
target_link_libraries(remote60_update_state_machine_test PRIVATE bcrypt winhttp)
if(MSVC)
  target_compile_options(remote60_update_state_machine_test PRIVATE /utf-8)
endif()

# Compiled but linked into nothing yet. This is the code that finds a real GNLinkHost.exe by
# name; it has no consumer until the updater is wired into a product binary. Building it as its
# own library keeps it honest -- it cannot silently rot while unused -- without putting those
# symbols anywhere a test could reach them.
add_library(remote60_update_process_targets STATIC
  src/update_process_targets.cpp
)
target_include_directories(remote60_update_process_targets PRIVATE src)
if(MSVC)
  target_compile_options(remote60_update_process_targets PRIVATE /utf-8)
endif()

# The other production-only half of the updater, alongside remote60_update_process_targets and
# for the same reason: this is the code that creates processes, talks to the SCM and reads the
# live host log. Built so it cannot rot, linked into nothing, so no test binary contains those
# symbols. The DECISIONS behind it (update_relaunch_plan.cpp, update_health.cpp) are separate and
# are linked into remote60_update_relaunch_test.
add_library(remote60_update_relaunch STATIC
  src/update_relaunch.cpp
  src/update_relaunch_plan.cpp
  src/update_health.cpp
  src/update_health_log.cpp
)
target_include_directories(remote60_update_relaunch PRIVATE src)
target_link_libraries(remote60_update_relaunch PRIVATE advapi32 ole32 oleaut32 shell32)
if(MSVC)
  target_compile_options(remote60_update_relaunch PRIVATE /utf-8)
endif()

# The update path's HTTPS client. The security rules it enforces are pure functions, so most of
# this suite needs no server; the one case that opens a socket points a TLS client at a cleartext
# listener on loopback and requires it to fail.
add_executable(remote60_update_http_test
  src/update_http.cpp
  src/update_http_test.cpp
)
target_include_directories(remote60_update_http_test PRIVATE src)
target_link_libraries(remote60_update_http_test PRIVATE winhttp ws2_32)
if(MSVC)
  target_compile_options(remote60_update_http_test PRIVATE /utf-8)
endif()

# What the tray item and the client's start-up check ask, with the fetcher injected so no test
# opens a socket. Pins that "could not reach it" never comes back as "up to date".
add_executable(remote60_update_check_test
  src/update_check.cpp
  src/update_endpoint.cpp
  src/url_origin.cpp
  src/update_http.cpp
  src/update_manifest.cpp
  src/payload_name.cpp
  src/update_signature.cpp
  src/update_check_test.cpp
)
target_include_directories(remote60_update_check_test PRIVATE src)
target_link_libraries(remote60_update_check_test PRIVATE bcrypt winhttp)
if(MSVC)
  target_compile_options(remote60_update_check_test PRIVATE /utf-8)
endif()

# Registration under a scratch HKCU key, with netsh, the SCM and the Start menu all recorded
# rather than performed. Nothing here touches the real uninstall key or GNLinkSecureInput.
add_executable(remote60_install_registration_test
  src/install_registration.cpp
  src/install_registration_test.cpp
)
target_include_directories(remote60_install_registration_test PRIVATE src)
target_link_libraries(remote60_install_registration_test PRIVATE advapi32)
if(MSVC)
  target_compile_options(remote60_install_registration_test PRIVATE /utf-8)
endif()

# Payload names from a manifest, and every shape of them that must not become a path.
add_executable(remote60_payload_name_test
  src/payload_name.cpp
  src/payload_name_test.cpp
)
target_include_directories(remote60_payload_name_test PRIVATE src)
if(MSVC)
  target_compile_options(remote60_payload_name_test PRIVATE /utf-8)
endif()

# The production UpdateEffects, driven inside a temp directory.
#
# NOTE: src/update_process_targets.cpp is deliberately NOT in this target. That file is the only
# code that can find a real GNLinkHost.exe by name, and leaving it unlinked means this test
# cannot reach it even by mistake -- the symbols are not in the binary.
add_executable(remote60_update_effects_test
  src/update_effects.cpp
  src/update_process_identity.cpp
  src/payload_name.cpp
  src/update_registration_wiring.cpp
  src/install_registration.cpp
  src/update_state_machine.cpp
  src/update_manifest.cpp
  src/update_http.cpp
  src/update_signature.cpp
  src/update_effects_test.cpp
)
target_include_directories(remote60_update_effects_test PRIVATE src)
target_link_libraries(remote60_update_effects_test PRIVATE bcrypt winhttp advapi32 ole32 uuid shell32)
if(MSVC)
  target_compile_options(remote60_update_effects_test PRIVATE /utf-8)
endif()

# A whole release of real files, driven end to end.
#
# Same link-time isolation as remote60_update_effects_test: src/update_process_targets.cpp is
# deliberately NOT in this target, so nothing here can find a real GNLinkHost.exe by name.
#
# The artifact bodies are the shared vector files, handed in as a path rather than copied, so this
# suite and the JavaScript and Kotlin manifest suites are reading the same bytes.
add_executable(remote60_update_release_test
  src/update_effects.cpp
  src/update_process_identity.cpp
  src/payload_name.cpp
  src/update_registration_wiring.cpp
  src/install_registration.cpp
  src/update_state_machine.cpp
  src/update_manifest.cpp
  src/update_http.cpp
  src/update_signature.cpp
  src/update_release_test.cpp
)
target_include_directories(remote60_update_release_test PRIVATE src)
target_compile_definitions(remote60_update_release_test PRIVATE
  GNLINK_UPDATE_VECTOR_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../shared/update_manifest/files")
target_link_libraries(remote60_update_release_test PRIVATE bcrypt winhttp advapi32 ole32 uuid shell32)
if(MSVC)
  target_compile_options(remote60_update_release_test PRIVATE /utf-8)
endif()

# The two decisions behind Relaunch and HealthCheck.
#
# src/update_relaunch.cpp -- the code that actually starts processes and talks to the SCM -- is
# deliberately NOT in this target. The decisions are testable; making them happen is not something
# this binary should be able to do.
add_executable(remote60_update_relaunch_test
  # update_relaunch.cpp IS linked here now. It was not, and that was wrong: the code that starts
  # processes had never executed, and "linked into nothing" was written up as isolation when it
  # was really an admission. Every path, name, service name and log it touches is an argument, so
  # this test points it at a temp directory full of dummies and runs the real OS path.
  src/update_relaunch.cpp
  src/update_relaunch_plan.cpp
  src/update_health.cpp
  src/update_health_log.cpp
  # The real identity comparison, not a stand-in. The liveness check decides whether to start
  # something, and a test answering that question its own way would be testing its own answer.
  src/update_process_identity.cpp
  src/update_relaunch_test.cpp
)
# Inside the repository: this suite fires launches through the user's shell, and the files it
# leaves behind are the ones a late request will look for.
target_compile_definitions(remote60_update_relaunch_test
  PRIVATE GNLINK_EXEC_ROOT=L"${CMAKE_SOURCE_DIR}/.claude/relaunch-runs")
target_include_directories(remote60_update_relaunch_test PRIVATE src)
target_link_libraries(remote60_update_relaunch_test PRIVATE advapi32 ole32 oleaut32 shell32)
if(MSVC)
  target_compile_options(remote60_update_relaunch_test PRIVATE /utf-8)
endif()

# GNLinkUpdater.exe -- the first and only caller of run_update.
#
# This is where every update_*.cpp finally connects. It is also the only production binary that
# links update_process_targets.cpp and update_relaunch.cpp: the two translation units that can
# reach a real GNLinkHost.exe by name and actually start processes.
add_executable(remote60_updater
  src/updater_main.cpp
  src/updater_effects.cpp
  src/update_endpoint.cpp
  src/url_origin.cpp
  src/update_handoff.cpp
  src/update_credential_channel.cpp
  src/update_handoff_wait.cpp
  src/updater_options.cpp
  src/update_job_guard.cpp
  src/update_state_machine.cpp
  src/update_effects.cpp
  src/update_process_identity.cpp
  src/update_manifest.cpp
  src/update_signature.cpp
  src/update_http.cpp
  src/update_check.cpp
  src/update_endpoint.cpp
  src/url_origin.cpp
  src/payload_name.cpp
  src/install_registration.cpp
  src/update_registration_wiring.cpp
  src/update_process_targets.cpp
  src/update_relaunch.cpp
  src/update_relaunch_plan.cpp
  src/update_health.cpp
  src/update_health_log.cpp
)
set_target_properties(remote60_updater PROPERTIES OUTPUT_NAME "GNLinkUpdater")
target_include_directories(remote60_updater PRIVATE src)
target_link_libraries(remote60_updater PRIVATE
  bcrypt winhttp advapi32 ole32 oleaut32 uuid shell32 user32
)
if(MSVC)
  target_compile_options(remote60_updater PRIVATE /utf-8)
  # It replaces files in %ProgramFiles% and restarts a service. Asking for the rights up front
  # means it fails at launch rather than half way through a swap.
  target_link_options(remote60_updater PRIVATE
    "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'"
  )
endif()

# Two conditions that used to be assumed and are now refused by default: surviving the process
# that started us, and a working directory only administrators can write.
add_executable(remote60_update_job_guard_test
  src/update_job_guard.cpp
  src/update_job_guard_test.cpp
)
target_include_directories(remote60_update_job_guard_test PRIVATE src)
target_link_libraries(remote60_update_job_guard_test PRIVATE advapi32)
if(MSVC)
  target_compile_options(remote60_update_job_guard_test PRIVATE /utf-8)
endif()

# Six ways an update can end, driven through the real assembly with real running processes.
#
# The verdict logic and the state machine each have suites and both pass. What neither shows is
# that when the UPDATER assembles them, every ending produces the right terminal result, the right
# backup lifetime and the right number of processes afterwards -- which is what a user actually
# has, and is decided by the combination.
# The fixture the scenario suite runs as a stand-in for a product executable.
#
# A real PE, so the launch paths under test are the real ones -- but one that RECORDS that it ran
# and then exits. Copies of cmd.exe were used before, and they never exit; every scenario left one
# behind, which is why the suite needed a sweep that terminated processes by name. Worse, nothing
# could tell whether a shell-routed launch had actually happened, so the suite removed its
# directory while a request was still on its way and the user got a "cannot find" dialog.
add_executable(remote60_scn_dummy src/scn_dummy_main.cpp)
if(MSVC)
  target_compile_options(remote60_scn_dummy PRIVATE /utf-8)
endif()

add_executable(remote60_updater_scenarios_test
  src/updater_scenarios_test.cpp
  src/updater_effects.cpp
  src/update_endpoint.cpp
  src/url_origin.cpp
  src/update_handoff.cpp
  src/update_credential_channel.cpp
  src/update_handoff_wait.cpp
  src/updater_options.cpp
  src/update_state_machine.cpp
  src/update_effects.cpp
  src/update_process_identity.cpp
  src/update_manifest.cpp
  src/update_signature.cpp
  src/update_http.cpp
  src/payload_name.cpp
  src/install_registration.cpp
  src/update_registration_wiring.cpp
  src/update_process_targets.cpp
  src/update_relaunch.cpp
  src/update_relaunch_plan.cpp
  src/update_health.cpp
  src/update_health_log.cpp
)
# The suite's files live INSIDE the repository. It used to build its tree under %TEMP% and then
# delete everything there matching its own name pattern, which is deleting outside the repository
# root -- forbidden, and not something running a test grants permission for.
target_compile_definitions(remote60_updater_scenarios_test
  PRIVATE GNLINK_SCN_ROOT=L"${CMAKE_SOURCE_DIR}/.claude/scenario-runs")
# The suite needs the fixture built before it runs, and needs to know where it landed.
add_dependencies(remote60_updater_scenarios_test remote60_scn_dummy)
target_compile_definitions(remote60_updater_scenarios_test
  PRIVATE GNLINK_SCN_DUMMY=L"$<TARGET_FILE:remote60_scn_dummy>")
target_include_directories(remote60_updater_scenarios_test PRIVATE src)
target_link_libraries(remote60_updater_scenarios_test PRIVATE
  bcrypt winhttp advapi32 ole32 oleaut32 uuid shell32 user32
 rstrtmgr)
if(MSVC)
  target_compile_options(remote60_updater_scenarios_test PRIVATE /utf-8)
endif()

# The updater's own assembly, driven end to end.
#
# Every other suite builds an UpdateEffectsConfig by hand; this one drives UpdaterEffects -- the
# same class the product binary uses. That layer had no test, and all five defects found in review
# lived in it while 890 checks stayed green.
#
# It links update_process_targets.cpp because product_image_names() defines what a swap replaces.
# Nothing here calls the enumerator: UpdaterDeps supplies it, and this test supplies its own.
add_executable(remote60_updater_assembly_test
  src/updater_assembly_test.cpp
  src/updater_effects.cpp
  src/update_endpoint.cpp
  src/url_origin.cpp
  src/update_handoff.cpp
  src/update_credential_channel.cpp
  src/update_handoff_wait.cpp
  src/updater_options.cpp
  src/update_state_machine.cpp
  src/update_effects.cpp
  src/update_process_identity.cpp
  src/update_manifest.cpp
  src/update_signature.cpp
  src/update_http.cpp
  src/payload_name.cpp
  src/install_registration.cpp
  src/update_registration_wiring.cpp
  src/update_process_targets.cpp
  src/update_relaunch.cpp
  src/update_relaunch_plan.cpp
  src/update_health.cpp
  src/update_health_log.cpp
)
target_include_directories(remote60_updater_assembly_test PRIVATE src)
target_link_libraries(remote60_updater_assembly_test PRIVATE
  bcrypt winhttp advapi32 ole32 oleaut32 uuid shell32 user32
)
if(MSVC)
  target_compile_options(remote60_updater_assembly_test PRIVATE /utf-8)
endif()

# When the host may get out of the way, and what it hands over. Pure decisions -- nothing here
# starts a process or waits on a handle.
# The two roles the handover test cannot play itself, as real processes.
add_executable(remote60_handoff_fixture
  src/handoff_fixture_main.cpp
  src/update_handoff.cpp
  src/update_credential_channel.cpp
)
target_include_directories(remote60_handoff_fixture PRIVATE src)
if(MSVC)
  target_compile_options(remote60_handoff_fixture PRIVATE /utf-8)
endif()

# The handover across three real processes. Two of them are started for real, and the host's own
# wait -- await_handoff, the function the product calls -- runs in this one. A test that reproduced
# the wait instead of calling it would be testing the reproduction, which is how the original
# stayed wrong.
add_executable(remote60_update_handoff_process_test
  src/update_handoff.cpp
  src/update_credential_channel.cpp
  src/update_handoff_wait.cpp
  src/update_handoff_process_test.cpp
  src/update_endpoint.cpp
  src/url_origin.cpp
  src/directory_client.cpp
  src/winhttp_transport.cpp
)
add_dependencies(remote60_update_handoff_process_test remote60_handoff_fixture)
target_include_directories(remote60_update_handoff_process_test PRIVATE src)
target_link_libraries(remote60_update_handoff_process_test PRIVATE common ws2_32 iphlpapi shell32 winhttp advapi32)
target_compile_definitions(remote60_update_handoff_process_test PRIVATE
  GNLINK_HANDOFF_FIXTURE=L"$<TARGET_FILE:remote60_handoff_fixture>"
  GNLINK_HANDOFF_ROOT=L"${CMAKE_SOURCE_DIR}/.claude/handoff-runs")
if(MSVC)
  target_compile_options(remote60_update_handoff_process_test PRIVATE /utf-8)
endif()

add_executable(remote60_update_handoff_test
  src/update_handoff.cpp
  src/update_credential_channel.cpp
  src/update_handoff_wait.cpp
  src/update_handoff_test.cpp
)
target_include_directories(remote60_update_handoff_test PRIVATE src)
if(MSVC)
  target_compile_options(remote60_update_handoff_test PRIVATE /utf-8)
endif()

# What the updater accepts as instructions. Pure parsing and validation -- the entry point that
# uses it is remote60_updater, which this does not link.
add_executable(remote60_updater_options_test
  src/updater_options.cpp
  src/updater_options_test.cpp
)
target_include_directories(remote60_updater_options_test PRIVATE src)
if(MSVC)
  target_compile_options(remote60_updater_options_test PRIVATE /utf-8)
endif()

add_executable(remote60_host_command_line_test
  src/host_command_line_test.cpp
)
target_include_directories(remote60_host_command_line_test PRIVATE src)
target_link_libraries(remote60_host_command_line_test PRIVATE shell32)

add_executable(remote60_host_bgra_scale_test
  src/host_bgra_scale_test.cpp
  src/host_bgra_scale.cpp
  src/host_args.cpp
)
target_include_directories(remote60_host_bgra_scale_test PRIVATE src)
target_link_libraries(remote60_host_bgra_scale_test PRIVATE common)

add_executable(remote60_host_sender_queue_policy_test
  src/host_sender_queue_policy_test.cpp
)
target_include_directories(remote60_host_sender_queue_policy_test PRIVATE src)

# P11 sender boundary on the real sender thread over loopback UDP: old-epoch AUs queued when the
# flush lands never start on the wire, an AU already being chunked completes (the documented
# exception), the new epoch's IDR is the next AU, and the fence is the epoch, not the generation.
add_executable(remote60_host_sender_epoch_test
  src/host_encoded_sender.cpp
  src/host_net_io.cpp
  src/directory_client.cpp
  src/update_endpoint.cpp
  src/url_origin.cpp
  src/winhttp_transport.cpp
  src/host_sender_epoch_test.cpp
)
target_include_directories(remote60_host_sender_epoch_test PRIVATE src)
target_link_libraries(remote60_host_sender_epoch_test PRIVATE
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

add_executable(remote60_host_backend_policy_test
  src/host_backend_policy_test.cpp
)
target_include_directories(remote60_host_backend_policy_test PRIVATE src)
target_link_libraries(remote60_host_backend_policy_test PRIVATE capture)

# Viewer split refactor T1: the recv thread's frame gate (stale drop, congestion state machine, keyframe wait)
# driven with time as an argument and a recording sink; no decoder, no sockets.
add_executable(remote60_viewer_frame_gate_test
  src/viewer_frame_gate.cpp
  src/viewer_frame_gate_test.cpp
)
target_include_directories(remote60_viewer_frame_gate_test PRIVATE src)
target_link_libraries(remote60_viewer_frame_gate_test PRIVATE common)

# The session-liveness verdict (recv-thread stall / silent link / dead session), pure logic.
add_executable(remote60_viewer_liveness_test
  src/viewer_liveness_test.cpp
)
target_include_directories(remote60_viewer_liveness_test PRIVATE src)

# Windows receive-path integration test (Windows NACK wiring / recovery state machine): the real
# VideoReceiver::run_udp -> assembler (in-order hold) -> FrameGate -> MF H264Decoder chain on a real
# UDP socket, against an in-process fake host that encodes real H.264 with the product's
# H264Encoder, chunks AUs with the host's wire geometry, drops / reorders / paces chunks, answers or
# ignores NACKs and honours or withholds keyframe requests. The two UI-thread entry points the path
# calls are stubbed in the test; no window, swap chain or capture backend is involved, so unlike the
# GNLinkStream+GNLinkViewer e2e it does not depend on the RDP state of the machine.
add_executable(remote60_viewer_udp_recovery_test
  src/host_encoded_sender.cpp
  src/host_net_io.cpp
  src/directory_client.cpp
  src/update_endpoint.cpp
  src/url_origin.cpp
  src/winhttp_transport.cpp
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
  capture
  common
  ws2_32
  runtimeobject
  windowsapp
  advapi32
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
add_executable(remote60_viewer_selection_gate_test
  src/viewer_selection_gate.cpp
  src/viewer_selection_gate_test.cpp
)
target_include_directories(remote60_viewer_selection_gate_test PRIVATE src)
target_link_libraries(remote60_viewer_selection_gate_test PRIVATE common)

# Viewer split refactor T3: the picker gesture latch (DOWN/UP on the same target, 300 ms stability, cancel on capture loss).
add_executable(remote60_viewer_picker_gesture_test
  src/viewer_picker_state.cpp
  src/native_video_client_shared_core.cpp
  src/viewer_picker_gesture_test.cpp
)
target_include_directories(remote60_viewer_picker_gesture_test PRIVATE src)
target_link_libraries(remote60_viewer_picker_gesture_test PRIVATE common ws2_32)

# Viewer split refactor T4: the viewer layout arithmetic (aspect fit, card grid, client layout, point mapping, hit test).
# Draws the real picker into a bitmap without a host: the panel's model takes a plain struct, so
# no part of the wire path is involved. A tool, not a test -- it produces pictures and judges
# nothing.
add_executable(remote60_picker_preview
  tools/picker_preview.cpp
  src/viewer_overlay_draw.cpp
  src/viewer_gdi_util.cpp
  src/viewer_layout.cpp
  src/viewer_picker_state.cpp
  src/viewer_log.cpp
  src/native_video_client_shared_core.cpp
  src/client_session_toolbar.cpp
)
target_include_directories(remote60_picker_preview PRIVATE src)
target_link_libraries(remote60_picker_preview PRIVATE user32 gdi32 gdiplus shlwapi ws2_32)
if(MSVC)
  target_compile_options(remote60_picker_preview PRIVATE /utf-8)
endif()

# The picker's empty-state decision, tested as a table with a negative control. Header-only logic:
# no window, no GDI, and no /utf-8 needed because it holds no Korean.
add_executable(remote60_picker_empty_state_test
  src/picker_empty_state_test.cpp
)
target_include_directories(remote60_picker_empty_state_test PRIVATE src)

add_executable(remote60_viewer_layout_test
  src/viewer_layout_test.cpp
)
target_include_directories(remote60_viewer_layout_test PRIVATE src)
target_link_libraries(remote60_viewer_layout_test PRIVATE user32 gdi32)

add_executable(remote60_bind_port_candidates_test
  src/bind_port_candidates_test.cpp
)
target_include_directories(remote60_bind_port_candidates_test PRIVATE src)

add_executable(remote60_secure_input_session_test
  src/secure_input_session_test.cpp
)
target_include_directories(remote60_secure_input_session_test PRIVATE src)

# Sealed unlock v1 crypto + replay primitives (CNG). Shared by the test now and the service/host later.
add_library(remote60_sealed_unlock STATIC
  src/sealed_unlock.cpp
)
target_include_directories(remote60_sealed_unlock PUBLIC src)
target_link_libraries(remote60_sealed_unlock PUBLIC bcrypt)

add_executable(remote60_sealed_unlock_test
  src/sealed_unlock_test.cpp
)
target_include_directories(remote60_sealed_unlock_test PRIVATE src)
target_link_libraries(remote60_sealed_unlock_test PRIVATE remote60_sealed_unlock)

add_executable(remote60_unlock_protocol_test
  src/unlock_protocol_test.cpp
)
target_include_directories(remote60_unlock_protocol_test PRIVATE src)
target_link_libraries(remote60_unlock_protocol_test PRIVATE remote60_sealed_unlock)

add_executable(remote60_dxgi_output_selection_test
  ${CMAKE_SOURCE_DIR}/libs/capture/src/dxgi_output_selection_test.cpp
)
target_link_libraries(remote60_dxgi_output_selection_test PRIVATE capture)

add_executable(remote60_secure_input_mapping_test
  src/secure_input_mapping_test.cpp
)
target_include_directories(remote60_secure_input_mapping_test PRIVATE src)

# The secure-input target rect rule (P9): the agent's rect follows the monitor the capture opened
# and is re-derived on every restart; window mode = no rect; origin-preserving for multi-monitor.
add_executable(remote60_host_input_target_rect_test
  src/host_input_target_rect_test.cpp
)
target_include_directories(remote60_host_input_target_rect_test PRIVATE src)

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
  src/update_endpoint.cpp
  src/url_origin.cpp
  src/winhttp_transport.cpp
  src/directory_session_bootstrap.cpp
  src/directory_rendezvous.cpp
)
target_include_directories(remote60_directory_session_client_test PRIVATE src)
target_link_libraries(remote60_directory_session_client_test PRIVATE common ws2_32)

# Counts what the clients ask for when the directory refuses with 409. The outcome alone does not
# separate "repaired it once" from "retried in a loop", so the assertions are about the counts.
add_executable(remote60_directory_retry_test
  src/directory_retry_test.cpp
  src/directory_session_client.cpp
  src/directory_client.cpp
  src/update_endpoint.cpp
  src/url_origin.cpp
  src/winhttp_transport.cpp
  src/directory_session_bootstrap.cpp
  src/directory_rendezvous.cpp
)
target_include_directories(remote60_directory_retry_test PRIVATE src)
target_link_libraries(remote60_directory_retry_test PRIVATE common ws2_32)

# The client's update check, through the production chain against a real socket. The check had
# one call site, before sign-in, so it always answered "not configured" and never ran again --
# a decision that lived where no test could reach it.
add_executable(remote60_client_update_flow_test
  src/client_update_flow_test.cpp
  src/client_update_gate.cpp
  src/client_shell_bridge.cpp
  src/update_check.cpp
  src/update_manifest.cpp
  src/update_endpoint.cpp
  src/url_origin.cpp
  src/update_http.cpp
  src/payload_name.cpp
  src/update_signature.cpp
  src/directory_client.cpp
  src/winhttp_transport.cpp
)
target_include_directories(remote60_client_update_flow_test PRIVATE src)
target_compile_definitions(remote60_client_update_flow_test PRIVATE
  "REMOTE60_UPDATE_VECTORS_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/../shared/update_manifest\""
  "REMOTE60_CLIENT_SHELL_SRC=\"${CMAKE_CURRENT_SOURCE_DIR}/src/client_shell_main.cpp\""
)
target_link_libraries(remote60_client_update_flow_test PRIVATE common ws2_32 bcrypt winhttp iphlpapi shell32)
if(MSVC)
  target_compile_options(remote60_client_update_flow_test PRIVATE /utf-8)
endif()

# Sends real mouse messages to a real toolbar window and checks that each of the three outcomes
# reports itself. Without it, an empty diagnostic log has two readings -- nobody clicked, or the
# logging was never wired -- and telling those apart afterwards is impossible.
# Runs the real viewer against a dead port and checks it does not disappear. Process-level
# because the thing being guarded is the process's lifetime.
add_executable(remote60_viewer_startup_failure_test
  src/viewer_startup_failure_test.cpp
)
target_link_libraries(remote60_viewer_startup_failure_test PRIVATE user32 ws2_32)
add_dependencies(remote60_viewer_startup_failure_test remote60_native_video_client_poc)

# Hypotheses 4 and 5: links the shipped window procedure on its own, with recording doubles
# standing in for the features it does not drive. Real mouse messages go through the gate (4), and
# the real present path below puts an actual flip-model swapchain in front of the window so the
# picker can be shown over a live frame (5).
add_executable(remote60_viewer_window_proc_isolated_test
  src/viewer_window_proc_isolated_test.cpp
  src/viewer_window_proc.cpp
  src/viewer_present.cpp
  src/mf_h264_codec.cpp
  src/client_session_toolbar.cpp
  src/viewer_picker.cpp
  src/viewer_picker_state.cpp
  src/viewer_selection_gate.cpp
  src/viewer_overlay_draw.cpp
  src/viewer_gdi_util.cpp
  src/viewer_layout.cpp
  src/viewer_log.cpp
  src/native_video_client_shared_core.cpp
)
target_include_directories(remote60_viewer_window_proc_isolated_test PRIVATE src)
# The real present path comes with it: D3D11, the shader compiler and Media Foundation. Linking
# them is what puts an actual flip-model swapchain in front of this window, which is the only way
# the reveal ordering can be reproduced at all.
target_link_libraries(remote60_viewer_window_proc_isolated_test PRIVATE
  user32 gdi32 shell32 ole32 shlwapi version ws2_32
  d3d11 dxgi d3dcompiler mfplat mf mfuuid wmcodecdspuuid)
if(MSVC)
  target_compile_options(remote60_viewer_window_proc_isolated_test PRIVATE /utf-8)
endif()

# Drives the whole target-selection chain with real mouse messages and the product's own picker
# call, and decides between the hypotheses by comparing pixels rather than reading a log line.
add_executable(remote60_picker_open_chain_test
  src/picker_open_chain_test.cpp
  src/client_session_toolbar.cpp
  src/viewer_picker.cpp
  src/viewer_picker_state.cpp
  src/viewer_selection_gate.cpp
  src/client_macro_window.cpp
  src/input_macro.cpp
  src/macro_shell_bridge.cpp
  src/viewer_overlay_draw.cpp
  src/viewer_gdi_util.cpp
  src/viewer_layout.cpp
  src/viewer_log.cpp
  src/native_video_client_shared_core.cpp
)
# The macro window is part of the toolbar's callbacks, and it is a WebView2 window.
target_include_directories(remote60_picker_open_chain_test
  PRIVATE src "${GNLINK_WEBVIEW2_DIR}/include")
target_link_libraries(remote60_picker_open_chain_test PRIVATE
  "${GNLINK_WEBVIEW2_DIR}/x64/WebView2LoaderStatic.lib"
  user32 gdi32 shell32 ole32 shlwapi version ws2_32)
if(MSVC)
  target_compile_options(remote60_picker_open_chain_test PRIVATE /utf-8)
endif()

add_executable(remote60_session_toolbar_click_test
  src/session_toolbar_click_test.cpp
  src/client_session_toolbar.cpp
)
target_include_directories(remote60_session_toolbar_click_test PRIVATE src)
target_link_libraries(remote60_session_toolbar_click_test PRIVATE user32 gdi32 shell32)
if(MSVC)
  target_compile_options(remote60_session_toolbar_click_test PRIVATE /utf-8)
endif()

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
    src/client_update_gate.cpp
    src/directory_session_client.cpp
  src/winhttp_transport.cpp
    src/directory_client.cpp
    src/update_endpoint.cpp
    src/url_origin.cpp
  src/update_endpoint.cpp
  src/url_origin.cpp
    src/log_upload.cpp
    # The start-up update check. Asking is all the client does: it is not elevated, so it cannot
    # replace anything in %ProgramFiles% itself.
    src/update_check.cpp
  src/update_endpoint.cpp
  src/url_origin.cpp
    src/update_handoff.cpp
    src/update_credential_channel.cpp
  src/update_credential_channel.cpp
  src/update_handoff_wait.cpp
    src/update_http.cpp
    src/update_manifest.cpp
    src/payload_name.cpp
    src/update_signature.cpp
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
    ws2_32 iphlpapi user32 ole32 shell32 shlwapi version gdi32 bcrypt winhttp
  )
  set_target_properties(remote60_client_shell PROPERTIES OUTPUT_NAME "GNLinkClient")
  # HTML-only fixes must be staged even when the executable does not need linking.
  add_custom_target(remote60_client_ui_assets
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:remote60_client_shell>/ui"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/ui/shell.html"
            "$<TARGET_FILE_DIR:remote60_client_shell>/ui/shell.html")
  add_dependencies(remote60_client_shell remote60_client_ui_assets)
  get_target_property(_recovery_shell_sources remote60_client_shell SOURCES)
  list(FILTER _recovery_shell_sources EXCLUDE REGEX "client_shell_main.cpp$")
  list(REMOVE_DUPLICATES _recovery_shell_sources)
  add_executable(remote60_client_recovery_ui_test src/client_recovery_ui_test.cpp ${_recovery_shell_sources})
  target_include_directories(remote60_client_recovery_ui_test PRIVATE src "${GNLINK_WEBVIEW2_DIR}/include")
  get_target_property(_recovery_shell_links remote60_client_shell LINK_LIBRARIES)
  target_link_libraries(remote60_client_recovery_ui_test PRIVATE ${_recovery_shell_links})
  add_dependencies(remote60_client_recovery_ui_test remote60_client_shell)

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

  # Renders the page that ships and presses the button on it. Guarded by the same SDK check as
  # the client itself: without WebView2 there is no page to render, and a source-reading
  # substitute is exactly what missed the button being absent in the first place.
  add_executable(remote60_client_update_ui_test
    src/client_update_ui_test.cpp
    src/client_shell_bridge.cpp
    src/update_manifest.cpp
    src/update_http.cpp
    src/payload_name.cpp
    src/update_signature.cpp
  )
  target_include_directories(remote60_client_update_ui_test
    PRIVATE src "${GNLINK_WEBVIEW2_DIR}/include")
  target_link_libraries(remote60_client_update_ui_test PRIVATE
    "${GNLINK_WEBVIEW2_DIR}/x64/WebView2LoaderStatic.lib"
    user32 ole32 shlwapi shell32 version bcrypt winhttp
  )
  # The client is built with /utf-8 and this renders the client's own strings; without it the
  # test's Korean arrives at the page as CP949 and the screenshot shows mojibake that the product
  # does not have.
  target_compile_options(remote60_client_update_ui_test PRIVATE /utf-8)

  # Photographs the shipping page in each state. A tool, not a test: it asserts nothing, so the
  # thing that produces the evidence is not also the thing that judges it.
  add_executable(remote60_ui_preview
    tools/ui_preview.cpp
    src/client_shell_bridge.cpp
  )
  target_include_directories(remote60_ui_preview PRIVATE src "${GNLINK_WEBVIEW2_DIR}/include")
  target_link_libraries(remote60_ui_preview PRIVATE
    "${GNLINK_WEBVIEW2_DIR}/x64/WebView2LoaderStatic.lib"
    user32 ole32 shlwapi shell32 version
  )
  target_compile_options(remote60_ui_preview PRIVATE /utf-8)
endif()

# Reads the TLS posture back off a live WinHTTP session, and dials a self-signed loopback fixture
# to see a bad certificate refused. What it deliberately does NOT test is in the file's header.
add_executable(remote60_winhttp_tls_posture_test
  src/winhttp_tls_posture_test.cpp
  src/winhttp_transport.cpp
)
target_include_directories(remote60_winhttp_tls_posture_test PRIVATE src)
target_link_libraries(remote60_winhttp_tls_posture_test PRIVATE winhttp)

# The two hops in real processes. The helper ships nowhere; it performs the same sequence
# updater_main performs so the chain can be driven without running the real updater (which would
# copy itself into an admin-only directory and stop product processes).
add_executable(remote60_cred_hop_helper
  src/update_credential_hop_helper.cpp
  src/update_credential_channel.cpp
  src/update_endpoint.cpp
  src/url_origin.cpp
)
target_include_directories(remote60_cred_hop_helper PRIVATE src)
target_link_libraries(remote60_cred_hop_helper PRIVATE advapi32)

add_executable(remote60_update_credential_hops_test
  src/update_credential_hops_test.cpp
  src/update_credential_channel.cpp
  src/update_endpoint.cpp
  src/url_origin.cpp
  src/directory_client.cpp
  src/winhttp_transport.cpp
)
target_include_directories(remote60_update_credential_hops_test PRIVATE src)
target_link_libraries(remote60_update_credential_hops_test PRIVATE common advapi32 ws2_32 iphlpapi shell32 winhttp)
add_dependencies(remote60_update_credential_hops_test remote60_cred_hop_helper)

# The credential channel: framing, and the check that refuses a client that is not the process we
# started. The refusal is driven by really connecting from the wrong process.
add_executable(remote60_update_credential_channel_test
  src/update_credential_channel_test.cpp
  src/update_credential_channel.cpp
)
target_include_directories(remote60_update_credential_channel_test PRIVATE src)
target_link_libraries(remote60_update_credential_channel_test PRIVATE advapi32)

# Holds the two http transports against one another. The socket path carries http and WinHTTP
# carries https, and the scheme decides which -- so any difference between them is a defect that
# shows up on one kind of deployment only. The server is in-process on an ephemeral loopback port.
add_executable(remote60_directory_http_contract_test
  src/directory_http_contract_test.cpp
  src/directory_client.cpp
  src/update_endpoint.cpp
  src/url_origin.cpp
  src/winhttp_transport.cpp
)
target_include_directories(remote60_directory_http_contract_test PRIVATE src)
target_link_libraries(remote60_directory_http_contract_test PRIVATE common ws2_32 iphlpapi shell32 winhttp)

# Diagnostic only. The parsing has unit tests; this answers the other half -- whether the requests
# are shaped the way a real directory expects.
add_executable(remote60_directory_login_probe
  tools/directory_login_probe.cpp
  src/directory_session_client.cpp
  src/winhttp_transport.cpp
  src/directory_client.cpp
  src/update_endpoint.cpp
  src/url_origin.cpp
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

add_executable(remote60_viewer_udp_ingress_test src/viewer_udp_ingress_test.cpp)
target_link_libraries(remote60_viewer_udp_ingress_test PRIVATE ws2_32)

add_executable(remote60_capture_final_update_test src/d3d_capture_readback.cpp src/capture_final_update_test.cpp)
target_link_libraries(remote60_capture_final_update_test PRIVATE d3d11 dxgi)

add_executable(remote60_windows_environment_snapshot_test src/windows_environment_snapshot_test.cpp)
add_executable(remote60_host_diagnostic_log_test src/host_diagnostic_log_test.cpp)
add_executable(remote60_peer_version_test src/peer_version_test.cpp src/udp_control_channel.cpp)
target_link_libraries(remote60_peer_version_test PRIVATE ws2_32)
if(MSVC)
  target_compile_options(remote60_host_diagnostic_log_test PRIVATE /utf-8)
endif()
