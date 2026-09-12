#include "update_credential_channel.hpp"
#include "bounded_process_exit.hpp"

#include <sddl.h>

#include <cstring>
#include <vector>

#pragma comment(lib, "advapi32.lib")

namespace remote60::native_poc::update {
namespace {

std::wstring to_wide_number(uint64_t value) {
  wchar_t buffer[32];
  swprintf_s(buffer, L"%llu", static_cast<unsigned long long>(value));
  return buffer;
}

void put_u32(std::string* out, uint32_t value) {
  out->push_back(static_cast<char>(value & 0xff));
  out->push_back(static_cast<char>((value >> 8) & 0xff));
  out->push_back(static_cast<char>((value >> 16) & 0xff));
  out->push_back(static_cast<char>((value >> 24) & 0xff));
}

uint32_t read_u32(const std::string& text, size_t at) {
  return static_cast<uint32_t>(static_cast<unsigned char>(text[at])) |
         (static_cast<uint32_t>(static_cast<unsigned char>(text[at + 1])) << 8) |
         (static_cast<uint32_t>(static_cast<unsigned char>(text[at + 2])) << 16) |
         (static_cast<uint32_t>(static_cast<unsigned char>(text[at + 3])) << 24);
}

void set_error(std::string* error, const std::string& text) {
  if (error) *error = text;
}

/** Overwrites the bytes before releasing them. Not a guarantee, but not leaving them either. */
void wipe(std::string* text) {
  if (!text || text->empty()) return;
  SecureZeroMemory(&(*text)[0], text->size());
  text->clear();
}

/**
 * The current user, SYSTEM and Administrators. Written out rather than inherited: the default DACL
 * depends on the token this happens to run under, and "whatever it happened to be" is not an
 * access decision.
 */
bool build_pipe_security(SECURITY_ATTRIBUTES* attributes, PSECURITY_DESCRIPTOR* descriptor) {
  // D: no inheritance, three explicit ACEs, full access to each.
  const wchar_t* sddl = L"D:P(A;;GA;;;OW)(A;;GA;;;SY)(A;;GA;;;BA)";
  if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SDDL_REVISION_1, descriptor,
                                                            nullptr)) {
    return false;
  }
  attributes->nLength = sizeof(SECURITY_ATTRIBUTES);
  attributes->lpSecurityDescriptor = *descriptor;
  attributes->bInheritHandle = FALSE;
  return true;
}

/** Waits for an overlapped operation, cancelling it if the deadline passes. */
bool wait_overlapped(HANDLE pipe, OVERLAPPED* overlapped, uint32_t deadlineMs, DWORD* transferred,
                     std::string* error) {
  const DWORD waited = WaitForSingleObject(overlapped->hEvent, deadlineMs);
  if (waited != WAIT_OBJECT_0) {
    // Cancelled rather than abandoned: leaving it pending would let it complete after the caller
    // has decided the attempt failed, which is the "late arrival is served anyway" case.
    CancelIoEx(pipe, overlapped);
    if (WaitForSingleObject(overlapped->hEvent, 1000) != WAIT_OBJECT_0) {
      const char message[] = "[credential] cancellation did not complete; terminating before releasing pending I/O storage\n";
      remote60::native_poc::terminate_with_diagnostic(47, message, sizeof(message) - 1);
    }
    set_error(error, waited == WAIT_TIMEOUT ? "timed out" : "wait failed");
    return false;
  }
  if (!GetOverlappedResult(pipe, overlapped, transferred, FALSE)) {
    set_error(error, "overlapped result failed (" + std::to_string(GetLastError()) + ")");
    return false;
  }
  return true;
}

struct Event {
  HANDLE h = nullptr;
  Event() { h = CreateEventW(nullptr, TRUE, FALSE, nullptr); }
  ~Event() { if (h) CloseHandle(h); }
};

}  // namespace

std::wstring make_credential_pipe_name(uint32_t pid, uint64_t tick) {
  // Its own name space, not derived from the ready event: the PC client deliberately has no ready
  // event (it does not wait for permission to leave), and a channel that carries a credential
  // still needs an identity of its own.
  return L"\\\\.\\pipe\\GNLinkUpdateCred-" + to_wide_number(pid) + L"-" + to_wide_number(tick);
}

std::string encode_credential_frame(const std::string& payload) {
  std::string frame;
  frame.reserve(12 + payload.size());
  put_u32(&frame, kCredentialFrameMagic);
  put_u32(&frame, kCredentialFrameVersion);
  put_u32(&frame, static_cast<uint32_t>(payload.size()));
  frame.append(payload);
  return frame;
}

