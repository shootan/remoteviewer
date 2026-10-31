cpp = 'C:/work/gnlink-wt/feat-apk-0233/apps/android_direct_client/app/src/main/cpp/native_bridge.cpp'
s = open(cpp, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')
a = s.index('// The install\'s device id + last generation, in one small file under the app\'s own files dir.')
b = s.index('bool urandom_fill(uint8_t* out, size_t n) {')
s = s[:a] + '// The device id + last generation file: standby_file_store.hpp (FileGenerationStore, shared and tested).\nusing remote60::native_poc::FileGenerationStore;\n\n' + s[b:]
assert s.count('#include "standby_runner.hpp"\n') == 1
s = s.replace('#include "standby_runner.hpp"\n', '#include "standby_file_store.hpp"\n#include "standby_runner.hpp"\n')
if crlf:
    s = s.replace('\n', '\r\n')
open(cpp, 'wb').write(s.encode('utf-8'))
print('ok')
