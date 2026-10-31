// viewer-sharpness r1 A: the APK decoder sink's visible size -- android_video_decoder.cpp itself against the stand-in
// NDK (test_fake_ndk/), whose output format can carry a crop as Codec2 does (one "crop" rect), as the four
// crop-left/top/right/bottom integers, or not at all.
//
// The phone case (SM-S948N, 2026-10-15): the decoder codes 1920x1080 as 1920x1088 and reports the visible 1080 only
// in the "crop" rect. The sink read the four integers, found none, and showed 1920x1088 -- 8 padding lines taken
// as picture, every row resampled by 1080/1088. Each case below has its expected visible size; [1] is the one the
// old read got wrong (it is the negative control: FAIL before the fix).
//
// What the stand-in cannot show: a real decoder's format, a real Surface's crop handling.

#include <cstdio>
#include <string>
#include <vector>

#include "android_video_decoder.hpp"
#include "fake_ndk.hpp"
#include "poc_protocol.hpp"

using remote60::android_direct::AndroidVideoDecoderSink;
using namespace remote60::native_poc;

namespace {

int gFailures = 0;
int gChecks = 0;

void check(const std::string& name, bool cond, const std::string& detail = {}) {
  ++gChecks;
  std::printf("%s  %s%s%s\n", cond ? "PASS" : "FAIL", name.c_str(), detail.empty() ? "" : "  ", detail.c_str());
  std::fflush(stdout);
  if (!cond) ++gFailures;
}

UdpH264AssembledFrame key_frame(uint32_t w, uint32_t h, uint64_t gen) {
  UdpH264AssembledFrame f;
  f.header.width = w;
  f.header.height = h;
  f.header.flags = 0x1u;
  f.header.streamGeneration = gen;
  static uint64_t capture = 1000000;
  capture += 16667;
  f.header.captureQpcUs = capture;
  const uint8_t sc[] = {0, 0, 0, 1};
  auto put = [&](std::initializer_list<uint8_t> nal) {
    f.payload.insert(f.payload.end(), sc, sc + 4);
    f.payload.insert(f.payload.end(), nal.begin(), nal.end());
  };
  put({0x67, 0x42, 0x00, 0x28, static_cast<uint8_t>(w & 0xff)});  // SPS (differs per size: a new codec)
  put({0x68, 0xCE, 0x3C, 0x80});
  put({0x65, 0x88, 0x11, 0x11});
  return f;
}

int surfaceToken = 1;
jobject surface() { return &surfaceToken; }

struct Fmt {
  int32_t w, h;
  bool rect, keys;
  int32_t l, t, r, b;
};

std::string size_str(uint64_t packed) {
  return std::to_string(packed >> 32) + "x" + std::to_string(packed & 0xffffffffu);
}

// One codec: started by a key frame of the wire size, then the decoder announces `fmt`. The visible size after it.
uint64_t visible_after(AndroidVideoDecoderSink& sink, uint32_t wireW, uint32_t wireH, uint64_t gen, const Fmt& fmt,
                       std::string* debug) {
  sink.OnEncodedH264Frame(key_frame(wireW, wireH, gen));
  {
    std::lock_guard<std::mutex> lk(fake_ndk::mu());
    fake_ndk::Controls::Output o;
    o.formatChange = true;
    o.width = fmt.w;
    o.height = fmt.h;
    o.cropRect = fmt.rect;
    o.cropKeys = fmt.keys;
    o.cropL = fmt.l, o.cropT = fmt.t, o.cropR = fmt.r, o.cropB = fmt.b;
    fake_ndk::controls().outputs.push_back(o);
  }
  const uint64_t packed = sink.VideoSizePacked();  // pumps: the format change is read here
  if (debug) *debug = sink.DebugStatus();
  return packed;
}

std::string field(const std::string& s, const std::string& key) {
  const size_t at = s.find(" " + key + "=");
  if (at == std::string::npos) return {};
  const size_t from = at + key.size() + 2;
  return s.substr(from, s.find(' ', from) - from);
}

}  // namespace

