// lockscreen-cred r1 (codex_lockcred_agent_r1.md): the service <-> one-shot agent channel (lock_cred_channel.hpp)
// on real pipes and real processes -- this test plays the service, copies of itself play the agent, a stranger
// and a foreign server. What it pins:
//   - the pipe is one-shot: a second create under the same name fails (FIRST_PIPE_INSTANCE);
//   - the job goes only to the held child: a stranger that connects first is refused and receives nothing;
//   - the agent talks only to its parent's pipe: a server that is not its parent gets no Hello;
//   - the SHIPPED GNLinkInputService.exe started directly as an agent against a pipe that is not the installed
//     service's refuses it (exit 3) -- it never reads a job; a malformed id is refused (exit 2);
//   - everything is bounded: a child that dies before connecting, and one that never answers, end the wait.
// What it cannot show: another session (all of this is one user in one session) and SYSTEM -- the real
// session-0 service -> console-session SYSTEM agent -> Winlogon is the field check.

#include <windows.h>
#include <sddl.h>

#include <chrono>
#include <cstdio>
#include <string>

#include "lock_cred_channel.hpp"

namespace lc = remote60::native_poc::lock_cred_channel;

namespace {
int gChecks = 0, gFailures = 0;
void check(const char* what, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : "  ", detail.c_str());
}

std::wstring self_path() {
  wchar_t p[MAX_PATH] = {};
  GetModuleFileNameW(nullptr, p, MAX_PATH);
  return p;
}
std::wstring dir_of(const std::wstring& p) { return p.substr(0, p.find_last_of(L"\\/") + 1); }

const wchar_t* user_sddl() {
  static std::wstring s;
  if (s.empty()) {
    HANDLE tok = nullptr;
    OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok);
    DWORD need = 0;
    GetTokenInformation(tok, TokenUser, nullptr, 0, &need);
    std::string buf(need, '\0');
    GetTokenInformation(tok, TokenUser, buf.data(), need, &need);
    CloseHandle(tok);
    wchar_t* sid = nullptr;
    ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buf.data())->User.Sid, &sid);
    s = L"D:P(A;;GA;;;SY)(A;;GA;;;" + std::wstring(sid) + L")";
    LocalFree(sid);
  }
  return s.c_str();
}

bool start(const std::wstring& exe, const std::wstring& args, PROCESS_INFORMATION* pi, bool suspended) {
  std::wstring cmd = L"\"" + exe + L"\" " + args;
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  return CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW | (suspended ? CREATE_SUSPENDED : 0), nullptr, nullptr, &si, pi) != FALSE;
}
DWORD wait_exit(const PROCESS_INFORMATION& pi, DWORD ms) {
  DWORD code = STILL_ACTIVE;
  if (WaitForSingleObject(pi.hProcess, ms) == WAIT_OBJECT_0) GetExitCodeProcess(pi.hProcess, &code);
  else TerminateProcess(pi.hProcess, 99);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return code;
}
DWORD my_session() {
  DWORD s = 0;
  ProcessIdToSessionId(GetCurrentProcessId(), &s);
  return s;
}
bool trust_parent_only(HANDLE) { return true; }  // the test's "trusted service" check: the parent check stands

