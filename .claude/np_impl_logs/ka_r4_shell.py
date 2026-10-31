p = r'C:\work\gnlink-wt\ka-pc\apps\native_poc\src\client_shell_main.cpp'
b = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in b
s = b.replace('\r\n', '\n')

def rep(a, c, cnt=1):
    global s
    assert s.count(a) == cnt, (s.count(a), a[:100])
    s = s.replace(a, c)

# ---------------------------------------------------------------- the slot: a second event
rep('''struct ViewerCancelSlot {
  DWORD pid = 0;
  HANDLE cancelEvent = nullptr;   // owned here; closed when the viewer is forgotten''', '''struct ViewerCancelSlot {
  DWORD pid = 0;
  HANDLE cancelEvent = nullptr;   // owned here; closed when the viewer is forgotten
  // ka-pc-impl r4 (Codex decision 1): the owner-end event -- signalled only when this shell's owner ends the
  // viewer's session (sign-out, account change, the shell closing). The cancel event calls off a connect only.
  HANDLE ownerEndEvent = nullptr;''')
rep('''void close_slot_handles(ViewerCancelSlot& slot) {
  if (slot.process) CloseHandle(slot.process);''', '''void close_slot_handles(ViewerCancelSlot& slot) {
  if (slot.ownerEndEvent) CloseHandle(slot.ownerEndEvent);
  slot.ownerEndEvent = nullptr;
  if (slot.process) CloseHandle(slot.process);''')
rep('''std::map<std::string, ViewerCancelSlot> gViewerByHost;
''', '''std::map<std::string, ViewerCancelSlot> gViewerByHost;
// ka-pc-impl r4 (Codex decision 1): viewers this owner started whose slot a replacement took over. The cancel only
// calls off a CONNECT -- one that had already connected stays open -- so they are still this owner's: the owner's
// end reaches them, and their exit forgets them. UI thread only.
std::vector<ViewerCancelSlot> gReplacedViewers;
''')

# ---------------------------------------------------------------- R2: one per-op state for pump end and exit
rep('''// ka-pc-impl r3 (Codex R2): an exit whose output pump had not finished within its bound: a "connected" line that
// comes after it is not counted (the pump takes its op out again when it ends). Under gConnectedMu.
std::set<uint64_t> gEndedBeforePump;''', '''// ka-pc-impl r3/r4 (Codex R2): pump end and exit, decided under ONE lock (gConnectedMu), whichever comes first:
//  - the pump ends first: its op goes into gPumpEnded; the exit then finds it there, takes it out, and is decided
//    with every line counted.
//  - the exit's bounded wait runs out first: its op goes into gEndedBeforePump -- a "connected" line after that is
//    not counted -- and the pump, when it ends, takes it out (and does not leave a gPumpEnded entry behind).
// Either way both sets are empty for that op once both have happened.
std::set<uint64_t> gEndedBeforePump;
std::set<uint64_t> gPumpEnded;''')
rep('''  struct EndGuard {
    uint64_t op;
    ~EndGuard() {
      std::lock_guard<std::mutex> lock(gConnectedMu);
      gEndedBeforePump.erase(op);
    }
  } endGuard{operation};''', '''  struct EndGuard {
    uint64_t op;
    ~EndGuard() {
      if (op == 0) return;
      std::lock_guard<std::mutex> lock(gConnectedMu);
      if (gEndedBeforePump.erase(op) == 0) gPumpEnded.insert(op);  // the exit has not been decided yet
    }
  } endGuard{operation};''')
