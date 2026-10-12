#pragma once

// motion-settle r1 M0: the base QP of an access unit's first slice, read from the bitstream itself
// (pic_init_qp_minus26 of its PPS + slice_qp_delta of the slice header).
//
// Why: the AMD MFT on the measurement PC does not attach MFSampleExtension_VideoEncodeQP to its output
// samples (MF_E_ATTRIBUTENOTFOUND on every sample), so the per-AU QP was Unknown -- and whether a
// refinement P after a motion burst was coded at the ceiling (and so carried nothing) is exactly the
// question. This is the QP the slice header SAYS; per-macroblock deltas (mb_qp_delta) are not
// followed, so it is the slice's starting QP, not an average. Reported as "sliceQp", next to and never
// in place of the sample attribute.
//
// Scope: Annex-B, progressive (frame_mbs_only) streams of the shapes the hosts produce -- I/P slices,
// one SPS/PPS kept from the last IDR, CAVLC or CABAC. Anything outside it (field coding, explicit
// weighted prediction, B slices, a missing parameter set, a truncated header) returns -1 rather than a
// guess.
// Pure; h264_slice_qp_test.cpp.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace remote60::native_poc {

class H264SliceQpReader {
 public:
  // Feed one access unit (Annex-B). SPS/PPS inside it are remembered; returns the first slice's QP,
  // or -1 when it cannot be read.
  int Read(const uint8_t* data, size_t size) {
    int qp = -1;
    for (size_t i = 0; i + 3 < size;) {
      size_t start = 0, sc = 0;
      if (!NextStart(data, size, i, &start, &sc)) break;
      size_t end = start;
      size_t nextStart = 0, nextSc = 0;
      if (NextStart(data, size, start, &nextStart, &nextSc)) end = nextStart - nextSc; else end = size;
      if (end > start) {
        const uint8_t nalType = data[start] & 0x1F;
        const uint8_t refIdc = (data[start] >> 5) & 0x3;
        std::vector<uint8_t> rbsp = Unescape(data + start + 1, end - start - 1);
        if (nalType == 7) ParseSps(rbsp);
        else if (nalType == 8) ParsePps(rbsp);
        else if ((nalType == 1 || nalType == 5) && qp < 0) qp = ParseSliceQp(rbsp, nalType == 5, refIdc);
      }
      i = end;
    }
    return qp;
  }

 private:
  struct Bits {
    const std::vector<uint8_t>& b;
    size_t pos = 0;
    bool bad = false;
    explicit Bits(const std::vector<uint8_t>& v) : b(v) {}
    uint32_t u(int n) {
      uint32_t v = 0;
      for (int k = 0; k < n; ++k) {
        if (pos >= b.size() * 8) { bad = true; return 0; }
        v = (v << 1) | ((b[pos / 8] >> (7 - pos % 8)) & 1u);
        ++pos;
      }
      return v;
    }
    uint32_t ue() {
      int zeros = 0;
      while (u(1) == 0) {
        if (bad || ++zeros > 31) { bad = true; return 0; }
      }
      return zeros == 0 ? 0 : ((1u << zeros) - 1 + u(zeros));
    }
    int32_t se() {
      const uint32_t k = ue();
      return (k & 1) ? static_cast<int32_t>((k + 1) / 2) : -static_cast<int32_t>(k / 2);
    }
  };

  static bool NextStart(const uint8_t* d, size_t n, size_t from, size_t* start, size_t* scLen) {
    for (size_t i = from; i + 2 < n; ++i) {
      if (d[i] == 0 && d[i + 1] == 0 && d[i + 2] == 1) {
        *start = i + 3;
        *scLen = (i > 0 && d[i - 1] == 0) ? 4 : 3;
        return true;
      }
    }
    return false;
  }
  static std::vector<uint8_t> Unescape(const uint8_t* d, size_t n) {
    std::vector<uint8_t> out;
    out.reserve(n < 64 ? n : 64);
    int zeros = 0;
    // The slice header lives in the first bytes; 512 unescaped bytes are far more than it needs.
    for (size_t i = 0; i < n && out.size() < 512; ++i) {
      if (zeros >= 2 && d[i] == 3) { zeros = 0; continue; }
      out.push_back(d[i]);
      zeros = d[i] == 0 ? zeros + 1 : 0;
    }
    return out;
  }

  // SPS fields the slice header needs.
  bool haveSps_ = false;
  uint32_t log2MaxFrameNum_ = 0, pocType_ = 0, log2MaxPocLsb_ = 0;
  bool frameMbsOnly_ = true, deltaPicOrderAlwaysZero_ = false, separateColourPlane_ = false;
  // PPS fields.
  bool havePps_ = false;
  bool cabac_ = false, bottomFieldPocPresent_ = false, redundantPicCnt_ = false, weightedPred_ = false;
  uint32_t numRefIdxL0Default_ = 0;
  int32_t picInitQp_ = 26;

