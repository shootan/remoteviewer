#pragma once

// ka-pc-impl r5/r6 (Codex codex_ka_pc_src_r4.md R3-a/R3-b, codex_ka_pc_src_r5.md F1/F2): the owner fence as ONE state
// under ONE lock (OwnerFenceGate, viewer_control_state.hpp) that every party goes through:
//
//  - the fence (UI thread) raises it FIRST -- fence_raise: from then on nothing but a release may start a write --
//    and records, in the same critical section, the input-exchange counters as they stand (its baseline);
//  - the control thread takes an input off the queue AND counts it (taken, in flight, is it a release) in one critical
//    section (gate_take_release / gate_begin_action) -- so a dequeued input is never "gone from the queue but not yet
//    counted" to anyone who holds the lock;
//  - every control-channel write (FenceGatedLink) and every bulk datagram (gate_bulk_send_allowed) asks the gate under
//    the same lock: after the fence only a counted release's exchange may write. A write is STARTED when the gate says
//    yes; one the gate allowed before the fence is a write that began before it (it is not cut off); one it refuses is
//    never started -- and its caller drops (zeroes) whatever it had already taken off a shared queue;
//  - the fence's verdict (fence_verdict) reads the queue, the in-flight mark, the counters and the baseline in one
//    critical section: confirmed only when every input exchange since the baseline ended in the host's InputAck (or
//    was deliberately not sent by the logon-screen gate), failed at once when one did not.
//
// The pending work the fence cancels (owner_fence_cancel_pending) and the release rule (action_is_release, the same
// rule as the shared core's release_identity) are here too. Header-only: the units test drives these very functions.

#include <winsock2.h>
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include "native_video_client_shared_core.hpp"
#include "udp_control_channel.hpp"
#include "viewer_control_state.hpp"

namespace remote60::native_poc::viewer {

// r7 (Codex N1): the shared core's ONE release rule (control_input_release_identity), asked about an outbound action --
// a key up, a mouse button up (left, right, middle, X1, X2), a physical key up. No second copy of the rule.
inline bool action_is_release(const ControlOutboundAction& a) {
  QueuedControlInputMessage m{};
  if (a.kind == ControlOutboundActionKind::InputEvent) {
    m.type = MessageType::ControlInputEvent;
    m.inputEvent = a.inputEvent;
  } else if (a.kind == ControlOutboundActionKind::PhysicalKey) {
    m.type = MessageType::ControlPhysicalKey;
    m.physicalKey = a.physicalKey;
  } else {
    return false;
  }
  return control_input_release_identity(m) != 0;
}
inline bool action_is_input(const ControlOutboundAction& a) {
  return a.kind == ControlOutboundActionKind::InputEvent || a.kind == ControlOutboundActionKind::InputText ||
         a.kind == ControlOutboundActionKind::PhysicalKey;
}

inline bool gate_is_fenced(const OwnerFenceGate& g) {
  std::lock_guard<std::mutex> lock(g.mu);
  return g.fenced;
}

// The fence: raised once, with its baseline taken in the same critical section.
inline void fence_raise(OwnerFenceGate& g) {
  std::lock_guard<std::mutex> lock(g.mu);
  if (g.fenced) return;
  g.fenced = true;
  g.taken0 = g.taken;
  g.acked0 = g.acked;
  g.failed0 = g.failed;
  g.skipped0 = g.skipped;
  g.cancelled0 = g.cancelled;
}

// Normal path (control thread): `take` runs the scheduler's choice (it may dequeue an input) INSIDE the gate's
// critical section; the result is admitted or refused there too. Refused: a non-release after the fence -- it is
// dropped, never written. Admitted inputs are counted (taken, in flight); a release's exchange may write after a fence.
enum class GateAdmit { None, Admitted, Refused };
template <typename Take>
inline GateAdmit gate_begin_action(OwnerFenceGate& g, ControlOutboundAction* out, Take take) {
  std::lock_guard<std::mutex> lock(g.mu);
  if (!take(out)) return GateAdmit::None;
  const bool input = action_is_input(*out);
  const bool release = input && action_is_release(*out);
  if (g.fenced && !release) {
    g.releaseExchange = false;
    return GateAdmit::Refused;
  }
  g.releaseExchange = release;
  if (input) {
    g.inFlight = true;
    ++g.taken;
  }
  return GateAdmit::Admitted;
}

// Fenced path (control thread): the next queued RELEASE, taken and counted in one critical section. Anything else
// still queued (there should be none: the fence dropped it) is dropped here, never sent. `betweenForTest` (null in
// the product) runs between the dequeue and the count, still inside the critical section -- a test's stall there.
inline bool gate_take_release(OwnerFenceGate& g, ClientInputQueue& queue, uint64_t nowUs, ControlOutboundAction* out,
                              const std::function<void()>* betweenForTest = nullptr) {
  std::lock_guard<std::mutex> lock(g.mu);
  for (;;) {
    QueuedControlInputMessage m{};
    if (!queue.TryDequeue(&m)) return false;
    ControlOutboundAction a{};
    a.expectedResponseType = MessageType::ControlInputAck;
    a.expectedResponseSize = static_cast<uint16_t>(sizeof(ControlInputAckMessage));  // as expected_message_size
    a.inputGeneratedUs = m.generatedUs;
    if (m.type == MessageType::ControlInputEvent) {
      a.kind = ControlOutboundActionKind::InputEvent;
      a.inputEvent = m.inputEvent;
      a.inputEvent.clientSendQpcUs = nowUs;
    } else if (m.type == MessageType::ControlPhysicalKey) {
      a.kind = ControlOutboundActionKind::PhysicalKey;
      a.physicalKey = m.physicalKey;
      a.physicalKey.clientSendQpcUs = nowUs;
    } else {
      continue;  // text: never a release
    }
    if (!action_is_release(a)) continue;  // a down / move / wheel: dropped, never sent
    if (betweenForTest && *betweenForTest) (*betweenForTest)();
    g.releaseExchange = true;
    g.inFlight = true;
    ++g.taken;
    *out = a;
    return true;
  }
}

enum class InputOutcome { Acked, Failed, Skipped, Cancelled };
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
}
inline void gate_end_other(OwnerFenceGate& g) {
  std::lock_guard<std::mutex> lock(g.mu);
  g.releaseExchange = false;
}

