// The chain, across three processes and two pipes.
//
// The primitive's own test drives each check in one process. That establishes the links; it does
// not establish that the chain runs. The failures that only exist between processes are the ones
// this file is for: a bootstrap that dies with the credential, a receiver that never
// acknowledges, a deadline that has to end a wait instead of extending it -- and the ordinary
// case, without which every refusal above would also pass for a chain that delivers nothing.
//
// The helper performs the same sequence updater_main performs. It is not updater_main: running
// the real updater in a test would have it copy itself into an administrators-only directory and
// stop product processes, and a test does not get to do either. What is covered is the channel
// between real processes; what is not is the updater's sequencing around it, and that is said
// again in the helper's own header.
//
// Everything is on this machine, under a directory named for this run, and removed afterwards.
// The only credential is a fixture string that appears nowhere else.

// directory_client.hpp pulls in winsock2, which must be seen before windows.h -- otherwise
// windows.h drags in the older winsock and the two redefine each other.
#include "directory_client.hpp"

#include <windows.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "update_credential_channel.hpp"
#include "update_endpoint.hpp"

namespace {

int gFailures = 0;

void check(const char* name, bool cond, const std::string& detail = {}) {
  std::printf("%s  %s%s%s\n", cond ? "PASS" : "FAIL", name, detail.empty() ? "" : "  ",
              detail.c_str());
  if (!cond) ++gFailures;
}

const char* kFixtureToken = "x-host-token: fixture-token-not-a-real-credential";

std::wstring exe_directory() {
  wchar_t path[MAX_PATH]{};
  GetModuleFileNameW(nullptr, path, MAX_PATH);
  std::wstring text(path);
  const size_t slash = text.find_last_of(L"\\/");
  return slash == std::wstring::npos ? L"." : text.substr(0, slash);
}

std::wstring widen(const std::string& text) {
  return std::wstring(text.begin(), text.end());  // ascii urls only, which is all these are
}

std::string read_one(const std::wstring& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) return {};
  std::ostringstream out;
  out << file.rdbuf();
  return out.str();
}

/** Each process writes its own file; the test reads them together. */
std::string read_all(const std::wstring& base) {
  return read_one(base + L".bootstrap") + read_one(base + L".worker");
}

/** True when the fixture credential is nowhere in a file that was actually written to disk. */
bool file_is_clean(const std::wstring& path) {
  return read_one(path).find("fixture-token-not-a-real-credential") == std::string::npos;
}

struct Child {
  PROCESS_INFORMATION pi{};
  bool started = false;
  ~Child() {
    if (started) {
      CloseHandle(pi.hThread);
      CloseHandle(pi.hProcess);
    }
  }
};

bool start(const std::wstring& command, Child* child) {
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  std::vector<wchar_t> mutableCommand(command.begin(), command.end());
  mutableCommand.push_back(L'\0');
  child->started = CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE,
                                  CREATE_NO_WINDOW, nullptr, nullptr, &si, &child->pi) != FALSE;
  return child->started;
}

DWORD wait_for(HANDLE process, DWORD ms) {
  if (WaitForSingleObject(process, ms) != WAIT_OBJECT_0) return 0xFFFFFFFF;
  DWORD code = 0xFFFFFFFF;
  GetExitCodeProcess(process, &code);
  return code;
}

}  // namespace

