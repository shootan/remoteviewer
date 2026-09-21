#include "update_relaunch.hpp"

#include <windows.h>

#include <tlhelp32.h>

#include <exdisp.h>
#include <shldisp.h>
#include <shlobj.h>
#include <wrl/client.h>

#include <memory>

#include "update_health.hpp"
#include "update_health_log.hpp"
#include "update_readiness.hpp"

namespace remote60::native_poc::update {
namespace {

using Microsoft::WRL::ComPtr;

std::string to_utf8(const std::wstring& text) {
  if (text.empty()) return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                                       nullptr, 0, nullptr, nullptr);
  std::string out(size <= 0 ? 0 : static_cast<size_t>(size), '\0');
  if (size > 0) {
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), size,
                        nullptr, nullptr);
  }
  return out;
}

/** A BSTR that frees itself. The shell interfaces want BSTRs and there are several of them. */
class Bstr {
 public:
  explicit Bstr(const std::wstring& text) : value_(SysAllocString(text.c_str())) {}
  ~Bstr() {
    if (value_) SysFreeString(value_);
  }
  Bstr(const Bstr&) = delete;
  Bstr& operator=(const Bstr&) = delete;
  BSTR get() const { return value_; }
  explicit operator bool() const { return value_ != nullptr; }

 private:
  BSTR value_ = nullptr;
};

/**
 * The shell's automation object, which runs at the ordinary user's integrity level.
 *
 * Reached through the desktop's shell view rather than by creating a Shell.Application directly:
 * an elevated process creating that object gets one running in ITS token, which would defeat the
 * whole purpose. Going through the already-running desktop is what makes the resulting
 * ShellExecute happen as the user.
 */
bool shell_dispatch(ComPtr<IShellDispatch2>* out) {
  ComPtr<IShellWindows> windows;
  if (FAILED(CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&windows)))) {
    return false;
  }

  VARIANT loc{};
  VARIANT empty{};
  VariantInit(&loc);
  VariantInit(&empty);
  long handle = 0;
  ComPtr<IDispatch> dispatch;
  const HRESULT found =
      windows->FindWindowSW(&loc, &empty, SWC_DESKTOP, &handle, SWFO_NEEDDISPATCH, &dispatch);
  VariantClear(&loc);
  VariantClear(&empty);
  if (FAILED(found) || !dispatch) return false;

  ComPtr<IServiceProvider> provider;
  if (FAILED(dispatch.As(&provider))) return false;
  ComPtr<IShellBrowser> browser;
  if (FAILED(provider->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(&browser)))) return false;
  ComPtr<IShellView> view;
  if (FAILED(browser->QueryActiveShellView(&view)) || !view) return false;
  ComPtr<IDispatch> background;
  if (FAILED(view->GetItemObject(SVGIO_BACKGROUND, IID_PPV_ARGS(&background)))) return false;
  ComPtr<IShellFolderViewDual> folderView;
  if (FAILED(background.As(&folderView))) return false;
  ComPtr<IDispatch> application;
  if (FAILED(folderView->get_Application(&application)) || !application) return false;
  return SUCCEEDED(application.As(out));
}


/**
 * Whether `target` is still the process it was when captured.
 *
 * The real answer, used whenever a caller does not supply one -- which in production is always.
 * It used to default to "everything left", so this check existed only in tests: the code that
 * shipped skipped it entirely, and the suites that covered it were covering a configuration
 * nobody ran.
 *
 * A process that cannot be opened is NOT assumed gone. Access can be denied for reasons that have
 * nothing to do with whether it is alive, and reading that as "exited" is what leads to starting
 * a second copy of something that is still running.
 */
/**
 * Whether the machine needs this image, as opposed to a person wanting it.
 *
 * The same rule RelaunchOutcome::required() applies, stated once for the plan so the phase filter
 * and the verdict cannot disagree about what "required" means. If they ever did, an image could
 * be started in the optional phase and then judged by the required rule, or the reverse.
 */
bool required_kind(RelaunchKind kind) {
  return kind == RelaunchKind::ElevatedProcess || kind == RelaunchKind::Service;
}

