p = r'C:\work\gnlink-wt\ka-pc\apps\native_poc\src\client_shell_main.cpp'
b = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in b
s = b.replace('\r\n', '\n')

def rep(a, c):
    global s
    assert s.count(a) == 1, (s.count(a), a[:100])
    s = s.replace(a, c)

# ---------------------------------------------------------------- globals
rep('''void end_owner_children(const std::string& reason);
void keep_after_close(const ShellConnectRequest& request, uint64_t operation);''',
'''void end_owner_children(const std::string& reason);
void keep_after_close(const ShellConnectRequest& request, bool statusIsMine);
void on_ordinary_connected(uint64_t operation, const std::string& hostId);''')
rep('''// This owner: a PC whose Host did not confirm standby -- "not confirmed", not "an old Host" (Codex): one more try
// is allowed 60 s later (e.g. a Host that ignored a too-quick re-admission, ka-pc-impl r1 FINDING), then none.
struct KeepUnconfirmed {
  int times = 0;
  uint64_t retryAtMs = 0;
};
std::map<std::string, KeepUnconfirmed> gKeepUnconfirmed;''',
'''// This owner: a PC whose Host did not confirm standby -- "not confirmed", not "an old Host" (Codex).
// ka-pc-impl r2 (Codex D2): the budget of the AUTOMATIC keep only -- a further automatic try is allowed once, and only
// when the next automatic request comes 60 s or more later (there is no timer: nothing retries by itself). A user's
// explicit connect to that PC clears it (a new, finite attempt), so it never blocks the user for the owner's run.
struct KeepUnconfirmed {
  int times = 0;
  uint64_t retryAtMs = 0;
};
std::map<std::string, KeepUnconfirmed> gKeepUnconfirmed;
// ka-pc-impl r2 (Codex N3): PCs the user released ("연결 해제") -- never kept again automatically (list return,
// sign-in preparation) until the user connects to that PC. This owner only.
std::set<std::string> gKeepSuppressed;
// ka-pc-impl r2 (Codex N2): at most one candidate is being prepared beyond the three logical PCs: it does not count
// until it is ready, and only then is the least recent standby PC let go (a candidate that fails costs nobody).
std::string gStagingHost;''')

# ---------------------------------------------------------------- pump: the ordinary connect, posted to the UI
rep('''void pump_viewer_output_to_log(HANDLE readEnd, const std::string& identity, const std::string& context,
                               uint64_t operation) {''',
'''void pump_viewer_output_to_log(HANDLE readEnd, const std::string& identity, const std::string& context,
                               uint64_t operation, const std::string& hostId = {}) {''')
rep('''      if (line.rfind("[native-video-client] connected host=", 0) == 0) {
        std::lock_guard<std::mutex> lock(gConnectedMu);
        gConnectedOps.insert(operation);
      }''', '''      if (operation != 0 && line.rfind("[native-video-client] connected host=", 0) == 0) {
        {
          std::lock_guard<std::mutex> lock(gConnectedMu);
          gConnectedOps.insert(operation);
        }
        // ka-pc-impl r2 (Codex N2): a real connect is when the recent order moves and when the PC count is settled.
        if (!hostId.empty()) post_ui([operation, hostId] { on_ordinary_connected(operation, hostId); });
      }''')
rep('''    gWorkers.Launch([pipeRead, identity = account + "@" + server, context, operation] {
      pump_viewer_output_to_log(pipeRead, identity, context, operation);
    });''', '''    gWorkers.Launch([pipeRead, identity = account + "@" + server, context, operation, hostId = request.hostId] {
      pump_viewer_output_to_log(pipeRead, identity, context, operation, hostId);
    });''')

