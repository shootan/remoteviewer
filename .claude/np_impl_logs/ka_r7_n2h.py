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

edit('viewer_control_state.hpp', [(
'''  uint64_t taken = 0, acked = 0, failed = 0, skipped = 0;      // input exchanges: started / InputAck / not / not sent
  uint64_t taken0 = 0, acked0 = 0, failed0 = 0, skipped0 = 0;  // as they stood when the fence was raised''',
'''  // input exchanges: started / ended in the host's InputAck / failed (a write or reply that went wrong, a refused
  // release) / not sent by the logon-screen gate / not started because the fence refused it (r7, Codex N2)
  uint64_t taken = 0, acked = 0, failed = 0, skipped = 0, cancelled = 0;
  uint64_t taken0 = 0, acked0 = 0, failed0 = 0, skipped0 = 0, cancelled0 = 0;  // as they stood when the fence was raised
  // r7 (Codex N2): why the latest write was refused -- the gated link records it, the caller of a failed exchange takes
  // it: refused by the fence before any byte of that message (not started), refused in the middle of a message
  // (broken), refused by the test build's release hook. None: no refusal -- a failure was the transport's own.
  enum class Refusal : uint8_t { None, FenceBeforeAnyByte, FenceMidMessage, TestHook };
  Refusal lastRefusal = Refusal::None;''')])

