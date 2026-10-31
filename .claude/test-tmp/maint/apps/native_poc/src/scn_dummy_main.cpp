// A stand-in for a product executable in the updater's scenario tests.
//
// It exists because the tests used copies of the command interpreter, and that had two costs a
// test has no business imposing on the machine it runs on.
//
// The first is that cmd.exe does not exit. Started with no arguments it waits for input forever,
// so every scenario left one behind and the suite needed a sweep to hunt them down -- a sweep that
// terminated processes, which is how it came to terminate ones it could not identify.
//
// The second is worse. A launch routed through the shell is performed by explorer, asynchronously,
// and there is no way to ask whether it has happened yet. The suite fired one, finished, and
// removed its directory; explorer got to the request afterwards and put "Windows cannot find
// ...\ScnClient.exe" on the user's desktop. Checking the file exists before asking is no defence,
// because the gap between the check and the launch is exactly where the deletion happened.
//
// So this runs, records that it ran, and exits. Recording is what makes the difference: the test
// can WAIT for the evidence that a launch landed instead of assuming a timeout was long enough,
// and a directory is only removed once nothing is still on its way to it.
//
// It writes one line to `witness.txt` beside its own image and returns immediately. No window
// worth the name, no lingering process, nothing to sweep.

#include <windows.h>

#include <cstdio>
#include <string>

namespace {

std::wstring self_path() {
  wchar_t path[MAX_PATH]{};
  GetModuleFileNameW(nullptr, path, MAX_PATH);
  return path;
}

std::wstring directory_of(const std::wstring& path) {
  const size_t slash = path.find_last_of(L"\\/");
  return (slash == std::wstring::npos) ? std::wstring() : path.substr(0, slash);
}

std::string narrow(const std::wstring& text) {
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

}  // namespace

int wmain() {
  const std::wstring self = self_path();
  const std::wstring dir = directory_of(self);
  const std::wstring leaf = self.substr(dir.empty() ? 0 : dir.size() + 1);

  // Appended, and each line names the image and the pid. A scenario that expects one launch and
  // gets two needs to be able to see the second, so this must never overwrite.
  //
  // Opened with sharing, and failure is ignored: another copy may be writing at the same moment,
  // and a fixture that killed itself over a locked witness file would be a fixture that fails the
  // test for reasons belonging to the fixture.
  const std::wstring witness = dir.empty() ? L"witness.txt" : (dir + L"\\witness.txt");
  HANDLE file = CreateFileW(witness.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                            nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file != INVALID_HANDLE_VALUE) {
    const std::string line = narrow(leaf) + " " + std::to_string(GetCurrentProcessId()) + "\n";
    DWORD written = 0;
    WriteFile(file, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
    CloseHandle(file);
  }
  return 0;
}
