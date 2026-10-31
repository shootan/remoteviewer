import io

p = 'apps/native_poc/src/update_credential_channel.cpp'
d = io.open(p, encoding='utf-8').read()

start = d.find('bool receive_credential(const std::wstring& pipeName, uint32_t deadlineMs, std::string* payload,')
assert start > 0, 'receive start'
end = d.find('}  // namespace remote60::native_poc::update')
assert end > start

new = '''bool receive_credential(const std::wstring& pipeName, uint32_t deadlineMs, std::string* payload,
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

'''

d = d[:start] + new + d[end:]
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('receiver now has a read deadline')