rep('''    // r3 (Codex R2): the output first -- a viewer that connected and was closed at once has its "connected" line
    // in the pipe. Bounded: past it, a late line is not counted (gEndedBeforePump).
    if (pumpDone && WaitForSingleObject(pumpDone.get(), 2000) != WAIT_OBJECT_0) {
      std::lock_guard<std::mutex> lock(gConnectedMu);
      gEndedBeforePump.insert(operation);
    }''', '''    // r3/r4 (Codex R2): the output first -- a viewer that connected and was closed at once has its "connected" line
    // in the pipe. Bounded; and decided under the same lock as the pump's end (see gPumpEnded).
    if (pumpDone) (void)WaitForSingleObject(pumpDone.get(), 2000);
    {
      std::lock_guard<std::mutex> lock(gConnectedMu);
      if (gPumpEnded.erase(operation) == 0) gEndedBeforePump.insert(operation);  // the pump is still reading
    }''')
# a launch without a pump: nothing to wait for and nothing to leave behind
rep('''  std::shared_ptr<void> pumpDone(CreateEventW(nullptr, TRUE, pipeOk ? FALSE : TRUE, nullptr),
                                 [](void* h) { if (h) CloseHandle(h); });''', '''  std::shared_ptr<void> pumpDone(CreateEventW(nullptr, TRUE, pipeOk ? FALSE : TRUE, nullptr),
                                 [](void* h) { if (h) CloseHandle(h); });
  if (!pipeOk) {
    std::lock_guard<std::mutex> lock(gConnectedMu);
    gPumpEnded.insert(operation);  // no pump: its "end" is now
  }''')

# ---------------------------------------------------------------- decision 1: the second event at launch
rep('''  HANDLE cancelEvent = remote60::native_poc::viewer::viewer_create_cancel_event();
''', '''  HANDLE cancelEvent = remote60::native_poc::viewer::viewer_create_cancel_event();
  // r4: the owner's end, apart from the connect's cancel (Codex decision 1). Same kind of handle, same whitelist.
  HANDLE ownerEndEvent = remote60::native_poc::viewer::viewer_create_cancel_event();
''')
rep('''  HANDLE inheritList[3] = {nullptr, nullptr, nullptr};''', '''  HANDLE inheritList[4] = {nullptr, nullptr, nullptr, nullptr};''')
rep('''        inheritList[0] = pipeWrite;
        inheritList[1] = nulIn;
        inheritList[2] = cancelEvent;
        // The size is the USED bytes, not the array's: a null third entry would be passed as a
        // handle to inherit and the launch fails with ERROR_INVALID_PARAMETER.
        const SIZE_T inheritBytes =
            sizeof(HANDLE) * (cancelEvent ? 3u : 2u);''', '''        SIZE_T used = 0;
        inheritList[used++] = pipeWrite;
        inheritList[used++] = nulIn;
        if (cancelEvent) inheritList[used++] = cancelEvent;
        if (ownerEndEvent) inheritList[used++] = ownerEndEvent;
        // The size is the USED bytes, not the array's: a null entry would be passed as a
        // handle to inherit and the launch fails with ERROR_INVALID_PARAMETER.
        const SIZE_T inheritBytes = sizeof(HANDLE) * used;''')
rep('''  if (cancelEvent && pipeOk) {
    command << L" --cancel-event "
            << remote60::native_poc::viewer::viewer_cancel_handle_arg(cancelEvent);
  }''', '''  if (cancelEvent && pipeOk) {
    command << L" --cancel-event "
            << remote60::native_poc::viewer::viewer_cancel_handle_arg(cancelEvent);
  }
  if (ownerEndEvent && pipeOk) {
    command << L" --owner-end-event "
            << remote60::native_poc::viewer::viewer_cancel_handle_arg(ownerEndEvent);
  }''')
rep('''    cancelEvent = remote60::native_poc::viewer::viewer_cancel_slot_handle(false, cancelEvent);
    post_status("error", "세션을 시작하지 못했습니다");
    return;
  }''', '''    cancelEvent = remote60::native_poc::viewer::viewer_cancel_slot_handle(false, cancelEvent);
    ownerEndEvent = remote60::native_poc::viewer::viewer_cancel_slot_handle(false, ownerEndEvent);
    post_host_status("error", "세션을 시작하지 못했습니다", request.hostId);
    return;
  }''')
