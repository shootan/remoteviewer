p = r'C:\work\gnlink-wt\ka-pc\apps\native_poc\src\client_shell_main.cpp'
b = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in b
s = b.replace('\r\n', '\n')

def rep(a, c, cnt=1):
    global s
    assert s.count(a) == cnt, (s.count(a), a[:100])
    s = s.replace(a, c)

# bring a viewer's window forward (decision 1), defined beside the slot map
rep('''/** Closes every handle a slot owns. */''', '''// ka-pc-impl r3 (Codex decision 1): the open window of a viewer this shell started, brought to the front. The
// shell has the foreground (its card was just pressed), so it may hand it on.
void bring_viewer_forward(DWORD pid) {
  struct Find {
    DWORD pid;
    HWND hwnd;
  } f{pid, nullptr};
  EnumWindows(
      [](HWND h, LPARAM p) -> BOOL {
        auto* f = reinterpret_cast<Find*>(p);
        DWORD owner = 0;
        GetWindowThreadProcessId(h, &owner);
        wchar_t cls[64] = {};
        GetClassNameW(h, cls, 64);
        if (owner == f->pid && wcscmp(cls, L"Remote60NativeVideoClient") == 0 && IsWindowVisible(h)) {
          f->hwnd = h;
          return FALSE;
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&f));
  if (!f.hwnd) return;
  AllowSetForegroundWindow(pid);
  if (IsIconic(f.hwnd)) ShowWindow(f.hwnd, SW_RESTORE);
  SetForegroundWindow(f.hwnd);
}

/** Closes every handle a slot owns. */''')

# logical_pcs: the reserved candidate is left out of BOTH sets
rep('''std::set<std::string> logical_pcs(const std::string& except = {}) {
  std::set<std::string> out;
  for (const auto& entry : gViewerByHost) out.insert(entry.first);
  if (gManaged)
    for (const auto& id : gManaged->HostIds())
      if (id != gStagingHost) out.insert(id);
  out.erase(except);
  return out;
}''', '''std::set<std::string> logical_pcs(const std::string& except = {}) {
  std::set<std::string> out;
  for (const auto& entry : gViewerByHost) out.insert(entry.first);
  if (gManaged)
    for (const auto& id : gManaged->HostIds()) out.insert(id);
  out.erase(gStagingHost);  // r3 (Codex R1): the reserved candidate -- ordinary or kept -- is not one of the three
  out.erase(except);
  return out;
}''')

rep('''// May a PC that is not one of the logical PCs start now? Yes while there are fewer than three, or while one of the
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
}''', '''// May a PC that is not one of the logical PCs start now? Yes while there are fewer than three, or while one of the
// three is a standby that can be let go once it succeeds.
bool room_for_new_pc(const std::string& hostId) {
  if (logical_pcs(hostId).size() < kept::kMaxKept) return true;
  return !standby_victim(hostId).empty();
}

// ka-pc-impl r3 (Codex R1): THE reservation of a candidate beside three PCs -- one at a time, for every kind of start
// (an ordinary connect by hand, an automatic reconnect, a kept PC's preparation). Taken before the start; nobody is
// let go until the candidate succeeds (settle_pc_count then judges again); released when it fails, is called off,
// ends without connecting, or its owner changes. true with nothing reserved when there is room anyway.
bool reserve_for_new_pc(const std::string& hostId, std::string* refusal) {
  if (gStagingHost == hostId) return true;                                  // its own reservation (a replacement)
  if (logical_pcs(hostId).size() < kept::kMaxKept) return true;             // room without anybody going
  if (!gStagingHost.empty()) { *refusal = "another PC is being opened"; return false; }
  if (standby_victim(hostId).empty()) { *refusal = "three PCs are in use"; return false; }
  gStagingHost = hostId;
  log_line("kept PCs: host=" + hostId + " reserved beside three PCs; one standby goes only when it has connected");
  return true;
}
void release_reservation(const std::string& hostId, const char* why) {
  if (gStagingHost.empty() || gStagingHost != hostId) return;
  gStagingHost.clear();
  log_line(std::string("kept PCs: reservation of host=") + hostId + " released (" + why + ")");
}
// The reserved candidate succeeded: it becomes one of the PCs, and the PCs are counted again NOW (a standby the user
// opened meanwhile is not let go -- settle_pc_count only ever lets a standing-by PC go).
void commit_reservation(const std::string& hostId) {
  if (gStagingHost != hostId) {
    settle_pc_count(hostId);
    return;
  }
  gStagingHost.clear();
  log_line("kept PCs: reservation of host=" + hostId + " committed (it connected)");
  settle_pc_count(hostId);
}

// An ordinary viewer's session connected ("connected host=", pumped on another thread). The real use: the recent
// order moves now -- not when its window closes -- and the PC count is settled. Only for this PC's current viewer,
// and only for the owner that started it (Codex R2: a late notice of a signed-out owner touches nothing).
void on_ordinary_connected(uint64_t operation, const std::string& hostId, uint64_t ownerEpoch) {
  { std::lock_guard<std::mutex> lock(gStateMu); if (ownerEpoch != gOwnerEpoch) return; }
  const auto it = gViewerByHost.find(hostId);
  if (it == gViewerByHost.end() || it->second.operation != operation) return;
  recent_touch_host(hostId);
  commit_reservation(hostId);
}''')

