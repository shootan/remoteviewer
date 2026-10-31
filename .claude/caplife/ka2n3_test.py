p = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/standby_entry_client_test.cpp'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')
a = s.index('// ② e: the file the APK keeps the device id + last generation in.')
b = s.index('}  // namespace\n\nint main() {')
new = r'''// ② e / r3 (Codex scope2 N3): the file the APK keeps the device id + last generation in. Only under REMOTE60_TEST_TMP
// (the repository's own scratch root): this test writes and removes its own files (and one directory) there, nowhere
// else; without the variable it is skipped. No crash is simulated on this PC: each Save step fails by injection.
#if defined(_WIN32)
std::wstring wide_of(const std::string& utf8) {
  const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
  std::wstring w(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), &w[0], n);
  return w;
}
void remove_u8(const std::string& p) { DeleteFileW(wide_of(p).c_str()); }
#else
void remove_u8(const std::string& p) { std::remove(p.c_str()); }
#endif

void file_store() {
  std::printf("\n--- the file store (device id + last generation) ---\n");
  const char* root = std::getenv("REMOTE60_TEST_TMP");
  if (!root || !*root) {
    std::printf("SKIP  REMOTE60_TEST_TMP not set (the file cases write only there)\n");
    return;
  }
  // a non-ASCII directory, as an app's files dir may be (u8"세대")
  const std::string dir = std::string(root) + "/\xEC\x84\xB8\xEB\x8C\x80";
#if defined(_WIN32)
  CreateDirectoryW(wide_of(dir).c_str(), nullptr);
#endif
  const std::string path = dir + "/standby_generation_test.bin";
  auto clean = [&] {
    remove_u8(path);
    remove_u8(path + ".tmp");
    remove_u8(path + ".lock");
  };
  clean();
  uint32_t first = 0, second = 0, third = 0;
  std::array<uint8_t, 16> id1{}, id2{};
  {
    n::FileGenerationStore fs(path);
    check("the store owns its path (the one writer)", fs.owns());
    n::FileGenerationStore other(path);
    check("a second store on the same path is refused -- closed, not shared", !other.owns() && [&] {
      std::array<uint8_t, 16> x{};
      uint32_t l = 0;
      return !other.Load(&x, &l) && !other.Save(x, 9);
    }());
    n::GenerationAllocator g(fs, rnd);
    check("nothing stored (a non-ASCII directory): the first generation is 1, a device id is made",
          g.Next(&first) && first == 1 && g.DeviceId(&id1) && id1[0] == 0xA0);
    check("...the next one is saved too (replaced, not appended)", g.Next(&second) && second == 2);
  }
  {
    n::FileGenerationStore fs(path);  // the app restarted
    n::GenerationAllocator g(fs, rnd);
    check("after a restart: the same device id, the counter goes on (3)", g.Next(&third) && third == 3 && g.DeviceId(&id2) &&
          id2 == id1);
  }
  // every Save step failing in turn: nothing is sent under the generation being saved, and after a reopen the same id
  // comes back with a generation never below the last one sent
  using Step = n::FileGenerationStore::Step;
  uint32_t sentMax = third;
  for (Step bad : {Step::Write, Step::Flush, Step::Close, Step::Replace, Step::DirSync}) {
    {
      n::FileGenerationStore fs(path, [bad](Step s) { return s != bad; });
      n::GenerationAllocator g(fs, rnd);
      Client c(g, 0);
      auto fx = c.Admit("cap", 0);
      check(("Save failing at step " + std::to_string(static_cast<int>(bad)) + ": nothing sent").c_str(),
            sent(fx, n::UdpPacketKind::StandbyHello) == nullptr && c.state() == Client::State::Waiting);
    }
    n::FileGenerationStore fs(path);
    n::GenerationAllocator g(fs, rnd);
    std::array<uint8_t, 16> idr{};
    uint32_t next = 0;
    const bool ok = g.Next(&next) && g.DeviceId(&idr);
    check(("...reopened: the same id, the next generation above every one sent (step " + std::to_string(static_cast<int>(bad)) +
           ")").c_str(), ok && idr == id1 && next > sentMax, "next=" + std::to_string(next) + " sentMax=" + std::to_string(sentMax));
    sentMax = next;
  }
  {
    FILE* f = nullptr;
#if defined(_WIN32)
    f = _wfopen(wide_of(path).c_str(), L"wb");
#else
    f = std::fopen(path.c_str(), "wb");
#endif
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
    check("...a new device id (a new incarnation), generations from 1 -- never the old id with a smaller one",
          g.Next(&gen) && gen == 1 && g.DeviceId(&id3) && id3[0] == 0x5C && id3 != id1);
  }
  clean();
#if defined(_WIN32)
  RemoveDirectoryW(wide_of(dir).c_str());
#endif
}
'''
s = s[:a] + new + s[b:]
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
