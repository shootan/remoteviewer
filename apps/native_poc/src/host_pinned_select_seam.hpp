#pragma once

#ifdef REMOTE60_PINNED_SELECT_TEST_SEAM
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <mutex>
#include <set>
#include <string>

namespace remote60::native_poc {

// TEST BUILD ONLY (GNLinkStreamPinnedSelectTest, keepalive r3 R3): the capture boundary of the pinned test
// host. Defined in host_control_session.cpp, next to the start admission.
//
// The pinned window is found once, at start (pinned_select_seam_admit_start): the exact HWND of the pinned
// process whose title holds the pinned title. Afterwards a target is the pinned one only while that same
// HWND is a window, still belongs to the pinned process (whose handle this host holds, so the pid cannot
// be reused) and still carries the title.
bool pinned_select_seam_target_ok(HWND hwnd);

// The pinned window found at start (null before / without it).
HWND pinned_select_seam_pinned_hwnd();

// Every capture start in this build passes here first: the startup item, each restart (WGC, DXGI, GDI),
// each item a selection or a rebind makes. `window` is the window to be captured; null means a screen.
// Anything but the pinned window ends this host at once (exit 65) -- never a fallback to a screen.
// With REMOTE60_PINNED_TEST_CAPTURE_PROBE=1 the first call reports what it would capture and ends the host
// (66 when admitted) before any capture object exists: the boundary is checked without a picture.
void pinned_select_seam_capture_gate(HWND window, const char* where);

// Ends this host now (exit 65), with the reason on stderr. Used when the pinned target cannot be held.
[[noreturn]] void pinned_select_seam_refuse(const char* where);

// REMOTE60_PINNED_TEST_CAPTURE_PROBE=1 with REMOTE60_PINNED_TEST_PROBE_UNGATED=1: the negative control.
// The admission's window check, the startup refusal and the gate's verdict are skipped (the 2abb8be
// behaviour); the probe still ends the host at the first capture call, so this shows where the old path
// would have gone -- the screen -- without ever capturing it. Never honoured without the probe.
bool pinned_select_seam_probe_ungated();

// REMOTE60_PINNED_TEST_NO_CAPTURE=1 (keepalive r3, parkclip): the host never starts a capture at all. Pinned
// as always (the fixture window of that process, on this host's own desktop, watched for its lifetime), it
// stops right before the capture target is chosen and serves only what its control threads do -- Hello,
// Ping, stream-state, clipboard -- until --seconds or its stop. For a host on a private window station,
// where no window is ever visible and WGC captures none.
bool pinned_select_seam_no_capture();

// keepalive r3 (K4 held tap, IDR/ACK order and loss): faults at the host's own packet boundaries, read at
// the moment they apply from the file REMOTE60_PINNED_TEST_FAULT_FILE names ("key=value" lines; absent or
// 0 = none) -- so one running test host serves each case in turn:
//   pong_delay_ms  every Pong is held this long while the stream is paused (the park's fence Pong first)
//   ack_delay_ms   a window-select answer is held this long after its selection was applied (its IDR
//                  goes out first: answer and key frame reach the phone in the reverse order)
//   drop_keys      this many key frames after a window-select are never put on the wire (nor cached
//                  for a NACK): lost for good
// The client's pause/fence/selection logic is the product's; nothing here answers in its place.
struct PinnedTestFaults {
  unsigned pongDelayMs = 0;
  unsigned ackDelayMs = 0;
  unsigned dropKeys = 0;
};
PinnedTestFaults pinned_select_seam_faults();
// Arms drop_keys (called on a window-select) / takes one drop (the sender, per key frame): true = drop it.
void pinned_select_seam_arm_key_drops(unsigned n);
bool pinned_select_seam_take_key_drop();

// keepalive-r2 e (Codex codex_ka_e_src_r3.md decision 2): the activation failure of ONE test episode, so the APK's
// fallback (nativeConnectStandby gen 0, still its operation -> the same tap's ordinary connect) runs for real.
//   fail_activation_once=<token>  the NEXT StandbyActivate this host receives is refused as an unknown entry (the
//                                 existing kUdpStandbyStatusUnknown answer, as when an entry expired) -- once per
//                                 token: a token already used never applies again, so a later activation and every
//                                 ordinary connect (a Hello, not a StandbyActivate) are untouched. A new episode
//                                 writes a new token (ASCII, up to 32: letters, digits, '-', '_').
// True when this activation is the one to refuse; *log then names the token, the entry and the generation.
// (Header-only: the host and its seam-built unit test, host_standby_activation_fault_test, share this one.)
// keepalive-r2 e (decision 2): fail_activation_once=<token> -- read at the moment an activation arrives; a token
// applies to one activation only (the tokens used are kept for this process's life).
inline bool pinned_select_seam_take_activation_fault(uint64_t entryId, uint32_t gen, std::string* log) {
  char path[MAX_PATH] = {};
  if (GetEnvironmentVariableA("REMOTE60_PINNED_TEST_FAULT_FILE", path, sizeof(path)) == 0) return false;
  FILE* fp = nullptr;
  if (fopen_s(&fp, path, "rb") != 0 || !fp) return false;
  std::string token;
  char line[128];
  while (std::fgets(line, sizeof(line), fp)) {
    char t[40] = {};
    if (std::sscanf(line, "fail_activation_once=%32[A-Za-z0-9_-]", t) == 1) token = t;
  }
  std::fclose(fp);
  if (token.empty()) return false;
  static std::mutex mu;
  static std::set<std::string> used;
  {
    std::lock_guard<std::mutex> lk(mu);
    if (!used.insert(token).second) return false;  // this episode's activation was already refused
  }
  if (log) {
    *log = "[pinned-select-test] fault: fail_activation applied token=" + token + " entry=" + std::to_string(entryId) +
           " gen=" + std::to_string(gen) + " -- refused as unknown (once)";
  }
  return true;
}


}  // namespace remote60::native_poc
#endif
