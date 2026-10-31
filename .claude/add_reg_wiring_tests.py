import io

p = 'apps/native_poc/src/update_effects_test.cpp'
d = io.open(p, encoding='utf-8').read()

# base_config: the two new required seams
old = """  c.registerInstall = []() { return true; };"""
new = """  c.captureRegistration = []() { return true; };
  c.registerInstall = []() { return true; };
  c.restoreRegistration = []() { return true; };"""
assert old in d, 'base_config anchor'
d = d.replace(old, new, 1)

# missing-field table
old = """        {"registryRoot", [](UpdateEffectsConfig& x) { x.registryRoot.clear(); }},"""
new = """        {"captureRegistration", [](UpdateEffectsConfig& x) { x.captureRegistration = nullptr; }},
        {"restoreRegistration", [](UpdateEffectsConfig& x) { x.restoreRegistration = nullptr; }},
        {"registryRoot", [](UpdateEffectsConfig& x) { x.registryRoot.clear(); }},"""
assert old in d, 'missing table anchor'
d = d.replace(old, new, 1)

# new section before cleanup
old = """  remove_tree(staging);
  remove_tree(install);"""
new = '''  // ---------------------------------------------------------------- registration through the shared unit

  {
    // The wiring, driven against a scratch HKCU key. What matters is the ORDER: the snapshot has
    // to be taken before a single file moves, because a rollback restores what was registered
    // then -- not the version being abandoned.
    const std::wstring subkey =
        L"Software\\\\GNLinkUpdateWiringTest\\\\" + std::to_wstring(GetCurrentProcessId());
    RegDeleteTreeW(HKEY_CURRENT_USER, subkey.c_str());
    check("wiring test key is not the real uninstall path",
          subkey.find(L"Microsoft\\\\Windows\\\\CurrentVersion\\\\Uninstall") == std::wstring::npos);

    const auto make_target = [&](const std::wstring& version) {
      install::RegistrationTarget t;
      t.installDir = install;
      t.setupPath = install + L"\\\\GNLinkSetup.exe";
      t.version = version;
      t.productName = L"GNLink Host (wiring test)";
      t.clientShortcutName = L"GNLink (wiring test)";
      t.publisher = L"GNLink";
      t.serviceName = L"GNLinkWiringTestService";
      t.firewallRuleName = L"GNLink Wiring Test";
      t.uninstallRoot = HKEY_CURRENT_USER;
      t.uninstallSubkey = subkey;
      t.hostExeName = L"GNLinkHost.exe";
      t.clientExeName = L"GNLinkClient.exe";
      t.serviceExeName = L"GNLinkInputService.exe";
      t.streamExeName = L"GNLinkStream.exe";
      return t;
    };
    // Recorded, not performed: no service, no firewall rule, no Start menu entry.
    install::RegistrationOps recordingOps;
    recordingOps.runProcess = [](const std::wstring&, const std::wstring&) { return 0; };
    recordingOps.createShortcut = [](const std::wstring&, const std::wstring&,
                                     const std::wstring&) { return true; };

    const auto read_version = [&]() -> std::wstring {
      HKEY key = nullptr;
      if (RegOpenKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return L"<none>";
      }
      wchar_t buffer[256]{};
      DWORD bytes = sizeof(buffer);
      DWORD type = 0;
      const LSTATUS s = RegQueryValueExW(key, L"DisplayVersion", nullptr, &type,
                                         reinterpret_cast<BYTE*>(buffer), &bytes);
      RegCloseKey(key);
      return (s == ERROR_SUCCESS) ? std::wstring(buffer) : L"<none>";
    };

    {
      // Seed a previous installation, the way an earlier version would have left it.
      RegistrationEffects seed = make_registration_effects(make_target(L"0.2.104"), recordingOps);
      check("seed registration succeeds", seed.apply());
      check("seed version recorded", read_version() == L"0.2.104", narrow_w(read_version()));
    }

    {
      // A successful update: capture, swap, register the NEW version.
      seed_install();
      UpdateEffectsConfig c = base_config(install, staging);
      RegistrationEffects reg = make_registration_effects(make_target(L"0.2.105"), recordingOps);
      c.captureRegistration = reg.capture;
      c.registerInstall = reg.apply;
      c.restoreRegistration = reg.restore;

      WindowsUpdateEffects e(c);
      e.Download(artifact_fields());
      check("wiring: swap succeeds", e.Swap(), e.last_error());
      check("wiring: register succeeds", e.RegisterInstall(), e.last_error());
      check("wiring: DisplayVersion is the new version", read_version() == L"0.2.105",
            narrow_w(read_version()));
      check("wiring: the shared unit reported all four steps",
            reg.lastResult().completed.size() == 4,
            std::to_string(reg.lastResult().completed.size()));
      e.DiscardDownload();
    }

    {
      // The case Codex called out: a failure AFTER the swap must put the registry back to the
      // PREVIOUS version. Restoring the new one would leave old files claiming to be new.
      seed_install();
      UpdateEffectsConfig c = base_config(install, staging);
      RegistrationEffects reg = make_registration_effects(make_target(L"0.3.0"), recordingOps);
      c.captureRegistration = reg.capture;
      c.registerInstall = reg.apply;
      c.restoreRegistration = reg.restore;
      c.relaunch = []() { return true; };
      c.healthCheck = []() { return false; };  // fails after registration

      WindowsUpdateEffects e(c);
      e.set_installed_version("0.2.105");
      e.set_manifest("schema=1\\nplatform=windows\\nversion=0.3.0\\nartifact=test.bin\\nsize=" +
                         std::to_string(artifact_fields().size) + "\\nsha256=" + gArtifactSha + "\\n",
                     std::string(128, '0'));
      const auto accept = [](const std::string&, const std::vector<uint8_t>&) { return true; };
      const UpdateOutcome out = run_update(e, accept, "windows");
      check("wiring: a health failure rolls back", out.result == UpdateResult::RolledBack,
            std::string(result_name(out.result)) + " " + out.detail);
      check("wiring: files went back to the old version",
            read_text(install + L"\\\\AlphaPayload.bin") == kOldAlpha);
      check("wiring: DisplayVersion went back to the PREVIOUS version, not 0.3.0",
            read_version() == L"0.2.105", narrow_w(read_version()));
    }

    {
      // Capture failing must stop the swap before anything moves.
      seed_install();
      UpdateEffectsConfig c = base_config(install, staging);
      c.captureRegistration = []() { return false; };
      WindowsUpdateEffects e(c);
      e.Download(artifact_fields());
      check("a failed capture stops the swap", !e.Swap(), e.last_error());
      check("and nothing was moved", read_text(install + L"\\\\AlphaPayload.bin") == kOldAlpha);
      check("no backups were made", !exists(install + L"\\\\AlphaPayload.bin.gnlink-old"));
      e.DiscardDownload();
    }

    {
      // Restoring without having captured is a true "nothing to put back", not a failure -- and
      // must not turn an ordinary rollback into RollbackFailed.
      RegistrationEffects reg = make_registration_effects(make_target(L"9.9.9"), recordingOps);
      check("restore without capture reports success", reg.restore());
    }

    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\\\GNLinkUpdateWiringTest");
  }

  remove_tree(staging);
  remove_tree(install);'''
assert old in d, 'cleanup anchor'
d = d.replace(old, new, 1)

# includes + a small helper
old = '#include "update_effects.hpp"'
new = '#include "update_effects.hpp"\n#include "update_registration_wiring.hpp"'
assert old in d
d = d.replace(old, new, 1)

old = 'bool exists(const std::wstring& path) {'
new = '''std::string narrow_w(const std::wstring& w) { return std::string(w.begin(), w.end()); }

bool exists(const std::wstring& path) {'''
assert old in d
d = d.replace(old, new, 1)

io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('registration wiring tests added')
