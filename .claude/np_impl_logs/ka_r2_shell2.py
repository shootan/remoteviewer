p = r'C:\work\gnlink-wt\ka-pc\apps\native_poc\src\client_shell_main.cpp'
b = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in b
s = b.replace('\r\n', '\n')

def rep(a, c):
    global s
    assert s.count(a) == 1, (s.count(a), a[:100])
    s = s.replace(a, c)

rep('''void on_ordinary_connected(uint64_t operation, const std::string& hostId);''',
'''void on_ordinary_connected(uint64_t operation, const std::string& hostId);
bool room_for_new_pc(const std::string& hostId);''')

i = s.index('// One PC to standby. false (+why): nothing new runs.\n')
j = s.index('// After a list arrives for this owner: names for the viewers')
new = r'''// ka-pc-impl r2 (Codex N2): the logical PCs of this owner -- every PC with a live ordinary viewer and every kept PC
// (a managed viewer), counted once per host, the one candidate being staged left out (it is not one of the three
// until it is ready). `except`: the PC being decided about.
std::set<std::string> logical_pcs(const std::string& except = {}) {
  std::set<std::string> out;
  for (const auto& entry : gViewerByHost) out.insert(entry.first);
  if (gManaged)
    for (const auto& id : gManaged->HostIds())
      if (id != gStagingHost) out.insert(id);
  out.erase(except);
  return out;
}

// On screen (or being opened): an ordinary viewer, or a kept PC with an activation. Never let go to make room.
bool pc_visible(const std::string& hostId) {
  if (gViewerByHost.count(hostId)) return true;
  const auto* c = gManaged ? gManaged->Find(hostId) : nullptr;
  return c && (c->activeOp != 0 || c->state == "active");
}

// The kept PC to let go for room: standing by (not on screen, not the staged candidate, not `except`), the least
// recently USED one (the recent list: real connects and reuses only). Empty: every PC is on screen.
std::string standby_victim(const std::string& except) {
  if (!gManaged) return {};
  std::string account;
  { std::lock_guard<std::mutex> lock(gStateMu); account = gAccountId; }
  using namespace remote60::native_poc::shell;
  const std::vector<std::string> recent = recent_for(recent_load(recent_path()), pc_origin(), account);
  std::string victim;
  size_t victimRank = 0;
  for (const auto& id : gManaged->HostIds()) {
    if (id == except || id == gStagingHost || pc_visible(id)) continue;
    size_t rank = recent.size();  // not in the recent list: the oldest
    for (size_t i = 0; i < recent.size(); ++i)
      if (recent[i] == id) rank = i;
    if (victim.empty() || rank > victimRank) { victim = id; victimRank = rank; }
  }
  return victim;
}

// Lets standby PCs go, least recent first, until at most three logical PCs remain. Only ever after a new PC has
// SUCCEEDED (an ordinary connect, a staged candidate ready).
void settle_pc_count(const std::string& keep) {
  while (logical_pcs().size() > kept::kMaxKept) {
    const std::string victim = standby_victim(keep);
    if (victim.empty()) return;  // all on screen: nothing to let go (the start was refused if it came to that)
    log_line("kept PCs: making room after host=" + keep + " succeeded, releasing host=" + victim);
    gManaged->Release(victim, "evicted");
    post_keep_state(victim, "none");
  }
}

// May a PC that is not one of the logical PCs start now? Yes while there are fewer than three, or while one of the
// three is a standby that can be let go once it succeeds.
bool room_for_new_pc(const std::string& hostId) {
  if (logical_pcs(hostId).size() < kept::kMaxKept) return true;
  return !standby_victim(hostId).empty();
}

// An ordinary viewer's session connected ("connected host=", pumped on another thread). The real use: the recent
// order moves now -- not when its window closes -- and the PC count is settled. Only for this PC's current viewer.
void on_ordinary_connected(uint64_t operation, const std::string& hostId) {
  const auto it = gViewerByHost.find(hostId);
  if (it == gViewerByHost.end() || it->second.operation != operation) return;
  recent_touch_host(hostId);
  settle_pc_count(hostId);
}

// One PC to standby. false (+why): nothing new runs. `automatic`: the list-return keep or sign-in preparation (the
// user's release and the not-confirmed budget hold it back); `statusIsMine`: this caller owns the status line.
bool keep_launch(const std::string& hostId, bool automatic, bool statusIsMine, std::string* why) {
  if (!gManaged) { *why = "no manager"; return false; }
  if (gManaged->Find(hostId)) return true;
  if (automatic && gKeepSuppressed.count(hostId)) { *why = "released by the user"; return false; }
  if (automatic) {
    const auto u = gKeepUnconfirmed.find(hostId);
    if (u != gKeepUnconfirmed.end() && (u->second.times >= 2 || GetTickCount64() < u->second.retryAtMs)) {
      *why = "the Host did not confirm standby";
      return false;
    }
  }
  std::string account, token;
  uint64_t epoch = 0;
  {
    std::lock_guard<std::mutex> lock(gStateMu);
    account = gAccountId;
    token = gSessionToken;
    epoch = gOwnerEpoch;
  }
  if (token.empty() || account.empty()) { *why = "not signed in"; return false; }
  // At most three logical PCs (Codex N2). Beside three others this one is STAGED: it runs, but it is not counted and
  // nobody is let go until it is ready (on_managed_event); if it fails, the three stay as they were. One at a time.
  bool staged = false;
  if (logical_pcs(hostId).size() >= kept::kMaxKept) {
    if (!gStagingHost.empty()) { *why = "another PC is being prepared"; return false; }
    if (standby_victim(hostId).empty()) {
      *why = "three PCs are in use";
      if (statusIsMine)
        post_status("error", "PC 3대가 모두 열려 있어 이 PC 는 대기로 두지 않습니다. 쓰지 않는 창을 닫아 주세요.");
      return false;
    }
    staged = true;
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
  if (staged) {
    gStagingHost = hostId;
    log_line("kept PCs: host=" + hostId + " staged beside three PCs; one standby goes only when it is ready");
  }
  post_keep_state(hostId, "starting");
  return true;
}

// A connected ordinary session the user closed: the PC goes to standby (model A, the first conversion -- its new
// directory requests are this conversion's, and say so in the log). The caller has checked it was this PC's own,
// connected, normally closed session (handle_viewer_exit). The recent order is NOT moved here (the connect did).
void keep_after_close(const ShellConnectRequest& request, bool statusIsMine) {
  if (!kept::kKeepOnListReturn || gStandbySupport != 1) return;
  std::string why;
  if (keep_launch(request.hostId, /*automatic=*/true, statusIsMine, &why)) {
    log_line("kept PC: first conversion to standby host=" + request.hostId + " (new observe + standby connect)");
  } else {
    log_line("kept PC: not kept host=" + request.hostId + " why=" + why);
    if (why == "the Host did not confirm standby") post_keep_state(request.hostId, "unsupported");
  }
}

// The user's "연결 해제" on a kept PC (Codex N3): its viewer gets Release (its lease goes; an open session closes
// through the owner fence), it is not kept again automatically until the user connects to it, and whatever it
// still sends is dropped (Current() no longer finds it). Other PCs are not touched.
void release_kept_by_user(const std::string& hostId) {
  gKeepSuppressed.insert(hostId);
  if (gStagingHost == hostId) gStagingHost.clear();
  const auto* c = gManaged ? gManaged->Find(hostId) : nullptr;
  if (!c) {
    log_line("kept PC: release asked for host=" + hostId + ", nothing kept (suppressed from now on)");
    post_keep_state(hostId, "none");
    return;
  }
  const std::string name = c->hostName;
  log_line("kept PC: released by the user host=" + hostId + " state=" + c->state +
           " active=" + (c->activeOp != 0 ? "1" : "0"));
  gManaged->Release(hostId, "user");
  post_keep_state(hostId, "none");
  post_status("idle", name + " 연결을 해제했습니다.");
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
    if (gStagingHost == e.hostId) gStagingHost.clear();
    gManaged->Forget(e.hostId);
    post_keep_state(e.hostId, last == "unsupported" ? "unsupported" : "none");
    if (wasActive) post_status("idle", "");
    return;
  }
  const auto& m = e.message;
  const std::string name = c->hostName;
  // ka-pc-impl r2 (Codex N6): an activation's messages are this activation's -- the op the shell sent -- or nothing.
  const bool activationMessage = m.type == Type::PickerReady || m.type == Type::Presented || m.type == Type::SessionEnded;
  if (activationMessage && (c->activeOp == 0 || m.num("op") != c->activeOp)) {
    log_line("kept PC host=" + e.hostId + ": a message of op=" + m.get("op") + " dropped (current op=" +
             std::to_string(c->activeOp) + ")");
    return;
  }
  switch (m.type) {
    case Type::State: {
      const std::string state = m.get("state");
      if (state != c->state) log_line("kept PC host=" + e.hostId + " state=" + state + " lost=" + m.get("lost"));
      c->state = state;
      if (state == "unsupported" || state == "failed") {
        // Codex: an unanswered standby Hello is "not confirmed", not proof of an old Host. The automatic budget
        // (gKeepUnconfirmed) counts it; a tap is an ordinary connect, and that connect clears the budget (D2).
        {
          KeepUnconfirmed& u = gKeepUnconfirmed[e.hostId];
          ++u.times;
          u.retryAtMs = GetTickCount64() + 60000;
        }
        if (gStagingHost == e.hostId) gStagingHost.clear();  // a staged candidate that failed: the three stay
        gManaged->Release(e.hostId, state);
        post_keep_state(e.hostId, state);
        return;
      }
      if (state == "ready" && gStagingHost == e.hostId) {
        gStagingHost.clear();  // it succeeded: now it is one of the PCs, and the least recent standby goes
        settle_pc_count(e.hostId);
      }
      if (c->activeOp == 0) post_keep_state(e.hostId, state);
      return;
    }
    case Type::PickerReady:
      log_line("kept PC activation host=" + e.hostId + " op=" + m.get("op") + " picker-ready ms=" + m.get("ms"));
      recent_touch_host(e.hostId);  // a real reuse
      post_keep_state(e.hostId, "active");
      post_status("idle", name + " 연결됨");
      return;
    case Type::Presented:
      log_line("kept PC activation host=" + e.hostId + " op=" + m.get("op") + " presented ms=" + m.get("ms"));
      return;
    case Type::SessionEnded: {
      c->activeOp = 0;
      const std::string reason = m.get("reason");
      log_line("kept PC session ended host=" + e.hostId + " op=" + m.get("op") + " rc=" + m.get("rc") +
               " reason=" + reason + " connected=" + m.get("connected") + " taken=" + m.get("taken"));
      if (reason == "not-ready") {
        post_status("error", name + " 를 아직 준비하는 중입니다. 잠시 뒤 다시 눌러 주세요.");
      } else if (m.get("connected") != "1") {
        // The lease's socket may have been taken and the handshake still failed (rc 6): a failed connect, said so.
        post_status("error", name + " 연결에 실패했습니다 (코드 " + m.get("rc") + ")");
      } else {
        post_status("idle", "");
      }
      post_keep_state(e.hostId, c->state == "active" ? "returning" : c->state);
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
// (Codex: fence first). The old owner's managed viewers are told to end now (Shutdown: the owner fence, then their
// own teardown); their bounded waits run on reaper threads.
// ka-pc-impl r2 (Codex N4): the ordinary viewers this shell started go the same way -- the cancel event it
// launched them with is, once a session is established, the owner fence (input and the clipboard cut at once, then
// the window's WM_CLOSE teardown). Only a viewer still there after kOrdinaryEndMs is ended with the launch handle:
// a bounded fallback, off the UI thread. Never by pid, never a viewer this shell did not start.
constexpr DWORD kOrdinaryEndMs = 8000;  // a teardown's own joins are bounded at 3 s + 3 s
void end_owner_children(const std::string& reason) {
  if (gManaged && gManaged->size()) {
    log_line("kept PCs: ending " + std::to_string(gManaged->size()) + " managed viewer(s): " + reason);
    for (const auto& id : gManaged->HostIds()) post_keep_state(id, "none");
    gManaged->EndAll(reason);
  }
  gKeepUnconfirmed.clear();
  gKeepSuppressed.clear();
  gStagingHost.clear();
  gStandbySupport = -1;
  for (auto& entry : gViewerByHost) {
    ViewerCancelSlot& slot = entry.second;
    const bool asked = remote60::native_poc::viewer::viewer_request_cancel(slot.cancelEvent);
    HANDLE t = nullptr;
    if (!slot.terminate ||
        !DuplicateHandle(GetCurrentProcess(), slot.terminate, GetCurrentProcess(), &t, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
      log_line("ending the viewer of host=" + entry.first + " pid=" + std::to_string(slot.pid) + ": " + reason +
               " (asked=" + (asked ? "1" : "0") + ", no fallback handle)");
      continue;
    }
    log_line("ending the viewer of host=" + entry.first + " pid=" + std::to_string(slot.pid) + ": " + reason +
             " (asked=" + (asked ? "1" : "0") + "; ended by its handle only if still there in " +
             std::to_string(kOrdinaryEndMs) + " ms)");
    gWorkers.Launch([t, host = entry.first] {
      if (WaitForSingleObject(t, kOrdinaryEndMs) != WAIT_OBJECT_0) {
        TerminateProcess(t, 0);
        post_ui([host] { log_line("the viewer of host=" + host + " did not close in time; it was ended by its handle"); });
      }
      CloseHandle(t);
    });
  }
}

'''
s = s[:i] + new + s[j:]

