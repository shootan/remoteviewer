import os
p = r'C:\work\gnlink-wt\ka-pc\apps\native_poc\src\client_shell_main.cpp'
b = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in b
s = b.replace('\r\n', '\n')

def rep(a, c, cnt=1):
    global s
    assert s.count(a) == cnt, (s.count(a), a[:90])
    s = s.replace(a, c)

# ---------------------------------------------------------------- includes
rep('#include "viewer_restore_target.hpp"\n', '#include "viewer_restore_target.hpp"\n#include "client_managed_viewers.hpp"\n#include "client_recent_hosts.hpp"\n')
rep('#include <map>\n', '#include <map>\n#include <set>\n')

# ---------------------------------------------------------------- slot: a terminate handle for logout (Codex ③)
rep("""  // The begin_session that launched it. What an exit is matched against: a pid can come round
  // again between the exit and its handling, an operation number cannot. (RV-06)
  uint64_t operation = 0;""", """  // The begin_session that launched it. What an exit is matched against: a pid can come round
  // again between the exit and its handling, an operation number cannot. (RV-06)
  uint64_t operation = 0;
  // ka-pc-impl r1 (KA ③, Codex decision ③): signing out ends the viewers this shell started for that account.
  // Duplicated from the LAUNCH handle (never reopened by pid), used only by end_owner_children -- after the cancel
  // event had its chance and a bounded wait. Replacing a viewer still never kills it (see begin_session).
  HANDLE terminate = nullptr;""")
rep("""void close_slot_handles(ViewerCancelSlot& slot) {
  if (slot.process) CloseHandle(slot.process);
  slot.process = nullptr;""", """void close_slot_handles(ViewerCancelSlot& slot) {
  if (slot.process) CloseHandle(slot.process);
  slot.process = nullptr;
  if (slot.terminate) CloseHandle(slot.terminate);
  slot.terminate = nullptr;""")

# forward declarations + connected-operations set
rep("""constexpr UINT_PTR kReconnectTimer = 89;
""", """constexpr UINT_PTR kReconnectTimer = 89;
// ka-pc-impl r1: operations whose viewer reported a connected session ("connected host=" on its stdout) -- what
// makes an ordinary session count as a successful connect for the recent list and the list-return keep.
std::mutex gConnectedMu;
std::set<uint64_t> gConnectedOps;
void end_owner_children(const std::string& reason);
void keep_after_close(const struct ShellConnectRequest& request, uint64_t operation);
""")

# pump: note connected
rep("""      if (const auto restore = remote60::native_poc::viewer::ParseRestoreLine(line)) {""",
    """      if (line.rfind("[native-video-client] connected host=", 0) == 0) {
        std::lock_guard<std::mutex> lock(gConnectedMu);
        gConnectedOps.insert(operation);
      }
      if (const auto restore = remote60::native_poc::viewer::ParseRestoreLine(line)) {""")

# begin_login / auto login / signed-out: fence first, then the old owner's children
rep("""  { std::lock_guard<std::mutex> lock(gStateMu); epoch = ++gOwnerEpoch; gSessionToken.clear(); }
  log_upload_clear_credentials("new sign-in operation");""", """  { std::lock_guard<std::mutex> lock(gStateMu); epoch = ++gOwnerEpoch; gSessionToken.clear(); }
  end_owner_children("a new sign-in");  // ka-pc-impl r1: after the fence, never before
  log_upload_clear_credentials("new sign-in operation");""")
rep("""    if (!gSessionToken.empty()) return;
    epoch = ++gOwnerEpoch;
  }
  post_auto_login("checking", std::string(), std::string());""", """    if (!gSessionToken.empty()) return;
    epoch = ++gOwnerEpoch;
  }
  end_owner_children("a new sign-in");
  post_auto_login("checking", std::string(), std::string());""")