rep('''  cancelEvent = remote60::native_poc::viewer::viewer_cancel_slot_handle(pipeOk, cancelEvent);''',
    '''  cancelEvent = remote60::native_poc::viewer::viewer_cancel_slot_handle(pipeOk, cancelEvent);
  ownerEndEvent = remote60::native_poc::viewer::viewer_cancel_slot_handle(pipeOk, ownerEndEvent);''')
rep('''  gViewerByHost[request.hostId] = ViewerCancelSlot{pi.dwProcessId, cancelEvent, liveness};
  gViewerByHost[request.hostId].operation = operation;''', '''  gViewerByHost[request.hostId] = ViewerCancelSlot{pi.dwProcessId, cancelEvent, nullptr, liveness};
  gViewerByHost[request.hostId].ownerEndEvent = ownerEndEvent;
  gViewerByHost[request.hostId].operation = operation;''')
rep('''  cancelEvent = nullptr;  // the map owns it now''', '''  cancelEvent = nullptr;  // the map owns it now
  ownerEndEvent = nullptr;''')
# the replacement: the old one is kept as this owner's, not forgotten
rep('''      if (previous->second.cancelEvent) CloseHandle(previous->second.cancelEvent);
      close_slot_handles(previous->second);
      gViewerByHost.erase(previous);
    }
  }''', '''      // r4 (Codex decision 1): the cancel only calls off a CONNECT. If the old viewer had in fact connected (its
      // line not yet here), it stays open -- and stays this owner's: kept apart, so the owner's end still reaches
      // it and its exit forgets it.
      gReplacedViewers.push_back(previous->second);
      gViewerByHost.erase(previous);
    }
  }''')

# handle_viewer_exit: a replaced viewer's exit forgets it; statuses about this PC are scoped
rep('''  bool currentForHost = false;
  {
    // By the operation that launched it, not by pid. (RV-06)
    (void)exitedPid;''', '''  bool currentForHost = false;
  for (auto r = gReplacedViewers.begin(); r != gReplacedViewers.end(); ++r) {
    if (r->operation == operation) {
      if (r->cancelEvent) CloseHandle(r->cancelEvent);
      close_slot_handles(*r);
      gReplacedViewers.erase(r);
      break;
    }
  }
  {
    // By the operation that launched it, not by pid. (RV-06)
    (void)exitedPid;''')
rep('''    if (!gReconnectRequest)
      post_status("idle", remaining > 0 ? std::to_string(remaining) + "개 연결이 계속 실행 중입니다." : "");
    return;
  }''', '''    if (!gReconnectRequest)
      post_host_status("idle", remaining > 0 ? std::to_string(remaining) + "개 연결이 계속 실행 중입니다." : "",
                       request.hostId);
    return;
  }''')
rep('''    post_status("reconnecting", "연결이 끊겨 다시 연결하는 중입니다.");''',
    '''    post_host_status("reconnecting", "연결이 끊겨 다시 연결하는 중입니다.", request.hostId);''')
rep('''    post_status("error", request.hostName + " 연결에 실패했습니다 (코드 " + std::to_string(exitCode) + ")");''',
    '''    post_host_status("error", request.hostName + " 연결에 실패했습니다 (코드 " + std::to_string(exitCode) + ")",
                     request.hostId);''')
rep('''    gReconnectAttempts = 0;
    post_status("idle", remaining > 0 ? std::to_string(remaining) + "개 연결이 계속 실행 중입니다." : "");
    if (keepThis) keep_after_close(request, /*statusIsMine=*/true);''', '''    gReconnectAttempts = 0;
    post_host_status("idle", remaining > 0 ? std::to_string(remaining) + "개 연결이 계속 실행 중입니다." : "",
                     request.hostId);
    if (keepThis) keep_after_close(request, /*statusIsMine=*/true);''')