bool decode_credential_frame(const std::string& frame, std::string* payload, std::string* error) {
  if (!payload) return false;
  payload->clear();
  if (frame.size() < 12) {
    set_error(error, "frame shorter than its header");
    return false;
  }
  if (read_u32(frame, 0) != kCredentialFrameMagic) {
    set_error(error, "not a credential frame");
    return false;
  }
  if (read_u32(frame, 4) != kCredentialFrameVersion) {
    set_error(error, "unknown frame version");
    return false;
  }
  const uint32_t length = read_u32(frame, 8);
  if (length > kCredentialMaxPayload) {
    set_error(error, "frame claims more than the maximum");
    return false;
  }
  // Short is a failure, not a truncated credential: a half-read header would otherwise arrive as
  // a plausible token.
  if (frame.size() != static_cast<size_t>(12) + length) {
    set_error(error, "frame length does not match what arrived");
    return false;
  }
  payload->assign(frame, 12, length);
  return true;
}

CredentialServer::~CredentialServer() { Close(); }

void CredentialServer::Close() {
  if (pipe_ != INVALID_HANDLE_VALUE) {
    DisconnectNamedPipe(pipe_);
    CloseHandle(pipe_);
    pipe_ = INVALID_HANDLE_VALUE;
  }
}

bool CredentialServer::Create(const std::wstring& pipeName, std::string* error) {
  Close();
  SECURITY_ATTRIBUTES attributes{};
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  if (!build_pipe_security(&attributes, &descriptor)) {
    set_error(error, "could not build the pipe's access list");
    return false;
  }
  pipe_ = CreateNamedPipeW(
      pipeName.c_str(),
      PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
      PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
      1,                              // one instance: a second connection has nowhere to land
      kCredentialMaxPayload + 64, kCredentialMaxPayload + 64, 0, &attributes);
  const DWORD created = GetLastError();
  if (descriptor) LocalFree(descriptor);
  if (pipe_ == INVALID_HANDLE_VALUE) {
    // ERROR_ACCESS_DENIED here usually means the name already existed -- which is the case
    // FILE_FLAG_FIRST_PIPE_INSTANCE exists to turn into a failure rather than a shared endpoint.
    set_error(error, "could not create the credential pipe (" + std::to_string(created) + ")");
    return false;
  }
  return true;
}

bool CredentialServer::Serve(HANDLE expectedProcess, std::string& payload, uint32_t deadlineMs,
                             std::string* error) {
  // Whatever happens, this copy does not outlive the call.
  struct Wiper {
    std::string* text;
    ~Wiper() { wipe(text); }
  } wiper{&payload};

  if (pipe_ == INVALID_HANDLE_VALUE) {
    set_error(error, "the credential pipe was never created");
    return false;
  }
  if (!expectedProcess || expectedProcess == INVALID_HANDLE_VALUE) {
    set_error(error, "no process handle to check the client against");
    return false;
  }
  const DWORD expectedPid = GetProcessId(expectedProcess);
  if (expectedPid == 0) {
    set_error(error, "could not read the expected process id");
    return false;
  }

  Event connected;
  if (!connected.h) {
    set_error(error, "could not create the connect event");
    return false;
  }
  OVERLAPPED overlapped{};
  overlapped.hEvent = connected.h;
  DWORD transferred = 0;
  if (!ConnectNamedPipe(pipe_, &overlapped)) {
    const DWORD err = GetLastError();
    if (err == ERROR_PIPE_CONNECTED) {
      SetEvent(connected.h);
    } else if (err != ERROR_IO_PENDING) {
      set_error(error, "connect failed (" + std::to_string(err) + ")");
      return false;
    }
  }
  if (!wait_overlapped(pipe_, &overlapped, deadlineMs, &transferred, error)) return false;

  // Who actually turned up. The handle we were given is still open, so this pid cannot have been
  // recycled onto some other process between the launch and this comparison.
  ULONG clientPid = 0;
  if (!GetNamedPipeClientProcessId(pipe_, &clientPid)) {
    set_error(error, "could not identify the client process");
    return false;
  }
  if (clientPid != expectedPid) {
    set_error(error, "the process that connected is not the one that was started (" +
                         std::to_string(clientPid) + " != " + std::to_string(expectedPid) + ")");
    // Dropped, not merely ignored. Left connected, the client would sit in a read that never
    // completes -- and a caller that waited for it would hang instead of failing.
    DisconnectNamedPipe(pipe_);
    return false;  // nothing written
  }

  const std::string frame = encode_credential_frame(payload);
  size_t sent = 0;
  while (sent < frame.size()) {
    ResetEvent(connected.h);
    OVERLAPPED writing{};
    writing.hEvent = connected.h;
    DWORD wrote = 0;
    if (!WriteFile(pipe_, frame.data() + sent, static_cast<DWORD>(frame.size() - sent), &wrote,
                   &writing)) {
      if (GetLastError() != ERROR_IO_PENDING) {
        set_error(error, "write failed (" + std::to_string(GetLastError()) + ")");
        return false;
      }
      if (!wait_overlapped(pipe_, &writing, deadlineMs, &wrote, error)) return false;
    }
    if (wrote == 0) {
      set_error(error, "write made no progress");
      return false;
    }
    // "One write" is not a guarantee that everything left. Counted, because a short write here
    // would deliver a prefix of a credential and look like a complete small one.
    sent += wrote;
  }

  // The receiver says it has the whole frame. Without this the sender cannot tell delivery from
  // a pipe that was closed while the last bytes were still in it.
  char ack = 0;
  ResetEvent(connected.h);
  OVERLAPPED reading{};
  reading.hEvent = connected.h;
  DWORD read = 0;
  if (!ReadFile(pipe_, &ack, 1, &read, &reading)) {
    if (GetLastError() != ERROR_IO_PENDING) {
      set_error(error, "no acknowledgement (" + std::to_string(GetLastError()) + ")");
      return false;
    }
    if (!wait_overlapped(pipe_, &reading, deadlineMs, &read, error)) return false;
  }
  if (read != 1 || ack != 1) {
    set_error(error, "the receiver did not acknowledge the credential");
    return false;
  }
  return true;
}

