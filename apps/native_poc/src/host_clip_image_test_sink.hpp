#pragma once

// TEST BUILD ONLY: the clipboard-image publisher of GNLinkStreamClipSink. See the .cpp.

#include <atomic>
#include <string>

#include "host_clip_image.hpp"

namespace remote60::native_poc {

class FileSinkClipImagePublisher : public HostClipImagePublisher {
 public:
  explicit FileSinkClipImagePublisher(std::wstring dir) : dir_(std::move(dir)) {}
  bool Enabled() const override { return !dir_.empty(); }
  uint64_t Sequence() const override { return seq_.load(); }
  ClipPublishResult Publish(uint64_t expectSequence, HGLOBAL pngGlobal, HGLOBAL dibv5,
                            const std::u16string& text) override;

 private:
  std::wstring dir_;
  std::atomic<uint64_t> seq_{1};
  std::atomic<uint64_t> count_{0};
};

}  // namespace remote60::native_poc
