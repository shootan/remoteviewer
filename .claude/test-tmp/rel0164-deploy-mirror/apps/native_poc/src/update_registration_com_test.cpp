// The registration path's COM dependency, executed rather than described.
//
// The field failure was one line in updater.log -- "register failed: registration failed" -- and
// the cause was that the updater never initialised COM. CoCreateInstance(CLSID_ShellLink) returns
// CO_E_NOTINITIALIZED, the shell link is never written, registration reports failure, and the
// update rolls back. The installer had the initialisation in its own main; when the registration
// code was extracted to be shared, the caller's precondition did not come with it.
//
// So this drives the real thing: the production createShortcut from
// production_registration_ops(), the real CLSID_ShellLink, the real IPersistFile::Save. The only
// injected value is WHERE the link goes -- a directory beside this executable, never the machine's
// Start menu. Writing into every user's Start menu to prove a point is not a thing a test may do.
//
// Build: remote60_update_registration_com_test. Run: prints PASS lines, exit 0.

#include <windows.h>
#include <objbase.h>
#include <shobjidl.h>

#include <cstdio>
#include <string>
#include <thread>

#include "update_registration_wiring.hpp"

using namespace remote60::native_poc::update;
namespace install = remote60::native_poc::install;

namespace {

int gFailures = 0;

void check(const char* what, bool ok, const std::string& detail = {}) {
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : "  ",
              detail.c_str());
  if (!ok) ++gFailures;
}

std::wstring exe_dir() {
  wchar_t buf[MAX_PATH]{};
  GetModuleFileNameW(nullptr, buf, MAX_PATH);
  std::wstring path(buf);
  const size_t slash = path.find_last_of(L"\\/");
  return slash == std::wstring::npos ? path : path.substr(0, slash);
}

std::string narrow(const std::wstring& w) {
  if (w.empty()) return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
  std::string out(n > 0 ? n - 1 : 0, '\0');
  if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, out.data(), n, nullptr, nullptr);
  return out;
}

bool exists(const std::wstring& path) {
  return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

uint64_t size_of(const std::wstring& path) {
  WIN32_FILE_ATTRIBUTE_DATA info{};
  if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info)) return 0;
  return (static_cast<uint64_t>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
}

}  // namespace

int main() {
  std::printf("update_registration_com_test\n");

  const std::wstring folder = exe_dir() + L"\\com-shortcut-fixture";
  CreateDirectoryW(folder.c_str(), nullptr);
  // Something real to point at. A shell link to a path that does not exist still saves, but
  // pointing at this executable keeps the fixture honest.
  wchar_t self[MAX_PATH]{};
  GetModuleFileNameW(nullptr, self, MAX_PATH);

  const std::wstring linkName = L"GNLinkComFixture";
  const std::wstring linkPath = folder + L"\\" + linkName + L".lnk";
  DeleteFileW(linkPath.c_str());

  // ------------------------------------------------------------------ the mechanism, on a bare thread
  //
  // Not a claim about the old code -- a demonstration that the API really does refuse on a thread
  // with no apartment. This is what the updater was doing on every attempt.
  HRESULT bareCreate = S_OK;
  std::thread([&bareCreate] {
    IShellLinkW* link = nullptr;
    bareCreate = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW,
                                  reinterpret_cast<void**>(&link));
    if (link) link->Release();
  }).join();
  check("on a thread with no apartment, CLSID_ShellLink is refused", FAILED(bareCreate),
        "hr=0x" + [&] {
          char b[16]{};
          std::snprintf(b, sizeof(b), "%08lx", static_cast<unsigned long>(bareCreate));
          return std::string(b);
        }());
  check("...and the reason is exactly CO_E_NOTINITIALIZED", bareCreate == CO_E_NOTINITIALIZED);

  // ------------------------------------------------------------------ the production op, same thread shape
  bool wroteOnBareThread = false;
  std::string bareDetail;
  std::thread([&] {
    install::RegistrationOps ops = production_registration_ops(folder);
    wroteOnBareThread = ops.createShortcut(self, linkName, L"fixture", &bareDetail);
  }).join();
  check("the production shortcut writer succeeds on that same bare thread", wroteOnBareThread,
        bareDetail);
  check("...and the .lnk is really on disk", exists(linkPath) && size_of(linkPath) > 0,
        narrow(linkPath) + " " + std::to_string(size_of(linkPath)) + " bytes");

  // ------------------------------------------------------------------ RPC_E_CHANGED_MODE
  //
  // A thread already in the multithreaded apartment refuses an APARTMENTTHREADED request with
  // RPC_E_CHANGED_MODE. COM is usable; what must not happen is uninitialising somebody else's
  // apartment, or treating that code as a reason to give up.
  DeleteFileW(linkPath.c_str());
  bool wroteOnMtaThread = false;
  std::string mtaDetail;
  HRESULT changed = S_OK;
  std::thread([&] {
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return;
    changed = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);  // the code the scope must survive
    install::RegistrationOps ops = production_registration_ops(folder);
    wroteOnMtaThread = ops.createShortcut(self, linkName, L"fixture", &mtaDetail);
    // Still ours: the writer must not have torn this down.
    IShellLinkW* link = nullptr;
    const HRESULT after = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                           IID_IShellLinkW, reinterpret_cast<void**>(&link));
    if (link) link->Release();
    if (FAILED(after)) mtaDetail += " [apartment was lost]";
    CoUninitialize();
  }).join();
  check("a thread in the MTA reports RPC_E_CHANGED_MODE for an STA request",
        changed == RPC_E_CHANGED_MODE, "hr=" + std::to_string(static_cast<long>(changed)));
  check("...and the writer carries on and still writes the link", wroteOnMtaThread, mtaDetail);
  check("...without taking the caller's apartment away",
        mtaDetail.find("apartment was lost") == std::string::npos, mtaDetail);

  // ------------------------------------------------------------------ the detail actually carries
  {
    // A destination that cannot be written. The point is not the failure -- it is that the HRESULT
    // reaches the caller, which is what "registration failed" used to swallow.
    install::RegistrationOps ops = production_registration_ops(folder + L"\\nope\\deeper");
    std::string detail;
    const bool wrote = ops.createShortcut(self, linkName, L"fixture", &detail);
    check("an unwritable destination fails", !wrote);
    check("...and the detail carries an HRESULT", detail.find("0x") != std::string::npos, detail);
  }

  DeleteFileW(linkPath.c_str());
  RemoveDirectoryW(folder.c_str());

  if (gFailures == 0) {
    std::printf("update_registration_com_test: PASS\n");
    return 0;
  }
  std::printf("update_registration_com_test: FAILED (%d)\n", gFailures);
  return 1;
}
