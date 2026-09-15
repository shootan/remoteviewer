#include "host_thumbnail_helper.hpp"

#include <atomic>
#include <mutex>
#include <sstream>
#include <string>

#include "host_thumbnail_kill.hpp"
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

// How long to wait for a killed helper to actually be gone. A process being terminated is not a
// process that has terminated, and the difference decides whether its memory can be released.
constexpr DWORD kKillConfirmMs = 5000;

/** At most one capture in flight, process wide. Checked, not assumed from the caller's shape. */
std::atomic<bool> gCaptureInFlight{false};

/**
 * A helper that would not die, and the handles belonging to that attempt.
 *
 * How the memory is actually shared, since an earlier version of this comment got it wrong and the
 * wrong version is the intuitive one. A file mapping is an object with its own lifetime. The helper
 * called OpenFileMapping and MapViewOfFile itself, so it holds a handle and a view of its own, in
 * its own address space, counted separately from ours. UnmapViewOfFile releases THIS process's
 * view; the section stays alive while any process still maps it. There is no page for the helper to
 * fall through, and no writer left holding a freed address -- that hazard belongs to threads
 * sharing one address space, which is the thing a process boundary removes.
 *
 * So holding these handles is not a memory-safety requirement, and this comment no longer pretends
 * otherwise. What it buys is bookkeeping: a process handle that is still open is a process whose
 * exit can still be observed, so helper_still_lingering() can tell when the thing finally goes and
 * release the rest then. The job handle stays open for the same reason -- it is what still ends
 * that process if this one exits. And refusing captures meanwhile keeps a situation nobody
 * understands from becoming one stray process per request.
 *
 * ⚠️ UNEXERCISED. Nothing in the suite reaches this, and not for want of trying: TerminateProcess
 * on a process this one started does not fail on this OS, so killConfirmed is always true and the
 * branch never runs. Removing it breaks no test. Read it as a guard, not as tested behaviour.
 */
struct Lingering {
  std::mutex mu;
  HANDLE process = nullptr;
  HANDLE mapping = nullptr;
  void* view = nullptr;
  HANDLE done = nullptr;
  HANDLE job = nullptr;
};
Lingering gLingering;

/** True while a previously killed helper has still not exited. Releases it once it has. */
bool helper_still_lingering() {
  std::lock_guard<std::mutex> lk(gLingering.mu);
  if (!gLingering.process) return false;
  if (WaitForSingleObject(gLingering.process, 0) != WAIT_OBJECT_0) return true;
  // It finally went. Now -- and only now -- is its memory safe to release.
  CloseHandle(gLingering.process);
  gLingering.process = nullptr;
  if (gLingering.view) UnmapViewOfFile(gLingering.view);
  if (gLingering.mapping) CloseHandle(gLingering.mapping);
  if (gLingering.done) CloseHandle(gLingering.done);
  if (gLingering.job) CloseHandle(gLingering.job);
  gLingering.view = nullptr;
  gLingering.mapping = nullptr;
  gLingering.done = nullptr;
  gLingering.job = nullptr;
  return false;
}

/**
 * Everything one attempt owns, torn down in an order that is not negotiable.
 *
 * The helper writes into the mapped view. Unmapping it while the helper might still be running is
 * releasing memory somebody else is writing to, so the process is ended, CONFIRMED gone, and only
 * then is the view released. If the confirmation does not come, nothing is released at all -- see
 * Lingering. The job object is the backstop: if this host dies, the OS ends the helper rather than
 * leaving it holding a window handle.
 */
struct Attempt {
  HANDLE job = nullptr;
  HANDLE mapping = nullptr;
  void* view = nullptr;
  HANDLE done = nullptr;
  PROCESS_INFORMATION pi{};
  // Not read anywhere outside this struct. Kept because the branch below turns on it and a
  // named flag says what that branch is about; an earlier comment claimed a caller read it, and
  // no caller does.
  bool killConfirmed = true;
  uint64_t* teardownOut = nullptr;  // where to report how long the teardown took

