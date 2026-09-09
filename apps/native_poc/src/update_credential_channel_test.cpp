// The credential channel, with the checks driven rather than described.
//
// What matters here is not that a credential arrives -- that is the easy half -- but that it does
// NOT arrive when the receiver is not the process that was started. So the mismatch case is driven
// by connecting from a process that really is not the expected one and observing that nothing was
// written, and the frame cases are driven by handing the decoder bytes that stop in the middle.
//
// Everything is on this machine, on a pipe named for this process, and torn down at the end. No
// installed component is touched, no fixed name is taken, and the only credential involved is the
// string "fixture-token-not-a-real-credential".

#include "update_credential_channel.hpp"

#include <atomic>
#include <cstdio>
#include <string>
#include <thread>

namespace {

int gFailures = 0;

void check(const char* name, bool cond, const std::string& detail = {}) {
  std::printf("%s  %s%s%s\n", cond ? "PASS" : "FAIL", name, detail.empty() ? "" : "  ",
              detail.c_str());
  if (!cond) ++gFailures;
}

const char* kFixtureToken = "x-host-token: fixture-token-not-a-real-credential";

}  // namespace

int main() {
  // Unbuffered: a suite that hangs should still show which case it reached.
  setvbuf(stdout, nullptr, _IONBF, 0);
  using namespace remote60::native_poc::update;

  // ---------------------------------------------------------------- the frame
  {
    const std::string frame = encode_credential_frame("abc");
    check("a frame is header plus payload", frame.size() == 15, std::to_string(frame.size()));

    std::string payload;
    std::string error;
    check("it decodes back", decode_credential_frame(frame, &payload, &error), error);
    check("...to what went in", payload == "abc", payload);

    // Every one of these used to be the same thing as "a short credential" if the reader simply
    // took what it had.
    check("a header on its own is not a frame",
          !decode_credential_frame(frame.substr(0, 12), &payload, &error));
    check("a frame cut in the payload is refused",
          !decode_credential_frame(frame.substr(0, 14), &payload, &error), error);
    check("...and yields nothing", payload.empty(), payload);
    check("a frame with trailing bytes is refused",
          !decode_credential_frame(frame + "x", &payload, &error), error);

    std::string wrongMagic = frame;
    wrongMagic[0] = 'X';
    check("something that is not a frame is refused",
          !decode_credential_frame(wrongMagic, &payload, &error), error);

    std::string wrongVersion = frame;
    wrongVersion[4] = 9;
    check("a version we do not know is refused",
          !decode_credential_frame(wrongVersion, &payload, &error), error);

    std::string huge = frame;
    huge[8] = static_cast<char>(0xff);
    huge[9] = static_cast<char>(0xff);
    check("a length larger than the maximum is refused before allocating",
          !decode_credential_frame(huge, &payload, &error), error);

    error.clear();  // or the line below reports the previous case's reason
    check("an empty payload is still a valid frame",
          decode_credential_frame(encode_credential_frame(""), &payload, &error) &&
              payload.empty(),
          error);
  }

  // ---------------------------------------------------------------- the wrong process
  //
  // The check that matters. This test process connects while the server is told to expect some
  // other process, and the credential must not be written. Driven, not asserted about: the
  // server's own error says which pid it saw, and the client observes an empty read.
  {
    const std::wstring name = make_credential_pipe_name(GetCurrentProcessId(), GetTickCount64());
    CredentialServer server;
    std::string error;
    check("the pipe is created before the child", server.Create(name, &error), error);

    // A real process that is not this one, and not the one that will connect. `expectedProcess`
    // therefore names something that never turns up.
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    wchar_t command[] = L"cmd.exe /c exit 0";
    const bool started = CreateProcessW(nullptr, command, nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                                        nullptr, nullptr, &si, &pi);
    check("a stand-in process starts", started);

    std::atomic<bool> receiverDone{false};
    std::string received = "not-cleared";
    std::string receiveError;
    std::thread receiver([&] {
      // This process connects. It is not `pi.hProcess`, so the server must refuse it.
      receiverDone = receive_credential(name, 4000, &received, &receiveError);
    });

    std::string payload = kFixtureToken;
    const bool served = server.Serve(pi.hProcess, payload, 4000, &error);
    receiver.join();

    check("a client that is not the started process is refused", !served, error);
    check("...and the refusal names the mismatch",
          error.find("not the one that was started") != std::string::npos, error);
    check("...and nothing was received", !receiverDone.load() && received.empty(), received);
    check("...and the payload was cleared on the way out", payload.empty(), payload);

    if (started) {
      WaitForSingleObject(pi.hProcess, 5000);
      CloseHandle(pi.hThread);
      CloseHandle(pi.hProcess);
    }
    server.Close();
  }

  // ---------------------------------------------------------------- the right process
  //
  // The same code path with the expectation pointing at the process that really does connect --
  // this one. Without this the refusal above would also pass for a channel that never delivers
  // anything to anybody.
  {
    const std::wstring name = make_credential_pipe_name(GetCurrentProcessId(), GetTickCount64());
    CredentialServer server;
    std::string error;
    server.Create(name, &error);

    std::string received;
    std::string receiveError;
    std::atomic<bool> ok{false};
    std::thread receiver([&] { ok = receive_credential(name, 4000, &received, &receiveError); });

    HANDLE self = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, GetCurrentProcessId());
    std::string payload = kFixtureToken;
    const bool served = server.Serve(self, payload, 4000, &error);
    receiver.join();
    if (self) CloseHandle(self);

    check("the process that was started receives it", served, error);
    check("...intact", ok.load() && received == kFixtureToken, receiveError + " / " + received);
    check("...and the sender's copy is gone", payload.empty(), payload);
    server.Close();
  }

  // ---------------------------------------------------------------- the name cannot be squatted
  {
    const std::wstring name = make_credential_pipe_name(GetCurrentProcessId(), GetTickCount64());
    CredentialServer first;
    CredentialServer second;
    std::string error;
    check("the first server takes the name", first.Create(name, &error), error);
    check("a second cannot take the same name", !second.Create(name, &error), error);
    first.Close();
  }

  // ---------------------------------------------------------------- nobody turns up
  {
    const std::wstring name = make_credential_pipe_name(GetCurrentProcessId(), GetTickCount64());
    CredentialServer server;
    std::string error;
    server.Create(name, &error);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    wchar_t command[] = L"cmd.exe /c exit 0";
    CreateProcessW(nullptr, command, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                   &si, &pi);

    const DWORD began = GetTickCount();
    std::string payload = kFixtureToken;
    const bool served = server.Serve(pi.hProcess, payload, 400, &error);
    const DWORD elapsed = GetTickCount() - began;

    check("a client that never connects is a failure, not a wait", !served, error);
    check("...bounded by the deadline it was given", elapsed < 3000, std::to_string(elapsed));
    check("...and nothing was left to send later", payload.empty());

    WaitForSingleObject(pi.hProcess, 5000);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    server.Close();
  }

  std::printf(gFailures == 0 ? "\nall credential channel checks passed\n" : "\n%d FAILED\n",
              gFailures);
  return gFailures == 0 ? 0 : 1;
}