  void ParseSps(const std::vector<uint8_t>& r) {
    Bits b(r);
    const uint32_t profile = b.u(8);
    b.u(8);  // constraint flags
    b.u(8);  // level
    b.ue();  // sps id
    separateColourPlane_ = false;
    if (profile == 100 || profile == 110 || profile == 122 || profile == 244 || profile == 44 || profile == 83 ||
        profile == 86 || profile == 118 || profile == 128 || profile == 138 || profile == 139 || profile == 134) {
      const uint32_t chroma = b.ue();
      if (chroma == 3) separateColourPlane_ = b.u(1) != 0;
      b.ue();  // bit depth luma
      b.ue();  // bit depth chroma
      b.u(1);  // qpprime y zero transform bypass
      if (b.u(1)) {  // seq scaling matrix present
        for (int i = 0; i < (chroma != 3 ? 8 : 12); ++i) {
          if (b.u(1)) {
            const int size = i < 6 ? 16 : 64;
            int last = 8, next = 8;
            for (int j = 0; j < size; ++j) {
              if (next != 0) next = (last + b.se() + 256) % 256;
              last = next == 0 ? last : next;
            }
          }
        }
      }
    }
    log2MaxFrameNum_ = b.ue() + 4;
    pocType_ = b.ue();
    if (pocType_ == 0) {
      log2MaxPocLsb_ = b.ue() + 4;
    } else if (pocType_ == 1) {
      deltaPicOrderAlwaysZero_ = b.u(1) != 0;
      b.se();
      b.se();
      const uint32_t n = b.ue();
      for (uint32_t i = 0; i < n && !b.bad; ++i) b.se();
    }
    b.ue();  // max num ref frames
    b.u(1);  // gaps allowed
    b.ue();  // width in mbs
    b.ue();  // height in map units
    frameMbsOnly_ = b.u(1) != 0;
    haveSps_ = !b.bad;
  }

  void ParsePps(const std::vector<uint8_t>& r) {
    Bits b(r);
    b.ue();  // pps id
    b.ue();  // sps id
    cabac_ = b.u(1) != 0;
    bottomFieldPocPresent_ = b.u(1) != 0;
    const uint32_t sliceGroups = b.ue() + 1;
    if (sliceGroups != 1) { havePps_ = false; return; }
    numRefIdxL0Default_ = b.ue();
    b.ue();  // l1 default
    weightedPred_ = b.u(1) != 0;
    b.u(2);  // weighted bipred idc
    picInitQp_ = 26 + b.se();
    havePps_ = !b.bad;
    (void)redundantPicCnt_;
    // deblocking/constrained intra/redundant pic cnt follow pic_init_qs/chroma offset:
    b.se();  // pic_init_qs
    b.se();  // chroma qp index offset
    b.u(1);  // deblocking filter control present
    b.u(1);  // constrained intra pred
    redundantPicCnt_ = b.u(1) != 0;
    havePps_ = !b.bad;
  }

  int ParseSliceQp(const std::vector<uint8_t>& r, bool idr, uint8_t refIdc) {
    if (!haveSps_ || !havePps_ || !frameMbsOnly_) return -1;
    Bits b(r);
    b.ue();  // first mb in slice
    const uint32_t sliceType = b.ue() % 5;  // 0 P, 1 B, 2 I, 3 SP, 4 SI
    if (sliceType == 1 || sliceType == 3 || sliceType == 4) return -1;
    b.ue();  // pps id
    if (separateColourPlane_) b.u(2);
    b.u(static_cast<int>(log2MaxFrameNum_));
    if (idr) b.ue();  // idr pic id
    if (pocType_ == 0) {
      b.u(static_cast<int>(log2MaxPocLsb_));
      if (bottomFieldPocPresent_) b.se();
    } else if (pocType_ == 1 && !deltaPicOrderAlwaysZero_) {
      b.se();
      if (bottomFieldPocPresent_) b.se();
    }
    if (redundantPicCnt_) b.ue();
    if (sliceType == 0) {  // P
      if (b.u(1)) b.ue();  // num ref idx override -> l0 active minus 1
      if (b.u(1)) {         // ref pic list modification l0
        for (int guard = 0; guard < 64; ++guard) {
          const uint32_t op = b.ue();
          if (op == 3 || b.bad) break;
          b.ue();
        }
      }
      if (weightedPred_) return -1;
    }
    if (refIdc != 0) {
      if (idr) {
        b.u(1);  // no output of prior pics
        b.u(1);  // long term reference
      } else if (b.u(1)) {  // adaptive ref pic marking
        for (int guard = 0; guard < 64; ++guard) {
          const uint32_t op = b.ue();
          if (op == 0 || b.bad) break;
          if (op == 1 || op == 3) b.ue();
          if (op == 2) b.ue();
          if (op == 3 || op == 6) b.ue();
          if (op == 4) b.ue();
        }
      }
    }
    if (cabac_ && sliceType != 2) b.ue();  // cabac init idc
    const int32_t qp = picInitQp_ + b.se();
    if (b.bad || qp < 0 || qp > 51) return -1;
    return qp;
  }
};

}  // namespace remote60::native_poc
