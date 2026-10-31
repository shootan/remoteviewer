import io

p = 'apps/native_poc/CMakeLists.txt'
d = io.open(p, encoding='utf-8').read()

anchor = '''add_executable(remote60_host_command_line_test
  src/host_command_line_test.cpp
)'''
new = '''# A whole release of real files, driven end to end.
#
# Same link-time isolation as remote60_update_effects_test: src/update_process_targets.cpp is
# deliberately NOT in this target, so nothing here can find a real GNLinkHost.exe by name.
#
# The artifact bodies are the shared vector files, handed in as a path rather than copied, so this
# suite and the JavaScript and Kotlin manifest suites are reading the same bytes.
add_executable(remote60_update_release_test
  src/update_effects.cpp
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

''' + anchor
assert anchor in d, 'cmake anchor'
d = d.replace(anchor, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('target added')
