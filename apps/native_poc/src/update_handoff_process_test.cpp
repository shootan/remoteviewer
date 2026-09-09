// The handover, across three real processes.
//
// This test exists because the defect it pins could not be seen anywhere else. Every part was
// right on its own: the bootstrap exits at once (it must -- it is running from a file the update
// replaces), the working copy signals when it has a verified download, and the host waits for that
// signal. Put together, the host was watching the BOOTSTRAP, saw it exit, and concluded the
// updater had died. It told the user the installed version was unchanged and stopped listening --
// while the working copy was still downloading. Then the copy signalled into an event nobody held
// and went on to stop the host it had just been promised would be left alone.
//
// A remote user is told the update was cancelled, and then loses the machine.
//
// So: real bootstrap, real worker, and the host's own wait -- `await_handoff`, the function the
// product calls, not a copy of it. The one thing played in-process is the host, because what is
// under test is its decision.

#include <windows.h>

#include <iostream>
#include <string>
#include <vector>

#include "update_handoff.hpp"

namespace {

using namespace remote60::native_poc::update;

int gFailures = 0;
int gChecks = 0;

void check(const std::string& name, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::cout << (ok ? "PASS  " : "FAIL  ") << name;
  if (!detail.empty()) std::cout << "  " << detail;
  std::cout << "\n";
}

std::string narrow(const std::wstring& s) { return std::string(s.begin(), s.end()); }

std::string read_text(const std::wstring& path) {
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return {};
  std::string out;
  char buffer[4096];
  DWORD read = 0;
  while (ReadFile(file, buffer, sizeof(buffer), &read, nullptr) && read > 0) {
    out.append(buffer, read);
  }
  CloseHandle(file);
  return out;
}

/** Where this run keeps its witness files: inside the repository, never %TEMP%. */
std::wstring run_root() {
  std::wstring base = GNLINK_HANDOFF_ROOT;
  for (wchar_t& c : base) {
    if (c == L'/') c = L'\\';
  }
  std::wstring built;
  for (size_t i = 0; i < base.size(); ++i) {
    built.push_back(base[i]);
    if (base[i] == L'\\' || i + 1 == base.size()) CreateDirectoryW(built.c_str(), nullptr);
  }
  wchar_t leaf[64]{};
  swprintf(leaf, 64, L"\\handoff-%lu", GetCurrentProcessId());
  const std::wstring path = base + leaf;
  CreateDirectoryW(path.c_str(), nullptr);
  return path;
}

/** One attempt, driven end to end. Everything the assertions need comes back in here. */
struct Attempt {
  HandoffStep step = HandoffStep::KeepRunning;
  std::string why;
  std::string witness;
  DWORD workerExit = 0xFFFFFFFF;
  bool workerFinished = false;
  /**
   * How long the WAIT took, not how long the attempt took.
   *
   * The two are different and the difference matters: after the wait returns, this test keeps
   * polling the witness so it can say what the worker decided. A timing assertion over the whole
   * attempt measures that polling -- which is the test's own patience, not the product's.
   */
  DWORD waitMs = 0;
};

}  // namespace

