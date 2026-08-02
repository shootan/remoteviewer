#include "gdi_capture_process.hpp"

#include "gdi_capture_protocol.hpp"
#include "time_utils.hpp"

#include <algorithm>
#include <cstring>
#include <sstream>
#include <utility>

namespace remote60::native_poc {
namespace {

std::wstring sibling_worker_path() {
  std::wstring path(32768, L'\0');
  const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
  if (length == 0 || length >= path.size()) return {};
  path.resize(length);
  const size_t slash = path.find_last_of(L"\\/");
  if (slash == std::wstring::npos) return {};
  path.resize(slash + 1);
  path += L"remote60_gdi_capture_worker.exe";
  return path;
}

std::string win32_error(const char* prefix) {
  std::ostringstream oss;
  oss << prefix << "_win32_" << GetLastError();
  return oss.str();
}

std::wstring object_name(const wchar_t* kind, DWORD pid, uint64_t nonce) {
  std::wostringstream oss;
  oss << L"Local\\remote60_gdi_" << kind << L"_" << pid << L"_" << nonce;
  return oss.str();
}

}  // namespace

struct GdiCaptureProcess::Impl {
  GdiCaptureProcessConfig config;
  GdiCaptureFrameHandler onFrame;
  GdiCaptureLogHandler onLog;
  GdiCaptureFallbackHandler onFallback;
  std::atomic<bool> stopping{false};
  std::atomic<bool> started{false};
  HANDLE mapping = nullptr;
  HANDLE frameEvent = nullptr;
  HANDLE stopEvent = nullptr;
  HANDLE job = nullptr;
  PROCESS_INFORMATION process{};
  void* view = nullptr;
  size_t viewBytes = 0;
  std::thread reader;

  ~Impl() { stop(); }

  void log(const std::string& phase, const std::string& message) const {
    if (onLog) onLog(phase, message);
  }

  void release_ready_slots() {
    if (!view) return;
    auto* header = static_cast<gdi_capture::SharedHeader*>(view);
    for (uint32_t i = 0; i < gdi_capture::kSlotCount; ++i) {
      auto& state = header->slots[i].state;
      const LONG current = InterlockedCompareExchange(&state, gdi_capture::SlotFree,
                                                       gdi_capture::SlotReady);
      if (current == gdi_capture::SlotReading) {
        InterlockedExchange(&state, gdi_capture::SlotFree);
      }
    }
  }

  void read_latest_frame() {
    auto* header = static_cast<gdi_capture::SharedHeader*>(view);
    uint32_t best = gdi_capture::kSlotCount;
    uint64_t bestSequence = 0;
    for (uint32_t i = 0; i < gdi_capture::kSlotCount; ++i) {
      const LONG state = InterlockedCompareExchange(&header->slots[i].state,
                                                    gdi_capture::SlotReady,
                                                    gdi_capture::SlotReady);
      if (state == gdi_capture::SlotReady && header->slots[i].sequence >= bestSequence) {
        best = i;
        bestSequence = header->slots[i].sequence;
      }
    }
    if (best == gdi_capture::kSlotCount) return;
    if (InterlockedCompareExchange(&header->slots[best].state, gdi_capture::SlotReading,
                                   gdi_capture::SlotReady) != gdi_capture::SlotReady) {
      return;
    }

    // Drop older ready frames before copying. The encoder always wants the freshest desktop,
    // and keeping obsolete slots would eventually block the producer during a short stall.
    for (uint32_t i = 0; i < gdi_capture::kSlotCount; ++i) {
      if (i == best) continue;
      if (header->slots[i].sequence <= bestSequence) {
        (void)InterlockedCompareExchange(&header->slots[i].state, gdi_capture::SlotFree,
                                         gdi_capture::SlotReady);
      }
    }

    const uint64_t captureUs = header->slots[best].captureQpcUs;
    const uint64_t captureCopyUs = header->slots[best].captureCopyUs;
    const uint64_t copyStartUs = qpc_now_us();
    auto pixels = std::make_shared<std::vector<uint8_t>>(header->frameBytes);
    if (pixels && !pixels->empty()) {
      const auto* source = static_cast<const uint8_t*>(view) +
                           gdi_capture::frame_data_offset(best, header->frameBytes);
      std::memcpy(pixels->data(), source, header->frameBytes);
    }
    const uint64_t copyDoneUs = qpc_now_us();
    InterlockedExchange(&header->slots[best].state, gdi_capture::SlotFree);
    if (pixels && !pixels->empty() && onFrame) {
      onFrame(std::move(pixels), header->width, header->height, header->stride,
              captureUs, captureCopyUs, copyDoneUs - copyStartUs);
    }
  }

