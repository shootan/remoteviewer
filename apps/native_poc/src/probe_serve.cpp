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

enum class IoEnd {
  Completed,   // the I/O finished with success
  IoFailed,    // the I/O finished with an error (*err)
  Stopped,     // STOP: cancelled and collected
  WaitFailed,  // the wait itself failed (*err): cancelled and collected
};

// A pending overlapped I/O, waited with STOP. Whatever ends the wait, the I/O is complete when this
// returns: a STOP or a failed wait cancels it and collects its completion (D4).
IoEnd finish_pending(HANDLE pipe, OVERLAPPED* ov, HANDLE stop, const ProbeServeHooks* hooks, ProbeServePoint point,
                     DWORD* bytes, DWORD* err) {
  *err = 0;
  HANDLE w[2] = {ov->hEvent, stop};
  const DWORD r = (hooks && hooks->wait) ? hooks->wait(2, w) : WaitForMultipleObjects(2, w, FALSE, INFINITE);
  if (r == WAIT_OBJECT_0) {
    if (GetOverlappedResult(pipe, ov, bytes, FALSE)) return IoEnd::Completed;
    *err = GetLastError();  // the I/O completed with this error
    return IoEnd::IoFailed;
  }
  const DWORD waitErr = (r == WAIT_FAILED) ? GetLastError() : ERROR_INVALID_FUNCTION;  // before any other call
  (void)CancelIoEx(pipe, ov);
  (void)GetOverlappedResult(pipe, ov, bytes, TRUE);  // the cancel is a request: wait for the completion
  if (hooks && hooks->reaped) hooks->reaped(point);
  if (r == WAIT_OBJECT_0 + 1) return IoEnd::Stopped;
  *err = waitErr;
  return IoEnd::WaitFailed;
}

// A read that ended because the client went away (or sent more than the probe reads) is not a fault.
bool client_gone(DWORD err) {
  return err == ERROR_BROKEN_PIPE || err == ERROR_PIPE_NOT_CONNECTED || err == ERROR_MORE_DATA ||
         err == ERROR_NO_DATA;
}

}  // namespace

ProbeServeResult ProbeServeRun(void* pipeHandle, void* stopEvent, void* ioEvent, const ProbeServeHooks* hooks) {
  HANDLE pipe = static_cast<HANDLE>(pipeHandle);
  HANDLE stop = static_cast<HANDLE>(stopEvent);
  if (!ioEvent) return {ProbeServeExit::EventError, ERROR_INVALID_HANDLE};
  OVERLAPPED ov{};
  ov.hEvent = static_cast<HANDLE>(ioEvent);
  char buf[64];  // outlives every read issued into it (each is complete before the next step)
  auto at = [&](ProbeServePoint p) {
    if (hooks && hooks->at) hooks->at(p);
  };
  for (;;) {
    at(ProbeServePoint::BeforeConnect);
    if (stop_set(stop)) return {ProbeServeExit::StopBeforeConnect, 0};  // (1)
    ResetEvent(ov.hEvent);
    const BOOL connected = ConnectNamedPipe(pipe, &ov);
    const DWORD gle = connected ? 0 : GetLastError();
    if (!connected && gle == ERROR_IO_PENDING) {
      at(ProbeServePoint::ConnectPending);
      DWORD b = 0, err = 0;
      const IoEnd e = finish_pending(pipe, &ov, stop, hooks, ProbeServePoint::ConnectPending, &b, &err);
      if (e == IoEnd::Stopped) return {ProbeServeExit::StopDuringConnect, 0};  // (3)
      if (e == IoEnd::WaitFailed) return {ProbeServeExit::EventError, err};
      if (e == IoEnd::IoFailed) return {ProbeServeExit::ConnectError, err};
    } else if (!connected && gle != ERROR_PIPE_CONNECTED) {
      return {ProbeServeExit::ConnectError, gle};
    }
    at(ProbeServePoint::Between);
    if (stop_set(stop)) {  // (2)
      DisconnectNamedPipe(pipe);
      return {ProbeServeExit::StopBetween, 0};
    }
    ResetEvent(ov.hEvent);
    DWORD got = 0;
    const BOOL rok = ReadFile(pipe, buf, sizeof(buf), &got, &ov);  // drain; a probe sends nothing
    const DWORD rerr = rok ? 0 : GetLastError();
    if (!rok && rerr == ERROR_IO_PENDING) {
      at(ProbeServePoint::ReadPending);
      DWORD err = 0;
      const IoEnd e = finish_pending(pipe, &ov, stop, hooks, ProbeServePoint::ReadPending, &got, &err);
      if (e == IoEnd::Stopped) {  // (4)
        DisconnectNamedPipe(pipe);
        return {ProbeServeExit::StopDuringRead, 0};
      }
      if (e == IoEnd::WaitFailed || (e == IoEnd::IoFailed && !client_gone(err))) {
        DisconnectNamedPipe(pipe);
        return {ProbeServeExit::EventError, err};
      }
    } else if (!rok && !client_gone(rerr)) {
      DisconnectNamedPipe(pipe);
      return {ProbeServeExit::EventError, rerr};
    }
    DisconnectNamedPipe(pipe);  // ready for the next client; name stays owned
  }
}

}  // namespace remote60::native_poc
