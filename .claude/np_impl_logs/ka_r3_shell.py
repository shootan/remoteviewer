p = r'C:\work\gnlink-wt\ka-pc\apps\native_poc\src\client_shell_main.cpp'
b = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in b
s = b.replace('\r\n', '\n')

def rep(a, c):
    global s
    assert s.count(a) == 1, (s.count(a), a[:100])
    s = s.replace(a, c)

# ---------------------------------------------------------------- R4: a status can belong to one PC
rep('''void post_status(const std::string& state, const std::string& detail);
void post_ui(std::function<void()> action);''', '''void post_status(const std::string& state, const std::string& detail);
// ka-pc-impl r3 (Codex R4): a status about ONE PC. The page shows its text, but a PC's idle/error does not end the
// "connecting" mark of ANOTHER card (only a status with no PC, or about the card being connected, does).
void post_host_status(const std::string& state, const std::string& detail, const std::string& hostId);
void post_ui(std::function<void()> action);''')
rep('''void post_status(const std::string& state, const std::string& detail) {
  post_to_page(shell_status_json(state, detail));
}''', '''void post_status(const std::string& state, const std::string& detail) {
  post_to_page(shell_status_json(state, detail));
}
void post_host_status(const std::string& state, const std::string& detail, const std::string& hostId) {
  std::string json = shell_status_json(state, detail);
  std::string id;
  for (char c : hostId)
    if (c != '"' && c != '\\\\' && static_cast<unsigned char>(c) >= 0x20) id += c;
  json.insert(json.size() - 1, ",\\"hostId\\":\\"" + id + "\\"");
  post_to_page(json);
}''')

# ---------------------------------------------------------------- R2: the connect notice is bound to its owner
rep('''void on_ordinary_connected(uint64_t operation, const std::string& hostId);
bool room_for_new_pc(const std::string& hostId);''', '''void on_ordinary_connected(uint64_t operation, const std::string& hostId, uint64_t ownerEpoch);
// ka-pc-impl r3 (Codex R1): the one reservation for a candidate beside three PCs (see the kept-PC section).
bool reserve_for_new_pc(const std::string& hostId, std::string* refusal);
void release_reservation(const std::string& hostId, const char* why);
// ka-pc-impl r3 (Codex R2): an exit whose output pump had not finished within its bound: a "connected" line that
// comes after it is not counted (the pump takes its op out again when it ends). Under gConnectedMu.
std::set<uint64_t> gEndedBeforePump;''')
rep('''void pump_viewer_output_to_log(HANDLE readEnd, const std::string& identity, const std::string& context,
                               uint64_t operation, const std::string& hostId = {}) {''',
'''void pump_viewer_output_to_log(HANDLE readEnd, const std::string& identity, const std::string& context,
                               uint64_t operation, const std::string& hostId = {}, uint64_t ownerEpoch = 0) {''')
rep('''      if (operation != 0 && line.rfind("[native-video-client] connected host=", 0) == 0) {
        {
          std::lock_guard<std::mutex> lock(gConnectedMu);
          gConnectedOps.insert(operation);
        }
        // ka-pc-impl r2 (Codex N2): a real connect is when the recent order moves and when the PC count is settled.
        if (!hostId.empty()) post_ui([operation, hostId] { on_ordinary_connected(operation, hostId); });
      }''', '''      if (operation != 0 && line.rfind("[native-video-client] connected host=", 0) == 0) {
        bool counted = false;
        {
          std::lock_guard<std::mutex> lock(gConnectedMu);
          // r3 (Codex R2): an exit already decided without this line is not reopened by it
          if (!gEndedBeforePump.count(operation)) {
            gConnectedOps.insert(operation);
            counted = true;
          }
        }
        // ka-pc-impl r2/r3 (Codex N2, R2): a real connect is when the recent order moves and when the PC count is
        // settled -- for the owner that started this viewer only.
        if (counted && !hostId.empty())
          post_ui([operation, hostId, ownerEpoch] { on_ordinary_connected(operation, hostId, ownerEpoch); });
      }''')
