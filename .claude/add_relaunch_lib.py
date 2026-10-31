import io

p = 'apps/native_poc/CMakeLists.txt'
d = io.open(p, encoding='utf-8').read()

anchor = '''# The update path's HTTPS client.'''
new = '''# The other production-only half of the updater, alongside remote60_update_process_targets and
# for the same reason: this is the code that creates processes, talks to the SCM and reads the
# live host log. Built so it cannot rot, linked into nothing, so no test binary contains those
# symbols. The DECISIONS behind it (update_relaunch_plan.cpp, update_health.cpp) are separate and
# are linked into remote60_update_relaunch_test.
add_library(remote60_update_relaunch STATIC
  src/update_relaunch.cpp
  src/update_relaunch_plan.cpp
  src/update_health.cpp
)
target_include_directories(remote60_update_relaunch PRIVATE src)
target_link_libraries(remote60_update_relaunch PRIVATE advapi32 ole32 oleaut32 shell32)
if(MSVC)
  target_compile_options(remote60_update_relaunch PRIVATE /utf-8)
endif()

''' + anchor
assert anchor in d, 'lib anchor'
d = d.replace(anchor, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('library added')
