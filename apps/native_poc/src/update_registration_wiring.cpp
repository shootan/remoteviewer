#include "update_registration_wiring.hpp"

#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <objbase.h>

#include <vector>

namespace remote60::native_poc::update {
namespace {

/** Shared between the three callbacks, which is why they are built together. */
struct RegistrationState {
  install::RegistrationTarget target;
  install::RegistrationOps ops;
  install::RegistrationSnapshot before;
  bool captured = false;
  install::RegistrationResult lastResult;
};

int run_and_wait(const std::wstring& application, const std::wstring& arguments) {
  std::wstring command = L"\"" + application + L"\" " + arguments;
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESHOWWINDOW;
  startup.wShowWindow = SW_HIDE;
  PROCESS_INFORMATION process{};
  std::vector<wchar_t> mutableCommand(command.begin(), command.end());
  mutableCommand.push_back(L'\0');
  if (!CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                      nullptr, nullptr, &startup, &process)) {
    return -1;
  }
  WaitForSingleObject(process.hProcess, 30000);
  DWORD exitCode = 0;
  (void)GetExitCodeProcess(process.hProcess, &exitCode);
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  return static_cast<int>(exitCode);
}

bool create_start_menu_shortcut(const std::wstring& target, const std::wstring& linkName,
                                const std::wstring& description) {
  PWSTR wide = nullptr;
  if (FAILED(SHGetKnownFolderPath(FOLDERID_CommonPrograms, KF_FLAG_CREATE, nullptr, &wide))) {
    return false;
  }
  std::wstring linkPath(wide);
  CoTaskMemFree(wide);
  linkPath += L"\\" + linkName + L".lnk";

  IShellLinkW* link = nullptr;
  if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW,
                              reinterpret_cast<void**>(&link)))) {
    return false;
  }
  bool ok = false;
  if (SUCCEEDED(link->SetPath(target.c_str()))) {
    const size_t slash = target.find_last_of(L'\\');
    if (slash != std::wstring::npos) {
      (void)link->SetWorkingDirectory(target.substr(0, slash).c_str());
    }
    (void)link->SetDescription(description.c_str());
    IPersistFile* persist = nullptr;
    if (SUCCEEDED(link->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&persist)))) {
      ok = SUCCEEDED(persist->Save(linkPath.c_str(), TRUE));
      persist->Release();
    }
  }
  link->Release();
  return ok;
}

}  // namespace

install::RegistrationOps production_registration_ops() {
  install::RegistrationOps ops;
  ops.runProcess = [](const std::wstring& exe, const std::wstring& args) {
    return run_and_wait(exe, args);
  };
  ops.createShortcut = [](const std::wstring& target, const std::wstring& link,
                          const std::wstring& description) {
    return create_start_menu_shortcut(target, link, description);
  };
  return ops;
}

RegistrationEffects make_registration_effects(install::RegistrationTarget target,
                                              install::RegistrationOps ops) {
  auto state = std::make_shared<RegistrationState>();
  state->target = std::move(target);
  state->ops = std::move(ops);

  RegistrationEffects effects;

  effects.capture = [state]() {
    // Taken BEFORE the swap. What is on disk after a rollback is the previous build, so what the
    // registry says afterwards has to be the previous version too.
    state->captured = install::capture_registration(state->target.uninstallRoot,
                                                    state->target.uninstallSubkey, &state->before);
    return state->captured;
  };

  effects.apply = [state]() {
    state->lastResult = install::register_install(state->target, state->ops);
    return state->lastResult.ok;
  };

  effects.restore = [state]() {
    if (!state->captured) {
      // Nothing was captured, so there is nothing to put back. Reported as success because it is
      // a true statement about the world, not a failure to do something that was asked for --
      // and treating it as a failure would turn an ordinary rollback into RollbackFailed, which
      // is reserved for the case where the install may actually be inconsistent.
      return true;
    }
    return install::restore_registration(state->target.uninstallRoot,
                                         state->target.uninstallSubkey, state->before);
  };

  effects.lastResult = [state]() { return state->lastResult; };
  return effects;
}

}  // namespace remote60::native_poc::update
