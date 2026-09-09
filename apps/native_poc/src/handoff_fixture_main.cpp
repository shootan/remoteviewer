// Stands in for the updater in the handover test, in both of its roles.
//
// The handover involves three processes and the defect it exists to pin lived in the seam between
// them: the host, the BOOTSTRAP it starts, and the WORKING COPY the bootstrap starts. No two of
// those can show it. The bootstrap exiting immediately is correct; the worker signalling later is
// correct; the host waiting is correct. Only all three at once produce "the host is told the
// update was cancelled, and then the update stops the host".
//
// So this plays the two roles the test cannot play itself, as real processes:
//
//   --bootstrap   start a worker with the remaining arguments, then exit with --exit-code.
//                 It does NOT wait. That is not a simplification -- the real bootstrap cannot
//                 wait, because the file it is running from is one of the files being replaced.
//
//   --worker      wait --delay ms, then optionally signal --ready, then wait --ack-wait ms for
//                 the acknowledgement, and record what happened.
//
// Everything it does is recorded in --witness, so the test can tell "it decided not to proceed"
// from "it never got there" -- which are the same silence from outside.

#include <windows.h>

#include <string>
#include <vector>

#include "update_handoff.hpp"

namespace {

using namespace remote60::native_poc::update;

std::wstring value_of(const std::vector<std::wstring>& args, const wchar_t* name) {
  for (size_t i = 0; i + 1 < args.size(); ++i) {
    if (args[i] == name) return args[i + 1];
  }
  return {};
}

bool has_flag(const std::vector<std::wstring>& args, const wchar_t* name) {
  for (const std::wstring& a : args) {
    if (a == name) return true;
  }
  return false;
}

void record(const std::wstring& witness, const std::string& line) {
  if (witness.empty()) return;
  HANDLE file = CreateFileW(witness.c_str(), FILE_APPEND_DATA,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return;
  const std::string text = line + "\n";
  DWORD written = 0;
  WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
  CloseHandle(file);
}

std::wstring self_path() {
  wchar_t path[MAX_PATH]{};
  GetModuleFileNameW(nullptr, path, MAX_PATH);
  return path;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  std::vector<std::wstring> args;
  for (int i = 1; i < argc; ++i) args.push_back(argv[i]);

  const std::wstring witness = value_of(args, L"--witness");

  if (has_flag(args, L"--bootstrap")) {
    // Rebuild the command line as a worker, dropping the bootstrap flag and its exit code.
    std::wstring command = L"\"" + self_path() + L"\" --worker";
    for (size_t i = 0; i < args.size(); ++i) {
      if (args[i] == L"--bootstrap") continue;
      if (args[i] == L"--exit-code") {
        ++i;  // and its value
        continue;
      }
      command += L" \"" + args[i] + L"\"";
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    const BOOL started = CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE,
                                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    if (started) {
      record(witness, "bootstrap started the worker");
      CloseHandle(pi.hThread);
      // NOT waited on, and not held. The real bootstrap cannot hold this either.
      CloseHandle(pi.hProcess);
    } else {
      record(witness, "bootstrap could not start the worker");
    }

    const std::wstring code = value_of(args, L"--exit-code");
    const int exitCode = code.empty() ? 0 : _wtoi(code.c_str());
    record(witness, "bootstrap exiting with " + std::to_string(exitCode));
    return exitCode;
  }

  // ---------------------------------------------------------------- the working copy
  const std::wstring readyForAlive = value_of(args, L"--ready");
  // Held for this process's lifetime, exactly as the real working copy holds it. Whoever is
  // waiting learns of a death without this having to announce one -- which is the point, because
  // the death worth catching is the one that runs no cleanup.
  HANDLE alive = nullptr;
  {
    const std::wstring aliveName = make_alive_mutex_name(readyForAlive);
    if (!aliveName.empty()) alive = CreateMutexW(nullptr, TRUE, aliveName.c_str());
  }

  const std::wstring delay = value_of(args, L"--delay");
  if (!delay.empty()) Sleep(static_cast<DWORD>(_wtoi(delay.c_str())));

  if (has_flag(args, L"--die-before-signal")) {
    // Stops without releasing anything, the way a crash does. The mutex goes with the process and
    // the waiter sees it abandoned -- which is the whole reason it is a mutex.
    record(witness, "worker died before signalling");
    TerminateProcess(GetCurrentProcess(), 99);
  }

  const std::wstring readyName = value_of(args, L"--ready");
  const std::wstring ackName = make_ack_event_name(readyName);

  // The channel is opened BEFORE the signal, as the updater does. The caller closes its own
  // handle the moment it has answered, so a handle opened later can find nothing there -- the
  // named object goes with the last handle. Holding one from before the signal keeps it alive
  // across the window where both sides are letting go.
  //
  // --open-ack-late is that window, made deliberate: it is what the code did before, and it is
  // here so the difference can be measured rather than argued about.
  const bool openLate = has_flag(args, L"--open-ack-late");
  HANDLE ack = nullptr;
  if (!openLate && !ackName.empty()) ack = OpenEventW(SYNCHRONIZE, FALSE, ackName.c_str());
  record(witness, ack ? "worker holds the ack channel before signalling"
                      : (openLate ? "worker deferred opening the ack channel"
                                  : "worker could not open the ack channel"));

  bool delivered = false;
  if (!has_flag(args, L"--no-signal") && !readyName.empty()) {
    HANDLE event = OpenEventW(EVENT_MODIFY_STATE, FALSE, readyName.c_str());
    if (event) {
      delivered = SetEvent(event) != FALSE;
      CloseHandle(event);
    }
  }
  record(witness, delivered ? "worker delivered the ready signal"
                            : "worker could not deliver the ready signal");

  const std::wstring ackWait = value_of(args, L"--ack-wait");
  const DWORD ackTimeout = ackWait.empty() ? 5000 : static_cast<DWORD>(_wtoi(ackWait.c_str()));

  // Long enough, when asked for, that the caller has certainly answered and let go by now.
  const std::wstring openDelay = value_of(args, L"--ack-open-delay");
  if (!openDelay.empty()) Sleep(static_cast<DWORD>(_wtoi(openDelay.c_str())));
  if (openLate && !ackName.empty()) {
    ack = OpenEventW(SYNCHRONIZE, FALSE, ackName.c_str());
    record(witness, ack ? "worker opened the ack channel late and found it"
                        : "worker opened the ack channel late and it was gone");
  }

  bool acked = false;
  if (ack) {
    acked = WaitForSingleObject(ack, ackTimeout) == WAIT_OBJECT_0;
    CloseHandle(ack);
    ack = nullptr;
  }

  // The decision, taken by the SAME function the updater uses. A fixture that decided for itself
  // would be testing the fixture.
  std::string why;
  const bool proceed = may_stop_the_product(delivered, acked, &why);
  record(witness, std::string(proceed ? "worker WOULD stop the product" : "worker stood down")
                      + ": " + why);
  if (alive) {
    ReleaseMutex(alive);
    CloseHandle(alive);
  }
  return proceed ? 0 : 20;
}