# the automatic preparation at sign-in: automatic
rep('''        if (online && !keep_launch(id, &why)) log_line("kept PC: not prepared host=" + id + " why=" + why);''',
    '''        if (online && !keep_launch(id, /*automatic=*/true, /*statusIsMine=*/false, &why))
          log_line("kept PC: not prepared host=" + id + " why=" + why);''')

# the page: connect (explicit) and releaseKept
rep('''  ShellConnectRequest connect{};
  if (shell_parse_connect(json, &connect)) {
    if (gManaged) {
      if (const auto* c = gManaged->Find(connect.hostId)) {
        if (c->state == "ready" || c->state == "returning") {
          uint64_t op = 0;
          if (gManaged->Activate(connect.hostId, &op)) {
            log_line("kept PC activation host=" + connect.hostId + " op=" + std::to_string(op));
            post_status("connecting", connect.hostName + " 창을 여는 중");
            return;
          }
        }''', '''  if (type == "releaseKept") {
    std::string hostId;
    json_profile::json_get_string(json, "hostId", &hostId);
    if (!hostId.empty()) release_kept_by_user(hostId);
    return;
  }
  ShellConnectRequest connect{};
  if (shell_parse_connect(json, &connect)) {
    // ka-pc-impl r2 (Codex N3/D2): the user's own connect to this PC lifts what held its AUTOMATIC keep back -- a
    // release by the user, an unconfirmed standby -- so the next list return tries again (once, finitely).
    gKeepSuppressed.erase(connect.hostId);
    gKeepUnconfirmed.erase(connect.hostId);
    if (gManaged) {
      if (const auto* c = gManaged->Find(connect.hostId)) {
        if (c->state == "ready" || c->state == "returning") {
          uint64_t op = 0;
          if (gManaged->Activate(connect.hostId, &op)) {
            log_line("kept PC activation host=" + connect.hostId + " op=" + std::to_string(op));
            post_keep_state(connect.hostId, "connecting");
            post_status("connecting", connect.hostName + " 창을 여는 중");
            return;
          }
          if (c->activeOp == 0 && !c->ending) {
            // Activate could not be sent (the channel to that viewer is gone): said, and nothing is pretended.
            log_line("kept PC activation host=" + connect.hostId + " could not be sent");
            post_status("error", connect.hostName + " 을(를) 여는 요청을 보내지 못했습니다. 잠시 뒤 다시 눌러 주세요.");
            return;
          }
        }''')
open(p, 'wb').write((s.replace('\n', '\r\n') if crlf else s).encode('utf-8'))
print('ok part 2')
