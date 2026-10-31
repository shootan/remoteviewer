# -*- coding: utf-8 -*-
import io

def edit(path, pairs):
    d = io.open(path, encoding='utf-8').read()
    for i, (old, new) in enumerate(pairs):
        assert d.count(old) == 1, '%s pair %d: %s' % (path, i, old[:70])
        d = d.replace(old, new, 1)
    io.open(path, 'w', encoding='utf-8', newline='').write(d)
    print('ok', path)


# ---------------------------------------------------------------- createShortcut can say why
edit('apps/native_poc/src/install_registration.hpp', [
 ('''  /** target, link name, description. */
  std::function<bool(const std::wstring&, const std::wstring&, const std::wstring&)> createShortcut;''',
  '''  /**
   * target, link name, description, and where the failure was when it fails.
   *
   * `detail` may be null. It exists because the only thing the caller could previously report was
   * "could not create the host shortcut" -- true, and indistinguishable from every other reason a
   * shell link does not get written. The one that actually happened in the field was COM not being
   * initialised, and that is an HRESULT nobody could see.
   */
  std::function<bool(const std::wstring&, const std::wstring&, const std::wstring&,
                     std::string* detail)>
      createShortcut;'''),
])

edit('apps/native_poc/src/install_registration.cpp', [
 ('''    if (!ops.createShortcut(target.installDir + L"\\\\" + target.hostExeName, target.productName,
                            L"GNLink remote desktop host")) {
      return fail(RegistrationStep::Shortcuts, "could not create the host shortcut");
    }
    if (!ops.createShortcut(target.installDir + L"\\\\" + target.clientExeName,
                            target.clientShortcutName, L"GNLink - connect to another PC")) {
      return fail(RegistrationStep::Shortcuts, "could not create the client shortcut");
    }''',
  '''    std::string why;
    if (!ops.createShortcut(target.installDir + L"\\\\" + target.hostExeName, target.productName,
                            L"GNLink remote desktop host", &why)) {
      return fail(RegistrationStep::Shortcuts,
                  "could not create the host shortcut" + (why.empty() ? "" : ": " + why));
    }
    why.clear();
    if (!ops.createShortcut(target.installDir + L"\\\\" + target.clientExeName,
                            target.clientShortcutName, L"GNLink - connect to another PC", &why)) {
      return fail(RegistrationStep::Shortcuts,
                  "could not create the client shortcut" + (why.empty() ? "" : ": " + why));
    }'''),
])

# ---------------------------------------------------------------- the wiring: COM + injection
edit('apps/native_poc/src/update_registration_wiring.cpp', [
 ('''int run_and_wait(const std::wstring& application, const std::wstring& arguments) {''',
  '''/** "0x800401f0" -- the number the caller needs when a shell call fails for no visible reason. */
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

int run_and_wait(const std::wstring& application, const std::wstring& arguments) {'''),
 ('''bool create_start_menu_shortcut(const std::wstring& target, const std::wstring& linkName,
                                const std::wstring& description) {''',
  '''/**
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
  }'''),
 ('''  PWSTR wide = nullptr;
  if (FAILED(SHGetKnownFolderPath(FOLDERID_CommonPrograms, KF_FLAG_CREATE, nullptr, &wide))) {
    return false;
  }
  std::wstring linkPath(wide);
  CoTaskMemFree(wide);
  linkPath += L"\\\\" + linkName + L".lnk";

  IShellLinkW* link = nullptr;
  if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW,
                              reinterpret_cast<void**>(&link)))) {
    return false;
  }
  bool ok = false;
  if (SUCCEEDED(link->SetPath(target.c_str()))) {
    const size_t slash = target.find_last_of(L'\\\\');
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
}''',
  '''  std::wstring linkPath;
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
  linkPath += L"\\\\" + linkName + L".lnk";

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
    const size_t slash = target.find_last_of(L'\\\\');
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
}'''),
 ('''install::RegistrationOps production_registration_ops() {
  install::RegistrationOps ops;
  ops.runProcess = [](const std::wstring& exe, const std::wstring& args) {
    return run_and_wait(exe, args);
  };
  ops.createShortcut = [](const std::wstring& target, const std::wstring& link,
                          const std::wstring& description) {
    return create_start_menu_shortcut(target, link, description);
  };
  return ops;
}''',
  '''install::RegistrationOps production_registration_ops(const std::wstring& shortcutFolder) {
  install::RegistrationOps ops;
  ops.runProcess = [](const std::wstring& exe, const std::wstring& args) {
    return run_and_wait(exe, args);
  };
  ops.createShortcut = [shortcutFolder](const std::wstring& target, const std::wstring& link,
                                        const std::wstring& description, std::string* detail) {
    return create_start_menu_shortcut(target, link, description, shortcutFolder, detail);
  };
  return ops;
}'''),
])

edit('apps/native_poc/src/update_registration_wiring.hpp', [
 ('''/** The operations a real installation needs: a process runner and a Start menu shortcut writer. */
install::RegistrationOps production_registration_ops();''',
  '''/**
 * The operations a real installation needs: a process runner and a Start menu shortcut writer.
 *
 * `shortcutFolder` empty means the machine's Start menu, which is what production passes. A test
 * passes a directory of its own so that it can drive these exact functions -- real COM, real
 * IPersistFile::Save -- without writing into every user's Start menu.
 */
install::RegistrationOps production_registration_ops(const std::wstring& shortcutFolder = {});'''),
])
