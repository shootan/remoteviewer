import os
base = r'C:\work\gnlink-wt\ka-pc\apps\native_poc\src' + os.sep

def edit(name, pairs):
    p = base + name
    b = open(p, 'rb').read().decode('utf-8')
    crlf = '\r\n' in b
    s = b.replace('\r\n', '\n')
    for a, c in pairs:
        assert s.count(a) == 1, (name, s.count(a), a[:90])
        s = s.replace(a, c)
    open(p, 'wb').write((s.replace('\n', '\r\n') if crlf else s).encode('utf-8'))

# R2: the two halves as functions (same lock, same sets)
edit('client_shell_main.cpp', [(
'''std::set<uint64_t> gEndedBeforePump;
std::set<uint64_t> gPumpEnded;''', '''std::set<uint64_t> gEndedBeforePump;
std::set<uint64_t> gPumpEnded;
// The pump has read its last line (its op's output is complete).
void note_pump_end(uint64_t op) {
  if (op == 0) return;
  std::lock_guard<std::mutex> lock(gConnectedMu);
  if (gEndedBeforePump.erase(op) == 0) gPumpEnded.insert(op);  // the exit has not been decided yet
}
// The exit is being decided (after its bounded wait for the pump).
void note_exit_decided(uint64_t op) {
  std::lock_guard<std::mutex> lock(gConnectedMu);
  if (gPumpEnded.erase(op) == 0) gEndedBeforePump.insert(op);  // the pump is still reading
}'''), (
'''  struct EndGuard {
    uint64_t op;
    ~EndGuard() {
      if (op == 0) return;
      std::lock_guard<std::mutex> lock(gConnectedMu);
      if (gEndedBeforePump.erase(op) == 0) gPumpEnded.insert(op);  // the exit has not been decided yet
    }
  } endGuard{operation};''', '''  struct EndGuard {
    uint64_t op;
    ~EndGuard() { note_pump_end(op); }
  } endGuard{operation};'''), (
'''    if (pumpDone) (void)WaitForSingleObject(pumpDone.get(), 2000);
    {
      std::lock_guard<std::mutex> lock(gConnectedMu);
      if (gPumpEnded.erase(operation) == 0) gEndedBeforePump.insert(operation);  // the pump is still reading
    }''', '''    if (pumpDone) (void)WaitForSingleObject(pumpDone.get(), 2000);
    note_exit_decided(operation);'''), (
'''  if (!pipeOk) {
    std::lock_guard<std::mutex> lock(gConnectedMu);
    gPumpEnded.insert(operation);  // no pump: its "end" is now
  }''', '''  if (!pipeOk) note_pump_end(operation);  // no pump: its "end" is now''')])

# R3: fence time readable by the control thread; a TEST-BUILD-ONLY hold of the sender after the fence
edit('viewer_session_state.hpp', [(
'''  uint64_t ownerFenceAtMs = 0;  // r4: when the fence was raised (GetTickCount64), for its drain budget''',
'''  // r4: when the fence was raised (GetTickCount64), for its drain budget; read by the control thread in test builds
  std::atomic<uint64_t> ownerFenceAtMs{0};''')])
edit('viewer_window_proc.cpp', [(
'''        const uint64_t waited = GetTickCount64() - ctx.session.ownerFenceAtMs;''',
'''        const uint64_t waited = GetTickCount64() - ctx.session.ownerFenceAtMs.load();''')])
edit('viewer_control_client.cpp', [(
'''    if (ctx.control.scheduler.NextAction(
            nowUs, capture_client_control_metrics_snapshot(ctx), &ctx.picker.windowPanel,''', '''#if defined(REMOTE60_VIEWER_KA_TEST_SEAM)
    // TEST BUILD ONLY (GNLinkViewerKaTest, ka-pc-impl r4, Codex R3): REMOTE60_VIEWER_TEST_HOLD_SEND_AFTER_FENCE_MS=N
    // holds every outbound control action for N ms after the owner fence -- a sender that is slow at exactly the
    // wrong moment -- so a test can show the fence waits for its releases instead of closing over them.
    {
      static const uint64_t holdMs = [] {
        const char* v = std::getenv("REMOTE60_VIEWER_TEST_HOLD_SEND_AFTER_FENCE_MS");
        return v ? std::strtoull(v, nullptr, 10) : 0ull;
      }();
      const uint64_t fencedAt = ctx.session.ownerFenceAtMs.load();
      if (holdMs && fencedAt && GetTickCount64() - fencedAt < holdMs) {
        Sleep(5);
        continue;
      }
    }
#endif
    if (ctx.control.scheduler.NextAction(
            nowUs, capture_client_control_metrics_snapshot(ctx), &ctx.picker.windowPanel,''')])
print('ok')
