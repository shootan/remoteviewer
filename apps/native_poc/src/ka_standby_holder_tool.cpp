// rel-0164 old-APK lane (Codex codex_rel_0164_split_decision.md (2), legacy combination 1, case 7): ONE PC-side standby
// lease held on a host while a phone (the published APK 0.2.32, or its same-source fixture build) uses that host.
// TEST TOOL ONLY -- never shipped. It plays the client shell's part for the PRODUCT managed viewer (GNLinkViewerKaTest
// --managed-standby: the product viewer with the test window seam -- its window, if any, beyond the right edge of the
// virtual screen, shown without focus), over the same two private pipes the shell uses (managed_viewer_ipc.hpp):
// Init, then nothing more -- no Activate: a standby only -- and Release at the end.
//
//   remote60_ka_standby_holder_tool --url http://127.0.0.1:<port> --host-name <name> --account-file <file>
//       --viewer <GNLinkViewerKaTest.exe> --root <folder> --stop-file <path> [--max-sec 900]
//
// --account-file: "<id> <password>" (the fixture's own test account file); the password is never printed or logged.
// --root: an existing folder of THIS run (not a link); everything the tool writes goes under it: standby_state.txt
// (each viewer message: time, type, state / lost / entry / reason -- no session, no token), standby_ready.txt (the first
// `ready`), viewer\viewer.log and viewer\lad (the viewer's LOCALAPPDATA). The viewer runs in a kill-on-close job of
// this process. The tool ends on --stop-file (then Release, and the viewer's exit is waited for) or at --max-sec.
// Exit (r3, ka_standby_holder_verdict.hpp): 0 ready, released, ended by itself, child verdict clean / 2 setup /
// 3 never ready / 4 the viewer died / 5 Release not sent or not ended within 15 s after it (cleanup) / 6 child verdict.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "directory_session_client.hpp"
#include "e2e_isolation.hpp"
#include "ka_pc_e2e_child.hpp"
#include "ka_standby_holder_verdict.hpp"
#include "managed_viewer_ipc.hpp"
#include "native_socket.hpp"

using namespace remote60::native_poc;
using managed_ipc::Message;
using managed_ipc::Type;

namespace {
uint64_t now_ms() {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}
std::wstring W(const std::string& s) { return std::wstring(s.begin(), s.end()); }
std::string iso_now() {
  SYSTEMTIME t;
  GetLocalTime(&t);
  char b[40];
  std::snprintf(b, sizeof(b), "%04u-%02u-%02uT%02u:%02u:%02u.%03u", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
                t.wSecond, t.wMilliseconds);
  return b;
}
const char* type_name(Type t) {
  switch (t) {
    case Type::Hello: return "Hello";
    case Type::State: return "State";
    case Type::NeedToken: return "NeedToken";
    case Type::PickerReady: return "PickerReady";
    case Type::Presented: return "Presented";
    case Type::SessionEnded: return "SessionEnded";
    default: return "other";
  }
}
bool is_dir_not_link(const std::wstring& p) {
  const DWORD a = GetFileAttributesW(p.c_str());
  return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) && !(a & FILE_ATTRIBUTE_REPARSE_POINT);
}
}  // namespace

