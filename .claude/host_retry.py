import io

p = 'apps/native_poc/src/directory_retry_test.cpp'
d = io.open(p, encoding='utf-8').read()

anchor = '''  WSACleanup();
  std::printf(gFailures == 0 ? "\\nall retry checks passed\\n" : "\\n%d FAILED\\n", gFailures);'''
assert d.count(anchor) == 1, 'tail'

added = '''  // ------------------------------------------------------------- the host's retry, end to end
  //
  // Through HostAgent itself, not a piece of it: register, observe, heartbeat, and the 409 that
  // arrives in the middle. Two things must be true and only one of them is about the outcome.
  //
  // The heartbeat has to be sent twice -- once refused, once accepted -- and the host must NOT
  // register again. A 401 clears the cached token and re-registers, which is right for a token
  // the server has forgotten and wrong for this: nothing is wrong with the token, and dropping it
  // would lose the one thing that lets an unattended PC come back without someone walking to it.
  {
    FakeDirectory dir;
    dir.Start();
    dir.Script("/api/host/register",
               {Reply{200, "{\\"hostId\\":\\"h1\\",\\"hostToken\\":\\"" + std::string(32, 'b') + "\\"}"}});
    dir.Script("/api/host/heartbeat", {Reply{409, "{\\"error\\":\\"observation_required\\"}"},
                                       Reply{200, "{\\"ok\\":true}"}});
    RunHost(dir, "409-then-200");

    check("the host heartbeats again after a 409", dir.Count("/api/host/heartbeat") == 2,
          std::to_string(dir.Count("/api/host/heartbeat")));
    check("...and does not register again, so the cached token survives",
          dir.Count("/api/host/register") == 1,
          std::to_string(dir.Count("/api/host/register")));
    check("...and it observed more than once", dir.ObserveProbes() >= 2,
          std::to_string(dir.ObserveProbes()));
    dir.Stop();
  }

  {
    // The contrast that gives the assertion above its meaning: 401 IS the token being gone, and
    // it does re-register. If both statuses took the same path, the test above would pass for a
    // client that treated every refusal as a lost token.
    FakeDirectory dir;
    dir.Start();
    dir.Script("/api/host/register",
               {Reply{200, "{\\"hostId\\":\\"h1\\",\\"hostToken\\":\\"" + std::string(32, 'b') + "\\"}"}});
    dir.Script("/api/host/heartbeat", {Reply{401, "{\\"error\\":\\"unknown host token\\"}"},
                                       Reply{200, "{\\"ok\\":true}"}});
    RunHost(dir, "401");

    check("a 401 does register again", dir.Count("/api/host/register") >= 2,
          std::to_string(dir.Count("/api/host/register")));
    dir.Stop();
  }

''' + anchor

d = d.replace(anchor, added, 1)

# The helper that drives a HostAgent against the fake directory.
helper_anchor = '''}  // namespace

int main() {'''
helper = '''/**
 * Runs a HostAgent against the fake directory for a few seconds.
 *
 * The agent never owns a socket -- the address the directory observes has to be the one media
 * arrives on -- so the test supplies one, forwards what the agent sends, and feeds the replies
 * back in through ConsumeUdpPacket. That is the same wiring the real host has.
 */
void RunHost(FakeDirectory& dir, const char* label) {
  SOCKET media = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  sockaddr_in bindAddr{};
  bindAddr.sin_family = AF_INET;
  bindAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  bind(media, reinterpret_cast<sockaddr*>(&bindAddr), sizeof(bindAddr));
  DWORD timeout = 200;
  setsockopt(media, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout),
             sizeof(timeout));

  remote60::native_poc::directory::HostAgent agent;
  std::atomic<bool> pumping{true};
  std::thread pump([&] {
    char buf[2048];
    while (pumping.load()) {
      sockaddr_in from{};
      int fromLen = sizeof(from);
      const int n = recvfrom(media, buf, sizeof(buf), 0, reinterpret_cast<sockaddr*>(&from),
                             &fromLen);
      if (n > 0) agent.ConsumeUdpPacket(buf, static_cast<size_t>(n), from);
    }
  });

  remote60::native_poc::directory::HostAgentConfig cfg;
  cfg.url = dir.url();
  cfg.accountId = "tester";
  cfg.password = "test-pass-1234";
  cfg.hostName = "Fixture PC";
  // Beside this executable and unique per case, so no run reads another's cached token and
  // nothing outside the build tree is written.
  cfg.cachePath = "retry-fixture-" + std::string(label) + ".json";
  cfg.observeUdpPort = dir.udpPort();
  cfg.heartbeatSeconds = 5;

  std::string error;
  const bool started = agent.Start(cfg, [&](const void* data, size_t len, const sockaddr_in& to) {
    sendto(media, static_cast<const char*>(data), static_cast<int>(len), 0,
           reinterpret_cast<const sockaddr*>(&to), sizeof(to));
  }, &error);
  check((std::string("the host agent starts (") + label + ")").c_str(), started, error);

  // Long enough for register + observe + the refused heartbeat + the retry, and no longer: the
  // next heartbeat is five seconds out, so nothing else can arrive inside this window.
  for (int i = 0; i < 40 && dir.Count("/api/host/heartbeat") < 2; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(300));

  agent.Stop();
  pumping = false;
  closesocket(media);
  if (pump.joinable()) pump.join();
  DeleteFileA(cfg.cachePath.c_str());
}

}  // namespace

int main() {'''
assert d.count(helper_anchor) == 1, 'helper anchor'
d = d.replace(helper_anchor, helper, 1)

d = d.replace('#include <atomic>', '#include <atomic>\n#include <chrono>', 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('host retry added')
