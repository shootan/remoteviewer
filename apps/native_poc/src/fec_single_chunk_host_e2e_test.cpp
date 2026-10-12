// fec-single-chunk-stride, end to end: a real, isolated host (GNLinkStream.exe) capturing a window
// of this process, once with the padded parity layout (REMOTE60_NATIVE_FEC_SINGLE_CHUNK_STRIDE=0,
// what every host in the field sends today) and once with the tight one (the default after this
// change), for three kinds of content -- a still page of text, the same page with a blinking caret
// and one creeping block, and busy video-like motion. MEASUREMENT of the wire, plus the checks
// that make the measurement meaningful:
//
//   - the host was started isolated (e2e_isolation.hpp) with input injection off, capturing only
//     this test's own composed, invisible, click-through window -- never the user's screen;
//   - the host's own startup line says which layout it runs (fecSingleChunkStride=tight|padded);
//   - the receiver is the product's shared session policy (SessionVideoPipeline: the real
//     assembler, in-order hold, NACK scheduler -- NACKs go to the real host and its real replay
//     answers them) and the product decoder on every delivered AU;
//   - lossless: every frame the host sent after the first arrives in order -- no gap, no
//     discontinuity, no keyframe request, nothing malformed, everything decodes -- under BOTH
//     layouts; then the wire: the host's own per-flow byte account (udpTxParityBytes and the
//     rest of its stats line) and this end's count of what arrived, tight against padded.
//   - --loss-permille P --loss-seed S: this end drops that share of video datagrams (by datagram
//     identity, so a replay of a lost chunk is a new draw) before the pipeline sees them, and the
//     NACK/FEC path of the real host is exercised; reported, compared, not gated (two live
//     captures are never the same frames).
//
//   remote60_fec_single_chunk_host_e2e_test [--host <GNLinkStream.exe>] [--seconds N]
//       [--content static|lowmotion|video|all] [--loss-permille P] [--loss-seed S] [--keep-dir]
//   (REMOTE60_ALLOW_HOST_E2E=1; exit 77 = skipped)
// display-capture (a composed window on the interactive desktop), gpu-or-software (encoder in the
// host, decoder here), network (loopback).

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iostream>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "e2e_isolation.hpp"
#include "mf_h264_codec.hpp"
#include "native_video_client_shared_core.hpp"
#include "native_video_client_tcp_control.hpp"
#include "poc_protocol.hpp"
#include "host_e2e_marker_fixture.hpp"  // r8: shared Target/EventLog/decode_event_marker
#include "host_e2e_wgc_probe.hpp"       // encoder-rc-budget r7: ask WGC ourselves, with the HRESULT
#include "host_e2e_dump_check.hpp"      // encoder-rc-budget r9: read a hang dump back, with symbols
#include "host_encoder_budget.hpp"      // encoder-rc-budget r2: E(R, fps), the same function the host uses
#include "session_video_pipeline.hpp"
#include "viewer_constants.hpp"       // r7 V2: FrameGate env defaults
#include "viewer_frame_gate.hpp"      // r7 V2: the real PC receive gate
#include "viewer_frame_gate_state.hpp"
#include "viewer_recv_stats.hpp"
#include "time_utils.hpp"
#include "udp_control_channel.hpp"
#include "control_resume_e2e_support.hpp"

#include <mfapi.h>
#include <mmsystem.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")  // encoder-rc-budget r8: the hang dump of a stalled host

#pragma comment(lib, "mfplat.lib")

using namespace remote60::native_poc;
using namespace remote60::native_poc::e2e;

namespace {

int gFails = 0;
int gCheckCount = 0;
void check(bool ok, const std::string& what, const std::string& detail = std::string()) {
  ++gCheckCount;
  if (ok) {
    std::printf("PASS %s\n", what.c_str());
  } else {
    std::printf("FAIL %s%s%s\n", what.c_str(), detail.empty() ? "" : " -- ", detail.c_str());
    ++gFails;
  }
}
std::string u(uint64_t v) { return std::to_string(v); }

// The host port is picked at run time per run (e2e_pick_free_udp_port), so runs and tests can overlap.
constexpr uint32_t kFps = 30;
constexpr uint32_t kBitrate = 1500000;  // the user's 1500 setting, where parity was 25% of payload
  using namespace remote60::native_poc::e2emarker;

// ------------------------------------------------------------------------------ host log

std::string value_of(const std::string& line, const std::string& key) {
  const std::string needle = " " + key + "=";
  const size_t at = line.find(needle);
  if (at == std::string::npos) return {};
  const size_t start = at + needle.size();
  const size_t end = line.find(' ', start);
  return line.substr(start, end == std::string::npos ? std::string::npos : end - start);
}
uint64_t num_of(const std::string& line, const std::string& key) {
  const std::string v = value_of(line, key);
  return v.empty() ? 0 : std::strtoull(v.c_str(), nullptr, 10);
}

struct HostAccount {
  std::string layoutToken;  // fecSingleChunkStride=... from the host's own startup line
  size_t statsLines = 0;
  // Cumulative counters of the last stats line (udpTxFrames, not sentFrames: that one is per interval).
  uint64_t txFrames = 0, txBytes = 0, parity = 0, chunkHdr = 0, videoDatagrams = 0, nackBytes = 0,
           nackDatagrams = 0, wireEst = 0;
  size_t keyframeLines = 0;
  size_t keyTuneOnlyLines = 0;  // r9: [keyframe] lines whose reasons are "tune" alone -- a key no encoder change owed
  // encoder-rc-budget: the host's R/E account (last encoder-budget line), plan lines per instance,
  // runtime RC writes, governor steps, key AU sizes.
  uint64_t requestedWire = 0, effectiveWire = 0, desiredEncoder = 0, appliedEncoder = 0;
  std::string rcProfile;
  std::vector<std::string> budgetActions;
  uint32_t rcInstances = 0, rcPlanPreTypes = 0, rcPlanPostStart = 0;
  std::vector<std::string> rcPlanStates;
  std::vector<uint64_t> rcPlanMeans;
  uint32_t rcReconfigureWrites = 0, rcGovernorWrites = 0, rcInitWrites = 0;
  uint32_t governorQpSteps = 0, governorFpsSteps = 0, deferredRetargets = 0;
  std::vector<uint64_t> keyBytes;
  std::vector<std::pair<uint64_t, uint64_t>> wireCapApplies;  // (bitrate, appliedQpcUs) of every same-clock apply marker
  std::vector<std::string> sizes;  // distinct encode sizes seen in the stats lines, in order (geometry fixed? -> one entry)
  // Isolation matrix (r2): max sender queue wait/depth from the host's own [wire] lines, and the
  // wire cap it logged at startup (wireCapBps=...), and how many capture frames the input gate skipped.
  uint64_t maxQueueWaitUs = 0, maxQueueDepth = 0, wireCapBps = 0, wireOverloadSkips = 0;
  uint64_t captureStallMaxUs = 0;  // r8: max oldestSubmittingUs over the stats lines -- the capture thread stuck in Submit
  // r5 G4: the host's same-clock wire-cap apply marker (qpc) and the bitrate it applied. The LAST one
  // seen (a run has one downshift). 0 = none logged.
  uint64_t wireCapAppliedQpcUs = 0;
  uint64_t wireCapAppliedBitrate = 0;
  // r4 R6 V1 (Codex 72a22d2): the authoritative SENT-side 2s window. burstWin2s is the host ledger's
  // max committed A(t-2s,t] over the run (max_window_bytes); cap2r is 2r at the logged rate. The TX
  // burst contract is burstWin2s <= cap2r (+ one datagram), checked independent of the RX arrival trace.
  uint64_t maxBurstWin2s = 0, cap2r = 0;
};

HostAccount read_host_log(const std::wstring& path) {
  HostAccount a;
  std::ifstream in(path);
  std::string line;
  std::string last;
  while (std::getline(in, line)) {
    if (line.find(" fecSingleChunkStride=") != std::string::npos) a.layoutToken = value_of(line, "fecSingleChunkStride");
    if (line.find(" udpTxWireEstBytes=") != std::string::npos) {
      last = line;
      ++a.statsLines;
    }
    if (line.find("[keyframe]") != std::string::npos) {
      ++a.keyframeLines;
      const std::string reasons = value_of(line, "reasons");
      if (reasons.find("tune") != std::string::npos && reasons.find("encoder_target") == std::string::npos) ++a.keyTuneOnlyLines;
    }
    if (line.find(" wire seq=") != std::string::npos) {
      a.maxQueueWaitUs = std::max(a.maxQueueWaitUs, num_of(line, "queueWaitUs"));
      a.maxQueueDepth = std::max(a.maxQueueDepth, num_of(line, "queueDepth"));
      if (line.find(" burstWin2s=") != std::string::npos)
        a.maxBurstWin2s = std::max(a.maxBurstWin2s, num_of(line, "burstWin2s"));
      if (line.find(" cap2r=") != std::string::npos) a.cap2r = std::max(a.cap2r, num_of(line, "cap2r"));
    }
    if (line.find(" wireCapBps=") != std::string::npos && a.wireCapBps == 0) a.wireCapBps = num_of(line, "wireCapBps");
    if (line.find(" wireOverloadSkips=") != std::string::npos) a.wireOverloadSkips = num_of(line, "wireOverloadSkips");
    if (line.find(" oldestSubmittingUs=") != std::string::npos) a.captureStallMaxUs = std::max(a.captureStallMaxUs, num_of(line, "oldestSubmittingUs"));
    if (line.find("wirecap applied ") != std::string::npos) {  // r5 G4 same-clock apply marker
      a.wireCapAppliedQpcUs = num_of(line, "appliedQpcUs");
      a.wireCapAppliedBitrate = num_of(line, "bitrate");
      a.wireCapApplies.push_back({num_of(line, "bitrate"), num_of(line, "appliedQpcUs")});  // every one, in order (r2 V1)
    }
    // encoder-rc-budget C1-C4: the host's own account of R vs E, the plan per instance, and every
    // runtime rate-control write it made.
    if (line.find(" encoder-budget requestedWire=") != std::string::npos) {
      a.requestedWire = num_of(line, "requestedWire");
      a.effectiveWire = num_of(line, "effectiveWire");
      a.desiredEncoder = num_of(line, "desiredEncoder");
      a.appliedEncoder = num_of(line, "appliedEncoder");
      a.rcProfile = value_of(line, "profile");
      a.budgetActions.push_back(value_of(line, "action"));
    }
    if (line.find(" h264 rc-identity instance=") != std::string::npos) ++a.rcInstances;
    if (line.find(" h264 rc-plan stage=pre-types") != std::string::npos) ++a.rcPlanPreTypes;
    if (line.find(" h264 rc-plan stage=post-types") != std::string::npos) {
      ++a.rcPlanPostStart;
      a.rcPlanStates.push_back(value_of(line, "state"));
      a.rcPlanMeans.push_back(num_of(line, "mean"));
    }
    if (line.find(" h264 rate-control reason=reconfigure") != std::string::npos) ++a.rcReconfigureWrites;
    if (line.find(" h264 rate-control reason=governor") != std::string::npos) ++a.rcGovernorWrites;
    if (line.find(" h264 rate-control reason=init") != std::string::npos) ++a.rcInitWrites;
    if (line.find("[rate-governor] maxQp ") != std::string::npos) ++a.governorQpSteps;
    if (line.find("[rate-governor] fps ") != std::string::npos) ++a.governorFpsSteps;
    if (line.find("[keyframe]") != std::string::npos) {
      // the AU size of each key the host sent, for the "big IDR" question
      const uint64_t bytes = num_of(line, "bytes");
      if (bytes) a.keyBytes.push_back(bytes);
    }
    if (line.find("deferred retarget applied") != std::string::npos) ++a.deferredRetargets;
    if (line.find(" bitrateTarget=") != std::string::npos) {  // the stats line: the encode size in force
      const std::string sz = value_of(line, "size");
      if (!sz.empty() && (a.sizes.empty() || a.sizes.back() != sz)) a.sizes.push_back(sz);
    }
  }
  if (!last.empty()) {
    a.txFrames = num_of(last, "udpTxFrames");
    a.txBytes = num_of(last, "udpTxBytes");
    a.parity = num_of(last, "udpTxParityBytes");
    a.chunkHdr = num_of(last, "udpTxChunkHdrBytes");
    a.videoDatagrams = num_of(last, "udpTxVideoDatagrams");
    a.nackBytes = num_of(last, "udpTxNackBytes");
    a.nackDatagrams = num_of(last, "udpTxNackDatagrams");
    a.wireEst = num_of(last, "udpTxWireEstBytes");
  }
  return a;
}

// ------------------------------------------------------------------------------- one run

struct RunResult {
  bool launched = false;
  // encoder-rc-budget r6: how the host ended. A host that exits during the live window (the Home PC:
  // "capture item create failed", exit 8, 0.4 s after start) used to be waited out for the full run
  // and then failed every downstream check with zeros; now the run stops at once and says why.
  bool hostExitedEarly = false;
  unsigned long hostExitCode = 0;
  bool captureItemFailed = false;  // host.log: "capture item create failed" (WGC CreateForWindow refused)
  bool monitorFallbackSeen = false;  // host.log: "capture-window target not found; fallback=monitor" -- the user's screen
  std::string hostLastLine;          // the last host.log line, the exit reason in the host's words
  std::string windowMode;            // fixture window mode this run used (invisible / visible-plain)
  std::string probeHr;               // the harness's own WGC CreateForWindow HRESULT for that mode (wgc-probe)
  std::string hangDump;              // r8: path of the minidump taken when the host stopped delivering (empty = none)
  bool hostExitedBeforeCleanup = false;  // r8: dead when the live window ended (its own exit, e.g. watchdog 43), not our TerminateProcess
  uint64_t fixturePaints = 0;            // r8: WM_PAINTs the fixture handled during the run
  double fixtureLastPaintAgeMs = 0;      // r8: age of the last fixture paint at the live window's end
  bool hello = false;
  bool nackNegotiated = false;
  HostAccount host;
  // This end.
  uint64_t rxData = 0, rxParity = 0, rxHeader = 0, rxReplay = 0, rxVideoDatagrams = 0, rxDropped = 0;
  uint32_t delivered = 0, firstSeq = 0, lastSeq = 0, gaps = 0, maxGap = 0, keyReq = 0, disc = 0, nacks = 0,
           malformed = 0, fec = 0, giveUps = 0, singleChunkFrames = 0, keyFrames = 0;
  // Sequence gaps the pipeline saw closed by a complete keyframe (the host's own doing: an IDR
  // supersedes the sender's backlog, whose seqs are never sent -- respond_to_sequence_gap).
  uint32_t gapsBehindKey = 0;
  uint32_t decoded = 0, decodeErrors = 0;
  std::string decoder;
  // Isolation-matrix instrumentation (bitrate-hard-cap r2, completion criterion 3).
  std::vector<std::pair<uint64_t, uint32_t>> wireEvents;  // (arrival qpc us, bytes incl. +28)
  std::vector<uint64_t> deliverUs;        // when each AU was delivered (qpc)
  std::vector<uint64_t> captureStampUs;   // its header captureQpcUs (latency, same clock domain)
  std::vector<uint8_t> deliverWasKey;     // 1 if the delivered AU was a keyframe
  uint32_t appliedBitrate = 0, appliedFps = 0;
  uint64_t switchUs = 0;  // when the runtime bitrate downshift was sent (0 = none)
  std::vector<uint64_t> tuneSentUs;  // every scheduled runtime tune the control thread sent (qpc)
  uint64_t recoveryMaxUs = 0;  // max time from a discontinuity to the next delivered keyframe
  uint32_t recoveryCount = 0;
  // Realtime decode (r4 addendum): a decoder thread consumes AUs AS THEY ARE DELIVERED (not post-hoc),
  // so these are present-ready timings -- when each frame actually finished decoding, and the span
  // from its capture to that moment. This is what a viewer would actually see keep up (or not).
  std::vector<uint64_t> decodeOutUs;      // qpc when each frame finished decoding, in order
  std::vector<uint64_t> decodeLatencyUs;  // decodeOut - captureStamp per frame
  uint32_t realtimeDecoded = 0;           // frames the realtime decoder produced
  uint64_t liveEndUs = 0;                 // qpc when the receive loop was stopped; decodes AFTER this
                                          // are the post-stop DRAIN, excluded from the present-latency
                                          // stat so a frame decoded during teardown does not inflate it.
  // Event-matched change->decoded-picture latency (r6 H3): per event, decodeOut - paint QPC, only for
  // the FIRST decoded frame carrying that event-ID marker. eventsExpected = ids painted; eventsMatched
  // = ids whose marker was decoded. This is the real single-change / full-transition response.
  std::vector<uint64_t> eventLatencyUs;
  uint32_t eventsExpected = 0, eventsMatched = 0;
  uint32_t eventsOwed = 0;  // ids painted >= 1 s before the live window closed (the rc gates' definition)
  std::wstring frameDir;    // where --save-frames writes decoded event frames (the run directory)
  // Per-event detail for the V1 paired table: id, paint QPC, first decoded-marker QPC, latency.
  struct EventRec { uint32_t id = 0; uint64_t paintUs = 0, decodedUs = 0, latencyUs = 0; };
  std::vector<EventRec> eventRecs;
  // r7 V2: the real PC VideoReceiver/FrameGate smoke path -- keyframe requests FrameGate made (the
  // "no unnecessary repeated keyframe" check) and the IDRs that actually decoded through the gate.
  uint32_t v2KeyframeRequests = 0;
  uint32_t v2IdrDecoded = 0;
  // encoder-rc-budget C5 quality: PSNR of the first decoded frame of each event against the page as
  // painted for that event (host_e2e_marker_fixture psnr_against_ref) -- whole picture and text rows.
  std::vector<double> psnrFull, psnrText;
  uint32_t psnrDecodedW = 0, psnrDecodedH = 0;  // the decoded visible size the PSNR was taken at
};

// A scheduled runtime tune the harness's control thread sends at `atMs` after the control thread's
// start (encoder-rc-budget C5: down/up shifts and an fps transition in one run). Millisecond steps
// (r4 H1): the E-only schedule must land INSIDE and OUTSIDE the host's real 2 s coalescing interval
// on purpose, which whole seconds cannot place. bitrate 0 / fps 0 = that field not sent.
struct TuneStep { uint32_t atMs = 0; uint32_t bitrate = 0; uint32_t fps = 0; };

// The owed event ids (1..eventsOwed) that were never decoded. Ids are allocated in paint order, so the
// events painted early enough to be owed are exactly 1..owed; a decoded id above owed is a late bonus
// and never stands in for a missing one (r2, Codex V1).
std::vector<uint32_t> missing_owed_ids(const RunResult& r) {
  std::set<uint32_t> seen;
  for (const auto& e : r.eventRecs) seen.insert(e.id);
  std::vector<uint32_t> missing;
  for (uint32_t id = 1; id <= r.eventsOwed; ++id)
    if (!seen.count(id)) missing.push_back(id);
  return missing;
}

// --save-frames (r3, the Home bundle): the first decoded frame of each event is written as a PGM (its
// visible Y plane) next to the run's host.log, so a person can look at the actual text the viewer got
// and compare it with the painted page -- the PSNR is a number, this is the picture. Only with
// --keep-dir, since the run directory is otherwise removed.
bool gSaveFrames = false;
// r10 (Codex H2): the saved frames of a run, read back -- how many, their size, and whether the picture has
// ink (dark pixels, i.e. text) rather than a blank page. Printed per marker run so the bundle's PGM
// material is checked by content, not by file existence.
struct PgmSummary { unsigned files = 0; unsigned w = 0, h = 0; double darkPct = 0; std::wstring first; };
PgmSummary summarize_pgms(const std::wstring& dir) {
  PgmSummary ps;
  WIN32_FIND_DATAW fd{};
  HANDLE hf = FindFirstFileW((dir + L"event_*.pgm").c_str(), &fd);
  if (hf == INVALID_HANDLE_VALUE) return ps;
  do {
    ++ps.files;
    if (ps.first.empty()) {
      ps.first = dir + fd.cFileName;
      FILE* fp = _wfopen(ps.first.c_str(), L"rb");
      if (fp) {
        unsigned w = 0, h = 0, maxv = 0;
        if (std::fscanf(fp, "P5 %u %u %u", &w, &h, &maxv) == 3 && w && h) {
          std::fgetc(fp);  // the single whitespace after maxval
          std::vector<uint8_t> px(static_cast<size_t>(w) * h);
          const size_t got = std::fread(px.data(), 1, px.size(), fp);
          size_t dark = 0;
          for (size_t i = 0; i < got; ++i) if (px[i] < 96) ++dark;
          ps.w = w; ps.h = h; ps.darkPct = got ? 100.0 * dark / got : 0;
        }
        std::fclose(fp);
      }
    }
  } while (FindNextFileW(hf, &fd));
  FindClose(hf);
  return ps;
}
void save_frame_pgm(const std::wstring& dir, uint32_t id, const DecodedFrameNv12& f) {
  if (dir.empty() || !f.bytes.data()) return;
  const uint32_t visW = f.visibleWidth ? f.visibleWidth : f.width;
  const uint32_t visH = f.visibleHeight ? f.visibleHeight : f.height;
  if (visW == 0 || visH == 0 || f.visibleLeft + visW > f.width || f.visibleTop + visH > f.height) return;
  const std::wstring path = dir + L"event_" + std::to_wstring(id) + L"_" + std::to_wstring(visW) + L"x" + std::to_wstring(visH) + L".pgm";
  FILE* fp = _wfopen(path.c_str(), L"wb");
  if (!fp) return;
  std::fprintf(fp, "P5\n%u %u\n255\n", visW, visH);
  for (uint32_t y = 0; y < visH; ++y)
    std::fwrite(f.bytes.data() + static_cast<size_t>(f.visibleTop + y) * f.width + f.visibleLeft, 1, visW, fp);
  std::fclose(fp);
}

// The quality sample for one decoded frame carrying event `id` (first decode of that id only).
void note_psnr(RunResult& r, EventLog& events, uint32_t id, const DecodedFrameNv12& f) {
  if (gSaveFrames) save_frame_pgm(r.frameDir, id, f);
  EventLog::RefY ref;
  if (!events.Ref(id, &ref)) return;
  const uint32_t visW = f.visibleWidth ? f.visibleWidth : f.width;
  const uint32_t visH = f.visibleHeight ? f.visibleHeight : f.height;
  const PsnrResult p = psnr_against_ref(f.bytes.data(), f.width, f.height, f.visibleLeft, f.visibleTop, visW, visH, ref);
  if (!p.ok) return;
  r.psnrFull.push_back(p.full);
  r.psnrText.push_back(p.text);
  r.psnrDecodedW = visW;
  r.psnrDecodedH = visH;
}

uint64_t fnv1a_u64(uint64_t h, uint64_t v) {
  const uint8_t* b = reinterpret_cast<const uint8_t*>(&v);
  for (size_t i = 0; i < sizeof(v); ++i) {
    h ^= b[i];
    h *= 1099511628211ull;
  }
  return h;
}

// The host's log while it may still be running (shared read; its stdout is block-buffered, so a line
// shows up here only once flushed -- always at exit, maybe earlier). Used for the early-exit reason
// and for the one line that must stop a run at once: the monitor fallback.
bool host_log_contains(const std::wstring& path, const char* needle) {
  std::ifstream in(path);
  std::string line;
  while (std::getline(in, line)) if (line.find(needle) != std::string::npos) return true;
  return false;
}
std::string host_log_last_line(const std::wstring& path) {
  std::ifstream in(path);
  std::string line, last;
  while (std::getline(in, line)) {
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ')) line.pop_back();
    if (!line.empty()) last = line;
  }
  return last;
}
// The saved PGM frames of a run whose host captured the MONITOR instead of our window would be the
// user's screen: they are removed, and the removal is printed.
void delete_saved_frames(const std::wstring& runDir) {
  WIN32_FIND_DATAW fd{};
  HANDLE h = FindFirstFileW((runDir + L"event_*.pgm").c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE) return;
  unsigned n = 0;
  do { if (DeleteFileW((runDir + fd.cFileName).c_str())) ++n; } while (FindNextFileW(h, &fd));
  FindClose(h);
  std::printf("monitor-fallback guard: removed %u saved frame(s) from %s\n", n, std::string(runDir.begin(), runDir.end()).c_str());
}

// encoder-rc-budget r8: the Home PC's hosts (candidate AND baseline) stopped mid-run with the watchdog's
// "main-loop hang phase=Loop" and nothing else to go on -- the phase says only "somewhere in the loop".
// So when a host is alive but has delivered nothing for kHangDumpAfterUs (the first frame, or the next
// one), the harness writes a minidump of the HOST process into the run directory before the host's own
// watchdog (10 s) kills it: thread stacks, handle data, unloaded modules -- a few MB, not full memory.
// Open it with the matching PDB (the exe hash is in identity.txt) in WinDbg/cdb: `!analyze -v` /
// `~*k`. Test-support only; it needs no product change and takes nothing from a healthy run.
constexpr uint64_t kHangDumpAfterUs = 5'000'000;
std::string write_hang_dump(HANDLE process, DWORD pid, const std::wstring& runDir, const char* why, bool fullMemory = false) {
  wchar_t name[64];
  std::swprintf(name, 64, fullMemory ? L"hang-pid%lu-full.dmp" : L"hang-pid%lu.dmp", static_cast<unsigned long>(pid));
  const uint64_t t0 = qpc_now_us();
  const std::wstring path = runDir + name;
  HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    std::printf("HANG-DUMP: could not create %s (gle=%lu)\n", std::string(path.begin(), path.end()).c_str(), GetLastError());
    return std::string();
  }
  // fast: stacks + thread info + handles + unloaded modules + what the stacks point at; the module headers
  // (r11) so a reader on another machine can identify ntdll/d3d11 builds without their images.
  // full: everything, for lock owners and the objects behind the stacks (one per run, see the caller).
  const MINIDUMP_TYPE type = fullMemory
                                 ? static_cast<MINIDUMP_TYPE>(MiniDumpWithFullMemory | MiniDumpWithHandleData | MiniDumpWithThreadInfo |
                                                              MiniDumpWithUnloadedModules | MiniDumpWithFullMemoryInfo)
                                 : static_cast<MINIDUMP_TYPE>(MiniDumpWithThreadInfo | MiniDumpWithHandleData | MiniDumpWithUnloadedModules |
                                                              MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithModuleHeaders);
  const BOOL ok = MiniDumpWriteDump(process, pid, file, type, nullptr, nullptr, nullptr);
  const DWORD gle = ok ? 0 : GetLastError();
  LARGE_INTEGER size{};
  GetFileSizeEx(file, &size);
  CloseHandle(file);
  std::printf("HANG-DUMP: %s -> %s (%s, %lld bytes, took %.0f ms%s, gle=%lu)\n", why, std::string(path.begin(), path.end()).c_str(),
              ok ? "written" : "FAILED", static_cast<long long>(size.QuadPart), (qpc_now_us() - t0) / 1000.0, fullMemory ? ", FULL memory" : "", gle);
  return ok ? std::string(path.begin(), path.end()) : std::string();
}
// Test-support only: REMOTE60_E2E_HANG_DUMP_AFTER_MS=<n> forces a dump n ms into the live window of
// every run even though frames flow -- to prove the dump path on a machine whose hosts do not hang.
const char* gCurrentHostTag = "";  // r10: "cand" / "base" while a matrix pair runs (the forced dump can target one side)
uint64_t forced_hang_dump_after_us() {
  char buf[32]{};
  const DWORD n = GetEnvironmentVariableA("REMOTE60_E2E_HANG_DUMP_AFTER_MS", buf, sizeof(buf));
  if (n == 0 || n >= sizeof(buf)) return 0;
  // REMOTE60_E2E_HANG_DUMP_HOST=base|cand restricts the forced dump to that side of a matrix pair (test-support)
  char only[16]{};
  const DWORD m = GetEnvironmentVariableA("REMOTE60_E2E_HANG_DUMP_HOST", only, sizeof(only));
  if (m != 0 && m < sizeof(only) && std::strcmp(only, gCurrentHostTag) != 0) return 0;
  return static_cast<uint64_t>(std::strtoull(buf, nullptr, 10)) * 1000ull;
}

