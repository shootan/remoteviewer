# -*- coding: utf-8 -*-
import io

p = 'apps/native_poc/src/client_update_ui_test.cpp'
d = io.open(p, encoding='utf-8').read()
B = chr(92)

# ------------------------------------------------------------------ repo copy is the source of truth
old = '''std::wstring production_page_path() {
  const std::wstring beside = executable_dir() + L"''' + B + B + '''ui''' + B + B + '''shell.html";
  if (PathFileExistsW(beside.c_str())) return beside;
  const wchar_t* candidates[] = {
      L"''' + B + B + '''..''' + B + B + '''..''' + B + B + '''..''' + B + B + '''..''' + B + B + '''apps''' + B + B + '''native_poc''' + B + B + '''ui''' + B + B + '''shell.html",
      L"''' + B + B + '''..''' + B + B + '''..''' + B + B + '''..''' + B + B + '''apps''' + B + B + '''native_poc''' + B + B + '''ui''' + B + B + '''shell.html",
  };
  for (const wchar_t* suffix : candidates) {
    wchar_t full[MAX_PATH]{};
    const std::wstring joined = executable_dir() + suffix;
    if (PathCanonicalizeW(full, joined.c_str()) && PathFileExistsW(full)) return full;
  }
  return {};
}'''
new = '''std::wstring production_page_path() {
  // The repository copy first, deliberately. The copy beside the executable is written by the
  // build and can lag the source by a whole edit -- it did on the first run of this test, which
  // would have meant asserting against a page nobody was shipping.
  const wchar_t* candidates[] = {
      L"''' + B + B + '''..''' + B + B + '''..''' + B + B + '''..''' + B + B + '''..''' + B + B + '''apps''' + B + B + '''native_poc''' + B + B + '''ui''' + B + B + '''shell.html",
      L"''' + B + B + '''..''' + B + B + '''..''' + B + B + '''..''' + B + B + '''apps''' + B + B + '''native_poc''' + B + B + '''ui''' + B + B + '''shell.html",
  };
  for (const wchar_t* suffix : candidates) {
    wchar_t full[MAX_PATH]{};
    const std::wstring joined = executable_dir() + suffix;
    if (PathCanonicalizeW(full, joined.c_str()) && PathFileExistsW(full)) return full;
  }
  const std::wstring beside = executable_dir() + L"''' + B + B + '''ui''' + B + B + '''shell.html";
  if (PathFileExistsW(beside.c_str())) return beside;
  return {};
}'''
assert d.count(old) == 1, 'path anchor'
d = d.replace(old, new, 1)

# ------------------------------------------------------------------ say why, when it will not come up
old = '''          [&created](HRESULT hr, ICoreWebView2Environment* environment) -> HRESULT {
            if (FAILED(hr) || !environment) {
              gFatal = true;
              return S_OK;
            }'''
new = '''          [&created](HRESULT hr, ICoreWebView2Environment* environment) -> HRESULT {
            if (FAILED(hr) || !environment) {
              std::printf("      environment hr=0x%08lx\\n", static_cast<unsigned long>(hr));
              gFatal = true;
              return S_OK;
            }'''
assert d.count(old) == 1, 'env anchor'
d = d.replace(old, new, 1)

old = '''                    [&created](HRESULT hr2, ICoreWebView2Controller* controller) -> HRESULT {
                      if (FAILED(hr2) || !controller) {
                        gFatal = true;
                        return S_OK;
                      }'''
new = '''                    [&created](HRESULT hr2, ICoreWebView2Controller* controller) -> HRESULT {
                      if (FAILED(hr2) || !controller) {
                        std::printf("      controller hr=0x%08lx\\n",
                                    static_cast<unsigned long>(hr2));
                        gFatal = true;
                        return S_OK;
                      }'''
assert d.count(old) == 1, 'controller anchor'
d = d.replace(old, new, 1)

# ------------------------------------------------------------------ its own user-data folder
old = '''  const HRESULT env = CreateCoreWebView2EnvironmentWithOptions(
      nullptr, nullptr, nullptr,'''
new = '''  // Its own user-data folder, under TEMP. Sharing the client's would have this test writing into
  // the profile of the program a person may be using at the time.
  wchar_t tempDir[MAX_PATH]{};
  GetTempPathW(MAX_PATH, tempDir);
  const std::wstring userData = std::wstring(tempDir) + L"gnlink-update-ui-test";
  CreateDirectoryW(userData.c_str(), nullptr);

  const HRESULT env = CreateCoreWebView2EnvironmentWithOptions(
      nullptr, userData.c_str(), nullptr,'''
assert d.count(old) == 1, 'userdata anchor'
d = d.replace(old, new, 1)

io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok')