rep('''  post_status("connecting", request.hostName + " 창을 여는 중");
}''', '''  post_host_status("connecting", request.hostName + " 창을 여는 중", request.hostId);
}''')

# end_owner_children: both events, and the replaced ones too
rep('''  std::map<std::string, ViewerCancelSlot> ending;
  ending.swap(gViewerByHost);''', '''  std::map<std::string, ViewerCancelSlot> ending;
  ending.swap(gViewerByHost);
  {
    size_t n = 0;
    for (auto& r : gReplacedViewers) ending.emplace("(replaced " + std::to_string(++n) + ") " + std::to_string(r.pid), r);
    gReplacedViewers.clear();
  }''')
rep('''    const bool asked = remote60::native_poc::viewer::viewer_request_cancel(slot.cancelEvent);''',
    '''    // r4: the owner's end (an established session: the fence) AND the connect's cancel (one still connecting)
    const bool ownerAsked = remote60::native_poc::viewer::viewer_request_cancel(slot.ownerEndEvent);
    const bool asked = remote60::native_poc::viewer::viewer_request_cancel(slot.cancelEvent) || ownerAsked;''')

# ---------------------------------------------------------------- R1: a reservation keeps its place
rep('''bool reserve_for_new_pc(const std::string& hostId, std::string* refusal) {
  if (gStagingHost == hostId) return true;                                  // its own reservation (a replacement)
  if (logical_pcs(hostId).size() < kept::kMaxKept) return true;             // room without anybody going
  if (!gStagingHost.empty()) { *refusal = "another PC is being opened"; return false; }''', '''bool reserve_for_new_pc(const std::string& hostId, std::string* refusal) {
  if (gStagingHost == hostId) return true;                                  // its own reservation (a replacement)
  // r4 (Codex R1): a reservation HOLDS its place until it succeeds or is given back -- even when the PC it would
  // have let go is released meanwhile. So it is counted as a PC here.
  const size_t occupied = logical_pcs(hostId).size() + (gStagingHost.empty() ? 0u : 1u);
  if (occupied < kept::kMaxKept) return true;                               // room without anybody going
  if (!gStagingHost.empty()) { *refusal = "another PC is being opened"; return false; }''')
rep('''void commit_reservation(const std::string& hostId) {
  if (gStagingHost != hostId) {
    settle_pc_count(hostId);
    return;
  }
  gStagingHost.clear();
  log_line("kept PCs: reservation of host=" + hostId + " committed (it connected)");
  settle_pc_count(hostId);
}''', '''void commit_reservation(const std::string& hostId) {
  if (gStagingHost == hostId) {
    gStagingHost.clear();
    log_line("kept PCs: reservation of host=" + hostId + " committed (it connected)");
  }
  settle_pc_count(hostId);
  // r4 (Codex R1): never confirmed as a fourth. If no standby could be let go, this one is ended by its owner (the
  // fence: an established session closes through its own teardown) and the user is told.
  if (logical_pcs().size() > kept::kMaxKept) {
    log_line("kept PCs: host=" + hostId + " would be a fourth PC with none standing by; it is closed");
    const auto o = gViewerByHost.find(hostId);
    if (o != gViewerByHost.end()) {
      (void)remote60::native_poc::viewer::viewer_request_cancel(o->second.ownerEndEvent);
      (void)remote60::native_poc::viewer::viewer_request_cancel(o->second.cancelEvent);
    } else if (gManaged && gManaged->Find(hostId)) {
      gManaged->Release(hostId, "a fourth PC");
      post_keep_state(hostId, "none");
    }
    post_host_status("error", "PC 3대가 이미 열려 있어 이 PC 를 닫았습니다. 쓰지 않는 창을 닫은 뒤 다시 눌러 주세요.", hostId);
  }
}''')
open(p, 'wb').write((s.replace('\n', '\r\n') if crlf else s).encode('utf-8'))
print('shell ok')