int main() {
  setvbuf(stdout, nullptr, _IONBF, 0);
  using namespace remote60::native_poc::update;

  const std::wstring helper = exe_directory() + L"\\remote60_cred_hop_helper.exe";
  if (GetFileAttributesW(helper.c_str()) == INVALID_FILE_ATTRIBUTES) {
    std::printf("FAIL  the hop helper is built beside this test\n");
    return 1;
  }

  const std::wstring dir =
      exe_directory() + L"\\cred-hops-" + std::to_wstring(GetCurrentProcessId());
  CreateDirectoryW(dir.c_str(), nullptr);

  // ---------------------------------------------------------------- the chain runs
  {
    const std::wstring recordPath = dir + L"\\ok.txt";
    const std::wstring pipeName =
        make_credential_pipe_name(GetCurrentProcessId(), GetTickCount64());
    CredentialServer server;
    std::string error;
    check("the parent's pipe is created before the bootstrap", server.Create(pipeName, &error),
          error);

    const std::wstring logPath = dir + L"\\ok.log";
    Child bootstrap;
    const std::wstring command = L"\"" + helper + L"\" --role bootstrap --in \"" + pipeName +
                                 L"\" --child \"" + helper + L"\" --record \"" + recordPath +
                                 L"\" --log \"" + logPath + L"\" --deadline 8000";
    check("the bootstrap starts", start(command, &bootstrap));

    std::string payload = kFixtureToken;
    const bool served = server.Serve(bootstrap.pi.hProcess, payload, 8000, &error);
    check("hop 1 delivers", served, error);
    const DWORD code = wait_for(bootstrap.pi.hProcess, 15000);
    check("the bootstrap finishes cleanly", code == 0, std::to_string(code));
    server.Close();

    const std::string record = read_all(recordPath);
    check("the bootstrap received it", record.find("bootstrap-received=1") != std::string::npos);
    check("hop 2 delivers", record.find("bootstrap-served=1") != std::string::npos);
    check("the worker got the credential intact",
          record.find(std::string("worker-received=") + kFixtureToken) != std::string::npos);

    // ---- and nothing carried it that should not have
    //
    // The record holds each process's own command line and environment block, written by the
    // process itself. A credential that had been passed either way would be in here.
    // The only line that may contain the token is the worker's own report of what it received.
    {
      std::istringstream lines(record);
      std::string line;
      bool leaked = false;
      while (std::getline(lines, line)) {
        if (line.rfind("worker-received=", 0) == 0) continue;
        if (line.find("fixture-token-not-a-real-credential") != std::string::npos) leaked = true;
      }
      check("the credential is in no command line and no environment block", !leaked);
    }

    // A log file, written to disk from the same error strings an updater's log is written from,
    // and then read back. "The message does not quote it" and "the file does not contain it" are
    // different statements, and only the second one is about a file.
    check("both processes wrote a log",
          !read_one(logPath + L".bootstrap").empty() && !read_one(logPath + L".worker").empty());
    check("...and the credential is in neither",
          file_is_clean(logPath + L".bootstrap") && file_is_clean(logPath + L".worker"));
    check("...while the log still says what happened",
          read_one(logPath + L".worker").find("credential received") != std::string::npos);
  }

  // ---------------------------------------------------------------- the bootstrap dies with it
  {
    const std::wstring recordPath = dir + L"\\die.txt";
    const std::wstring pipeName =
        make_credential_pipe_name(GetCurrentProcessId(), GetTickCount64());
    CredentialServer server;
    std::string error;
    server.Create(pipeName, &error);

    Child bootstrap;
    const std::wstring command = L"\"" + helper + L"\" --role bootstrap --in \"" + pipeName +
                                 L"\" --child \"" + helper + L"\" --record \"" + recordPath +
                                 L"\" --deadline 4000 --die-after-receive";
    start(command, &bootstrap);

    std::string payload = kFixtureToken;
    const bool served = server.Serve(bootstrap.pi.hProcess, payload, 8000, &error);
    const DWORD code = wait_for(bootstrap.pi.hProcess, 10000);
    server.Close();

    // Hop 1 did happen; hop 2 did not. The parent cannot tell those apart from its side, which is
    // exactly why the exit code has to say so rather than the absence of a complaint.
    check("hop 1 still reports delivery", served, error);
    check("...and the bootstrap's own exit says it went no further", code == 6,
          std::to_string(code));
    const std::string record = read_all(recordPath);
    check("nothing reached a worker", record.find("worker-received=") == std::string::npos);
  }

  // ---------------------------------------------------------------- nobody acknowledges
  {
    const std::wstring recordPath = dir + L"\\noack.txt";
    const std::wstring noackLog = dir + L"\\noack.log";
    const std::wstring pipeName =
        make_credential_pipe_name(GetCurrentProcessId(), GetTickCount64());
    CredentialServer server;
    std::string error;
    server.Create(pipeName, &error);

    Child bootstrap;
    const std::wstring command = L"\"" + helper + L"\" --role bootstrap --in \"" + pipeName +
                                 L"\" --child \"" + helper + L"\" --record \"" + recordPath +
                                 L"\" --log \"" + noackLog +
                                 L"\" --deadline 1500 --child-never-reads";
    start(command, &bootstrap);

    std::string payload = kFixtureToken;
    server.Serve(bootstrap.pi.hProcess, payload, 8000, &error);
    const DWORD began = GetTickCount();
    const DWORD code = wait_for(bootstrap.pi.hProcess, 20000);
    const DWORD elapsed = GetTickCount() - began;
    server.Close();

    check("a receiver that never reads makes hop 2 fail", code == 9, std::to_string(code));
    check("...within the deadline rather than for ever", elapsed < 15000, std::to_string(elapsed));
    const std::string record = read_all(recordPath);
    check("...and the failure is recorded as a failure",
          record.find("bootstrap-serve-failed=") != std::string::npos);
    // The failure path writes the reason to the log. That reason must not carry the value.
    check("...and that log line does not quote the credential",
          file_is_clean(noackLog + L".bootstrap"));
  }

  // ---------------------------------------------------------------- the parent never serves
  {
    const std::wstring recordPath = dir + L"\\silent.txt";
    const std::wstring pipeName =
        make_credential_pipe_name(GetCurrentProcessId(), GetTickCount64());
    CredentialServer server;
    std::string error;
    server.Create(pipeName, &error);

    Child bootstrap;
    const std::wstring command = L"\"" + helper + L"\" --role bootstrap --in \"" + pipeName +
                                 L"\" --child \"" + helper + L"\" --record \"" + recordPath +
                                 L"\" --deadline 1200";
    start(command, &bootstrap);
    // The parent creates the pipe and then says nothing at all.
    const DWORD code = wait_for(bootstrap.pi.hProcess, 20000);
    server.Close();

    check("a bootstrap that is never served gives up", code == 5, std::to_string(code));
    const std::string record = read_all(recordPath);
    check("...and says why", record.find("bootstrap-failed=") != std::string::npos);
    check("...and no worker was ever started",
          record.find("worker-received=") == std::string::npos);
  }

  // ------------------------------------------------- the owner decides, across real processes
  //
  // The decision itself has unit coverage. What was missing is what the PROCESSES do when it goes
  // each way -- and the two outcomes are supposed to look completely different from the outside:
  // one ends with a worker holding a credential, the other with a bootstrap that gave up and no
  // worker at all.
  //
  // The endpoints here are built by the product's own builder, so the comparison under test is
  // the one the client and the host run.
  {
    using remote60::native_poc::directory::update_endpoint_for;  // the product's own builder

    const auto atLaunch = update_endpoint_for("", "https://rem.example", "windows",
                                              "Authorization: Bearer aaa", "alice", 7);

    struct Case {
      const char* name;
      remote60::native_poc::update::UpdateEndpoint now;
      bool shouldSend;
    };
    const Case cases[] = {
        {"unchanged", atLaunch, true},
        {"same account signed out and back in",
         update_endpoint_for("", "https://rem.example", "windows",
                             "Authorization: Bearer bbb", "alice", 8),
         false},
        {"a different account on the same server",
         update_endpoint_for("", "https://rem.example", "windows",
                             "Authorization: Bearer ccc", "bob", 7),
         false},
        {"signed out entirely",
         update_endpoint_for("", "https://rem.example", "windows", "", "", 0), false},
        {"the server was changed",
         update_endpoint_for("", "https://other.example", "windows",
                             "Authorization: Bearer aaa", "alice", 7),
         false},
    };

    for (const Case& c : cases) {
      const std::wstring recordPath = dir + L"\\owner-" + std::to_wstring(&c - cases) + L".txt";
      const std::wstring pipeName =
          make_credential_pipe_name(GetCurrentProcessId(), GetTickCount64());
      CredentialServer server;
      std::string error;
      server.Create(pipeName, &error);

      Child bootstrap;
      const std::wstring command = L"\"" + helper + L"\" --role bootstrap --in \"" + pipeName +
                                   L"\" --child \"" + helper + L"\" --record \"" + recordPath +
                                   L"\" --deadline 2500";
      start(command, &bootstrap);

      // Exactly the rule the product applies before sending, run here against real processes.
      std::string payload;
      if (c.now.same_owner_as(atLaunch) && c.now.url == atLaunch.url &&
          c.now.origin == atLaunch.origin && !c.now.credentialHeader.empty()) {
        payload = remote60::native_poc::update::encode_update_descriptor(c.now);
      }

      bool served = false;
      if (!payload.empty()) served = server.Serve(bootstrap.pi.hProcess, payload, 5000, &error);
      const DWORD code = wait_for(bootstrap.pi.hProcess, 15000);
      server.Close();
      const std::string record = read_all(recordPath);

      if (c.shouldSend) {
        check((std::string(c.name) + ": the credential goes out").c_str(), served, error);
        check((std::string(c.name) + ": and a worker receives it").c_str(),
              record.find("worker-received=") != std::string::npos);
        check((std::string(c.name) + ": and the bootstrap finishes cleanly").c_str(), code == 0,
              std::to_string(code));
      } else {
        check((std::string(c.name) + ": nothing is sent").c_str(), !served && payload.empty());
        // The visible end of it: the bootstrap waits, gets nothing, and gives up without ever
        // starting a worker. An attempt whose owner is gone installs nothing.
        check((std::string(c.name) + ": the bootstrap gives up").c_str(), code == 5,
              std::to_string(code));
        check((std::string(c.name) + ": and no worker was started").c_str(),
              record.find("worker-received=") == std::string::npos);
      }
    }
  }

  // ------------------------------------------------- a descriptor for a different url is refused
  //
  // The frame arrived over a channel that proved who was listening; the arguments proved nothing.
  // Neither is trusted alone, so a disagreement between them ends the run rather than being
  // resolved in favour of one of them.
  {
    using remote60::native_poc::directory::update_endpoint_for;  // the product's own builder
    const auto endpoint = update_endpoint_for("", "https://rem.example", "windows",
                                              "Authorization: Bearer aaa", "alice", 7);

    const std::wstring recordPath = dir + L"\\mismatch.txt";
    const std::wstring pipeName =
        make_credential_pipe_name(GetCurrentProcessId(), GetTickCount64());
    CredentialServer server;
    std::string error;
    server.Create(pipeName, &error);

    Child bootstrap;
    // The worker is told to expect a different url than the frame names.
    const std::wstring command = L"\"" + helper + L"\" --role bootstrap --in \"" + pipeName +
                                 L"\" --child \"" + helper + L"\" --record \"" + recordPath +
                                 L"\" --expect-url \"https://elsewhere.example\" --deadline 5000";
    start(command, &bootstrap);

    std::string payload = remote60::native_poc::update::encode_update_descriptor(endpoint);
    server.Serve(bootstrap.pi.hProcess, payload, 5000, &error);
    wait_for(bootstrap.pi.hProcess, 15000);
    server.Close();

    const std::string record = read_all(recordPath);
    check("a frame naming another url is refused by the worker",
          record.find("worker-url-mismatch=1") != std::string::npos, record.substr(0, 200));
    check("...and the credential is not treated as received",
          record.find("worker-received=") == std::string::npos);
  }

  // ------------------------------------------------- the frame carries who authorised the run
  {
    using remote60::native_poc::directory::update_endpoint_for;  // the product's own builder
    const auto endpoint = update_endpoint_for("", "https://rem.example", "windows",
                                              "Authorization: Bearer aaa", "alice", 7);
    const std::wstring recordPath = dir + L"\\owner-frame.txt";
    const std::wstring pipeName =
        make_credential_pipe_name(GetCurrentProcessId(), GetTickCount64());
    CredentialServer server;
    std::string error;
    server.Create(pipeName, &error);

    Child bootstrap;
    const std::wstring command = L"\"" + helper + L"\" --role bootstrap --in \"" + pipeName +
                                 L"\" --child \"" + helper + L"\" --record \"" + recordPath +
                                 L"\" --expect-url \"" + widen(endpoint.url) +
                                 L"\" --deadline 5000";
    start(command, &bootstrap);

    std::string payload = remote60::native_poc::update::encode_update_descriptor(endpoint);
    const bool served = server.Serve(bootstrap.pi.hProcess, payload, 5000, &error);
    wait_for(bootstrap.pi.hProcess, 15000);
    server.Close();

    const std::string record = read_all(recordPath);
    check("a matching frame is accepted", served && record.find("worker-received=") !=
                                              std::string::npos, error);
    check("...and the worker reads the owner out of the frame, not out of its arguments",
          record.find("worker-owner=alice/7") != std::string::npos, record.substr(0, 200));
  }

  // ---------------------------------------------------------------- teardown, path-confirmed
  if (dir.find(L"cred-hops-") != std::wstring::npos) {
    for (const wchar_t* name : {L"ok.txt", L"die.txt", L"noack.txt", L"silent.txt"}) {
      DeleteFileW((dir + L"\\" + name + L".bootstrap").c_str());
      DeleteFileW((dir + L"\\" + name + L".worker").c_str());
    }
    for (const wchar_t* name : {L"owner-0.txt", L"owner-1.txt", L"owner-2.txt", L"owner-3.txt",
                                L"owner-4.txt", L"mismatch.txt", L"owner-frame.txt"}) {
      DeleteFileW((dir + L"\\" + name + L".bootstrap").c_str());
      DeleteFileW((dir + L"\\" + name + L".worker").c_str());
    }
    for (const wchar_t* name : {L"ok.log", L"noack.log"}) {
      DeleteFileW((dir + L"\\" + name + L".bootstrap").c_str());
      DeleteFileW((dir + L"\\" + name + L".worker").c_str());
    }
    RemoveDirectoryW(dir.c_str());
  }

  std::printf(gFailures == 0 ? "\nall credential hop checks passed\n" : "\n%d FAILED\n",
              gFailures);
  return gFailures == 0 ? 0 : 1;
}
