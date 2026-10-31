#include "windows_environment_snapshot.hpp"
#include <cstdio>

int main() {
  using remote60::native_poc::WindowsEnvironmentSnapshot;
  const std::wstring key = L"REMOTE60_TEST_ENV_SNAPSHOT_" + std::to_wstring(GetCurrentProcessId());
  WindowsEnvironmentSnapshot cleanup(key.c_str());
  const std::wstring longValue(4096, L'x');
  bool pass = SetEnvironmentVariableW(key.c_str(), longValue.c_str()) != FALSE;
  {
    WindowsEnvironmentSnapshot snapshot(key.c_str());
    pass = pass && snapshot.Original() && std::wstring(snapshot.Original()) == longValue;
    pass = snapshot.Set(L"wgc") && pass;
  }
  {
    WindowsEnvironmentSnapshot restored(key.c_str());
    pass = pass && restored.Original() && std::wstring(restored.Original()) == longValue;
  }
  SetEnvironmentVariableW(key.c_str(), nullptr);
  { WindowsEnvironmentSnapshot absent(key.c_str()); pass = absent.Set(L"wgc") && pass; }
  { WindowsEnvironmentSnapshot restored(key.c_str()); pass = pass && restored.Original() == nullptr; }
  SetEnvironmentVariableW(key.c_str(), L"");
  {
    WindowsEnvironmentSnapshot empty(key.c_str());
    pass = pass && empty.Original() && std::wstring(empty.Original()).empty();
    pass = empty.Set(L"wgc") && pass;
  }
  { WindowsEnvironmentSnapshot restored(key.c_str()); pass = pass && restored.Original() && std::wstring(restored.Original()).empty(); }
  std::printf("windows_environment_snapshot_test: %s (4096 chars, absent, empty restored)\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}
