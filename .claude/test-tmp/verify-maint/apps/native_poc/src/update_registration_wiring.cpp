#include "update_registration_wiring.hpp"

#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <objbase.h>

#include <cstdio>
#include <string>
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

/** "0x800401f0" -- the number the caller needs when a shell call fails for no visible reason. */
std::string hresult_hex(HRESULT hr) {
  char buf[16]{};
  std::snprintf(buf, sizeof(buf), "0x%08lx", static_cast<unsigned long>(hr));
  return buf;
}

/**
 * COM for the length of one scope, without taking the caller's apartment away.
 *
 * The updater had none of this on the registration path, and CoCreateInstance(CLSID_ShellLink)
 * therefore failed with CO_E_NOTINITIALIZED -- reported only as "registration failed". The
 * installer initialises COM in its own main (installer_main.cpp), and when this code was extracted
 * to be shared the caller's precondition did not come with it.
 *
 * The pattern is the one update_relaunch.cpp already uses correctly: ask for an apartment, and
 * remember whether THIS call is the one that got it. RPC_E_CHANGED_MODE means another apartment
 * model is already live on this thread -- COM is perfectly usable, we simply must not uninitialise
 * it, so it is a failure code that is deliberately carried on past.
 */
struct ApartmentScope {
  HRESULT hr = S_OK;
  bool mine = false;

  ApartmentScope() {
    hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    mine = SUCCEEDED(hr);  // includes S_FALSE: already initialised, and still ours to release
  }
  ~ApartmentScope() {
    if (mine) CoUninitialize();
  }
  ApartmentScope(const ApartmentScope&) = delete;
  ApartmentScope& operator=(const ApartmentScope&) = delete;

  bool usable() const { return SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE; }
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

/**
 * Writes one shell link.
 *
 * `folder` is where it goes; empty means the machine's Start menu. It is a parameter so that a
 * test can drive this exact function -- the real COM calls, the real IPersistFile::Save -- against
 * a directory of its own, instead of writing into every user's Start menu to prove a point.
 */
bool create_start_menu_shortcut(const std::wstring& target, const std::wstring& linkName,
                                const std::wstring& description, const std::wstring& folder,
                                std::string* detail) {
  const auto say = [detail](const std::string& what) {
    if (detail) *detail = what;
    return false;
  };

  // Must outlive every COM call below, including the IPersistFile release.
  ApartmentScope com;
  if (!com.usable()) {
    return say("CoInitializeEx failed " + hresult_hex(com.hr));
  }
  std::wstring linkPath;
  if (folder.empty()) {
    PWSTR wide = nullptr;
    const HRESULT known =
        SHGetKnownFolderPath(FOLDERID_CommonPrograms, KF_FLAG_CREATE, nullptr, &wide);
    if (FAILED(known)) return say("SHGetKnownFolderPath failed " + hresult_hex(known));
    linkPath = wide;
    CoTaskMemFree(wide);
  } else {
    linkPath = folder;
  }
  linkPath += L"\\" + linkName + L".lnk";

  IShellLinkW* link = nullptr;
  const HRESULT created = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                           IID_IShellLinkW, reinterpret_cast<void**>(&link));
  if (FAILED(created)) {
    // The field failure lands here as 0x800401f0 (CO_E_NOTINITIALIZED) when the apartment is
    // missing. Named rather than summarised, because "could not create the shortcut" was what
    // made the cause invisible for a whole release.
    return say("CoCreateInstance(CLSID_ShellLink) failed " + hresult_hex(created));
  }
  HRESULT last = S_OK;
  bool ok = false;
  last = link->SetPath(target.c_str());
  if (SUCCEEDED(last)) {
    const size_t slash = target.find_last_of(L'\\');
    if (slash != std::wstring::npos) {
      (void)link->SetWorkingDirectory(target.substr(0, slash).c_str());
    }
    (void)link->SetDescription(description.c_str());
    IPersistFile* persist = nullptr;
    last = link->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&persist));
    if (SUCCEEDED(last)) {
      last = persist->Save(linkPath.c_str(), TRUE);
      ok = SUCCEEDED(last);
      persist->Release();
    }
  }
  link->Release();
  if (!ok && detail) *detail = "IShellLink/IPersistFile failed " + hresult_hex(last);
  return ok;
}

}  // namespace

install::RegistrationOps production_registration_ops(const std::wstring& shortcutFolder) {
  install::RegistrationOps ops;
  ops.runProcess = [](const std::wstring& exe, const std::wstring& args) {
    return run_and_wait(exe, args);
  };
  ops.createShortcut = [shortcutFolder](const std::wstring& target, const std::wstring& link,
                                        const std::wstring& description, std::string* detail) {
    return create_start_menu_shortcut(target, link, description, shortcutFolder, detail);
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
