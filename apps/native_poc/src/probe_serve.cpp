#include "probe_serve.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace remote60::native_poc {

const char* probe_serve_exit_name(ProbeServeExit e) {
  switch (e) {
    case ProbeServeExit::StopBeforeConnect: return "stop-before-connect";
    case ProbeServeExit::StopDuringConnect: return "stop-during-connect";
    case ProbeServeExit::StopBetween: return "stop-between";
    case ProbeServeExit::StopDuringRead: return "stop-during-read";
    case ProbeServeExit::ConnectError: return "connect-error";
    case ProbeServeExit::EventError: return "event-error";
  }
  return "?";
}

namespace {
bool stop_set(HANDLE stop) { return stop && WaitForSingleObject(stop, 0) == WAIT_OBJECT_0; }
}  // namespace

ProbeServeExit ProbeServeLoop(void* pipeHandle, void* stopEvent) {
  HANDLE pipe = static_cast<HANDLE>(pipeHandle);
  HANDLE stop = static_cast<HANDLE>(stopEvent);
  OVERLAPPED ov{};
  ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!ov.hEvent) return ProbeServeExit::EventError;  // C4: a failed CreateEvent is not ignored
  ProbeServeExit result = ProbeServeExit::ConnectError;
  for (;;) {
    if (stop_set(stop)) { result = ProbeServeExit::StopBeforeConnect; break; }  // (1)
    ResetEvent(ov.hEvent);
    BOOL connected = ConnectNamedPipe(pipe, &ov);
    DWORD gle = GetLastError();
    if (!connected && gle == ERROR_IO_PENDING) {
      HANDLE w[2] = {ov.hEvent, stop};
      DWORD r = WaitForMultipleObjects(2, w, FALSE, INFINITE);
      if (r == WAIT_FAILED) { result = ProbeServeExit::EventError; break; }
      if (r != WAIT_OBJECT_0) {  // (3) STOP won
        CancelIoEx(pipe, &ov);
        DWORD b = 0;
        GetOverlappedResult(pipe, &ov, &b, TRUE);
        result = ProbeServeExit::StopDuringConnect;
        break;
      }
      DWORD b = 0;
      if (!GetOverlappedResult(pipe, &ov, &b, TRUE)) { result = ProbeServeExit::EventError; break; }
      connected = TRUE;
    } else if (!connected && gle != ERROR_PIPE_CONNECTED) {
      result = ProbeServeExit::ConnectError;
      break;
    }
    if (stop_set(stop)) {  // (2)
      DisconnectNamedPipe(pipe);
      result = ProbeServeExit::StopBetween;
      break;
    }
    ResetEvent(ov.hEvent);
    char buf[64];
    DWORD got = 0;
    BOOL rok = ReadFile(pipe, buf, sizeof(buf), &got, &ov);  // drain; a probe sends nothing
    if (!rok && GetLastError() == ERROR_IO_PENDING) {
      HANDLE w[2] = {ov.hEvent, stop};
      DWORD r = WaitForMultipleObjects(2, w, FALSE, INFINITE);
      if (r == WAIT_FAILED) { result = ProbeServeExit::EventError; break; }
      if (r != WAIT_OBJECT_0) {  // (4) STOP won
        CancelIoEx(pipe, &ov);
        GetOverlappedResult(pipe, &ov, &got, TRUE);
        DisconnectNamedPipe(pipe);
        result = ProbeServeExit::StopDuringRead;
        break;
      }
      GetOverlappedResult(pipe, &ov, &got, TRUE);  // completed read
    }
    DisconnectNamedPipe(pipe);  // ready for the next client; name stays owned
  }
  CloseHandle(ov.hEvent);
  return result;
}

}  // namespace remote60::native_poc
