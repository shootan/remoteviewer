#pragma once

// Control-channel state of the viewer (Phase 1-5 state struct).
//
// Role:    the request states the control scheduler drains (input queue, keyframe requests,
//          runtime tune, stream state, capture mode), the scheduler itself, the UDP control
//          tunnel and whether it is in use, connection status, and the secure-desktop transition
//          latch of the control loop.
// Thread:  the control thread owns the scheduler and writes connected / reportedSecure;
//          UI, recv and main enqueue requests (each request state is atomic- or mutex-backed);
//          UDP ingress alone reads the socket and feeds udpControl.OnPacket. Ingress, receiver
//          maintenance and the control thread can Tick; UdpControlChannel serializes them with
//          its mutex. ControlLink.Receive waits on the channel queue, not on the socket.
// Input:   UI/recv requests, host replies.
// Output:  outbound control actions.
// Callers: main() (connect/setup), control thread, viewer_picker, viewer_input_forward, viewer_log,
//          viewer_overlay_draw, recv thread.
//
// Fields are the former globals gControlScheduler / gKeyframeRequests / gRuntimeTuneState /
// gStreamStateControl / gCaptureModeRequests / gInputQueueState / gUdpControl / gControlOverUdp /
// gControlConnected and the control loop's static reportedSecure, initialisers unchanged (viewer
// split refactor Phase 1-5). The write-only capture-meta mirror that came with them is gone (F-03):
// the pong's fields are logged straight from the message.

#include "viewer_common.hpp"
#include "viewer_constants.hpp"
#include "bulk_arbiter.hpp"
#include "clip_image_client.hpp"
#include "file_copy_client.hpp"
#include "clipboard_sync.hpp"
#include "viewer_control_resume.hpp"

namespace remote60::native_poc::viewer {

// Clipboard text sync (K1). The UI thread hears local clipboard changes (WM_CLIPBOARDUPDATE) and
// leaves the ones worth sending as a pending outbound; the control thread drains that pending item,
// sends it, and polls the host for changes the other way. `enabled` is the viewer's on/off toggle
// (default on); `hostSupports` is set from the pong capability bit. The core and the pending item
// are guarded by `mu` because the UI thread produces and the control thread consumes.
// The remote PC's text for the UI thread, with what decides whether it may land here -- all from the
// one poll reply it came in (r7, D1): the host's copy generation then and the connection it came on.
// The UI thread decides against this PC's copy state as it is when it handles it (it records the
// copies here), and checks once more with the clipboard held.
struct RemoteTextApply {
  std::u16string text;
  uint64_t hash = 0;
  bool hasCopyGen = false;  // an older host sends none: no order fence then (r2 behaviour)
  uint64_t copyGen = 0;
  uint64_t connGen = 0;
  int deferrals = 0;        // times put back behind a clipboard change here not handled yet
};

// r7 (D2): a remote copy of files the file client asks the UI thread about, with the connection the
// offer came on (read where it was asked, on the control thread).
struct RemoteFilesDecide {
  remote60::native_poc::FileCopyClient::RemoteFilesForGate files;
  uint64_t connGen = 0;
  int deferrals = 0;
};

struct ClipboardSyncState {
  std::atomic<bool> enabled{true};        // the toolbar toggle; off means no send and no poll
  std::atomic<bool> hostSupports{false};  // host advertised kCaptureFlagClipboardTextV1 (pong)
  // The clipboard sequence number right after this viewer wrote the host's text to it (UI thread).
  // The WM_CLIPBOARDUPDATE that write provokes is its own echo, not a new copy by the user.
  std::atomic<uint32_t> ownWriteSeq{0};

  std::mutex mu;
  remote60::native_poc::ClipboardSyncCore core;  // echo/duplicate suppression, both directions
  bool hasPending = false;                // a local change waiting for the control thread to send
  std::u16string pendingText;
  uint64_t pendingHash = 0;
  uint32_t nextSeq = 0;

  // control thread only: the session-boundary rules (baseline poll, one-shot push, poll interval),
  // shared with the Android session so both clients behave the same.
  remote60::native_poc::ClipboardClientPolicy policy;

