#pragma once

// motion-settle r1 M0 (hardened in r2): the STARTING QP of an access unit's first slice, read from the
// bitstream (pic_init_qp_minus26 of the PPS the slice names + its slice_qp_delta).
//
// Why: the AMD MFT on the measurement PC attaches no MFSampleExtension_VideoEncodeQP to its output
// samples (MF_E_ATTRIBUTENOTFOUND on every sample). This is a DIAGNOSTIC figure, logged as "sliceQp" next
// to (never in place of) the sample attribute, and no product decision depends on it:
//   - first slice only, starting QP only: mb_qp_delta is not followed, so it is not the picture's mean
//     QP nor the QP of the text it carries;
//   - parameter sets are kept by id; a slice is read only through the PPS it names and the SPS that PPS
//     names; a new encoder instance starts a new reader;
//   - supported: frame_mbs_only, 4:2:0, 8-bit, one slice group, I/P slices, no explicit weighted
//     prediction. Anything else -- an out-of-range slice_type, a missing/unsupported parameter set, a
//     truncated header, a list loop that does not terminate within its bound -- is -1 (Unknown), never
//     a guess.
// Pure; h264_slice_qp_test.cpp (hand-built decision vectors + the real encoders of the machine).

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

namespace remote60::native_poc {

class H264SliceQpReader {
 public:
  // Feed one access unit (Annex-B). Parameter sets inside it are remembered; returns the first slice's
  // starting QP, or -1 when it cannot be read.
  int Read(const uint8_t* data, size_t size) {
    int qp = -1;
    bool sliceSeen = false;
    size_t pos = 0;
    while (true) {
      size_t start = 0, sc = 0;
      if (!NextStart(data, size, pos, &start, &sc)) break;
      size_t nextStart = 0, nextSc = 0;
      const size_t end = NextStart(data, size, start, &nextStart, &nextSc) ? nextStart - nextSc : size;
      if (end > start) {
        const uint8_t nalType = data[start] & 0x1F;
        const uint8_t refIdc = (data[start] >> 5) & 0x3;
        const std::vector<uint8_t> rbsp = Unescape(data + start + 1, end - start - 1);
        if (nalType == 7) ParseSps(rbsp);
        else if (nalType == 8) ParsePps(rbsp);
        else if ((nalType == 1 || nalType == 5) && !sliceSeen) {
          sliceSeen = true;
          qp = ParseSliceQp(rbsp, nalType == 5, refIdc);
        }
      }
      pos = end;
      if (pos >= size) break;
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
    int zeros = 0;
    // Headers live in the first bytes; 512 unescaped bytes are far more than any of them needs.
    for (size_t i = 0; i < n && out.size() < 512; ++i) {
      if (zeros >= 2 && d[i] == 3) { zeros = 0; continue; }
      out.push_back(d[i]);
      zeros = d[i] == 0 ? zeros + 1 : 0;
    }
    return out;
  }

  struct Sps {
    bool supported = false;
    uint32_t log2MaxFrameNum = 0, pocType = 0, log2MaxPocLsb = 0;
    bool deltaPicOrderAlwaysZero = false;
  };
  struct Pps {
    bool supported = false;
    uint32_t spsId = 0;
    bool cabac = false, bottomFieldPocPresent = false, redundantPicCnt = false, weightedPred = false;
    int32_t picInitQp = 26;
  };
  std::map<uint32_t, Sps> sps_;
  std::map<uint32_t, Pps> pps_;

  void ParseSps(const std::vector<uint8_t>& r) {
    Bits b(r);
    Sps s;
    const uint32_t profile = b.u(8);
    b.u(8);  // constraint flags
    b.u(8);  // level
    const uint32_t id = b.ue();
    if (b.bad || id > 31) return;
    bool supported = true;
    if (profile == 100 || profile == 110 || profile == 122 || profile == 244 || profile == 44 || profile == 83 ||
        profile == 86 || profile == 118 || profile == 128 || profile == 138 || profile == 139 || profile == 134) {
      const uint32_t chroma = b.ue();
      if (chroma != 1) supported = false;  // 4:2:0 only (no separate colour planes)
      if (b.ue() != 0 || b.ue() != 0) supported = false;  // 8-bit luma and chroma only
      b.u(1);  // qpprime y zero transform bypass
      if (b.u(1)) {  // seq scaling matrix present
        for (int i = 0; i < 8 && !b.bad; ++i) {
          if (b.u(1)) {
            const int n = i < 6 ? 16 : 64;
            int last = 8, next = 8;
            for (int j = 0; j < n && !b.bad; ++j) {
              if (next != 0) next = (last + b.se() + 256) % 256;
              last = next == 0 ? last : next;
            }
          }
        }
      }
    }
    s.log2MaxFrameNum = b.ue() + 4;
    s.pocType = b.ue();
    if (s.pocType == 0) {
      s.log2MaxPocLsb = b.ue() + 4;
    } else if (s.pocType == 1) {
      s.deltaPicOrderAlwaysZero = b.u(1) != 0;
      b.se();
      b.se();
      const uint32_t n = b.ue();
      if (n > 255) supported = false;
      for (uint32_t i = 0; i < n && i < 256 && !b.bad; ++i) b.se();
    } else if (s.pocType != 2) {
      supported = false;
    }
    b.ue();  // max num ref frames
    b.u(1);  // gaps allowed
    b.ue();  // width in mbs
    b.ue();  // height in map units
    if (b.u(1) == 0) supported = false;  // frame_mbs_only
    if (s.log2MaxFrameNum > 16 || s.log2MaxPocLsb > 16) supported = false;
    s.supported = supported && !b.bad;
    sps_[id] = s;  // an unsupported SPS replaces an older one of the same id: its slices read as Unknown
  }

  void ParsePps(const std::vector<uint8_t>& r) {
    Bits b(r);
    Pps p;
    const uint32_t id = b.ue();
    p.spsId = b.ue();
    if (b.bad || id > 255 || p.spsId > 31) return;
    p.cabac = b.u(1) != 0;
    p.bottomFieldPocPresent = b.u(1) != 0;
    bool supported = b.ue() == 0;  // one slice group
    b.ue();  // l0 default
    b.ue();  // l1 default
    p.weightedPred = b.u(1) != 0;
    b.u(2);  // weighted bipred idc
    p.picInitQp = 26 + b.se();
    b.se();  // pic_init_qs
    b.se();  // chroma qp index offset
    b.u(1);  // deblocking filter control present
    b.u(1);  // constrained intra pred
    p.redundantPicCnt = b.u(1) != 0;
    if (p.picInitQp < 0 || p.picInitQp > 51) supported = false;
    p.supported = supported && !b.bad;
    pps_[id] = p;
  }

  int ParseSliceQp(const std::vector<uint8_t>& r, bool idr, uint8_t refIdc) {
    Bits b(r);
    b.ue();  // first mb in slice
    const uint32_t rawType = b.ue();
    if (b.bad || rawType > 9) return -1;  // out of range: not a slice type at all
    const uint32_t sliceType = rawType % 5;  // 0 P, 1 B, 2 I, 3 SP, 4 SI
    if (sliceType != 0 && sliceType != 2) return -1;
    const uint32_t ppsId = b.ue();
    const auto pi = pps_.find(ppsId);
    if (b.bad || pi == pps_.end() || !pi->second.supported) return -1;
    const Pps& pps = pi->second;
    const auto si = sps_.find(pps.spsId);
    if (si == sps_.end() || !si->second.supported) return -1;
    const Sps& sps = si->second;
    b.u(static_cast<int>(sps.log2MaxFrameNum));  // frame_num (frame_mbs_only: no field flags)
    if (idr) b.ue();                             // idr_pic_id
    if (sps.pocType == 0) {
      b.u(static_cast<int>(sps.log2MaxPocLsb));
      if (pps.bottomFieldPocPresent) b.se();
    } else if (sps.pocType == 1 && !sps.deltaPicOrderAlwaysZero) {
      b.se();
      if (pps.bottomFieldPocPresent) b.se();
    }
    if (pps.redundantPicCnt) b.ue();
    if (sliceType == 0) {  // P
      if (b.u(1)) b.ue();  // num_ref_idx_active_override -> l0 active minus 1
      if (b.u(1)) {        // ref_pic_list_modification_flag_l0
        bool terminated = false;
        for (int guard = 0; guard < 64 && !b.bad; ++guard) {
          const uint32_t op = b.ue();
          if (op == 3) { terminated = true; break; }
          if (op > 5) return -1;
          b.ue();
        }
        if (!terminated) return -1;
      }
      if (pps.weightedPred) return -1;  // pred_weight_table not followed
    }
    if (refIdc != 0) {
      if (idr) {
        b.u(1);  // no_output_of_prior_pics
        b.u(1);  // long_term_reference
      } else if (b.u(1)) {  // adaptive_ref_pic_marking_mode
        bool terminated = false;
        for (int guard = 0; guard < 64 && !b.bad; ++guard) {
          const uint32_t op = b.ue();
          if (op == 0) { terminated = true; break; }
          if (op > 6) return -1;
          if (op == 1 || op == 3) b.ue();
          if (op == 2) b.ue();
          if (op == 3 || op == 6) b.ue();
          if (op == 4) b.ue();
        }
        if (!terminated) return -1;
      }
    }
    if (pps.cabac && sliceType != 2) {
      if (b.ue() > 2) return -1;  // cabac_init_idc
    }
    const int32_t qp = pps.picInitQp + b.se();
    if (b.bad || qp < 0 || qp > 51) return -1;
    return qp;
  }
};

}  // namespace remote60::native_poc