# keep_launch uses the same reservation
rep('''  // At most three logical PCs (Codex N2). Beside three others this one is STAGED: it runs, but it is not counted and
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
  }''', '''  // At most three logical PCs (Codex N2) -- through the ONE reservation every start uses (Codex R1): beside three
  // others this one is reserved, it runs but is not counted, and nobody is let go until it is ready.
  const bool hadReservation = gStagingHost == hostId;
  {
    std::string refusal;
    if (!reserve_for_new_pc(hostId, &refusal)) {
      *why = refusal;
      if (statusIsMine && refusal == "three PCs are in use")
        post_host_status("error", "PC 3대가 모두 열려 있어 이 PC 는 대기로 두지 않습니다. 쓰지 않는 창을 닫아 주세요.", hostId);
      return false;
    }
  }''')
rep('''  if (!gManaged->Launch(spec, why)) return false;
  if (staged) {
    gStagingHost = hostId;
    log_line("kept PCs: host=" + hostId + " staged beside three PCs; one standby goes only when it is ready");
  }
  post_keep_state(hostId, "starting");''', '''  if (!gManaged->Launch(spec, why)) {
    if (!hadReservation) release_reservation(hostId, "launch failed");
    return false;
  }
  post_keep_state(hostId, "starting");''')

# release_kept_by_user: host-scoped status, its reservation released
rep('''  gKeepSuppressed.insert(hostId);
  if (gStagingHost == hostId) gStagingHost.clear();''', '''  gKeepSuppressed.insert(hostId);
  release_reservation(hostId, "released by the user");''')
rep('''  post_status("idle", name + " 연결을 해제했습니다.");
}''', '''  post_host_status("idle", name + " 연결을 해제했습니다.", hostId);
}''')

# on_managed_event: reservation through the helpers; every status scoped to its PC
rep('''    if (gStagingHost == e.hostId) gStagingHost.clear();
    gManaged->Forget(e.hostId);
    post_keep_state(e.hostId, last == "unsupported" ? "unsupported" : "none");
    if (wasActive) post_status("idle", "");''', '''    release_reservation(e.hostId, "its viewer ended");
    gManaged->Forget(e.hostId);
    post_keep_state(e.hostId, last == "unsupported" ? "unsupported" : "none");
    if (wasActive) post_host_status("idle", "", e.hostId);''')
rep('''        if (gStagingHost == e.hostId) gStagingHost.clear();  // a staged candidate that failed: the three stay''',
    '''        release_reservation(e.hostId, "not confirmed");  // a reserved candidate that failed: the three stay''')
rep('''      if (state == "ready" && gStagingHost == e.hostId) {
        gStagingHost.clear();  // it succeeded: now it is one of the PCs, and the least recent standby goes
        settle_pc_count(e.hostId);
      }''', '''      if (state == "ready" && gStagingHost == e.hostId) commit_reservation(e.hostId);  // it succeeded''')
rep('''      post_keep_state(e.hostId, "active");
      post_status("idle", name + " 연결됨");''', '''      post_keep_state(e.hostId, "active");
      post_host_status("idle", name + " 연결됨", e.hostId);''')
