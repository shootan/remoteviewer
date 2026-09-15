// The four ways a thumbnail capture can end, and how long each of them takes to find out.
//
// The property that matters most is the second one: a worker that dies without producing anything
// must be noticed immediately, not after the deadline. That is what makes an update that stops
// the helper, a crash, and a kill from anywhere else all cost the same -- nothing.
//
// Most cases use plain events, because the wait does not care what kind of waitable handle it was
// given and events make the timing exact. One case uses a real child process, because "a process
// exiting signals its handle" is the assumption everything else rests on, and assuming it here
// would be assuming the thing under test.

#include "host_thumbnail_wait.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace {

using remote60::native_poc::outcome_for_wait;
using remote60::native_poc::ThumbnailOutcome;
using remote60::native_poc::ThumbnailWaitHandles;
using remote60::native_poc::ThumbnailWaitResult;
using remote60::native_poc::wait_for_thumbnail;

int gFailures = 0;
int gChecks = 0;

void check(const std::string& name, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::cout << (ok ? "PASS  " : "FAIL  ") << name;
  if (!detail.empty()) std::cout << "  " << detail;
  std::cout << "\n";
}

constexpr uint64_t kDeadlineUs = 1000 * 1000;  // the candidate one second
// What "immediately" has to mean for this to be worth doing. Two orders of magnitude under the
// deadline, and still loose enough that a busy machine cannot fail it.
constexpr uint64_t kPromptUs = 250 * 1000;

struct Event {
  HANDLE h = nullptr;
  explicit Event(bool signalled = false) {
    h = CreateEventW(nullptr, TRUE, signalled ? TRUE : FALSE, nullptr);
  }
  ~Event() { if (h) CloseHandle(h); }
  Event(const Event&) = delete;
  Event& operator=(const Event&) = delete;
};

const char* name_of(ThumbnailWaitResult r) {
  switch (r) {
    case ThumbnailWaitResult::Completed: return "Completed";
    case ThumbnailWaitResult::WorkerGone: return "WorkerGone";
    case ThumbnailWaitResult::TimedOut: return "TimedOut";
    case ThumbnailWaitResult::Canceled: return "Canceled";
  }
  return "?";
}

void test_completion_is_seen() {
  Event done(true);
  Event worker;
  ThumbnailWaitHandles handles{done.h, worker.h, nullptr};
  uint64_t elapsed = 0;
  const auto result = wait_for_thumbnail(handles, kDeadlineUs, &elapsed);
  check("a worker that signals is Completed", result == ThumbnailWaitResult::Completed,
        name_of(result));
  check("...without waiting", elapsed < kPromptUs, std::to_string(elapsed) + "us");
  check("...and costs the window nothing",
        outcome_for_wait(result) == ThumbnailOutcome::Ok);
}

void test_dead_worker_is_seen_immediately() {
  // The one this file exists for. Nothing signals `done`; the worker handle goes instead.
  Event done;
  Event worker(true);
  ThumbnailWaitHandles handles{done.h, worker.h, nullptr};
  uint64_t elapsed = 0;
  const auto result = wait_for_thumbnail(handles, kDeadlineUs, &elapsed);
  check("a worker that dies without a result is WorkerGone",
        result == ThumbnailWaitResult::WorkerGone, name_of(result));
  check("...and the deadline is NOT spent finding out", elapsed < kPromptUs,
        std::to_string(elapsed) + "us of a " + std::to_string(kDeadlineUs) + "us deadline");
  check("...and it is charged like any other failure",
        outcome_for_wait(result) == ThumbnailOutcome::Failed);
}

void test_completion_wins_a_tie() {
  // A worker that writes its result, signals, and exits leaves both handles signalled. Reading
  // that as a dead worker would throw away a perfectly good thumbnail and charge the window for
  // it -- and since a well behaved worker exits every time, it would do so constantly.
  Event done(true);
  Event worker(true);
  ThumbnailWaitHandles handles{done.h, worker.h, nullptr};
  const auto result = wait_for_thumbnail(handles, kDeadlineUs);
  check("a worker that signalled and then exited is Completed, not WorkerGone",
        result == ThumbnailWaitResult::Completed, name_of(result));
}

