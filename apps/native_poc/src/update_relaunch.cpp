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
 * Pids currently running the image at `path`.
 *
 * Needed because a shell-routed launch reports no pid: the shell starts the process, not us, so
 * ShellExecute has nothing to hand back. Without this the client this attempt started could never
 * be stopped -- and a rollback that has to move the client's file would fail on it, holding open
 * by a process the rollback itself had launched.
 *
 * Compared on the full path, so a same-named program somewhere else is not ours.
 */
std::vector<DWORD> pids_running_image(const std::wstring& path) {
  std::vector<DWORD> pids;
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) return pids;
  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  if (Process32FirstW(snapshot, &entry)) {
    do {
      HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
      if (!h) continue;
      wchar_t image[MAX_PATH]{};
      DWORD size = MAX_PATH;
      if (QueryFullProcessImageNameW(h, 0, image, &size) && _wcsicmp(image, path.c_str()) == 0) {
        pids.push_back(entry.th32ProcessID);
      }
      CloseHandle(h);
    } while (Process32NextW(snapshot, &entry));
  }
  CloseHandle(snapshot);
  return pids;
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
RelaunchConfig::Liveness real_liveness(const ProcessTarget& target) {
  HANDLE h = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, target.pid);
  if (!h) {
    // ERROR_INVALID_PARAMETER is what a pid that no longer exists gives; anything else means the
    // question could not be asked, which is a different answer.
    const DWORD err = GetLastError();
    return (err == ERROR_INVALID_PARAMETER) ? RelaunchConfig::Liveness::Exited
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

bool launch_via_shell(const std::wstring& exePath, const std::wstring& arguments) {
  // Asked before the shell is involved. ShellExecute through the desktop has no "do not show UI"
  // option, so handing it a path that is not there puts a modal error dialog on the desktop --
  // from an updater, on a machine that may have nobody in front of it, at the moment the product
  // is supposed to be coming back. Answering the question ourselves avoids that entirely.
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

  effects.relaunch = [shared]() {
    shared->outcomes.clear();
    // Before anything is started, so nothing written by a previous run can be read as evidence.
    shared->logMark = file_size_or_zero(shared->config.healthLogPath);
    shared->marked = true;

    for (const RelaunchEntry& entry : shared->plan) {
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
        bool alive = false;
        bool unknown = false;
        for (const ProcessTarget& target : shared->stopped) {
          if (image_leaf_lower(target.imagePath) != image_leaf_lower(entry.imageName)) continue;
          const RelaunchConfig::Liveness answer = ask(target);
          if (answer == RelaunchConfig::Liveness::Running) alive = true;
          if (answer == RelaunchConfig::Liveness::Unknown) unknown = true;
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
        // Taken before, so the one that appears afterwards can be told from any that were
        // already there. The shell starts it, so there is no pid to be handed back.
        const std::vector<DWORD> before = pids_running_image(exePath);
        outcome.started = launch_via_shell(exePath, L"");
        if (outcome.started) {
          // Briefly: the shell hands off asynchronously, so the process may not exist yet.
          std::vector<DWORD> appeared;
          for (int attempt = 0; attempt < 20 && appeared.empty(); ++attempt) {
            for (DWORD pid : pids_running_image(exePath)) {
              if (std::find(before.begin(), before.end(), pid) == before.end()) {
                appeared.push_back(pid);
              }
            }
            if (appeared.empty()) Sleep(50);
          }
          // ONE new process running exactly this image, appearing between the snapshot and now.
          // That is per-launch correlation, and it is the only basis on which this claims to own
          // something the shell created. Two would mean the user started one at the same moment,
          // and a rollback that terminated on a guess would be terminating a stranger's window --
          // so ambiguity is left unowned, which later reads as "could not be stopped" and stops
          // the rollback rather than proceeding on an assumption.
          if (appeared.size() == 1) {
            outcome.startedPid = appeared[0];
            HANDLE h = OpenProcess(
                SYNCHRONIZE | PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                appeared[0]);
            if (h) {
              // Checked again through the handle. Between the snapshot and the open the pid could
              // have been reused, and from here on the handle -- not the number -- is the
              // identity, so this is the last moment the check can be made at all.
              wchar_t image[MAX_PATH]{};
              DWORD size = MAX_PATH;
              if (QueryFullProcessImageNameW(h, 0, image, &size) &&
                  _wcsicmp(image, exePath.c_str()) == 0) {
                shared->ownedHandles.push_back({entry.imageName, h});
              } else {
                CloseHandle(h);
              }
            }
          } else if (appeared.size() > 1) {
            outcome.detail = "started, but more than one appeared -- not claimed as ours";
          }
        }
        // Not started, and deliberately not retried as a child of this process. Starting it here
        // would give it an administrator token it never had. That is a safe failure, not a
        // success, so it is recorded as a failure and the user is told.
        if (outcome.detail.empty()) {
          outcome.detail = outcome.started
                               ? "started in the user context"
                               : "could not be started in the user context, and was NOT started "
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

  effects.healthCheck = [shared]() {
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
      const std::string fresh =
          read_from_offset(shared->config.healthLogPath, shared->logMark);
      const HealthSignals signals = scan_health_report(fresh);
      std::string detail;
      const HealthVerdict verdict =
          judge_health(signals, shared->config.expectedVersion, &detail);
      shared->healthDetail = std::string(health_verdict_name(verdict)) + ": " + detail;
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