int main(int argc, char** argv) {
  std::map<std::string, std::string> a;
  for (int i = 1; i + 1 < argc; i += 2) a[argv[i]] = argv[i + 1];
  const std::string url = a["--url"], hostName = a["--host-name"], accountFile = a["--account-file"],
                    viewer = a["--viewer"], root = a["--root"], stopFile = a["--stop-file"];
  const int maxSec = a.count("--max-sec") ? std::atoi(a["--max-sec"].c_str()) : 900;
  if (url.empty() || hostName.empty() || accountFile.empty() || viewer.empty() || root.empty() || stopFile.empty() ||
      maxSec <= 0 || maxSec > 3600) {
    std::printf("SETUP  usage: --url --host-name --account-file --viewer --root --stop-file [--max-sec <=3600]\n");
    return 2;
  }
  const std::wstring wroot = W(root) + L"\\";
  if (!is_dir_not_link(W(root))) {
    std::printf("SETUP  --root is not an existing folder (or is a link)\n");
    return 2;
  }
  std::string account, password;
  {
    std::ifstream f(accountFile);
    f >> account >> password;
  }
  if (account.empty() || password.empty()) {
    std::printf("SETUP  the account file holds no \"<id> <password>\"\n");
    return 2;
  }
  std::ofstream stateOut(root + "\\standby_state.txt", std::ios::app);
  auto note = [&](const std::string& line) {
    stateOut << iso_now() << " mono=" << now_ms() << " " << line << "\n";
    stateOut.flush();
    std::printf("%s %s\n", iso_now().c_str(), line.c_str());
    std::fflush(stdout);
  };
  std::string err;
  if (!initialize_sockets(&err)) {
    note("SETUP sockets: " + err);
    return 2;
  }

  // the host's id, by its name, as the account sees it (online)
  std::string session, hostId;
  directory::ObserveEndpoint adv;
  const uint64_t listUntil = now_ms() + 60000;
  while (now_ms() < listUntil && hostId.empty()) {
    if (session.empty()) directory_login(url, account, password, &session, &err, &adv);
    std::vector<DirectoryHostEntry> hosts;
    if (!session.empty() && directory_list_hosts(url, session, &hosts, &err)) {
      for (const auto& h : hosts)
        if (h.hostName == hostName && h.online) hostId = h.hostId;
    }
    if (hostId.empty()) Sleep(500);
  }
  password.assign(password.size(), '\0');
  if (hostId.empty()) {
    note("SETUP host \"" + hostName + "\" not online for the account within 60 s");
    return 2;
  }
  note("host " + hostName + " id=" + hostId);

  // the managed viewer, with this tool as its shell
  SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
  HANDLE toChildR = nullptr, toChildW = nullptr, fromChildR = nullptr, fromChildW = nullptr;
  if (!(CreatePipe(&toChildR, &toChildW, &sa, 1 << 16) && CreatePipe(&fromChildR, &fromChildW, &sa, 1 << 16) &&
        SetHandleInformation(toChildW, HANDLE_FLAG_INHERIT, 0) && SetHandleInformation(fromChildR, HANDLE_FLAG_INHERIT, 0))) {
    note("SETUP the channel's pipes");
    return 2;
  }
  const std::wstring vdir = wroot + L"viewer\\";
  CreateDirectoryW(vdir.c_str(), nullptr);
  CreateDirectoryW((vdir + L"lad").c_str(), nullptr);
  // the test window seam: beyond the right edge of the virtual screen
  const int right = GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN);
  SetEnvironmentVariableW(L"REMOTE60_VIEWER_TEST_WINDOW_X", std::to_wstring(right + 200).c_str());
  SetEnvironmentVariableW(L"REMOTE60_VIEWER_TEST_WINDOW_Y", std::to_wstring(GetSystemMetrics(SM_YVIRTUALSCREEN) + 40).c_str());
  const auto env = e2e::e2e_isolated_environment(vdir + L"lad");
  const std::wstring wviewer = W(viewer);
  const std::wstring vbuild = wviewer.substr(0, wviewer.find_last_of(L"\\/") + 1);
  const std::wstring cmd = L"\"" + wviewer + L"\" --transport udp --codec h264 --enable-input-channel --initial-view targets"
                           L" --directory-url \"" + W(url) + L"\" --directory-host-id \"" + W(hostId) +
                           L"\" --managed-standby --ipc-read " + std::to_wstring(reinterpret_cast<uintptr_t>(toChildR)) +
                           L" --ipc-write " + std::to_wstring(reinterpret_cast<uintptr_t>(fromChildW));
  ka_pc_e2e::Child proc;
  proc.logPath = vdir + L"viewer.log";
  const bool started = proc.Start(cmd, vbuild, &env, {toChildR, fromChildW});
  CloseHandle(toChildR);
  CloseHandle(fromChildW);
  if (!started) {
    CloseHandle(fromChildR);
    CloseHandle(toChildW);
    note("SETUP the viewer did not start");
    return 2;
  }
  managed_ipc::Channel ch;
  ch.Start(fromChildR, toChildW);
  const std::string key = url + "\n" + account + "\n" + hostId, inc = "1", epoch = "1";
  auto send = [&](Type t, std::map<std::string, std::string> f) {
    Message m;
    m.type = t;
    m.fields = std::move(f);
    m.fields["key"] = key;
    m.fields["inc"] = inc;
    m.fields["epoch"] = epoch;
    return ch.Send(m);
  };
  const std::string genDir = std::string(root) + "\\viewer\\standby";
  send(Type::Init, {{"url", url}, {"session", session}, {"tokenVersion", "1"}, {"hostId", hostId}, {"hostName", hostName},
                    {"genDir", genDir}});
  session.assign(session.size(), '\0');
  note("Init sent (standby only: no Activate)");

  bool ready = false, released = false, died = false;
  std::string lastState;
  const uint64_t until = now_ms() + static_cast<uint64_t>(maxSec) * 1000;
  auto pump = [&](int ms) {
    Message m;
    if (ch.Receive(&m, ms)) {
      std::string line = std::string(type_name(m.type));
      for (const char* k : {"state", "lost", "entry", "reason", "op", "rc"}) {
        const std::string v = m.get(k);
        if (!v.empty()) line += std::string(" ") + k + "=" + v;
      }
      note(line);
      if (m.type == Type::State) {
        lastState = m.get("state");
        if (lastState == "ready" && !ready) {
          ready = true;
          std::ofstream(root + "\\standby_ready.txt") << iso_now() << " ready host=" << hostName << "\n";
        }
      }
      if (m.type == Type::NeedToken) note("NeedToken: not answered (the run is short; the lease's own token is kept)");
    }
  };
  while (now_ms() < until) {
    pump(200);
    if (proc.Exited(0)) {
      died = true;
      note("the viewer EXITED on its own");
      break;
    }
    if (GetFileAttributesW(W(stopFile).c_str()) != INVALID_FILE_ATTRIBUTES) {
      note("stop file seen: Release");
      break;
    }
  }
  // r3 (Codex codex_old_apk_lane_src_r2.md N2): the exit is decided from the whole end -- Release sent, the viewer
  // ended by itself within its deadline, and the shared child verdict (end confirmed, output read to EOF, log kept).
  // A viewer the job has to end is CLEANUP only: it is reported as such and never makes a pass.
  ka_standby::HolderEnd end;
  end.ready = ready;
  end.died = died;
  if (!died) {
    end.releaseSent = send(Type::Release, {{"reason", "test"}});
    if (!end.releaseSent) note("Release could NOT be sent");
    const uint64_t endBy = now_ms() + 15000;
    while (end.releaseSent && now_ms() < endBy && !proc.Exited(0)) pump(200);
    released = end.releaseSent && proc.Exited(0);
    end.endedAfterRelease = released;
    if (!released) note("viewer did NOT end by itself within 15 s of Release -- CLEANUP: its job ends it (not a pass)");
    else note("viewer ended after Release");
  }
  ch.Close();
  proc.End();                                       // its job ended, its output read to EOF, its log kept -- or a failure
  end.childFailures = ka_pc_e2e::children_end_all();  // the shared verdict of every child this tool started
  const int rc = ka_standby::holder_exit(end);
  note(std::string("RESULT rc=") + std::to_string(rc) + " (" + ka_standby::holder_exit_why(end) + ") ready=" +
       (ready ? "1" : "0") + " lastState=" + lastState + " died=" + (died ? "1" : "0") + " releaseSent=" +
       (end.releaseSent ? "1" : "0") + " released=" + (released ? "1" : "0") + " childFailures=" +
       std::to_string(end.childFailures));
  return rc;
}
