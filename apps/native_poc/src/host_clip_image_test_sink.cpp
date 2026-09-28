// TEST BUILD ONLY (GNLinkStreamClipSink, compiled with REMOTE60_CLIP_IMAGE_TEST_SINK). Never linked
// into GNLinkStream.exe -- the build gate in clip_image_build_gate_test checks the shipped binary
// for the environment variable's name.
//
// Where a received image lands in the test build: a folder, not a clipboard. The host under test
// runs on the interactive station next to the user's own session, so publishing to its clipboard
// would overwrite what the user copied; the plan's OS-boundary injection puts the effect here
// instead. Everything before the publish -- offer, bulk stream, SHA-256, PNG header check, WIC decode
// into CF_DIBV5 -- is the product's own code in the same binary.

#include "host_clip_image_test_sink.hpp"

#include <cstdio>
#include <string>

#include "clip_image_core.hpp"

namespace remote60::native_poc {

ClipPublishResult FileSinkClipImagePublisher::Publish(uint64_t expectSequence, HGLOBAL pngGlobal, HGLOBAL dibv5,
                                                      const std::u16string& text) {
  ClipPublishResult r = ClipPublishResult::Superseded;
  if (expectSequence == seq_.load()) {
    r = ClipPublishResult::Published;
    const uint64_t n = ++count_;
    const std::wstring base = dir_ + L"\\published_" + std::to_wstring(n);
    if (pngGlobal) {
      if (FILE* f = _wfopen((base + L".png").c_str(), L"wb")) {
        const void* p = GlobalLock(pngGlobal);
        if (p) std::fwrite(p, 1, GlobalSize(pngGlobal), f);
        GlobalUnlock(pngGlobal);
        std::fclose(f);
      }
    }
    uint32_t w = 0, h = 0;
    bool dibOk = false;
    if (dibv5) {
      const auto* p = static_cast<const uint8_t*>(GlobalLock(dibv5));
      const DibInfo info = validate_dib(p, GlobalSize(dibv5));
      dibOk = info.ok();
      w = info.width;
      h = info.height;
      GlobalUnlock(dibv5);
    }
    if (FILE* f = _wfopen((base + L".txt").c_str(), L"w")) {
      std::fprintf(f, "published=%llu pngBytes=%zu dibv5=%d w=%u h=%u textUtf16=%zu\n",
                   static_cast<unsigned long long>(n), pngGlobal ? static_cast<size_t>(GlobalSize(pngGlobal)) : 0,
                   dibOk ? 1 : 0, w, h, text.size());
      std::fclose(f);
    }
    seq_.fetch_add(1);
  }
  if (pngGlobal) GlobalFree(pngGlobal);
  if (dibv5) GlobalFree(dibv5);
  return r;
}

}  // namespace remote60::native_poc