RelaunchConfig::Liveness real_liveness(const ProcessTarget& target) {
  HANDLE h = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, target.pid);
  if (!h) {
    // The same rule as the stop side, from the same function: one error means the pid is not a
    // process, every other one means the question could not be asked. Liveness::Unknown is not a
    // synonym for Exited here either -- a relaunch decision made on an unanswered question starts
    // a second copy of something already running.
    return (classify_open_error(GetLastError()) == OpenFailure::NotAProcess)
               ? RelaunchConfig::Liveness::Exited
               : RelaunchConfig::Liveness::Unknown;
  }
  const bool signalled = WaitForSingleObject(h, 0) == WAIT_OBJECT_0;
  if (signalled) {
    CloseHandle(h);
    return RelaunchConfig::Liveness::Exited;
  }
  // Alive -- but is it the one that was captured? A pid can be reused, and stopping or skipping a
  // stranger because it inherited a number is exactly what identity checking is for.
  const bool same = process_identity_matches(h, target);
  CloseHandle(h);
  return same ? RelaunchConfig::Liveness::Running : RelaunchConfig::Liveness::Exited;
}

bool wait_for_service_state(SC_HANDLE service, DWORD wanted, uint32_t timeoutMs,
                            std::string* detail) {
  const DWORD deadline = GetTickCount() + timeoutMs;
  for (;;) {
    SERVICE_STATUS_PROCESS status{};
    DWORD needed = 0;
    if (!QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO,
                              reinterpret_cast<LPBYTE>(&status), sizeof(status), &needed)) {
      if (detail) *detail = "could not read the service state";
      return false;
    }
    if (status.dwCurrentState == wanted) return true;
    if (GetTickCount() >= deadline) {
      if (detail) {
        *detail = "the service did not reach the running state (state " +
                  std::to_string(status.dwCurrentState) + ")";
      }
      return false;
    }
    Sleep(200);
  }
}

}  // namespace

namespace {
bool gShellLaunchDisabled = false;
}  // namespace

void set_shell_launch_disabled_for_test(bool disabled) { gShellLaunchDisabled = disabled; }

/**
 * Starts `exePath` as the logged-on user and returns a handle to it.
 *
 * The shell's own process is the reference for "the interactive user": whoever owns the desktop
 * owns that window, so duplicating its token gets the right user, the right session and the right
 * integrity level without this having to work any of that out. CreateProcessWithTokenW then
 * behaves like an ordinary launch -- it returns a PROCESS_INFORMATION -- which is the difference
 * that matters. ShellExecute through the desktop does the same job and hands back nothing.
 *
 * Needs SeImpersonatePrivilege. An elevated process has it; that is the reason this belongs to
 * the updater rather than to something the product does for itself.
 */
bool launch_as_shell_user(const std::wstring& exePath, const std::wstring& workDir,
                          void** processHandleOut, uint32_t* pidOut) {
  if (gShellLaunchDisabled) return false;  // the same switch the tests use for the other route
  // Asked before anything else: a path that is not there cannot be started, and finding that out
  // here keeps it out of the shell fallback, which has no way to fail quietly.
  if (GetFileAttributesW(exePath.c_str()) == INVALID_FILE_ATTRIBUTES) return false;

  HWND shell = GetShellWindow();
  if (!shell) return false;  // no interactive desktop -- nothing to run as
  DWORD shellPid = 0;
  GetWindowThreadProcessId(shell, &shellPid);
  if (!shellPid) return false;

  HANDLE shellProcess = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, shellPid);
  if (!shellProcess) return false;
  HANDLE shellToken = nullptr;
  const BOOL gotToken = OpenProcessToken(shellProcess, TOKEN_DUPLICATE | TOKEN_QUERY, &shellToken);
  CloseHandle(shellProcess);
  if (!gotToken) return false;

  HANDLE primary = nullptr;
  const BOOL duplicated = DuplicateTokenEx(shellToken, MAXIMUM_ALLOWED, nullptr,
                                           SecurityImpersonation, TokenPrimary, &primary);
  CloseHandle(shellToken);
  if (!duplicated) return false;

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  std::wstring commandLine = L"\"" + exePath + L"\"";
  const BOOL started =
      CreateProcessWithTokenW(primary, 0, nullptr, commandLine.data(), 0, nullptr,
                              workDir.empty() ? nullptr : workDir.c_str(), &si, &pi);
  CloseHandle(primary);
  if (!started) return false;

  CloseHandle(pi.hThread);
  if (processHandleOut) *processHandleOut = pi.hProcess;
  else CloseHandle(pi.hProcess);
  if (pidOut) *pidOut = pi.dwProcessId;
  return true;
}

