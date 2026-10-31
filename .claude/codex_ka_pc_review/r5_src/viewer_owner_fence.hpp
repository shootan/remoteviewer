#pragma once

// ka-pc-impl r5 (Codex codex_ka_pc_src_r4.md R3-a / R3-b): the owner fence's two halves that are not window messages.
//
//  - what the fence CANCELS at once (UI thread, the fence handler): every queued input that is not a release (downs,
//    text, moves, wheel -- ClientInputQueue::DropAllButReleases), the pending clipboard text (and the clipboard sync
//    itself: `enabled` off, so pump_clipboard_sync sends and polls nothing more), a typed logon / lock-screen
//    password that was waiting to be sealed and sent (zeroed, never sent);
//  - what the control thread still SENDS after it: releases only, taken straight off the input queue
//    (take_release_action) -- no ping-scheduled work, no window / monitor pick, no stream state, no clipboard, no
//    credential, no IME align;
//  - how the fence knows its releases ARRIVED (fence_release_verdict): counters the control thread keeps for every
//    input exchange it RUNS -- taken, acknowledged by the host (an InputAck), failed. An empty queue or an idle
//    channel is not an acknowledgement: a release whose exchange failed leaves both empty and was never confirmed.
//
// Header-only, so the units test drives the very functions the viewer runs.

#include <winsock2.h>
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

#include "native_video_client_shared_core.hpp"
#include "viewer_control_state.hpp"

namespace remote60::native_poc::viewer {

// InputExchangeCounters (viewer_control_state.hpp): the control thread's count of input exchanges.

struct FenceCounterSnapshot {
  uint64_t taken = 0, acked = 0, failed = 0;
};

inline FenceCounterSnapshot snapshot(const InputExchangeCounters& c) {
  return FenceCounterSnapshot{c.taken.load(), c.acked.load(), c.failed.load()};
}

enum class FenceVerdict { Pending, Confirmed, Failed };

// `atFence`: taken BEFORE the fence queued its releases. Confirmed only when nothing is queued, nothing is in flight,
// and every exchange started since the fence was acknowledged by the host (an exchange already in flight at the fence
// may be acknowledged too, hence >=). Failed as soon as any exchange since the fence did not end in an InputAck.
inline FenceVerdict fence_release_verdict(const FenceCounterSnapshot& atFence, const FenceCounterSnapshot& now,
                                          bool queueEmpty, bool inFlight) {
  if (now.failed > atFence.failed) return FenceVerdict::Failed;
  if (!queueEmpty || inFlight) return FenceVerdict::Pending;
  return (now.acked - atFence.acked) >= (now.taken - atFence.taken) ? FenceVerdict::Confirmed : FenceVerdict::Pending;
}

// The fence's cancellation of what has not left yet (UI thread). Returns how many queued inputs were dropped.
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

// After the fence, the only thing the control thread sends: the next queued input, which (the fence having dropped
// everything else and gated new input) is a release. The same mapping the scheduler's input branch uses.
inline bool take_release_action(ClientInputQueue& queue, uint64_t nowUs, ControlOutboundAction* out) {
  QueuedControlInputMessage outbound{};
  if (!queue.TryDequeue(&outbound)) return false;
  *out = ControlOutboundAction{};
  out->expectedResponseType = MessageType::ControlInputAck;
  out->expectedResponseSize = static_cast<uint16_t>(sizeof(ControlInputAckMessage));  // as expected_message_size gives
  out->inputGeneratedUs = outbound.generatedUs;
  if (outbound.type == MessageType::ControlInputEvent) {
    out->kind = ControlOutboundActionKind::InputEvent;
    out->inputEvent = outbound.inputEvent;
    out->inputEvent.clientSendQpcUs = nowUs;
    return true;
  }
  if (outbound.type == MessageType::ControlInputText) {
    out->kind = ControlOutboundActionKind::InputText;
    out->inputText = outbound.inputText;
    out->inputText.clientSendQpcUs = nowUs;
    return true;
  }
  if (outbound.type == MessageType::ControlPhysicalKey) {
    out->kind = ControlOutboundActionKind::PhysicalKey;
    out->physicalKey = outbound.physicalKey;
    out->physicalKey.clientSendQpcUs = nowUs;
    return true;
  }
  return false;
}

}  // namespace remote60::native_poc::viewer