rep("""          gSessionToken.clear();
          ++gOwnerEpoch;
        }
        remote60::native_poc::log_upload_clear_credentials("the session ended");""", """          gSessionToken.clear();
          ++gOwnerEpoch;
        }
        end_owner_children("the session ended");
        remote60::native_poc::log_upload_clear_credentials("the session ended");""")

# handle_viewer_exit: the list-return keep, after a connected session the user closed
rep("""  } else {
    gReconnectAttempts = 0;
    post_status("idle", remaining > 0 ? std::to_string(remaining) + "개 연결이 계속 실행 중입니다." : "");
  }
}""", """  } else {
    gReconnectAttempts = 0;
    post_status("idle", remaining > 0 ? std::to_string(remaining) + "개 연결이 계속 실행 중입니다." : "");
    if (waited == WAIT_OBJECT_0 && exitCode == 0) keep_after_close(request, operation);
  }
  std::lock_guard<std::mutex> lock(gConnectedMu);
  gConnectedOps.erase(operation);
}""")

# begin_session: a terminate-capable duplicate for logout
rep("""  gViewerByHost[request.hostId] = ViewerCancelSlot{pi.dwProcessId, cancelEvent, liveness};
  gViewerByHost[request.hostId].operation = operation;""", """  gViewerByHost[request.hostId] = ViewerCancelSlot{pi.dwProcessId, cancelEvent, liveness};
  gViewerByHost[request.hostId].operation = operation;
  {
    HANDLE terminate = nullptr;
    if (DuplicateHandle(GetCurrentProcess(), pi.hProcess, GetCurrentProcess(), &terminate,
                        PROCESS_TERMINATE | SYNCHRONIZE, FALSE, 0)) {
      gViewerByHost[request.hostId].terminate = terminate;
    }
  }""")