  ~Attempt() {
    const uint64_t teardownStart = qpc_now_us();
    struct Report {
      uint64_t* out;
      uint64_t start;
      ~Report() { if (out) *out = qpc_now_us() - start; }
    } report{teardownOut, teardownStart};

    if (pi.hThread) CloseHandle(pi.hThread);
    if (!pi.hProcess) {
      if (view) UnmapViewOfFile(view);
      if (mapping) CloseHandle(mapping);
      if (done) CloseHandle(done);
      if (job) CloseHandle(job);
      return;
    }

    // The one OS call in this teardown, and the only thing that can tell us the helper is really
    // gone. It lives in its own translation unit so the lingering test can link a version that
    // reports failure -- see host_thumbnail_kill.hpp for why that is link-time and not a flag.
    killConfirmed = terminate_and_confirm(pi.hProcess, kKillConfirmMs);

    if (killConfirmed) {
      CloseHandle(pi.hProcess);
      if (view) UnmapViewOfFile(view);
      if (mapping) CloseHandle(mapping);
      if (done) CloseHandle(done);
      if (job) CloseHandle(job);  // KILL_ON_JOB_CLOSE: nothing outlives this
      return;
    }

    // It did not die. Hand everything to Lingering rather than releasing memory the helper may
    // still write into, and leave the job open so the OS still ends it when this process goes.
    std::lock_guard<std::mutex> lk(gLingering.mu);
    gLingering.process = pi.hProcess;
    gLingering.mapping = mapping;
    gLingering.view = view;
    gLingering.done = done;
    gLingering.job = job;
  }
};

}  // namespace

namespace {

/**
 * The attempt itself. Wrapped below so that `elapsedUs` can be stamped after Attempt is destroyed.
 *
 * That ordering is the whole reason for the split: ending a helper and confirming it is gone
 * happens in the destructor, the caller is blocked for all of it, and a number that stopped at the
 * wait would describe something nobody experiences. The deadline is a budget for the round trip,
 * so the round trip is what gets measured.
 */
ThumbnailCaptureResult capture_thumbnail_attempt(HWND hwnd, uint32_t maxW, uint32_t maxH,
                                                 uint64_t deadlineUs, HANDLE cancelEvent,
                                                 uint64_t* teardownUs) {
  ThumbnailCaptureResult result;
  const uint64_t start = qpc_now_us();
  const auto fail = [&](const char* why) {
    result.outcome = ThumbnailOutcome::Failed;
    result.detail = why;
    result.elapsedUs = qpc_now_us() - start;
    return result;
  };
  /**
   * A refusal that is ours rather than the window's.
   *
   * Failed spends one of the window's three attempts, and these two have nothing to do with the
   * window: one means another dispatcher got there first, the other means a previous helper will
   * not die. Charging for them puts a healthy window into a sixty second cooldown for something it
   * did not do -- three concurrent requests is all it takes. Canceled already means "nothing was
   * asked of it" and the budget treats it as free.
   */
  const auto refuse = [&](const char* why) {
    result.outcome = ThumbnailOutcome::Canceled;
    result.detail = why;
    result.elapsedUs = qpc_now_us() - start;
    return result;
  };

  // A helper that survived being killed is still out there. Starting another would make two, and
  // whatever is wrong with the first is not improved by company. Refused immediately -- the caller
  // gets a failure in microseconds rather than a deadline, and the window is charged a retry.
  if (helper_still_lingering()) return refuse("thumb_helper_lingering");

  // One at a time, enforced rather than inferred. The control dispatcher is not one thread: TCP
  // and UDP both serve, so two requests really can arrive together.
  bool idle = false;
  if (!gCaptureInFlight.compare_exchange_strong(idle, true)) {
    return refuse("thumb_busy");
  }
  struct InFlightGuard {
    ~InFlightGuard() { gCaptureInFlight.store(false); }
  } inFlightGuard;

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
  result.spawnUs = qpc_now_us() - start;
  // The caller owns this, deliberately. Pointing it at a field of `result` would have the
  // destructor write into an object the return has already moved from unless NRVO happens to
  // apply, and "happens to" is not a guarantee.
  attempt.teardownOut = teardownUs;

  ThumbnailWaitHandles handles;
  handles.done = attempt.done;
  handles.worker = attempt.pi.hProcess;
  handles.cancel = cancelEvent;
  uint64_t waited = 0;
  const ThumbnailWaitResult waitResult = wait_for_thumbnail(handles, deadlineUs, &waited);
  result.waitUs = waited;

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

}  // namespace

ThumbnailCaptureResult capture_thumbnail_isolated(HWND hwnd, uint32_t maxW, uint32_t maxH,
                                                  uint64_t deadlineUs, HANDLE cancelEvent) {
  const uint64_t start = qpc_now_us();
  uint64_t teardownUs = 0;
  ThumbnailCaptureResult result =
      capture_thumbnail_attempt(hwnd, maxW, maxH, deadlineUs, cancelEvent, &teardownUs);
  result.teardownUs = teardownUs;
  // Re-stamped now that the helper has been ended and confirmed gone. This is what the caller
  // actually waited for -- spawn, IPC, capture, and the kill confirmation when there was one.
  result.elapsedUs = qpc_now_us() - start;
  // Asked after the teardown, since that is when a helper that would not die becomes known.
  result.helperLingering = helper_still_lingering();
  return result;
}

}  // namespace remote60::native_poc
