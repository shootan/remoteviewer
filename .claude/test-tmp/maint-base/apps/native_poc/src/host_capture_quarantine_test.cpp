// dxgi-wedge-fix r1 (Codex B7, Bⓐ2): the REAL supervisor, stopped and started again, against the
// persistent DXGI quarantine.
//
// It compiles the product host_app_main.cpp (entry renamed, REMOTE60_HOST_TEST_SEAM on) and drives a real
// StreamingHostProcess whose streaming child is THIS executable in stand-in mode. The stand-in prints what a
// real GNLinkStream prints about its capture adapter (the chosen line), writes down the
// REMOTE60_CAPTURE_DXGI_QUARANTINE value it was STARTED with -- the child environment the supervisor built,
// the only thing a real child reads -- and exits with the chosen code. The supervisor's own exit path
// records the wedge in a test capture-recovery folder (the product's protected-folder code, a test root and
// an owner rule that trusts this user); a NEW StreamingHostProcess is the supervisor started again.
//
// What differs from the product: the store root and owner rule (gHostTest.captureRecovery*), and the list
// of adapters "present" (gHostTest.presentAdapters) -- the stand-in reports a made-up adapter, so the
// machine's real GPUs are not what is matched. The child is not GNLinkStream: that the real child reads
// this variable and keeps WGC is host_capture_quarantine_child_e2e's part.
// Covered: same driver -> WGC after a supervisor restart; driver changed / expired / damaged / from the
// future -> the default (DXGI) with the reason logged; exit 44 without an adapter line -> nothing recorded,
// this run still WGC; a quarantine value left in the PARENT's environment never reaches a child.

#define wWinMain host_product_entry
#include "host_app_main.cpp"
#undef wWinMain

#include <sddl.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "capture_recovery_store.hpp"
#include "e2e_isolation.hpp"

using namespace remote60::native_poc;
namespace cr = remote60::native_poc::capture_recovery;