# ---------------------------------------------------------------- N1: per PC, not "the newest operation"
rep('''void handle_viewer_exit(const ShellConnectRequest& request, uint64_t operation, uint64_t ownerEpoch,
                        DWORD waited, DWORD exitCode, uint64_t ranMs, DWORD exitedPid) {
  // Forgotten only if it is still the one recorded. A replacement has already overwritten this
  // entry, and erasing it here would forget the viewer that is actually open.
  {
    // By the operation that launched it, not by pid. (RV-06)
    (void)exitedPid;
    const auto it = gViewerByHost.find(request.hostId);
    if (it != gViewerByHost.end() && it->second.operation == operation) {
      if (it->second.cancelEvent) CloseHandle(it->second.cancelEvent);
      close_slot_handles(it->second);
      gViewerByHost.erase(it);
    }
  }''', '''void handle_viewer_exit(const ShellConnectRequest& request, uint64_t operation, uint64_t ownerEpoch,
                        DWORD waited, DWORD exitCode, uint64_t ranMs, DWORD exitedPid) {
  // ka-pc-impl r2 (Codex N1): this operation's "connected" mark goes on EVERY way out (an owner change and a stale
  // operation used to return before it was cleared).
  bool connected = false;
  {
    std::lock_guard<std::mutex> lock(gConnectedMu);
    connected = gConnectedOps.erase(operation) != 0;
  }
  // Forgotten only if it is still the one recorded. A replacement has already overwritten this
  // entry, and erasing it here would forget the viewer that is actually open.
  bool currentForHost = false;
  {
    // By the operation that launched it, not by pid. (RV-06)
    (void)exitedPid;
    const auto it = gViewerByHost.find(request.hostId);
    if (it != gViewerByHost.end() && it->second.operation == operation) {
      currentForHost = true;
      if (it->second.cancelEvent) CloseHandle(it->second.cancelEvent);
      close_slot_handles(it->second);
      gViewerByHost.erase(it);
    }
  }''')
rep('''  { std::lock_guard<std::mutex> lock(gStateMu); if (ownerEpoch != gOwnerEpoch) return; }
  if (operation != gViewerOperation) {''', '''  { std::lock_guard<std::mutex> lock(gStateMu); if (ownerEpoch != gOwnerEpoch) return; }
  // ka-pc-impl r2 (Codex N1): the list-return keep is decided PER PC -- this owner, this PC's own session (not one
  // a newer viewer of the same PC replaced), connected, closed normally. Whether it is the newest operation overall
  // only decides who owns the shell's status line (A, B opened, A closed: A is still kept).
  const bool keepThis = currentForHost && connected && waited == WAIT_OBJECT_0 && exitCode == 0;
  if (operation != gViewerOperation) {
    if (keepThis) keep_after_close(request, /*statusIsMine=*/false);''')
rep('''    if (waited == WAIT_OBJECT_0 && exitCode == 0) keep_after_close(request, operation);
  }
  std::lock_guard<std::mutex> lock(gConnectedMu);
  gConnectedOps.erase(operation);
}''', '''    if (keepThis) keep_after_close(request, /*statusIsMine=*/true);
  }
}''')

# ---------------------------------------------------------------- N2: begin_session refuses a 4th visible PC
rep('''  // Call off the viewer already open on this PC, if there is one. It may be mid-connect -- up to''',
'''  // ka-pc-impl r2 (Codex N2): one count of logical PCs -- ordinary viewers and kept ones together, at most three.
  // A new PC beside three others starts only if one of those is a standby that can be let go -- and that happens
  // only once this one has connected (on_ordinary_connected). Three PCs all on screen: refused, nothing evicted.
  if (!room_for_new_pc(request.hostId)) {
    log_line("kept PCs: refused host=" + request.hostId + ": three PCs are open");
    post_status("error", "PC 3대가 모두 열려 있어 새 PC 를 열지 않았습니다. 쓰지 않는 창을 닫아 주세요.");
    return;
  }

  // Call off the viewer already open on this PC, if there is one. It may be mid-connect -- up to''')

open(p, 'wb').write((s.replace('\n', '\r\n') if crlf else s).encode('utf-8'))
print('ok part 1')
