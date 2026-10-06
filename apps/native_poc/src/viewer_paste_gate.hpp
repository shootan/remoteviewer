#pragma once

// Paste on demand (t-y4wj64jw): the viewer's decisions, without a window.
//
// Role:    A copy on this PC no longer changes the remote PC's clipboard. Ctrl+V / Shift+Insert in
//          the GNLink window does: the clipboard as it is at that moment goes to the remote PC, and
//          only once the remote PC says it is ON ITS CLIPBOARD does the paste key go -- rebuilt from
//          scratch, so the modifiers released while waiting do not stay held on the remote PC. A
//          failure sends no key at all: the remote PC's older clipboard must never be pasted in the
//          user's name. This header is the part of that which needs no window, no socket and no
//          clock of its own: which keys are a paste, what a paste gesture should do, the one
//          pending paste and the one waiting behind it, the keys typed meanwhile, and the key
//          sequence that performs the paste.
// Thread:  the viewer's UI thread only (not synchronised).
// Callers: viewer_window_proc.cpp, viewer_paste_gate_test.cpp.

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace remote60::native_poc::viewer {

/** The modifiers held on THIS keyboard when a key goes down (GetKeyState). */
struct PasteMods {
  bool ctrl = false;
  bool shift = false;
  bool alt = false;
  bool win = false;
};

enum class PasteKey : uint8_t { None = 0, CtrlV, CtrlShiftV, ShiftInsert };

/**
 * Whether a key down is a paste gesture. Ctrl+V (also Ctrl+Shift+V, "paste as text" in many
 * programs) and Shift+Insert. Never with Alt -- Ctrl+Alt is AltGr on many layouts, and AltGr+V is a
 * character -- and never with Win.
 */
PasteKey classify_paste_key(uint32_t vk, const PasteMods& m);

/**
 * Whether a key down is a copy gesture sent to the remote PC: Ctrl+C, Ctrl+X, Ctrl+Insert. It makes
 * the remote PC's clipboard the newest copy (LatestCopy), so the next Ctrl+V pastes that and does
 * not overwrite it with this PC's older one.
 */
bool is_copy_key(uint32_t vk, const PasteMods& m);

/**
 * Which clipboard is the newest copy the user made: this PC's, or the remote PC's. A paste sends this
 * PC's clipboard only when it is the newest; otherwise the remote PC pastes its own, exactly as it
 * did before this feature -- an ordinary copy-and-paste inside the remote PC must keep working, and
 * images copied there never reach this PC at all.
 */
class LatestCopy {
 public:
  /** A session opens: this PC's clipboard is the newest if it holds something that can be sent (Q2). */
  void ResetForSession(bool localHasPasteable) { local_ = localHasPasteable; }
  /** The user copied on this PC (not an echo of something written here from the remote PC). */
  void NoteLocalCopy() { local_ = true; }
  /** A copy on the remote PC: a copy key sent there, or its clipboard brought to this PC. */
  void NoteRemoteCopy() { local_ = false; }
  bool LocalIsLatest() const { return local_; }

 private:
  bool local_ = false;
};

enum class PasteRoute : uint8_t {
  Forward = 0,      // the feature does not apply (input or clipboard sync off): the key goes as before
  PassThrough = 1,  // the remote PC's own clipboard is the newest: the key goes at once
  Send = 2,         // send this PC's clipboard, wait for "on the clipboard", then the key
  UpdateNeeded = 3, // this PC's copy is the newest but the remote GNLink cannot confirm a paste
};

/**
 * `featureOn`: input enabled, clipboard sync switched on and the host has clipboard sync.
 * `hostConfirms`: the host advertises kCaptureFlagPasteOnDemandV1.
 */
PasteRoute route_paste(bool featureOn, bool hostConfirms, bool localIsLatest);

/** What was on this PC's clipboard at the gesture. */
enum class PasteFormat : uint8_t { None = 0, Text = 1, Image = 2, Files = 3 };

/** Why a paste ended without its key. Shown on the bar (Cancelled: also logged only, see Silent). */
enum class PasteFailure : uint8_t {
  None = 0,
  Timeout,          // no answer in time
  HostWrite,        // the remote PC could not write its clipboard
  Refused,          // the remote PC would not take the image / files
  HelperUnavailable,// the remote PC's file helper could not run
  Empty,            // nothing on this PC's clipboard that can be pasted
  TooLarge,         // the copied image or text is over the limit
  ReadFailed,       // this PC's clipboard could not be read
  UpdateNeeded,     // the remote GNLink cannot confirm a paste (older version)
  Busy,             // a paste is running and another is already waiting
  Link,             // the control link went away
  Cancelled,        // Esc, a mouse click, the bar's cancel
  Silent,           // focus left, the target changed, the session ended: logged, not shown
};

/** The control thread's answer for one paste (posted to the UI thread as kMsgPasteResult). */
struct PasteAnswer {
  uint64_t id = 0;
  bool applied = false;  // on the remote PC's clipboard
  PasteFormat format = PasteFormat::None;
  PasteFailure failure = PasteFailure::None;  // when not applied
  uint32_t detail = 0;                        // the wire's reason (result/stage/verdict), for the log
  // r4: the answer to a paste's check before sending (one fresh poll), not to the paste itself.
  bool probe = false;
  bool genKnown = false;      // the host sends a copy generation
  uint64_t hostCopyGen = 0;   // its value now
};

/** One key edge typed while a paste was pending, kept to be sent after it. */
struct HeldKey {
  uint32_t msg = 0;  // WM_KEYDOWN / WM_KEYUP / WM_SYSKEYDOWN / WM_SYSKEYUP
  uint64_t wp = 0;
  int64_t lp = 0;
  // kHeldCtrl / kHeldShift as held on this keyboard when the key was typed: replayed later, a
  // Ctrl+B typed while waiting must still reach the remote PC as Ctrl+B, not as B.
  uint8_t mods = 0;
};
constexpr uint8_t kHeldCtrl = 1;
constexpr uint8_t kHeldShift = 2;

/** One edge of the rebuilt paste key sequence. `scan`/`ext` are used on the host-IME path. */
struct PasteStep {
  bool down = false;
  uint16_t vk = 0;
  uint16_t scan = 0;
  bool ext = false;
};

/**
 * The key sequence that performs the paste on the remote PC, complete and balanced: modifiers down,
 * the key down and up, modifiers up in reverse. `keyScan`/`keyExt` are the scan code the user's key
 * produced (0 = the US-layout default), so a remapped layout pastes with its own key.
 */
std::vector<PasteStep> paste_chord(PasteKey key, uint16_t keyScan, bool keyExt);

constexpr uint64_t kPasteTextDeadlineUs = 3000000;   // Q5: the host waits up to 2 s on its clipboard
constexpr uint64_t kPasteFilesDeadlineUs = 8000000;  // Q5: the host waits up to 5 s on its helper
constexpr size_t kPasteHeldKeyLimit = 32;            // keys typed while waiting; more cancels the paste

/**
 * The paste in flight and the one gesture that may wait behind it. Ids are the caller's (random,
 * never 0): an answer counts only for the id it names, the connection it was asked on and the target
 * that was selected then -- a late answer cannot release a key for a different paste.
 */
class PasteGate {
 public:
  struct Ticket {
    uint64_t id = 0;
    uint64_t connGen = 0;
    uint64_t targetGen = 0;
    PasteKey key = PasteKey::None;
    uint16_t scan = 0;
    bool ext = false;
    PasteFormat format = PasteFormat::None;
    uint64_t budgetUs = 0;    // how long it may take once started; 0 = no deadline of the gate's own
                              // (an image: its transfer has a size-based one, and a Cancel button)
    uint64_t startedUs = 0;   // set by Admit (Started) or Promote
    uint64_t deadlineUs = 0;  // startedUs + budgetUs, or 0
  };

  enum class Admitted : uint8_t { Started = 0, Waiting = 1, Dropped = 2 };
  /**
   * A gesture whose route is Send. Started: it is the pending paste now (the caller sends it).
   * Waiting: it waits behind the pending one (the caller keeps its snapshot). Dropped: one is
   * already waiting -- a third press does nothing but say so.
   */
  Admitted Admit(const Ticket& t, uint64_t nowUs);

  bool Pending() const { return pending_.id != 0; }
  const Ticket& pending() const { return pending_; }
  bool HasWaiting() const { return waiting_.id != 0; }

  enum class Outcome : uint8_t { Ignored = 0, Inject = 1, Fail = 2 };
  /**
   * The answer for paste `id`. Inject: send paste_chord(ticket.key) then the held keys. Fail: no
   * key, and the held key downs are dropped. Ignored: it is not the pending paste's answer (late,
   * another id, another connection or target) -- nothing changes. On Inject/Fail `*done` is the
   * ticket that ended and the pending slot is empty: Promote() starts the waiting one.
   */
  Outcome OnAnswer(uint64_t id, bool applied, uint64_t connGen, uint64_t targetGen, Ticket* done);
  /** The pending paste ran out of time. True (with the ticket) when it did. */
  bool OnTick(uint64_t nowUs, Ticket* expired);
  /** The waiting gesture becomes the pending one. False when nothing is waiting. */
  bool Promote(uint64_t nowUs, Ticket* started);
  /** Everything ends: the pending and the waiting paste, and the held keys. Returns what ended. */
  std::vector<Ticket> CancelAll();

  // Keys typed while a paste is pending. Hold() false = over the limit (the caller cancels).
  bool Hold(const HeldKey& k);
  /** Whether a key down for `wp` is among the held ones (its up is then held too). */
  bool HoldsDown(uint64_t wp) const;
  std::vector<HeldKey> TakeHeld();
  size_t HeldCount() const { return held_.size(); }

 private:
  Ticket pending_;
  Ticket waiting_;
  std::deque<HeldKey> held_;
};

/** The bar's line for a failed paste (Silent and None: empty). */
std::wstring paste_failure_text(PasteFailure f);

}  // namespace remote60::native_poc::viewer
