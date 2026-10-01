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
  src/clipboard_monitor.cpp
  src/host_clip_image.cpp
  src/clip_image_clipboard.cpp
  src/clip_image_wic.cpp
  src/host_thumbnail_helper.cpp
  src/host_thumbnail_kill.cpp
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

# TEST BUILD of the host for clipboard-image runs next to a user's session: the same sources and
# libraries as GNLinkStream, plus a publisher that writes received images to a folder
# (REMOTE60_CLIP_IMAGE_TEST_SINK_DIR) instead of the clipboard. Its own output name, never packaged;
# clip_image_build_gate_test checks the shipped GNLinkStream.exe carries none of it.
get_target_property(_clipsink_host_sources remote60_native_video_host_poc SOURCES)
add_executable(remote60_native_video_host_clipsink ${_clipsink_host_sources} src/host_clip_image_test_sink.cpp)
set_target_properties(remote60_native_video_host_clipsink PROPERTIES OUTPUT_NAME "GNLinkStreamClipSink")
target_include_directories(remote60_native_video_host_clipsink PRIVATE src)
target_compile_definitions(remote60_native_video_host_clipsink PRIVATE
  REMOTE60_CLIP_IMAGE_TEST_SINK=1
  REMOTE60_NATIVE_ENCODED_EXPERIMENT=${_native_encoded_compile_value}
)
target_link_libraries(remote60_native_video_host_clipsink PRIVATE
  remote60_file_copy_host
  capture common ws2_32 d3d11 mfplat mf mfuuid wmcodecdspuuid ole32 runtimeobject windowsapp advapi32
  bcrypt windowscodecs shlwapi winmm
)

# TEST BUILD of the host for the cached-sign-in migration: the same sources and libraries as
# GNLinkStream, with the product's server and its former names replaced by loopback fixtures read
# from the environment (REMOTE60_STREAM_TEST_SEAM, in directory_client.cpp and nowhere else). The
# line that hands the list to the agent, in host_startup_connect.cpp, is the product's in both.
# Its own output name, never packaged; automation/gnlink_check_fixed_server.py checks the shipped
# GNLinkStream.exe carries none of it.
get_target_property(_migration_host_sources remote60_native_video_host_poc SOURCES)
add_executable(remote60_native_video_host_migration_seam ${_migration_host_sources})
set_target_properties(remote60_native_video_host_migration_seam PROPERTIES
  OUTPUT_NAME "GNLinkStreamMigrationTest")
target_include_directories(remote60_native_video_host_migration_seam PRIVATE src)
target_compile_definitions(remote60_native_video_host_migration_seam PRIVATE
  REMOTE60_STREAM_TEST_SEAM=1
  REMOTE60_NATIVE_ENCODED_EXPERIMENT=${_native_encoded_compile_value}
)
target_link_libraries(remote60_native_video_host_migration_seam PRIVATE
  remote60_file_copy_host  # the same list as GNLinkStream: its sources reference file copy (t-zdmsd4gb)
  capture common ws2_32 d3d11 mfplat mf mfuuid wmcodecdspuuid ole32 runtimeobject windowsapp advapi32
  bcrypt windowscodecs shlwapi winmm
)

add_executable(remote60_clip_image_build_gate_test src/clip_image_build_gate_test.cpp)
add_dependencies(remote60_clip_image_build_gate_test remote60_native_video_host_poc remote60_native_video_host_clipsink
                 remote60_native_video_client_poc remote60_file_copy_helper remote60_file_copy_net_e2e_test)

