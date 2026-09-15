#include "host_thumbnail_helper.hpp"

#include <sstream>
#include <string>

#include "host_thumbnail_wait.hpp"
#include "thumbnail_ipc.hpp"
#include "time_utils.hpp"

namespace remote60::native_poc {
namespace {

namespace ipc = thumbnail_ipc;

std::wstring sibling_capture_path() {
  std::wstring path(32768, L'\0');
  const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
  if (length == 0 || length >= path.size()) return {};
  path.resize(length);
  const size_t slash = path.find_last_of(L"\\/");
  if (slash == std::wstring::npos) return {};
  path.resize(slash + 1);
  path += L"GNLinkCapture.exe";
  return path;
}

/** Unique per attempt: a name reused across attempts could be opened by a helper we gave up on. */
std::wstring object_name(const wchar_t* kind, uint64_t nonce) {
  std::wostringstream oss;
  oss << L"Local\\remote60_thumb_" << kind << L"_" << GetCurrentProcessId() << L"_" << nonce;
  return oss.str();
}

/**
 * Everything one attempt owns, torn down in an order that is not negotiable.
 *
 * The helper writes into the mapped view. Unmapping it while the helper might still be running is
 * releasing memory somebody else is writing to, so the process is ended and WAITED FOR first, and
 * only then is the view released. The job object is the backstop: if this host dies, the OS ends
 * the helper rather than leaving it holding a window handle.
 */
struct Attempt {
  HANDLE job = nullptr;
  HANDLE mapping = nullptr;
  void* view = nullptr;
  HANDLE done = nullptr;
  PROCESS_INFORMATION pi{};

  ~Attempt() {
    if (pi.hProcess) {
      // Ordered on purpose: end it, confirm it is gone, and only then let go of the memory it was
      // writing into.
      TerminateProcess(pi.hProcess, 1);
      WaitForSingleObject(pi.hProcess, 5000);
      CloseHandle(pi.hProcess);
    }
    if (pi.hThread) CloseHandle(pi.hThread);
    if (view) UnmapViewOfFile(view);
    if (mapping) CloseHandle(mapping);
    if (done) CloseHandle(done);
    if (job) CloseHandle(job);  // KILL_ON_JOB_CLOSE: nothing outlives this
  }
};

}  // namespace

ThumbnailCaptureResult capture_thumbnail_isolated(HWND hwnd, uint32_t maxW, uint32_t maxH,
                                                  uint64_t deadlineUs, HANDLE cancelEvent) {
  ThumbnailCaptureResult result;
  const uint64_t start = qpc_now_us();
  const auto fail = [&](const char* why) {
    result.outcome = ThumbnailOutcome::Failed;
    result.detail = why;
    result.elapsedUs = qpc_now_us() - start;
    return result;
  };

  Attempt attempt;
  attempt.job = CreateJobObjectW(nullptr, nullptr);
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  if (!attempt.job ||
      !SetInformationJobObject(attempt.job, JobObjectExtendedLimitInformation, &limits,
                               sizeof(limits))) {
    return fail("thumb_job_create_failed");
  }

  const uint64_t nonce = qpc_now_us();
  const std::wstring mappingName = object_name(L"map", nonce);
  const std::wstring doneName = object_name(L"done", nonce);

  attempt.mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                       ipc::kBlockBytes, mappingName.c_str());
  if (!attempt.mapping) return fail("thumb_mapping_failed");
  attempt.view = MapViewOfFile(attempt.mapping, FILE_MAP_ALL_ACCESS, 0, 0, ipc::kBlockBytes);
  if (!attempt.view) return fail("thumb_map_view_failed");
  // Zeroed before the helper starts, so a partially written block cannot be mistaken for a
  // complete one from a previous attempt.
  auto* header = static_cast<ipc::Header*>(attempt.view);
  *header = ipc::Header{};

  attempt.done = CreateEventW(nullptr, TRUE, FALSE, doneName.c_str());
  if (!attempt.done) return fail("thumb_event_failed");

  const std::wstring worker = sibling_capture_path();
  if (worker.empty() || GetFileAttributesW(worker.c_str()) == INVALID_FILE_ATTRIBUTES) {
    return fail("thumb_worker_missing");
  }

  std::wostringstream command;
  command << L'"' << worker << L"\" --thumbnail --mapping \"" << mappingName
          << L"\" --done-event \"" << doneName << L"\" --hwnd "
          << static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(hwnd)) << L" --max-w "
          << maxW << L" --max-h " << maxH;
  std::wstring commandLine = command.str();

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  if (!CreateProcessW(worker.c_str(), commandLine.data(), nullptr, nullptr, FALSE,
                      CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr, &si, &attempt.pi)) {
    return fail("thumb_worker_create_failed");
  }
  // Assigned before it runs: a helper that started and escaped the job would be exactly the
  // orphan this design exists to make impossible.
  if (!AssignProcessToJobObject(attempt.job, attempt.pi.hProcess)) {
    return fail("thumb_job_assign_failed");
  }
  if (ResumeThread(attempt.pi.hThread) == static_cast<DWORD>(-1)) {
    return fail("thumb_worker_resume_failed");
  }

  ThumbnailWaitHandles handles;
  handles.done = attempt.done;
  handles.worker = attempt.pi.hProcess;
  handles.cancel = cancelEvent;
  uint64_t waited = 0;
  const ThumbnailWaitResult waitResult = wait_for_thumbnail(handles, deadlineUs, &waited);

  result.outcome = outcome_for_wait(waitResult);
  result.elapsedUs = qpc_now_us() - start;

  if (waitResult != ThumbnailWaitResult::Completed) {
    // Nothing to read. The destructor ends the helper and waits for it before the view goes.
    switch (waitResult) {
      case ThumbnailWaitResult::TimedOut: result.detail = "thumb_deadline"; break;
      case ThumbnailWaitResult::Canceled: result.detail = "thumb_canceled"; break;
      default: result.detail = "thumb_worker_gone"; break;
    }
    return result;
  }

  // Completed. Everything below re-derives what it expects instead of trusting the block: the
  // helper is a separate process and a header it wrote is input, not a promise.
  if (header->magic != ipc::kMagic || header->version != ipc::kVersion || header->ok != 1) {
    result.outcome = ThumbnailOutcome::Failed;
    result.detail = "thumb_no_pixels";
    return result;
  }
  const uint64_t expected = static_cast<uint64_t>(header->width) * header->height * 4ull;
  if (header->width == 0 || header->height == 0 || header->stride != header->width * 4u ||
      expected != header->byteCount || header->byteCount > ipc::kMaxPixelBytes) {
    result.outcome = ThumbnailOutcome::Failed;
    result.detail = "thumb_bad_block";
    return result;
  }

  const auto* pixels = reinterpret_cast<const uint8_t*>(header + 1);
  result.bgra.assign(pixels, pixels + header->byteCount);
  result.width = header->width;
  result.height = header->height;
  result.outcome = ThumbnailOutcome::Ok;
  result.detail = "ok";
  return result;
}

}  // namespace remote60::native_poc
