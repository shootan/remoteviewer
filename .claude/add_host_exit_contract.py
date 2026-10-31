# -*- coding: utf-8 -*-
import io

p = 'apps/native_poc/src/update_stop_process_test.cpp'
d = io.open(p, encoding='utf-8').read()

old = '''  // ---------------------------------------------------------------- cleanup'''
new = '''  // ---------------------------------------------------------------- the host's own exit contract
  //
  // ⚠ STATIC WIRING CHECK, not a behavioural one. It reads the source and confirms the handoff
  // exit path stops the child and writes down whether it went. It does NOT prove the Host does
  // that when it really stands down -- only running the real Host can show that, and that belongs
  // to the field re-test. Labelled so nobody reads it as more than it is.
  //
  // Worth having anyway: the defect was that the exit path stopped nothing verifiably and said
  // nothing, so a child that outlived its parent left no trace until the NEXT host failed to bind.
#ifdef REMOTE60_HOST_APP_SRC
  {
    std::ifstream in(REMOTE60_HOST_APP_SRC, std::ios::binary);
    std::ostringstream os;
    os << in.rdbuf();
    const std::string src = os.str();
    const size_t exitNow = src.find("The updater holds the lock and has a verified download");
    const size_t stopCall = exitNow == std::string::npos
                                ? std::string::npos
                                : src.find("g.streaming.Stop(&detail)", exitNow);
    const size_t destroy = exitNow == std::string::npos
                               ? std::string::npos
                               : src.find("DestroyWindow(window)", exitNow);
    check("(static) the handoff exit stops the child", stopCall != std::string::npos);
    check("(static) ...before destroying the window",
          stopCall != std::string::npos && destroy != std::string::npos && stopCall < destroy);
    check("(static) ...and records whether it went",
          src.find("update: standing down -- ") != std::string::npos);
    check("(static) ...and warns when a child outlived it",
          src.find("a streaming child outlived this process") != std::string::npos);
  }
#else
  check("(static) the host exit contract is checked", false, "REMOTE60_HOST_APP_SRC not defined");
#endif

  // ---------------------------------------------------------------- cleanup'''
assert d.count(old) == 1, 'anchor'
d = d.replace(old, new, 1)

for inc in ['#include <fstream>', '#include <sstream>']:
    if inc not in d:
        d = d.replace('#include <string>', inc + '\n#include <string>', 1)

io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok test')

p2 = 'apps/native_poc/CMakeLists.txt'
c = io.open(p2, encoding='utf-8').read()
old2 = '''target_include_directories(remote60_update_stop_process_test PRIVATE src)'''
new2 = '''target_include_directories(remote60_update_stop_process_test PRIVATE src)
target_compile_definitions(remote60_update_stop_process_test PRIVATE
  "REMOTE60_HOST_APP_SRC=\\"${CMAKE_CURRENT_SOURCE_DIR}/src/host_app_main.cpp\\""
)'''
assert c.count(old2) == 1, 'cmake'
io.open(p2, 'w', encoding='utf-8', newline='').write(c.replace(old2, new2, 1))
print('ok cmake')