namespace {

int gChecks = 0, gFailures = 0;
void check(const std::string& name, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", name.c_str(), detail.empty() ? "" : "  ", detail.c_str());
  std::fflush(stdout);
}

template <typename P>
bool wait_until(P p, int ms) {
  const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
  while (std::chrono::steady_clock::now() < end) {
    if (p()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
  }
  return p();
}

std::wstring widen_ascii(const std::string& s) { return std::wstring(s.begin(), s.end()); }
std::string narrow_ascii(const std::wstring& s) {
  std::string o;
  for (wchar_t c : s) o.push_back(c < 0x80 ? static_cast<char>(c) : '?');
  return o;
}

std::string env_a(const char* name) {
  wchar_t v[1024] = {};
  const DWORD n = GetEnvironmentVariableW(widen_ascii(name).c_str(), v, 1024);
  if (n == 0 || n >= 1024) return {};
  return narrow_ascii(std::wstring(v, n));
}

std::string slurp(const std::wstring& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream s;
  s << in.rdbuf();
  return s.str();
}

// ---- stand-in streaming child ----------------------------------------------------------------
int run_fixture_child() {
  const std::wstring state = widen_ascii(env_a("GNLINK_FIXTURE_STATE"));
  int idx = 0;
  {
    std::ifstream in(state);
    if (in) in >> idx;
  }
  { std::ofstream out(state); out << (idx + 1); }
  const std::string pfx = "FIXTURE_" + std::to_string(idx) + "_";
  const std::string line = env_a((pfx + "LINE").c_str());
  const std::string runMsS = env_a((pfx + "RUNMS").c_str());
  const std::string exitS = env_a((pfx + "EXIT").c_str());
  // What THIS child was started with -- read before anything else could change it.
  const std::string quarantine = env_a("REMOTE60_CAPTURE_DXGI_QUARANTINE");
  {
    std::ofstream log(widen_ascii(env_a("GNLINK_FIXTURE_ENVLOG")), std::ios::app);
    log << "pid=" << GetCurrentProcessId() << " q=" << (quarantine.empty() ? "-" : quarantine) << "\n";
  }
  if (!line.empty()) {
    std::printf("%s\n", line.c_str());
    std::fflush(stdout);
  }
  Sleep(runMsS.empty() ? 100u : std::strtoul(runMsS.c_str(), nullptr, 10));
  ExitProcess(static_cast<UINT>(exitS.empty() ? 0u : std::strtoul(exitS.c_str(), nullptr, 10)));
}

std::wstring gStateFile, gEnvLog;
void reset_launches() {
  { std::ofstream out(gStateFile); out << 0; }
  { std::ofstream out(gEnvLog, std::ios::trunc); }
  for (int i = 0; i < 4; ++i) {
    for (const char* k : {"LINE", "RUNMS", "EXIT"}) {
      SetEnvironmentVariableW(widen_ascii("FIXTURE_" + std::to_string(i) + "_" + k).c_str(), nullptr);
    }
  }
}
void set_launch(int idx, const std::string& line, unsigned long runMs, unsigned long exitCode) {
  const std::string p = "FIXTURE_" + std::to_string(idx) + "_";
  SetEnvironmentVariableW(widen_ascii(p + "LINE").c_str(), widen_ascii(line).c_str());
  SetEnvironmentVariableW(widen_ascii(p + "RUNMS").c_str(), std::to_wstring(runMs).c_str());
  SetEnvironmentVariableW(widen_ascii(p + "EXIT").c_str(), std::to_wstring(exitCode).c_str());
}
std::vector<std::string> env_lines() {
  std::vector<std::string> out;
  std::istringstream in(slurp(gEnvLog));
  for (std::string l; std::getline(in, l);) {
    if (!l.empty() && l.back() == '') l.pop_back();
    if (!l.empty()) out.push_back(l);
  }
  return out;
}
std::string q_of(const std::string& envLine) {
  const size_t at = envLine.find(" q=");
  return at == std::string::npos ? std::string("?") : envLine.substr(at + 3);
}

// One supervisor run: start, wait until `launches` children have reported, stop.
std::vector<std::string> run_supervisor(int launches, int waitMs = 15000) {
  StreamingHostProcess sp;
  sp.Configure(L"http://127.0.0.1:1/", L"acct", L"host-Q");
  sp.Start();
  wait_until([&] { return static_cast<int>(env_lines().size()) >= launches; }, waitMs);
  sp.Stop();
  return env_lines();
}

CaptureAdapterIdentity adapter(const char* key, const char* driver) {
  CaptureAdapterIdentity a;
  a.key = key;
  a.driver = driver;
  return a;
}
std::string adapter_line(const CaptureAdapterIdentity& a) {
  std::string l = capture_adapter_log_line(a);
  if (!l.empty() && l.back() == '\n') l.pop_back();
  return l;
}

std::wstring my_sid_string() {
  HANDLE tok = nullptr;
  OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok);
  DWORD need = 0;
  GetTokenInformation(tok, TokenUser, nullptr, 0, &need);
  std::vector<BYTE> buf(need);
  GetTokenInformation(tok, TokenUser, buf.data(), need, &need);
  CloseHandle(tok);
  LPWSTR s = nullptr;
  ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buf.data())->User.Sid, &s);
  std::wstring out = s;
  LocalFree(s);
  return out;
}
bool make_dir(const std::wstring& path, const std::wstring& sddl) {
  PSECURITY_DESCRIPTOR sd = nullptr;
  if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &sd, nullptr)) return false;
  SECURITY_ATTRIBUTES sa{sizeof(sa), sd, FALSE};
  const bool ok = CreateDirectoryW(path.c_str(), &sa) != FALSE;
  LocalFree(sd);
  return ok;
}
bool write_private(const std::wstring& path, const std::wstring& sddl, const std::string& body) {
  PSECURITY_DESCRIPTOR sd = nullptr;
  if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &sd, nullptr)) return false;
  SECURITY_ATTRIBUTES sa{sizeof(sa), sd, FALSE};
  HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  LocalFree(sd);
  if (f == INVALID_HANDLE_VALUE) return false;
  DWORD w = 0;
  const bool ok = WriteFile(f, body.data(), static_cast<DWORD>(body.size()), &w, nullptr) && w == body.size();
  CloseHandle(f);
  return ok;
}

}  // namespace

