#include "native_socket.hpp"
#include <windows.h>
#include <cstdio>
#include <string>
#include <thread>
#include "bounded_process_exit.hpp"
#include "host_recovery_policy.hpp"
#include "host_watchdog.hpp"
#include "udp_receive_pump.hpp"
#include "udp_control_channel.hpp"
#include "bounded_pipe_io.hpp"
#include "capture_callback_gate.hpp"
#include "child_environment.hpp"

int main(int argc, char**) {
  using namespace remote60::native_poc;
  if (argc > 1) {
    // The parent never reads this pipe. The diagnostic fills it and blocks, but must not stop
    // the independently armed termination. This is a real OS pipe and a real child process.
    std::string diagnostic(1024 * 1024, 'x');
    terminate_with_diagnostic(44, diagnostic.data(), static_cast<DWORD>(diagnostic.size()));
  }
  int failures = 0;
  auto check = [&](bool ok, const char* name) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++failures;
  };
  HostRecoveryPolicy policy;
  {
    wchar_t previous[128]{};
    const DWORD size = GetEnvironmentVariableW(L"GNLINK_RECOVERY_ENV_TEST", previous, 128);
    SetEnvironmentVariableW(L"GNLINK_RECOVERY_ENV_TEST", L"parent");
    const auto child = child_environment_with(L"GNLINK_RECOVERY_ENV_TEST", L"child");
    bool found = false;
    for (const wchar_t* entry = child.data(); *entry; entry += std::wcslen(entry) + 1)
      if (std::wstring(entry) == L"GNLINK_RECOVERY_ENV_TEST=child") found = true;
    wchar_t current[128]{}; GetEnvironmentVariableW(L"GNLINK_RECOVERY_ENV_TEST", current, 128);
    check(found && std::wstring(current) == L"parent", "recovery child environment cannot contaminate parent launches");
    SetEnvironmentVariableW(L"GNLINK_RECOVERY_ENV_TEST", size > 0 && size < 128 ? previous : nullptr);
  }
  {
    auto gate = std::make_shared<CaptureCallbackGate>();
    std::atomic<bool> entered{false};
    std::thread callback([gate, &entered] {
      CaptureCallbackLease lease(gate.get());
      entered.store(static_cast<bool>(lease));
      Sleep(50);
    });
    while (!entered.load()) Sleep(1);
    gate->Close();
    check(!gate->Enter(), "late WGC callbacks cannot enter a closed attachment");
    check(gate->Drain(std::chrono::milliseconds(1000)), "capture teardown waits for active callback lease");
    callback.join();
  }
  {
    const std::wstring name = L"\\\\.\\pipe\\GNLink.RecoveryFixture." + std::to_wstring(GetCurrentProcessId());
    HANDLE server = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_INBOUND,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
        1, 1024, 1024, 0, nullptr);
    HANDLE client = CreateFileW(name.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    check(server != INVALID_HANDLE_VALUE && client != INVALID_HANDLE_VALUE, "create isolated input pipe");
    if (server != INVALID_HANDLE_VALUE && client != INVALID_HANDLE_VALUE) {
      const bool connected = ConnectNamedPipe(server, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED;
      check(connected, "connect isolated input pipe");
      std::vector<char> body(1024 * 1024, 'x');
      const uint64_t started = GetTickCount64();
      check(!write_pipe_bounded(client, body.data(), static_cast<DWORD>(body.size()), 50) &&
            GetTickCount64() - started < 1500, "non-reading input service cannot block writer indefinitely");
    }
    if (client != INVALID_HANDLE_VALUE) CloseHandle(client);
    if (server != INVALID_HANDLE_VALUE) CloseHandle(server);
    const std::wstring healthyName = name + L".healthy";
    server = CreateNamedPipeW(healthyName.c_str(), PIPE_ACCESS_INBOUND,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
        1, 1024, 1024, 0, nullptr);
    client = CreateFileW(healthyName.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    bool delivered = false;
    if (server != INVALID_HANDLE_VALUE && client != INVALID_HANDLE_VALUE) {
      ConnectNamedPipe(server, nullptr);
      const char message[] = "release";
      const bool sent = write_pipe_bounded(client, message, sizeof(message), 50);
      DWORD available = 0;
      if (sent && PeekNamedPipe(server, nullptr, 0, nullptr, &available, nullptr) && available == sizeof(message)) {
        char received[sizeof(message)]{}; DWORD read = 0;
        delivered = ReadFile(server, received, sizeof(received), &read, nullptr) &&
                    read == sizeof(message) && std::memcmp(received, message, sizeof(message)) == 0;
      }
    }
    check(delivered, "bounded pipe still delivers a healthy write exactly once");
    if (client != INVALID_HANDLE_VALUE) CloseHandle(client);
    if (server != INVALID_HANDLE_VALUE) CloseHandle(server);
  }
  {
    WinsockScope sockets;
    SOCKET receiver = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    SOCKET sender = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in address{}; address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    check(sockets.ok && receiver != INVALID_SOCKET && sender != INVALID_SOCKET &&
          bind(receiver, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0,
          "isolated UDP pump socket");
    int addressSize = sizeof(address); getsockname(receiver, reinterpret_cast<sockaddr*>(&address), &addressSize);
    set_recv_timeout(receiver, 25);
    UdpControlChannel channel;
    channel.Configure([](const void*, size_t) { return true; }, 2, 1, 1200);
    {
      UdpReceivePump pump(receiver, [&](const void* data, size_t size) { return channel.OnPacket(data, size); },
                          [&] { channel.Tick(); });
      std::vector<uint8_t> media(1000, 'v');
      // No consumer runs: simulate a decoder stalled with a full media backlog.
      for (int i = 0; i < 1200; ++i) {
        sendto(sender, reinterpret_cast<const char*>(media.data()), static_cast<int>(media.size()), 0,
               reinterpret_cast<sockaddr*>(&address), sizeof(address));
        Sleep(1);
      }
      UdpControlChunkHeader header{};
      header.magic = kMagic; header.kind = static_cast<uint16_t>(UdpPacketKind::ControlData);
      header.streamId = 1; header.messageSeq = 1; header.totalSize = 1;
      header.fragCount = 1; header.fragSize = 1;
      std::vector<uint8_t> packet(sizeof(header) + 1);
      std::memcpy(packet.data(), &header, sizeof(header)); packet.back() = 'c';
      sendto(sender, reinterpret_cast<const char*>(packet.data()), static_cast<int>(packet.size()), 0,
               reinterpret_cast<sockaddr*>(&address), sizeof(address));
      std::vector<uint8_t> control;
      check(channel.Receive(&control, 1000) && control == std::vector<uint8_t>{'c'},
            "real control message bypasses stalled media consumer");
      check(pump.Drops() > 0, "media backlog is bounded while control remains live");
    }
    closesocket(receiver); closesocket(sender);
  }
  policy.OnExit(44, 1000, 1000); check(!policy.UseWgc(1001), "one transient wedge retains backend");
  policy.OnExit(44, 1000, 3000); check(policy.UseWgc(3001), "repeat wedge selects WGC");
  check(!policy.UseWgc(303001), "backend quarantine expires");
  {
    MainLoopWatchdogThread watchdog;
    std::atomic<bool> returned{false};
    watchdog.thread = std::thread([&] {
      while (watchdog.WaitOrStop(std::chrono::milliseconds(10))) {}
      returned = true;
    });
    watchdog.RequestStop();
    watchdog.thread.join();
    check(returned, "watchdog stop wakes and joins");
  }
  SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
  HANDLE readPipe = nullptr, writePipe = nullptr;
  check(CreatePipe(&readPipe, &writePipe, &sa, 4096) != FALSE, "create isolated blocking diagnostic pipe");
  if (!readPipe) return 1;
  SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);
  wchar_t exe[MAX_PATH]{}; GetModuleFileNameW(nullptr, exe, MAX_PATH);
  std::wstring command = L"\"" + std::wstring(exe) + L"\" child";
  STARTUPINFOW si{}; si.cb = sizeof(si); si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdOutput = si.hStdError = writePipe;
  PROCESS_INFORMATION process{};
  const ULONGLONG start = GetTickCount64();
  const bool created = CreateProcessW(exe, command.data(), nullptr, nullptr, TRUE,
                                     CREATE_NO_WINDOW, nullptr, nullptr, &si, &process) != FALSE;
  CloseHandle(writePipe);
  check(created, "launch isolated child");
  if (created) {
    const DWORD waited = WaitForSingleObject(process.hProcess, 2000);
    DWORD code = 0; GetExitCodeProcess(process.hProcess, &code);
    check(waited == WAIT_OBJECT_0 && code == 44 && GetTickCount64() - start < 2000,
          "blocked diagnostic cannot prevent watchdog exit44");
    if (waited != WAIT_OBJECT_0) { TerminateProcess(process.hProcess, 99); WaitForSingleObject(process.hProcess, 2000); }
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
  }
  CloseHandle(readPipe);
  std::printf("recovery_process_test: %s failures=%d\n", failures ? "FAIL" : "PASS", failures);
  return failures ? 1 : 0;
}
