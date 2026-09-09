// The two hops, in real processes, so a test can drive the chain rather than one link of it.
//
// This is a test helper and ships nowhere. It links the product's update_credential_channel.cpp
// and performs the same sequence updater_main.cpp performs -- receive on the pipe the parent
// named, create a pipe of its own, launch the next process, keep that process's handle for the
// whole exchange, serve, and only then let go.
//
// What it therefore covers: the channel across two real pipes and three real processes, and the
// failure shapes that only exist between processes (a bootstrap that dies before serving, a worker
// that never acknowledges, a deadline that has to end a wait rather than extend it).
//
// What it does NOT cover, said here because the difference matters: updater_main's own sequencing
// around this -- its exit codes, its working-copy checks, its refusal to signal ready without a
// credential. Running the real updater in a test would have it copy itself into a
// administrators-only directory and stop product processes, and a test does not get to do that.
//
// The only credential involved is whatever the test passes in, which is a fixture string.

#include <windows.h>

#include <cstdio>
#include <string>
#include <vector>

#include "update_credential_channel.hpp"
#include "update_endpoint.hpp"

namespace {

std::string narrow(const std::wstring& text) {
  if (text.empty()) return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                                       nullptr, 0, nullptr, nullptr);
  std::string out(size <= 0 ? 0 : static_cast<size_t>(size), '\0');
  if (size > 0) {
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), size,
                        nullptr, nullptr);
  }
  return out;
}

std::wstring value_of(const std::vector<std::wstring>& args, const wchar_t* name) {
  for (size_t i = 0; i + 1 < args.size(); ++i) {
    if (args[i] == name) return args[i + 1];
  }
  return {};
}

bool has_flag(const std::vector<std::wstring>& args, const wchar_t* name) {
  for (const std::wstring& arg : args) {
    if (arg == name) return true;
  }
  return false;
}

/** Appends a line to the record the test reads afterwards. */
void record(const std::wstring& path, const std::string& line) {
  if (path.empty()) return;
  FILE* file = nullptr;
  if (_wfopen_s(&file, path.c_str(), L"ab") != 0 || !file) return;
  fprintf(file, "%s\n", line.c_str());
  fclose(file);
}

/**
 * A log file written the way updater_main writes one: timestamped lines, built from the same
 * error strings. The point of writing one here is that a test can then READ it -- "the message
 * does not quote the credential" and "the file on disk does not contain it" are different
 * statements, and only the second one is about a file.
 */
void log_line(const std::wstring& logPath, const std::string& text) {
  if (logPath.empty()) return;
  FILE* file = nullptr;
  if (_wfopen_s(&file, logPath.c_str(), L"ab") != 0 || !file) return;
  SYSTEMTIME now{};
  GetLocalTime(&now);
  fprintf(file, "%02d:%02d:%02d [cred-hop] %s\n", now.wHour, now.wMinute,
          now.wSecond, text.c_str());
  fclose(file);
}