// --- the child roles ---------------------------------------------------------------------------------
int agent_role(const std::wstring& id, bool stall) {  // the agent's side, as the product's agent does it
  const char* why = "";
  HANDLE pipe = lc::OpenFromAgent(id, lc::ParentProcessId(), trust_parent_only, &why);
  if (pipe == INVALID_HANDLE_VALUE) return 3;
  lc::Hello h{};
  h.pid = GetCurrentProcessId();
  DWORD n = 0;
  WriteFile(pipe, &h, sizeof(h), &n, nullptr);
  if (stall) {
    Sleep(60000);
    return 5;
  }
  lc::Job job{};
  DWORD got = 0, total = 0;
  while (total < sizeof(job) && ReadFile(pipe, reinterpret_cast<uint8_t*>(&job) + total, sizeof(job) - total, &got, nullptr) && got)
    total += got;
  if (total != sizeof(job) || job.magic != lc::kJobMagic || job.count != 7) return 4;
  // r2 (L2): as the product's agent -- found its target, asks the service's fence; "types" only on Go.
  const bool go = lc::AgentAskGo(pipe, job.requestId);
  lc::AgentAnswer(pipe, job.requestId, go ? 8 : 13, 0);  // Injected : TargetNotReady
  SecureZeroMemory(&job, sizeof(job));
  CloseHandle(pipe);
  return go ? 0 : 6;  // 6: it typed nothing
}
int stranger_role(const std::wstring& id) {  // connects first, says Hello with its own pid, waits for anything
  HANDLE pipe = CreateFileW(lc::PipeNameFor(id).c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
  if (pipe == INVALID_HANDLE_VALUE) return 3;
  lc::Hello h{};
  h.pid = GetCurrentProcessId();
  DWORD n = 0;
  WriteFile(pipe, &h, sizeof(h), &n, nullptr);
  uint8_t b[16];
  const BOOL r = ReadFile(pipe, b, sizeof(b), &n, nullptr);  // returns when the server lets go
  CloseHandle(pipe);
  return (r && n > 0) ? 7 : 0;  // 7: it was SENT something
}
int server_role(const std::wstring& id) {  // a server that is not the agent's parent
  uint32_t err = 0;
  std::wstring got;
  HANDLE pipe = lc::CreateServerPipe(user_sddl(), &got, &err, id);
  if (pipe == INVALID_HANDLE_VALUE) return 3;
  OVERLAPPED ov{};
  ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  ConnectNamedPipe(pipe, &ov);
  const bool connected = WaitForSingleObject(ov.hEvent, 8000) == WAIT_OBJECT_0;
  lc::Hello h{};
  const bool hello = connected && lc::ReadBounded(pipe, &h, sizeof(h), 2000);
  CloseHandle(pipe);
  return hello ? 7 : 0;  // 7: the agent said Hello to a server that is not its parent
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
  if (argc == 3) {
    const std::wstring mode = argv[1];
    if (mode == L"--agent") return agent_role(argv[2], false);
    if (mode == L"--stall") return agent_role(argv[2], true);
    if (mode == L"--stranger") return stranger_role(argv[2]);
    if (mode == L"--server") return server_role(argv[2]);
    if (mode == L"--die") return 9;
  }
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::printf("the service <-> lock-screen agent channel (lockscreen-cred r1)\n\n");
  const std::wstring me = self_path();
  const DWORD session = my_session();

  {
    uint32_t err = 0;
    std::wstring id;
    HANDLE first = lc::CreateServerPipe(user_sddl(), &id, &err);
    uint32_t err2 = 0;
    std::wstring id2;
    HANDLE second = lc::CreateServerPipe(user_sddl(), &id2, &err2, id);
    check("one-shot: a fresh random 32-hex id, and a second create of the same name FAILS (first instance only)",
          first != INVALID_HANDLE_VALUE && lc::IdIsWellFormed(id) && second == INVALID_HANDLE_VALUE,
          "err2=" + std::to_string(err2));
    if (second != INVALID_HANDLE_VALUE) CloseHandle(second);
    if (first != INVALID_HANDLE_VALUE) CloseHandle(first);
    std::wstring id3;
    HANDLE other = lc::CreateServerPipe(user_sddl(), &id3, &err);
    check("...and the next pipe gets another id", other != INVALID_HANDLE_VALUE && id3 != id);
    if (other != INVALID_HANDLE_VALUE) CloseHandle(other);
    check("a malformed id is refused", !lc::IdIsWellFormed(L"..\\x") && !lc::IdIsWellFormed(L"ABCDEF") &&
                                           lc::CreateServerPipe(user_sddl(), nullptr, &err, L"zz") == INVALID_HANDLE_VALUE);
  }

  std::printf("\n[the held child gets the job]\n");
  {
    uint32_t err = 0;
    std::wstring id;
    HANDLE pipe = lc::CreateServerPipe(user_sddl(), &id, &err);
    PROCESS_INFORMATION pi{};
    const bool started = start(me, L"--agent " + id, &pi, true);
    ResumeThread(pi.hThread);
    const char* why = "";
    const bool accepted = started && lc::AcceptOwnedChild(pipe, pi.hProcess, pi.dwProcessId, session, false, 5000, &why);
    lc::Job job{};
    job.session = session;
    job.requestId = 41;
    job.count = 7;
    int fenceAsked = 0;
    uint32_t w32 = 0;
    const lc::JobOutcome outcome =
        accepted ? lc::RunJob(pipe, job, [&] { ++fenceAsked; return true; }, 3000, 3000, &w32) : lc::JobOutcome::NoAnswer;
    CloseHandle(pipe);
    const DWORD code = started ? wait_exit(pi, 5000) : 99;
    check("the child the service started (held handle: pid, running, session, its Hello) is accepted", accepted, why);
    check("...gets the job whole, asks the fence once right before typing, is allowed, answers Typed, exits by itself",
          outcome == lc::JobOutcome::Typed && fenceAsked == 1 && code == 0, "exit " + std::to_string(code));
  }

  std::printf("\n[r2 L2: the fence says no at the commit]\n");
  {
    uint32_t err = 0;
    std::wstring id;
    HANDLE pipe = lc::CreateServerPipe(user_sddl(), &id, &err);
    PROCESS_INFORMATION pi{};
    const bool started = start(me, L"--agent " + id, &pi, true);
    ResumeThread(pi.hThread);
    const char* why = "";
    const bool accepted = started && lc::AcceptOwnedChild(pipe, pi.hProcess, pi.dwProcessId, session, false, 5000, &why);
    lc::Job job{};
    job.session = session;
    job.requestId = 42;
    job.count = 7;
    uint32_t w32 = 0;
    const lc::JobOutcome outcome = accepted ? lc::RunJob(pipe, job, [] { return false; }, 3000, 3000, &w32) : lc::JobOutcome::NoAnswer;
    CloseHandle(pipe);
    const DWORD code = started ? wait_exit(pi, 5000) : 99;
    check("L2: the request no longer holds when the agent is ready: Denied, and the agent typed NOTHING (exit 6)",
          outcome == lc::JobOutcome::Denied && code == 6, "exit " + std::to_string(code));
  }

  std::printf("\n[NEGATIVE: a stranger connects first]\n");
  {
    uint32_t err = 0;
    std::wstring id;
    HANDLE pipe = lc::CreateServerPipe(user_sddl(), &id, &err);
    PROCESS_INFORMATION stranger{}, child{};
    const bool s = start(me, L"--stranger " + id, &stranger, false);
    Sleep(500);  // it is connected before the child exists
    const bool c = start(me, L"--agent " + id, &child, true);
    ResumeThread(child.hThread);
    const char* why = "";
    const bool accepted = s && c && lc::AcceptOwnedChild(pipe, child.hProcess, child.dwProcessId, session, false, 5000, &why);
    CloseHandle(pipe);  // nothing written: the stranger's read ends with nothing
    const DWORD strangerCode = s ? wait_exit(stranger, 5000) : 99;
    const DWORD childCode = c ? wait_exit(child, 10000) : 99;
    check("NEGATIVE: a client that is not the held child is refused (\"client-not-the-child\")",
          !accepted && std::string(why) == "client-not-the-child", why);
    check("NEGATIVE: ...and it was sent nothing", strangerCode == 0, "stranger exit " + std::to_string(strangerCode));
    check("...the real child, kept out, reads no job (exit 3/4)", childCode == 3 || childCode == 4, std::to_string(childCode));
  }

  std::printf("\n[NEGATIVE: the agent and a server that is not its parent]\n");
  {
    // A fresh id the foreign server creates; the agent is started by THIS process -- its parent is not that server.
    uint32_t err = 0;
    std::wstring id;
    HANDLE probe = lc::CreateServerPipe(user_sddl(), &id, &err);
    CloseHandle(probe);  // just to draw a fresh id
    PROCESS_INFORMATION server{}, agent{};
    const bool s = start(me, L"--server " + id, &server, false);
    Sleep(500);
    const bool a = start(me, L"--agent " + id, &agent, false);
    const DWORD agentCode = a ? wait_exit(agent, 8000) : 99;
    const DWORD serverCode = s ? wait_exit(server, 10000) : 99;
    check("NEGATIVE: the agent refuses a server that is not its parent (exit 3)", agentCode == 3, std::to_string(agentCode));
    check("NEGATIVE: ...that server never got its Hello", serverCode == 0, "server exit " + std::to_string(serverCode));
  }

  std::printf("\n[NEGATIVE: the SHIPPED service binary started directly as an agent]\n");
  {
    const std::wstring shipped = dir_of(me) + L"GNLinkInputService.exe";
    uint32_t err = 0;
    std::wstring id;
    HANDLE pipe = lc::CreateServerPipe(user_sddl(), &id, &err);
    PROCESS_INFORMATION pi{};
    const bool started = GetFileAttributesW(shipped.c_str()) != INVALID_FILE_ATTRIBUTES && start(shipped, L"--lockcred-agent " + id, &pi, false);
    // It may connect (and look) -- it must not stay, say Hello, or read a job: this server is not the installed service.
    OVERLAPPED ov{};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ConnectNamedPipe(pipe, &ov);
    WaitForSingleObject(ov.hEvent, 5000);
    lc::Hello h{};
    const bool hello = lc::ReadBounded(pipe, &h, sizeof(h), 2000);
    CloseHandle(ov.hEvent);
    CloseHandle(pipe);
    const DWORD code = started ? wait_exit(pi, 8000) : 99;
    check("NEGATIVE: GNLinkInputService.exe --lockcred-agent <id> against a pipe that is not the installed service's: "
          "refused (exit 3), no Hello",
          started && code == 3 && !hello, "exit " + std::to_string(code));
    PROCESS_INFORMATION bad{};
    const bool b = start(shipped, L"--lockcred-agent ..\\pipe\\x", &bad, false);
    check("NEGATIVE: ...with a malformed id: refused (exit 2)", b && wait_exit(bad, 5000) == 2);
  }

  std::printf("\n[bounded]\n");
  {
    uint32_t err = 0;
    std::wstring id;
    HANDLE pipe = lc::CreateServerPipe(user_sddl(), &id, &err);
    PROCESS_INFORMATION pi{};
    const bool started = start(me, L"--die " + id, &pi, false);
    const auto t0 = std::chrono::steady_clock::now();
    const char* why = "";
    const bool accepted = started && lc::AcceptOwnedChild(pipe, pi.hProcess, pi.dwProcessId, session, false, 20000, &why);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    CloseHandle(pipe);
    if (started) wait_exit(pi, 2000);
    check("a child that dies before connecting ends the wait at once (not the 20 s bound)", !accepted && ms < 3000,
          std::string(why) + " " + std::to_string(ms) + " ms");
  }
  {
    uint32_t err = 0;
    std::wstring id;
    HANDLE pipe = lc::CreateServerPipe(user_sddl(), &id, &err);
    PROCESS_INFORMATION pi{};
    const bool started = start(me, L"--stall " + id, &pi, false);
    const char* why = "";
    const bool accepted = started && lc::AcceptOwnedChild(pipe, pi.hProcess, pi.dwProcessId, session, false, 5000, &why);
    lc::Job job{};
    job.requestId = 43;
    const auto t0 = std::chrono::steady_clock::now();
    uint32_t w32 = 0;
    const bool answered = accepted && lc::RunJob(pipe, job, [] { return true; }, 1500, 1500, &w32) != lc::JobOutcome::NoAnswer;
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    CloseHandle(pipe);
    if (started) wait_exit(pi, 100);  // ended by the test (the service's job does this)
    check("a child that never answers: the read ends at its bound", accepted && !answered && ms < 3000,
          std::to_string(ms) + " ms");
  }

  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", gFailures ? "FAILED" : "ALL PASS", gChecks, gFailures);
  return gFailures ? 1 : 0;
}
