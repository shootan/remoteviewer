p = r'C:\work\gnlink-wt\ka-pc\apps\native_poc\src\ka_pc_managed_e2e_test.cpp'
b = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in b
s = b.replace('\r\n', '\n')

def rep(a, c, cnt=1):
    global s
    assert s.count(a) == cnt, (s.count(a), a[:80])
    s = s.replace(a, c)

rep('#include "managed_viewer_ipc.hpp"\n', '#include "client_managed_viewers.hpp"\n#include "managed_viewer_ipc.hpp"\n')

# ---------------------------------------------------------------- other device in use: no interference, tap takes over
anchor = '  // ---------------------------------------------------------------- the message fence\n'
other = r'''  // ---------------------------------------------------------------- another device using the Host
  // An ordinary client (the same test viewer, its ordinary directory path: observe, connect, punch, Hello) holds
  // the Host. The kept PC's lease keeps pinging the whole time: the other session must go on undisturbed. Only the
  // tap (Activate) takes the Host over; the other session then ends, and nothing takes it back automatically.
  {
    Child other;
    const std::wstring odir = gRoot + L"viewer-other\\";
    CreateDirectoryW(odir.c_str(), nullptr);
    Managed::Add_offscreen();
    const auto oenv = env_with(odir + L"lad", {});
    other.logPath = odir + L"viewer.log";
    const bool started = other.Start(L"\"" + gBuild + L"GNLinkViewerKaTest.exe\" --transport udp --codec h264 --enable-input-channel"
                                     L" --initial-view stream --directory-url \"" + W(url) + L"\" --directory-session \"" + W(session) +
                                     L"\" --directory-host-id \"" + W(idA) + L"\" --seconds 60",
                                     gBuild, &oenv);
    const bool connected = started && other.WaitFor([](const std::string& t) { return t.find("connected host=") != std::string::npos; }, 20000);
    check("another device (an ordinary client) holds the Host", connected);
    const std::string before = hostA.Text();
    const size_t standbyPingsSeen = a.Count();
    Sleep(12000);  // two Ping periods and more, the kept PC standing by
    const std::string during = hostA.Text().substr(before.size());
    check("...the kept PC kept standing by meanwhile (its state still reported)", a.Count() > standbyPingsSeen);
    check("...and the other session was not disturbed: no new session, no takeover, it is still running",
          during.find("session epoch=") == std::string::npos && during.find("standby activation") == std::string::npos &&
              !other.Exited(0),
          during.find("session epoch=") == std::string::npos ? "" : "a session started on the Host");
    const size_t from = a.Count();
    a.Send(Type::Activate, {{"op", "3"}});
    const int pr = a.WaitMsg(Type::PickerReady, from, 15000, [](const Message& m) { return m.get("op") == "3"; });
    check("the tap takes the Host over (PickerReady)", pr >= 0);
    const bool otherEnded = other.WaitFor([](const std::string& t) {
      return t.find("control-lost") != std::string::npos || t.find("session ended") != std::string::npos ||
             t.find("peer") != std::string::npos;
    }, 1) || other.Exited(20000);
    check("...and the other device's session ends", otherEnded || other.Text().find("done") != std::string::npos);
    const size_t from2 = a.Count();
    a.Send(Type::Park, {{"op", "3"}});
    check("...park", a.WaitMsg(Type::SessionEnded, from2, 15000) >= 0);
    a.WaitMsg(Type::State, from2, 40000, [](const Message& m) { return m.get("state") == "ready"; });
    other.End();
  }

'''
rep(anchor, other + anchor)

# ---------------------------------------------------------------- the shell's module itself
anchor2 = '  hostA.End();\n  if (haveOld) {'
module = r'''  // ---------------------------------------------------------------- the shell's ManagedViewers (product module)
  // The product module the client shell uses: suspended -> its own job -> resumed, Init over the pipes, events
  // stamped with the child and the owner, EndAll (the sign-out path: the caller has moved the owner epoch first),
  // and the shell's death (its job handles closing).
  {
    struct Queue {
      std::mutex mu;
      std::vector<std::function<void()>> q;
    } queue;
    std::vector<shell::ManagedEvent> events;
    shell::ManagedViewers mv(
        [&queue](std::function<void()> f) { std::lock_guard<std::mutex> l(queue.mu); queue.q.push_back(std::move(f)); },
        [](const std::string& line) { std::printf("  [module] %s\n", line.c_str()); },
        [](HANDLE read, uint32_t, const std::string&) {
          std::thread([read] {
            char b[4096];
            DWORD got = 0;
            while (ReadFile(read, b, sizeof(b), &got, nullptr) && got > 0) {}
            CloseHandle(read);
          }).detach();
        },
        [&events](const shell::ManagedEvent& e) { events.push_back(e); });
    auto drain = [&](int ms) {
      const uint64_t until = now_ms() + ms;
      while (now_ms() < until) {
        std::vector<std::function<void()>> q;
        {
          std::lock_guard<std::mutex> l(queue.mu);
          q.swap(queue.q);
        }
        for (auto& f : q) f();
        Sleep(50);
      }
    };
    shell::ManagedLaunch spec;
    spec.exe = gBuild + L"GNLinkViewerKaTest.exe";
    spec.workDir = gBuild;
    spec.baseArgs = L"--transport udp --codec h264 --enable-input-channel --initial-view targets --directory-url \"" + W(url) +
                    L"\" --directory-host-id \"" + W(idA) + L"\"";
    spec.url = url;
    spec.session = session;
    spec.tokenVersion = 1;
    spec.hostId = idA;
    spec.hostName = "KA-A";
    spec.key = url + "\ntester\n" + idA;
    spec.genDir = N(gRoot) + "viewer-A\\standby";
    spec.ownerEpoch = 7;
    std::string why;
    check("the shell module launches the managed viewer", mv.Launch(spec, &why), why);
    std::string state;
    const uint64_t until = now_ms() + 40000;
    while (now_ms() < until && state != "ready") {
      drain(200);
      for (const auto& e : events)
        if (!e.exited && e.message.type == Type::State && mv.Current(e, 7)) state = e.message.get("state");
    }
    check("...its State reaches the shell: ready", state == "ready", state);
    // an event under a newer owner epoch (the shell signed out in between) is not the current child's
    bool dropped = true;
    for (const auto& e : events)
      if (mv.Current(e, 8)) dropped = false;
    check("...events of an older owner are dropped (Current under epoch 8 is none)", dropped);
    const auto* c = mv.Find(idA);
    const DWORD pid = c ? c->pid : 0;
    HANDLE ph = pid ? OpenProcess(SYNCHRONIZE, FALSE, pid) : nullptr;
    const uint64_t t0 = now_ms();
    mv.EndAll("signed out");
    check("EndAll returns at once (the waits are elsewhere)", now_ms() - t0 < 500, std::to_string(now_ms() - t0) + " ms");
    check("...and the child ends within the bound", ph && WaitForSingleObject(ph, 6000) == WAIT_OBJECT_0);
    if (ph) CloseHandle(ph);
    mv.JoinReapers();
    check("...nothing is kept after it", mv.size() == 0);
    // the shell's death: a child launched by the module, then the module (its job handles) destroyed without EndAll
    // is covered by ~ManagedViewers -> EndAllNow; the raw job close is the case above (KILL_ON_JOB_CLOSE).
  }

'''
rep(anchor2, module + anchor2)
open(p, 'wb').write((s.replace('\n', '\r\n') if crlf else s).encode('utf-8'))
print('ok')