bool launch_via_shell(const std::wstring& exePath, const std::wstring& arguments) {
  // Asked before the shell is involved, because ShellExecute through the desktop has no "do not
  // show UI" option and a path that is not there becomes a modal error dialog -- from an updater,
  // on a machine that may have nobody in front of it, at the moment the product is supposed to be
  // coming back.
  //
  // It does NOT close the hole, and it must not be read as if it did. The shell performs the
  // launch later, in another process; between this check and that moment the file can go. That is
  // not hypothetical -- a test deleted its own fixtures after firing one of these, and the dialog
  // arrived on a real desktop afterwards. What prevents it is the file still being there when the
  // request lands, which is a question about who may delete it and when, not about checking
  // first.
  //
  // FOR THE REST OF THIS ATTEMPT that holds: the optional images start only after the commit, so
  // no rollback follows to move them, and nothing in this attempt removes the installation
  // directory. It is NOT a claim about the file's existence in general. A later update, or an
  // uninstall, may replace or remove it, and a shell request still in flight from this attempt
  // would then find whatever they left. That window is small and nothing here can close it --
  // saying so is better than implying a guarantee this does not have.
  if (GetFileAttributesW(exePath.c_str()) == INVALID_FILE_ATTRIBUTES) return false;
  // The one branch that cannot be arranged by any other means: a machine with no route to the
  // user's context. What must NOT happen there is a fallback to starting it as a child.
  if (gShellLaunchDisabled) return false;

  // The updater's own apartment may or may not be initialised; ask for one and remember whether
  // it was this call that got it, so it is not uninitialised out from under the caller.
  const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  const bool weInitialised = SUCCEEDED(init);

  bool started = false;
  {
    ComPtr<IShellDispatch2> shell;
    if (shell_dispatch(&shell) && shell) {
      Bstr file(exePath);
      VARIANT args{};
      VARIANT dir{};
      VARIANT op{};
      VARIANT show{};
      VariantInit(&args);
      VariantInit(&dir);
      VariantInit(&op);
      VariantInit(&show);
      if (!arguments.empty()) {
        args.vt = VT_BSTR;
        args.bstrVal = SysAllocString(arguments.c_str());
      }
      show.vt = VT_I4;
      show.lVal = SW_SHOWNORMAL;
      if (file) started = SUCCEEDED(shell->ShellExecute(file.get(), args, dir, op, show));
      VariantClear(&args);
      VariantClear(&dir);
      VariantClear(&op);
      VariantClear(&show);
    }
  }

  if (weInitialised) CoUninitialize();
  return started;
}

bool start_service_and_wait(const std::wstring& serviceName, uint32_t timeoutMs,
                            std::string* detail) {
  if (serviceName.empty()) {
    if (detail) *detail = "no service name was configured";
    return false;
  }
  SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
  if (!manager) {
    if (detail) *detail = "could not open the service manager";
    return false;
  }
  SC_HANDLE service =
      OpenServiceW(manager, serviceName.c_str(), SERVICE_START | SERVICE_QUERY_STATUS);
  if (!service) {
    if (detail) *detail = "could not open the service";
    CloseServiceHandle(manager);
    return false;
  }

  bool ok = false;
  SERVICE_STATUS_PROCESS status{};
  DWORD needed = 0;
  if (QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&status),
                           sizeof(status), &needed) &&
      status.dwCurrentState == SERVICE_RUNNING) {
    // Already up. Nothing to do, and reporting failure here would be reporting the wrong thing.
    ok = true;
  } else if (StartServiceW(service, 0, nullptr)) {
    ok = wait_for_service_state(service, SERVICE_RUNNING, timeoutMs, detail);
  } else if (GetLastError() == ERROR_SERVICE_ALREADY_RUNNING) {
    ok = true;
  } else if (detail) {
    *detail = "the service refused to start (error " + std::to_string(GetLastError()) + ")";
  }

  CloseServiceHandle(service);
  CloseServiceHandle(manager);
  return ok;
}

