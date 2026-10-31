# -*- coding: utf-8 -*-
import io

p = 'apps/native_poc/src/log_upload_shutdown_test.cpp'
d = io.open(p, encoding='utf-8').read()
old = '#include <fstream>'
new = '#include <cstring>\n#include <fstream>\n#include <functional>'
assert d.count(old) == 1
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok includes')

p = 'apps/native_poc/CMakeLists.txt'
d = io.open(p, encoding='utf-8').read()
old = '''# The thing a user installs: sign-in window plus tray, supervising the streaming host.
add_executable(remote60_host_app WIN32'''
new = '''# The uploader's SHUTDOWN, in child processes, because the failure is an exit rather than an
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
  "REMOTE60_MAIN_SRC_DIR=\\"${CMAKE_CURRENT_SOURCE_DIR}/src\\""
)
target_link_libraries(remote60_log_upload_shutdown_test PRIVATE common ws2_32 iphlpapi shell32)

# The thing a user installs: sign-in window plus tray, supervising the streaming host.
add_executable(remote60_host_app WIN32'''
assert d.count(old) == 1, 'cmake anchor'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok cmake')