// r11 (Home4): every host -- baseline, original, r9 -- stopped logging 1-2 s after start and the main thread
// sat in ntdll under the stats write; the fourth run's deltas against the third were a LOCAL %TEMP% host.log
// (the third wrote to Z:), the harness's 200 ms host.log polling, the 3 s low-fps dump and --save-frames.
// These knobs let one short run per combination say which of them matters (run.cmd home5 / H0):
//   --host-log nul|run|dir:<path>   where the host's stdout goes (NUL device / the run dir [default] / a dir)
//   --no-log-poll                   do not read host.log while the host runs (the monitor-fallback scan)
//   --no-dump                       no hang dumps at all
//   --low-fps-dump                  re-enable the 3 s low-fps trigger (OFF by default since r11: a host
//                                   that still delivers is not dumped; silence >= 5 s is the trigger)
enum class HostLogDest { RunDir, Nul, Dir };
HostLogDest gHostLogDest = HostLogDest::RunDir;
std::wstring gHostLogDir;
bool gNoLogPoll = false;
bool gNoDump = false;
bool gLowFpsDump = false;
int gFullDumps = 0;  // full-memory dumps written by this harness process (cap 3)

int gCaptureUnavailableRuns = 0;  // runs whose capture item could not be created even after the fallback
bool capture_unavailable() { return gCaptureUnavailableRuns > 0; }
// r10 (Codex H1): a matrix phase that runs the ORIGINAL candidate to collect its stall evidence. An invalid
// candidate row there is the expected material: counted as DIAGNOSTIC-COLLECTED (exit 4 -- not success,
// not a product failure) instead of a FAIL. Baseline validity and the candidate's own wire checks still
// apply. Without this flag every invalid row is a FAIL.
bool gDiagnosticMode = false;
int gDiagnosticCollected = 0;
// r11: --rc-observe runs a cell for its liveness only (the H0 log-destination set runs the BASELINE host,
// which has none of the candidate's rc/E lines): the row is printed, the host-exit/host-log lines are the
// result, and the only check is that the row is a valid measurement (alive, frames, no stall, no dump).
bool gObserveMode = false;

// r7: the harness asks WGC itself, once per window mode, on a window built exactly as the run would
// build it. The HRESULT is the OS's own answer (the host's log discards it). A simulated refusal
// (REMOTE60_E2E_FIXTURE_PROBE=simulate) is marked as such in the line.
struct WgcProbe { bool done = false; HRESULT hr = S_OK; bool simulated = false; };
WgcProbe gWgcProbe[3];
WgcProbe& wgc_probe_slot(FixtureWindowMode m) { return gWgcProbe[static_cast<int>(m)]; }
std::string hr_hex(HRESULT hr) {
  char b[16];
  std::snprintf(b, sizeof(b), "0x%08lx", static_cast<unsigned long>(hr));
  return b;
}
const WgcProbe& wgc_probe_mode(FixtureWindowMode mode) {
  WgcProbe& pr = wgc_probe_slot(mode);
  if (pr.done) return pr;
  const FixtureWindowMode saved = gFixtureWindowMode;
  gFixtureWindowMode = mode;
  Target probeWindow;
  probeWindow.title = L"remote60 fec single-chunk wgc-probe " + std::to_wstring(GetCurrentProcessId());
  const bool up = probeWindow.Start();
  const bool simulate = fixture_probe().style == "simulate" && fixture_probe_applies();
  if (!up) pr.hr = E_FAIL;
  else if (simulate) { pr.hr = E_INVALIDARG; pr.simulated = true; }
  else pr.hr = remote60::native_poc::e2e::wgc_probe_create_for_window(probeWindow.hwnd());
  std::printf("wgc-probe: mode=%s hwnd=0x%llx hr=%s%s%s\n", fixture_window_mode_name(mode),
              static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(probeWindow.hwnd())), hr_hex(pr.hr).c_str(),
              SUCCEEDED(pr.hr) ? " (accepted)" : " (REFUSED)", pr.simulated ? " [simulated by REMOTE60_E2E_FIXTURE_PROBE]" : "");
  probeWindow.Stop();
  gFixtureWindowMode = saved;
  pr.done = true;
  return pr;
}

