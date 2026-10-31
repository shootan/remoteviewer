# -*- coding: utf-8 -*-
import io

p = 'apps/native_poc/src/update_process_targets.cpp'
d = io.open(p, encoding='utf-8').read()

# ---------------------------------------------------------------- the false positive
old = '''  HANDLE held = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, target.pid);
  if (!held) return true;  // already gone; nothing to ask'''
new = '''  SetLastError(0);
  HANDLE held = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, target.pid);
  if (!held) {
    // "Could not open" was being read as "already gone", and the two are not the same answer.
    // ERROR_INVALID_PARAMETER is a pid that is no longer a process -- nothing to ask, so the ask
    // succeeded vacuously. ERROR_ACCESS_DENIED is a process we cannot even look at, and calling
    // that success means the swap proceeds over something that may still be holding the files it
    // is about to replace. Access denied is not death; it is not knowing.
    if (GetLastError() == ERROR_ACCESS_DENIED) return false;
    return true;
  }'''
assert d.count(old) == 1, 'false positive'
d = d.replace(old, new, 1)

# ---------------------------------------------------------------- the service goes through the SCM
old2 = '''  const uint32_t pid = target.pid;
  // A GUI process gets WM_CLOSE on its top-level windows.'''
new2 = '''  const uint32_t pid = target.pid;

  // A service is not asked the way a program is. It has no window and no console, so both
  // mechanisms below are inapplicable by construction -- and it does have a supervisor: the SCM.
  // Asking the SCM is the polite request for a service, exactly as WM_CLOSE is for a window.
  if (image_leaf_is(target.imagePath, L"GNLinkInputService.exe")) {
    return request_service_stop(kSecureInputServiceName);
  }

  // A GUI process gets WM_CLOSE on its top-level windows.'''
assert d.count(old2) == 1, 'service branch'
d = d.replace(old2, new2, 1)

# ---------------------------------------------------------------- helpers
old3 = '''/** Whether this process owns a top-level window, i.e. whether WM_CLOSE is a possible request. */'''
new3 = '''/** Case-insensitive comparison of a full path's file name against one name. */
bool image_leaf_is(const std::wstring& imagePath, const wchar_t* name) {
  const size_t slash = imagePath.find_last_of(L"\\\\/");
  const std::wstring leaf = slash == std::wstring::npos ? imagePath : imagePath.substr(slash + 1);
  return _wcsicmp(leaf.c_str(), name) == 0;
}

/**
 * Asks the SCM to stop a service, and says nothing it cannot support.
 *
 * Three answers collapsed into two would be the same mistake as everywhere else here: a service
 * that is already stopped is a success, a stop that was accepted is a success, and being unable
 * to open or control it is NOT -- it is the case where we do not know, and an update that treats
 * it as success proceeds over a running service.
 */
bool request_service_stop(const wchar_t* serviceName) {
  SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
  if (!manager) return false;
  SC_HANDLE service = OpenServiceW(manager, serviceName, SERVICE_STOP | SERVICE_QUERY_STATUS);
  if (!service) {
    const DWORD err = GetLastError();
    CloseServiceHandle(manager);
    // Not installed at all: there is nothing to stop, which is not a failure to ask.
    return err == ERROR_SERVICE_DOES_NOT_EXIST;
  }

  SERVICE_STATUS status{};
  bool ok = ControlService(service, SERVICE_CONTROL_STOP, &status) != FALSE;
  if (!ok && GetLastError() == ERROR_SERVICE_NOT_ACTIVE) ok = true;  // already where we want it
  CloseServiceHandle(service);
  CloseServiceHandle(manager);
  return ok;
}

/** Whether this process owns a top-level window, i.e. whether WM_CLOSE is a possible request. */'''
assert d.count(old3) == 1, 'helpers'
d = d.replace(old3, new3, 1)

if '#include "secure_input_protocol.hpp"' not in d:
    anchor = '#include "update_process_targets.hpp"'
    assert d.count(anchor) == 1, 'include anchor'
    d = d.replace(anchor, '#include "secure_input_protocol.hpp"\n' + anchor, 1)

io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok')
