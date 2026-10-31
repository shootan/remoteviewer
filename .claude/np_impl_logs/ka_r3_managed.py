p = r'C:\work\gnlink-wt\ka-pc\apps\native_poc\src\ka_pc_managed_e2e_test.cpp'
b = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in b
s = b.replace('\r\n', '\n')
a = '''  // ---------------------------------------------------------------- the managed viewer of KA-A
'''
assert s.count(a) == 1
s = s.replace(a, '''  // ---------------------------------------------------------------- an ordinary viewer called off before it began
  // r3 (Codex R3): the shell's cancel event is already signalled when the viewer starts (its owner signed out while it
  // was starting). The product viewer must open nothing: no window, no connect, exit 0.
  {
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE cancel = CreateEventW(&sa, TRUE, TRUE, nullptr);  // inheritable, already signalled
    Child pre;
    const std::wstring pdir = gRoot + L"viewer-presignalled\\\\";
    CreateDirectoryW(pdir.c_str(), nullptr);
    Managed::Add_offscreen();
    const auto penv = env_with(pdir + L"lad", {});
    pre.logPath = pdir + L"viewer.log";
    const bool started = pre.Start(L"\\"" + gBuild + L"GNLinkViewerKaTest.exe\\" --transport udp --codec h264 --enable-input-channel"
                                   L" --initial-view targets --directory-url \\"" + W(url) + L"\\" --directory-session \\"" + W(session) +
                                   L"\\" --directory-host-id \\"" + W(idA) + L"\\" --cancel-event " +
                                   std::to_wstring(reinterpret_cast<uintptr_t>(cancel)),
                                   gBuild, &penv, {cancel});
    bool windowSeen = false;
    const uint64_t until = now_ms() + 10000;
    while (started && now_ms() < until && !pre.Exited(0)) {
      windowSeen = windowSeen || viewer_window(pre.pi.dwProcessId) != nullptr;
      Sleep(10);
    }
    DWORD code = 99;
    const bool ended = started && pre.Exited(2000) && GetExitCodeProcess(pre.pi.hProcess, &code);
    const std::string t = pre.Text();
    check("a viewer whose cancel was signalled before it began opens nothing: no window, no connect, exit 0",
          ended && code == 0 && !windowSeen && t.find("called off before the session began") != std::string::npos &&
              t.find("connected host=") == std::string::npos,
          "exit=" + std::to_string(code) + " window=" + (windowSeen ? "1" : "0"));
    CloseHandle(cancel);
  }

''' + a)
open(p, 'wb').write((s.replace('\n', '\r\n') if crlf else s).encode('utf-8'))
print('ok')