bool receive_credential(const std::wstring& pipeName, uint32_t deadlineMs, std::string* payload,
                        std::string* error) {
  if (!payload) return false;
  payload->clear();

  const DWORD began = GetTickCount();
  const auto remaining = [&]() -> DWORD {
    const DWORD spent = GetTickCount() - began;
    return spent >= deadlineMs ? 0 : deadlineMs - spent;
  };

  // Overlapped on this side too. Opening the pipe is not the same as being sent anything, and a
  // blocking read has no way to give up: a server that connects and then says nothing would leave
  // this process waiting for ever, which is the one outcome the deadline exists to prevent. The
  // three-process test found exactly that -- the bootstrap hung instead of failing.
  HANDLE pipe = INVALID_HANDLE_VALUE;
  for (;;) {
    pipe = CreateFileW(pipeName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                       FILE_FLAG_OVERLAPPED, nullptr);
    if (pipe != INVALID_HANDLE_VALUE) break;
    const DWORD err = GetLastError();
    if (err != ERROR_PIPE_BUSY && err != ERROR_FILE_NOT_FOUND) {
      set_error(error, "could not open the credential pipe (" + std::to_string(err) + ")");
      return false;
    }
    if (remaining() == 0) {
      set_error(error, "the credential pipe never appeared");
      return false;
    }
    Sleep(25);
  }

  Event ready;
  if (!ready.h) {
    CloseHandle(pipe);
    set_error(error, "could not create the read event");
    return false;
  }

  std::string frame;
  char buffer[512];
  bool ok = false;
  std::string failure = "the credential was not delivered";
  for (;;) {
    if (remaining() == 0) {
      failure = "timed out waiting for the credential";
      break;
    }
    ResetEvent(ready.h);
    OVERLAPPED reading{};
    reading.hEvent = ready.h;
    DWORD read = 0;
    if (!ReadFile(pipe, buffer, sizeof(buffer), &read, &reading)) {
      if (GetLastError() != ERROR_IO_PENDING) {
        failure = "read failed (" + std::to_string(GetLastError()) + ")";
        break;
      }
      std::string waitError;
      if (!wait_overlapped(pipe, &reading, remaining(), &read, &waitError)) {
        failure = "waiting for the credential " + waitError;
        break;
      }
    }
    if (read == 0) break;
    frame.append(buffer, read);
    if (frame.size() > kCredentialMaxPayload + 64) {
      failure = "more arrived than a credential frame can be";
      break;
    }
    if (frame.size() >= 12) {
      const uint32_t length = read_u32(frame, 8);
      if (length <= kCredentialMaxPayload && frame.size() >= static_cast<size_t>(12) + length) {
        ok = decode_credential_frame(frame.substr(0, 12 + length), payload, &failure);
        break;
      }
    }
  }

  if (ok) {
    const char ack = 1;
    ResetEvent(ready.h);
    OVERLAPPED writing{};
    writing.hEvent = ready.h;
    DWORD wrote = 0;
    if (!WriteFile(pipe, &ack, 1, &wrote, &writing)) {
      std::string waitError;
      if (GetLastError() != ERROR_IO_PENDING ||
          !wait_overlapped(pipe, &writing, remaining(), &wrote, &waitError)) {
        wrote = 0;
      }
    }
    if (wrote != 1) {
      ok = false;
      failure = "could not acknowledge the credential";
      wipe(payload);
    }
  }
  wipe(&frame);
  CloseHandle(pipe);
  if (!ok) set_error(error, failure);
  return ok;
}

}  // namespace remote60::native_poc::update
