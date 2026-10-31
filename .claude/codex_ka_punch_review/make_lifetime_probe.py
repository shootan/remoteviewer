from pathlib import Path
import re

root = Path('D:/remote/remote/.claude/codex_ka_punch_review')
source = Path('C:/work/gnlink-wt/ka-r3/apps/native_poc/src/directory_client.cpp').read_text(encoding='utf-8')
methods = []
for marker in ('void HostAgent::Stop()', 'void HostAgent::PunchLoop()',
               'void HostAgent::SetStatus(', 'void HostAgent::Punch('):
    begin = source.index(marker)
    end = source.index('\n}', begin) + 2
    methods.append(source[begin:end])
constants = '\n'.join(re.findall(r'^constexpr int kPunch(?:Packets|IntervalMs) = .*?;', source, re.MULTILINE))
cpp = r'''// Review only: unmodified method bodies extracted from fixed a7773e6; no network/HTTP.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "directory_observe.hpp"
#include "host_diag_log.hpp"
#include "punch_reply.hpp"
#include "update_endpoint.hpp"
#define private public
#include "directory_client.hpp"
#undef private
#include "poc_protocol.hpp"
namespace remote60::native_poc::directory {
namespace {
__CONSTANTS__
}
HostAgent::~HostAgent() { Stop(); }
__METHODS__
}
int main() {
  using remote60::native_poc::directory::HostAgent;
  HostAgent a;
  HostAgent::PunchTarget old;
  old.ipv4NetworkOrder = htonl(INADDR_LOOPBACK);
  old.port = 54321;
  old.hostProof = "old-owner-host-proof";
  std::atomic<int> oldSends{0}, newSends{0};
  a.send_ = [&](const void*, size_t, const sockaddr_in&) { ++oldSends; };
  a.running_.store(true);
  a.punchThread_ = std::thread([&] { a.PunchLoop(); });
  a.Punch({old});
  const auto waitFor = [](const std::atomic<int>& n) {
    const ULONGLONG t = GetTickCount64();
    while (n.load() == 0 && GetTickCount64() - t < 1000) Sleep(1);
    return n.load() != 0;
  };
  if (!waitFor(oldSends)) ExitProcess(2);
  a.Stop();
  size_t jobs;
  int left = 0;
  { std::lock_guard<std::mutex> lock(a.punchMu_);
    jobs = a.punchJobs_.size();
    if (jobs) left = a.punchJobs_[0].left;
  }
  // The Start() assignments which restart the punch worker. Full Start/HTTP is not executed.
  a.send_ = [&](const void* p, size_t len, const sockaddr_in& to) {
    auto* packet = static_cast<const remote60::native_poc::UdpHelloPacket*>(p);
    if (len == sizeof(*packet) && ntohs(to.sin_port) == old.port &&
        std::string(packet->authToken) == old.hostProof) ++newSends;
  };
  a.running_.store(true);
  a.punchThread_ = std::thread([&] { a.PunchLoop(); });
  const bool stale = waitFor(newSends);
  a.Stop();
  std::printf("REVIEW pending_jobs_after_stop=%zu packets_left=%d stale_sends_after_worker_restart=%d\n",
              jobs, left, newSends.load());
  return jobs > 0 && stale ? 0 : 1; // 0 = reproduced, not product PASS
}
'''.replace('__CONSTANTS__', constants).replace('__METHODS__', '\n\n'.join(methods))
(root / 'lifetime_probe.cpp').write_text(cpp, encoding='utf-8', newline='\n')
print('generated exact method bodies:', len(methods))