void test_silence_times_out() {
  Event done;
  Event worker;
  ThumbnailWaitHandles handles{done.h, worker.h, nullptr};
  uint64_t elapsed = 0;
  const uint64_t shortDeadline = 200 * 1000;
  const auto result = wait_for_thumbnail(handles, shortDeadline, &elapsed);
  check("a worker that is alive and silent times out", result == ThumbnailWaitResult::TimedOut,
        name_of(result));
  // Not `elapsed >= shortDeadline`. A Windows wait is granular to the system timer tick, and
  // measured against QPC it can come back a millisecond or two early -- observed at 198.6ms of a
  // 200ms deadline. The claim worth making is that it waited, not that it waited to the
  // microsecond, so the bound carries a tick's worth of slack.
  constexpr uint64_t kTimerSlackUs = 20 * 1000;
  check("...after waiting out the deadline", elapsed + kTimerSlackUs >= shortDeadline,
        std::to_string(elapsed) + "us vs " + std::to_string(shortDeadline) + "us deadline");
  check("...and not much longer than it", elapsed < shortDeadline * 2,
        std::to_string(elapsed) + "us");
  check("...and is charged", outcome_for_wait(result) == ThumbnailOutcome::TimedOut);
}

void test_cancel_is_free() {
  Event done;
  Event worker;
  Event cancel(true);
  ThumbnailWaitHandles handles{done.h, worker.h, cancel.h};
  uint64_t elapsed = 0;
  const auto result = wait_for_thumbnail(handles, kDeadlineUs, &elapsed);
  check("a cancelled wait is Canceled", result == ThumbnailWaitResult::Canceled, name_of(result));
  check("...promptly", elapsed < kPromptUs, std::to_string(elapsed) + "us");
  check("...and costs the window nothing",
        outcome_for_wait(result) == ThumbnailOutcome::Canceled);
}

void test_missing_handles_do_not_wait() {
  ThumbnailWaitHandles none;
  uint64_t elapsed = 0;
  const auto result = wait_for_thumbnail(none, kDeadlineUs, &elapsed);
  check("no completion handle is WorkerGone, not a wait",
        result == ThumbnailWaitResult::WorkerGone, name_of(result));
  check("...and returns at once", elapsed < kPromptUs, std::to_string(elapsed) + "us");

  // A worker handle is optional in the signature; without one the deadline is all there is.
  Event done;
  ThumbnailWaitHandles doneOnly{done.h, nullptr, nullptr};
  const auto timed = wait_for_thumbnail(doneOnly, 100 * 1000, &elapsed);
  check("without a worker handle the deadline still applies",
        timed == ThumbnailWaitResult::TimedOut, name_of(timed));
}

void test_a_real_process_exit_signals() {
  // Everything above assumes a process handle behaves like an event. It does, and this is the
  // check that says so rather than taking it on trust.
  std::wstring cmd = L"cmd.exe /c exit 3";
  std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
  mutableCmd.push_back(L'\0');
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                      nullptr, nullptr, &si, &pi)) {
    check("a short-lived child process could be started", false);
    return;
  }

  Event done;  // never signalled: the child produces no result
  ThumbnailWaitHandles handles{done.h, pi.hProcess, nullptr};
  uint64_t elapsed = 0;
  const auto result = wait_for_thumbnail(handles, 5 * 1000 * 1000, &elapsed);
  check("a real process exiting is seen as WorkerGone", result == ThumbnailWaitResult::WorkerGone,
        name_of(result));
  check("...well inside a five second deadline", elapsed < 5 * 1000 * 1000,
        std::to_string(elapsed / 1000) + "ms");

  DWORD code = 0;
  GetExitCodeProcess(pi.hProcess, &code);
  check("...whatever it exited with", code == 3, "exit " + std::to_string(code));
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
}

}  // namespace

int main() {
  test_completion_is_seen();
  test_dead_worker_is_seen_immediately();
  test_completion_wins_a_tie();
  test_silence_times_out();
  test_cancel_is_free();
  test_missing_handles_do_not_wait();
  test_a_real_process_exit_signals();

  std::cout << "\n" << (gFailures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILED") << "  ("
            << gChecks << " checks, " << gFailures << " failed)\n";
  return gFailures == 0 ? 0 : 1;
}