/** Everything this process was given, so the test can look for what should not be in it. */
void record_surroundings(const std::wstring& path) {
  record(path, std::string("cmdline=") + narrow(GetCommandLineW()));
  const wchar_t* block = GetEnvironmentStringsW();
  if (!block) return;
  std::string all;
  for (const wchar_t* at = block; *at;) {
    const std::wstring entry(at);
    all += narrow(entry);
    all += ';';
    at += entry.size() + 1;
  }
  FreeEnvironmentStringsW(const_cast<wchar_t*>(block));
  record(path, "env=" + all);
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  using namespace remote60::native_poc::update;

  std::vector<std::wstring> args;
  for (int i = 1; i < argc; ++i) args.push_back(argv[i]);

  const std::wstring role = value_of(args, L"--role");
  const std::wstring inPipe = value_of(args, L"--in");
  // One file per process. Three processes appending to one file is a race, and the loser is a
  // half-written line that reads as a missing one -- a test that then fails only under load,
  // which is the kind that gets called flaky instead of read.
  const std::wstring recordBase = value_of(args, L"--record");
  const std::wstring recordPath = recordBase.empty() ? std::wstring() : recordBase + L"." + role;
  const uint32_t deadline =
      value_of(args, L"--deadline").empty()
          ? 5000
          : static_cast<uint32_t>(_wtoi(value_of(args, L"--deadline").c_str()));

  const std::wstring logBase = value_of(args, L"--log");
  const std::wstring logPath = logBase.empty() ? std::wstring() : logBase + L"." + role;

  record_surroundings(recordPath);
  log_line(logPath, "starting role=" + narrow(role));

  if (role == L"worker") {
    if (has_flag(args, L"--never-read")) {
      // A worker that connects and then says nothing, so the sender's wait for an acknowledgement
      // is the thing under test rather than the write.
      Sleep(deadline + 2000);
      return 0;
    }
    std::string payload;
    std::string error;
    if (!receive_credential(inPipe, deadline, &payload, &error)) {
      log_line(logPath, "no credential arrived: " + error);
      record(recordPath, "worker-failed=" + error);
      return 4;
    }
    // The log gets the fact, never the value -- that is the whole discipline being tested.
    log_line(logPath, "credential received");
    // The same check updater_main makes: the frame and the arguments have to agree. The frame
    // arrived over a channel that proved who was listening; the arguments proved nothing, so
    // neither is trusted alone and a disagreement is the answer.
    const std::wstring expectUrl = value_of(args, L"--expect-url");
    if (!expectUrl.empty()) {
      UpdateEndpoint fromFrame;
      std::string decodeError;
      if (!decode_update_descriptor(payload, &fromFrame, &decodeError)) {
        log_line(logPath, "the credential frame did not parse: " + decodeError);
        record(recordPath, "worker-frame-bad=" + decodeError);
        return 6;
      }
      if (fromFrame.url != narrow(expectUrl)) {
        log_line(logPath, "the credential was issued for a different url than this run was given");
        record(recordPath, "worker-url-mismatch=1");
        return 6;
      }
      record(recordPath, "worker-owner=" + fromFrame.ownerKey + "/" +
                             std::to_string(fromFrame.ownerEpoch));
    }
    record(recordPath, "worker-received=" + payload);
    return 0;
  }

  if (role != L"bootstrap") {
    std::fprintf(stderr, "unknown role\n");
    return 2;
  }

  std::string credential;
  std::string error;
  if (!receive_credential(inPipe, deadline, &credential, &error)) {
    log_line(logPath, "no credential arrived: " + error);
    record(recordPath, "bootstrap-failed=" + error);
    return 5;
  }
  record(recordPath, "bootstrap-received=1");  // never the value: this file is read by a test

  if (has_flag(args, L"--die-after-receive")) {
    // The shape where the credential has left the parent and the next hop never happens.
    return 6;
  }

  const std::wstring childPipe =
      make_credential_pipe_name(GetCurrentProcessId(), GetTickCount64());
  CredentialServer server;
  if (!server.Create(childPipe, &error)) {
    record(recordPath, "bootstrap-no-pipe=" + error);
    return 7;
  }

  std::wstring command = L"\"" + value_of(args, L"--child") + L"\" --role worker --in \"" +
                         childPipe + L"\"";
  if (!recordBase.empty()) command += L" --record \"" + recordBase + L"\"";
  if (!logBase.empty()) command += L" --log \"" + logBase + L"\"";
  if (has_flag(args, L"--child-never-reads")) command += L" --never-read";
  if (!value_of(args, L"--expect-url").empty()) {
    command += L" --expect-url \"" + value_of(args, L"--expect-url") + L"\"";
  }
  command += L" --deadline " + std::to_wstring(deadline);

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  std::vector<wchar_t> mutableCommand(command.begin(), command.end());
  mutableCommand.push_back(L'\0');
  if (!CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                      nullptr, nullptr, &si, &pi)) {
    record(recordPath, "bootstrap-no-child=1");
    return 8;
  }
  CloseHandle(pi.hThread);

  // The handle stays open across the whole exchange -- that is what keeps this process id pinned
  // to the process it names.
  const bool served = server.Serve(pi.hProcess, credential, deadline, &error);
  log_line(logPath, served ? "handed the credential to the working copy"
                           : "the working copy did not receive the credential: " + error);
  record(recordPath, served ? "bootstrap-served=1" : "bootstrap-serve-failed=" + error);
  WaitForSingleObject(pi.hProcess, deadline);
  CloseHandle(pi.hProcess);
  server.Close();
  return served ? 0 : 9;
}