# the pump's end: its op leaves the guard set
rep('''void pump_viewer_output_to_log(HANDLE readEnd, const std::string& identity, const std::string& context,
                               uint64_t operation, const std::string& hostId = {}, uint64_t ownerEpoch = 0) {
  std::string pending;''', '''void pump_viewer_output_to_log(HANDLE readEnd, const std::string& identity, const std::string& context,
                               uint64_t operation, const std::string& hostId = {}, uint64_t ownerEpoch = 0) {
  struct EndGuard {
    uint64_t op;
    ~EndGuard() {
      std::lock_guard<std::mutex> lock(gConnectedMu);
      gEndedBeforePump.erase(op);
    }
  } endGuard{operation};
  std::string pending;''')

# begin_session: the pump knows its owner and says when it is done; the exit waits for it (bounded)
rep('''    gWorkers.Launch([pipeRead, identity = account + "@" + server, context, operation, hostId = request.hostId] {
      pump_viewer_output_to_log(pipeRead, identity, context, operation, hostId);
    });''', '''    gWorkers.Launch([pipeRead, identity = account + "@" + server, context, operation, hostId = request.hostId,
                     ownerEpoch, pumpDone] {
      pump_viewer_output_to_log(pipeRead, identity, context, operation, hostId, ownerEpoch);
      SetEvent(pumpDone.get());
    });''')
rep('''  if (!pipeOk) log_line("session output not captured (no pipe); viewer log will be empty");''',
'''  if (!pipeOk) log_line("session output not captured (no pipe); viewer log will be empty");
  // ka-pc-impl r3 (Codex R2): set when this viewer's output pump has read its last line. The exit below waits for it
  // (bounded, on its worker) so a "connected" line still in the pipe is counted before the exit is decided.
  std::shared_ptr<void> pumpDone(CreateEventW(nullptr, TRUE, pipeOk ? FALSE : TRUE, nullptr),
                                 [](void* h) { if (h) CloseHandle(h); });''')
rep('''  gWorkers.Launch([handle = pi.hProcess, request, operation, ownerEpoch,
                   pid = pi.dwProcessId, startedMs = GetTickCount64()]() {
    DWORD waited = WAIT_TIMEOUT;
    while (!gWorkers.Stopping() && (waited = WaitForSingleObject(handle, 250)) == WAIT_TIMEOUT) {}
    if (gWorkers.Stopping()) { CloseHandle(handle); return; }
    DWORD exitCode = 0;
    GetExitCodeProcess(handle, &exitCode);
    CloseHandle(handle);''', '''  gWorkers.Launch([handle = pi.hProcess, request, operation, ownerEpoch,
                   pid = pi.dwProcessId, startedMs = GetTickCount64(), pumpDone]() {
    DWORD waited = WAIT_TIMEOUT;
    while (!gWorkers.Stopping() && (waited = WaitForSingleObject(handle, 250)) == WAIT_TIMEOUT) {}
    if (gWorkers.Stopping()) { CloseHandle(handle); return; }
    DWORD exitCode = 0;
    GetExitCodeProcess(handle, &exitCode);
    CloseHandle(handle);
    // r3 (Codex R2): the output first -- a viewer that connected and was closed at once has its "connected" line
    // in the pipe. Bounded: past it, a late line is not counted (gEndedBeforePump).
    if (pumpDone && WaitForSingleObject(pumpDone.get(), 2000) != WAIT_OBJECT_0) {
      std::lock_guard<std::mutex> lock(gConnectedMu);
      gEndedBeforePump.insert(operation);
    }''')