# ---------------------------------------------------------------- the kept-PC section (after begin_session)
section = r'''
// ==================================================================================================================
// ka-pc-impl r1 (KA ③; proposal_ka_pc_r2.md, Codex codex_ka_pc_design_r1.md): kept PCs -- a managed standby viewer
// per PC (client_managed_viewers.hpp), at most three logical PCs (active + standby) per signed-in owner.
// UI thread only.
namespace kept {
// Two switches, named apart on purpose (task ka-pc-impl r1): the release keeps automatic preparation OFF -- nothing
// starts on its own at sign-in -- while a directory PC the user connected to and then closed goes to standby
// (kept model A). With the first off, a shipped build still leaves a standby viewer running after such a close.
constexpr bool kPrepareRecentOnSignIn = false;
constexpr bool kKeepOnListReturn = true;
constexpr size_t kMaxKept = 3;
}  // namespace kept

std::unique_ptr<remote60::native_poc::shell::ManagedViewers> gManaged;
uint64_t gTokenVersion = 1;
std::map<std::string, std::string> gHostNames;   // the last list's names, for the viewers' titles and logs
std::set<std::string> gKeepUnconfirmed;          // this owner: a PC whose Host did not confirm standby (no retry now)
int gStandbySupport = -1;                         // this owner's directory: -1 unknown, 0 no, 1 yes

std::string pc_origin() {
  std::string o = shell_directory_url();
  for (char& c : o) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  while (!o.empty() && o.back() == '/') o.pop_back();
  return o;
}
std::string pc_key(const std::string& account, const std::string& hostId) {
  return pc_origin() + "\n" + account + "\n" + hostId;
}
std::wstring shell_data_dir() {
  std::wstring p = settings_path();
  const size_t slash = p.find_last_of(L"\\/");
  return slash == std::wstring::npos ? L"." : p.substr(0, slash);
}
std::wstring recent_path() { return shell_data_dir() + L"\\recent_hosts.v1"; }

void recent_touch_host(const std::string& hostId) {
  std::string account;
  { std::lock_guard<std::mutex> lock(gStateMu); account = gAccountId; }
  if (account.empty()) return;
  using namespace remote60::native_poc::shell;
  const auto all = recent_touch(recent_load(recent_path()), pc_origin(), account, hostId);
  if (!recent_save(recent_path(), all)) log_line("recent hosts: could not be saved");
}

std::string json_escape_min(const std::string& v) {
  std::string out;
  for (char c : v) {
    if (c == '"' || c == '\\') out += '\\';
    if (static_cast<unsigned char>(c) < 0x20) continue;
    out += c;
  }
  return out;
}
void post_keep_state(const std::string& hostId, const std::string& state) {
  post_to_page("{\"type\":\"hostKeep\",\"hostId\":\"" + json_escape_min(hostId) + "\",\"state\":\"" +
               json_escape_min(state) + "\"}");
}

std::wstring managed_base_args(const std::string& hostId, const std::string& hostName) {
  ShellRuntimeSettings settings;
  { std::lock_guard<std::mutex> lock(gStateMu); settings = gSettings; }
  std::wstringstream a;
  a << L"--transport udp --codec h264 --enable-input-channel --initial-view targets"
    << L" --runtime-bitrate " << (settings.bitrateKbps * 1000u) << L" --runtime-fps " << settings.fps
    << L" --monitor " << settings.monitorId << L" --directory-url \"" << widen(shell_directory_url()) << L"\""
    << L" --directory-host-id \"" << widen(hostId) << L"\"";
  (void)hostName;
  return a.str();
}

// One PC to standby. false (+why): nothing new runs.
bool keep_launch(const std::string& hostId, std::string* why) {
  if (!gManaged) { *why = "no manager"; return false; }
  if (gManaged->Find(hostId)) return true;
  if (gKeepUnconfirmed.count(hostId)) { *why = "the Host did not confirm standby"; return false; }
  std::string account, token;
  uint64_t epoch = 0;
  {
    std::lock_guard<std::mutex> lock(gStateMu);
    account = gAccountId;
    token = gSessionToken;
    epoch = gOwnerEpoch;
  }
  if (token.empty() || account.empty()) { *why = "not signed in"; return false; }
  // At most three logical PCs, active + standby (Codex §1): make room by the least recent PC that is not in use.
  while (gManaged->size() >= kept::kMaxKept) {
    using namespace remote60::native_poc::shell;
    const std::vector<std::string> recent = recent_for(recent_load(recent_path()), pc_origin(), account);
    std::string victim;
    size_t victimRank = 0;
    for (const auto& id : gManaged->HostIds()) {
      const auto* c = gManaged->Find(id);
      if (!c || c->activeOp != 0 || c->state == "active") continue;  // a PC on the screen is never evicted
      size_t rank = recent.size();  // not in the recent list: the oldest
      for (size_t i = 0; i < recent.size(); ++i)
        if (recent[i] == id) rank = i;
      if (victim.empty() || rank > victimRank) { victim = id; victimRank = rank; }
    }
    if (victim.empty()) {
      *why = "three PCs are in use";
      post_status("error", "PC 3대가 모두 열려 있어 이 PC 는 유지하지 않습니다. 쓰지 않는 창을 닫아 주세요.");
      return false;
    }
    log_line("kept PCs: making room, releasing host=" + victim);
    gManaged->Release(victim, "evicted");
    post_keep_state(victim, "none");
  }
  remote60::native_poc::shell::ManagedLaunch spec;
  spec.exe = executable_dir() + L"\\GNLinkViewer.exe";
#ifdef REMOTE60_SHELL_TEST_SEAM
  if (!gShellTestManagedViewerExe.empty()) spec.exe = gShellTestManagedViewerExe;
#endif
  spec.workDir = executable_dir();
  const auto name = gHostNames.find(hostId);
  spec.hostName = name == gHostNames.end() ? hostId : name->second;
  spec.baseArgs = managed_base_args(hostId, spec.hostName);
  spec.url = shell_directory_url();
  spec.session = token;
  spec.tokenVersion = gTokenVersion;
  spec.hostId = hostId;
  spec.key = pc_key(account, hostId);
  spec.genDir = narrow(shell_data_dir() + L"\\standby");
  spec.ownerEpoch = epoch;
  if (!gManaged->Launch(spec, why)) return false;
  post_keep_state(hostId, "starting");
  return true;
}

// A connected ordinary session the user closed: the PC goes to standby (model A, the first conversion -- its new
// directory requests are this conversion's, and say so in the log). Not for a session that never connected.
void keep_after_close(const ShellConnectRequest& request, uint64_t operation) {
  bool connected = false;
  {
    std::lock_guard<std::mutex> lock(gConnectedMu);
    connected = gConnectedOps.count(operation) != 0;
  }
  if (!connected) return;
  recent_touch_host(request.hostId);
  if (!kept::kKeepOnListReturn || gStandbySupport != 1) return;
  std::string why;
  if (keep_launch(request.hostId, &why)) {
    log_line("kept PC: first conversion to standby host=" + request.hostId + " (new observe + standby connect)");
  } else {
    log_line("kept PC: not kept host=" + request.hostId + " why=" + why);
    if (why == "the Host did not confirm standby") post_keep_state(request.hostId, "unsupported");
  }
}

void on_managed_event(const remote60::native_poc::shell::ManagedEvent& e) {
  using remote60::native_poc::managed_ipc::Type;
  uint64_t epoch = 0;
  { std::lock_guard<std::mutex> lock(gStateMu); epoch = gOwnerEpoch; }
  auto* c = gManaged ? gManaged->Current(e, epoch) : nullptr;
  if (!c) return;  // an older child or an older owner: nothing it says reaches this UI
  if (e.exited) {
    log_line("managed viewer exited host=" + e.hostId + " code=" + std::to_string(e.exitCode) +
             (c->state == "unsupported" ? std::string(" (standby not confirmed)") : std::string()));
    const bool wasActive = c->activeOp != 0;
    const std::string last = c->state;
    gManaged->Forget(e.hostId);
    post_keep_state(e.hostId, last == "unsupported" ? "unsupported" : "none");
    if (wasActive) post_status("idle", "");
    return;
  }
  const auto& m = e.message;
  const std::string name = c->hostName;
  switch (m.type) {
    case Type::State: {
      const std::string state = m.get("state");
      if (state != c->state) log_line("kept PC host=" + e.hostId + " state=" + state + " lost=" + m.get("lost"));
      c->state = state;
      if (state == "unsupported" || state == "failed") {
        // Codex: an unanswered standby Hello is "not confirmed", not proof of an old Host -- no retry for this
        // owner's run, and a tap is an ordinary connect.
        gKeepUnconfirmed.insert(e.hostId);
        gManaged->Release(e.hostId, state);
        post_keep_state(e.hostId, state);
        return;
      }
      post_keep_state(e.hostId, state);
      return;
    }
    case Type::PickerReady:
      log_line("kept PC activation host=" + e.hostId + " op=" + m.get("op") + " picker-ready ms=" + m.get("ms"));
      recent_touch_host(e.hostId);  // a real reuse
      post_status("idle", name + " 연결됨");
      return;
    case Type::Presented:
      log_line("kept PC activation host=" + e.hostId + " op=" + m.get("op") + " presented ms=" + m.get("ms"));
      return;
    case Type::SessionEnded: {
      c->activeOp = 0;
      const std::string reason = m.get("reason");
      log_line("kept PC session ended host=" + e.hostId + " op=" + m.get("op") + " rc=" + m.get("rc") +
               " reason=" + reason + " connected=" + m.get("connected"));
      if (reason == "not-ready") {
        post_status("error", name + " 를 아직 준비하는 중입니다. 잠시 뒤 다시 눌러 주세요.");
      } else if (m.get("connected") != "1") {
        post_status("error", name + " 연결에 실패했습니다 (코드 " + m.get("rc") + ")");
      } else {
        post_status("idle", "");
      }
      return;
    }
    case Type::Bye:
      log_line("managed viewer says bye host=" + e.hostId + " reason=" + m.get("reason"));
      return;
    default:
      return;
  }
}

// Sign-out, a new sign-in, an ended session, the shell's exit. The caller has ALREADY moved the owner epoch on
// (Codex: fence first). The old owner's managed viewers are told to end now; their waits run on reaper threads.
// The ordinary viewers this shell started go too (Codex ③): the cancel event first (a connect in progress ends),
// then -- off the UI thread, after 3 s -- the launch handle's terminate.
void end_owner_children(const std::string& reason) {
  if (gManaged && gManaged->size()) {
    log_line("kept PCs: ending " + std::to_string(gManaged->size()) + " managed viewer(s): " + reason);
    for (const auto& id : gManaged->HostIds()) post_keep_state(id, "none");
    gManaged->EndAll(reason);
  }
  gKeepUnconfirmed.clear();
  gStandbySupport = -1;
  for (auto& entry : gViewerByHost) {
    ViewerCancelSlot& slot = entry.second;
    if (!slot.terminate) continue;
    (void)remote60::native_poc::viewer::viewer_request_cancel(slot.cancelEvent);
    HANDLE t = nullptr;
    if (!DuplicateHandle(GetCurrentProcess(), slot.terminate, GetCurrentProcess(), &t, 0, FALSE, DUPLICATE_SAME_ACCESS))
      continue;
    log_line("ending the viewer of host=" + entry.first + " pid=" + std::to_string(slot.pid) + ": " + reason);
    gWorkers.Launch([t] {
      if (WaitForSingleObject(t, 3000) != WAIT_OBJECT_0) TerminateProcess(t, 0);
      CloseHandle(t);
    });
  }
}

// After a list arrives for this owner: names for the viewers, the directory's standby support (read-only,
// /api/hosts metadata), and -- only with the automatic switch on -- the recent PCs to standby.
void kept_after_hosts(const std::vector<DirectoryHostEntry>& hosts) {
  for (const auto& h : hosts) gHostNames[h.hostId] = h.hostName;
  std::string token;
  uint64_t epoch = 0;
  {
    std::lock_guard<std::mutex> lock(gStateMu);
    token = gSessionToken;
    epoch = gOwnerEpoch;
  }
  if (token.empty()) return;
  gWorkers.Launch([token, epoch, hosts] {
    bool supported = false, relay = false;
    std::string error;
    const bool asked = remote60::native_poc::directory_standby_support(shell_directory_url(), token, &supported, &relay, &error);
    post_ui([asked, supported, relay, epoch, hosts] {
      if (!epoch_is(epoch)) return;
      gStandbySupport = asked && supported ? 1 : 0;
      log_line(std::string("kept PCs: directory standby ") + (gStandbySupport == 1 ? "supported" : "not supported") +
               (relay ? " (relay)" : ""));
      if (!kept::kPrepareRecentOnSignIn || gStandbySupport != 1) return;
      std::string account;
      { std::lock_guard<std::mutex> lock(gStateMu); account = gAccountId; }
      using namespace remote60::native_poc::shell;
      for (const auto& id : recent_for(recent_load(recent_path()), pc_origin(), account)) {
        bool online = false;
        for (const auto& h : hosts) online = online || (h.hostId == id && h.online);
        std::string why;
        if (online && !keep_launch(id, &why)) log_line("kept PC: not prepared host=" + id + " why=" + why);
      }
    });
  });
}
// ==================================================================================================================
'''
rep("""void handle_page_message(const std::string& json) {""", section + "\nvoid handle_page_message(const std::string& json) {")