rep('''      if (reason == "not-ready") {
        post_status("error", name + " 를 아직 준비하는 중입니다. 잠시 뒤 다시 눌러 주세요.");
      } else if (m.get("connected") != "1") {
        // The lease's socket may have been taken and the handshake still failed (rc 6): a failed connect, said so.
        post_status("error", name + " 연결에 실패했습니다 (코드 " + m.get("rc") + ")");
      } else {
        post_status("idle", "");
      }''', '''      if (reason == "not-ready") {
        post_host_status("error", name + " 를 아직 준비하는 중입니다. 잠시 뒤 다시 눌러 주세요.", e.hostId);
      } else if (m.get("connected") != "1") {
        // The lease's socket may have been taken and the handshake still failed (rc 6): a failed connect, said so.
        post_host_status("error", name + " 연결에 실패했습니다 (코드 " + m.get("rc") + ")", e.hostId);
      } else {
        post_host_status("idle", "", e.hostId);
      }''')

# end_owner_children: the old owner's ordinary slots leave the registry at once (their teardown is owned apart)
rep('''  gKeepUnconfirmed.clear();
  gKeepSuppressed.clear();
  gStagingHost.clear();
  gStandbySupport = -1;
  for (auto& entry : gViewerByHost) {
    ViewerCancelSlot& slot = entry.second;''', '''  gKeepUnconfirmed.clear();
  gKeepSuppressed.clear();
  gStagingHost.clear();
  gStandbySupport = -1;
  // r3 (Codex R2): the old owner's ordinary viewers leave the registry NOW -- they no longer count among this
  // shell's PCs, a new owner's start cannot find them as "already open", and a late notice of theirs matches no slot.
  // Their ending (the fence, then the bounded fallback) is owned by the workers below, with handles of their own.
  std::map<std::string, ViewerCancelSlot> ending;
  ending.swap(gViewerByHost);
  struct CloseSlots {
    std::map<std::string, ViewerCancelSlot>* m;
    ~CloseSlots() {
      for (auto& entry : *m) {
        if (entry.second.cancelEvent) CloseHandle(entry.second.cancelEvent);
        close_slot_handles(entry.second);
      }
    }
  } closeSlots{&ending};
  for (auto& entry : ending) {
    ViewerCancelSlot& slot = entry.second;''')

# the managed activation that would use the last standby while a candidate is reserved
rep('''        if (c->state == "ready" || c->state == "returning") {
          uint64_t op = 0;
          if (gManaged->Activate(connect.hostId, &op)) {''', '''        if ((c->state == "ready" || c->state == "returning") && !gStagingHost.empty() &&
            gStagingHost != connect.hostId && standby_victim(connect.hostId).empty()) {
          // r3 (Codex R1): a PC is reserved beside three, and this standby is the last one that could make room for
          // it -- opening it now would leave four on screen. Refused while the other PC is being opened.
          log_line("kept PC activation host=" + connect.hostId + " refused: host=" + gStagingHost +
                   " is reserved and this is the last standby");
          post_host_status("error", "다른 PC 를 여는 중입니다. 그 PC 가 열린 뒤 다시 눌러 주세요.", connect.hostId);
          return;
        }
        if (c->state == "ready" || c->state == "returning") {
          uint64_t op = 0;
          if (gManaged->Activate(connect.hostId, &op)) {''')
rep('''            post_status("error", connect.hostName + " 을(를) 여는 요청을 보내지 못했습니다. 잠시 뒤 다시 눌러 주세요.");''',
    '''            post_host_status("error", connect.hostName + " 을(를) 여는 요청을 보내지 못했습니다. 잠시 뒤 다시 눌러 주세요.",
                             connect.hostId);''')
rep('''        if (c->state == "active" || c->activeOp != 0) {
          post_status("idle", connect.hostName + " 은(는) 이미 열려 있습니다.");''', '''        if (c->state == "active" || c->activeOp != 0) {
          post_host_status("idle", connect.hostName + " 은(는) 이미 열려 있습니다.", connect.hostId);''')
rep('''        post_status("error", connect.hostName + " 를 아직 준비하는 중입니다. 잠시 뒤 다시 눌러 주세요.");
        return;''', '''        post_host_status("error", connect.hostName + " 를 아직 준비하는 중입니다. 잠시 뒤 다시 눌러 주세요.", connect.hostId);
        return;''')
open(p, 'wb').write((s.replace('\n', '\r\n') if crlf else s).encode('utf-8'))
print('ok part 2')
