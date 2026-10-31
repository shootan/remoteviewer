# -*- coding: utf-8 -*-
import io

def edit(path, pairs):
    d = io.open(path, encoding='utf-8').read()
    for i, (old, new) in enumerate(pairs):
        assert d.count(old) == 1, '%s pair %d: %s' % (path, i, old[:70])
        d = d.replace(old, new, 1)
    io.open(path, 'w', encoding='utf-8', newline='').write(d)
    print('ok', path)


# ---------------------------------------------------------------- (6) the required set, before the disk
edit('apps/native_poc/src/update_effects_test.cpp', [
 ('''    e.DiscardDownload();
    check("discard removes the staged artifact", !e.VerifyDownload(f));
  }
''',
  '''    e.DiscardDownload();
    check("discard removes the staged artifact", !e.VerifyDownload(f));
  }

  {
    // The 0.2.109 failure, as a unit.
    //
    // A manifest that does not name a file the swap replaces used to pass everything here --
    // signature, sizes, hashes -- and die at the swap, by which time the product had been asked
    // to stop and the user was looking at a closed application. The answer was available before
    // any of that: the manifest is in hand and so is the list of what will be replaced.
    UpdateEffectsConfig c = base_config(install, staging);
    c.payloadNames = {L"AlphaPayload.bin", L"BetaPayload.bin", L"GNLinkSetup.exe"};
    WindowsUpdateEffects e(c);
    const ManifestFields f = artifact_fields();  // Alpha and Beta only -- no Setup
    check("download still writes what the manifest does name", e.Download(f), e.last_error());
    check("a manifest missing a replaced file is refused before the swap", !e.VerifyDownload(f),
          e.last_error());
    check("...and the reason names the missing file",
          e.last_error().find("GNLinkSetup.exe") != std::string::npos, e.last_error());

    // The same release with the file named passes, so the refusal above is about the omission and
    // not about the case existing at all.
    UpdateEffectsConfig ok = base_config(install, staging);
    ok.payloadNames = {L"AlphaPayload.bin", L"BetaPayload.bin", L"GNLinkSetup.exe"};
    WindowsUpdateEffects e2(ok);
    const ManifestFields complete =
        artifact_fields({"AlphaPayload.bin", "BetaPayload.bin", "GNLinkSetup.exe"});
    check("naming it makes the same release acceptable",
          e2.Download(complete) && e2.VerifyDownload(complete), e2.last_error());
    e2.DiscardDownload();
    e.DiscardDownload();
  }
'''),
])

# ---------------------------------------------------------------- (5) the two lists
edit('apps/native_poc/src/updater_assembly_test.cpp', [
 ('''  const auto seed_install = [&]() {''',
  '''  // ================================================================ what is stopped vs replaced

  {
    // These were one list, on the argument that sharing them prevents drift. It did not: the
    // updater was in neither, so no release has ever replaced GNLinkUpdater.exe, and a fix to the
    // updater could not be delivered by an update. They are two lists now, one derived from the
    // other, and the invariant that actually matters is asserted rather than assumed.
    const std::vector<std::wstring> stop = product_image_names();
    const std::vector<std::wstring> payload = product_payload_names();
    const auto has = [](const std::vector<std::wstring>& v, const wchar_t* name) {
      for (const std::wstring& n : v) {
        if (_wcsicmp(n.c_str(), name) == 0) return true;
      }
      return false;
    };

    check("everything stopped is also replaced", product_payload_list_contract());
    check("the updater is replaced", has(payload, L"GNLinkUpdater.exe"));
    // Stopping it would be either a no-op or self-sabotage: the running updater is a copy outside
    // installDir, and anything else answering to that name is not ours to close.
    check("...and is NOT a stop target", !has(stop, L"GNLinkUpdater.exe"));
    check("the installer is both", has(stop, L"GNLinkSetup.exe") && has(payload, L"GNLinkSetup.exe"));
    check("the data files are replaced", has(payload, L"ui\\\\shell.html") &&
                                             has(payload, L"ui\\\\macro.html"));
    check("...and are not processes", !has(stop, L"ui\\\\shell.html"));
    check("the payload is exactly the stop list plus three", payload.size() == stop.size() + 3,
          std::to_string(payload.size()) + " vs " + std::to_string(stop.size()));

    // No duplicates: a name listed twice would be swapped twice, and the second swap would move
    // the file just written aside as if it were the old one.
    bool duplicate = false;
    for (size_t i = 0; i < payload.size(); ++i) {
      for (size_t j = i + 1; j < payload.size(); ++j) {
        if (_wcsicmp(payload[i].c_str(), payload[j].c_str()) == 0) duplicate = true;
      }
    }
    check("no name appears twice", !duplicate);
  }

  const auto seed_install = [&]() {'''),
])