RunResult run_host_once(const std::wstring& hostExe, const std::wstring& runDir, Content content, bool tight,
                        uint16_t port, int seconds, uint32_t lossPermille, uint32_t lossSeed, bool wireCapOn,
                        uint32_t bitrate, uint32_t fps, uint32_t downshiftBitrate, uint32_t downshiftAtSec,
                        bool v2FrameGate, const std::vector<TuneStep>* tunes) {
  RunResult r;
  r.windowMode = fixture_window_mode_name();
  {
    const WgcProbe& pr = wgc_probe_slot(gFixtureWindowMode);
    r.probeHr = pr.done ? hr_hex(pr.hr) + (pr.simulated ? "(simulated)" : "") : "not-probed";
  }
  CreateDirectoryW(runDir.c_str(), nullptr);
  r.frameDir = runDir;
  const std::wstring me = self_path();
  const bool staged = CopyFileW(hostExe.c_str(), (runDir + L"GNLinkStream.exe").c_str(), FALSE) &&
                      CopyFileW(me.c_str(), (runDir + L"GNLinkCapture.exe").c_str(), FALSE);
  check(staged, "the host could be staged into the run directory", std::string(hostExe.begin(), hostExe.end()));
  if (!staged) return r;

  EventLog eventLog;  // r6 H3: paint-event ids/QPCs, shared with the decode thread
  Target target;
  target.events = &eventLog;
  target.content = content;
  target.title = L"remote60 fec single-chunk target " + std::to_wstring(GetCurrentProcessId()) + L" " +
                 std::wstring(tight ? L"tight" : L"padded");
  check(target.Start(), gFixtureWindowMode == FixtureWindowMode::VisiblePlain
                            ? "an on-screen frameless VISIBLE window (window-mode=visible-plain, a taskbar window) is up for the host to capture"
                            : gFixtureWindowMode == FixtureWindowMode::VisibleFramed
                                  ? "an on-screen captioned VISIBLE window (window-mode=visible-framed, LAST RESORT: client crop jitters, geometry "
                                    "checks are not a measurement in this mode) is up for the host to capture"
                                  : "an on-screen, invisible, click-through window is up for the host to capture");
  const HWND targetHwnd = target.hwnd();

  HANDLE job = CreateJobObjectW(nullptr, nullptr);
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));

  std::wstring hostLogPath = runDir + L"host.log";
  if (gHostLogDest == HostLogDest::Dir && !gHostLogDir.empty()) {
    // the run's own file name keeps the logs apart when every run of a phase shares one directory
    std::wstring tag = runDir;
    while (!tag.empty() && tag.back() == L'\\') tag.pop_back();
    const size_t cut = tag.find_last_of(L"\\/");
    hostLogPath = gHostLogDir + L"\\host_" + (cut == std::wstring::npos ? tag : tag.substr(cut + 1)) + L".log";
  }
  HANDLE hostLog = INVALID_HANDLE_VALUE;
  PROCESS_INFORMATION hostPi{};
  {
    SetEnvironmentVariableW(L"REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE", L"1");
    SetEnvironmentVariableW(L"REMOTE60_NATIVE_STATS_PRINT_EVERY_SEC", L"1");
    SetEnvironmentVariableW(L"REMOTE60_NATIVE_FEC_SINGLE_CHUNK_STRIDE", tight ? L"1" : L"0");
    // The FEC-layout runs pin the wire cap OFF (this test's original purpose is the parity layout,
    // and the cap paces a large AU over its send time, which is orthogonal). A separate cap-ON
    // lossless run below asserts "lossless -> premature NACK 0" so the pin does not erase the cap-ON
    // evidence that the r2 progress-based tail fix holds on real hardware. (bitrate-hard-cap r2.)
    SetEnvironmentVariableW(L"REMOTE60_NATIVE_WIRE_CAP", wireCapOn ? L"1" : L"0");
    std::wstring cmd = L"\"" + runDir + L"GNLinkStream.exe\" --transport udp --codec h264" +
                       L" --bind-address 127.0.0.1 --bind-port " + std::to_wstring(port) + L" --fps " +
                       std::to_wstring(fps) + L" --bitrate " + std::to_wstring(bitrate) + L" --seconds " +
                       std::to_wstring(seconds + 30) + L" --input-injection-mode none" +
                       remote60::native_poc::e2e::e2e_capture_window_args(target.title) +
                       (gFixtureWindowMode == FixtureWindowMode::VisibleFramed ? L" --capture-window-client-only" : L"");
    const bool noInjection = cmd.find(L"--input-injection-mode none") != std::wstring::npos;
    check(noInjection, "the host is started with input injection off");
    std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
    mutableCmd.push_back(L'\0');
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    hostLog = gHostLogDest == HostLogDest::Nul
                  ? CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr)
                  : CreateFileW(hostLogPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    std::printf("host-log: dest=%s path=%s poll=%d dump=%s\n",
                gHostLogDest == HostLogDest::Nul ? "nul" : gHostLogDest == HostLogDest::Dir ? "dir" : "run-dir",
                gHostLogDest == HostLogDest::Nul ? "NUL" : std::string(hostLogPath.begin(), hostLogPath.end()).c_str(), gNoLogPoll ? 0 : 1,
                gNoDump ? "off" : (gLowFpsDump ? "silence>=5s+lowfps" : "silence>=5s"));
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    if (hostLog != INVALID_HANDLE_VALUE) {
      si.dwFlags = STARTF_USESTDHANDLES;
      si.hStdOutput = hostLog;
      si.hStdError = hostLog;
    }
    const std::wstring isoAppData = runDir + L"localappdata";
    CreateDirectoryW(isoAppData.c_str(), nullptr);
    std::vector<wchar_t> isoEnv = e2e_isolated_environment(isoAppData);
    std::string isoWhy;
    const bool isoOk = e2e_command_is_isolated(cmd, runDir, &isoWhy) &&
                       e2e_path_is_under(e2e_block_localappdata(isoEnv), runDir);
    check(isoOk, "the launched host is isolated from the user's files", isoWhy);
    r.launched = noInjection && isoOk &&
                 CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, hostLog != INVALID_HANDLE_VALUE,
                                CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT, isoEnv.data(),
                                runDir.c_str(), &si, &hostPi) != 0;
    if (r.launched) {
      AssignProcessToJobObject(job, hostPi.hProcess);
      ResumeThread(hostPi.hThread);
    }
  }
  check(r.launched, "the host started");

  if (r.launched) {
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in hostAddr{};
    hostAddr.sin_family = AF_INET;
    hostAddr.sin_port = htons(port);
    InetPtonW(AF_INET, L"127.0.0.1", &hostAddr.sin_addr);
    connect(sock, reinterpret_cast<const sockaddr*>(&hostAddr), sizeof(hostAddr));
    DWORD rcvTimeout = 10;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&rcvTimeout), sizeof(rcvTimeout));
    int rcvBuf = 4 << 20;
    setsockopt(sock, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&rcvBuf), sizeof(rcvBuf));

    UdpHelloOptions hello;
    hello.budgetMs = 20000;
    hello.sliceMaxMs = 250;
    hello.retrySleepMs = 50;
    hello.requestNack = true;  // the Windows viewer and the session both ask for NACK
    std::string helloError;
    uint32_t ackFeatures = 0;
    r.hello = udp_hello_handshake(sock, hello, nullptr, &helloError, &ackFeatures, nullptr);
    check(r.hello, "the viewer's Hello is answered", helloError);
    r.nackNegotiated = (ackFeatures & kUdpFeatureVideoNack) != 0;

    UdpControlChannel control;
    control.Configure(
        [&sock](const void* data, size_t len) {
          return send(sock, static_cast<const char*>(data), static_cast<int>(len), 0) > 0;
        },
        kUdpControlStreamClientToHost, kUdpControlStreamHostToClient, 1200);
    UdpControlLink link(&control, 12000);

    // The product's receive policy, fed from this socket.
    std::mutex pmu;
    std::atomic<bool> pendingKeyRequest{false};
    uint32_t lastDelivered = 0;
    uint64_t lastDiscUs = 0;  // for recovery-time: a discontinuity awaiting its next delivered key
    // Realtime decode (r4 addendum): a decoder thread consumes AUs as they are delivered, so the
    // fps/latency/freeze reflect what a viewer would actually see keep up -- not a post-hoc batch.
    std::mutex dmu;
    std::condition_variable dcv;
    bool dstop = false;
    std::deque<std::pair<std::vector<uint8_t>, std::pair<bool, uint64_t>>> dq;  // payload, (key, captureStamp)

    // r7 V2: the real PC receive gate. When v2FrameGate, each assembled AU goes through the real
    // FrameGate (admit -> gate verdict), and on Decode is decoded INLINE on the ingress thread (as the
    // viewer's recv thread does), then note_decode_ok/note_reference_sync/clear_empty_streak; fg.tick()
    // runs after every datagram and timeout to drive the recovery (keyframe re-ask) timer. The sink
    // counts keyframe requests and routes them to the same control path. (SessionVideoPipeline still
    // does the assembly + NACK below; FrameGate is the pre-decode gate layered on its delivery.)
    using viewer::FrameGate;
    using viewer::FrameGateInputs;
    using viewer::FrameGateLag;
    using viewer::FrameGateState;
    using viewer::FrameGateVerdict;
    using viewer::RecvStats;
    FrameGateState v2gate;
    RecvStats v2st;
    std::unique_ptr<H264Decoder> v2dec;
    uint64_t v2PresentedCapUs = 0;
    int64_t v2stamp = 10'000'000;
    std::set<uint32_t> v2seen;
    struct V2Sink : viewer::FrameGateSink {
      RunResult* r = nullptr;
      std::atomic<bool>* pending = nullptr;
      void reset_decoder() override {}
      bool rebuild_decoder() override { return true; }
      void request_keyframe(uint16_t reason) override {
        (void)reason;
        ++r->v2KeyframeRequests;
        pending->store(true);
      }
    } v2sink;
    v2sink.r = &r;
    v2sink.pending = &pendingKeyRequest;
    FrameGate v2fg(v2gate, v2st, v2sink);
    if (v2FrameGate) {
      v2gate.catchupReenterMinIntervalUs = viewer::kCatchupReenterMinIntervalUsDefault;
      v2gate.staleCaptureDropUs = viewer::kStaleCaptureDropUs;
      v2gate.staleReferenceRecoveryMinIntervalUs = viewer::kStaleRecoveryMinIntervalUsDefault;
      v2gate.congestionRecoverMinUs = viewer::kCongestionRecoverMinUsDefault;
      v2gate.congestionRecoveryTimeoutUs = viewer::kCongestionRecoveryTimeoutUsDefault;
      v2gate.decodeQueueLagDropUs = viewer::kDecodeQueueLagDropUs;
      v2gate.catchupLagDropUs = viewer::kCatchupLagDropUs;
      v2gate.denseArrivalMaxGapUs = viewer::kDenseArrivalMaxGapUsDefault;
      v2gate.lagTriggerStreakMin = viewer::kLagTriggerStreakMinDefault;
      v2gate.recoveryRetryIntervalUs = viewer::kKeyRecoveryRetryUsDefault;
      v2gate.recoveryRetryMaxIntervalUs = viewer::kKeyRecoveryRetryMaxUsDefault;
      v2gate.recoveryRetryDeferMaxUs = viewer::kKeyRecoveryDeferMaxUsDefault;
      v2gate.frameIntervalUs = 1'000'000ULL / (fps ? fps : 60);
      v2gate.waitForKeyFrame = true;  // an H.264 session starts waiting for its first IDR
    }

    SessionVideoPipelineConfig cfg;
    cfg.nackEnabled = r.nackNegotiated;
    cfg.holdUs = r.nackNegotiated ? 120000 : 0;
    cfg.maxConcurrent = 8;
    SessionVideoPipeline::Callbacks cb;
    std::atomic<uint64_t> lastDeliverUs{0};  // r8: when the host last delivered a frame (the hang detector's clock)
    std::atomic<uint32_t> deliveredAtomic{0};  // r9: the live-loop's copy of r.delivered (no data race with the ingress thread)
    std::atomic<uint64_t> deliveredBytesAtomic{0};  // r9: payload bytes delivered, for the starvation trigger's "idle wire" test
    cb.deliver = [&](UdpH264AssembledFrame&& f) {
      ++r.delivered;
      deliveredAtomic.fetch_add(1, std::memory_order_relaxed);
      deliveredBytesAtomic.fetch_add(f.payload.size(), std::memory_order_relaxed);
      lastDeliverUs.store(qpc_now_us(), std::memory_order_relaxed);
      if (r.firstSeq == 0) r.firstSeq = f.header.seq;
      if (lastDelivered != 0 && f.header.seq > lastDelivered + 1) {
        ++r.gaps;
        r.maxGap = std::max(r.maxGap, f.header.seq - lastDelivered - 1);
      }
      lastDelivered = f.header.seq;
      r.lastSeq = f.header.seq;
      const bool key = (f.header.flags & kEncodedFrameFlagKeyFrame) != 0;
      if (key) ++r.keyFrames;
      const uint64_t deliverNowUs = qpc_now_us();
      r.deliverUs.push_back(deliverNowUs);
      r.captureStampUs.push_back(f.header.captureQpcUs);
      r.deliverWasKey.push_back(key ? 1 : 0);
      if (key && lastDiscUs != 0 && deliverNowUs >= lastDiscUs) {
        r.recoveryMaxUs = std::max(r.recoveryMaxUs, deliverNowUs - lastDiscUs);
        ++r.recoveryCount;
        lastDiscUs = 0;  // recovered
      }
      if (v2FrameGate) {
        // The real PC pre-decode gate, inline on this (ingress) thread.
        FrameGateInputs in{};
        in.captureQpcUs = f.header.captureQpcUs;
        in.sendQpcUs = f.header.sendQpcUs;
        in.seq = f.header.seq;
        in.keyFrame = key;
        in.packetNowUs = deliverNowUs;
        in.recvGapUs = v2fg.note_packet(deliverNowUs, false);
        in.presentedCapUs = v2PresentedCapUs;
        in.decodedCapUs = v2PresentedCapUs;
        FrameGateLag lag{};
        const FrameGateVerdict v = v2fg.admit(in, &lag);
        if (v == FrameGateVerdict::Decode && v2dec) {
          std::vector<DecodedFrameNv12> dout;
          bool ov = false;
          v2stamp += 333333;
          if (v2dec->decode_access_unit(f.payload, key, v2stamp, &dout, &ov)) {
            v2fg.note_decode_ok();
            v2fg.note_reference_sync(in);
            v2fg.clear_empty_streak();
            if (key) ++r.v2IdrDecoded;
            const uint64_t outUs = qpc_now_us();
            v2PresentedCapUs = f.header.captureQpcUs;
            for (const auto& df : dout) {
              if (df.bytes.empty()) continue;
              const uint32_t mid = decode_event_marker(df.bytes.data(), df.width, df.height, df.visibleLeft, df.visibleTop,
                                                       df.visibleWidth ? df.visibleWidth : df.width);
              if (mid != 0 && v2seen.insert(mid).second) {
                const uint64_t paintQpc = eventLog.PaintQpc(mid);
                if (paintQpc != 0 && outUs >= paintQpc) {
                  r.eventLatencyUs.push_back(outUs - paintQpc);
                  r.eventRecs.push_back({mid, paintQpc, outUs, outUs - paintQpc});
                }
                note_psnr(r, eventLog, mid, df);
              }
            }
          } else {
            v2fg.note_decode_failure(in, lag);
          }
        }
        return;  // v2 decodes inline; nothing goes to the realtime decode queue
      }
      {
        std::lock_guard<std::mutex> dlk(dmu);
        dq.emplace_back(std::move(f.payload), std::make_pair(key, f.header.captureQpcUs));
      }
      dcv.notify_one();
    };
    cb.requestKeyframe = [&] {
      ++r.keyReq;
      pendingKeyRequest.store(true);
    };
    cb.discontinuity = [&] { ++r.disc; if (lastDiscUs == 0) lastDiscUs = qpc_now_us(); };
    cb.sendNack = [&](const UdpVideoNackPacket& p) {
      ++r.nacks;
      (void)send(sock, reinterpret_cast<const char*>(&p), sizeof(p), 0);  // to the real host
    };
    SessionVideoPipeline pipeline(cfg, cb);

    // The realtime decode thread is used only by the measurement path; V2 decodes inline on the
    // ingress thread (its own v2dec), so it is not started under v2FrameGate.
    std::thread decodeThread;
    if (!v2FrameGate) decodeThread = std::thread([&] {
      H264Decoder dec;
      if (!dec.initialize(kWinW, kWinH, fps)) {
        r.decoder = "init-failed";
        return;
      }
      r.decoder = dec.backend_name();
      int64_t stamp = 10000000;
      std::set<uint32_t> seenEvents;  // event-ids whose marker was already matched (first-decode only)
      for (;;) {
        std::pair<std::vector<uint8_t>, std::pair<bool, uint64_t>> au;
        {
          std::unique_lock<std::mutex> dlk(dmu);
          dcv.wait(dlk, [&] { return dstop || !dq.empty(); });
          if (dq.empty()) {
            if (dstop) break;
            continue;
          }
          au = std::move(dq.front());
          dq.pop_front();
        }
        std::vector<DecodedFrameNv12> out;
        bool overflow = false;
        stamp += 333333;
        if (!dec.decode_access_unit(au.first, au.second.first, stamp, &out, &overflow)) ++r.decodeErrors;
        const uint64_t outUs = qpc_now_us();
        for (size_t k = 0; k < out.size(); ++k) {
          ++r.realtimeDecoded;
          r.decodeOutUs.push_back(outUs);
          r.decodeLatencyUs.push_back(outUs >= au.second.second ? outUs - au.second.second : 0);
          // r6 H3: read the event-ID marker out of the decoded Y plane and match the FIRST decoded
          // frame of each event to its paint QPC -> a true change->decoded-picture latency.
          const DecodedFrameNv12& f = out[k];
          if (!f.bytes.empty()) {
            const uint32_t id = decode_event_marker(f.bytes.data(), f.width, f.height, f.visibleLeft, f.visibleTop,
                                                    f.visibleWidth ? f.visibleWidth : f.width);
            if (id != 0 && seenEvents.insert(id).second) {
              const uint64_t paintQpc = eventLog.PaintQpc(id);
              if (paintQpc != 0 && outUs >= paintQpc) {
                r.eventLatencyUs.push_back(outUs - paintQpc);
                r.eventRecs.push_back({id, paintQpc, outUs, outUs - paintQpc});
              }
              note_psnr(r, eventLog, id, f);
            }
          }
        }
      }
      dec.shutdown();
    });

    std::atomic<bool> stop{false};
    std::map<uint64_t, uint32_t> occurrences;
    std::thread ingress([&] {
      if (v2FrameGate) {  // the V2 decoder lives on this (recv) thread, like the real viewer
        v2dec = std::make_unique<H264Decoder>();
        if (v2dec->initialize(kWinW, kWinH, fps)) r.decoder = v2dec->backend_name();
        else r.decoder = "init-failed";
      }
      std::vector<uint8_t> buf(2048);
      while (!stop.load()) {
        control.Tick();
        const int n = recv(sock, reinterpret_cast<char*>(buf.data()), static_cast<int>(buf.size()), 0);
        const uint64_t now = qpc_now_us();
        if (n > 0 && !control.OnPacket(buf.data(), static_cast<size_t>(n)) &&
            n >= static_cast<int>(sizeof(UdpVideoChunkHeader))) {
          UdpVideoChunkHeader h{};
          std::memcpy(&h, buf.data(), sizeof(h));
          if (h.magic == kMagic && h.kind == static_cast<uint16_t>(UdpPacketKind::VideoChunk) &&
              h.size == sizeof(UdpVideoChunkHeader)) {
            const bool parity = (h.flags & 0x10u) != 0;
            const uint64_t id = (static_cast<uint64_t>(h.streamGeneration) << 48) ^ (static_cast<uint64_t>(h.seq) << 24) ^
                                (static_cast<uint64_t>(h.chunkIndex) << 1) ^ (parity ? 1u : 0u);
            const uint32_t occ = ++occurrences[id];
            const uint32_t body = static_cast<uint32_t>(n) - static_cast<uint32_t>(sizeof(UdpVideoChunkHeader));
            ++r.rxVideoDatagrams;
            r.rxHeader += sizeof(UdpVideoChunkHeader);
            r.wireEvents.emplace_back(now, static_cast<uint32_t>(n) + 28u);  // IP+UDP for the window math
            if (occ > 1) r.rxReplay += static_cast<uint64_t>(n);
            else if (parity) r.rxParity += body;
            else r.rxData += body;
            if (!parity && h.chunkCount == 1 && occ == 1) ++r.singleChunkFrames;
            bool drop = false;
            if (lossPermille > 0) {
              uint64_t x = fnv1a_u64(0xcbf29ce484222325ull ^ lossSeed, id);
              x = fnv1a_u64(x, occ);
              drop = (x % 1000u) < lossPermille;
            }
            if (drop) {
              ++r.rxDropped;
            } else {
              std::lock_guard<std::mutex> lk(pmu);
              pipeline.OnDatagram(buf.data(), static_cast<size_t>(n), now);
              if (v2FrameGate) v2fg.tick(now);  // recovery timer after every datagram (real recv loop)
            }
            continue;
          }
        }
        std::lock_guard<std::mutex> lk(pmu);
        pipeline.OnTick(now);
        if (v2FrameGate) v2fg.tick(now);  // and on every receive timeout
      }
    });

    // The viewer's control thread: pings keep the session served; keyframe requests go out the
    // way the product sends them.
    std::atomic<bool> controlStop{false};
    std::thread controlThread([&] {
      ClientControlScheduler scheduler;
      WindowPanelStateModel windowPanel;
      StreamStateControl streamState;
      CaptureModeRequestState captureMode;
      KeyframeRequestState keyframe(120000, 300000, 3);
      RuntimeTuneState runtimeTune(300000, 30000000, 250000, 1, 240);
      ClientInputQueue inputQueue;
      scheduler.Reset(kClientControlIntervalMsDefault, qpc_now_us());
      // The schedule of runtime tunes: the legacy single downshift, plus any TuneSteps the caller gave
      // (encoder-rc-budget C5 transitions). Each is sent once at its time; the first one sent stamps
      // switchUs (the downshift analysis), all of them are recorded in tuneSentUs.
      struct Pending { uint64_t atUs; uint32_t bitrate, fps; bool sent; };
      std::vector<Pending> schedule;
      const uint64_t t0 = qpc_now_us();
      if (downshiftBitrate > 0) schedule.push_back({t0 + downshiftAtSec * 1'000'000ull, downshiftBitrate, 0, false});
      if (tunes) for (const TuneStep& s : *tunes) schedule.push_back({t0 + s.atMs * 1'000ull, s.bitrate, s.fps, false});
      uint32_t dseq = 50000;
      while (!controlStop.load()) {
        for (Pending& p : schedule) {
          if (p.sent || qpc_now_us() < p.atUs) continue;
          ControlRuntimeEncoderConfigMessage m{};
          m.header.magic = kMagic;
          m.header.type = static_cast<uint16_t>(MessageType::ControlRuntimeEncoderConfig);
          m.header.size = static_cast<uint16_t>(sizeof(m));
          m.seq = ++dseq;
          m.bitrate = p.bitrate;
          m.fps = p.fps;
          m.flags = (p.bitrate ? 0x1u : 0u) | (p.fps ? 0x4u : 0u);  // bit0 bitrate valid, bit2 fps valid
          m.clientSendQpcUs = qpc_now_us();
          if (link.Write(&m, sizeof(m)) && link.EndMessage()) {
            if (r.switchUs == 0) r.switchUs = qpc_now_us();
            r.tuneSentUs.push_back(qpc_now_us());
            p.sent = true;
          }
        }
        if (pendingKeyRequest.exchange(false)) (void)keyframe.Request(2, qpc_now_us());
        ClientControlMetricsSnapshot metrics{};
        ControlOutboundAction action{};
        const uint64_t now = qpc_now_us();
        if (scheduler.NextAction(now, metrics, &windowPanel, &streamState, &captureMode, &keyframe, &runtimeTune,
                                 &inputQueue, &action, nullptr, nullptr)) {
          TcpControlResponse response;
          if (execute_control_action(link, action, &response) && action.kind == ControlOutboundActionKind::Ping) {
            scheduler.OnPingCompleted(qpc_now_us());
          }
        } else {
          std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
      }
    });

    // The live window. The host is watched, not slept through (r6): a host that exits ends the run
    // now, and the one line that means the host is capturing the USER'S SCREEN (it could not find our
    // window and fell back to the monitor) ends it now too, by terminating the host.
    {
      const uint64_t liveStartUs = qpc_now_us();
      const uint64_t liveUntilUs = liveStartUs + static_cast<uint64_t>(seconds) * 1'000'000ull;
      const uint64_t forcedDumpUs = forced_hang_dump_after_us();
      bool dumped = false;
      // r9: the low-fps progress snapshot. The Home PC's stalled motion runs kept delivering ~1 small AU
      // per second (trailing kicks) after their capture publication stopped, so a 5 s silence never came.
      // Each second: if the fixture painted >= 10 times (a continuous scene -- a discrete one paints
      // every ~2 s and is left alone), <= 1 frame was delivered AND the wire carried under a fifth of R
      // (a saturated low-rate cell also delivers ~1 fps, but at R: that is the gate working, not a
      // stall -- the Home stall delivered 72-byte AUs on an idle wire), for 3 seconds running, dump once.
      uint64_t nextSecondUs = liveStartUs + 1'000'000ull;
      uint32_t prevDelivered = 0;
      uint64_t prevDeliveredBytes = 0;
      uint64_t prevPaints = target.paints();
      int lowFpsStreak = 0;
      uint32_t prevDeliveredForSilence = 0;
      uint64_t lastSilenceSampleUs = liveStartUs;
      bool fullDumped = false;
      uint64_t fastDumpAtUs = 0;
      while (qpc_now_us() < liveUntilUs) {
        if (!dumped) {
          // r8: alive but silent -- the first frame never came, or the frames stopped. One dump per run.
          // Reads are ordered (atomics loaded after `now`) and the elapsed time saturates at 0: a
          // delivery that lands between the two loads cannot make an unsigned wrap look like silence.
          const uint64_t nowUs = qpc_now_us();
          const uint64_t last = std::max(lastDeliverUs.load(std::memory_order_acquire), liveStartUs);
          const uint64_t silentUs = nowUs > last ? nowUs - last : 0;
          const uint32_t deliveredNow = deliveredAtomic.load(std::memory_order_acquire);
          // r11: a host that is still delivering is never dumped (a dump suspends it); silence >= 5 s is the
          // trigger, the low-fps trigger only when asked for, nothing with --no-dump.
          const bool silent = silentUs >= kHangDumpAfterUs && deliveredNow == prevDeliveredForSilence;
          const bool forced = forcedDumpUs != 0 && nowUs >= liveStartUs + forcedDumpUs;
          bool lowFps = false;
          if (nowUs >= nextSecondUs) {
            nextSecondUs += 1'000'000ull;
            const uint64_t paintsNow = target.paints();
            const uint64_t bytesNow = deliveredBytesAtomic.load(std::memory_order_acquire);
            const bool painting = paintsNow - prevPaints >= 10;
            const bool starving = deliveredNow - prevDelivered <= 1;
            // delivered AU PAYLOAD under R/5 this second -- the assembler's output, not the wire (FEC/NACK/
            // partial AUs are not in it); a diagnostic trigger, not a verdict on the capture
            const bool payloadIdle = (bytesNow - prevDeliveredBytes) * 8 * 5 < static_cast<uint64_t>(bitrate);
            lowFpsStreak = (painting && starving && payloadIdle) ? lowFpsStreak + 1 : 0;
            prevPaints = paintsNow;
            prevDelivered = deliveredNow;
            prevDeliveredBytes = bytesNow;
            lowFps = gLowFpsDump && lowFpsStreak >= 3;
          }
          if (nowUs - lastSilenceSampleUs >= 4'000'000ull) {  // the delivered count 4 s ago: "silent" also means no delivery since
            prevDeliveredForSilence = deliveredNow;
            lastSilenceSampleUs = nowUs;
          }
          if (!gNoDump && (silent || forced || lowFps)) {
            // alive? (a host that already died leaves nothing to dump; its exit is recorded below)
            if (WaitForSingleObject(hostPi.hProcess, 0) == WAIT_OBJECT_0) {
              dumped = true;  // nothing to take; the early-exit branch below records the exit code
            } else {
              fastDumpAtUs = qpc_now_us();
              char why[200];
              std::snprintf(why, sizeof(why), "%s (delivered=%u, silent %.1f s, fixture paints=%llu, window-mode=%s)",
                            forced ? "forced by REMOTE60_E2E_HANG_DUMP_AFTER_MS"
                                   : lowFps ? "fixture painting, delivered AU payload < R/5 and <=1 frame/s for 3 s (capture publication or encode starved; diagnostic trigger)"
                                            : "host alive but no frame delivered",
                            deliveredNow, silentUs / 1e6, (unsigned long long)target.paints(), r.windowMode.c_str());
              r.hangDump = write_hang_dump(hostPi.hProcess, hostPi.dwProcessId, runDir, why);
              dumped = true;
            }
          }
        }
        if (dumped && !fullDumped && !gNoDump && fastDumpAtUs != 0 && gFullDumps < 3 && !r.hangDump.empty()) {
          // r11: a fast dump has stacks; a FULL-memory dump also has the heap, so a debugger can read lock
          // owners (!locks) and the D3D/stdout objects the stacks point at. Taken once, 3 s after the fast one,
          // only if the host is still alive and still silent; at most one per run and three per harness run.
          // REMOTE60_E2E_HANG_DUMP_FULL_FORCE=1 (test-support) takes it 1 s after a forced fast dump regardless
          // of silence, to prove the path and measure its cost on a machine whose hosts do not hang.
          static const bool fullForce = [] { char b[8]{}; return GetEnvironmentVariableA("REMOTE60_E2E_HANG_DUMP_FULL_FORCE", b, sizeof(b)) == 1 && b[0] == '1'; }();
          const uint64_t nowUs2 = qpc_now_us();
          const uint64_t last2 = std::max(lastDeliverUs.load(std::memory_order_acquire), liveStartUs);
          const bool silentEnough = nowUs2 > last2 && nowUs2 - last2 >= kHangDumpAfterUs;
          if (((nowUs2 - fastDumpAtUs >= 3'000'000ull && silentEnough) || (fullForce && nowUs2 - fastDumpAtUs >= 1'000'000ull)) &&
              WaitForSingleObject(hostPi.hProcess, 0) != WAIT_OBJECT_0) {
            ++gFullDumps;
            fullDumped = true;
            write_hang_dump(hostPi.hProcess, hostPi.dwProcessId, runDir, "still silent 3 s after the fast dump -- FULL memory", true);
          }
        }
        if (WaitForSingleObject(hostPi.hProcess, 200) == WAIT_OBJECT_0) {
          r.hostExitedEarly = true;
          DWORD code = 0;
          if (GetExitCodeProcess(hostPi.hProcess, &code)) r.hostExitCode = code;
          break;
        }
        if (!gNoLogPoll && gHostLogDest != HostLogDest::Nul && host_log_contains(hostLogPath, "fallback=monitor")) {
          r.monitorFallbackSeen = true;
          TerminateProcess(hostPi.hProcess, 9);
          break;
        }
      }
    }

    controlStop.store(true);
    if (controlThread.joinable()) controlThread.join();
    r.liveEndUs = qpc_now_us();  // anything decoded after this is drain, not a live present
    stop.store(true);
    if (ingress.joinable()) ingress.join();
    // Drain and stop the realtime decoder (it finishes any AUs still queued, then exits).
    {
      std::lock_guard<std::mutex> dlk(dmu);
      dstop = true;
    }
    dcv.notify_all();
    if (decodeThread.joinable()) decodeThread.join();
    r.decoded = r.realtimeDecoded;  // the matrix's correctness check now reflects the realtime decode
    // ids painted EARLY ENOUGH to be decodable before the live window closed: an event painted in the
    // last second is not owed to the receiver (the same boundary rule as viewer_udp_recovery_test's r9
    // marker gate). The 1080p matrix at 6 Mbps showed the last event painted ~0.5 s before the end and
    // missed by baseline and candidate alike -- a harness boundary, not a loss; it FAILed only when the
    // two hosts happened to land on different sides of it. (encoder-rc-budget C5)
    r.eventsExpected = eventLog.Count();  // ids painted (the paired/v2 modes' original definition)
    r.eventsOwed = eventLog.CountUpTo(r.liveEndUs > 1'000'000 ? r.liveEndUs - 1'000'000 : 0);
    r.eventsMatched = static_cast<uint32_t>(r.eventLatencyUs.size());  // ids decoded + matched
    {
      std::lock_guard<std::mutex> lk(pmu);
      r.malformed = static_cast<uint32_t>(pipeline.stats().malformed);
      r.fec = static_cast<uint32_t>(pipeline.stats().fecRecoveredChunks);
      r.giveUps = static_cast<uint32_t>(pipeline.stats().stuckHeadGiveUps);
      r.gapsBehindKey = static_cast<uint32_t>(pipeline.stats().gapsBehindCompleteKey);
    }
    closesocket(sock);
  }

  r.fixturePaints = target.paints();
  {
    const uint64_t lp = target.last_paint_us();
    r.fixtureLastPaintAgeMs = lp ? (static_cast<double>(qpc_now_us()) - static_cast<double>(lp)) / 1000.0 : -1.0;
  }
  target.Stop();
  if (hostPi.hProcess) {
    // r8: a host that died during the live window but after the watcher's last look (or whose watchdog
    // fired in the last 200 ms) must not be recorded as "exit 0 by our TerminateProcess".
    if (!r.hostExitedEarly && WaitForSingleObject(hostPi.hProcess, 0) == WAIT_OBJECT_0) {
      DWORD code = 0;
      if (GetExitCodeProcess(hostPi.hProcess, &code)) r.hostExitCode = code;
      r.hostExitedBeforeCleanup = true;
    }
    TerminateProcess(hostPi.hProcess, 0);
    WaitForSingleObject(hostPi.hProcess, 5000);
    CloseHandle(hostPi.hProcess);
  }
  if (hostPi.hThread) CloseHandle(hostPi.hThread);
  if (hostLog != INVALID_HANDLE_VALUE) CloseHandle(hostLog);
  CloseHandle(job);
  r.host = read_host_log(hostLogPath);
  r.hostLastLine = host_log_last_line(hostLogPath);
  {
    // r11: how much the host wrote (the write that the Home4 main threads sat in) -- lines, bytes, per second
    size_t lines = 0, bytes = 0;
    if (gHostLogDest != HostLogDest::Nul) {
      std::ifstream in(hostLogPath, std::ios::binary);
      std::string line;
      while (std::getline(in, line)) { ++lines; bytes += line.size() + 1; }
    }
    const double secs = static_cast<double>(seconds);
    std::printf("host-log: lines=%zu bytes=%zu (%.1f lines/s, %.1f KB/s over the %d s live window; dest=%s)\n", lines, bytes,
                secs > 0 ? lines / secs : 0.0, secs > 0 ? bytes / 1024.0 / secs : 0.0, seconds,
                gHostLogDest == HostLogDest::Nul ? "NUL (not counted)" : "file");
  }
  r.captureItemFailed = host_log_contains(hostLogPath, "capture item create failed");
  if (host_log_contains(hostLogPath, "fallback=monitor")) r.monitorFallbackSeen = true;
  std::printf("host-exit: early=%d exited-before-cleanup=%d code=%lu captureItemFailed=%d monitorFallback=%d window-mode=%s probe-hr=%s "
              "hang-dump=%s capture-stall-max-ms=%llu fixture-paints=%llu fixture-last-paint-age-ms=%.0f last=\"%s\"\n",
              r.hostExitedEarly ? 1 : 0, r.hostExitedBeforeCleanup ? 1 : 0, r.hostExitCode, r.captureItemFailed ? 1 : 0,
              r.monitorFallbackSeen ? 1 : 0, r.windowMode.c_str(), r.probeHr.c_str(), r.hangDump.empty() ? "none" : r.hangDump.c_str(),
              (unsigned long long)(r.host.captureStallMaxUs / 1000), (unsigned long long)r.fixturePaints, r.fixtureLastPaintAgeMs,
              r.hostLastLine.c_str());
  // r8: a host that its own main-loop watchdog killed (exit 43) is a failed run, whatever else was counted.
  check(!((r.hostExitedEarly || r.hostExitedBeforeCleanup) && r.hostExitCode == 43),
        "the host was not killed by its main-loop watchdog (exit 43) during the live window", r.hostLastLine);
  if (r.monitorFallbackSeen) delete_saved_frames(runDir);
  check(!r.monitorFallbackSeen, "the host never fell back to capturing the MONITOR (the user's screen) -- it found our window",
        r.hostLastLine);
  if (gHostLogDest == HostLogDest::Nul) {
    std::printf("capture: (host log went to NUL -- the capture-window line cannot be read; the window check is not judged in this run)\n");
  } else {
    std::string captureLine;
    const bool own = remote60::native_poc::e2e::e2e_host_captured_window(hostLogPath, targetHwnd, &captureLine);
    std::printf("capture: %s (this pid %lu)\n", captureLine.c_str(), static_cast<unsigned long>(GetCurrentProcessId()));
    check(own, std::string(tight ? "tight" : "padded") +
                   ": the host captured the window this test paints (its hwnd, this pid), not another test's",
          captureLine);
  }

  // Decode is realtime now (the decodeThread above), so there is no post-hoc batch pass here.
  r.appliedBitrate = bitrate;
  r.appliedFps = fps;
  return r;
}

// One run, with the one retry the Home PC needed (r6): when the host could not create a WGC capture
// item for the invisible fixture window, the run is repeated ONCE with a visible window. The mode is
// sticky for the rest of the process -- every later run starts visible instead of paying a failed
// attempt each -- and every run prints its window-mode. If the visible window is refused too, the run
// counts as capture-unavailable and the callers stop early (fail-fast) instead of producing rows of
// zeros for fifteen minutes. Never a monitor fallback.
RunResult run_host(const std::wstring& hostExe, const std::wstring& runDir, Content content, bool tight,
                   uint16_t port, int seconds, uint32_t lossPermille, uint32_t lossSeed, bool wireCapOn,
                   uint32_t bitrate = kBitrate, uint32_t fps = kFps, uint32_t downshiftBitrate = 0,
                   uint32_t downshiftAtSec = 0, bool v2FrameGate = false,
                   const std::vector<TuneStep>* tunes = nullptr) {
  // r7: ask WGC before starting a host. A refused mode is skipped without a host launch; the ladder
  // is invisible -> visible-plain -> visible-framed and stops there (no focus-stealing variants).
  const auto fallback_notice = [&](FixtureWindowMode to, const std::string& why) {
    std::printf("CAPTURE-FALLBACK: %s; switching to window-mode=%s for the rest of this harness run: %s showing only the "
                "harness's test page is now on this screen (a taskbar entry). Please leave it alone and do not close it. No "
                "monitor capture.%s\n",
                why.c_str(), fixture_window_mode_name(to),
                to == FixtureWindowMode::VisiblePlain ? "a frameless 1920x1080-page window" : "an ordinary captioned window",
                to == FixtureWindowMode::VisibleFramed
                    ? " LAST RESORT: the host's client-area crop jitters by a few pixels in this mode, so size/instance checks are "
                      "not a measurement here -- only whether Windows captures a captioned window."
                    : "");
  };
  for (;;) {
    const WgcProbe& pr = wgc_probe_mode(gFixtureWindowMode);
    if (SUCCEEDED(pr.hr)) break;
    FixtureWindowMode next;
    if (!fixture_next_window_mode(gFixtureWindowMode, &next)) {
      std::string all;
      for (int m = 0; m < 3; ++m) {
        const WgcProbe& q = gWgcProbe[m];
        all += std::string(" ") + fixture_window_mode_name(static_cast<FixtureWindowMode>(m)) + " hr=" +
               (q.done ? hr_hex(q.hr) + (q.simulated ? "(simulated)" : "") : std::string("n/a"));
      }
      RunResult refused;
      refused.windowMode = fixture_window_mode_name();
      refused.captureItemFailed = true;
      refused.probeHr = hr_hex(pr.hr) + (pr.simulated ? "(simulated)" : "");
      refused.hostLastLine = "wgc-probe refused every window mode:" + all + " -- no host was started";
      std::printf("host-exit: early=0 code=0 captureItemFailed=1 monitorFallback=0 window-mode=%s probe-hr=%s last=\"%s\"\n",
                  refused.windowMode.c_str(), refused.probeHr.c_str(), refused.hostLastLine.c_str());
      ++gCaptureUnavailableRuns;
      check(false, "a capture item could be created for the fixture window (window-mode=" + refused.windowMode + ")", refused.hostLastLine);
      return refused;
    }
    fallback_notice(next, std::string("WGC refused the ") + fixture_window_mode_name() + " fixture window (wgc-probe hr=" + hr_hex(pr.hr) +
                              (pr.simulated ? "(simulated)" : "") + ")");
    gFixtureWindowMode = next;
  }
  RunResult r = run_host_once(hostExe, runDir, content, tight, port, seconds, lossPermille, lossSeed, wireCapOn, bitrate, fps,
                              downshiftBitrate, downshiftAtSec, v2FrameGate, tunes);
  FixtureWindowMode nextAfterHost;
  if (r.captureItemFailed && fixture_next_window_mode(gFixtureWindowMode, &nextAfterHost)) {
    // The probe accepted but the host did not (second line of defence; r6): one rung up, one retry.
    fallback_notice(nextAfterHost, std::string("the host could not create a capture item for the ") + fixture_window_mode_name() +
                                       " fixture window although the probe accepted it (host said: " + r.hostLastLine + ")");
    gFixtureWindowMode = nextAfterHost;
    std::wstring retryDir = runDir;
    while (!retryDir.empty() && retryDir.back() == L'\\') retryDir.pop_back();
    retryDir += L"_visible\\";
    const uint16_t retryPort = remote60::native_poc::e2e::e2e_pick_free_udp_port();
    r = run_host_once(hostExe, retryDir, content, tight, retryPort != 0 ? retryPort : port, seconds, lossPermille, lossSeed,
                      wireCapOn, bitrate, fps, downshiftBitrate, downshiftAtSec, v2FrameGate, tunes);
  }
  if (r.captureItemFailed) ++gCaptureUnavailableRuns;
  check(!r.captureItemFailed, std::string("a capture item could be created for the fixture window (window-mode=") + r.windowMode + ")",
        r.hostLastLine);
  return r;
}

void print_run(Content content, bool tight, uint32_t lossPermille, uint32_t lossSeed, const RunResult& r) {
  std::printf("HOSTRUN content=%s layout=%s hostSays=%s loss=%u seed=%u nack=%d hostTxFrames=%llu hostKeyLines=%zu"
              " hostTxBytes=%llu hostParity=%llu hostChunkHdr=%llu hostVideoDg=%llu hostNackBytes=%llu hostWireEst=%llu"
              " rxDelivered=%u rxFirstSeq=%u rxLastSeq=%u rxSingleChunk=%u rxKeyFrames=%u rxGaps=%u rxGapsBehindKey=%u rxMaxGap=%u"
              " rxKeyReq=%u rxDisc=%u rxNacks=%u rxFec=%u rxGiveUps=%u rxMalformed=%u rxDropped=%llu"
              " rxData=%llu rxParity=%llu rxHdr=%llu rxReplay=%llu rxVideoDg=%llu decoded=%u decErr=%u decoder=%s\n",
              content_name(content), tight ? "tight" : "padded", r.host.layoutToken.c_str(), lossPermille, lossSeed,
              r.nackNegotiated ? 1 : 0, static_cast<unsigned long long>(r.host.txFrames), r.host.keyframeLines,
              static_cast<unsigned long long>(r.host.txBytes), static_cast<unsigned long long>(r.host.parity),
              static_cast<unsigned long long>(r.host.chunkHdr), static_cast<unsigned long long>(r.host.videoDatagrams),
              static_cast<unsigned long long>(r.host.nackBytes), static_cast<unsigned long long>(r.host.wireEst),
              r.delivered, r.firstSeq, r.lastSeq, r.singleChunkFrames, r.keyFrames, r.gaps, r.gapsBehindKey, r.maxGap,
              r.keyReq, r.disc,
              r.nacks, r.fec, r.giveUps, r.malformed, static_cast<unsigned long long>(r.rxDropped),
              static_cast<unsigned long long>(r.rxData), static_cast<unsigned long long>(r.rxParity),
              static_cast<unsigned long long>(r.rxHeader), static_cast<unsigned long long>(r.rxReplay),
              static_cast<unsigned long long>(r.rxVideoDatagrams), r.decoded, r.decodeErrors, r.decoder.c_str());
}

// Peak bytes in any sliding window, as bits/s (two-pointer; wireEvents are in arrival order).
double window_peak_bps(const std::vector<std::pair<uint64_t, uint32_t>>& ev, uint64_t windowUs) {
  double peak = 0;
  size_t lo = 0;
  uint64_t sum = 0;
  for (size_t hi = 0; hi < ev.size(); ++hi) {
    sum += ev[hi].second;
    while (ev[hi].first - ev[lo].first >= windowUs) sum -= ev[lo++].second;
    const double bps = static_cast<double>(sum) * 8.0 * 1e6 / static_cast<double>(windowUs);
    if (bps > peak) peak = bps;
  }
  return peak;
}

// The isolation-matrix metrics for one run + the cap-window / responsiveness assertions.
//
// Two layers are reported, kept distinct (r4 R4 + addendum):
//   delivery* -- measured at ASSEMBLER DELIVERY (deliveredFps / deliverLat / deliverGapMax).
//   decode*   -- measured at the REALTIME DECODER output (decodeFps / decodeLat / decodeGapMax): a
//                decoder thread consumed the AUs as they arrived, so this is present-ready timing --
//                what a viewer would actually see keep up. `realtimeDecoded`/decErr is the correctness
//                check. The pre-fixed responsiveness ceiling `maxDecodeGapMs` (0 = no gate) is applied
//                to the DECODE gap, so a candidate that holds the cap by freezing the picture FAILS.
void matrix_metrics(const char* label, uint64_t capBps, const RunResult& r, double maxDecodeGapMs,
                    double maxDecodeLatMs = 0.0) {
  const double p1s = window_peak_bps(r.wireEvents, 1'000'000);
  const double p250 = window_peak_bps(r.wireEvents, 250'000);
  // r4 R5 (Codex e1bc633): the burst contract is the 2s rolling average, so the 2s window of the EXTERNAL
  // received packet trace (original + FEC + NACK, all in wireEvents) is the real pass/fail. In burst mode
  // the 250ms/1s windows INTENTIONALLY exceed the cap (that is Option 1); only p2s must stay within it.
  const double p2s = window_peak_bps(r.wireEvents, 2'000'000);
  char burstEnv[8] = {0};
  const bool burstMode = GetEnvironmentVariableA("REMOTE60_NATIVE_FORCE_CLAMP_BURST", burstEnv,
                                                 sizeof(burstEnv)) > 0 &&
                         (burstEnv[0] == '1' || burstEnv[0] == 't' || burstEnv[0] == 'T');
  double deliveredFps = 0, firstFrameMs = 0, deliverGapMaxMs = 0, idrPerSec = 0;
  if (r.deliverUs.size() >= 2) {
    const uint64_t span = r.deliverUs.back() - r.deliverUs.front();
    if (span > 0) {
      deliveredFps = static_cast<double>(r.delivered) * 1e6 / static_cast<double>(span);
      idrPerSec = static_cast<double>(r.keyFrames) * 1e6 / static_cast<double>(span);
    }
    const uint64_t t0 = r.wireEvents.empty() ? r.deliverUs.front() : r.wireEvents.front().first;
    firstFrameMs = (r.deliverUs.front() - t0) / 1000.0;
    uint64_t gap = 0;
    for (size_t i = 1; i < r.deliverUs.size(); ++i) gap = std::max(gap, r.deliverUs[i] - r.deliverUs[i - 1]);
    deliverGapMaxMs = gap / 1000.0;
  }
  // stutter-keyframe r3 C: the COUNT of >250ms output gaps (the periodic-IDR stutter metric the
  // contract asks for), not just the max. Computed from the retained per-frame deliver/decode times.
  uint32_t deliverGap250 = 0, decodeGap250 = 0;
  for (size_t i = 1; i < r.deliverUs.size(); ++i)
    if (r.deliverUs[i] - r.deliverUs[i - 1] > 250'000ULL) ++deliverGap250;
  for (size_t i = 1; i < r.decodeOutUs.size(); ++i)
    if (r.decodeOutUs[i] - r.decodeOutUs[i - 1] > 250'000ULL) ++decodeGap250;
  // Realtime decode layer.
  double decodeFps = 0, decodeGapMaxMs = 0, decodeLatP95Ms = 0, decodeLatMaxMs = 0, firstDecodeMs = 0;
  if (r.decodeOutUs.size() >= 2) {
    const uint64_t span = r.decodeOutUs.back() - r.decodeOutUs.front();
    if (span > 0) decodeFps = static_cast<double>(r.realtimeDecoded) * 1e6 / static_cast<double>(span);
    const uint64_t t0 = r.wireEvents.empty() ? r.decodeOutUs.front() : r.wireEvents.front().first;
    firstDecodeMs = (r.decodeOutUs.front() - t0) / 1000.0;
    uint64_t gap = 0;
    for (size_t i = 1; i < r.decodeOutUs.size(); ++i) gap = std::max(gap, r.decodeOutUs[i] - r.decodeOutUs[i - 1]);
    decodeGapMaxMs = gap / 1000.0;
    // Present latency over frames decoded DURING the live run (exclude the post-stop drain, which would
    // inflate the max with a frame finished during teardown).
    std::vector<uint64_t> lat;
    for (size_t i = 0; i < r.decodeLatencyUs.size() && i < r.decodeOutUs.size(); ++i)
      if (r.liveEndUs == 0 || r.decodeOutUs[i] <= r.liveEndUs) lat.push_back(r.decodeLatencyUs[i]);
    if (!lat.empty()) {
      std::sort(lat.begin(), lat.end());
      decodeLatP95Ms = lat[(lat.size() * 95) / 100] / 1000.0;
      decodeLatMaxMs = lat.back() / 1000.0;
    }
  }
  std::printf("MATRIX %s: cap=%llu applied=%u/%ufps | win1s=%.0f (%.1f%%) win250=%.0f (%.1f%%) | "
              "deliveredFps=%.1f deliverGapMax=%.0fms firstFrame=%.0fms | realtimeDecoded=%u decodeFps=%.1f "
              "firstDecode=%.0fms decodeGapMax=%.0fms decodeLatP95=%.0fms decodeLatMax=%.0fms | idr/s=%.2f keyReq=%u "
              "disc=%u nacks=%u giveUps=%u decErr=%u decoder=%s rxDropped=%llu gap250(deliver/decode)=%u/%u\n",
              label, (unsigned long long)capBps, r.appliedBitrate, r.appliedFps, p1s, 100.0 * p1s / capBps, p250,
              100.0 * p250 / capBps, deliveredFps, deliverGapMaxMs, firstFrameMs, r.realtimeDecoded, decodeFps,
              firstDecodeMs, decodeGapMaxMs, decodeLatP95Ms, decodeLatMaxMs, idrPerSec, r.keyReq, r.disc, r.nacks,
              r.giveUps, r.decodeErrors, r.decoder.c_str(), (unsigned long long)r.rxDropped, deliverGap250, decodeGap250);
  std::printf("       %s wire-window: win2s=%.0f (%.1f%% of cap) mode=%s\n", label, p2s, 100.0 * p2s / capBps,
              burstMode ? "burst(2s gate)" : "strict(250ms/1s+2s gates)");
  std::printf("       %s host: queueWaitMax=%llums queueDepthMax=%llu wireCapBps=%llu | recoveryMax=%.0fms recoveries=%u\n",
              label, (unsigned long long)(r.host.maxQueueWaitUs / 1000), (unsigned long long)r.host.maxQueueDepth,
              (unsigned long long)r.host.wireCapBps, r.recoveryMaxUs / 1000.0, r.recoveryCount);
  const std::string tag = std::string("matrix ") + label;
  // V1 (Codex 72a22d2): the burst CONTRACT is on the SENT wire, so assert the host's own authoritative
  // 2s window -- burstWin2s = max committed A(t-2s,t]. NOTE the bound: a NORMAL (non-grant) datagram is
  // paced by the pre-existing WireLimiter, whose token bucket permits the SAME ~10% short-window
  // tolerance the matrix already accepts for p1s/p250 (a saturated row sits at ~100.4% of 2r in the
  // STRICT run too -- it is the limiter, not the burst). The BURST never exceeds that bound: a grant is
  // admission-capped (Reserve) at AdmissionBudget, and when the window is already near 2r the grant gets
  // 0 budget and does not fire. So the e2e bound here is the wire cap's established cap*1.10; the TIGHT
  // 2r admission of a GRANT is proven at the unit level (host_burst_ledger_test B1-1/S2 and the real
  // send path in host_burst_sender_admission_test), not re-derived from this RX-perturbed figure.
  // V1/C2 (Codex 597aca2): with the unified admission (C1) EVERY B1 datagram -- normal, grant and replay
  // -- passes the 2s window Reserve, which only admits when committed + bytes <= 2r. So the committed 2s
  // window never exceeds 2r; the bound is 2r + ONE datagram (the single in-flight / 2s measurement edge),
  // NOT 2r + B. The limiter's bucket depth B is the LEGACY strict-only (cap-off) envelope and must NOT
  // widen the B1 judgment to allow a constant-rate overshoot. r8: in burst mode require burstWin2s > 0 so
  // a host that logged nothing (max=0) cannot pass by absence of evidence.
  const uint64_t cap2r = r.host.cap2r ? r.host.cap2r : capBps / 4;  // 2r = 2*(cap/8) bytes
  const uint64_t Ldg = 1500;                                         // one datagram (<=MTU 1400 + 28), rounded
  const uint64_t txBound = cap2r + Ldg;                              // 2r + one datagram (B1 contract)
  if (burstMode)
    check(r.host.maxBurstWin2s > 0, tag + ": burst mode logged a 2s window (evidence present, not max=0)",
          "burstWin2s=" + std::to_string(r.host.maxBurstWin2s));
  if (r.host.maxBurstWin2s > 0)
    check(r.host.maxBurstWin2s <= txBound,
          tag + ": host burstWin2s <= 2r + one datagram (the B1 2s contract; unified admission caps it)",
          "burstWin2s=" + std::to_string(r.host.maxBurstWin2s) + " bound(2r+L)=" + std::to_string(txBound));
  // p2s is the client-RECEIVED trace: OS/network queueing can compress arrivals tighter than the send
  // spacing, so it is a DIAGNOSTIC of arrival shape with a jitter tolerance, NOT the contract. A gross RX
  // overshoot still fails (a real wire-rate break would show here too).
  check(p2s <= capBps * 1.25, tag + ": [RX diagnostic] 2 s arrival window within jitter tolerance (not the contract)");
  // The 250ms/1s strict windows are the pass/fail ONLY in strict mode; in burst mode they intentionally
  // exceed the cap (Option 1) and are reported for information, with the 2s gate above governing.
  if (!burstMode) {
    check(p1s <= capBps * 1.10, tag + ": every 1 s window <= cap +10%");
    check(p250 <= capBps * 1.10, tag + ": every 250 ms window <= cap +10%");
  } else {
    std::printf("       %s [burst] 250ms=%.1f%% 1s=%.1f%% of cap intentionally exceed; 2s gate governs\n",
                label, 100.0 * p250 / capBps, 100.0 * p1s / capBps);
  }
  check(r.realtimeDecoded >= 2 && r.decodeErrors == 0, tag + ": the realtime decoder produced frames (no decode error)");
  check(firstDecodeMs >= 0 && firstDecodeMs <= 3000, tag + ": the first frame decodes within a bounded time");
  // r4 R4-2 / addendum: a pre-fixed responsiveness ceiling on the REALTIME DECODE gap, so a candidate
  // that keeps the cap by freezing the decoded picture FAILS. Only where a long freeze is a defect
  // (motion-bearing content); 0 disables it (a static page legitimately has long gaps).
  if (maxDecodeGapMs > 0)
    check(decodeGapMaxMs <= maxDecodeGapMs,
          tag + ": max realtime-decode gap within the responsiveness ceiling (" + std::to_string((int)maxDecodeGapMs) + "ms)");
  // r5 measurement: input->screen present latency. For the single-change case this is the capture->
  // decoded-present time of the change frame AFTER idle (no backlog to inflate it), the real "a change
  // reaches the screen" figure Codex asked for -- gated so a laggy present FAILS. deliverGap is NOT used.
  if (maxDecodeLatMs > 0)
    check(decodeLatMaxMs <= maxDecodeLatMs,
          tag + ": input->screen present latency within the ceiling (" + std::to_string((int)maxDecodeLatMs) + "ms)");
}

// A runtime 6 -> 1.5 Mbps downshift. r5 G4: the apply point is the host's OWN same-clock apply marker
// (`wirecap applied ... appliedQpcUs=`, logged where UpdateWireCap changes the cap) -- an INDEPENDENT
// event, not a rate observed later from the result. EVERY complete window from that instant must
// respect the new cap (the credit is preserved across the change, never refilled). The request->apply
// latency is reported separately. A step-down observed on the wire is kept only as an aux cross-check.
void matrix_metrics_downshift(const RunResult& r) {
  const uint64_t applyUs = r.host.wireCapAppliedQpcUs;  // host same-clock apply instant (G4)
  // Aux cross-check (NOT the judged point): where the wire rate actually stepped down.
  uint64_t stepDownUs = 0;
  if (r.switchUs) {
    for (size_t i = 0; i < r.wireEvents.size(); ++i) {
      const uint64_t t = r.wireEvents[i].first;
      if (t < r.switchUs) continue;
      uint64_t bytes = 0;
      for (size_t j = i + 1; j-- > 0;) {
        if (r.wireEvents[j].first + 250'000 < t) break;
        bytes += r.wireEvents[j].second;
      }
      if (static_cast<double>(bytes) * 8.0 * 1e6 / 250'000.0 <= 3'000'000.0) { stepDownUs = t; break; }
    }
  }
  std::vector<std::pair<uint64_t, uint32_t>> before, after;
  for (const auto& e : r.wireEvents) {
    if (r.switchUs && e.first < r.switchUs) before.push_back(e);
    if (applyUs && e.first >= applyUs) after.push_back(e);  // EVERY complete window from the host apply instant
  }
  const double b1 = window_peak_bps(before, 1'000'000);
  const double a1 = window_peak_bps(after, 1'000'000);
  const double a250 = window_peak_bps(after, 250'000);
  // V3 (Codex 72a22d2): the 2s CONTRACT across the downshift. `after` holds only post-apply (new-rate)
  // sends; S2 caps new admissions at the new 2r, so their 2s window must respect 1.5M. The transition
  // window (full trace around the apply) still carries 6M-era debt -- reported, not failed.
  const double a2s = window_peak_bps(after, 2'000'000);
  std::vector<std::pair<uint64_t, uint32_t>> transition;  // spans the apply instant -> shows debt drain
  if (applyUs)
    for (const auto& e : r.wireEvents)
      if (e.first + 2'000'000 >= applyUs && e.first <= applyUs + 2'000'000) transition.push_back(e);
  const double tr2s = window_peak_bps(transition, 2'000'000);
  std::printf("MATRIX 6->1.5-downshift: switched=%d appliedBitrate=%llu requestToApplyMs=%.0f stepDownVsApplyMs=%.0f "
              "before1s=%.0f (%.1f%% of 6M) after1s=%.0f (%.1f%% of 1.5M) after250=%.0f (%.1f%%) decoded=%u decErr=%u "
              "beforeEv=%zu afterEv=%zu\n",
              r.switchUs != 0 ? 1 : 0, (unsigned long long)r.host.wireCapAppliedBitrate,
              (applyUs && r.switchUs) ? (double)(applyUs - r.switchUs) / 1000.0 : -1.0,
              (applyUs && stepDownUs) ? (double)((int64_t)stepDownUs - (int64_t)applyUs) / 1000.0 : 0.0,
              b1, 100.0 * b1 / 6'000'000.0, a1, 100.0 * a1 / 1'500'000.0, a250, 100.0 * a250 / 1'500'000.0, r.decoded,
              r.decodeErrors, before.size(), after.size());
  std::printf("       6->1.5-downshift 2s: afterNewRate=%.0f (%.1f%% of 1.5M) transitionWindow=%.0f (%.1f%% of 1.5M, "
              "6M-era debt draining -- reported not failed)\n",
              a2s, 100.0 * a2s / 1'500'000.0, tr2s, 100.0 * tr2s / 1'500'000.0);
  check(r.switchUs != 0, "matrix downshift: the runtime bitrate change was sent");
  check(applyUs != 0 && r.host.wireCapAppliedBitrate == 1'500'000,
        "matrix downshift: the host logged a same-clock wire-cap apply at the new rate (independent apply point)");
  check(before.empty() || b1 <= 6'000'000 * 1.10, "matrix downshift: before the switch, 1 s window <= 6 Mbps +10%");
  check(!after.empty() && a1 <= 1'500'000 * 1.10,
        "matrix downshift: EVERY 1 s window from the host apply instant <= 1.5 Mbps +10%");
  check(after.empty() || a250 <= 1'500'000 * 1.10,
        "matrix downshift: EVERY 250 ms window from the host apply instant <= 1.5 Mbps +10%");
  // V3: the 2s window of post-apply (new-rate) sends respects the new 2r -- S2 blocks a new admission
  // from exceeding the new cap, so no new-rate 2s window overshoots even while 6M-era debt drains.
  check(after.empty() || a2s <= 1'500'000 * 1.10,
        "matrix downshift: EVERY 2 s window of post-apply new-rate sends <= 1.5 Mbps +10% (S2)");
}

// r6 H3 paired comparison: candidate cap ON / candidate cap OFF / feature-pre baseline (a95215e), on
// the two discrete-change scenarios x two rates, with EVENT-MATCHED change->decoded-picture latency.
// The judgement (Codex): if the candidate ON is not worse than the baseline and loses no events, the
// (pre-existing) ~1 s idle latency is a separate follow-up and the cap may ship; a candidate-only
// increase or new loss/stall is fixed here.
struct EvStats { double p50 = 0, p95 = 0, max = 0; uint32_t matched = 0, expected = 0; };
EvStats event_stats(const RunResult& r) {
  EvStats s;
  s.matched = r.eventsMatched;
  s.expected = r.eventsExpected;
  std::vector<uint64_t> v = r.eventLatencyUs;
  if (!v.empty()) {
    std::sort(v.begin(), v.end());
    s.p50 = v[v.size() / 2] / 1000.0;
    s.p95 = v[(v.size() * 95) / 100] / 1000.0;
    s.max = v.back() / 1000.0;
  }
  return s;
}

int run_paired(const std::wstring& candidateHost, const std::wstring& baselineHost, const std::wstring& dir,
               int seconds) {
  struct Scn { const char* label; Content content; uint32_t bitrate, fps; };
  const Scn scns[] = {
      {"singlechange-6M60", Content::SingleChange, 6'000'000, 60},
      {"singlechange-1.5M30", Content::SingleChange, 1'500'000, 30},
      {"fulltransition-6M60", Content::FullTransition, 6'000'000, 60},
      {"fulltransition-1.5M30", Content::FullTransition, 1'500'000, 30},
  };
  const bool haveBaseline = !baselineHost.empty();
  std::printf("\n=== r6 H3 paired event-ID latency (candidate ON/OFF%s) ===\n",
              haveBaseline ? " / a95215e baseline" : " -- NO baseline host given");
  int idx = 0;
  for (const Scn& s : scns) {
    auto one = [&](const std::wstring& host, bool capOn, const char* tag) -> EvStats {
      const uint16_t port = remote60::native_poc::e2e::e2e_pick_free_udp_port();
      if (port == 0) return {};
      std::wstring safe(s.label, s.label + std::strlen(s.label));
      for (wchar_t& c : safe) if (c == L'.' || c == L'/') c = L'_';
      const std::wstring runDir = dir + L"p_" + safe + L"_" + std::wstring(tag, tag + std::strlen(tag)) +
                                  L"_" + std::to_wstring(idx++) + L"\\";
      const RunResult r = run_host(host, runDir, s.content, /*tight=*/true, port, seconds, 0, 1, capOn, s.bitrate, s.fps);
      // V1: per-event table (event 1 = first screen, includes connect+host init+first IDR; separated
      // from the later idle->change events) + the slowest event's byte-time trace.
      size_t slow = 0;
      for (size_t i = 1; i < r.eventRecs.size(); ++i)
        if (r.eventRecs[i].latencyUs > r.eventRecs[slow].latencyUs) slow = i;
      std::printf("   [%s/%s] events(id:lat ms): ", s.label, tag);
      for (size_t i = 0; i < r.eventRecs.size(); ++i)
        std::printf("%s%u:%.0f%s", i == 1 ? "| runtime: " : (i == 0 ? "ev1(init): " : ""),
                    r.eventRecs[i].id, r.eventRecs[i].latencyUs / 1000.0, (i + 1 < r.eventRecs.size()) ? " " : "\n");
      if (r.eventRecs.empty()) std::printf("(none)\n");
      if (!r.eventRecs.empty()) {
        const auto& e = r.eventRecs[slow];
        const double latMs = e.latencyUs / 1000.0;
        // A CONVERSION only (latency x rate / 8) for scale -- NOT the measured AU size. It is the wire
        // budget that fits in this latency at the cap; do not read it as "the IDR was this big" or as a
        // proof of cause (the same formula applies to OFF/baseline). The host's wire log (AU bytes /
        // send times) would be needed to attribute the latency; run-max queueWait is not yet tied to
        // this AU. (r8 V1 correction.)
        const double wireBudgetBytes = latMs / 1000.0 * s.bitrate / 8.0;
        std::printf("        slowest ev%u lat=%.0fms: (scale only) latency*rate/8 @%.1fMbps = %.0f bytes -- a "
                    "conversion, NOT a measured AU size; run-max host queueWaitMax=%llums queueDepthMax=%llu "
                    "(not yet tied to this AU)\n",
                    e.id, latMs, s.bitrate / 1e6, wireBudgetBytes, (unsigned long long)(r.host.maxQueueWaitUs / 1000),
                    (unsigned long long)r.host.maxQueueDepth);
      }
      return event_stats(r);
    };
    const EvStats on = one(candidateHost, true, "on");
    const EvStats off = one(candidateHost, false, "off");
    EvStats base{};
    if (haveBaseline) base = one(baselineHost, true, "base");
    std::printf("PAIRED %-22s | ON p50/p95/max=%.0f/%.0f/%.0fms m=%u/%u | OFF %.0f/%.0f/%.0fms m=%u/%u%s\n",
                s.label, on.p50, on.p95, on.max, on.matched, on.expected, off.p50, off.p95, off.max, off.matched,
                off.expected,
                haveBaseline ? ("" ) : " | (baseline missing)");
    if (haveBaseline)
      std::printf("        %-22s | a95215e p50/p95/max=%.0f/%.0f/%.0fms m=%u/%u\n", "", base.p50, base.p95, base.max,
                  base.matched, base.expected);
    const std::string t = std::string("paired ") + s.label;
    // No loss / no gross stall, every config (the real failure modes Codex named).
    check(on.matched == on.expected && on.expected > 0, t + ": candidate ON decoded every painted event (no loss)");
    check(off.matched == off.expected && off.expected > 0, t + ": candidate OFF decoded every painted event (no loss)");
    check(on.max <= 2500.0, t + ": candidate ON no multi-second stall on a change (max=" + std::to_string((int)on.max) + "ms)");
    if (haveBaseline) {
      check(base.matched == base.expected && base.expected > 0, t + ": baseline decoded every painted event");
      // The NEW-LOGIC regression test: with the cap OFF the gate/staging/replay changes are present but
      // the limiter is not, so OFF must match the feature-pre baseline. A real code regression shows
      // HERE. (5 events/run -> p95 is noisy; a generous tolerance.)
      const double tol = std::max(base.p95 * 0.60, 250.0);
      check(off.p95 <= base.p95 + tol,
            t + ": NEW LOGIC (cap OFF) not regressed vs a95215e (off=" + std::to_string((int)off.p95) +
                " base=" + std::to_string((int)base.p95) + "ms)");
      // ON vs baseline is REPORTED, not failed: at a low cap the limiter paces a burst (a change frame)
      // to the configured rate, which inevitably costs latency vs the uncapped baseline -- the hard
      // cap's contractual trade-off, not a code defect (ON>OFF, OFF==baseline isolates it to the cap).
      std::printf("        %-22s | cap rate-bound cost (ON vs a95215e): p95 %+.0fms  [reported, not a gate]\n", "",
                  on.p95 - base.p95);
      check(on.matched >= base.matched, t + ": candidate ON loses no event the baseline delivered");
    }
  }
  return 0;
}

// r7 V2: the real PC VideoReceiver/FrameGate receive path (admit gates decode, tick drives recovery)
// on big-IDR + idle->change content at 6M/60 and 1.5M/30 -- a local smoke (not 2-PC/WAN). Confirms
// the event marker reaches the decoded picture through the gate, recovery is finite, and there is NO
// unnecessary repeated keyframe request (the IDR-storm defect FrameGate exists to avoid).
int run_v2(const std::wstring& hostExe, const std::wstring& dir, int seconds) {
  struct Scn { const char* label; Content content; uint32_t bitrate, fps; };
  const Scn scns[] = {
      {"singlechange-6M60", Content::SingleChange, 6'000'000, 60},
      {"singlechange-1.5M30", Content::SingleChange, 1'500'000, 30},
      {"fulltransition-6M60", Content::FullTransition, 6'000'000, 60},
      {"fulltransition-1.5M30", Content::FullTransition, 1'500'000, 30},
  };
  std::printf("\n=== r7 V2 PC VideoReceiver/FrameGate smoke (cap ON, real gate+decoder) ===\n");
  int idx = 0;
  for (const Scn& s : scns) {
    const uint16_t port = remote60::native_poc::e2e::e2e_pick_free_udp_port();
    if (port == 0) continue;
    std::wstring safe(s.label, s.label + std::strlen(s.label));
    for (wchar_t& c : safe) if (c == L'.' || c == L'/') c = L'_';
    const std::wstring runDir = dir + L"v2_" + safe + L"_" + std::to_wstring(idx++) + L"\\";
    const RunResult r = run_host(hostExe, runDir, s.content, /*tight=*/true, port, seconds, 0, 1, /*wireCapOn=*/true,
                                 s.bitrate, s.fps, 0, 0, /*v2FrameGate=*/true);
    std::printf("V2 %-22s | marker events matched=%u/%u | IDR decoded=%u | FrameGate keyframe reqs=%u | "
                "disc=%u nacks=%u decoder=%s\n",
                s.label, r.eventsMatched, r.eventsExpected, r.v2IdrDecoded, r.v2KeyframeRequests, r.disc, r.nacks,
                r.decoder.c_str());
    const std::string t = std::string("v2 ") + s.label;
    check(r.eventsExpected > 0 && r.eventsMatched == r.eventsExpected,
          t + ": every change marker reached the decoded picture through the real FrameGate");
    check(r.v2IdrDecoded >= 1, t + ": at least one IDR decoded through the gate (recovery works)");
    // No IDR storm: FrameGate must not re-ask every frame. ~5 change events over `seconds`; a healthy
    // run needs only the first-IDR wait + the occasional recovery. A storm is many requests per event.
    check(r.v2KeyframeRequests <= 3u + r.eventsExpected,
          t + ": no unnecessary repeated keyframe requests (reqs=" + std::to_string(r.v2KeyframeRequests) +
              " events=" + std::to_string(r.eventsExpected) + ")");
  }
  return 0;
}

// ------------------------------------------------------------- encoder-rc-budget C5: the RC matrix
//
// Five desktop-like scenes x {30, 60} fps x wire R {1.5, 3, 6} Mbps, on a 1080p page, candidate host
// against the baseline host (f5de6e8, the same binary the field runs), cap ON, strict windows. Per run:
// the wire against R (every 1 s / 250 ms window, external receive), the realtime decode (fps, >250 ms
// gaps, present latency p95/max), the event-matched change->picture latency (marker scenes), the
// matched PSNR against the painted page (marker scenes: whole picture + text rows), the host's own
// R/E account and plan state, the IDR sizes it sent, and what it actually put on the wire as payload
// (udpTxBytes over the live seconds -- the encoder's real output that was sent, not the genKbps
// estimate). The baseline is REPORTED beside the candidate; the candidate is GATED: wire windows
// within cap+10%, no decode error, no event lost, no multi-second stall.
struct RcCell {
  const char* label;
  Content content;
  bool marker;      // event-id scenes: latency + PSNR are event-matched
  double maxDecodeGapMs;  // responsiveness ceiling for continuous-motion scenes (0 = none)
};
struct RcRow {
  std::string host, scene;
  uint32_t fps = 0, wireBps = 0;
  double win1sPct = 0, win250Pct = 0, win267Pct = 0, deliveredFps = 0, decodeFps = 0, decodeGapMaxMs = 0, decodeLatP95Ms = 0,
         decodeLatMaxMs = 0, evP50 = 0, evP95 = 0, evMax = 0, psnrFullMean = 0, psnrFullMin = 0, psnrTextMean = 0,
         psnrTextMin = 0, sentPayloadKbps = 0, idrPerSec = 0, keyMaxKb = 0, keyMeanKb = 0;
  uint32_t gap250 = 0, evMatched = 0, evExpected = 0, decErr = 0, keyReq = 0, nacks = 0, psnrN = 0, rcInstances = 0,
           rcReconfigureWrites = 0, rcGovernorWrites = 0, decodedW = 0, decodedH = 0;
  uint64_t desiredEncoder = 0, appliedEncoder = 0, effectiveWire = 0, hostSkips = 0;
  std::string rcProfile, planState, windowMode;
  unsigned long hostExit = 0;      // r8: the host's own exit code if it died (0 = alive at the end)
  bool hostDied = false;
  uint64_t stallMaxMs = 0;         // r8: capture thread's longest Submitting stall
  uint64_t fixturePaints = 0;
  bool valid = true;               // r8: false = not a throughput sample (dead host, no frame, stalled capture, dumped)
  bool dumped = false;             // r9: a hang dump was taken during this run
};

RcRow rc_row_of(const char* host, const RcCell& cell, uint32_t fps, uint32_t wireBps, int seconds, const RunResult& r) {
  RcRow row;
  row.host = host;
  row.scene = cell.label;
  row.fps = fps;
  row.wireBps = wireBps;
  row.win1sPct = 100.0 * window_peak_bps(r.wireEvents, 1'000'000) / wireBps;
  row.win250Pct = 100.0 * window_peak_bps(r.wireEvents, 250'000) / wireBps;
  row.win267Pct = 100.0 * window_peak_bps(r.wireEvents, 267'000) / wireBps;  // the probe's 267 ms peak, on the external wire
  if (r.deliverUs.size() >= 2) {
    const uint64_t span = r.deliverUs.back() - r.deliverUs.front();
    if (span > 0) {
      row.deliveredFps = static_cast<double>(r.delivered) * 1e6 / static_cast<double>(span);
      row.idrPerSec = static_cast<double>(r.keyFrames) * 1e6 / static_cast<double>(span);
    }
  }
  if (r.decodeOutUs.size() >= 2) {
    const uint64_t span = r.decodeOutUs.back() - r.decodeOutUs.front();
    if (span > 0) row.decodeFps = static_cast<double>(r.realtimeDecoded) * 1e6 / static_cast<double>(span);
    uint64_t gap = 0;
    for (size_t i = 1; i < r.decodeOutUs.size(); ++i) {
      const uint64_t g = r.decodeOutUs[i] - r.decodeOutUs[i - 1];
      gap = std::max(gap, g);
      if (g > 250'000ULL) ++row.gap250;
    }
    row.decodeGapMaxMs = gap / 1000.0;
    std::vector<uint64_t> lat;
    for (size_t i = 0; i < r.decodeLatencyUs.size() && i < r.decodeOutUs.size(); ++i)
      if (r.liveEndUs == 0 || r.decodeOutUs[i] <= r.liveEndUs) lat.push_back(r.decodeLatencyUs[i]);
    if (!lat.empty()) {
      std::sort(lat.begin(), lat.end());
      row.decodeLatP95Ms = lat[(lat.size() * 95) / 100] / 1000.0;
      row.decodeLatMaxMs = lat.back() / 1000.0;
    }
  }
  const EvStats ev = event_stats(r);
  row.evP50 = ev.p50; row.evP95 = ev.p95; row.evMax = ev.max; row.evMatched = ev.matched;
  row.evExpected = r.eventsOwed;  // what the receiver is OWED (painted >= 1 s before the end), not every paint
  if (!r.psnrFull.empty()) {
    double sf = 0, st = 0, mf = 1e9, mt = 1e9;
    for (size_t i = 0; i < r.psnrFull.size(); ++i) {
      sf += r.psnrFull[i]; st += r.psnrText[i];
      mf = std::min(mf, r.psnrFull[i]); mt = std::min(mt, r.psnrText[i]);
    }
    row.psnrN = static_cast<uint32_t>(r.psnrFull.size());
    row.psnrFullMean = sf / r.psnrFull.size(); row.psnrFullMin = mf;
    row.psnrTextMean = st / r.psnrText.size(); row.psnrTextMin = mt;
    row.decodedW = r.psnrDecodedW; row.decodedH = r.psnrDecodedH;
  }
  row.sentPayloadKbps = seconds > 0 ? static_cast<double>(r.host.txBytes) * 8.0 / 1000.0 / seconds : 0.0;
  if (!r.host.keyBytes.empty()) {
    uint64_t mx = 0, sum = 0;
    for (uint64_t b : r.host.keyBytes) { mx = std::max(mx, b); sum += b; }
    row.keyMaxKb = mx / 1024.0;
    row.keyMeanKb = static_cast<double>(sum) / r.host.keyBytes.size() / 1024.0;
  }
  row.decErr = r.decodeErrors; row.keyReq = r.keyReq; row.nacks = r.nacks;
  row.rcInstances = r.host.rcInstances; row.rcReconfigureWrites = r.host.rcReconfigureWrites; row.rcGovernorWrites = r.host.rcGovernorWrites;
  row.desiredEncoder = r.host.desiredEncoder; row.appliedEncoder = r.host.appliedEncoder;
  row.effectiveWire = r.host.effectiveWire; row.hostSkips = r.host.wireOverloadSkips;
  row.rcProfile = r.host.rcProfile;
  row.windowMode = r.windowMode;
  row.hostDied = r.hostExitedEarly || r.hostExitedBeforeCleanup;
  row.hostExit = row.hostDied ? r.hostExitCode : 0;
  row.stallMaxMs = r.host.captureStallMaxUs / 1000;
  row.fixturePaints = r.fixturePaints;
  // A row is a throughput measurement only if the host lived, frames came, the capture thread never
  // stalled for more than 2 s, and no dump was taken (a MiniDumpWriteDump suspends the host for its
  // duration, so a dumped run's latencies are not the product's). Otherwise it is a FAILED run.
  row.dumped = !r.hangDump.empty();
  row.valid = !row.hostDied && r.delivered >= 2 && row.stallMaxMs <= 2000 && !row.dumped;
  row.planState = r.host.rcPlanStates.empty() ? "-" : r.host.rcPlanStates.back();
  return row;
}

void print_rc_row(const RcRow& w) {
  std::printf("RCMATRIX host=%s scene=%s fps=%u R=%u window-mode=%s valid=%d hostExit=%s stallMaxMs=%llu paints=%llu dumped=%d | win1s=%.1f%% win250=%.1f%% win267=%.1f%% sentPayload=%.0fkbps E=%llu/%llu(desired/applied) "
              "effWire=%llu profile=%s plan=%s | deliveredFps=%.1f decodeFps=%.1f gap250=%u decodeGapMax=%.0fms "
              "decodeLatP95=%.0fms decodeLatMax=%.0fms | ev p50/p95/max=%.0f/%.0f/%.0fms m=%u/%u | psnr full=%.2f(min %.2f) "
              "text=%.2f(min %.2f) n=%u at %ux%u | idr/s=%.2f keyMax=%.0fKB keyMean=%.0fKB hostSkips=%llu rcWrites=%u(reconf=%u gov=%u) instances=%u "
              "decErr=%u keyReq=%u nacks=%u\n",
              w.host.c_str(), w.scene.c_str(), w.fps, w.wireBps, w.windowMode.c_str(), w.valid ? 1 : 0,
              w.hostDied ? std::to_string(w.hostExit).c_str() : "alive", (unsigned long long)w.stallMaxMs, (unsigned long long)w.fixturePaints,
              w.dumped ? 1 : 0, w.win1sPct, w.win250Pct, w.win267Pct, w.sentPayloadKbps,
              (unsigned long long)w.desiredEncoder, (unsigned long long)w.appliedEncoder, (unsigned long long)w.effectiveWire,
              w.rcProfile.c_str(), w.planState.c_str(), w.deliveredFps, w.decodeFps, w.gap250, w.decodeGapMaxMs,
              w.decodeLatP95Ms, w.decodeLatMaxMs, w.evP50, w.evP95, w.evMax, w.evMatched, w.evExpected, w.psnrFullMean,
              w.psnrFullMin, w.psnrTextMean, w.psnrTextMin, w.psnrN, w.decodedW, w.decodedH, w.idrPerSec, w.keyMaxKb,
              w.keyMeanKb, (unsigned long long)w.hostSkips, w.rcReconfigureWrites + w.rcGovernorWrites, w.rcReconfigureWrites, w.rcGovernorWrites,
              w.rcInstances, w.decErr, w.keyReq, w.nacks);
}

// r8: --rc-fps / --rc-rate pick ONE cell; --rc-order cand-first runs the candidate before the baseline so
// a GPU/time-order effect is not read as a host difference (the default order is baseline first).
int run_rc_matrix(const std::wstring& candidateHost, const std::wstring& baselineHost, const std::wstring& dir, int seconds,
                  const std::string& only, uint32_t fpsOnly, uint32_t rateOnly, bool candFirst) {
  const RcCell cells[] = {
      {"textscroll", Content::TextScroll, false, 1200.0},        // small text, scrolling (full-frame real text motion)
      {"windowdrag", Content::WindowDrag, false, 1200.0},        // a window dragged over a text page
      {"singlechange", Content::SingleChange, true, 0.0},        // idle -> one localised change
      {"fulltransition", Content::FullTransition, true, 0.0},    // the whole page swaps
      {"motion", Content::Video, false, 1200.0},                 // busy video-like motion (the IDR/burst stress)
  };
  const uint32_t fpsList[] = {30, 60};
  const uint32_t rateList[] = {1'500'000, 3'000'000, 6'000'000};
  std::printf("\n=== encoder-rc-budget C5 matrix: %dx%d page, %d s per run, candidate%s ===\n", kWinW, kWinH, seconds,
              baselineHost.empty() ? " (no baseline host given)" : " vs baseline");
  std::vector<RcRow> rows;
  int idx = 0;
  for (const RcCell& cell : cells) {
    if (!only.empty() && only != cell.label) continue;
    for (uint32_t fps : fpsList) {
      if (fpsOnly != 0 && fps != fpsOnly) continue;
      for (uint32_t rate : rateList) {
        if (rateOnly != 0 && rate != rateOnly) continue;
        auto one = [&](const std::wstring& host, const char* tag) -> RunResult {
          const uint16_t port = remote60::native_poc::e2e::e2e_pick_free_udp_port();
          if (port == 0) return RunResult{};
          const std::wstring runDir = dir + L"rc_" + std::wstring(cell.label, cell.label + std::strlen(cell.label)) + L"_" +
                                      std::to_wstring(fps) + L"_" + std::to_wstring(rate / 100000) + L"_" +
                                      std::wstring(tag, tag + std::strlen(tag)) + L"_" + std::to_wstring(idx++) + L"\\";
          gCurrentHostTag = tag;
          RunResult rr = run_host(host, runDir, cell.content, /*tight=*/true, port, seconds, 0, 1, /*wireCapOn=*/true, rate, fps);
          gCurrentHostTag = "";
          return rr;
        };
        RunResult base, cand;
        RcRow baseRow, candRow;
        const auto run_base = [&]() -> bool {
          if (baselineHost.empty()) return true;
          base = one(baselineHost, "base");
          if (capture_unavailable()) return false;
          baseRow = rc_row_of("baseline", cell, fps, rate, seconds, base);
          print_rc_row(baseRow);
          rows.push_back(baseRow);
          return true;
        };
        const auto run_cand = [&]() -> bool {
          cand = one(candidateHost, "cand");
          if (capture_unavailable()) return false;
          candRow = rc_row_of("candidate", cell, fps, rate, seconds, cand);
          print_rc_row(candRow);
          rows.push_back(candRow);
          return true;
        };
        const bool ok = candFirst ? (run_cand() && run_base()) : (run_base() && run_cand());
        if (!ok) {
          std::printf("CAPTURE-UNAVAILABLE: the matrix stops here (first run without a capture item, even with the visible fallback)\n");
          return 3;
        }
        if (gObserveMode) {
          const std::string tagO = std::string("rc ") + cell.label + " " + std::to_string(fps) + "fps " + std::to_string(rate / 1000) + "k";
          check(candRow.valid, tagO + ": observe: the host stayed alive, delivered frames, no capture stall, no dump (valid row)",
                std::string("hostExit=") + (candRow.hostDied ? std::to_string(candRow.hostExit) : "alive") + " stallMaxMs=" + std::to_string(candRow.stallMaxMs) +
                    " dumped=" + (candRow.dumped ? "1" : "0") + " delivered=" + std::to_string(cand.delivered));
          continue;
        }
        if (gSaveFrames && cell.marker) {
          // r10 (Codex H2): the pictures this cell promised -- per host, with content, not just existence
          auto pgm_line = [&](const char* who, const RunResult& rr) {
            const PgmSummary ps = summarize_pgms(rr.frameDir);
            std::printf("PGM host=%s scene=%s fps=%u R=%u files=%u size=%ux%u darkPct=%.1f first=%s\n", who, cell.label, fps, rate, ps.files, ps.w,
                        ps.h, ps.darkPct, ps.first.empty() ? "-" : std::string(ps.first.begin(), ps.first.end()).c_str());
            return ps;
          };
          const PgmSummary pc = pgm_line("candidate", cand);
          if (!baselineHost.empty()) pgm_line("baseline", base);
          const std::string tag0 = std::string("rc ") + cell.label + " " + std::to_string(fps) + "fps " + std::to_string(rate / 1000) + "k";
          check(pc.files >= 1 && pc.w > 0 && pc.h > 0 && pc.darkPct > 0.5,
                tag0 + ": --save-frames wrote the candidate's event frames and the first one has text ink (not a blank page)",
                "files=" + std::to_string(pc.files) + " size=" + std::to_string(pc.w) + "x" + std::to_string(pc.h) + " darkPct=" + std::to_string(pc.darkPct));
        }
        const RcRow& c = candRow;  // the checks below read the candidate row; the baseline row is baseRow (any order)
        if (cell.marker) {
          // Which events were matched (id:latency ms) and which painted ids were not, for both hosts: a
          // lost event is named, not just counted.
          auto events_line = [&](const char* who, const RunResult& r) {
            std::printf("RCEVENTS host=%s scene=%s fps=%u R=%u painted=%u owed=%u matched:", who, cell.label, fps, rate,
                        r.eventsExpected, r.eventsOwed);
            std::set<uint32_t> seen;
            for (const auto& e : r.eventRecs) { std::printf(" %u:%.0f", e.id, e.latencyUs / 1000.0); seen.insert(e.id); }
            std::printf(" missing:");
            for (uint32_t id = 1; id <= r.eventsExpected; ++id) if (!seen.count(id)) std::printf(" %u", id);
            std::printf(" keyframes=%u decoded=%u hostKeyLines=%zu\n", r.keyFrames, r.realtimeDecoded, r.host.keyframeLines);
          };
          if (!baselineHost.empty()) events_line("baseline", base);
          events_line("candidate", cand);
        }
        const std::string tag = std::string("rc ") + cell.label + " " + std::to_string(fps) + "fps " + std::to_string(rate / 1000) + "k";
        check(cand.launched && cand.hello, tag + ": candidate host started and answered Hello");
        check(c.win1sPct <= 110.0, tag + ": candidate every 1 s wire window <= R +10% (" + std::to_string((int)c.win1sPct) + "%)");
        check(c.win250Pct <= 110.0, tag + ": candidate every 250 ms wire window <= R +10% (" + std::to_string((int)c.win250Pct) + "%)");
        check(cand.realtimeDecoded >= 2 && cand.decodeErrors == 0, tag + ": candidate decodes without error");
        check(c.desiredEncoder > 0 && c.desiredEncoder < rate, tag + ": the host's encoder budget E is below R (E=" +
                                                                   std::to_string(c.desiredEncoder) + ")");
        check(c.effectiveWire == rate, tag + ": the wire cap in force is R itself, not E (effWire=" + std::to_string(c.effectiveWire) + ")");
        // Responsiveness and loss are judged AGAINST THE BASELINE where one ran: a 1080p text scroll at
        // 1.5 Mbps saturates the wire on both hosts (the AMD encoder generates several times its mean
        // under motion and the gate skips frames to hold the cap -- pre-existing, not this change), so an
        // absolute ceiling would fail both alike and say nothing about the candidate. The candidate must
        // not be worse than the baseline beyond a tolerance; without a baseline the absolute ceiling holds.
        const RcRow* b = baselineHost.empty() ? nullptr : &baseRow;
        // r10 (Codex H1): validity is a condition, not a note. An invalid candidate row (dead host, no frame,
        // capture stall, dump) is a FAIL -- or, in a diagnostic phase, DIAGNOSTIC-COLLECTED (exit 4, never
        // PASS). An invalid comparison baseline makes every baseline-relative check INCONCLUSIVE = FAIL: a
        // stalled baseline's low fps or big gap must never be the candidate's licence. The candidate's own
        // wire checks above stand on their own.
        const std::string whyInvalid = std::string(c.hostDied ? "host died (exit " + std::to_string(c.hostExit) + ") " : "") +
                                       (c.dumped ? "dumped " : "") + (c.stallMaxMs > 2000 ? "capture stalled " : "") +
                                       (cand.delivered < 2 ? "no frames " : "");
        if (!c.valid) {
          if (gDiagnosticMode) {
            ++gDiagnosticCollected;
            std::printf("DIAGNOSTIC-COLLECTED %s: the candidate row is invalid (%s) -- the stall evidence of this run (dump=%s); not a PASS\n",
                        tag.c_str(), whyInvalid.c_str(), cand.hangDump.empty() ? "none" : cand.hangDump.c_str());
          } else {
            check(false, tag + ": candidate row is a valid measurement", "INVALID: " + whyInvalid + (cand.hangDump.empty() ? "" : "dump=" + cand.hangDump));
          }
        }
        const bool baselineInvalid = b && !b->valid;
        if (baselineInvalid) {
          check(false, tag + ": baseline row is a valid measurement (the baseline-relative checks below are INCONCLUSIVE, not a licence)",
                std::string("INVALID baseline: ") + (b->hostDied ? "host died (exit " + std::to_string(b->hostExit) + ") " : "") +
                    (b->dumped ? "dumped " : "") + (b->stallMaxMs > 2000 ? "capture stalled " : ""));
        }
        if (!c.valid) {
          // nothing below is a measurement of this candidate run
        } else if (cell.marker) {
          // r2 (Codex V1): the owed events are the IDs 1..owed (ids are allocated in paint order, and
          // owed = those painted >= 1 s before the end); each one must have been decoded -- a count
          // cannot hide a missing id behind a late extra one, and a loss on the baseline is reported
          // beside the candidate, never used as the candidate's licence.
          const std::vector<uint32_t> missing = missing_owed_ids(cand);
          std::string missingStr;
          for (uint32_t id : missing) missingStr += (missingStr.empty() ? "" : ",") + std::to_string(id);
          check(cand.eventsOwed > 0 && missing.empty(),
                tag + ": candidate decoded EVERY owed event id (owed 1.." + std::to_string(cand.eventsOwed) +
                    (missing.empty() ? "" : ", missing " + missingStr) + ")");
          if (b) {
            const std::vector<uint32_t> baseMissing = missing_owed_ids(base);
            if (!baseMissing.empty()) std::printf("       (baseline missed owed id(s) in this cell: %zu -- reported, not a licence)\n", baseMissing.size());
          }
          const double stallBound = (b && !baselineInvalid) ? std::max(2500.0, b->evMax * 1.25 + 250.0) : 2500.0;
          check(c.evMax <= stallBound, tag + ": candidate: no new multi-second stall on a change (max=" +
                                           std::to_string((int)c.evMax) + "ms bound=" + std::to_string((int)stallBound) + ")");
          check(c.psnrN > 0, tag + ": a matched PSNR was taken against the painted page");
        } else if (cell.maxDecodeGapMs > 0) {
          const double gapBound = (b && !baselineInvalid) ? std::max(cell.maxDecodeGapMs, b->decodeGapMaxMs * 1.25 + 250.0) : cell.maxDecodeGapMs;
          check(c.decodeGapMaxMs <= gapBound, tag + ": candidate max realtime-decode gap not worse than baseline/ceiling (" +
                                                  std::to_string((int)c.decodeGapMaxMs) + "ms bound=" + std::to_string((int)gapBound) + ")");
        }
        if (b && c.valid && !baselineInvalid) {
          check(c.deliveredFps >= b->deliveredFps * 0.75 - 0.5, tag + ": candidate delivered fps not below 75% of baseline (" +
                                                                    std::to_string((int)c.deliveredFps) + " vs " +
                                                                    std::to_string((int)b->deliveredFps) + ")");
          // r9 positive condition (Codex r8 contract): on the motion scene the candidate must deliver at
          // least the baseline's rate -- 10% tolerance for a single noisy pair, and ONLY between two valid
          // rows: a dead, stalled or dumped host on either side is a failed run, not a rate to compare.
          if (std::strcmp(cell.label, "motion") == 0) {
            // both rows valid here (the invalid cases failed above); a single-pair filter, not a shipping bound
            check(c.deliveredFps >= b->deliveredFps * 0.9 - 0.5,
                  tag + ": motion: candidate delivered fps >= baseline (10% tolerance; r9 positive condition, one pair) (" +
                      std::to_string((int)c.deliveredFps) + " vs " + std::to_string((int)b->deliveredFps) + ")");
          }
        }
        if (!baselineHost.empty()) {
          const RcRow& b = baseRow;
          std::printf("RCDELTA scene=%s fps=%u R=%u | win1s %.1f->%.1f%% win250 %.1f->%.1f%% | gap250 %u->%u decodeGapMax %.0f->%.0fms "
                      "decodeLatP95 %.0f->%.0fms | ev p95 %.0f->%.0fms max %.0f->%.0fms | psnr text %.2f->%.2f full %.2f->%.2f | "
                      "keyMax %.0f->%.0fKB sentPayload %.0f->%.0fkbps hostSkips %llu->%llu\n",
                      cell.label, fps, rate, b.win1sPct, c.win1sPct, b.win250Pct, c.win250Pct, b.gap250, c.gap250, b.decodeGapMaxMs,
                      c.decodeGapMaxMs, b.decodeLatP95Ms, c.decodeLatP95Ms, b.evP95, c.evP95, b.evMax, c.evMax, b.psnrTextMean,
                      c.psnrTextMean, b.psnrFullMean, c.psnrFullMean, b.keyMaxKb, c.keyMaxKb, b.sentPayloadKbps, c.sentPayloadKbps,
                      (unsigned long long)b.hostSkips, (unsigned long long)c.hostSkips);
        }
      }
    }
  }
  return 0;
}

// encoder-rc-budget C5 transitions on the candidate: the paths that build or re-tune an instance.
//   T1 6M -> 1.5M (down), then 1.5M -> 6M (up): R applies at once (existing same-clock marker, every
//      window after it within the new R); E follows (live on the legacy profile, a new instance on
//      a static one) and the host logs desired/applied E for each.
//   T2 the SAME target sent twice: no additional RC write, no additional instance (the C3 no-op).
//   T3 an fps change 30 -> 60: a new instance, and that instance logs its plan (pre-types stage on the
//      NVIDIA profile; init on legacy) with mean == the host's E.
int run_rc_transitions(const std::wstring& hostExe, const std::wstring& dir, int seconds) {
  std::printf("\n=== encoder-rc-budget C5 transitions (candidate) ===\n");
  const int runSec = std::max(seconds, 16);
  {
    const uint16_t port = remote60::native_poc::e2e::e2e_pick_free_udp_port();
    std::vector<TuneStep> steps = {{static_cast<uint32_t>(runSec / 4) * 1000u, 1'500'000, 0},
                                   {static_cast<uint32_t>(runSec / 4) * 1000u, 1'500'000, 0},   // the same target again (T2)
                                   {static_cast<uint32_t>(runSec * 3 / 5) * 1000u, 6'000'000, 0}};
    const RunResult r = run_host(hostExe, dir + L"t_downup\\", Content::FullTransition, true, port, runSec, 0, 1, true,
                                 6'000'000, 30, 0, 0, false, &steps);
    if (capture_unavailable()) {
      std::printf("CAPTURE-UNAVAILABLE: no capture item even with the visible fallback; the remaining transition runs are skipped\n");
      return 3;
    }
    std::printf("TRANSITION down/up: window-mode=%s tunesSent=%zu budgetActions=", r.windowMode.c_str(), r.tuneSentUs.size());
    for (const auto& a : r.host.budgetActions) std::printf("%s,", a.c_str());
    std::printf(" rcInstances=%u rcReconfigureWrites=%u deferred=%u effWire(last)=%llu E(desired/applied)=%llu/%llu profile=%s "
                "planMeans=",
                r.host.rcInstances, r.host.rcReconfigureWrites, r.host.deferredRetargets, (unsigned long long)r.host.effectiveWire,
                (unsigned long long)r.host.desiredEncoder, (unsigned long long)r.host.appliedEncoder, r.host.rcProfile.c_str());
    for (uint64_t m : r.host.rcPlanMeans) std::printf("%llu,", (unsigned long long)m);
    std::printf(" events=%u/%u decErr=%u\n", r.eventsMatched, r.eventsExpected, r.decodeErrors);
    check(r.tuneSentUs.size() == 3, "transitions: the three runtime tunes were sent");
    // r2 (Codex V1): the R judgement starts at the host's OWN same-clock apply marker for each change
    // (`wirecap applied bitrate=... appliedQpcUs=...`), not at the tune's send time plus a guess. The
    // first marker at 1.5M is the downshift's apply, the next marker at 6M the upshift's; every
    // complete window from the down marker to the up marker must respect 1.5M, and from the up marker
    // on, 6M. The request-to-apply latency of each is reported.
    uint64_t downApplyUs = 0, upApplyUs = 0;
    for (const auto& m : r.host.wireCapApplies) {
      if (!downApplyUs && m.first == 1'500'000) downApplyUs = m.second;
      else if (downApplyUs && !upApplyUs && m.first == 6'000'000 && m.second > downApplyUs) upApplyUs = m.second;
    }
    check(downApplyUs != 0 && upApplyUs != 0, "transitions: the host logged a same-clock wire-cap apply for the downshift AND the upshift",
          "markers=" + std::to_string(r.host.wireCapApplies.size()));
    if (r.tuneSentUs.size() == 3 && downApplyUs && upApplyUs)
      std::printf("TRANSITION apply latency: down request->apply=%.0fms up request->apply=%.0fms\n",
                  (static_cast<double>(downApplyUs) - static_cast<double>(r.tuneSentUs[0])) / 1000.0,
                  (static_cast<double>(upApplyUs) - static_cast<double>(r.tuneSentUs[2])) / 1000.0);
    std::vector<std::pair<uint64_t, uint32_t>> low, high;
    for (const auto& e : r.wireEvents) {
      if (downApplyUs && upApplyUs && e.first >= downApplyUs && e.first < upApplyUs) low.push_back(e);
      if (upApplyUs && e.first >= upApplyUs) high.push_back(e);
    }
    const double low1s = window_peak_bps(low, 1'000'000);
    const double low250 = window_peak_bps(low, 250'000);
    check(!low.empty() && low1s <= 1'500'000 * 1.10,
          "transitions: from the DOWN apply marker, every 1 s window <= 1.5M +10% (" + std::to_string((int)low1s) + ")");
    check(!low.empty() && low250 <= 1'500'000 * 1.10,
          "transitions: from the DOWN apply marker, every 250 ms window <= 1.5M +10% (" + std::to_string((int)low250) + ")");
    const double high1s = window_peak_bps(high, 1'000'000);
    check(!high.empty() && high1s <= 6'000'000 * 1.10, "transitions: from the UP apply marker, every 1 s window <= 6M +10%");
    check(r.host.effectiveWire == 6'000'000, "transitions: the wire cap ends at the upshift's R (6M)");
    check(r.host.desiredEncoder > 0 && r.host.desiredEncoder < 6'000'000 && r.host.appliedEncoder == r.host.desiredEncoder,
          "transitions: after the upshift the applied encoder E equals the desired E for 6M, below R");
    // T2: the repeated target must not have produced another RC write / instance. Count what a change
    // costs on this profile: legacy = one reconfigure write per change (2 changes), static = one instance
    // per change. Either way the duplicate adds nothing.
    // A user bitrate tune re-picks the encode ladder (a 1080p page goes to a smaller box at 1.5M), so
    // on this page a change is a REBUILD on every profile (geometry changed), with the live write only
    // when the size happens to stay. Either way: two distinct changes = two transitions, and the
    // repeated target adds none -- that is the C3 no-op being tested, counted as writes + rebuilds.
    const uint32_t transitions = r.host.rcReconfigureWrites + (r.host.rcInstances > 0 ? r.host.rcInstances - 1 : 0);
    check(transitions == 2, "transitions: exactly two encoder transitions (live writes + rebuilds) for two distinct changes -- "
                            "the repeated target produced none (writes=" + std::to_string(r.host.rcReconfigureWrites) +
                            " instances=" + std::to_string(r.host.rcInstances) + ")");
    const bool legacy = r.host.rcProfile == "legacy";
    if (!legacy) check(r.host.rcReconfigureWrites == 0, "transitions (static profile): no runtime RC write at all");
    check(r.host.rcPlanMeans.size() == r.host.rcInstances, "transitions: every instance logged its plan readback");
    {
      const std::vector<uint32_t> missing = missing_owed_ids(r);  // r3 T1: each owed id, not a count
      std::string ms;
      for (uint32_t id : missing) ms += (ms.empty() ? "" : ",") + std::to_string(id);
      check(r.eventsOwed > 0 && missing.empty(), "transitions: EVERY owed event id decoded across both changes (owed 1.." +
                                                     std::to_string(r.eventsOwed) + (ms.empty() ? "" : ", missing " + ms) + ")");
    }
    check(r.decodeErrors == 0, "transitions: no decode error across the changes");
    // r9 (③): the same target asked twice must not cost a key; the one key per encoder change comes from
    // ApplyTarget (reason encoder_target). A key whose reasons are "tune" alone is the caller's extra.
    check(r.host.keyTuneOnlyLines == 0, "transitions: no key was requested for a repeated/no-op/deferred tune (tune-only keys=" +
                                            std::to_string(r.host.keyTuneOnlyLines) + ")");
  }
  {
    const uint16_t port = remote60::native_poc::e2e::e2e_pick_free_udp_port();
    std::vector<TuneStep> steps = {{static_cast<uint32_t>(runSec / 2) * 1000u, 0, 60}};
    const RunResult r = run_host(hostExe, dir + L"t_fps\\", Content::FullTransition, true, port, runSec, 0, 1, true, 3'000'000, 30,
                                 0, 0, false, &steps);
    if (capture_unavailable()) {
      std::printf("CAPTURE-UNAVAILABLE: the fps transition run had no capture item; the remaining transition runs are skipped\n");
      return 3;
    }
    std::printf("TRANSITION fps 30->60: rcInstances=%u planPostStart=%u planStates=", r.host.rcInstances, r.host.rcPlanPostStart);
    for (const auto& s : r.host.rcPlanStates) std::printf("%s,", s.c_str());
    std::printf(" planMeans=");
    for (uint64_t m : r.host.rcPlanMeans) std::printf("%llu,", (unsigned long long)m);
    std::printf(" E=%llu events=%u/%u decErr=%u\n", (unsigned long long)r.host.desiredEncoder, r.eventsMatched, r.eventsExpected,
                r.decodeErrors);
    check(r.host.rcInstances == 2, "fps transition: the fps change built exactly one new instance (2 in the run)");
    // One static-profile check the Home bundle needs here too: the fps rebuild's plan is pre-types and
    // read back accepted on that profile (legacy logs no pre-types stage by design).
    if (r.host.rcProfile == "nvidia_pre_types")
      check(r.host.rcPlanPreTypes == 2 && r.host.rcPlanStates.size() == 2 && r.host.rcPlanStates[1] == "accepted",
            "fps transition (nvidia): both instances set their plan before the types and the rebuilt one reads back accepted");
    check(r.host.rcPlanPostStart == 2, "fps transition: every instance logged its plan readback (post-types)");
    // E depends on the frame rate (r2: the per-AU model), so the 30 fps instance and the 60 fps instance
    // are OWED different means: each must equal E(R, its fps) from the same budget function the host
    // uses, and neither may be R.
    WireGeometry g;
    g.mtuBytes = 1200;
    g.chunkHeaderBytes = static_cast<uint32_t>(sizeof(UdpVideoChunkHeader));
    g.fecGroupSize = kUdpVideoFecGroupSize;
    const uint32_t e30 = encoder_payload_budget_bps(3'000'000, g, EncoderBudgetPolicy{}, 30);
    const uint32_t e60 = encoder_payload_budget_bps(3'000'000, g, EncoderBudgetPolicy{}, 60);
    const bool meansMatch = r.host.rcPlanMeans.size() == 2 && r.host.rcPlanMeans[0] == e30 && r.host.rcPlanMeans[1] == e60 &&
                            r.host.desiredEncoder == e60 && e30 < 3'000'000 && e60 < 3'000'000;
    check(meansMatch, "fps transition: each instance's plan mean is E(R, its fps) -- " + std::to_string(e30) + " at 30, " +
                          std::to_string(e60) + " at 60 -- not R",
          r.host.rcPlanMeans.size() == 2 ? std::to_string(r.host.rcPlanMeans[0]) + "," + std::to_string(r.host.rcPlanMeans[1]) : "n/a");
    {
      const std::vector<uint32_t> missing = missing_owed_ids(r);  // r3 T1
      std::string ms;
      for (uint32_t id : missing) ms += (ms.empty() ? "" : ",") + std::to_string(id);
      check(r.eventsOwed > 0 && missing.empty(), "fps transition: EVERY owed event id decoded (owed 1.." + std::to_string(r.eventsOwed) +
                                                     (ms.empty() ? "" : ", missing " + ms) + ")");
    }
    check(r.decodeErrors == 0, "fps transition: no decode error");
  }
  return 0;
}

// encoder-rc-budget C3/C5 E-only transitions at a FIXED geometry: the wire budget R moves while
// nothing but E can change (1080p, 30 fps, 5..6 Mbps stays on the top ladder rung). Both profiles
// receive the same ms schedule; what they must do with it differs (r4 H1 -- the same sequence is
// pinned first on the real manager + scheduler + fake MFT in host_encoder_apply_target_test [7], this
// is that sequence on the product host + real MFT, timed against the host's REAL 2 s interval):
//
//   t(ms)  request        static profile (NVIDIA: no live mean)     legacy (AMD: live mean)
//   4000   B = 5.0M       >=2 s after startup -> adopt (inst 2)       live write
//   5000   A = 6.0M       inside [4000,6000) -> deferred              live write
//   5500   B = 5.0M       desired == applied -> deferral CANCELLED    live write
//   9000   C = 5.5M       interval over -> adopt (inst 3)             live write
//  10000   D = 5.2M       inside [9000,11000) -> deferred             live write
//  10500   E = 5.4M       still inside -> deferred, LATEST wins       live write
// ~11000   (host tick)    due -> E adopted ONCE (inst 4); D never built
//  14000   E = 5.4M       repeat -> nothing                            nothing (same mean)
//
// So: static = 4 instances / 1 deferred adoption / 0 runtime writes; legacy = 1 instance / 6 live
// writes / 0 rebuilds. Both: R follows every DISTINCT request at once (six apply markers, in order),
// the encode size never changes, every owed event decodes. Startup no longer owes a rebuild to the
// first tune (the startup keyint goes through the same key-period policy, r4), so a tune that changes
// nothing changes nothing on either profile.
int run_rc_eonly(const std::wstring& hostExe, const std::wstring& dir, int seconds) {
  if (capture_unavailable()) {
    std::printf("CAPTURE-UNAVAILABLE: the E-only run is skipped\n");
    return 3;
  }
  std::printf("\n=== encoder-rc-budget r4 E-only transitions at a fixed geometry (6M <-> 5M <-> 5.5M <-> 5.2M <-> 5.4M, 1080p) ===\n");
  const int runSec = std::max(seconds, 18);
  const uint16_t port = remote60::native_poc::e2e::e2e_pick_free_udp_port();
  std::vector<TuneStep> steps = {{4000, 5'000'000, 0}, {5000, 6'000'000, 0},  {5500, 5'000'000, 0},  {9000, 5'500'000, 0},
                                 {10000, 5'200'000, 0}, {10500, 5'400'000, 0}, {14000, 5'400'000, 0}};
  const RunResult r = run_host(hostExe, dir + L"t_eonly\\", Content::SingleChange, true, port, runSec, 0, 1, true, 6'000'000, 30, 0, 0,
                               false, &steps);
  if (capture_unavailable()) {
    std::printf("CAPTURE-UNAVAILABLE: the E-only run had no capture item; its checks are skipped\n");
    return 3;
  }
  std::printf("EONLY: window-mode=%s tunesSent=%zu budgetActions=", r.windowMode.c_str(), r.tuneSentUs.size());
  for (const auto& a : r.host.budgetActions) std::printf("%s,", a.c_str());
  std::printf(" rcInstances=%u rcReconfigureWrites=%u rcGovernorWrites=%u deferredApplied=%u effWire(last)=%llu E(desired/applied)=%llu/%llu profile=%s sizes=",
              r.host.rcInstances, r.host.rcReconfigureWrites, r.host.rcGovernorWrites, r.host.deferredRetargets,
              (unsigned long long)r.host.effectiveWire, (unsigned long long)r.host.desiredEncoder, (unsigned long long)r.host.appliedEncoder,
              r.host.rcProfile.c_str());
  for (const auto& s : r.host.sizes) std::printf("%s,", s.c_str());
  std::printf(" events owed=%u matched=%u decErr=%u\n", r.eventsOwed, r.eventsMatched, r.decodeErrors);
  // The adoption markers on the harness clock: when each tune left and when each R apply landed (the
  // host's same-clock marker), so a reader sees where in the 2 s interval each request fell.
  if (!r.tuneSentUs.empty()) {
    const uint64_t t0 = r.tuneSentUs.front() - 4000ull * 1000ull;  // the schedule's origin: B left at +4000 ms
    std::printf("EONLY tunes(ms from t0):");
    for (uint64_t t : r.tuneSentUs) std::printf(" %+.0f", (static_cast<double>(t) - static_cast<double>(t0)) / 1000.0);
    std::printf("\nEONLY wirecap applies (R@ms from t0):");
    for (const auto& m : r.host.wireCapApplies)
      std::printf(" %llu@%+.0f", (unsigned long long)m.first, (static_cast<double>(m.second) - static_cast<double>(t0)) / 1000.0);
    std::printf("\n");
  }
  check(r.tuneSentUs.size() == 7, "e-only: the seven runtime tunes were sent");
  check(r.host.sizes.size() == 1 && r.host.sizes[0] == "1920x1080",
        "e-only: the encode size never changed (1920x1080 throughout; the ladder leaves 5M..6M alone)",
        r.host.sizes.empty() ? "no stats size" : r.host.sizes.front() + ".." + r.host.sizes.back());
  // R moves at once for every DISTINCT request, in order, and not for the repeat.
  {
    std::vector<uint64_t> seq;
    for (const auto& m : r.host.wireCapApplies) seq.push_back(m.first);
    const std::vector<uint64_t> want = {5'000'000, 6'000'000, 5'000'000, 5'500'000, 5'200'000, 5'400'000};
    check(seq == want, "e-only: the wire cap followed every distinct R at once, in order (5M,6M,5M,5.5M,5.2M,5.4M), and not the repeat",
          std::to_string(seq.size()) + " markers");
  }
  check(r.host.effectiveWire == 5'400'000 && r.host.desiredEncoder > 0 && r.host.desiredEncoder < 5'400'000,
        "e-only: the run ends on R=5.4M with E below it");
  const bool legacy = r.host.rcProfile == "legacy";
  const uint32_t rebuilds = r.host.rcInstances > 0 ? r.host.rcInstances - 1 : 0;
  check(!(legacy && r.host.budgetActions.size() > 1 && r.host.budgetActions[1] == "reinit"),
        "e-only: the first tune did NOT rebuild the encoder for a reason outside E (startup keyint follows the key-period policy, r4)");
  if (legacy) {
    check(r.host.rcReconfigureWrites == 6, "e-only (legacy): six distinct changes = six LIVE mean writes; the repeat produced none (writes=" +
                                               std::to_string(r.host.rcReconfigureWrites) + ")");
    check(rebuilds == 0, "e-only (legacy): no rebuild at all -- one instance for the whole run (" + std::to_string(r.host.rcInstances) + ")");
    bool allLive = r.host.budgetActions.size() == 7;  // startup + six changes; the repeat logs nothing
    for (size_t i = 1; i < r.host.budgetActions.size() && allLive; ++i) allLive = r.host.budgetActions[i] == "live";
    check(allLive, "e-only (legacy): every logged change is a LIVE mean change (" + std::to_string(r.host.budgetActions.size()) + " actions)");
    check(r.host.appliedEncoder == r.host.desiredEncoder, "e-only (legacy): the instance holds E(5.4M)");
  } else {
    check(r.host.rcInstances == 4, "e-only (static): startup + B + C + E adopted = 4 instances; A was cancelled, D was superseded, the repeat "
                                   "built nothing (" + std::to_string(r.host.rcInstances) + " instances)");
    check(r.host.deferredRetargets == 1, "e-only (static): the deferred retarget was applied exactly once, with the latest target (" +
                                             std::to_string(r.host.deferredRetargets) + ")");
    check(r.host.rcReconfigureWrites == 0, "e-only (static): no runtime RC write on a running instance");
    check(r.host.appliedEncoder == r.host.desiredEncoder, "e-only (static): the adopted instance holds E(5.4M), not E(5.2M)");
    // startup, B reinit, A deferred, B none (the cancel; R changed so it is logged), C reinit, D
    // deferred, E deferred, the due tick's reinit. The repeat changes neither R nor E and logs nothing.
    const std::vector<std::string> want = {"startup", "reinit", "deferred", "none", "reinit", "deferred", "deferred", "reinit"};
    std::string got;
    for (const auto& a : r.host.budgetActions) got += a + ",";
    check(r.host.budgetActions == want, "e-only (static): the action sequence is adopt/defer/cancel/adopt/defer/defer(latest)/due-adopt", got);
  }
  {
    const std::vector<uint32_t> missing = missing_owed_ids(r);
    std::string ms;
    for (uint32_t id : missing) ms += (ms.empty() ? "" : ",") + std::to_string(id);
    check(r.eventsOwed > 0 && missing.empty(), "e-only: EVERY owed event id decoded across the changes (owed 1.." + std::to_string(r.eventsOwed) +
                                                   (ms.empty() ? "" : ", missing " + ms) + ")");
  }
  check(r.decodeErrors == 0, "e-only: no decode error");
  check(r.host.keyTuneOnlyLines == 0, "e-only: no key was requested for a repeated/no-op/deferred tune (tune-only keys=" +
                                          std::to_string(r.host.keyTuneOnlyLines) + ")");
  // Where the host's forced keys went: every change on the static profile is one instance = one IDR;
  // on legacy a live change owes one forced key each. Reported, the exact count depends on the scene.
  std::printf("EONLY keys: hostKeyframeLines=%zu rxKeyFrames=%u\n", r.host.keyframeLines, r.keyFrames);
  return 0;
}

// completion criterion 3: the product-equivalent host + real MFT + real decoder, across the scenario
// matrix, each with the cap ON and every 1 s / 250 ms window measured on the real UDP receive.
int run_matrix(const std::wstring& hostExe, const std::wstring& dir, int seconds) {
  struct Scn {
    const char* label;
    Content content;
    uint32_t bitrate, fps, lossPermille;
    double maxDecodeGapMs;  // realtime-decode responsiveness ceiling; 0 = no gate (static/loss lenient)
    double maxDecodeLatMs;  // input->screen present-latency ceiling (single-change); 0 = no gate
  };
  const Scn scns[] = {
      {"6M/60-motion", Content::Video, 6'000'000, 60, 0, 500.0, 0.0},
      {"6M/60-static", Content::StaticText, 6'000'000, 60, 0, 0.0, 0.0},
      {"1.5M/30-motion", Content::Video, 1'500'000, 30, 0, 900.0, 0.0},
      {"6M/60-loss5%-nack", Content::Video, 6'000'000, 60, 50, 0.0, 0.0},
      {"6M/60-windowdrag", Content::WindowDrag, 6'000'000, 60, 0, 600.0, 0.0},
      {"3M/60-partialvideo", Content::PartialVideo, 3'000'000, 60, 0, 700.0, 0.0},
      // r5 required pre-release measurement: the 3 desktop-like types x {6M/60, 1.5M/30}, realtime
      // decode. Text scroll is continuous motion -> gate the decode GAP. Single change and full
      // transition are DISCRETE (deliberate ~2 s idle between events), so the decode gap is dominated
      // by that legitimate idle, not a freeze -- they gate the input->screen PRESENT LATENCY of the
      // change/transition frame instead (decodeLatMax), never the gap.
      {"6M/60-textscroll", Content::TextScroll, 6'000'000, 60, 0, 600.0, 0.0},
      {"1.5M/30-textscroll", Content::TextScroll, 1'500'000, 30, 0, 1200.0, 0.0},
      // NOTE: the single-change present latency measures the host's idle->change capture/kick
      // surfacing path (the cap is far from binding here -- win1s ~18%, queueDepth 0), NOT the wire
      // cap. The ceiling catches a gross multi-second freeze; the measured value (6M/60 ~1 s, 1.5M/30
      // ~0.47 s, from the host kick latency on an idle screen) is reported for judgement, not hidden.
      {"6M/60-singlechange", Content::SingleChange, 6'000'000, 60, 0, 0.0, 1800.0},
      {"1.5M/30-singlechange", Content::SingleChange, 1'500'000, 30, 0, 0.0, 1800.0},
      {"6M/60-fulltransition", Content::FullTransition, 6'000'000, 60, 0, 0.0, 1200.0},
      {"1.5M/30-fulltransition", Content::FullTransition, 1'500'000, 30, 0, 0.0, 1200.0},
  };
  for (const Scn& s : scns) {
    const uint16_t port = remote60::native_poc::e2e::e2e_pick_free_udp_port();
    if (port == 0) continue;
    std::wstring safe(s.label, s.label + std::strlen(s.label));
    for (wchar_t& c : safe)
      if (c == L'/' || c == L'%' || c == L'.') c = L'_';  // a run-dir name, not a path
    const std::wstring runDir = dir + L"m_" + safe + L"\\";
    const RunResult r = run_host(hostExe, runDir, s.content, /*tight=*/true, port, seconds, s.lossPermille, 1,
                                 /*wireCapOn=*/true, s.bitrate, s.fps);
    matrix_metrics(s.label, s.bitrate, r, s.maxDecodeGapMs, s.maxDecodeLatMs);
  }
  // Runtime 6 -> 1.5 Mbps downshift mid-run.
  const uint16_t dport = remote60::native_poc::e2e::e2e_pick_free_udp_port();
  if (dport != 0) {
    const RunResult r = run_host(hostExe, dir + L"m_downshift\\", Content::Video, /*tight=*/true, dport,
                                 std::max(seconds, 10), 0, 1, /*wireCapOn=*/true, 6'000'000, 60,
                                 /*downshiftBitrate=*/1'500'000, /*downshiftAtSec=*/static_cast<uint32_t>(std::max(seconds, 10) / 2));
    matrix_metrics_downshift(r);
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  // r9: offline modes -- no hosts, no WSA/MF.
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--pe-id" && i + 1 < argc) {  // RSDS GUID/Age of an exe, for identity.txt
      std::wstring exe(argv[i + 1], argv[i + 1] + std::strlen(argv[i + 1]));
      return remote60::native_poc::e2e::pe_identity(exe);
    }
    if (std::string(argv[i]) == "--dump-check" && i + 1 < argc) {
      std::wstring dump(argv[i + 1], argv[i + 1] + std::strlen(argv[i + 1]));
      std::wstring symbols;
      std::wstring hostModule = L"GNLinkStream.exe";
      for (int j = 1; j + 1 < argc; ++j) {
        if (std::string(argv[j]) == "--symbols") symbols.assign(argv[j + 1], argv[j + 1] + std::strlen(argv[j + 1]));
        if (std::string(argv[j]) == "--host-module") hostModule.assign(argv[j + 1], argv[j + 1] + std::strlen(argv[j + 1]));
      }
      bool verbose = false;
      for (int j = 1; j < argc; ++j) if (std::string(argv[j]) == "--dump-verbose") verbose = true;
      return remote60::native_poc::e2e::dump_check(dump, symbols, hostModule, verbose);
    }
  }
  std::wstring hostExe;
  int seconds = 12;
  std::string contentArg = "all";
  uint32_t lossPermille = 0;
  uint32_t lossSeed = 1;
  bool keepDir = false;
  bool matrix = false;
  bool paired = false;
  bool v2 = false;
  bool rcMatrix = false;
  bool rcTransitions = false;
  std::string rcOnly;
  uint32_t rcFps = 0, rcRate = 0;
  bool rcCandFirst = false;
  bool rcDiagnostic = false;
  std::wstring baselineHost;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--rc-matrix") {
      rcMatrix = true;
    } else if (a == "--rc-transitions") {
      rcTransitions = true;
    } else if (a == "--rc-only" && i + 1 < argc) {
      rcOnly = argv[++i];
    } else if (a == "--rc-fps" && i + 1 < argc) {
      rcFps = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));  // r8: one fps of the matrix
    } else if (a == "--rc-rate" && i + 1 < argc) {
      rcRate = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));  // r8: one R (bps) of the matrix
    } else if (a == "--rc-order" && i + 1 < argc) {
      rcCandFirst = std::string(argv[++i]) == "cand-first";  // r8: candidate before baseline
    } else if (a == "--rc-diagnostic") {
      rcDiagnostic = true;  // r10: the ORIGINAL candidate's stall evidence run; invalid candidate rows = COLLECTED (exit 4), not PASS
    } else if (a == "--host-log" && i + 1 < argc) {  // r11: nul | run | dir:<path>
      const std::string v = argv[++i];
      if (v == "nul") gHostLogDest = HostLogDest::Nul;
      else if (v.rfind("dir:", 0) == 0) { gHostLogDest = HostLogDest::Dir; gHostLogDir.assign(v.begin() + 4, v.end()); CreateDirectoryW(gHostLogDir.c_str(), nullptr); }
      else gHostLogDest = HostLogDest::RunDir;
    } else if (a == "--no-log-poll") {
      gNoLogPoll = true;  // r11
    } else if (a == "--rc-observe") {
      gObserveMode = true;  // r11: liveness only (H0)
    } else if (a == "--no-dump") {
      gNoDump = true;  // r11
    } else if (a == "--low-fps-dump") {
      gLowFpsDump = true;  // r11: opt-in
    } else if (a == "--save-frames") {
      gSaveFrames = true;  // decoded event frames as PGM beside each run's host.log (needs --keep-dir)
    } else if (a == "--window" && i + 1 < argc) {
      // encoder-rc-budget C5: the fixture page size, e.g. 1920x1080 (default 640x360).
      int w = 0, h = 0;
      if (std::sscanf(argv[++i], "%dx%d", &w, &h) == 2) set_fixture_window(w, h);
    } else if (a == "--host" && i + 1 < argc) {
      const std::string v = argv[++i];
      hostExe.assign(v.begin(), v.end());
    } else if (a == "--baseline" && i + 1 < argc) {
      const std::string v = argv[++i];
      baselineHost.assign(v.begin(), v.end());
    } else if (a == "--paired") {
      paired = true;
    } else if (a == "--v2") {
      v2 = true;
    } else if (a == "--seconds" && i + 1 < argc) {
      seconds = std::max(4, std::atoi(argv[++i]));
    } else if (a == "--content" && i + 1 < argc) {
      contentArg = argv[++i];
    } else if (a == "--loss-permille" && i + 1 < argc) {
      lossPermille = static_cast<uint32_t>(std::atoi(argv[++i]));
    } else if (a == "--loss-seed" && i + 1 < argc) {
      lossSeed = static_cast<uint32_t>(std::atoi(argv[++i]));
    } else if (a == "--keep-dir") {
      keepDir = true;
    } else if (a == "--matrix") {
      matrix = true;
    }
  }
  char allow[8]{};
  if (GetEnvironmentVariableA("REMOTE60_ALLOW_HOST_E2E", allow, sizeof(allow)) == 0 || std::string(allow) != "1") {
    std::printf("SKIP fec_single_chunk_host_e2e_test: starts a real (isolated) GNLinkStream.exe capturing a window of this"
                " process.\n      Set REMOTE60_ALLOW_HOST_E2E=1 to run it.\n\nRESULT: SKIPPED\n");
    return kE2eSkippedExit;
  }
  WSADATA wsa{};
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;
  if (FAILED(MFStartup(MF_VERSION))) {
    std::printf("FAIL MFStartup\n");
    return 1;
  }
  // Staged inside the repository's test scratch root, never %TEMP%; each run's host gets its own
  // subdirectory beneath it. Removed when main returns, whichever way it returns, unless --keep-dir.
  remote60::native_poc::e2e::StagingDir staging;
  if (!staging.Create(L"fec_e2e")) {
    std::printf("FAIL  %s\n", staging.why().c_str());
    return 1;
  }
  staging.set_keep(keepDir, keepDir ? "--keep-dir" : "");
  const std::wstring dir = staging.path();
  const std::wstring me = self_path();
  if (hostExe.empty()) hostExe = directory_of(me) + L"GNLinkStream.exe";
  std::printf("fec_single_chunk_host_e2e_test: host=%s seconds=%d content=%s loss=%u/1000 seed=%u\n",
              std::string(hostExe.begin(), hostExe.end()).c_str(), seconds, contentArg.c_str(), lossPermille, lossSeed);

  std::vector<Content> contents;
  if (contentArg == "all") contents = {Content::StaticText, Content::LowMotion, Content::Video};
  else if (contentArg == "static") contents = {Content::StaticText};
  else if (contentArg == "lowmotion") contents = {Content::LowMotion};
  else if (contentArg == "video") contents = {Content::Video};
  else {
    std::printf("usage: --content static|lowmotion|video|all\n");
    return 1;
  }

  if (rcMatrix || rcTransitions) {
    gDiagnosticMode = rcDiagnostic;
    if (rcMatrix) run_rc_matrix(hostExe, baselineHost, dir, seconds, rcOnly, rcFps, rcRate, rcCandFirst);
    if (rcTransitions) {
      run_rc_transitions(hostExe, dir, seconds);
      run_rc_eonly(hostExe, dir, seconds);
    }
  } else if (v2) {
    run_v2(hostExe, dir, seconds);
  } else if (paired) {
    run_paired(hostExe, baselineHost, dir, seconds);
  } else if (matrix) {
    run_matrix(hostExe, dir, seconds);
  } else
  for (Content content : contents) {
    const char* cls = content_name(content);
    RunResult res[2];
    for (int layout = 0; layout < 2; ++layout) {
      const bool tight = layout == 1;  // padded first: what the field sends today
      const std::wstring runDir = dir + std::wstring(cls, cls + std::strlen(cls)) + L"_" +
                                  (tight ? L"tight" : L"padded") + L"\\";
      const uint16_t port = remote60::native_poc::e2e::e2e_pick_free_udp_port();
      check(port != 0, std::string(cls) + (tight ? " tight" : " padded") + ": a free UDP port for the host", u(port));
      if (port == 0) {
        // --bind-port 0 would send the host to the product's default port: nothing is started.
        std::printf("SKIP-LAUNCH %s %s: no free port, the host is not started\n", cls, tight ? "tight" : "padded");
        continue;
      }
      res[layout] = run_host(hostExe, runDir, content, tight, port, seconds, lossPermille, lossSeed,
                             /*wireCapOn=*/false);
      print_run(content, tight, lossPermille, lossSeed, res[layout]);
      const RunResult& r = res[layout];
      const std::string tag = std::string(cls) + " " + (tight ? "tight" : "padded");
      check(r.host.layoutToken == (tight ? "tight" : "padded"), tag + ": the host's own startup line names this layout",
            r.host.layoutToken);
      check(r.host.statsLines >= static_cast<size_t>(seconds / 2), tag + ": the host wrote its stats lines",
            u(r.host.statsLines));
      check(r.delivered >= 2, tag + ": frames were received and delivered", u(r.delivered));
      check(r.malformed == 0, tag + ": nothing the host sent was malformed to the receiver", u(r.malformed));
      check(r.decodeErrors == 0 && r.decoded + 4 >= r.delivered, tag + ": every delivered AU decodes",
            "decoded " + u(r.decoded) + " of " + u(r.delivered) + " errors " + u(r.decodeErrors) + " " + r.decoder);
      if (lossPermille == 0) {
        // A seq gap on a lossless loopback is the host's: an IDR (encoder_gop) supersedes the
        // sender's two-deep backlog and those seqs are never sent. The receiver sees it as a gap
        // closed by a complete keyframe -- no request, no discontinuity (respond_to_sequence_gap).
        // Anything else -- a gap not closed by a key, a discontinuity, a request -- would be a loss.
        check(r.disc == 0 && r.keyReq == 0 && r.gaps == r.gapsBehindKey,
              tag + ": lossless -> in order, no discontinuity, no keyframe request, every seq gap is the"
                    " host's keyframe supersede (closed by a complete IDR)",
              "gaps " + u(r.gaps) + " behindKey " + u(r.gapsBehindKey) + " maxGap " + u(r.maxGap) + " disc " +
                  u(r.disc) + " keyReq " + u(r.keyReq));
        check(r.rxDropped == 0 && r.nacks == 0 && r.fec == 0, tag + ": lossless -> nothing dropped, no NACK, no repair");
        // What the host says it sent and what arrived here agree on the parity bytes (both count
        // datagram payload; the stats line is at most a second behind the receiver, so the
        // allowance is a second and a half of this run's parity rate, not a share of the total --
        // a share is too tight for a short run, where one second is a large share).
        const uint64_t hp = r.host.parity;
        const uint64_t rp = r.rxParity;
        const uint64_t allowance = (std::max(hp, rp) * 3) / (static_cast<uint64_t>(seconds) * 2) + 4096;
        check(hp > 0 && rp > 0 && (hp <= rp + allowance) && (rp <= hp + allowance),
              tag + ": the host's parity account and the received parity agree (within a second of stats)",
              "host " + u(hp) + " received " + u(rp));
      }
      if (tight && content != Content::Video) {
        check(r.singleChunkFrames > 0, tag + ": single-chunk frames occurred (the case this work is about)",
              u(r.singleChunkFrames));
      }
    }
    const RunResult& padded = res[0];
    const RunResult& tight = res[1];
    const std::string tag = std::string(cls);
    // Per frame the host sent (its cumulative udpTxFrames), because two live captures never send
    // the same number of frames in the same seconds.
    const double padParityPerFrame = padded.host.txFrames ? double(padded.host.parity) / padded.host.txFrames : 0;
    const double tightParityPerFrame = tight.host.txFrames ? double(tight.host.parity) / tight.host.txFrames : 0;
    const double padWirePerFrame = padded.host.txFrames ? double(padded.host.wireEst) / padded.host.txFrames : 0;
    const double tightWirePerFrame = tight.host.txFrames ? double(tight.host.wireEst) / tight.host.txFrames : 0;
    // The wire as a multiple of the payload it carried: on a still screen one 64 KB IDR is most
    // of the 12 seconds' bytes and the frame count is ten or eleven, so "per frame" swings 10%
    // on whether the last refresh frame made the last stats line. Numerator and denominator
    // here cover the same frames.
    const double padOverhead = padded.host.txBytes ? double(padded.host.wireEst) / padded.host.txBytes : 0;
    const double tightOverhead = tight.host.txBytes ? double(tight.host.wireEst) / tight.host.txBytes : 0;
    std::printf("COMPARE content=%s padded: frames=%llu payload=%llu parity=%llu wireEst=%llu parity/frame=%.0f wire/frame=%.0f"
                " wire/payload=%.3f | tight: frames=%llu payload=%llu parity=%llu wireEst=%llu parity/frame=%.0f wire/frame=%.0f"
                " wire/payload=%.3f | parity/frame ratio=%.2f wire/frame ratio=%.2f wire/payload ratio=%.3f\n",
                cls, static_cast<unsigned long long>(padded.host.txFrames), static_cast<unsigned long long>(padded.host.txBytes),
                static_cast<unsigned long long>(padded.host.parity), static_cast<unsigned long long>(padded.host.wireEst),
                padParityPerFrame, padWirePerFrame, padOverhead, static_cast<unsigned long long>(tight.host.txFrames),
                static_cast<unsigned long long>(tight.host.txBytes), static_cast<unsigned long long>(tight.host.parity),
                static_cast<unsigned long long>(tight.host.wireEst), tightParityPerFrame, tightWirePerFrame, tightOverhead,
                padParityPerFrame > 0 ? tightParityPerFrame / padParityPerFrame : 0,
                padWirePerFrame > 0 ? tightWirePerFrame / padWirePerFrame : 0,
                padOverhead > 0 ? tightOverhead / padOverhead : 0);
    if (lossPermille == 0 && padded.launched && tight.launched) {
      check(padded.host.txFrames > 0 && tight.host.txFrames > 0, tag + ": both hosts counted the frames they sent",
            u(padded.host.txFrames) + " / " + u(tight.host.txFrames));
      if (content != Content::Video) {
        check(tightParityPerFrame < padParityPerFrame, tag + ": tight sends fewer parity bytes per frame than padded",
              "tight " + std::to_string(tightParityPerFrame) + " padded " + std::to_string(padParityPerFrame));
        check(tightOverhead < padOverhead, tag + ": tight puts fewer wire bytes per payload byte than padded",
              "tight " + std::to_string(tightOverhead) + " padded " + std::to_string(padOverhead));
      } else {
        // Busy motion is multi-chunk frames: the layouts are the same bytes there, so the two live
        // captures differ only by what the encoder produced. Reported, not gated.
        std::printf("INFO video: parity/frame tight %.0f vs padded %.0f (multi-chunk frames are identical under both;"
                    " two live captures)\n", tightParityPerFrame, padParityPerFrame);
      }
    }
  }

  // Cap-ON evidence (bitrate-hard-cap r2): the same lossless scenario with the hard wire cap ON must,
  // after the progress-based tail fix, still make NO premature NACK -- the pinned cap-OFF runs above
  // keep this test's FEC-layout purpose, this run keeps the cap-ON proof. Only on a lossless path.
  if (!matrix && lossPermille == 0 && !capture_unavailable()) {
    const uint16_t capPort = remote60::native_poc::e2e::e2e_pick_free_udp_port();
    if (capPort != 0) {
      const std::wstring capDir = dir + L"capon_lossless\\";
      const RunResult cr = run_host(hostExe, capDir, Content::Video, /*tight=*/true, capPort, seconds, 0, lossSeed,
                                    /*wireCapOn=*/true);
      print_run(Content::Video, true, 0, lossSeed, cr);
      check(cr.delivered >= 2 && cr.decodeErrors == 0,
            "cap ON lossless: frames delivered and decoded", "delivered " + u(cr.delivered) + " decErr " + u(cr.decodeErrors));
      check(cr.nacks == 0,
            "cap ON lossless: NO premature NACK (the r2 progress-based tail holds under the cap on real hardware)",
            "rxNacks " + u(cr.nacks));
    }
  }

  MFShutdown();
  WSACleanup();
  if (keepDir) std::printf("kept %s\n", std::string(dir.begin(), dir.end()).c_str());
  else check(staging.Remove(), "the staging directory is removed", staging.why());
  if (capture_unavailable()) {
    std::printf("fec_single_chunk_host_e2e_test: CAPTURE-UNAVAILABLE (%d run(s) without a capture item even with the visible fallback; "
                "%d of %d checks failed) -- exit 3 so the runner skips what cannot be measured\n",
                gCaptureUnavailableRuns, gFails, gCheckCount);
    return 3;
  }
  if (gFails != 0) {
    std::printf("fec_single_chunk_host_e2e_test: FAIL (%d of %d checks)\n", gFails, gCheckCount);
    return 1;
  }
  if (gDiagnosticCollected > 0) {
    // r10: the original candidate's stall evidence was collected -- material, not a pass; exit 4
    std::printf("fec_single_chunk_host_e2e_test: DIAGNOSTIC-COLLECTED (%d invalid candidate row(s) recorded with their dumps; %d checks otherwise passed) -- exit 4, not a PASS\n",
                gDiagnosticCollected, gCheckCount);
    return 4;
  }
  std::printf("fec_single_chunk_host_e2e_test: PASS (%d checks)\n", gCheckCount);
  return 0;
}
