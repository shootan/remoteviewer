#pragma once

// TEST BUILD ONLY: the clipboard-image publisher of GNLinkStreamClipSink. See the .cpp.

#include <atomic>
#include <string>
#include <thread>

#include "host_clip_image.hpp"
#include "host_file_copy.hpp"

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

// TEST BUILD ONLY: the file-copy R->P source of GNLinkStreamClipSink (REMOTE60_FILE_COPY_TEST_SOURCE_DIR).
// The host's real clipboard is the user's, so the "copy of files" comes from a folder instead: when
// <dir>\\go appears, every file in <dir>\\files is handed to HostFileCopyService::OnHostClipboard
// exactly as the clipboard monitor would hand over a CF_HDROP (path strings). Everything after that
// -- the helper's identification and pin as the user, the paced sender, the chunk SHA-256 -- is the
// product's own code. The helper runs as this user on a private window station (its clipboard is not
// the user's). The build gate checks GNLinkStream.exe carries none of this.
class FileCopyTestSource {
 public:
  explicit FileCopyTestSource(std::wstring dir) : dir_(std::move(dir)) {}
  ~FileCopyTestSource() { Stop(); }
  bool Enabled() const { return !dir_.empty(); }
  /** The helper, as this user, on a private window station. */
  HostFileCopyService::HelperLauncher Launcher();
  /** Watches for the trigger and hands the copy over once. */
  void Start(HostFileCopyService* service);
  void Stop();

 private:
  std::wstring dir_;
  std::wstring desktop_;
  std::thread thread_;
  std::atomic<bool> stop_{false};
};

}  // namespace remote60::native_poc