int main() {
  setvbuf(stdout, nullptr, _IONBF, 0);
  std::cout.setf(std::ios::unitbuf);

  const std::wstring root = run_root();
  const std::wstring fixture = GNLINK_HANDOFF_FIXTURE;
  int attemptNo = 0;

  /**
   * Runs one handover.
   *
   * `bootstrapExit` is what the bootstrap returns, `delayMs` how long the worker waits before
   * signalling, `signal` whether it signals at all, and `hostTimeoutMs` how long the host waits.
   */
  const auto run = [&](int bootstrapExit, int delayMs, bool signal, DWORD hostTimeoutMs,
                       int ackWaitMs, bool dieEarly = false, bool openAckLate = false,
                       int ackOpenDelayMs = 0, bool withoutAckChannel = false,
                       bool ackUnwritable = false) {
    Attempt a;
    ++attemptNo;
    wchar_t stem[128]{};
    swprintf(stem, 128, L"Local\\GNLinkHandoffTest-%lu-%d", GetCurrentProcessId(), attemptNo);
    const std::wstring readyName = stem;
    const std::wstring ackName = make_ack_event_name(readyName);
    wchar_t witnessLeaf[64]{};
    swprintf(witnessLeaf, 64, L"\\witness-%d.txt", attemptNo);
    const std::wstring witness = root + witnessLeaf;

    // Created BEFORE anything is started, so there is no window in which the worker signals an
    // event nobody holds -- which is a different failure from the one under test and would
    // otherwise be indistinguishable from it.
    HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, readyName.c_str());
    HANDLE ack = CreateEventW(nullptr, TRUE, FALSE, ackName.c_str());
    // A handle that can be waited on but NOT set. `SetEvent` fails on it with access denied,
    // which is a different case from having no channel at all: here there is something, and the
    // answer still cannot be sent. Opening with only SYNCHRONIZE is a deterministic way to get
    // that, rather than closing a handle and relying on undefined behaviour.
    HANDLE ackOwned = nullptr;
    if (ackUnwritable && ack) {
      ackOwned = ack;
      ack = OpenEventW(SYNCHRONIZE, FALSE, ackName.c_str());
    }

    std::wstring command = L"\"" + fixture + L"\" --bootstrap --exit-code " +
                           std::to_wstring(bootstrapExit) + L" --ready \"" + readyName +
                           L"\" --witness \"" + witness + L"\" --delay " +
                           std::to_wstring(delayMs) + L" --ack-wait " + std::to_wstring(ackWaitMs);
    if (!signal) command += L" --no-signal";
    if (dieEarly) command += L" --die-before-signal";
    if (openAckLate) command += L" --open-ack-late";
    if (ackOpenDelayMs > 0) command += L" --ack-open-delay " + std::to_wstring(ackOpenDelayMs);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    const BOOL started = CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE,
                                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    if (!started) {
      a.why = "the fixture would not start";
      if (ready) CloseHandle(ready);
      if (ack) CloseHandle(ack);
      if (ackOwned) CloseHandle(ackOwned);
      return a;
    }
    CloseHandle(pi.hThread);

    // The product's own wait. Not a copy of it -- a copy is what let the original stay wrong.
    const DWORD waitBegan = GetTickCount();
    // `withoutAckChannel` hands the wait no channel at all -- the caller cannot answer. It must
    // not agree to exit on that basis.
    a.step = await_handoff(ready, withoutAckChannel ? nullptr : ack, pi.hProcess, readyName,
                           hostTimeoutMs, nullptr, &a.why);
    a.waitMs = GetTickCount() - waitBegan;

    // Released IMMEDIATELY, the way the host releases them -- before the worker has necessarily
    // got to its side of the handshake. That is the window; leaving them open until the end of
    // the attempt would close it and the test would prove nothing.
    if (ready) CloseHandle(ready);
    if (ack) CloseHandle(ack);
    if (ackOwned) CloseHandle(ackOwned);
    ready = nullptr;
    ack = nullptr;
    ackOwned = nullptr;

    // The worker outlives the bootstrap, so the witness is read after giving it time to finish.
    // Waiting on it is what a host cannot do and a test can.
    CloseHandle(pi.hProcess);
    for (int waited = 0; waited < 15000; waited += 100) {
      const std::string text = read_text(witness);
      if (text.find("worker WOULD stop") != std::string::npos ||
          text.find("worker stood down") != std::string::npos ||
          text.find("died before signalling") != std::string::npos) {
        a.workerFinished = true;
        break;
      }
      Sleep(100);
    }
    a.witness = read_text(witness);
    return a;
  };

  // ---------------------------------------------------------------- 1. the defect itself
  {
    // The bootstrap exits immediately with 0 -- as it always does -- and the worker takes a
    // moment before it is ready. This is the ordinary, healthy update, and it was the broken one.
    const Attempt a = run(/*bootstrapExit=*/0, /*delayMs=*/700, /*signal=*/true,
                          /*hostTimeoutMs=*/20000, /*ackWaitMs=*/5000);
    check("1 bootstrap exits at once: the host keeps waiting and then agrees to exit",
          a.step == HandoffStep::ExitNow, std::string(handoff_step_name(a.step)) + ": " + a.why);
    check("1 the worker was acknowledged and would go ahead",
          a.witness.find("worker WOULD stop the product") != std::string::npos, a.witness);
  }

  // ---------------------------------------------------------------- 2. the bootstrap really fails
  {
    // A non-zero exit means it never got as far as starting a worker. There is nothing left to
    // wait for, and the host must say so rather than sitting out the full timeout.
    const Attempt a = run(/*bootstrapExit=*/3, /*delayMs=*/0, /*signal=*/false,
                          /*hostTimeoutMs=*/20000, /*ackWaitMs=*/1000);
    check("2 bootstrap failure: the host stops waiting", a.step == HandoffStep::KeepRunning,
          std::string(handoff_step_name(a.step)) + ": " + a.why);
    check("2 bootstrap failure: and does not sit out the whole timeout", a.waitMs < 15000,
          std::to_string(a.waitMs) + "ms");
  }

  // ---------------------------------------------------------------- 3. the worker never signals
  {
    // Nothing is wrong with the bootstrap; the worker simply does not get there -- a download
    // that failed, a server that was unreachable. The host waits, gives up, and keeps running.
    const Attempt a = run(/*bootstrapExit=*/0, /*delayMs=*/0, /*signal=*/false,
                          /*hostTimeoutMs=*/2000, /*ackWaitMs=*/500);
    check("3 no signal: the host keeps running", a.step == HandoffStep::KeepRunning,
          std::string(handoff_step_name(a.step)) + ": " + a.why);
    check("3 no signal: and the worker stands down rather than stopping anything",
          a.witness.find("worker stood down") != std::string::npos, a.witness);
  }

  // ---------------------------------------------------------------- 4. late: after the host gave up
  {
    // THE case. The host's patience runs out, it decides no update is happening -- in the product
    // it says so to the user -- and the worker turns up afterwards. It must not be acknowledged,
    // and it must not stop anything.
    const Attempt a = run(/*bootstrapExit=*/0, /*delayMs=*/2500, /*signal=*/true,
                          /*hostTimeoutMs=*/700, /*ackWaitMs=*/3000);
    check("4 late worker: the host had already decided to keep running",
          a.step == HandoffStep::KeepRunning, std::string(handoff_step_name(a.step)) + ": " + a.why);
    check("4 late worker: it is never acknowledged, so it stands down",
          a.witness.find("worker stood down") != std::string::npos, a.witness);
    check("4 late worker: and it does NOT decide to stop the product",
          a.witness.find("worker WOULD stop the product") == std::string::npos, a.witness);
  }

  // ---------------------------------------------------------------- 4b. the worker dies
  {
    // The download process stops. Watching only the bootstrap, this cost the caller the entire
    // timeout -- there was nothing left being watched, so "died at once" and "still going" looked
    // identical for ten minutes. The liveness the worker holds is released by its death, so the
    // caller learns promptly and, importantly, learns WHY.
    const Attempt a = run(/*bootstrapExit=*/0, /*delayMs=*/300, /*signal=*/true,
                          /*hostTimeoutMs=*/20000, /*ackWaitMs=*/1000, /*dieEarly=*/true);
    check("4b worker dies: the host keeps running", a.step == HandoffStep::KeepRunning,
          std::string(handoff_step_name(a.step)) + ": " + a.why);
    // The wait itself, against a 20s deadline. Well under it means the death was noticed rather
    // than waited out -- which is the whole reason the working copy holds a liveness handle.
    check("4b worker dies: it is noticed rather than waited out", a.waitMs < 5000,
          std::to_string(a.waitMs) + "ms of a 20000ms deadline");
    check("4b worker dies: and the reason says the working copy stopped, not that time ran out",
          a.why.find("stopped before") != std::string::npos, a.why);
  }

  // ---------------------------------------------------------------- 6. the acknowledgement window
  {
    // The channel is held from before the signal. The host answers and lets go of its handle at
    // once -- as it does -- and the worker must still find the answer, because its own handle has
    // been keeping the object alive the whole time.
    const Attempt a = run(/*bootstrapExit=*/0, /*delayMs=*/300, /*signal=*/true,
                          /*hostTimeoutMs=*/20000, /*ackWaitMs=*/5000, /*dieEarly=*/false,
                          /*openAckLate=*/false, /*ackOpenDelayMs=*/600);
    check("6 ack held early: the host agrees to exit", a.step == HandoffStep::ExitNow,
          std::string(handoff_step_name(a.step)) + ": " + a.why);
    check("6 ack held early: the worker held the channel before signalling",
          a.witness.find("holds the ack channel before signalling") != std::string::npos,
          a.witness);
    check("6 ack held early: and the answer was still there after the host let go",
          a.witness.find("worker WOULD stop the product") != std::string::npos, a.witness);
  }
  {
    // The counter-example, and it is the defect: open the channel only when the answer is wanted.
    // By then the host has answered and closed, the named object is gone with the last handle,
    // and the answer that WAS delivered cannot be found. The update stands down having already
    // told the host to leave.
    //
    // A test on the return value of the wait would not see this. The host returned ExitNow and
    // was right to; the handle stopped existing.
    const Attempt a = run(/*bootstrapExit=*/0, /*delayMs=*/300, /*signal=*/true,
                          /*hostTimeoutMs=*/20000, /*ackWaitMs=*/2000, /*dieEarly=*/false,
                          /*openAckLate=*/true, /*ackOpenDelayMs=*/600);
    check("6 counter-example: the host still agreed to exit", a.step == HandoffStep::ExitNow,
          std::string(handoff_step_name(a.step)) + ": " + a.why);
    check("6 counter-example: opening late finds nothing",
          a.witness.find("late and it was gone") != std::string::npos, a.witness);
    check("6 counter-example: so the worker stands down -- which is why it opens early",
          a.witness.find("worker stood down") != std::string::npos, a.witness);
  }
  {
    // No channel at all on the caller's side. It cannot answer, so it must not leave: the updater
    // is waiting to be told it may go ahead, and a caller that departs unable to say so leaves it
    // holding a product it is not allowed to stop.
    const Attempt a = run(/*bootstrapExit=*/0, /*delayMs=*/300, /*signal=*/true,
                          /*hostTimeoutMs=*/20000, /*ackWaitMs=*/1000, /*dieEarly=*/false,
                          /*openAckLate=*/false, /*ackOpenDelayMs=*/0,
                          /*withoutAckChannel=*/true);
    check("6 no channel: the host does NOT agree to exit", a.step == HandoffStep::KeepRunning,
          std::string(handoff_step_name(a.step)) + ": " + a.why);
    check("6 no channel: and says the acknowledgement could not be delivered",
          a.why.find("could not be delivered") != std::string::npos, a.why);
    check("6 no channel: the worker stands down too",
          a.witness.find("worker stood down") != std::string::npos, a.witness);
  }

  {
    // The channel exists and the answer cannot be SENT -- distinct from having no channel, and
    // the distinction matters because the code path is different: one skips the send, the other
    // attempts it and is refused. Both must end the same way, with the caller staying put.
    const Attempt a = run(/*bootstrapExit=*/0, /*delayMs=*/300, /*signal=*/true,
                          /*hostTimeoutMs=*/20000, /*ackWaitMs=*/1000, /*dieEarly=*/false,
                          /*openAckLate=*/false, /*ackOpenDelayMs=*/0,
                          /*withoutAckChannel=*/false, /*ackUnwritable=*/true);
    check("6 unwritable channel: the host does NOT agree to exit",
          a.step == HandoffStep::KeepRunning,
          std::string(handoff_step_name(a.step)) + ": " + a.why);
    check("6 unwritable channel: and says the acknowledgement could not be delivered",
          a.why.find("could not be delivered") != std::string::npos, a.why);
    check("6 unwritable channel: the worker stands down rather than stopping anything",
          a.witness.find("worker stood down") != std::string::npos, a.witness);
  }

  // ---------------------------------------------------------------- 5. the race, both directions
  {
    // The host decides at the same moment the worker signals. Whichever way it lands, the two
    // must agree: acknowledged means the worker goes ahead, not acknowledged means it does not.
    // What must never happen is the host keeping running while the worker goes ahead anyway.
    // The timeout is swept ACROSS the worker's delay so the decision lands on both sides. A
    // fixed timing produced the same answer every run, and a race test that only ever sees one
    // outcome is not testing the race -- it would pass unchanged if the other side were broken.
    bool sawExit = false;
    bool sawKeep = false;
    for (int timeout = 200; timeout <= 900; timeout += 100) {
      const Attempt a = run(/*bootstrapExit=*/0, /*delayMs=*/500, /*signal=*/true,
                            /*hostTimeoutMs=*/static_cast<DWORD>(timeout), /*ackWaitMs=*/2000);
      const bool hostLeaving = a.step == HandoffStep::ExitNow;
      const bool workerGoing = a.witness.find("worker WOULD stop the product") != std::string::npos;
      sawExit = sawExit || hostLeaving;
      sawKeep = sawKeep || !hostLeaving;
      // The one thing that must never happen: the host stays and the updater goes ahead anyway.
      check("5 race: the host and the worker never disagree", hostLeaving == workerGoing,
            std::to_string(timeout) + "ms: " + handoff_step_name(a.step) + " vs " + a.witness);
    }
    // Non-vacuity, stated rather than assumed: the sweep really did land on both sides.
    check("5 race: both outcomes actually occurred", sawExit && sawKeep,
          std::string("exit=") + (sawExit ? "yes" : "no") + " keep=" + (sawKeep ? "yes" : "no"));
  }

  // Only this run's own directory, and only if it is under the configured root.
  {
    std::wstring base = GNLINK_HANDOFF_ROOT;
    for (wchar_t& c : base) {
      if (c == L'/') c = L'\\';
    }
    const bool mine = root.size() > base.size() &&
                      _wcsnicmp(root.c_str(), base.c_str(), base.size()) == 0;
    if (mine) {
      WIN32_FIND_DATAW find{};
      HANDLE h = FindFirstFileW((root + L"\\*").c_str(), &find);
      if (h != INVALID_HANDLE_VALUE) {
        do {
          const std::wstring name = find.cFileName;
          if (name == L"." || name == L"..") continue;
          DeleteFileW((root + L"\\" + name).c_str());
        } while (FindNextFileW(h, &find));
        FindClose(h);
      }
      RemoveDirectoryW(root.c_str());
    } else {
      std::cout << "NOTE  refusing to remove " << narrow(root) << " -- not under the test root\n";
    }
  }

  std::cout << (gFailures == 0 ? "RESULT: ALL PASS  (" : "RESULT: FAILED  (") << gChecks
            << " checks, " << gFailures << " failed)\n";
  return gFailures == 0 ? 0 : 1;
}