# connect: a ready kept PC is activated; one still preparing says so (Codex ④)
rep("""  ShellConnectRequest connect{};
  if (shell_parse_connect(json, &connect)) {
    begin_session(connect);
  }
}""", """  ShellConnectRequest connect{};
  if (shell_parse_connect(json, &connect)) {
    if (gManaged) {
      if (const auto* c = gManaged->Find(connect.hostId)) {
        if (c->state == "ready") {
          uint64_t op = 0;
          if (gManaged->Activate(connect.hostId, &op)) {
            log_line("kept PC activation host=" + connect.hostId + " op=" + std::to_string(op));
            post_status("connecting", connect.hostName + " 창을 여는 중");
            return;
          }
        }
        if (c->state == "active" || c->activeOp != 0) {
          post_status("idle", connect.hostName + " 은(는) 이미 열려 있습니다.");
          return;
        }
        // Not ready yet: said, not pretended (Codex ④) -- and no ordinary connect beside the lease.
        post_status("error", connect.hostName + " 를 아직 준비하는 중입니다. 잠시 뒤 다시 눌러 주세요.");
        return;
      }
    }
    begin_session(connect);
  }
}""")

# logout: fence then end
rep("""      ++gOwnerEpoch;
    }
    remote60::native_poc::log_upload_clear_credentials("logged out");""", """      ++gOwnerEpoch;
    }
    end_owner_children("signed out");
    remote60::native_poc::log_upload_clear_credentials("logged out");""")

