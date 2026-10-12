#pragma once

// motion-settle r1 M0: what each emitted access unit was -- the QP the encoder says it used, where its
// input came from, and what happened to it -- so a blurry still or a slow onset can be traced to the
// AU that carried it instead of guessed from byte sizes.
//
// Role:    AuQpAccount: per-stats-interval QP summary by picture kind (I/P), Unknown counted apart.
//          au_trace_enabled / AuTraceBudget: the bounded per-AU line, off unless
//          REMOTE60_NATIVE_AU_TRACE=1 (a measurement build or an explicit diagnosis), never in the
//          default log.
// Thread:  main encode loop only.
// Input:   H264AccessUnit provenance (mf_h264_codec.hpp) + the emit stage's verdict.
// Output:  "[native-video-host][au-qp]" once per stats tick, "[native-video-host][au]" per AU when on.
// Callers: host_stage_encode_send_h264_au.cpp (Note / trace), host_stage_stats.cpp (line + reset).
// Pure apart from the getenv.

#include <cstdint>
#include <cstdlib>
#include <ostream>

namespace remote60::native_poc {

struct AuQpKind {
  uint64_t count = 0;    // AUs of this kind with a QP
  uint64_t sum = 0;
  int32_t min = -1;
  int32_t max = -1;
  void Note(int32_t qp) {
    ++count;
    sum += static_cast<uint64_t>(qp);
    if (min < 0 || qp < min) min = qp;
    if (max < 0 || qp > max) max = qp;
  }
};

struct AuQpAccount {
  AuQpKind key;          // IDR / clean point
  AuQpKind delta;        // P
  AuQpKind synthDelta;   // P produced from a synthetic input (kick / static refresh / settle)
  uint64_t unknown = 0;  // AUs whose sample carried no QP attribute
  uint64_t aus = 0;
  // The slice-header QP (h264_slice_qp.hpp), same split; always read, whatever the sample says.
  AuQpKind sliceKey, sliceDelta, sliceSynthDelta;

  void NoteSlice(bool keyFrame, bool synthetic, int32_t sliceQp) {
    if (sliceQp < 0) return;
    (keyFrame ? sliceKey : synthetic ? sliceSynthDelta : sliceDelta).Note(sliceQp);
  }
  void Note(bool keyFrame, bool synthetic, int32_t qp) {
    ++aus;
    if (qp < 0) {
      ++unknown;
      return;
    }
    if (keyFrame) {
      key.Note(qp);
    } else if (synthetic) {
      synthDelta.Note(qp);
    } else {
      delta.Note(qp);
    }
  }
  void Reset() { *this = AuQpAccount{}; }

  // " qpKey=min/avg/max ..." -- "-" for a kind with no QP this interval.
  static void AppendKind(std::ostream& os, const char* name, const AuQpKind& k) {
    os << " " << name << "=";
    if (k.count == 0) {
      os << "-";
      return;
    }
    os << k.min << "/" << (static_cast<double>(k.sum) / static_cast<double>(k.count)) << "/" << k.max;
  }
  void Append(std::ostream& os) const {
    os << " aus=" << aus;
    AppendKind(os, "qpKey", key);
    AppendKind(os, "qpP", delta);
    AppendKind(os, "qpSynthP", synthDelta);
    os << " qpUnknown=" << unknown;
    AppendKind(os, "sliceQpKey", sliceKey);
    AppendKind(os, "sliceQpP", sliceDelta);
    AppendKind(os, "sliceQpSynthP", sliceSynthDelta);
  }
};

inline bool au_trace_enabled() {
  static const bool on = [] {
    const char* v = std::getenv("REMOTE60_NATIVE_AU_TRACE");
    return v && (v[0] == '1' || v[0] == 't' || v[0] == 'T');
  }();
  return on;
}

// The per-AU line is bounded even when on: 60 fps for an hour is 216000 lines.
constexpr uint64_t kAuTraceMaxLines = 250000;

}  // namespace remote60::native_poc