std::string relaunch_user_notice(const std::vector<RelaunchOutcome>& outcomes) {
  std::vector<std::string> missing;
  for (const RelaunchOutcome& outcome : outcomes) {
    if (outcome.failed()) missing.push_back(to_utf8(outcome.imageName));
  }
  if (missing.empty()) return {};

  std::string names;
  for (size_t i = 0; i < missing.size(); ++i) {
    if (i) names += ", ";
    names += missing[i];
  }
  // Says three things, in this order, because that is the order the user needs them: the update
  // worked, this did not come back, here is what to do. Leaving out the first turns a partial
  // success into what looks like a broken installation.
  return "업데이트는 완료되었습니다. 다만 " + names +
         " 을(를) 자동으로 다시 시작하지 못했습니다. 시작 메뉴에서 직접 실행해 주세요.";
}

RelaunchEffects make_relaunch_effects(RelaunchConfig config,
                                      const std::vector<ProcessTarget>& stopped) {
  return make_relaunch_effects(std::move(config), stopped, product_images());
}

RelaunchEffects make_relaunch_effects(RelaunchConfig config,
                                      const std::vector<ProcessTarget>& stopped,
                                      const std::vector<KnownImage>& table) {
  /**
   * Shared by both callbacks. The log mark is the reason they are built together: it has to be
   * taken before anything starts writing, and a health check built separately could not know it.
   */
  struct Shared {
    RelaunchConfig config;
    std::vector<RelaunchEntry> plan;
    uint64_t logMark = 0;
    bool marked = false;
    std::vector<RelaunchOutcome> outcomes;
    std::vector<ProcessTarget> stopped;
    /**
     * Handles for processes this attempt created, by image name.
     *
     * A handle IS ownership: it names one process for as long as it is held, which is precisely
     * what a pid does not do. Kept so a rollback can stop what this attempt started without
     * having to guess which pid is still the right one.
     */
    std::vector<std::pair<std::wstring, HANDLE>> ownedHandles;
    std::string healthDetail;
    /** This attempt's readiness nonce. Never logged; an empty one refuses every claim. */
    std::string attemptNonce;

    /**
     * Every handle this attempt kept is released when the attempt is over.
     *
     * Not housekeeping. An open handle to a process that has ALREADY EXITED keeps the process
     * object alive, and while that object is alive Windows still holds its image file -- so the
     * file cannot be replaced or deleted, and it reports ACCESS_DENIED as if something were still
     * running it. Nothing appears in a process snapshot, because nothing is running; the lock is
     * held by the handle alone.
     *
     * Keeping the handles is what makes ownership provable, so this is the price of that, and it
     * is paid here rather than left to whoever noticed a file that would not move.
     */
    ~Shared() {
      for (const auto& owned : ownedHandles) {
        if (owned.second) CloseHandle(owned.second);
      }
    }
  };
  auto shared = std::make_shared<Shared>();
  shared->config = std::move(config);
  shared->plan = relaunch_plan(stopped, table);
  shared->stopped = stopped;

  RelaunchEffects effects;

  // One body, two entry points. `wantRequired` selects which half of the plan runs, and the two
  // halves never overlap: an entry is required or it is not.
  //
  // Written once rather than twice because everything except the filter -- the liveness check, the
  // ownership rules, the log mark -- must be identical for both. Two copies of this would drift,
  // and the half that drifted would be the one that runs after the commit, where nothing is
  // watching any more.
  const auto run_phase = [shared](bool wantRequired) {
    if (!shared->marked) {
      // Taken once per attempt, before anything is started, so nothing written by a previous run
      // can be read as evidence. The optional phase runs later and must NOT move the mark -- the
      // health check has already been made against this one.
      shared->logMark = file_size_or_zero(shared->config.healthLogPath);
      shared->marked = true;
      // The readiness channel is opened at the same moment and for the same reason: nothing
      // a previous run left behind may be read as this attempt's evidence. The stale claim
      // goes first, so a Host that never writes one cannot inherit an old file.
      {
        namespace up = remote60::native_poc::update;
        up::remove_claim(up::readiness_claim_path(shared->config.healthLogPath));
        shared->attemptNonce = up::mint_attempt_nonce();
        up::AttemptTicket ticket;
        ticket.nonce = shared->attemptNonce;
        ticket.version = shared->config.expectedVersion;
        ticket.valid = !ticket.nonce.empty();
        if (ticket.valid) {
          up::write_attempt_ticket(up::attempt_ticket_path(shared->config.healthLogPath), ticket);
        }
      }
    }
    // Cleared at the START of an attempt, which is the required phase, and appended to by the
    // optional one. So `lastOutcomes` means "everything this attempt tried", not "whatever ran
    // last" -- and a caller that reads it after both phases still sees the host.
    //
    // Clearing per phase looked tidier and quietly dropped half the record: the required outcomes
    // vanished the moment the optional phase ran, so anything reading afterwards saw an empty
    // list and indexed into it. A rollback calls the required phase again, and that is a new
    // attempt against the restored build, so clearing there is right.
    if (wantRequired) shared->outcomes.clear();

    for (const RelaunchEntry& entry : shared->plan) {
      if (required_kind(entry.kind) != wantRequired) continue;
      RelaunchOutcome outcome;
      outcome.imageName = entry.imageName;
      outcome.kind = entry.kind;

      // Still there? Then it never left, and starting it would produce a second instance. This is
      // the ordinary case when an attempt is abandoned after a waiting caller was released: some
      // of the product went and some did not, and having captured an identity says nothing about
      // whether that process has since exited.
      {
        // The supplied check, or the real one. There is no "skip this" branch any more: that
        // branch was what production took, so the check shipped switched off.
        const auto ask = shared->config.isStillRunning
                             ? shared->config.isStillRunning
                             : std::function<RelaunchConfig::Liveness(const ProcessTarget&)>(
                                   real_liveness);
        const auto evaluate = [&](bool* outAlive, bool* outUnknown) {
          *outAlive = false;
          *outUnknown = false;
          for (const ProcessTarget& target : shared->stopped) {
            if (image_leaf_lower(target.imagePath) != image_leaf_lower(entry.imageName)) continue;
            const RelaunchConfig::Liveness answer = ask(target);
            if (answer == RelaunchConfig::Liveness::Running) *outAlive = true;
            if (answer == RelaunchConfig::Liveness::Unknown) *outUnknown = true;
          }
        };

        bool alive = false;
        bool unknown = false;
        evaluate(&alive, &unknown);

        // Everything here was asked to stop, so "still running" is as likely to mean "closing" as
        // "refused". Asked once, nine milliseconds after the acknowledgement, the two are
        // indistinguishable -- and the field run resolved that ambiguity the wrong way, skipped
        // the relaunch, and left the machine with nothing running once the host finished closing.
        //
        // So a Running answer is re-asked until it settles or the grace runs out. Running out is
        // not an exit: it stays Running and the skip below happens for the reason it names.
        if (alive && shared->config.closingGraceMs > 0) {
          const DWORD deadline = GetTickCount() + shared->config.closingGraceMs;
          while (alive && GetTickCount() < deadline) {
            Sleep(50);
            evaluate(&alive, &unknown);
          }
        }

        if (alive) {
          outcome.alreadyRunning = true;
          outcome.detail = "still running -- nothing to bring back";
          shared->outcomes.push_back(outcome);
          continue;
        }
        if (unknown) {
          // Neither started nor excused. Starting it might make two; not starting it might leave
          // the machine down -- so it is reported as a failure and the caller decides, which for
          // a required image means rolling back rather than declaring success.
          outcome.livenessUnknown = true;
          outcome.detail = "could not determine whether it is still running, so it was not started";
          shared->outcomes.push_back(outcome);
          continue;
        }
      }

      if (entry.kind == RelaunchKind::SupervisedByAnother) {
        // Not started, and not a failure. Something else owns its lifetime.
        outcome.skipped = true;
        outcome.detail = entry.reason;
        shared->outcomes.push_back(outcome);
        continue;
      }

      const std::wstring exePath = shared->config.installDir + L"\\" + entry.imageName;
      if (entry.kind == RelaunchKind::Service) {
        std::string detail;
        outcome.started = start_service_and_wait(shared->config.serviceName, 30000, &detail);
        outcome.detail = outcome.started ? "service running" : ("service did not start -- " + detail);
      } else if (entry.kind == RelaunchKind::UserProcess) {
        // Owned, or declared unowned. There is no third option any more.
        //
        // This used to launch through the shell and then work out which process appeared by
        // comparing snapshots taken either side of the call. One new process meant "that is
        // ours". It does not: a user starting the same program in that window produces the same
        // single difference, and the handle opened on the strength of it fixes a STRANGER's
        // identity -- which a rollback would then terminate. The snapshot comparison is gone.
        const auto launch =
            shared->config.launchInUserContext
                ? shared->config.launchInUserContext
                : std::function<bool(const std::wstring&, const std::wstring&, void**, uint32_t*)>(
                      launch_as_shell_user);
        void* handle = nullptr;
        uint32_t pid = 0;
        if (launch(exePath, shared->config.installDir, &handle, &pid)) {
          outcome.started = true;
          outcome.startedPid = pid;
          if (handle) shared->ownedHandles.push_back({entry.imageName, static_cast<HANDLE>(handle)});
          outcome.detail = "started in the user context";
        } else if (launch_via_shell(exePath, L"")) {
          // Running, and unidentified. Reported as such rather than guessed at: the consequence
          // is that this attempt will not stop it and a rollback will not start, which is the
          // correct trade against terminating something that might belong to the user.
          outcome.started = true;
          outcome.ownershipUnknown = true;
          outcome.detail = "started by the shell, which returns no handle -- this attempt cannot "
                           "prove which process is its own, so it will not stop it";
        } else {
          // Not started, and deliberately not retried as a child of this process. Starting it
          // here would give it an administrator token it never had. A safe failure, not a
          // success, so it is recorded as a failure and the user is told.
          outcome.detail = "could not be started in the user context, and was NOT started "
                           "elevated instead";
        }
      } else {
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        std::wstring commandLine = L"\"" + exePath + L"\"";
        if (CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr,
                           shared->config.installDir.c_str(), &si, &pi)) {
          CloseHandle(pi.hThread);
          // The handle is KEPT, not closed. It is the only proof of ownership there is -- while it
          // is held the pid cannot be reused, so "this handle" and "the process we started" stay
          // the same thing. Matching a pid to an image name later is not the same claim.
          shared->ownedHandles.push_back({entry.imageName, pi.hProcess});
          outcome.started = true;
          outcome.startedPid = pi.dwProcessId;
          outcome.detail = "started";
        } else {
          outcome.detail = "could not be started (error " + std::to_string(GetLastError()) + ")";
        }
      }

      shared->outcomes.push_back(outcome);
    }

    // Folded into a verdict rather than a bool, because a missing client and a missing host call
    // for different responses: one is an inconvenience, the other leaves a remote user with no
    // way into the machine.
    RelaunchVerdict verdict = RelaunchVerdict::AllBack;
    for (const RelaunchOutcome& outcome : shared->outcomes) {
      if (!outcome.failed()) continue;
      if (outcome.required()) return RelaunchVerdict::RequiredMissing;  // decides on its own
      verdict = RelaunchVerdict::OptionalMissing;
    }
    return verdict;
  };

  effects.relaunchRequired = [run_phase]() { return run_phase(true); };
  effects.relaunchOptional = [run_phase]() { return run_phase(false); };

  effects.healthCheck = [shared]() {
    // The readiness channel, tried first. It answers the question the log line only implies: is
    // THIS process, of THIS build, started by THIS attempt, ready now. A log line has nothing in
    // it that says which attempt it belonged to.
    //
    // The log contract below is kept and is still what decides when the channel says nothing. An
    // updater that predates the channel reads only the log, and that is the updater performing the
    // upgrade that installs a Host which can write the channel -- so the log path has to keep
    // working, not merely survive.
    const auto claim_verdict = [shared](std::string* detail) {
      namespace up = remote60::native_poc::update;
      if (shared->attemptNonce.empty()) return false;
      const up::ReadinessClaim claim =
          up::read_claim(up::readiness_claim_path(shared->config.healthLogPath));
      up::ReadinessExpectation expect;
      expect.nonce = shared->attemptNonce;
      expect.version = shared->config.expectedVersion;
      expect.nowMs = static_cast<uint64_t>(GetTickCount64());
      expect.maxAgeMs = shared->config.healthTimeoutMs;
      expect.processAlive = false;
      for (const auto& owned : shared->ownedHandles) {
        if (!owned.second) continue;
        FILETIME created{}, exited{}, kernel{}, user{};
        if (!GetProcessTimes(owned.second, &created, &exited, &kernel, &user)) continue;
        ULARGE_INTEGER qw{};
        qw.LowPart = created.dwLowDateTime;
        qw.HighPart = created.dwHighDateTime;
        if (qw.QuadPart != claim.createTimeQw) continue;
        expect.pid = claim.pid;
        expect.createTimeQw = qw.QuadPart;
        expect.processAlive = WaitForSingleObject(owned.second, 0) != WAIT_OBJECT_0;
        break;
      }
      const up::ReadinessVerdict verdict = up::readiness_judge(claim, expect);
      if (detail) *detail = up::readiness_verdict_name(verdict);
      return verdict == up::ReadinessVerdict::Accept;
    };

    // Nothing that reports was brought back, so there is nothing to wait for. This is not a
    // shortcut: waiting anyway would time out and roll back an update whose files are correct,
    // for the sole reason that the process which writes the report was not running beforehand.
    if (!shared->config.healthReporterImage.empty()) {
      bool reporterStarted = false;
      for (const RelaunchOutcome& outcome : shared->outcomes) {
        if (outcome.imageName == shared->config.healthReporterImage && outcome.started) {
          reporterStarted = true;
        }
      }
      if (!reporterStarted) {
        shared->healthDetail = "nothing that reports health was relaunched, so there is no "
                               "report to wait for";
        return true;
      }
    }
    if (!shared->marked) {
      // Nothing was relaunched, so there is nothing that could have reported. Saying "healthy"
      // here would be answering a question that was never asked.
      shared->healthDetail = "no relaunch was attempted, so there is no report to wait for";
      return false;
    }
    const DWORD deadline = GetTickCount() + shared->config.healthTimeoutMs;
    for (;;) {
      std::string claimDetail;
      if (claim_verdict(&claimDetail)) {
        shared->healthDetail = "Healthy: readiness claim accepted for version " +
                               shared->config.expectedVersion;
        return true;
      }
      const std::string fresh =
          read_from_offset(shared->config.healthLogPath, shared->logMark);
      const HealthSignals signals = scan_health_report(fresh);
      std::string detail;
      const HealthVerdict verdict =
          judge_health(signals, shared->config.expectedVersion, &detail);
      shared->healthDetail = std::string(health_verdict_name(verdict)) + ": " + detail +
                             " (readiness channel: " + claimDetail + ")";
      if (verdict == HealthVerdict::Healthy) return true;
      // Waiting longer cannot turn the old version into the new one.
      if (verdict == HealthVerdict::WrongVersion) return false;
      if (GetTickCount() >= deadline) {
        shared->healthDetail = "timed out -- " + detail;
        return false;
      }
      Sleep(shared->config.healthPollMs);
    }
  };

  effects.stopStarted = [shared]() {
    RelaunchEffects::StopReport report;
    // Only processes this attempt created, identified by the handle it was given at creation.
    for (auto& owned : shared->ownedHandles) {
      if (!owned.second) continue;
      if (WaitForSingleObject(owned.second, 0) == WAIT_OBJECT_0) {
        // Already exited on its own.
        CloseHandle(owned.second);
        owned.second = nullptr;
        continue;
      }
      if (TerminateProcess(owned.second, 0) &&
          WaitForSingleObject(owned.second, 5000) == WAIT_OBJECT_0) {
        ++report.stopped;
        CloseHandle(owned.second);
        owned.second = nullptr;
        continue;
      }
      // Asked and it did not go. Reported, not assumed away -- a rollback that moved files now
      // would be moving files something is still holding.
      report.unstoppable.push_back(owned.first);
    }

    // Anything started through the shell that could NOT be tied to one process. The shell creates
    // it, so there is no handle to inherit; a handle is only claimed when exactly one process
    // running exactly that image appeared between the before-snapshot and the after-snapshot. When
    // that correlation did not hold -- nothing appeared, or several did -- there is no identity to
    // act on, and killing by name would mean killing whatever else answers to it.
    for (const RelaunchOutcome& outcome : shared->outcomes) {
      if (outcome.kind != RelaunchKind::UserProcess || !outcome.started) continue;
      bool owned = false;
      for (const auto& pair : shared->ownedHandles) {
        if (pair.first == outcome.imageName) owned = true;
      }
      if (!owned) report.unstoppable.push_back(outcome.imageName);
    }
    return report;
  };

  effects.lastOutcomes = [shared]() { return shared->outcomes; };
  effects.userNotice = [shared]() { return relaunch_user_notice(shared->outcomes); };
  effects.lastHealthDetail = [shared]() { return shared->healthDetail; };
  return effects;
}

}  // namespace remote60::native_poc::update
