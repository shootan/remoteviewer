import io

p = 'apps/native_poc/CMakeLists.txt'
d = io.open(p, encoding='utf-8').read()

anchor = '''add_executable(remote60_host_command_line_test
  src/host_command_line_test.cpp
)'''
new = '''# The two decisions behind Relaunch and HealthCheck.
#
# src/update_relaunch.cpp -- the code that actually starts processes and talks to the SCM -- is
# deliberately NOT in this target. The decisions are testable; making them happen is not something
# this binary should be able to do.
add_executable(remote60_update_relaunch_test
  src/update_relaunch_plan.cpp
  src/update_health.cpp
  src/update_relaunch_test.cpp
)
target_include_directories(remote60_update_relaunch_test PRIVATE src)
if(MSVC)
  target_compile_options(remote60_update_relaunch_test PRIVATE /utf-8)
endif()

''' + anchor
assert anchor in d, 'cmake anchor'
d = d.replace(anchor, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('target added')