  // Paste on demand (t-y4wj64jw). A copy here sends nothing; a Ctrl+V in the window sends what is on
  // the clipboard then and waits for the host to say it is on ITS clipboard.
  // Host advertised kCaptureFlagPasteOnDemandV1 (pong; control writes, UI reads).
  std::atomic<bool> hostPasteOnDemand{false};
  // Bumped each time a clipboard session opens (the first pong with the clipboard bit): an answer
  // asked on an earlier connection cannot release a key on this one.
  std::atomic<uint64_t> connGen{0};
  // The text paste the UI thread left for the control thread (under mu). Images and files go
  // through their own clients (SubmitSnapshotForPaste / SubmitLocalFilesForPaste).
  // Paste on demand r4: the host's copy generation from the last poll (control writes, UI reads), and
  // whether this host sends one at all (an older host does not: then r2 behaviour).
  std::atomic<uint64_t> hostCopyGen{0};
  std::atomic<bool> hostCopyGenKnown{false};
  // A paste asks for one poll NOW, before it sends anything (under mu): did the host copy since?
  bool probeRequested = false;
  uint64_t probeId = 0;
  // r5 (F1): a copy here asks for one poll right after it, so the host generation that marks it is
  // known (its baseline). The token is the local clipboard sequence of that copy.
  bool baselineRequested = false;
  uint64_t baselineToken = 0;
  // The clipboard sequence of this PC's last copy, as the UI thread recorded it (published for
  // observers; the UI thread itself decides with its own copy state, r7).
  std::atomic<uint64_t> localCopySeq{0};
  bool havePasteText = false;
  uint64_t pasteTextId = 0;
  uint32_t pasteTextRevision = 0;
  std::u16string pasteText;
  uint64_t pasteTextHash = 0;
};

struct ControlChannelState {
  // control thread only.
  ClientControlScheduler scheduler;
  // cross-thread: producers UI/recv/main, consumer control (atomic-backed request states).
  KeyframeRequestState keyframeRequests{
      kKeyframeRequestMinIntervalUsDefault,
      kKeyframeRequestTokenRefillUsDefault,
      kKeyframeRequestTokenCapacityDefault};
  RuntimeTuneState runtimeTune{
      kRuntimeBitrateMin,
      kRuntimeBitrateMax,
      kRuntimeBitrateStep,
      kRuntimeKeyintMin,
      kRuntimeKeyintMax};
  remote60::native_poc::StreamStateControl streamState;
  CaptureModeRequestState captureModeRequests;
  ClientInputQueue inputQueue;
  // C0 stage 1: the receive thread's latest bandwidth observation, for the control thread to
  // send. Guarded; copied out whole so a report is never sent half-updated.
  std::mutex bandwidthMu;
  ClientBandwidthSnapshot bandwidth;
  // Control over the media socket, for hosts reached through the directory.
  //
  // A second TCP connection cannot be opened to a host behind NAT: only the UDP socket was
  // punched, so control has to ride it. Everything the session needs -- input, the window list,
  // the monitor list, runtime tuning -- goes through here, which is why a session without it
  // shows a picture and responds to nothing.
  // cross-thread: control writes, recv ticks + OnPacket, main configures/closes.
  remote60::native_poc::UdpControlChannel udpControl;
  std::atomic<bool> overUdp{false};
  // Control resume (item 8, C3): the recovery that rebuilds the tunnel after peer-lost
  // without a reconnect. The control worker drives it; UDP ingress hands it verified
  // answers and nothing else. Idle -- and never asked -- against a host that did not
  // advertise kUdpFeatureControlResume.
  // cross-thread: worker decides and re-keys, ingress validates, main configures.
  remote60::native_poc::ViewerControlResume resume;
  // cross-thread: control writes after every pong, recv reads (the A04 give-up's reply allowance
  // is 2 x this RTT, when fresh). 0 = no pong yet.
  std::atomic<uint64_t> lastRttUs{0};
  std::atomic<uint64_t> lastRttAtUs{0};  // qpc of the pong that measured it
  // cross-thread: main/control write, UI/picker read.
  std::atomic<bool> connected{false};
  // control thread only: say the secure-desktop transition once (was a function static). reset: never (F-14).
  std::atomic<bool> reportedSecure{false};
  // Clipboard text sync (K1): UI thread produces local changes, control thread sends + polls.
  ClipboardSyncState clipboard;
  // Clipboard image v1 (direction A): UI thread submits image copies, control thread offers and
  // polls, recv thread routes the bulk stream to it. Started at connect, stopped at shutdown.
  remote60::native_poc::ClipImageClient clipImage;
  // File copy (t-zdmsd4gb): UI thread submits CF_HDROP copies, control thread offers / queries /
  // prepares, recv thread routes the file bulk streams to it. Started at connect, stopped at shutdown.
  remote60::native_poc::FileCopyClient fileCopy;
  // One bulk per session: the image and the file paths take it in turn (bulk_arbiter.hpp).
  remote60::native_poc::BulkArbiter bulkArbiter;
};

}  // namespace remote60::native_poc::viewer