// May this control-channel write start now? (FenceGatedLink, under the lock.) `bytesOfMessageSoFar`: how much of the
// current message the link already wrote -- a refusal then is recorded as before-any-byte or mid-message (r7, N2).
inline bool gate_write_allowed(OwnerFenceGate& g, size_t bytesOfMessageSoFar = 0) {
  std::lock_guard<std::mutex> lock(g.mu);
  if (bytesOfMessageSoFar == 0) g.lastRefusal = OwnerFenceGate::Refusal::None;  // a new message: a fresh record
  if (!g.fenced) return true;
  if (!g.releaseExchange) {
    g.lastRefusal = bytesOfMessageSoFar == 0 ? OwnerFenceGate::Refusal::FenceBeforeAnyByte
                                             : OwnerFenceGate::Refusal::FenceMidMessage;
    return false;
  }
#if defined(REMOTE60_VIEWER_KA_TEST_SEAM)
  // TEST BUILD ONLY (GNLinkViewerKaTest): REMOTE60_VIEWER_TEST_REFUSE_RELEASE_WRITE_AFTER_FENCE=1 -- the FIRST release
  // write after the fence is refused: no byte of it leaves (a write the link refused), so its exchange fails and the
  // host never acknowledges it. Not an after-the-fact flip of a result (r5's hook was that, and proved nothing about
  // delivery).
  {
    static bool armed = [] {
      const char* v = std::getenv("REMOTE60_VIEWER_TEST_REFUSE_RELEASE_WRITE_AFTER_FENCE");
      return v && *v == '1';
    }();
    if (armed) {
      armed = false;
      g.lastRefusal = OwnerFenceGate::Refusal::TestHook;
      return false;
    }
  }
#endif
  return true;
}
// May a bulk (clipboard image / file) datagram go now? Never after the fence.
inline bool gate_bulk_send_allowed(OwnerFenceGate& g) { return !gate_is_fenced(g); }

enum class FenceVerdict { Pending, Confirmed, Failed };
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
}

// A ControlLink whose writes ask the gate first. Follows the link it was given even when that is replaced.
class FenceGatedLink : public ControlLink {
 public:
  FenceGatedLink(std::unique_ptr<ControlLink>& inner, OwnerFenceGate& gate) : inner_(&inner), gate_(gate) {}
  bool Read(void* out, size_t len) override { return *inner_ && (*inner_)->Read(out, len); }
  bool Write(const void* data, size_t len) override {
    if (!gate_write_allowed(gate_, messageBytes_)) return false;  // never started (the reason is in the gate)
    const bool ok = *inner_ && (*inner_)->Write(data, len);
    if (ok) messageBytes_ += len;
    else messageBytes_ = 0;  // that exchange is over (a transport failure); the next write starts a new message
    return ok;
  }
  bool EndMessage() override {
    messageBytes_ = 0;  // the next Write begins a new message
    return *inner_ && (*inner_)->EndMessage();
  }
  bool Alive() const override { return *inner_ && (*inner_)->Alive(); }

 private:
  std::unique_ptr<ControlLink>* inner_;
  OwnerFenceGate& gate_;
  size_t messageBytes_ = 0;  // of the message being written (control thread only)
};

// The fence's cancellation of what has not left yet (UI thread, right after fence_raise). Returns how many queued
// inputs were dropped.
inline uint32_t owner_fence_cancel_pending(ClientInputQueue& queue, ClipboardSyncState& clip, std::mutex& credMu,
                                           std::wstring& credPassword, std::atomic<bool>& credRequested) {
  uint32_t dropped = 0, keptReleases = 0;
  queue.DropAllButReleases(&dropped, &keptReleases);
  clip.enabled.store(false, std::memory_order_release);  // no send, no poll from here on
  {
    std::lock_guard<std::mutex> lock(clip.mu);
    if (!clip.pendingText.empty()) SecureZeroMemory(&clip.pendingText[0], clip.pendingText.size() * sizeof(char16_t));
    clip.pendingText.clear();
    clip.hasPending = false;
  }
  credRequested.store(false, std::memory_order_release);
  {
    std::lock_guard<std::mutex> lock(credMu);
    if (!credPassword.empty()) SecureZeroMemory(&credPassword[0], credPassword.size() * sizeof(wchar_t));
    credPassword.clear();
  }
  return dropped;
}

}  // namespace remote60::native_poc::viewer
