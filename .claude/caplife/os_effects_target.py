import re

W = 'C:/work/gnlink-wt/ka-r3/'


def edit(p, fn):
    s = open(p, encoding='utf-8', newline='').read()
    crlf = '\r\n' in s
    if crlf:
        s = s.replace('\r\n', '\n')
    s2 = fn(s)
    assert s2 != s, p
    if crlf:
        s2 = s2.replace('\n', '\r\n')
    open(p, 'w', encoding='utf-8', newline='').write(s2)
    print('ok', p.rsplit('/', 1)[-1])


def startup(s):
    a = '''  if (kInputPolicyForceBlock) {
    std::cout << "[native-video-host] input injection blocked by compile-time policy\\n";'''
    assert s.count(a) == 1
    s = s.replace(a, '''  // Finding-2 (2026-10-28): a record-only test build says so first, as its own state -- not "injection on/off"
  // (its policy and dispatch run; no OS input effect exists in it: os_input_effects.hpp)
  if (remote60::native_poc::os_input::kRecordOnly) {
    std::cout << "[native-video-host] os-input-record: os input effects=record-only (test build: SendInput, input "
                 "PostMessage, SetCursorPos and the secure broker are recorded, never performed)\\n";
  }
''' + a)
    last = list(re.finditer(r'^#include "[^"]+"\n', s, flags=re.M))[-1]
    return s[:last.end()] + '#include "os_input_effects.hpp"\n' + s[last.end():]


edit(W + 'apps/native_poc/src/host_startup_config.cpp', startup)


def cmake(s):
    a = '''# TEST BUILD (dxgi-wedge-fix r1, Codex B1)'''
    assert s.count(a) == 1
    block = '''# TEST BUILD (2026-10-28, Codex codex_test_injection_finding.md Finding-2): the migration seam's host (the same
# sources and libraries as GNLinkStream, REMOTE60_STREAM_TEST_SEAM as above) with every OS input effect RECORDED and
# none performed (REMOTE60_OS_INPUT_RECORD_ONLY, in os_input_effects.hpp and nowhere else): SendInput, input
# PostMessage, SetCursorPos and the secure-desktop broker. Receipt, policy, routing and dispatch are the product's.
# Its own output name, never packaged; automation/gnlink_check_fixed_server.py refuses a shipped GNLinkStream.exe that
# carries its marker ("os-input-record").
add_executable(remote60_native_video_host_input_record ${_migration_host_sources})
set_target_properties(remote60_native_video_host_input_record PROPERTIES
  OUTPUT_NAME "GNLinkStreamInputRecordTest")
target_include_directories(remote60_native_video_host_input_record PRIVATE src)
target_compile_definitions(remote60_native_video_host_input_record PRIVATE
  REMOTE60_STREAM_TEST_SEAM=1
  REMOTE60_OS_INPUT_RECORD_ONLY=1
  REMOTE60_NATIVE_ENCODED_EXPERIMENT=${_native_encoded_compile_value}
)
target_link_libraries(remote60_native_video_host_input_record PRIVATE
  remote60_file_copy_host
  capture common ws2_32 d3d11 mfplat mf mfuuid wmcodecdspuuid ole32 runtimeobject windowsapp advapi32
  bcrypt windowscodecs shlwapi winmm
)

'''
    return s.replace(a, block + a)


edit(W + 'apps/native_poc/CMakeLists.txt', cmake)


def gate(s):
    a = """    'stream-test-seam',           # directory_client.cpp, REMOTE60_STREAM_TEST_SEAM
"""
    assert s.count(a) == 1
    return s.replace(a, a + """    'os-input-record',            # os_input_effects.hpp, REMOTE60_OS_INPUT_RECORD_ONLY (record-only test host)
""")


edit(W + 'automation/gnlink_check_fixed_server.py', gate)