  void reader_loop() {
    HANDLE waits[] = {frameEvent, process.hProcess};
    while (!stopping.load(std::memory_order_acquire)) {
      const DWORD wait = WaitForMultipleObjects(2, waits, FALSE, 250);
      if (wait == WAIT_OBJECT_0) {
        read_latest_frame();
        continue;
      }
      if (wait == WAIT_OBJECT_0 + 1) {
        DWORD exitCode = 0;
        (void)GetExitCodeProcess(process.hProcess, &exitCode);
        started.store(false, std::memory_order_release);
        if (!stopping.load(std::memory_order_acquire) && onFallback) {
          onFallback("gdi_worker_exit_" + std::to_string(exitCode));
        }
        return;
      }
      if (wait != WAIT_TIMEOUT) {
        if (!stopping.load(std::memory_order_acquire) && onFallback) {
          onFallback(win32_error("gdi_worker_wait_failed"));
        }
        return;
      }
    }
  }

  void stop() {
    stopping.store(true, std::memory_order_release);
    if (stopEvent) SetEvent(stopEvent);
    if (frameEvent) SetEvent(frameEvent);
    if (process.hProcess) {
      const DWORD wait = WaitForSingleObject(process.hProcess, 1500);
      if (wait == WAIT_TIMEOUT) {
        // This handle is the exact child created by this instance; terminating it cannot
        // affect the installed host or another user's capture session.
        (void)TerminateProcess(process.hProcess, 1223);
        (void)WaitForSingleObject(process.hProcess, 500);
      }
    }
    if (reader.joinable()) reader.join();
    release_ready_slots();
    if (view) UnmapViewOfFile(view);
    view = nullptr;
    if (process.hThread) CloseHandle(process.hThread);
    if (process.hProcess) CloseHandle(process.hProcess);
    process = {};
    if (stopEvent) CloseHandle(stopEvent);
    if (frameEvent) CloseHandle(frameEvent);
    if (mapping) CloseHandle(mapping);
    if (job) CloseHandle(job);
    stopEvent = nullptr;
    frameEvent = nullptr;
    mapping = nullptr;
    job = nullptr;
    started.store(false, std::memory_order_release);
  }
};

GdiCaptureProcess::GdiCaptureProcess() : impl_(std::make_unique<Impl>()) {}

GdiCaptureProcess::~GdiCaptureProcess() { Stop(); }

bool GdiCaptureProcess::Start(const GdiCaptureProcessConfig& config,
                              GdiCaptureFrameHandler onFrame,
                              GdiCaptureLogHandler onLog,
                              GdiCaptureFallbackHandler onFallback,
                              std::string* detailOut) {
  Stop();
  impl_ = std::make_unique<Impl>();
  impl_->config = config;
  impl_->onFrame = std::move(onFrame);
  impl_->onLog = std::move(onLog);
  impl_->onFallback = std::move(onFallback);
  if (config.width < 2 || config.height < 2 || config.fps < 1) {
    if (detailOut) *detailOut = "gdi_invalid_config";
    return false;
  }
  const uint64_t stride64 = static_cast<uint64_t>(config.width) * 4;
  const uint64_t frameBytes64 = stride64 * config.height;
  if (frameBytes64 == 0 || frameBytes64 > 512ULL * 1024 * 1024) {
    if (detailOut) *detailOut = "gdi_frame_size_out_of_range";
    return false;
  }
  const uint32_t stride = static_cast<uint32_t>(stride64);
  const uint32_t frameBytes = static_cast<uint32_t>(frameBytes64);
  impl_->viewBytes = gdi_capture::mapping_bytes(frameBytes);

  static std::atomic<uint64_t> nonce{1};
  const uint64_t id = nonce.fetch_add(1, std::memory_order_relaxed);
  const DWORD pid = GetCurrentProcessId();
  const std::wstring mappingName = object_name(L"map", pid, id);
  const std::wstring frameEventName = object_name(L"frame", pid, id);
  const std::wstring stopEventName = object_name(L"stop", pid, id);

  impl_->mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                      static_cast<DWORD>(impl_->viewBytes >> 32),
                                      static_cast<DWORD>(impl_->viewBytes), mappingName.c_str());
  if (!impl_->mapping) {
    if (detailOut) *detailOut = win32_error("gdi_mapping_create_failed");
    return false;
  }
  impl_->view = MapViewOfFile(impl_->mapping, FILE_MAP_ALL_ACCESS, 0, 0, impl_->viewBytes);
  if (!impl_->view) {
    if (detailOut) *detailOut = win32_error("gdi_mapping_view_failed");
    impl_->stop();
    return false;
  }
  std::memset(impl_->view, 0, impl_->viewBytes);
  auto* header = static_cast<gdi_capture::SharedHeader*>(impl_->view);
  header->magic = gdi_capture::kMagic;
  header->version = gdi_capture::kVersion;
  header->width = config.width;
  header->height = config.height;
  header->stride = stride;
  header->frameBytes = frameBytes;
  header->slotCount = gdi_capture::kSlotCount;

