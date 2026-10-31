#pragma once

#include <windows.h>
#include <cstdint>

namespace remote60::native_poc {

// Arm termination BEFORE writing to an inherited synchronous pipe. A parent whose log reader
// stopped must not be able to stop its child's watchdog as well. The terminator owns no object
// from the failing thread; its only input is the exit code passed by value.
[[noreturn]] inline void terminate_with_diagnostic(unsigned int code, const char* text, DWORD bytes) {
  HANDLE terminator = CreateThread(nullptr, 0, [](void* value) -> DWORD {
    Sleep(100);
    TerminateProcess(GetCurrentProcess(), static_cast<UINT>(reinterpret_cast<uintptr_t>(value)));
    return 0;
  }, reinterpret_cast<void*>(static_cast<uintptr_t>(code)), 0, nullptr);
  if (terminator) {
    CloseHandle(terminator);
    HANDLE err = GetStdHandle(STD_ERROR_HANDLE);
    if (err && err != INVALID_HANDLE_VALUE && text && bytes) {
      DWORD written = 0;
      WriteFile(err, text, bytes, &written, nullptr);
    }
  }
  TerminateProcess(GetCurrentProcess(), code);
  // TerminateProcess on this process does not return on success. Do not run DLL detach on an
  // already wedged process if an exceptional OS failure made it return.
  for (;;) Sleep(INFINITE);
}

}  // namespace remote60::native_poc
