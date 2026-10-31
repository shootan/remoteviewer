import io

# --- update_relaunch.cpp: use the shared unit instead of its own copies
p = 'apps/native_poc/src/update_relaunch.cpp'
d = io.open(p, encoding='utf-8').read()

d = d.replace('#include "update_health.hpp"', '#include "update_health.hpp"\n'
              '#include "update_health_log.hpp"', 1)

start = d.find('uint64_t file_size_or_zero(const std::wstring& path) {')
end = d.find('bool launch_via_shell(')
assert start != -1 and end != -1 and start < end, 'file helper block'
d = d[:start] + d[end:]
io.open(p, 'w', encoding='utf-8', newline='').write(d)

# --- and its header no longer declares them
p = 'apps/native_poc/src/update_relaunch.hpp'
d = io.open(p, encoding='utf-8').read()
old = '''
/** Size of a file in bytes, or 0 when it cannot be read. Used to mark where a log ends. */
uint64_t file_size_or_zero(const std::wstring& path);

/** Reads `path` from `offset` to the end. Empty when there is nothing new or it cannot be read. */
std::string read_from_offset(const std::wstring& path, uint64_t offset);
'''
assert old in d, 'header decl block'
d = d.replace(old, '', 1)
d = d.replace('#include "update_effects.hpp"',
              '#include "update_effects.hpp"\n#include "update_health_log.hpp"', 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)

# --- CMake: the shared unit joins both the library and the test
p = 'apps/native_poc/CMakeLists.txt'
d = io.open(p, encoding='utf-8').read()
d = d.replace('''add_library(remote60_update_relaunch STATIC
  src/update_relaunch.cpp
  src/update_relaunch_plan.cpp
  src/update_health.cpp
)''', '''add_library(remote60_update_relaunch STATIC
  src/update_relaunch.cpp
  src/update_relaunch_plan.cpp
  src/update_health.cpp
  src/update_health_log.cpp
)''', 1)
d = d.replace('''add_executable(remote60_update_relaunch_test
  src/update_relaunch_plan.cpp
  src/update_health.cpp
  src/update_relaunch_test.cpp
)''', '''add_executable(remote60_update_relaunch_test
  src/update_relaunch_plan.cpp
  src/update_health.cpp
  src/update_health_log.cpp
  src/update_relaunch_test.cpp
)''', 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('health log unit shared')