int main() {
  JNIEnv* env = nullptr;
  struct Case {
    const char* name;
    uint32_t wireW, wireH;
    Fmt fmt;
    uint32_t wantW, wantH;
    const char* wantSrc;
  };
  const Case cases[] = {
      {"[1] coded 1920x1088, visible 1080 in the Codec2 \"crop\" rect (the phone) -> 1920x1080", 1920, 1080,
       {1920, 1088, true, false, 0, 0, 1919, 1079}, 1920, 1080, "rect"},
      {"[2] a real 1920x1088 stream, no crop at all -> 1920x1088 kept (no constant 1080)", 1920, 1088,
       {1920, 1088, false, false, 0, 0, 0, 0}, 1920, 1088, "absent"},
      {"[3] a non-zero crop offset (8,4)-(1927,1083) in 1936x1088 -> 1920x1080", 1920, 1080,
       {1936, 1088, true, false, 8, 4, 1927, 1083}, 1920, 1080, "rect"},
      {"[4] an invalid rect (right == width) -> not used, the coded size shown", 1920, 1080,
       {1920, 1088, true, false, 0, 0, 1920, 1079}, 1920, 1088, "invalid_rect"},
      {"[5] the four crop-* integers only -> used", 1920, 1080,
       {1920, 1088, false, true, 0, 0, 1919, 1079}, 1920, 1080, "keys"},
      {"[6] an odd window 1281x721 coded 1296x736 -> its last row and column kept", 1281, 721,
       {1296, 736, true, false, 0, 0, 1280, 720}, 1281, 721, "rect"},
      {"[7] a crop of the whole frame (0,0,w-1,h-1) is a real crop, not 'absent'", 480, 270,
       {480, 270, true, false, 0, 0, 479, 269}, 480, 270, "rect"},
  };
  uint64_t gen = 10;
  for (const Case& c : cases) {
    AndroidVideoDecoderSink sink;
    sink.SetSurface(env, surface());
    std::string dbg;
    const uint64_t got = visible_after(sink, c.wireW, c.wireH, ++gen, c.fmt, &dbg);
    const bool ok = (got >> 32) == c.wantW && (got & 0xffffffffu) == c.wantH && field(dbg, "cropSrc") == c.wantSrc;
    check(c.name, ok, "visible " + size_str(got) + " cropSrc=" + field(dbg, "cropSrc") + " coded=" + field(dbg, "coded"));
    sink.SetSurface(env, nullptr);
  }

  std::puts("\n--- [8] a new codec does not carry the last one's crop (resolution / selection change, reconnect)");
  {
    AndroidVideoDecoderSink sink;
    sink.SetSurface(env, surface());
    std::string dbg;
    const uint64_t first = visible_after(sink, 1920, 1080, ++gen, {1936, 1088, true, false, 8, 4, 1927, 1083}, &dbg);
    check("first stream: 1920x1080 from its crop", first == ((1920ull << 32) | 1080u), size_str(first));
    sink.OnVideoDiscontinuity();                      // the codec goes; a new one comes with the next IDR
    sink.OnEncodedH264Frame(key_frame(1280, 720, ++gen));  // a new stream size, no format of it yet
    const std::string d2 = sink.DebugStatus();
    const uint64_t second = sink.VideoSizePacked();
    check("NEW CODEC BEFORE ITS FORMAT: the wire size, no crop of the old one",
          second == ((1280ull << 32) | 720u) && field(d2, "cropSrc") == "none" && field(d2, "crop") == "0,0,0,0",
          size_str(second) + " cropSrc=" + field(d2, "cropSrc") + " crop=" + field(d2, "crop"));
    sink.OnVideoStreamReset();  // a reconnect: everything of the stream goes
    const std::string d3 = sink.DebugStatus();
    check("after a stream reset: no codec, no crop kept", field(d3, "cropSrc") == "none" || field(d3, "codec") == "off",
          "codec=" + field(d3, "codec") + " cropSrc=" + field(d3, "cropSrc"));
    sink.SetSurface(env, nullptr);
  }

  if (gFailures == 0) {
    std::printf("\nandroid_decoder_geometry_test: %d passed, 0 failed\nRESULT: ALL PASS\n", gChecks);
    return 0;
  }
  std::printf("\nandroid_decoder_geometry_test: %d passed, %d failed\nRESULT: FAIL\n", gChecks - gFailures, gFailures);
  return 1;
}