# WM_DESTROY
rep("""    case WM_DESTROY:
      { std::lock_guard<std::mutex> lock(gStateMu); ++gOwnerEpoch; gSessionToken.clear(); }""", """    case WM_DESTROY:
      { std::lock_guard<std::mutex> lock(gStateMu); ++gOwnerEpoch; gSessionToken.clear(); }
      end_owner_children("the client is closing");""")

# adopt_session: token version, names, support
rep("""  std::string message = shell_hosts_json(hosts);
  // The page shows which account it is listing, so it travels with the list.""", """  // ka-pc-impl r1: a renewed token reaches the kept PCs' viewers (newest version wins there), and the list feeds
  // the kept-PC bookkeeping.
  ++gTokenVersion;
  if (gManaged) gManaged->TokenRenewed(token, gTokenVersion);
  kept_after_hosts(hosts);

  std::string message = shell_hosts_json(hosts);
  // The page shows which account it is listing, so it travels with the list.""")

# wWinMain: create / destroy the manager
rep("""  if (!gWindow) return 2;
  ShowWindow(gWindow, SW_SHOW);""", """  if (!gWindow) return 2;
  ShowWindow(gWindow, SW_SHOW);
  gManaged = std::make_unique<remote60::native_poc::shell::ManagedViewers>(
      [](std::function<void()> f) { post_ui(std::move(f)); }, [](const std::string& line) { log_line(line); },
      [](HANDLE read, uint32_t pid, const std::string& hostId) {
        std::string account;
        { std::lock_guard<std::mutex> lock(gStateMu); account = gAccountId; }
        const std::string context = " viewerSession=managed-" + std::to_string(pid) + " viewerPid=" + std::to_string(pid) +
                                    " hostId=" + hostId + " productVersion=" + narrow(remote60::native_poc::kProductVersion);
        gWorkers.Launch([read, identity = account + "@" + shell_directory_url(), context] {
          pump_viewer_output_to_log(read, identity, context, 0);
        });
      },
      [](const remote60::native_poc::shell::ManagedEvent& e) { on_managed_event(e); });""")
rep("""  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  return 0;
}""", """  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  // ka-pc-impl r1: the kept PCs' viewers were told to end at WM_DESTROY; their bounded waits finish here.
  if (gManaged) {
    gManaged->JoinReapers();
    gManaged.reset();
  }
  return 0;
}""")
open(p, 'wb').write((s.replace('\n', '\r\n') if crlf else s).encode('utf-8'))
print('ok')