# R1 + decision 1 + decision 2 in begin_session
rep('''  // ka-pc-impl r2 (Codex N2): one count of logical PCs -- ordinary viewers and kept ones together, at most three.
  // A new PC beside three others starts only if one of those is a standby that can be let go -- and that happens
  // only once this one has connected (on_ordinary_connected). Three PCs all on screen: refused, nothing evicted.
  if (!room_for_new_pc(request.hostId)) {
    log_line("kept PCs: refused host=" + request.hostId + ": three PCs are open");
    post_status("error", "PC 3대가 모두 열려 있어 새 PC 를 열지 않았습니다. 쓰지 않는 창을 닫아 주세요.");
    return;
  }
''', '''  // ka-pc-impl r3 (Codex decision 1): a PC whose ordinary session is OPEN (connected) is not replaced by a plain
  // press of its card -- its window is brought forward and the user is told it is open. Replacing applies only to an
  // attempt still connecting. (A viewer that has connected but whose "connected" line has not reached this shell
  // yet still counts as connecting -- the remaining edge.)
  if (!automatic) {
    const auto open = gViewerByHost.find(request.hostId);
    bool connectedNow = false;
    if (open != gViewerByHost.end()) {
      std::lock_guard<std::mutex> lock(gConnectedMu);
      connectedNow = gConnectedOps.count(open->second.operation) != 0;
    }
    if (connectedNow) {
      bring_viewer_forward(open->second.pid);
      log_line("connect: host=" + request.hostId + " is open (pid=" + std::to_string(open->second.pid) +
               "): shown, not replaced");
      post_host_status("idle", request.hostName + " 은(는) 이미 열려 있습니다.", request.hostId);
      return;
    }
  }
  // ka-pc-impl r2/r3 (Codex N2, R1, decision 2): one count of logical PCs -- ordinary viewers and kept ones, at most
  // three -- and ONE reservation for a candidate beside three: taken here, before the launch, by an ordinary start
  // (by hand or an automatic reconnect) exactly as by a kept PC's preparation. Released if the start fails or the
  // viewer ends without connecting; committed (one standby let go) only when it has connected.
  {
    std::string refusal;
    if (!reserve_for_new_pc(request.hostId, &refusal)) {
      log_line("kept PCs: refused host=" + request.hostId + " (" + (automatic ? "reconnect" : "connect") + "): " + refusal);
      if (automatic) {
        // decision 2: the reconnect is not made, said so, and not retried against the limit
        KillTimer(gWindow, kReconnectTimer);
        gReconnectRequest.reset();
        gReconnectRestore.clear();
        gReconnectAttempts = 0;
        log_line("viewer reconnect=refused reason=three-pcs host=" + request.hostId);
        post_host_status("error", request.hostName + " 의 연결이 끊겼지만, PC 3대가 열려 있어 다시 연결하지 않았습니다. "
                                  "다른 창을 닫은 뒤 이 PC 를 다시 눌러 주세요.", request.hostId);
      } else if (refusal == "another PC is being opened") {
        post_host_status("error", "다른 PC 를 여는 중입니다. 그 PC 가 열린 뒤 다시 눌러 주세요.", request.hostId);
      } else {
        post_host_status("error", "PC 3대가 모두 열려 있어 새 PC 를 열지 않았습니다. 쓰지 않는 창을 닫아 주세요.",
                         request.hostId);
      }
      return;
    }
  }
''')
# a failed launch releases the reservation
rep('''    log_line("session launch failed err=" + std::to_string(launchErr));''', '''    log_line("session launch failed err=" + std::to_string(launchErr));
    release_reservation(request.hostId, "launch failed");''')

# handle_viewer_exit: a staged candidate that ended releases its reservation (any path)
rep('''  const uint32_t remaining = gActiveViewers.load();
  // dxgi-wedge-fix r1: this operation's last confirmed + rendered target, whatever happens next -- taken''',
'''  // r3 (Codex R1): a candidate that ends before its connect committed releases the reservation (cancelled, failed)
  if (currentForHost) release_reservation(request.hostId, connected ? "ended after connecting" : "ended without connecting");
  const uint32_t remaining = gActiveViewers.load();
  // dxgi-wedge-fix r1: this operation's last confirmed + rendered target, whatever happens next -- taken''')

open(p, 'wb').write((s.replace('\n', '\r\n') if crlf else s).encode('utf-8'))
print('ok part 1')
