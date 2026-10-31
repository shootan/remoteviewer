# -*- coding: utf-8 -*-
import io

p = 'apps/native_poc/src/host_app_main.cpp'
d = io.open(p, encoding='utf-8').read()

# ---------------------------------------------------------------- Stop() verifies, and says so
old = '''  void Stop() {
    if (!running_.exchange(false)) return;
    TerminateChild();
    if (supervisor_.joinable()) supervisor_.join();
  }'''
new = '''  /**
   * Stops supervising and does not return until the child is gone.
   *
   * It used to ask and leave: clear the flag, TerminateProcess, join. That is enough when the join
   * is reached, and the join is what actually waits -- but a process leaving for an update needs
   * more than "enough when nothing goes wrong". On 2026-09-11 a Host stood down for an update and a
   * GNLinkStream created one second after its acknowledgement was still running afterwards, holding
   * UDP 43000 and 3478. The replacement Host could not bind, died with code 3, and retried until
   * the machine was unusable -- twice. Recovery took a person killing the orphan by hand.
   *
   * So this reports what it actually achieved. `outDetail` is for the log: which pid, and whether
   * it really went. A caller that is about to exit can then say so instead of assuming.
   */
  bool Stop(std::string* outDetail = nullptr) {
    const uint32_t pid = ChildPid();
    if (!running_.exchange(false)) {
      // Already stopped -- but "already" is not the same as "gone", and this is the path that used
      // to return without looking. If a child is somehow still there, the caller must hear it.
      if (pid != 0 && ChildAlive()) {
        if (outDetail) {
          *outDetail = "child pid " + std::to_string(pid) +
                       " is still alive although supervision had already stopped";
        }
        TerminateChild();
        const bool gone = WaitForChildExit(kChildExitBudgetMs);
        if (outDetail && gone) *outDetail += "; it exited after being asked again";
        return gone;
      }
      if (outDetail) *outDetail = "already stopped, no child";
      return true;
    }
    TerminateChild();
    if (supervisor_.joinable()) supervisor_.join();
    // The join is the real guarantee: Supervise() only returns once its wait on the child has
    // returned. Checked anyway, because this is the one place whose failure leaves a process
    // holding the ports the next one needs.
    const bool gone = !ChildAlive();
    if (outDetail) {
      if (pid == 0) {
        *outDetail = "no child was running";
      } else if (gone) {
        *outDetail = "child pid " + std::to_string(pid) + " exited";
      } else {
        *outDetail = "child pid " + std::to_string(pid) + " is STILL RUNNING after the stop";
      }
    }
    return gone;
  }

  /** The child's pid, or 0. Taken under the lock that publishes it. */
  uint32_t ChildPid() {
    std::lock_guard<std::mutex> lock(mu_);
    return child_.hProcess ? child_.dwProcessId : 0;
  }'''
assert d.count(old) == 1, 'Stop'
d = d.replace(old, new, 1)

# ---------------------------------------------------------------- a bounded wait helper
old2 = '''  void TerminateChild() {
    std::lock_guard<std::mutex> lock(mu_);
    if (child_.hProcess) TerminateProcess(child_.hProcess, 0);
  }'''
new2 = '''  void TerminateChild() {
    std::lock_guard<std::mutex> lock(mu_);
    if (child_.hProcess) TerminateProcess(child_.hProcess, 0);
  }

  /** How long Stop() waits for a child on the path where the supervisor is not there to join. */
  static constexpr DWORD kChildExitBudgetMs = 5000;

  /**
   * Waits for childAlive_ to clear, bounded.
   *
   * Deliberately not a wait on the handle: ownership of that handle belongs to Supervise() alone
   * (Ledger H-01), and this is called on paths where Supervise() may not be running to close it.
   * The flag is what both sides already agree on.
   */
  bool WaitForChildExit(DWORD budgetMs) {
    const DWORD deadline = GetTickCount() + budgetMs;
    while (ChildAlive() && GetTickCount() < deadline) Sleep(50);
    return !ChildAlive();
  }'''
assert d.count(old2) == 1, 'TerminateChild'
d = d.replace(old2, new2, 1)

# ---------------------------------------------------------------- the handoff exit cleans up FIRST
old3 = '''      if (wParam != 0) {
        // The updater holds the lock and has a verified download, so leaving is safe and
        // necessary -- this process is one of the files it is about to replace.
        DestroyWindow(window);
        return 0;
      }'''
new3 = '''      if (wParam != 0) {
        // The updater holds the lock and has a verified download, so leaving is safe and
        // necessary -- this process is one of the files it is about to replace.
        //
        // The child goes first, and whether it went is written down.
        //
        // Leaving it to WM_DESTROY was not wrong so much as unverified: on 2026-09-11 a Stream
        // created a second after this acknowledgement outlived the Host that owned it and kept UDP
        // 43000 and 3478, so the Host the updater started next could not bind and died in a loop.
        // Nothing in the log said a child had been left behind -- the updater only reported that it
        // could not ask some pid to stop. A supervisor that is leaving has one job on the way out,
        // and now it reports whether it did it.
        std::string detail;
        const bool childGone = g.streaming.Stop(&detail);
        append_host_app_log(std::string("[host-app] update: standing down -- ") + detail);
        if (!childGone) {
          // Said plainly, because the consequence lands on the next process rather than this one:
          // whatever is still holding the streaming ports will stop the new build from starting.
          append_host_app_log(
              "[host-app] update: WARNING a streaming child outlived this process; the next host "
              "may fail to bind its ports");
        }
        DestroyWindow(window);
        return 0;
      }'''
assert d.count(old3) == 1, 'ExitNow'
d = d.replace(old3, new3, 1)

# ---------------------------------------------------------------- WM_DESTROY logs too
old4 = '''    case WM_DESTROY:
      KillTimer(window, kStatusTimer);
      g.streaming.Stop();'''
new4 = '''    case WM_DESTROY: {
      KillTimer(window, kStatusTimer);
      // Ordinarily a no-op by now: the handoff path above already stopped it. Still reported,
      // because "already stopped, no child" and "a child is somehow still alive" are different
      // answers and only one of them is fine.
      std::string stopDetail;
      if (!g.streaming.Stop(&stopDetail)) {
        append_host_app_log("[host-app] shutting down -- " + stopDetail);
      }'''
assert d.count(old4) == 1, 'WM_DESTROY'
d = d.replace(old4, new4, 1)

io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok')