add_executable(remote60_gdi_capture_worker
  src/gdi_capture_worker_main.cpp
  # The one-shot --thumbnail mode calls the product's own capture_window_thumbnail rather than a
  # second copy of it, which is what makes the helper's output the same picture the host used to
  # produce in process.
  src/host_bgra_scale.cpp
  src/host_args.cpp
)
set_target_properties(remote60_gdi_capture_worker PROPERTIES OUTPUT_NAME "GNLinkCapture")
target_include_directories(remote60_gdi_capture_worker PRIVATE src)
target_link_libraries(remote60_gdi_capture_worker PRIVATE
  common
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
  remote60_file_copy_host  # file copy (t-zdmsd4gb): the helper link, pulls, chunk checks
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
  bcrypt
  windowscodecs
  shlwapi
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

# Clipboard image v1 (plan .claude/clip_image_plan.md r1 + r2): the viewer's image client (package,
# offer/cancel/status, bulk stream, pacer) and the clipboard/WIC pieces it shares with the host. A
# library because the viewer's state holds the client, so every target that links the viewer's
# sources needs it.
add_library(remote60_clip_image STATIC
  src/clip_image_client.cpp
  # File copy, viewer side (t-zdmsd4gb): offer, paste query, pin as the user, the shared bulk sender.
  # The files are identified and opened by the helper's own user-token code.
  src/file_copy_client.cpp
  src/file_copy_client_progress.cpp
  src/file_copy_local_files.cpp
  src/clip_image_clipboard.cpp
  src/clip_image_wic.cpp
  # The viewer's transfer bar (percent, seconds, Cancel): in the library because every target built
  # from the viewer's window procedure moves and destroys it.
  src/viewer_clip_transfer_bar.cpp
)
target_include_directories(remote60_clip_image PUBLIC src)
target_link_libraries(remote60_clip_image PUBLIC remote60_file_copy_paste bcrypt windowscodecs ole32 shlwapi user32 gdi32 ws2_32)
if(MSVC)
  # The transfer bar's Korean lines (history #523: a Korean literal does not survive without it).
  target_compile_options(remote60_clip_image PRIVATE /utf-8)
endif()

# The Medium-integrity clipboard helper the elevated host starts as the interactive user
# (file-copy-helper r1): an async IDataObject for files on the viewer's PC, and the user's own
# rights for files the user copied here. One pipe to the host, no network. Not yet in the
# installer payload -- the network side (control 65~79, bulk) and the host wiring come with the
# file-copy task; this ships once that lands.
add_executable(remote60_file_copy_helper
  src/file_copy_helper_main.cpp
  src/file_copy_data_object.cpp
  src/file_copy_local_files.cpp
)
set_target_properties(remote60_file_copy_helper PROPERTIES OUTPUT_NAME "GNLinkClipHelper")
target_include_directories(remote60_file_copy_helper PRIVATE src)
target_link_libraries(remote60_file_copy_helper PRIVATE ole32 shell32 shlwapi user32 uuid)
if(MSVC)
  target_compile_options(remote60_file_copy_helper PRIVATE /utf-8)
endif()

# file-copy-helper r1 tests.
# The pipe codec: every message round-trips, every bound refuses, a truncated / padded payload is
# rejected, and names that may not travel do not. Pure.
# file-copy network (t-zdmsd4gb): the wire codec (65~78) and the rules both ends apply -- offer
# names / counts / sizes, ranges, per-chunk SHA-256, "whole file verified". No network, no files.
add_library(remote60_file_copy_net STATIC src/file_copy_net_rules.cpp)
target_include_directories(remote60_file_copy_net PUBLIC src)
target_link_libraries(remote60_file_copy_net PUBLIC bcrypt)
if(MSVC)
  target_compile_options(remote60_file_copy_net PRIVATE /utf-8)
endif()
# What both ends of a file paste are made of (file_copy_paste_parts.hpp): the helper channel and its
# launch, the receiving side's pulls and chunk checks, the serving side's chunks, and the paced bulk
# sender the image path shares. The bulk stream is a UdpControlChannel; targets that link it without
# the channel get it from here (a static library contributes it only where it is not already linked).
add_library(remote60_file_copy_paste STATIC
  src/file_copy_paste_parts.cpp
  src/file_copy_helper_host.cpp
  src/bulk_uplink.cpp
  src/bulk_pacer.cpp
  src/udp_control_channel.cpp
)
target_include_directories(remote60_file_copy_paste PUBLIC src)
target_link_libraries(remote60_file_copy_paste PUBLIC remote60_file_copy_net advapi32 wtsapi32 bcrypt ws2_32)
if(MSVC)
  target_compile_options(remote60_file_copy_paste PRIVATE /utf-8)
endif()
# The host's file-copy side, both directions (host_file_copy.hpp).
add_library(remote60_file_copy_host STATIC src/host_file_copy.cpp)
target_include_directories(remote60_file_copy_host PUBLIC src)
target_link_libraries(remote60_file_copy_host PUBLIC remote60_file_copy_paste)
if(MSVC)
  target_compile_options(remote60_file_copy_host PRIVATE /utf-8)
endif()
# P->R end to end: the product's viewer and host sides over loopback UDP, the real helper and a real
# paste consumer on a private window station, bytes compared. StagingDir scratch.
add_executable(remote60_file_copy_net_e2e_test src/file_copy_net_e2e_test.cpp)
target_link_libraries(remote60_file_copy_net_e2e_test PRIVATE remote60_clip_image remote60_file_copy_host
  remote60_test_scratch_dir ws2_32 ole32)
if(MSVC)
  target_compile_options(remote60_file_copy_net_e2e_test PRIVATE /utf-8)
endif()
add_dependencies(remote60_file_copy_net_e2e_test remote60_file_copy_helper remote60_file_copy_helper_e2e_test)
# The receiving side against a scripted serving side: stall (A4), progress, stale answers. No sockets.
add_executable(remote60_file_copy_receiver_test src/file_copy_receiver_test.cpp)
target_link_libraries(remote60_file_copy_receiver_test PRIVATE remote60_file_copy_paste)
if(MSVC)
  target_compile_options(remote60_file_copy_receiver_test PRIVATE /utf-8)
endif()
add_executable(remote60_file_copy_wire_test src/file_copy_wire_test.cpp)
target_link_libraries(remote60_file_copy_wire_test PRIVATE remote60_file_copy_net)
if(MSVC)
  target_compile_options(remote60_file_copy_wire_test PRIVATE /utf-8)
endif()

add_executable(remote60_file_copy_pipe_test
  src/file_copy_pipe_test.cpp
)
target_include_directories(remote60_file_copy_pipe_test PRIVATE src)
if(MSVC)
  target_compile_options(remote60_file_copy_pipe_test PRIVATE /utf-8)
endif()

# r2: the frame reader against the poll-boundary race that lost PasteBegin / PasteEnd in r1 --
# thousands of frames at 0-2 ms gaps polled at 1 ms, a frame arriving in pieces across timed-out
# polls, and the error vocabulary. The pre-r2 receive path is kept in the test to measure its loss.
add_executable(remote60_file_copy_pipe_io_test
  src/file_copy_pipe_io_test.cpp
  src/file_copy_helper_host.cpp
)
target_include_directories(remote60_file_copy_pipe_io_test PRIVATE src)
target_link_libraries(remote60_file_copy_pipe_io_test PRIVATE advapi32 wtsapi32 bcrypt)
if(MSVC)
  target_compile_options(remote60_file_copy_pipe_io_test PRIVATE /utf-8)
endif()

# r3: the data object against a transport that re-enters it -- a new paste or a cleared offer
# arriving while a descriptor / Read wait pumps must not confirm the new operation with the old
# reply nor hand bytes to a stream of a paste that ended. Deterministic: no shell, no pipe.
add_executable(remote60_file_copy_data_object_test
  src/file_copy_data_object_test.cpp
  src/file_copy_data_object.cpp
)
target_include_directories(remote60_file_copy_data_object_test PRIVATE src)
target_link_libraries(remote60_file_copy_data_object_test PRIVATE ole32 shell32 shlwapi user32 uuid)
if(MSVC)
  target_compile_options(remote60_file_copy_data_object_test PRIVATE /utf-8)
endif()

# The helper's local-file side in process: stat / classify, pin with FILE_SHARE_READ (a writer is
# refused, and refused while pinned), FileId check (replaced file refused), size / time check,
# positional reads, lease expiry with a fake clock, unpin. Files live in the repo's scratch root.
add_executable(remote60_file_copy_local_files_test
  src/file_copy_local_files_test.cpp
  src/file_copy_local_files.cpp
)
target_include_directories(remote60_file_copy_local_files_test PRIVATE src)
target_link_libraries(remote60_file_copy_local_files_test PRIVATE remote60_test_scratch_dir)
if(MSVC)
  target_compile_options(remote60_file_copy_local_files_test PRIVATE /utf-8)
endif()

# The real helper, started the way the product will (HelperLink: pipe + nonce + Job) but as this
# user (the Medium test launch), on a private window station so the interactive clipboard is never
# touched; this test is the host and, re-launched on that station, the Explorer-equivalent paste
# consumer (the destination folder's IDropTarget, driven as SHSimulateDrop does). Token refusals,
# handshake refusals, both directions, the counter-examples, and the helper's exit.
add_executable(remote60_file_copy_helper_e2e_test
  src/file_copy_helper_e2e_test.cpp
  src/file_copy_helper_host.cpp
)
target_include_directories(remote60_file_copy_helper_e2e_test PRIVATE src)
target_link_libraries(remote60_file_copy_helper_e2e_test PRIVATE
  remote60_test_scratch_dir ole32 shell32 shlwapi user32 advapi32 wtsapi32 bcrypt uuid)
if(MSVC)
  target_compile_options(remote60_file_copy_helper_e2e_test PRIVATE /utf-8)
endif()
add_dependencies(remote60_file_copy_helper_e2e_test remote60_file_copy_helper)

add_executable(remote60_native_video_client_poc
  src/native_video_client_main.cpp
  src/viewer_startup.cpp
  src/viewer_clip_image_wiring.cpp
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
  remote60_clip_image
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
  src/host_app_log.cpp
  src/update_readiness.cpp
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

# The same window in a build a test can start: every source GNLinkHost has, with the entry point
# renamed and three things replaced through REMOTE60_HOST_TEST_SEAM -- the directory, host.json
# and the streaming child. asInvoker, so a runner without elevation can start it and UI
# Automation can reach it; it therefore cannot show requireAdministrator, UAC or UIPI. Nothing
# ships this, and automation/gnlink_check_fixed_server.py checks the seam is not in what does.
get_target_property(_host_ui_test_sources remote60_host_app SOURCES)
list(FILTER _host_ui_test_sources EXCLUDE REGEX "host_app_main.cpp$")
list(REMOVE_DUPLICATES _host_ui_test_sources)
add_executable(remote60_host_app_ui_test WIN32
  src/host_app_ui_test.cpp
  ${_host_ui_test_sources}
)
set_target_properties(remote60_host_app_ui_test PROPERTIES OUTPUT_NAME "GNLinkHostUiTest")
target_compile_definitions(remote60_host_app_ui_test PRIVATE REMOTE60_HOST_TEST_SEAM=1)
target_compile_options(remote60_host_app_ui_test PRIVATE /utf-8)
target_include_directories(remote60_host_app_ui_test PRIVATE src)
target_link_libraries(remote60_host_app_ui_test PRIVATE
  ws2_32
  comctl32
  shlwapi
  bcrypt
  winhttp
)
if(MSVC)
  target_link_options(remote60_host_app_ui_test PRIVATE
    "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
  )
endif()

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
set(GNLINK_PAYLOAD_CLIP_HELPER "${_gnlink_payload_dir}/GNLinkClipHelper.exe")

# The .rc reader takes these paths literally, so escape the separators for the generated script.
foreach(_var GNLINK_INSTALLER_ICON GNLINK_PAYLOAD_HOST_APP GNLINK_PAYLOAD_VIDEO_HOST
             GNLINK_PAYLOAD_SECURE_INPUT GNLINK_PAYLOAD_GDI_WORKER
             GNLINK_PAYLOAD_CLIENT_SHELL GNLINK_PAYLOAD_CLIENT_VIEWER GNLINK_PAYLOAD_CLIENT_UI
             GNLINK_PAYLOAD_MACRO_UI GNLINK_PAYLOAD_UPDATER GNLINK_PAYLOAD_CLIP_HELPER)
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
    "${_gnlink_payload_dir}/GNLinkClipHelper.exe"
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
          "$<TARGET_FILE:remote60_file_copy_helper>"
          "${_gnlink_payload_dir}/GNLinkClipHelper.exe"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_CURRENT_SOURCE_DIR}/ui/shell.html" "${_gnlink_payload_dir}/shell.html"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_CURRENT_SOURCE_DIR}/ui/macro.html" "${_gnlink_payload_dir}/macro.html"
  DEPENDS remote60_host_app remote60_native_video_host_poc
          remote60_secure_input_service remote60_gdi_capture_worker
          remote60_client_shell remote60_native_video_client_poc remote60_updater
          remote60_file_copy_helper
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
    "${_gnlink_payload_dir}/GNLinkClipHelper.exe"
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

# quality r5: the closed loop on the encoder's quantiser ceiling (pure).
add_executable(remote60_host_rate_governor_test
  src/host_rate_governor_test.cpp
)
target_include_directories(remote60_host_rate_governor_test PRIVATE src)

# clip-image: limits, DIB header check, bulk demux/generations, bulk rate (pure).
add_executable(remote60_clip_image_core_test
  src/clip_image_core_test.cpp
)
target_include_directories(remote60_clip_image_core_test PRIVATE src)

# clip-image: DIB -> PNG -> CF_DIBV5 through WIC, header-first sizing.
add_executable(remote60_clip_image_wic_test
  src/clip_image_wic.cpp
  src/clip_image_wic_test.cpp
)
target_include_directories(remote60_clip_image_wic_test PRIVATE src)
target_link_libraries(remote60_clip_image_wic_test PRIVATE windowscodecs ole32 shlwapi)

# clip-image: two private window stations keep separate clipboards; WinSta0 does not move.
# clip-image direction A: the transfer state machines (tamper, stale epoch/generation, busy, cancel,
# stall/budget) and the bulk pacer on a real high-resolution timer.
add_executable(remote60_clip_image_transfer_test
  src/clip_image_transfer_test.cpp
  src/bulk_pacer.cpp
)
target_include_directories(remote60_clip_image_transfer_test PRIVATE src)
if(MSVC)
  target_compile_options(remote60_clip_image_transfer_test PRIVATE /utf-8)
endif()

# clip-image direction A, two-sided on loopback: the product client and host service over real UDP
# sockets, through an impairment proxy; the clipboard injected at the OS boundary (see the file).
# clip-image (Codex review of 4528dc9): the client against a scripted host -- cancel races,
# supersede, refusals and packages that never leave, deterministic.
add_executable(remote60_clip_image_client_test src/clip_image_client_test.cpp)
target_include_directories(remote60_clip_image_client_test PRIVATE src)
target_link_libraries(remote60_clip_image_client_test PRIVATE remote60_clip_image)
if(MSVC)
  target_compile_options(remote60_clip_image_client_test PRIVATE /utf-8)
endif()

# clip-image ④: the transfer bar's words and when it shows them -- the functions the bar calls.
add_executable(remote60_clip_transfer_bar_test src/clip_transfer_bar_test.cpp)
target_include_directories(remote60_clip_transfer_bar_test PRIVATE src)
target_link_libraries(remote60_clip_transfer_bar_test PRIVATE remote60_clip_image)
if(MSVC)
  target_compile_options(remote60_clip_transfer_bar_test PRIVATE /utf-8)
endif()

add_executable(remote60_clip_image_e2e_test
  src/clip_image_e2e_test.cpp
  src/host_clip_image.cpp
  src/clipboard_monitor.cpp
  src/udp_control_channel.cpp
)
target_include_directories(remote60_clip_image_e2e_test PRIVATE src)
target_link_libraries(remote60_clip_image_e2e_test PRIVATE remote60_clip_image winmm)
if(MSVC)
  target_compile_options(remote60_clip_image_e2e_test PRIVATE /utf-8)
endif()

# r2 ③: a repeated image offer must not release the running image's bulk (the product host service
# and the session's BulkArbiter; no network, no clipboard).
add_executable(remote60_clip_image_arbiter_test
  src/clip_image_arbiter_test.cpp
  src/host_clip_image.cpp
  src/clipboard_monitor.cpp
  src/udp_control_channel.cpp
)
target_include_directories(remote60_clip_image_arbiter_test PRIVATE src)
target_link_libraries(remote60_clip_image_arbiter_test PRIVATE remote60_clip_image winmm)
if(MSVC)
  target_compile_options(remote60_clip_image_arbiter_test PRIVATE /utf-8)
endif()

# clip-image direction A: the real clipboard, one side at a time, on a private window station --
# the viewer's snapshot read and the host's publish through HostClipboardHub (never WinSta0).
add_executable(remote60_clip_image_clipboard_test
  src/clip_image_clipboard_test.cpp
  src/clipboard_monitor.cpp
)
target_include_directories(remote60_clip_image_clipboard_test PRIVATE src)
target_link_libraries(remote60_clip_image_clipboard_test PRIVATE remote60_clip_image remote60_test_scratch_dir)
if(MSVC)
  target_compile_options(remote60_clip_image_clipboard_test PRIVATE /utf-8)
endif()

add_executable(remote60_clip_image_winsta_test
  src/clip_image_winsta_test.cpp
)
target_link_libraries(remote60_clip_image_winsta_test PRIVATE user32 ws2_32 remote60_test_scratch_dir)

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

# The lingering branch, executed. Links host_thumbnail_kill_fault.cpp INSTEAD of
# host_thumbnail_kill.cpp: the one OS call at the teardown boundary reports failure and terminates
# nothing, so the product's "the helper would not die" path runs for real. Everything else is the
# product. shipped_helper_gate_test checks the fault TU is in neither shipped executable.
add_executable(remote60_host_thumbnail_lingering_test
  src/host_thumbnail_lingering_test.cpp
  src/host_thumbnail_helper.cpp
  src/host_thumbnail_kill_fault.cpp
)
target_include_directories(remote60_host_thumbnail_lingering_test PRIVATE src)
if(MSVC)
  target_compile_options(remote60_host_thumbnail_lingering_test PRIVATE /utf-8)
endif()

# Every source CMakeLists names exists. Five commits in this branch did not configure because a
# target was registered while its source was left uncommitted -- it built for the author and for
# nobody else. This is the check that would have caught that, and it costs a file read.
add_executable(remote60_cmake_sources_exist_test
  src/cmake_sources_exist_test.cpp
)
target_include_directories(remote60_cmake_sources_exist_test PRIVATE src)
target_compile_definitions(remote60_cmake_sources_exist_test PRIVATE
  "REMOTE60_NATIVE_POC_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\""
)
if(MSVC)
  target_compile_options(remote60_cmake_sources_exist_test PRIVATE /utf-8)
endif()

# The build gate for the fault seam -- or rather, the check that there is no seam to gate. The
# stall used by the deadline tests is a different executable staged in a scratch directory, not a
# switch on the shipped one, and this reads the built GNLinkCapture to say so.
add_executable(remote60_shipped_helper_gate_test
  src/shipped_helper_gate_test.cpp
)
target_include_directories(remote60_shipped_helper_gate_test PRIVATE src)
if(MSVC)
  target_compile_options(remote60_shipped_helper_gate_test PRIVATE /utf-8)
endif()
add_dependencies(remote60_shipped_helper_gate_test
  remote60_gdi_capture_worker
  remote60_native_video_host_poc
)

# Both dispatchers at once. A UDP session (control tunnelled over the media socket) and a TCP
# session (its own port) against one host, with a helper that never answers -- so one dispatcher is
# inside a doomed preview while the other is asked for a round trip. This is where the budget lock
# and the helper cap of one meet two real Serve loops rather than two threads.
# item 8 C3 r3: the release-all trigger, driven through the product window procedure and worker.
add_executable(remote60_viewer_release_all_e2e_test
  src/viewer_release_all_e2e_test.cpp
  src/viewer_window_proc.cpp
  src/viewer_input_forward.cpp
  src/viewer_control_client.cpp
  src/viewer_present.cpp
  src/viewer_picker.cpp
  src/viewer_picker_state.cpp
  src/viewer_selection_gate.cpp
  src/viewer_overlay_draw.cpp
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
target_include_directories(remote60_viewer_release_all_e2e_test PRIVATE src)
target_link_libraries(remote60_viewer_release_all_e2e_test PRIVATE
  remote60_clip_image
  common user32 gdi32 shell32 ole32 shlwapi version ws2_32 imm32 bcrypt ncrypt
  d3d11 dxgi d3dcompiler mfplat mf mfuuid wmcodecdspuuid)
if(MSVC)
  target_compile_options(remote60_viewer_release_all_e2e_test PRIVATE /utf-8)
endif()

# mouse-xbutton r1: the back / forward buttons through the product's window procedure, the wire, a
# real isolated host and into a window of this process -- and not at all (--legacy-host) against a
# host that does not advertise them. Same link set as the release-all test: the product's viewer.
add_executable(remote60_viewer_mouse_xbutton_e2e_test
  src/viewer_mouse_xbutton_e2e_test.cpp
  src/viewer_window_proc.cpp
  src/viewer_input_forward.cpp
  src/viewer_control_client.cpp
  src/viewer_present.cpp
  src/viewer_picker.cpp
  src/viewer_picker_state.cpp
  src/viewer_selection_gate.cpp
  src/viewer_overlay_draw.cpp
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
target_include_directories(remote60_viewer_mouse_xbutton_e2e_test PRIVATE src)
target_link_libraries(remote60_viewer_mouse_xbutton_e2e_test PRIVATE
  remote60_clip_image  # the viewer's state holds the clipboard image client
  common user32 gdi32 shell32 ole32 shlwapi version ws2_32 imm32 bcrypt ncrypt
  d3d11 dxgi d3dcompiler mfplat mf mfuuid wmcodecdspuuid)
if(MSVC)
  target_compile_options(remote60_viewer_mouse_xbutton_e2e_test PRIVATE /utf-8)
endif()
add_dependencies(remote60_viewer_mouse_xbutton_e2e_test remote60_native_video_host_poc)

# clip-image ④ (3rd rate agreement, DECISIONS §5): the transfer bar -- percent, seconds, Cancel --
# through the product's window procedure, image client wiring and control worker, against a real
# isolated ClipSink host: screenshots while sending / after Cancel / after a publish, the cancel
# arriving at the host, and the next copy accepted. Same link set as the mouse-xbutton test.
add_executable(remote60_viewer_clip_bar_e2e_test
  src/viewer_clip_bar_e2e_test.cpp
  src/viewer_clip_image_wiring.cpp
  src/viewer_window_proc.cpp
  src/viewer_input_forward.cpp
  src/viewer_control_client.cpp
  src/viewer_present.cpp
  src/viewer_picker.cpp
  src/viewer_picker_state.cpp
  src/viewer_selection_gate.cpp
  src/viewer_overlay_draw.cpp
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
target_include_directories(remote60_viewer_clip_bar_e2e_test PRIVATE src)
target_link_libraries(remote60_viewer_clip_bar_e2e_test PRIVATE
  remote60_clip_image  # the viewer's state holds the clipboard image client
  remote60_test_scratch_dir  # scratch inside the repository, never %TEMP%
  common user32 gdi32 shell32 ole32 shlwapi version ws2_32 imm32 bcrypt ncrypt
  d3d11 dxgi d3dcompiler mfplat mf mfuuid wmcodecdspuuid)
if(MSVC)
  target_compile_options(remote60_viewer_clip_bar_e2e_test PRIVATE /utf-8)
endif()
add_dependencies(remote60_viewer_clip_bar_e2e_test remote60_native_video_host_clipsink)

# mouse-xbutton r1: the button vocabulary (wire bit <-> VK <-> MK_* / XBUTTON / MOUSEEVENTF_*), and
# the host's window-mode injection against a window of this process: X1 / X2 arrive as
# WM_XBUTTONDOWN/UP naming the right button, an unknown key posts nothing (it used to become a LEFT
# click), the kill switch makes an X edge unsupported. No host, no network, no SendInput.
add_executable(remote60_mouse_button_map_test
  src/mouse_button_map_test.cpp
  src/host_input_inject.cpp
  src/host_window_enum.cpp
)
target_include_directories(remote60_mouse_button_map_test PRIVATE src)
target_link_libraries(remote60_mouse_button_map_test PRIVATE user32 imm32)
if(MSVC)
  target_compile_options(remote60_mouse_button_map_test PRIVATE /utf-8)
endif()

# item 8 C3: a real host, a control-only break, and the repair measured end to end.
add_executable(remote60_control_resume_e2e_test
  src/control_resume_e2e_test.cpp
  src/native_video_client_shared_core.cpp
  src/native_video_client_tcp_control.cpp
  src/udp_control_channel.cpp
)
target_include_directories(remote60_control_resume_e2e_test PRIVATE src)
target_link_libraries(remote60_control_resume_e2e_test PRIVATE common ws2_32)
if(MSVC)
  target_compile_options(remote60_control_resume_e2e_test PRIVATE /utf-8)
endif()

add_executable(remote60_host_two_dispatcher_e2e_test
  src/host_two_dispatcher_e2e_test.cpp
  src/native_video_client_shared_core.cpp
  src/native_video_client_tcp_control.cpp
  src/udp_control_channel.cpp
  src/native_video_client_session.cpp
)
target_include_directories(remote60_host_two_dispatcher_e2e_test PRIVATE src)
target_link_libraries(remote60_host_two_dispatcher_e2e_test PRIVATE common ws2_32)
if(MSVC)
  target_compile_options(remote60_host_two_dispatcher_e2e_test PRIVATE /utf-8)
endif()
add_dependencies(remote60_host_two_dispatcher_e2e_test remote60_native_video_host_poc)

# The punch reply, end to end: a real GNLinkStream against a scripted fake directory, the
# product's DirectoryRendezvous picking a candidate, and the product's udp_hello_handshake
# opening a session on the socket that won the race -- with a capability issued against the
# host's PUBLIC tuple and presented from a private one. C1's earlier coverage stopped at the
# choice; this is whether the choice is worth anything.
add_executable(remote60_host_punch_reply_e2e_test
  src/host_punch_reply_e2e_test.cpp
  src/native_video_client_shared_core.cpp
  src/native_video_client_session.cpp
  src/native_video_client_tcp_control.cpp
  src/udp_control_channel.cpp
  src/directory_rendezvous.cpp
)
target_include_directories(remote60_host_punch_reply_e2e_test PRIVATE src)
target_link_libraries(remote60_host_punch_reply_e2e_test PRIVATE common ws2_32)
if(MSVC)
  target_compile_options(remote60_host_punch_reply_e2e_test PRIVATE /utf-8)
endif()
add_dependencies(remote60_host_punch_reply_e2e_test remote60_native_video_host_poc)

# The cached-sign-in migration, run as a process: a real streaming host (the test-seam build
# above) started the way GNLinkHost starts it, against fixture directories that record what they
# were sent. What is judged is the requests each address received and the bytes of the cache.
add_executable(remote60_host_migration_e2e_test
  src/host_migration_e2e_test.cpp
  src/directory_client.cpp
  src/update_endpoint.cpp
  src/url_origin.cpp
  src/winhttp_transport.cpp
)
target_include_directories(remote60_host_migration_e2e_test PRIVATE src)
target_link_libraries(remote60_host_migration_e2e_test PRIVATE common ws2_32)
if(MSVC)
  target_compile_options(remote60_host_migration_e2e_test PRIVATE /utf-8)
endif()
add_dependencies(remote60_host_migration_e2e_test remote60_native_video_host_migration_seam)

# Calling off a viewer that is already connecting, the way the shell does it. A real
# GNLinkViewer against a scripted fake directory, cancelled with the same function the shell
# calls, measured on how long it takes to stop and checked on why it says it stopped.
add_executable(remote60_viewer_cancel_e2e_test
  src/viewer_cancel_e2e_test.cpp
)
target_include_directories(remote60_viewer_cancel_e2e_test PRIVATE src)
target_link_libraries(remote60_viewer_cancel_e2e_test PRIVATE common ws2_32)
if(MSVC)
  target_compile_options(remote60_viewer_cancel_e2e_test PRIVATE /utf-8)
endif()
add_dependencies(remote60_viewer_cancel_e2e_test remote60_native_video_client_poc)

# The dispatcher itself. Runs the real GNLinkStream on isolated ports with a GNLinkCapture that
# never answers, connects to its real control port, and measures whether the channel still replies
# while a preview is timing out -- which is the thing that failed on 2026-09-15.
add_executable(remote60_host_control_deadline_e2e_test
  src/host_control_deadline_e2e_test.cpp
  src/native_video_client_tcp_control.cpp
  src/udp_control_channel.cpp
)
target_include_directories(remote60_host_control_deadline_e2e_test PRIVATE src)
target_link_libraries(remote60_host_control_deadline_e2e_test PRIVATE common ws2_32)
if(MSVC)
  target_compile_options(remote60_host_control_deadline_e2e_test PRIVATE /utf-8)
endif()
add_dependencies(remote60_host_control_deadline_e2e_test remote60_native_video_host_poc)

# The isolated preview path end to end. capture_thumbnail_isolated resolves its helper beside the
# running executable, so staging a scratch directory with a chosen GNLinkCapture.exe next to a copy
# of this test exercises the production path with no test hook in the product: once with the real
# helper, once with one that never answers.
add_executable(remote60_thumbnail_helper_e2e_test
  src/thumbnail_helper_e2e_test.cpp
  src/host_thumbnail_helper.cpp
  src/host_thumbnail_kill.cpp
  # Also calls capture_window_thumbnail directly, so the isolated path can be timed against the
  # in-process one it replaced in the same run rather than against another machine's numbers.
  src/host_bgra_scale.cpp
  src/host_args.cpp
)
target_include_directories(remote60_thumbnail_helper_e2e_test PRIVATE src)
target_link_libraries(remote60_thumbnail_helper_e2e_test PRIVATE common)
if(MSVC)
  target_compile_options(remote60_thumbnail_helper_e2e_test PRIVATE /utf-8)
endif()
add_dependencies(remote60_thumbnail_helper_e2e_test remote60_gdi_capture_worker)

# The four ways an isolated capture can end. The one that matters: a worker that dies without a
# result is seen the moment it dies, not after the deadline -- so an update stopping the helper, a
# crash, and a kill from anywhere else all arrive on the same path and cost the same.
add_executable(remote60_host_thumbnail_wait_test
  src/host_thumbnail_wait_test.cpp
)
target_include_directories(remote60_host_thumbnail_wait_test PRIVATE src)
if(MSVC)
  target_compile_options(remote60_host_thumbnail_wait_test PRIVATE /utf-8)
endif()

# When a client asks for a preview again. One predicate, two clients -- the Windows picker and the
# session controller the Android client drives both had the same defect, and sharing the rule is
# what stops it coming back on one side only.
add_executable(remote60_thumbnail_fetch_policy_test
  src/thumbnail_fetch_policy_test.cpp
)
target_include_directories(remote60_thumbnail_fetch_policy_test PRIVATE src)
if(MSVC)
  target_compile_options(remote60_thumbnail_fetch_policy_test PRIVATE /utf-8)
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

# Runs a program under a minimal debugger and reports the exception record and stack.
#
# 0xC0000409 is raised by __fastfail for several different reasons and the first exception
# parameter is the one that says which -- a stack cookie failure and an ordinary abort() are the
# same status code. __fastfail bypasses SEH, so only a debugger can read those parameters.
add_executable(remote60_crash_probe src/crash_probe.cpp)
target_link_libraries(remote60_crash_probe PRIVATE dbghelp)
if(MSVC)
  target_compile_options(remote60_crash_probe PRIVATE /utf-8)
endif()

# Debug info for the suite under investigation, so the probe's stacks carry names rather than
# offsets. /Zi does not change code generation; it only emits a PDB beside the binary.
set_property(TARGET remote60_crash_probe PROPERTY MSVC_DEBUG_INFORMATION_FORMAT
  "$<$<CONFIG:Release>:ProgramDatabase>")

add_executable(remote60_update_stop_process_test
  src/update_effects.cpp
  src/update_target_watch.cpp
  src/update_rollback_safety.cpp
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
# Symbols for this target, for the crash investigation. /Zi is debug information only -- the
# optimiser settings are untouched -- and /DEBUG makes the linker emit the PDB.
target_compile_options(remote60_update_stop_process_test PRIVATE $<$<CONFIG:Release>:/Zi>)
target_link_options(remote60_update_stop_process_test PRIVATE $<$<CONFIG:Release>:/DEBUG>)
target_include_directories(remote60_update_stop_process_test PRIVATE src)
target_compile_definitions(remote60_update_stop_process_test PRIVATE
  "REMOTE60_HOST_APP_SRC=\"${CMAKE_CURRENT_SOURCE_DIR}/src/host_app_main.cpp\""
)
target_link_libraries(remote60_update_stop_process_test PRIVATE bcrypt winhttp user32 advapi32
  remote60_test_scratch_dir)
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
  src/update_readiness.cpp
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
# Test-only seam: makes "disable redirects" report failure and counts the sends after it. Defined
# for this target and no other -- automation/gnlink_check_fixed_server.py looks for its marker in
# what ships.
target_compile_definitions(remote60_update_http_test PRIVATE REMOTE60_UPDATE_HTTP_TEST_SEAM=1)
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
  src/update_target_watch.cpp
  src/update_rollback_safety.cpp
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
  src/update_target_watch.cpp
  src/update_rollback_safety.cpp
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
  src/update_readiness.cpp
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
target_link_libraries(remote60_update_relaunch_test PRIVATE bcrypt)
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
  src/update_target_watch.cpp
  src/update_rollback_safety.cpp
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
  src/update_readiness.cpp
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

# The same updater, built asInvoker so a non-elevated runner can start it. Same sources, same
# compile options; only the manifest differs. Not a payload: the release copies
# $<TARGET_FILE:remote60_updater>, never this. It cannot show requireAdministrator, UAC, or the
# admin-only working directory -- remote60_updater_exit_record_test does not rely on any of them.
get_target_property(_gnlink_updater_sources remote60_updater SOURCES)
add_executable(remote60_updater_asinvoker_test ${_gnlink_updater_sources})
target_include_directories(remote60_updater_asinvoker_test PRIVATE src)
target_link_libraries(remote60_updater_asinvoker_test PRIVATE
  bcrypt winhttp advapi32 ole32 oleaut32 uuid shell32 user32
)
if(MSVC)
  target_compile_options(remote60_updater_asinvoker_test PRIVATE /utf-8)
  target_link_options(remote60_updater_asinvoker_test PRIVATE
    "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
  )
endif()

# Window titles cut at a character, not a byte; the Android bridge's UTF-8 <-> UTF-16 (2026-09-25).
add_executable(remote60_utf8_bounded_test src/utf8_bounded_test.cpp src/host_window_enum.cpp)
target_include_directories(remote60_utf8_bounded_test PRIVATE src)
target_link_libraries(remote60_utf8_bounded_test PRIVATE user32 dwmapi)
if(MSVC)
  target_compile_options(remote60_utf8_bounded_test PRIVATE /utf-8)
endif()

# Every updater run ends with a verdict line, and the working copy's start is recorded (0.0.10).
add_executable(remote60_updater_exit_record_test
  src/update_credential_channel.cpp
  src/updater_exit_record_test.cpp
)
target_include_directories(remote60_updater_exit_record_test PRIVATE src)
target_compile_definitions(remote60_updater_exit_record_test PRIVATE
  GNLINK_UPDATER_ASINVOKER=L"$<TARGET_FILE:remote60_updater_asinvoker_test>"
)
target_link_libraries(remote60_updater_exit_record_test PRIVATE advapi32)
add_dependencies(remote60_updater_exit_record_test remote60_updater_asinvoker_test)

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
  src/update_target_watch.cpp
  src/update_rollback_safety.cpp
  src/update_process_identity.cpp
  src/update_manifest.cpp
  src/update_signature.cpp
  src/update_http.cpp
  src/payload_name.cpp
  src/install_registration.cpp
  src/update_registration_wiring.cpp
  src/update_process_targets.cpp
  src/update_readiness.cpp
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
  src/update_target_watch.cpp
  src/update_rollback_safety.cpp
  src/update_process_identity.cpp
  src/update_manifest.cpp
  src/update_signature.cpp
  src/update_http.cpp
  src/payload_name.cpp
  src/install_registration.cpp
  src/update_registration_wiring.cpp
  src/update_process_targets.cpp
  src/update_readiness.cpp
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

# The negative control for the preview deadline: a window that pumps messages and never answers
# WM_PRINT, and what today's capture does when it meets one. Spawns itself as fixture and probe so
# the blocking call is always in a process this one can kill; a job object with KILL_ON_JOB_CLOSE
# means nothing outlives the run.
add_executable(remote60_thumbnail_deadline_test
  src/thumbnail_deadline_test.cpp
  src/host_bgra_scale.cpp
  src/host_args.cpp
)
target_include_directories(remote60_thumbnail_deadline_test PRIVATE src)
target_link_libraries(remote60_thumbnail_deadline_test PRIVATE common d3d11 dxgi)
if(MSVC)
  target_compile_options(remote60_thumbnail_deadline_test PRIVATE /utf-8)
endif()

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
  remote60_clip_image
  capture
  common
  ws2_32
  winmm
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
target_link_libraries(remote60_picker_preview PRIVATE
  remote60_clip_image user32 gdi32 gdiplus shlwapi ws2_32)
if(MSVC)
  target_compile_options(remote60_picker_preview PRIVATE /utf-8)
endif()

# The picker's empty-state decision, tested as a table with a negative control. Header-only logic:
# no window, no GDI, and no /utf-8 needed because it holds no Korean.
# The panel model comes along now: the connect status line is part of what this file pins, and
# a field with no writer was how it went unnoticed that nothing ever set it.
add_executable(remote60_picker_empty_state_test
  src/picker_empty_state_test.cpp
  src/native_video_client_shared_core.cpp
)
target_include_directories(remote60_picker_empty_state_test PRIVATE src)
target_link_libraries(remote60_picker_empty_state_test PRIVATE common ws2_32)
if(MSVC)
  target_compile_options(remote60_picker_empty_state_test PRIVATE /utf-8)
endif()

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

# mouse-xbutton r2: the SYSTEM agent's per-event decision and the calls it makes, run through the
# product's executor with recording SetCursorPos / SendInput. An unknown button key makes no call
# at all (it used to move the cursor first); X1 / X2 place the cursor once then send XDOWN / XUP.
add_executable(remote60_secure_input_inject_test
  src/secure_input_inject_test.cpp
)
target_include_directories(remote60_secure_input_inject_test PRIVATE src)
target_link_libraries(remote60_secure_input_inject_test PRIVATE user32)
if(MSVC)
  target_compile_options(remote60_secure_input_inject_test PRIVATE /utf-8)
endif()

# The secure-input target rect rule (P9): the agent's rect follows the monitor the capture opened
# and is re-derived on every restart; window mode = no rect; origin-preserving for multi-monitor.
add_executable(remote60_host_input_target_rect_test
  src/host_input_target_rect_test.cpp
)
target_include_directories(remote60_host_input_target_rect_test PRIVATE src)

# What the host releases on its own when a control session stops, and which ups it then does not
# inject a second time (RV-01).
# C0 stage 1: the receive-side bandwidth estimator against synthetic arrival sequences.
add_executable(remote60_viewer_bwe_test
  src/viewer_bwe_test.cpp
)
target_include_directories(remote60_viewer_bwe_test PRIVATE src)

# C0 stage 1 on the wire: layout, host validation, negotiation, and the scheduler's gating.
add_executable(remote60_bandwidth_observe_wire_test
  src/native_video_client_shared_core.cpp
  src/native_video_client_tcp_control.cpp
  src/udp_control_channel.cpp
  src/bandwidth_observe_wire_test.cpp
)
target_include_directories(remote60_bandwidth_observe_wire_test PRIVATE src)
target_link_libraries(remote60_bandwidth_observe_wire_test PRIVATE common ws2_32)

# C0 stage 1 observation, end to end through a bottleneck whose truth the test knows.
add_executable(remote60_bwe_observe_e2e_test
  src/bwe_observe_e2e_test.cpp
  src/native_video_client_shared_core.cpp
  src/native_video_client_tcp_control.cpp
  src/udp_control_channel.cpp
)
target_include_directories(remote60_bwe_observe_e2e_test PRIVATE src)
target_link_libraries(remote60_bwe_observe_e2e_test PRIVATE common ws2_32)
if(MSVC)
  target_compile_options(remote60_bwe_observe_e2e_test PRIVATE /utf-8)
endif()

# fec-single-chunk-stride: a real isolated host capturing this test's composed window, padded vs
# tight parity layout, three kinds of content; the shared session receive policy + the product
# decoder at this end; wire totals from the host's own stats line and from what arrived.
add_executable(remote60_fec_single_chunk_host_e2e_test
  src/fec_single_chunk_host_e2e_test.cpp
  src/native_video_client_shared_core.cpp
  src/native_video_client_tcp_control.cpp
  src/udp_control_channel.cpp
  src/mf_h264_codec.cpp
  src/viewer_frame_gate.cpp
)
target_include_directories(remote60_fec_single_chunk_host_e2e_test PRIVATE src)
target_link_libraries(remote60_fec_single_chunk_host_e2e_test PRIVATE
  capture common ws2_32 user32 gdi32 winmm runtimeobject windowsapp advapi32 d3d11 dxgi
  mfplat mf mfuuid wmcodecdspuuid ole32)
if(MSVC)
  target_compile_options(remote60_fec_single_chunk_host_e2e_test PRIVATE /utf-8)
endif()

# quality r1: ABR with a client that never sends decode metrics, APK-style present-only, or stops.
add_executable(remote60_abr_client_evidence_e2e_test
  src/abr_client_evidence_e2e_test.cpp
  src/native_video_client_shared_core.cpp
  src/native_video_client_tcp_control.cpp
  src/udp_control_channel.cpp
)
target_include_directories(remote60_abr_client_evidence_e2e_test PRIVATE src)
target_link_libraries(remote60_abr_client_evidence_e2e_test PRIVATE common ws2_32 user32 gdi32 winmm psapi remote60_clip_image)
# --file-bench stages the clipboard helper and runs the paste consumer beside this test.
add_dependencies(remote60_abr_client_evidence_e2e_test remote60_file_copy_helper remote60_file_copy_helper_e2e_test)
if(MSVC)
  target_compile_options(remote60_abr_client_evidence_e2e_test PRIVATE /utf-8)
endif()

add_executable(remote60_host_injected_input_test
  src/host_injected_input_test.cpp
)
target_include_directories(remote60_host_injected_input_test PRIVATE src)

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

# The stored sign-in: the files, the lock, and what a sign-in, a return and a sign-out each leave
# behind. The store and the flow are the product's; the directory is functions the test owns,
# because a real one cannot be told to answer after a sign-out.
add_executable(remote60_login_flow_test
  src/login_flow_test.cpp
  src/login_flow.cpp
  src/login_credential_store.cpp
  src/directory_session_client.cpp
  src/directory_client.cpp
  src/update_endpoint.cpp
  src/url_origin.cpp
  src/winhttp_transport.cpp
)
target_include_directories(remote60_login_flow_test PRIVATE src)
target_link_libraries(remote60_login_flow_test PRIVATE common ws2_32 crypt32)
if(MSVC)
  target_compile_options(remote60_login_flow_test PRIVATE /utf-8)
endif()

# Where a host token cached under another address goes: to the server when the address is a
# listed former name of it (and the cache is rewritten only once accepted), nowhere when it is
# not. Also that a redirect answered to a credentialed request is not followed. Counts what each
# fixture directory received, because a credential sent to the wrong place still signs in.
add_executable(remote60_directory_migration_test
  src/directory_migration_test.cpp
  src/directory_client.cpp
  src/update_endpoint.cpp
  src/url_origin.cpp
  src/winhttp_transport.cpp
)
target_include_directories(remote60_directory_migration_test PRIVATE src)
target_link_libraries(remote60_directory_migration_test PRIVATE common ws2_32)

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
  remote60_clip_image
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
  remote60_clip_image
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

# Clipboard text sync core (K1): the wire codec and the echo-loop state machine, with no Win32,
# no session and no sockets. /utf-8 because the round-trip cases include Korean and an emoji.
# Control-channel resume decisions and the resume packet's wire shape (item 8, layer 1). Pure
# logic -- no socket, no peer, no clock.
# Same / Different / Unknown, and never two of them collapsed (updater-abandon-race r3, item B).
add_executable(remote60_update_identity_match_test
  src/update_identity_match_test.cpp
  src/update_process_identity.cpp
  src/update_target_watch.cpp
)
target_include_directories(remote60_update_identity_match_test PRIVATE src)
if(MSVC)
  target_compile_options(remote60_update_identity_match_test PRIVATE /utf-8)
endif()

# Where tests are allowed to make a mess (updater-abandon-race r5, item 3).
#
# Inside the build directory, which is inside the repository -- not %TEMP%, which is shared with
# everything else on the machine. The recursive cleanup refuses anything outside this root, which
# matters most for the fixtures meant to be run elevated.
# Both roots are passed in, and the relationship between them is CHECKED at runtime rather than
# asserted here: CMAKE_BINARY_DIR is wherever the build was configured, which need not be inside
# the repository at all. A scratch root that fails that check is refused, not relocated.
set(REMOTE60_TEST_SCRATCH_ROOT "${CMAKE_BINARY_DIR}/test-scratch")
set(REMOTE60_TEST_REPO_ROOT "${CMAKE_SOURCE_DIR}")

add_library(remote60_test_scratch_dir STATIC src/test_scratch_dir.cpp)
target_include_directories(remote60_test_scratch_dir PUBLIC src)
target_compile_definitions(remote60_test_scratch_dir PUBLIC
  "REMOTE60_TEST_SCRATCH_ROOT=\"${REMOTE60_TEST_SCRATCH_ROOT}\""
  "REMOTE60_TEST_REPO_ROOT=\"${REMOTE60_TEST_REPO_ROOT}\""
)
if(MSVC)
  target_compile_options(remote60_test_scratch_dir PRIVATE /utf-8)
endif()

# The boundary itself, checked: prefix traps, climbing paths, and a real junction.
add_executable(remote60_test_scratch_dir_test src/test_scratch_dir_test.cpp)
target_link_libraries(remote60_test_scratch_dir_test PRIVATE remote60_test_scratch_dir)
if(MSVC)
  target_compile_options(remote60_test_scratch_dir_test PRIVATE /utf-8)
endif()

# The SCM half of the stop, against an ISOLATED fixture service (updater-abandon-race r4, G).
# Part 1 runs anywhere; part 2 needs elevation to install a service and reports BLOCKED without it.
add_executable(remote60_update_service_stop_test
  src/update_service_stop_test.cpp
  src/update_effects.cpp
  src/update_target_watch.cpp
  src/update_rollback_safety.cpp
  src/update_process_targets.cpp
  src/update_process_identity.cpp
  src/update_relaunch_plan.cpp
  src/update_state_machine.cpp
  src/update_manifest.cpp
  src/update_http.cpp
  src/payload_name.cpp
  src/update_signature.cpp
)
target_include_directories(remote60_update_service_stop_test PRIVATE src)
target_link_libraries(remote60_update_service_stop_test PRIVATE bcrypt winhttp user32 advapi32
  remote60_test_scratch_dir)
if(MSVC)
  target_compile_options(remote60_update_service_stop_test PRIVATE /utf-8)
endif()

# Whether a rollback may start (updater-health-gate D2). Pure: no file, no process.
add_executable(remote60_update_rollback_safety_test
  src/update_rollback_safety_test.cpp
  src/update_rollback_safety.cpp
)
target_include_directories(remote60_update_rollback_safety_test PRIVATE src)
if(MSVC)
  target_compile_options(remote60_update_rollback_safety_test PRIVATE /utf-8)
endif()

# What the updater will believe about a relaunched Host (updater-health-gate D1, item 2).
# Pure: no file, no process, no clock.
add_executable(remote60_update_readiness_test
  src/update_readiness_test.cpp
  src/update_readiness.cpp
)
target_include_directories(remote60_update_readiness_test PRIVATE src)
target_link_libraries(remote60_update_readiness_test PRIVATE bcrypt)
if(MSVC)
  target_compile_options(remote60_update_readiness_test PRIVATE /utf-8)
endif()

# The single writer for host_app.log (updater-health-gate D1, items 1 and 3): both writers keep
# every line, across rotation, and a write that fails says so.
add_executable(remote60_host_app_log_test
  src/host_app_log_test.cpp
  src/host_app_log.cpp
)
target_include_directories(remote60_host_app_log_test PRIVATE src)
if(MSVC)
  target_compile_options(remote60_host_app_log_test PRIVATE /utf-8)
endif()

# Phase 1 measurement for updater-health-gate D2: is the rollback's ACCESS_DENIED a running
# image or an ACL? Opens handles and closes them; deletes nothing.
add_executable(remote60_rollback_denial_probe
  src/rollback_denial_probe.cpp
)
target_include_directories(remote60_rollback_denial_probe PRIVATE src)
if(MSVC)
  target_compile_options(remote60_rollback_denial_probe PRIVATE /utf-8)
endif()

# Phase 1 measurement for updater-health-gate: do host_app.log's two writers lose lines when
# they overlap? Prints a measurement; asserts nothing.
add_executable(remote60_host_log_writers_probe
  src/host_log_writers_probe.cpp
)
target_include_directories(remote60_host_log_writers_probe PRIVATE src)
if(MSVC)
  target_compile_options(remote60_host_log_writers_probe PRIVATE /utf-8)
endif()

# What the UDP hello handshake reports about itself (pc2-connect-diag r1, scope C-1). Uses a real
# socket aimed at a loopback port nobody is listening on.
add_executable(remote60_hello_stats_test
  src/hello_stats_test.cpp
  src/native_video_client_tcp_control.cpp
  src/udp_control_channel.cpp
)
target_include_directories(remote60_hello_stats_test PRIVATE src)
target_link_libraries(remote60_hello_stats_test PRIVATE common ws2_32)
if(MSVC)
  target_compile_options(remote60_hello_stats_test PRIVATE /utf-8)
endif()

# The decisions inside the host's connect diagnostics (pc2-connect-diag r1, scope B). Pure logic.
# c1-lan-relay: whether the host answers a punch, and where it stops. Pure logic.
add_executable(remote60_punch_reply_test
  src/punch_reply_test.cpp
)
target_include_directories(remote60_punch_reply_test PRIVATE src)
if(MSVC)
  target_compile_options(remote60_punch_reply_test PRIVATE /utf-8)
endif()

add_executable(remote60_host_diag_log_test
  src/host_diag_log_test.cpp
)
target_include_directories(remote60_host_diag_log_test PRIVATE src)
if(MSVC)
  target_compile_options(remote60_host_diag_log_test PRIVATE /utf-8)
endif()

add_executable(remote60_control_resume_test
  src/control_resume_test.cpp
  src/udp_control_channel.cpp
)
target_include_directories(remote60_control_resume_test PRIVATE src)
target_link_libraries(remote60_control_resume_test PRIVATE common ws2_32)
if(MSVC)
  target_compile_options(remote60_control_resume_test PRIVATE /utf-8)
endif()

# The viewer's half of item 8: the resume state machine, over a real control channel.
add_executable(remote60_viewer_control_resume_test
  src/viewer_control_resume_test.cpp
  src/udp_control_channel.cpp
)
target_include_directories(remote60_viewer_control_resume_test PRIVATE src)
target_link_libraries(remote60_viewer_control_resume_test PRIVATE common ws2_32)
if(MSVC)
  target_compile_options(remote60_viewer_control_resume_test PRIVATE /utf-8)
endif()

add_executable(remote60_clipboard_sync_core_test
  src/clipboard_sync_core_test.cpp
)
target_include_directories(remote60_clipboard_sync_core_test PRIVATE src)
if(MSVC)
  target_compile_options(remote60_clipboard_sync_core_test PRIVATE /utf-8)
endif()

# Clipboard monitor / hub against the REAL session clipboard (K1). SKIP by default -- it writes and
# reads the actual clipboard -- and gated behind REMOTE60_ALLOW_CLIPBOARD_TEST=1, snapshotting and
# restoring the text. It links the same clipboard_monitor.cpp the host does.
add_executable(remote60_clipboard_monitor_test
  src/clipboard_monitor_test.cpp
  src/clipboard_monitor.cpp
  src/clip_image_clipboard.cpp
)
target_include_directories(remote60_clipboard_monitor_test PRIVATE src)
target_link_libraries(remote60_clipboard_monitor_test PRIVATE user32 bcrypt)
if(MSVC)
  target_compile_options(remote60_clipboard_monitor_test PRIVATE /utf-8)
endif()
# The same monitor reporting copies of files (file copy R->P), run on a private window station so
# the clipboard is never the user's -- always runs, no gate.
add_executable(remote60_clipboard_monitor_files_test
  src/clipboard_monitor_files_test.cpp
  src/clipboard_monitor.cpp
  src/clip_image_clipboard.cpp
)
target_include_directories(remote60_clipboard_monitor_files_test PRIVATE src)
target_link_libraries(remote60_clipboard_monitor_files_test PRIVATE user32 bcrypt shell32)
if(MSVC)
  target_compile_options(remote60_clipboard_monitor_files_test PRIVATE /utf-8)
endif()

# Clipboard wire round-trip (K1): the client send/poll helpers over a ControlLink, against a mock
# host that parses and replies as the real Serve loop does. No sockets, no OS clipboard. It links
# the real client wire helpers (native_video_client_tcp_control.cpp) and the ControlLink.
add_executable(remote60_clipboard_wire_test
  src/clipboard_wire_test.cpp
  src/native_video_client_tcp_control.cpp
  src/udp_control_channel.cpp
)
target_include_directories(remote60_clipboard_wire_test PRIVATE src)
target_link_libraries(remote60_clipboard_wire_test PRIVATE common ws2_32)
if(MSVC)
  target_compile_options(remote60_clipboard_wire_test PRIVATE /utf-8)
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
    src/login_credential_store.cpp
    src/login_flow.cpp
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
  # Test-only seam: the shell's settings path moves into the fixture, and the directory it talks
# to becomes the fixture's. Never defined for GNLinkClient -- checked by grepping the shipped exe
# for the seam's own message (RV-00, automation/gnlink_check_fixed_server.py).
target_compile_definitions(remote60_client_recovery_ui_test PRIVATE REMOTE60_SHELL_TEST_SEAM=1)
if(MSVC)
  # The page's Korean is compared against literals in the test, and that file has no byte order
  # mark to tell the compiler what it is.
  target_compile_options(remote60_client_recovery_ui_test PRIVATE /utf-8)
endif()
target_include_directories(remote60_client_recovery_ui_test PRIVATE src "${GNLINK_WEBVIEW2_DIR}/include")
  get_target_property(_recovery_shell_links remote60_client_shell LINK_LIBRARIES)
  target_link_libraries(remote60_client_recovery_ui_test PRIVATE ${_recovery_shell_links})
  add_dependencies(remote60_client_recovery_ui_test remote60_client_shell)

  # Coming back signed in, in the client's own window: the same shell, the same seam, one start
  # of the client per run. apps/directory/test/client_auto_login_runner.js strings the runs
  # together around a real directory that it restarts, stops and slows down.
  add_executable(remote60_client_auto_login_ui_test src/client_auto_login_ui_test.cpp ${_recovery_shell_sources})
  target_compile_definitions(remote60_client_auto_login_ui_test PRIVATE REMOTE60_SHELL_TEST_SEAM=1)
  if(MSVC)
    target_compile_options(remote60_client_auto_login_ui_test PRIVATE /utf-8)
  endif()
  target_include_directories(remote60_client_auto_login_ui_test PRIVATE src "${GNLINK_WEBVIEW2_DIR}/include")
  target_link_libraries(remote60_client_auto_login_ui_test PRIVATE ${_recovery_shell_links})
  add_dependencies(remote60_client_auto_login_ui_test remote60_client_shell)

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

# bitrate-hard-cap r1: the hard wire-rate cap bucket math + the real send path (send_udp_chunks_timed
# / send_udp_chunk_indices) charged through one shared limiter on a fake clock + recording sink, with
# sliding-window maxima and bypass negative controls. Pure-logic (no sockets, no threads).
add_executable(remote60_host_wire_limiter_test
  src/host_net_io.cpp
  src/host_wire_limiter_test.cpp
)
target_include_directories(remote60_host_wire_limiter_test PRIVATE src)
target_link_libraries(remote60_host_wire_limiter_test PRIVATE common ws2_32)
if(MSVC)
  target_compile_options(remote60_host_wire_limiter_test PRIVATE /utf-8)
endif()

# bitrate-hard-cap r1: the large IDR vs the real receiver (assembler + NACK scheduler) under the cap.
# The real chunker emits a cap-paced IDR on a fake clock; the datagrams replay into the real receiver
# while the real scheduler is polled -- reproducing and quantifying the premature-tail-NACK /
# keyframe-before-completion loop the current receiver has with a cap-spread IDR (plan point 7).
add_executable(remote60_host_wire_cap_idr_test
  src/host_net_io.cpp
  src/native_video_client_shared_core.cpp
  src/native_video_client_tcp_control.cpp
  src/udp_control_channel.cpp
  src/native_video_client_session.cpp
  src/host_wire_cap_idr_test.cpp
)
target_include_directories(remote60_host_wire_cap_idr_test PRIVATE src)
target_link_libraries(remote60_host_wire_cap_idr_test PRIVATE common ws2_32)
if(MSVC)
  target_compile_options(remote60_host_wire_cap_idr_test PRIVATE /utf-8)
endif()

# bitrate-hard-cap r2: the host's deferred-NACK recovery (real SenderState, loopback) -- a hole
# requested while the AU is still being sent is deferred and served once StoreAu caches the AU.
add_executable(remote60_host_wire_cap_defer_test
  src/host_encoded_sender.cpp
  src/host_net_io.cpp
  src/host_wire_cap_defer_test.cpp
)
target_include_directories(remote60_host_wire_cap_defer_test PRIVATE src)
target_link_libraries(remote60_host_wire_cap_defer_test PRIVATE capture common ws2_32
  runtimeobject windowsapp advapi32 d3d11 dxgi gdi32 user32 mfplat mf mfuuid wmcodecdspuuid ole32)
if(MSVC)
  target_compile_options(remote60_host_wire_cap_defer_test PRIVATE /utf-8)
endif()

add_executable(remote60_host_wire_cap_fix_test
  src/host_encoded_sender.cpp
  src/host_net_io.cpp
  src/host_wire_cap_fix_test.cpp
)
target_include_directories(remote60_host_wire_cap_fix_test PRIVATE src)
target_link_libraries(remote60_host_wire_cap_fix_test PRIVATE capture common ws2_32
  runtimeobject windowsapp advapi32 d3d11 dxgi gdi32 user32 mfplat mf mfuuid wmcodecdspuuid ole32)
if(MSVC)
  target_compile_options(remote60_host_wire_cap_fix_test PRIVATE /utf-8)
endif()

add_executable(remote60_host_wire_cap_sender_thread_test
  src/host_encoded_sender.cpp
  src/host_net_io.cpp
  src/directory_client.cpp
  src/url_origin.cpp
  src/winhttp_transport.cpp
  src/host_wire_cap_sender_thread_test.cpp
)
target_include_directories(remote60_host_wire_cap_sender_thread_test PRIVATE src)
target_link_libraries(remote60_host_wire_cap_sender_thread_test PRIVATE capture common ws2_32 winhttp
  runtimeobject windowsapp advapi32 d3d11 dxgi gdi32 user32 mfplat mf mfuuid wmcodecdspuuid ole32)
if(MSVC)
  target_compile_options(remote60_host_wire_cap_sender_thread_test PRIVATE /utf-8)
endif()

# fec-single-chunk-stride: the host's real chunker over loopback, its datagrams judged by the real
# shared assembler -- boundary sizes, loss counter-examples, the NACK replay geometry, both layouts.
add_executable(remote60_udp_fec_single_chunk_test
  src/host_net_io.cpp
  src/native_video_client_shared_core.cpp
  src/native_video_client_tcp_control.cpp
  src/udp_control_channel.cpp
  src/native_video_client_session.cpp
  src/udp_fec_single_chunk_test.cpp
)
target_include_directories(remote60_udp_fec_single_chunk_test PRIVATE src)
target_link_libraries(remote60_udp_fec_single_chunk_test PRIVATE common ws2_32)

# fec-single-chunk-stride under loss: one fixed H.264 AU stream per content class through the real
# chunker, the real NACK cache/replay (no threads), seeded loss by datagram identity, the shared
# session receive policy on a virtual clock, and the real decoder -- padded vs tight, same losses.
add_executable(remote60_udp_fec_single_chunk_loss_test
  src/host_encoded_sender.cpp
  src/host_net_io.cpp
  src/directory_client.cpp
  src/update_endpoint.cpp
  src/url_origin.cpp
  src/winhttp_transport.cpp
  src/native_video_client_shared_core.cpp
  src/native_video_client_tcp_control.cpp
  src/udp_control_channel.cpp
  src/native_video_client_session.cpp
  src/mf_h264_codec.cpp
  src/udp_fec_single_chunk_loss_test.cpp
)
target_include_directories(remote60_udp_fec_single_chunk_loss_test PRIVATE src)
target_link_libraries(remote60_udp_fec_single_chunk_loss_test PRIVATE
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

# RV-20 r2: every host e2e stages inside the repository's test scratch root (e2e_isolation.hpp
# StagingDir over test_scratch_dir), never %TEMP%. The boundary cases have their own test.
foreach(t
    remote60_abr_client_evidence_e2e_test remote60_bwe_observe_e2e_test remote60_control_resume_e2e_test
    remote60_fec_single_chunk_host_e2e_test remote60_host_control_deadline_e2e_test
    remote60_host_punch_reply_e2e_test remote60_host_two_dispatcher_e2e_test remote60_udp_control_e2e_test
    remote60_viewer_release_all_e2e_test remote60_viewer_mouse_xbutton_e2e_test remote60_thumbnail_helper_e2e_test
    remote60_host_migration_e2e_test)
  target_link_libraries(${t} PRIVATE remote60_test_scratch_dir)
endforeach()
add_executable(remote60_e2e_staging_test src/e2e_staging_test.cpp)
target_include_directories(remote60_e2e_staging_test PRIVATE src)
target_link_libraries(remote60_e2e_staging_test PRIVATE remote60_test_scratch_dir)
if(MSVC)
  target_compile_options(remote60_e2e_staging_test PRIVATE /utf-8)
endif()
