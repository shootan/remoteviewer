# -*- coding: utf-8 -*-
import io

p = 'apps/native_poc/src/client_update_ui_test.cpp'
d = io.open(p, encoding='utf-8').read()
B = chr(92)

# ---------------------------------------------------------------- negative controls after the click
old = '''  // ------------------------------------------------------------------ busy, then released'''
new = '''  // ------------------------------------------------------------------ the controls that must fail
  //
  // Three ways a button can be present, enabled, and still unpressable. If any of these comes
  // back "pressable", the check above is measuring nothing and every PASS beside it is worthless.
  gWebView->PostWebMessageAsString(widen(available).c_str());
  pump_until([]() { return pressable(); }, 8000);

  eval(L"document.getElementById('updateNow').style.display='none'");
  ok(!pressable(), "negative control: display:none is not pressable", eval(kPressableScript));
  eval(L"document.getElementById('updateNow').style.display=''");

  eval(L"var o=document.createElement('div');o.id='blocker';"
       L"o.style.cssText='position:fixed;inset:0;z-index:99999;background:rgba(0,0,0,0)';"
       L"document.body.appendChild(o)");
  ok(!pressable(), "negative control: a transparent cover is not pressable", eval(kPressableScript));
  eval(L"document.getElementById('blocker').remove()");

  ok(pressable(), "and it is pressable again once the cover is gone", eval(kPressableScript));

'''+ old
assert d.count(old) == 1, 'negative anchor'
d = d.replace(old, new, 1)

# ---------------------------------------------------------------- the stale-page control + hash pin
old = '''  if (gController) gController->Close();
  DestroyWindow(gWindow);'''
new = '''  // ------------------------------------------------------------------ negative control: the old page
  //
  // The page as it shipped in 0.2.115 -- notice, no button. Loading it has to fail the same
  // assertions, because the whole point of this file is that it would have caught that release.
  const std::wstring stalePath = std::wstring(tempDir) + L"gnlink-stale-shell.html";
  {
    std::string stale = read_file(page);
    const std::string barStart = "<div id=\\"updateBar\\"";
    const size_t from = stale.find(barStart);
    const size_t to = stale.find("</div>", stale.find("id=\\"updateLater\\""));
    if (from != std::string::npos && to != std::string::npos) {
      stale.erase(from, (to + 6) - from);
    }
    std::ofstream out(stalePath, std::ios::binary);
    out.write(stale.data(), static_cast<std::streamsize>(stale.size()));
  }
  gReady = false;
  gFromPage.clear();
  gWebView->Navigate(stalePath.c_str());
  pump_until([]() { return gReady.load(); }, 20000);
  gWebView->PostWebMessageAsString(widen(available).c_str());
  Sleep(500);
  ok(!pressable(), "negative control: a page with no update button fails this test",
     eval(kPressableScript));
  gFromPage.clear();
  eval(L"var b=document.getElementById('updateNow'); if(b) b.click();");
  Sleep(500);
  ok(!page_sent("update"), "and it cannot send the message either");
  DeleteFileW(stalePath.c_str());

  if (gController) gController->Close();
  DestroyWindow(gWindow);'''
assert d.count(old) == 1, 'stale anchor'
d = d.replace(old, new, 1)

# ---------------------------------------------------------------- pin to the signed manifest
old = '''  gWebView->Navigate(page.c_str());
  ok(pump_until([]() { return gReady.load(); }, 30000),
     "the shipped page loads and reports ready");'''
new = '''  // ------------------------------------------------------------------ the page must be THE page
  //
  // Not "the repository copy", which is only the candidate until something says so. The signed
  // manifest names a sha256 for ui\\\\shell.html; the file loaded below has to be that file. Same
  // commit and same toolchain do not make two builds identical, so the hash is the only fixed
  // point there is.
  const std::string pageSha = sha256_hex_of_file(page);
  std::printf("sha   %s\\n", pageSha.c_str());
  const std::wstring manifestPath = release_manifest_path();
  if (manifestPath.empty()) {
    ok(false, "a signed manifest to pin against was not found");
  } else {
    const remote60::native_poc::update::ManifestResult loaded =
        remote60::native_poc::update::load_manifest(
            read_file(manifestPath), read_file(manifest_sig_path(manifestPath)),
            remote60::native_poc::update::trusted_public_key_hex(), "windows");
    if (loaded.outcome != remote60::native_poc::update::ManifestOutcome::Ok) {
      ok(false, "the manifest pinned against did not verify", loaded.detail);
    } else {
      std::string named;
      for (const auto& artifact : loaded.manifest.artifacts()) {
        if (artifact.name == "ui\\\\shell.html") named = artifact.sha256;
      }
      ok(!named.empty() && named == pageSha,
         "the page under test is the one the signed manifest names",
         named.empty() ? std::string("the manifest does not name ui") + "\\\\shell.html"
                       : named.substr(0, 16) + " vs " + pageSha.substr(0, 16));
    }
  }

  gWebView->Navigate(page.c_str());
  ok(pump_until([]() { return gReady.load(); }, 30000),
     "the shipped page loads and reports ready");'''
assert d.count(old) == 1, 'pin anchor'
d = d.replace(old, new, 1)

# ---------------------------------------------------------------- locate the manifest pair
old = '''// ---------------------------------------------------------------------------- the harness'''
new = '''/**
 * The newest signed release staged in this working copy.
 *
 * Deliberately not a fixed version: the pin has to follow the release being prepared, and a
 * version typed in here would rot into pinning against something that shipped months ago.
 */
std::wstring release_manifest_path() {
  const wchar_t* roots[] = {
      L"''' + B + B + '''..''' + B + B + '''..''' + B + B + '''..''' + B + B + '''..''' + B + B + '''.claude''' + B + B + '''rel",
      L"''' + B + B + '''..''' + B + B + '''..''' + B + B + '''..''' + B + B + '''.claude''' + B + B + '''rel",
  };
  std::wstring best;
  for (const wchar_t* suffix : roots) {
    wchar_t root[MAX_PATH]{};
    if (!PathCanonicalizeW(root, (executable_dir() + suffix).c_str())) continue;
    WIN32_FIND_DATAW found{};
    const HANDLE search = FindFirstFileW((std::wstring(root) + L"''' + B + B + '''*").c_str(), &found);
    if (search == INVALID_HANDLE_VALUE) continue;
    do {
      if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
      const std::wstring candidate =
          std::wstring(root) + L"''' + B + B + '''" + found.cFileName + L"''' + B + B + '''windows.manifest";
      if (!PathFileExistsW(candidate.c_str())) continue;
      // Lexical order is enough for 0.2.NNN and keeps this from needing a version parser.
      if (candidate > best) best = candidate;
    } while (FindNextFileW(search, &found));
    FindClose(search);
    if (!best.empty()) return best;
  }
  return best;
}

std::wstring manifest_sig_path(const std::wstring& manifestPath) {
  return manifestPath.substr(0, manifestPath.size() - 8) + L"sig";
}

// ---------------------------------------------------------------------------- the harness'''
assert d.count(old) == 1, 'locator anchor'
d = d.replace(old, new, 1)

io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('part 2 ok')