edit('viewer_owner_fence.hpp', [
('''  g.failed0 = g.failed;
  g.skipped0 = g.skipped;
}''', '''  g.failed0 = g.failed;
  g.skipped0 = g.skipped;
  g.cancelled0 = g.cancelled;
}'''),
('''enum class InputOutcome { Acked, Failed, Skipped };
// The end of an admitted input exchange (control thread).
inline void gate_end_input(OwnerFenceGate& g, InputOutcome o) {
  std::lock_guard<std::mutex> lock(g.mu);
  g.inFlight = false;
  g.releaseExchange = false;
  if (o == InputOutcome::Acked) ++g.acked;
  else if (o == InputOutcome::Failed) ++g.failed;
  else ++g.skipped;
}''', '''enum class InputOutcome { Acked, Failed, Skipped, Cancelled };
// The end of an admitted input exchange (control thread).
inline void gate_end_input(OwnerFenceGate& g, InputOutcome o) {
  std::lock_guard<std::mutex> lock(g.mu);
  g.inFlight = false;
  g.releaseExchange = false;
  if (o == InputOutcome::Acked) ++g.acked;
  else if (o == InputOutcome::Failed) ++g.failed;
  else if (o == InputOutcome::Skipped) ++g.skipped;
  else ++g.cancelled;
}

// r7 (Codex N2): why an exchange failed, one type for every caller (input, clipboard, IME, thumbnail, any action):
//  - NotStarted: the fence refused its write before any byte of it -- a cancellation by policy (nothing was sent; the
//    link is intact, the drain of releases goes on);
//  - Broken: refused in the middle of a message -- the stream is no longer whole;
//  - HookRefused: the test build refused a release write (a failed release, as a refused write would be);
//  - Transport: no refusal at all -- the write, the reply or the protocol itself failed.
enum class ExchangeFailure { NotStarted, Broken, HookRefused, Transport };
inline ExchangeFailure gate_take_failure(OwnerFenceGate& g) {
  std::lock_guard<std::mutex> lock(g.mu);
  const OwnerFenceGate::Refusal r = g.lastRefusal;
  g.lastRefusal = OwnerFenceGate::Refusal::None;
  switch (r) {
    case OwnerFenceGate::Refusal::FenceBeforeAnyByte: return ExchangeFailure::NotStarted;
    case OwnerFenceGate::Refusal::FenceMidMessage: return ExchangeFailure::Broken;
    case OwnerFenceGate::Refusal::TestHook: return ExchangeFailure::HookRefused;
    default: return ExchangeFailure::Transport;
  }
}
// The outcome of an admitted input exchange that did not end in an InputAck.
inline InputOutcome outcome_of_failure(ExchangeFailure f) {
  return f == ExchangeFailure::NotStarted ? InputOutcome::Cancelled : InputOutcome::Failed;
}'''),
('''// May this control-channel write start now? (FenceGatedLink, under the lock)
inline bool gate_write_allowed(OwnerFenceGate& g) {
  std::lock_guard<std::mutex> lock(g.mu);
  if (!g.fenced) return true;
  if (!g.releaseExchange) return false;''', '''// May this control-channel write start now? (FenceGatedLink, under the lock.) `bytesOfMessageSoFar`: how much of the
// current message the link already wrote -- a refusal then is recorded as before-any-byte or mid-message (r7, N2).
inline bool gate_write_allowed(OwnerFenceGate& g, size_t bytesOfMessageSoFar = 0) {
  std::lock_guard<std::mutex> lock(g.mu);
  if (bytesOfMessageSoFar == 0) g.lastRefusal = OwnerFenceGate::Refusal::None;  // a new message: a fresh record
  if (!g.fenced) return true;
  if (!g.releaseExchange) {
    g.lastRefusal = bytesOfMessageSoFar == 0 ? OwnerFenceGate::Refusal::FenceBeforeAnyByte
                                             : OwnerFenceGate::Refusal::FenceMidMessage;
    return false;
  }'''),
('''    if (armed) {
      armed = false;
      return false;
    }''', '''    if (armed) {
      armed = false;
      g.lastRefusal = OwnerFenceGate::Refusal::TestHook;
      return false;
    }'''),
('''enum class FenceVerdict { Pending, Confirmed, Failed };
// The verdict, all of it read in ONE critical section with the takes and ends above.
inline FenceVerdict fence_verdict(const OwnerFenceGate& g, const ClientInputQueue& queue) {
  std::lock_guard<std::mutex> lock(g.mu);
  if (g.failed > g.failed0) return FenceVerdict::Failed;
  if (!queue.Empty() || g.inFlight) return FenceVerdict::Pending;
  return (g.acked - g.acked0) + (g.skipped - g.skipped0) >= (g.taken - g.taken0) ? FenceVerdict::Confirmed
                                                                                : FenceVerdict::Pending;
}''', '''enum class FenceVerdict { Pending, Confirmed, Failed };
// What the verdict saw, for the log (r7: an InputAck is said apart from what was deliberately not sent).
struct FenceTally {
  uint64_t taken = 0, acked = 0, failed = 0, skipped = 0, cancelled = 0;
};
// The verdict, all of it read in ONE critical section with the takes and ends above. Confirmed: nothing queued, nothing
// in flight, and every input exchange since the fence ended in the host's InputAck, or was deliberately not sent
// (skipped by the logon-screen gate, or a non-release the fence cancelled before any byte of it). Failed: one did fail.
inline FenceVerdict fence_verdict(const OwnerFenceGate& g, const ClientInputQueue& queue, FenceTally* tally = nullptr) {
  std::lock_guard<std::mutex> lock(g.mu);
  if (tally) {
    *tally = FenceTally{g.taken - g.taken0, g.acked - g.acked0, g.failed - g.failed0, g.skipped - g.skipped0,
                        g.cancelled - g.cancelled0};
  }
  if (g.failed > g.failed0) return FenceVerdict::Failed;
  if (!queue.Empty() || g.inFlight) return FenceVerdict::Pending;
  return (g.acked - g.acked0) + (g.skipped - g.skipped0) + (g.cancelled - g.cancelled0) >= (g.taken - g.taken0)
             ? FenceVerdict::Confirmed
             : FenceVerdict::Pending;
}'''),
('''  bool Write(const void* data, size_t len) override {
    if (!gate_write_allowed(gate_)) return false;  // never started
    return *inner_ && (*inner_)->Write(data, len);
  }
  bool EndMessage() override { return *inner_ && (*inner_)->EndMessage(); }''', '''  bool Write(const void* data, size_t len) override {
    if (!gate_write_allowed(gate_, messageBytes_)) return false;  // never started (the reason is in the gate)
    const bool ok = *inner_ && (*inner_)->Write(data, len);
    if (ok) messageBytes_ += len;
    return ok;
  }
  bool EndMessage() override {
    messageBytes_ = 0;  // the next Write begins a new message
    return *inner_ && (*inner_)->EndMessage();
  }'''),
('''  std::unique_ptr<ControlLink>* inner_;
  OwnerFenceGate& gate_;
};''', '''  std::unique_ptr<ControlLink>* inner_;
  OwnerFenceGate& gate_;
  size_t messageBytes_ = 0;  // of the message being written (control thread only)
};'''),
])
print('ok')