int main() {
  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  for (int i = 1; argv && i < argc; ++i) {
    if (std::wstring(argv[i]) == L"--transport") return run_fixture_child();
  }
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::printf("host_capture_quarantine_test (dxgi-wedge-fix r1)\n");

  remote60::native_poc::e2e::StagingDir staging;
  if (!staging.Create(L"capq")) {
    std::printf("FAIL  %s\n", staging.why().c_str());
    return 1;
  }
  const std::wstring root = staging.path() + L"root";
  SetEnvironmentVariableW(L"LOCALAPPDATA", (staging.path() + L"lad").c_str());
  CreateDirectoryW((staging.path() + L"lad").c_str(), nullptr);
  gStateFile = staging.path() + L"launch-index.txt";
  gEnvLog = staging.path() + L"child-env.txt";
  SetEnvironmentVariableW(L"GNLINK_FIXTURE_STATE", gStateFile.c_str());
  SetEnvironmentVariableW(L"GNLINK_FIXTURE_ENVLOG", gEnvLog.c_str());

  const std::wstring me = my_sid_string();
  PSID mine = nullptr;
  ConvertStringSidToSidW(me.c_str(), &mine);
  const std::wstring privateSddl = L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;FA;;;" + me + L")";
  const std::wstring ancestorSddl = L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;FA;;;" + me + L")(A;;0x1200a9;;;BU)";
  const std::wstring fileSddl = L"D:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;FA;;;" + me + L")";
  const std::wstring folder = root + L"\\GNLink\\capture-recovery";
  check("test capture-recovery folder chain", make_dir(root, ancestorSddl) && make_dir(root + L"\\GNLink", ancestorSddl) &&
                                                  make_dir(folder, privateSddl));
  gHostTest.streamExe = own_executable_path();
  gHostTest.captureRecoveryFolder = folder;
  gHostTest.captureRecoveryOwnerTrusted = [mine](PSID s) {
    return prelogin::OwnerIsSystemOrAdministrators(s) || (s && EqualSid(s, mine));
  };
  gHostTest.captureRecoveryFileSddl = fileSddl;
  const auto A = adapter("0123456789abcdef", "31.0.15.5244");
  const auto Anew = adapter("0123456789abcdef", "32.0.15.6094");
  gHostTest.presentAdapters = {A};
  gHostTest.presentAdaptersSet = true;
  const std::wstring recA = folder + L"\\0123456789abcdef.rec";
  const std::wstring hostLog = staging.path() + L"lad\\GNLink\\host_app.log";

  // ---- 1: a wedge in a fresh supervisor, with the adapter line --------------------------------
  {
    reset_launches();
    set_launch(0, adapter_line(A), 300, 44);
    set_launch(1, "", 60000, 0);
    const auto envs = run_supervisor(2);
    check("1: two children ran", envs.size() >= 2, std::to_string(envs.size()));
    check("1: the first child of a fresh supervisor (nothing stored) is not quarantined",
          envs.size() >= 1 && q_of(envs[0]) == "-", envs.empty() ? "" : envs[0]);
    check("1: after its exit 44 the next child is quarantined for this run",
          envs.size() >= 2 && q_of(envs[1]) == "wedge-this-run", envs.size() >= 2 ? envs[1] : "");
    const std::string rec = slurp(recA);
    check("1: the wedge is recorded against the adapter the child reported",
          rec.find("adapter=0123456789abcdef\n") != std::string::npos &&
              rec.find("driver=31.0.15.5244\n") != std::string::npos && rec.find("wedges=1\n") != std::string::npos,
          rec);
    check("1: the supervisor logged the record", slurp(hostLog).find("[host-app][capture-quarantine] recorded key=0123456789abcdef") !=
                                                   std::string::npos);
  }
  // ---- 2: the supervisor started again: same adapter, same driver -> WGC from the first child ----
  {
    reset_launches();
    set_launch(0, adapter_line(A), 60000, 0);
    const auto envs = run_supervisor(1);
    const std::string want = "persisted:key=0123456789abcdef:driver=31.0.15.5244:expires=";
    check("2: a NEW supervisor quarantines its very first child (same adapter + driver)",
          envs.size() >= 1 && q_of(envs[0]).rfind(want, 0) == 0, envs.empty() ? "" : envs[0]);
    check("2: and says so in its log", slurp(hostLog).find("launch decision=wgc reason=persisted:key=0123456789abcdef") !=
                                         std::string::npos);
  }
  // ---- 3: the driver changed -> default ------------------------------------------------------
  {
    gHostTest.presentAdapters = {Anew};
    reset_launches();
    set_launch(0, adapter_line(Anew), 60000, 0);
    const auto envs = run_supervisor(1);
    check("3: after a driver update the first child is NOT quarantined", envs.size() >= 1 && q_of(envs[0]) == "-",
          envs.empty() ? "" : envs[0]);
    check("3: the log names the reason (driver-changed)",
          slurp(hostLog).find("skipped=driver-changed recorded=31.0.15.5244 now=32.0.15.6094") != std::string::npos);
    gHostTest.presentAdapters = {A};
  }
  // ---- 4: the adapter is gone -> default -------------------------------------------------------
  {
    gHostTest.presentAdapters = {adapter("fedcba9876543210", "1.2.3.4")};
    reset_launches();
    set_launch(0, "", 60000, 0);
    const auto envs = run_supervisor(1);
    check("4: the recorded adapter is not present -> not quarantined", envs.size() >= 1 && q_of(envs[0]) == "-",
          envs.empty() ? "" : envs[0]);
    gHostTest.presentAdapters = {A};
  }
  // ---- 5: expired -> default ---------------------------------------------------------------
  {
    cr::Record old = cr::NextRecord(nullptr, A, cr::NowUnix() - cr::kQuarantineSeconds - 60);
    check("5: an expired record is written", write_private(recA, fileSddl, cr::FormatRecord(old)));
    reset_launches();
    set_launch(0, "", 60000, 0);
    const auto envs = run_supervisor(1);
    check("5: 7 days after the last wedge the first child is NOT quarantined", envs.size() >= 1 && q_of(envs[0]) == "-",
          envs.empty() ? "" : envs[0]);
    check("5: the log says expired", slurp(hostLog).find("key=0123456789abcdef skipped=expired") != std::string::npos);
  }
  // ---- 6: a damaged record / one from the future -> default, with the reason -----------------
  {
    check("6: a damaged record is written", write_private(recA, fileSddl, "schema=1\ngarbage\n"));
    reset_launches();
    set_launch(0, "", 60000, 0);
    auto envs = run_supervisor(1);
    check("6: a damaged record quarantines nothing", envs.size() >= 1 && q_of(envs[0]) == "-",
          envs.empty() ? "" : envs[0]);
    check("6: ...and is reported", slurp(hostLog).find("key=0123456789abcdef refused=malformed") != std::string::npos);
    cr::Record future = cr::NextRecord(nullptr, A, cr::NowUnix() + 86400);
    write_private(recA, fileSddl, cr::FormatRecord(future));
    reset_launches();
    set_launch(0, "", 60000, 0);
    envs = run_supervisor(1);
    check("6: a record from the future quarantines nothing", envs.size() >= 1 && q_of(envs[0]) == "-",
          envs.empty() ? "" : envs[0]);
    check("6: ...and is reported", slurp(hostLog).find("key=0123456789abcdef refused=future") != std::string::npos);
    DeleteFileW(recA.c_str());
  }
  // ---- 7: exit 44 without an adapter line -> nothing recorded, this run still WGC ---------------
  {
    reset_launches();
    set_launch(0, "", 300, 44);
    set_launch(1, "", 60000, 0);
    const auto envs = run_supervisor(2);
    check("7: still WGC for the rest of this run", envs.size() >= 2 && q_of(envs[1]) == "wedge-this-run",
          envs.size() >= 2 ? envs[1] : "");
    check("7: but nothing is recorded (the default GPU is not assumed)", GetFileAttributesW(recA.c_str()) == INVALID_FILE_ATTRIBUTES);
    check("7: and the log says why", slurp(hostLog).find("not recorded reason=no-adapter-line") != std::string::npos);
  }
  // ---- 8: a quarantine left in the PARENT's environment never reaches a child ---------------------
  {
    SetEnvironmentVariableW(L"REMOTE60_CAPTURE_DXGI_QUARANTINE", L"leaked-from-parent");
    reset_launches();
    set_launch(0, "", 60000, 0);
    const auto envs = run_supervisor(1);
    check("8: a value in the parent's environment is stripped from the child",
          envs.size() >= 1 && q_of(envs[0]) == "-", envs.empty() ? "" : envs[0]);
    SetEnvironmentVariableW(L"REMOTE60_CAPTURE_DXGI_QUARANTINE", nullptr);
  }

  LocalFree(mine);
  std::printf("\n%s  (%d checks, %d failed)\n", gFailures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILED", gChecks,
              gFailures);
  return gFailures == 0 ? 0 : 1;
}
