p = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/standby_entry_client_test.cpp'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')

def rep(old, new):
    global s
    assert s.count(old) == 1, (s.count(old), old[:80])
    s = s.replace(old, new)

rep('#include "standby_entry_client.hpp"\n', '#include <cstdlib>\n\n#include "standby_entry_client.hpp"\n#include "standby_file_store.hpp"\n')
rep('''}  // namespace

int main() {''', r'''// ② e: the file the APK keeps the device id + last generation in. Only under REMOTE60_TEST_TMP (the repository's own
// scratch root): this test writes and removes its own two files there, nowhere else; without the variable it is skipped.
void file_store() {
  std::printf("\n--- the file store (device id + last generation) ---\n");
  const char* root = std::getenv("REMOTE60_TEST_TMP");
  if (!root || !*root) {
    std::printf("SKIP  REMOTE60_TEST_TMP not set (the file cases write only there)\n");
    return;
  }
  const std::string path = std::string(root) + "/standby_generation_test.bin";
  std::remove(path.c_str());
  std::remove((path + ".tmp").c_str());
  uint32_t first = 0, second = 0, third = 0;
  std::array<uint8_t, 16> id1{}, id2{};
  {
    n::FileGenerationStore fs(path);
    n::GenerationAllocator g(fs, rnd);
    check("nothing stored: the first generation is 1, a device id is made", g.Next(&first) && first == 1 && g.DeviceId(&id1) &&
          id1[0] == 0xA0);
    check("...and the next one is saved too (the file is replaced, not appended)", g.Next(&second) && second == 2);
  }
  {
    n::FileGenerationStore fs(path);  // the app restarted
    n::GenerationAllocator g(fs, rnd);
    check("after a restart: the same device id, the counter goes on (3)", g.Next(&third) && third == 3 && g.DeviceId(&id2) &&
          id2 == id1);
  }
  {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (f) {
      std::fwrite("junk!", 1, 5, f);
      std::fclose(f);
    }
    n::FileGenerationStore fs(path);
    std::array<uint8_t, 16> scratch{};
    uint32_t last = 0;
    check("a damaged (short) file loads as nothing", !fs.Load(&scratch, &last));
    n::GenerationAllocator g(fs, [](uint8_t* b, size_t len) { for (size_t i = 0; i < len; ++i) b[i] = 0x5C; return true; });
    uint32_t gen = 0;
    std::array<uint8_t, 16> id3{};
    check("...a new device id, generations from 1", g.Next(&gen) && gen == 1 && g.DeviceId(&id3) && id3[0] == 0x5C);
  }
  std::remove(path.c_str());
  std::remove((path + ".tmp").c_str());
}
}  // namespace

int main() {''')
rep('''  release();
  generations();''', '''  release();
  generations();
  file_store();''')
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
