#include "update_relaunch.hpp"

#include <windows.h>

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
    std::string healthDetail;
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
      if (shared->config.isStillRunning) {
        bool alive = false;
        for (const ProcessTarget& target : shared->stopped) {
          if (image_leaf_lower(target.imagePath) != image_leaf_lower(entry.imageName)) continue;
          if (shared->config.isStillRunning(target)) alive = true;
        }
        if (alive) {
          outcome.alreadyRunning = true;
          outcome.detail = "still running -- nothing to bring back";
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
        outcome.started = launch_via_shell(exePath, L"");
        // Not started, and deliberately not retried as a child of this process. Starting it here
        // would give it an administrator token it never had. That is a safe failure, not a
        // success, so it is recorded as a failure and the user is told.
        outcome.detail = outcome.started
                             ? "started in the user context"
                             : "could not be started in the user context, and was NOT started "
                               "elevated instead";
      } else {
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        std::wstring commandLine = L"\"" + exePath + L"\"";
        if (CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr,
                           shared->config.installDir.c_str(), &si, &pi)) {
          CloseHandle(pi.hThread);
          CloseHandle(pi.hProcess);
          outcome.started = true;
          // Remembered so a rollback can stop what this attempt started before it tries to move
          // the files those processes are holding open.
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
    // Only what THIS attempt started, identified by the pid it was handed at creation and checked
    // against the image before anything is done to it. Nothing else on the machine is this
    // function's business, and a pid alone is not an identity.
    int stopped = 0;
    for (RelaunchOutcome& outcome : shared->outcomes) {
      if (!outcome.started || outcome.startedPid == 0) continue;
      HANDLE h = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
                             FALSE, outcome.startedPid);
      if (!h) {
        // Cannot be opened: already gone, and there is nothing left to do about it.
        outcome.startedPid = 0;
        continue;
      }
      wchar_t image[MAX_PATH]{};
      DWORD size = MAX_PATH;
      const bool named = QueryFullProcessImageNameW(h, 0, image, &size) != FALSE;
      if (named && image_leaf_lower(image) == image_leaf_lower(outcome.imageName)) {
        // Counted only when it actually ended. Reporting a stop for something still holding the
        // files open would send a rollback into exactly the failure this call exists to prevent,
        // and it would look like the rollback's fault.
        if (TerminateProcess(h, 0) && WaitForSingleObject(h, 5000) == WAIT_OBJECT_0) {
          ++stopped;
          // Forgotten, so a second call does not count it again -- "nothing left to stop" is the
          // answer then, and it should be visible as one.
          outcome.startedPid = 0;
        }
      }
      CloseHandle(h);
    }
    return stopped;
  };
  effects.lastOutcomes = [shared]() { return shared->outcomes; };
  effects.userNotice = [shared]() { return relaunch_user_notice(shared->outcomes); };
  effects.lastHealthDetail = [shared]() { return shared->healthDetail; };
  return effects;
}

}  // namespace remote60::native_poc::update