  impl_->frameEvent = CreateEventW(nullptr, FALSE, FALSE, frameEventName.c_str());
  impl_->stopEvent = CreateEventW(nullptr, TRUE, FALSE, stopEventName.c_str());
  if (!impl_->frameEvent || !impl_->stopEvent) {
    if (detailOut) *detailOut = win32_error("gdi_event_create_failed");
    impl_->stop();
    return false;
  }

  impl_->job = CreateJobObjectW(nullptr, nullptr);
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION jobLimits{};
  jobLimits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  if (!impl_->job ||
      !SetInformationJobObject(impl_->job, JobObjectExtendedLimitInformation,
                               &jobLimits, sizeof(jobLimits))) {
    if (detailOut) *detailOut = win32_error("gdi_worker_job_create_failed");
    impl_->stop();
    return false;
  }

  const std::wstring worker = sibling_worker_path();
  if (worker.empty() || GetFileAttributesW(worker.c_str()) == INVALID_FILE_ATTRIBUTES) {
    if (detailOut) *detailOut = "gdi_worker_missing";
    impl_->stop();
    return false;
  }
  std::wostringstream command;
  command << L'"' << worker << L"\" --mapping \"" << mappingName
          << L"\" --frame-event \"" << frameEventName
          << L"\" --stop-event \"" << stopEventName
          << L"\" --fps " << config.fps;
  if (config.captureLayeredWindows) command << L" --capture-layered";
  std::wstring commandLine = command.str();
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  if (!CreateProcessW(worker.c_str(), commandLine.data(), nullptr, nullptr, FALSE,
                      CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr,
                      &startup, &impl_->process)) {
    if (detailOut) *detailOut = win32_error("gdi_worker_create_failed");
    impl_->stop();
    return false;
  }
  if (!AssignProcessToJobObject(impl_->job, impl_->process.hProcess)) {
    if (detailOut) *detailOut = win32_error("gdi_worker_job_assign_failed");
    impl_->stop();
    return false;
  }
  if (ResumeThread(impl_->process.hThread) == static_cast<DWORD>(-1)) {
    if (detailOut) *detailOut = win32_error("gdi_worker_resume_failed");
    impl_->stop();
    return false;
  }
  impl_->stopping.store(false, std::memory_order_release);
  impl_->started.store(true, std::memory_order_release);
  impl_->reader = std::thread([instance = impl_.get()]() { instance->reader_loop(); });
  impl_->log("capture", "gdi_worker_started pid=" + std::to_string(impl_->process.dwProcessId) +
                            " fps=" + std::to_string(config.fps) +
                            " sharedMiB=" + std::to_string(impl_->viewBytes / (1024 * 1024)));
  if (detailOut) *detailOut = "ok";
  return true;
}

void GdiCaptureProcess::Stop() {
  if (impl_) impl_->stop();
}

bool GdiCaptureProcess::running() const {
  return impl_ && impl_->started.load(std::memory_order_acquire);
}

}  // namespace remote60::native_poc
