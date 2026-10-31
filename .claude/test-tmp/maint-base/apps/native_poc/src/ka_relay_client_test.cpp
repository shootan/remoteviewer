// keepalive r3 §5: a second client through the fixture directory's RELAY -- the "other device" that takes a
// host from a parked phone. TEST ONLY, never packaged. The product's own directory calls (login, list,
// observe, connect), rendezvous (observe, punch) and client session; only the relay candidate is used, and
// its address -- the AVD's alias of this PC (10.0.2.2) -- is reached here at 127.0.0.1.
//
//   ka_relay_client_test --dir-url URL --id ID --pw PW --host-name NAME --relay-ip IP --relay-port P
//                        [--sec N] [--out FILE]
#include "e2e_isolation.hpp"
#include "control_resume_e2e_support.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <random>
#include <string>
#include <thread>

#include "directory_client.hpp"
#include "directory_rendezvous.hpp"
#include "directory_session_client.hpp"
#include "native_video_client_session.hpp"
#include "time_utils.hpp"

using namespace remote60::native_poc;
using namespace remote60::native_poc::e2e;

namespace {
class CountSink : public ClientEncodedFrameSink {
 public:
  std::atomic<uint64_t> frames{0};
  std::atomic<uint64_t> firstUs{0};
  void OnEncodedH264Frame(UdpH264AssembledFrame&&) override {
    if (frames.fetch_add(1) == 0) firstUs.store(qpc_now_us());
  }
  void OnVideoStreamReset() override {}
};
std::string arg(int argc, char** argv, const char* name, const char* def = "") {
  for (int i = 1; i + 1 < argc; ++i) {
    if (std::string(argv[i]) == name) return argv[i + 1];
  }
  return def;
}
void say(const std::string& s) { std::printf("%s\n", s.c_str()); }
}  // namespace

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  const std::string url = arg(argc, argv, "--dir-url"), id = arg(argc, argv, "--id"), pw = arg(argc, argv, "--pw");
  const std::string hostName = arg(argc, argv, "--host-name"), relayIp = arg(argc, argv, "--relay-ip");
  const int relayPort = std::atoi(arg(argc, argv, "--relay-port", "0").c_str());
  const int sec = std::atoi(arg(argc, argv, "--sec", "20").c_str());
  const std::string outPath = arg(argc, argv, "--out");
  WSADATA wsa{};
  WSAStartup(MAKEWORD(2, 2), &wsa);

  std::string token, err;
  directory::ObserveEndpoint observe{};
  check("B signs in at the fixture directory", directory_login(url, id, pw, &token, &err, &observe), err);
  std::vector<DirectoryHostEntry> hosts;
  check("B lists the account's PCs", directory_list_hosts(url, token, &hosts, &err), err);
  std::string hostId;
  for (const auto& h : hosts) {
    if (h.hostName == hostName) hostId = h.hostId;
  }
  check("B finds " + hostName, !hostId.empty());
  std::string dirHost;
  uint16_t dirPort = 0;
  directory::parse_directory_url(url, &dirHost, &dirPort, &err);
  std::mt19937_64 rng(std::random_device{}());
  char obsToken[33];
  std::snprintf(obsToken, sizeof(obsToken), "%016llx%016llx", static_cast<unsigned long long>(rng()),
                static_cast<unsigned long long>(rng()));
  DirectoryRendezvous rv;
  std::string observed;
  const bool seen = rv.Observe(observe.host.empty() ? dirHost : observe.host, observe.port, obsToken, &observed, &err);
  check("B's media socket is observed by the directory", seen, observed + " " + err);
  DirectoryConnectTarget target;
  check("B asks the directory for the host (connect)", directory_connect(url, token, hostId, obsToken, &target, &err), err);
  std::vector<RendezvousCandidate> relay;
  std::string all;
  for (const auto& c : target.candidates) {
    all += c.ip + ":" + std::to_string(c.port) + " ";
    if (c.ip == relayIp && c.port == relayPort) relay.push_back({"127.0.0.1", c.port, "relay"});
  }
  check("the directory offered the relay; B uses only it", !relay.empty(), all);
  RendezvousCandidate chosen;
  const bool punched = !relay.empty() && rv.PunchAny(relay, 4000, &chosen, &err);
  say("NOTE  relay punch answered=" + std::to_string(punched) + " " + err);
  const SocketHandle prepared = rv.Release();
  CountSink sink;
  ClientSessionController session;
  ClientSessionConnectArgs a;
  a.host = "127.0.0.1";
  a.videoPort = relayPort;
  a.controlPort = relayPort;
  a.requireTcpControl = false;
  a.controlOverUdp = true;
  a.peerAuthToken = target.punchToken;
  a.preparedUdpSocket = prepared;
  a.encodedFrameSink = &sink;
  const uint64_t t0 = qpc_now_us();
  const bool connected = prepared != kInvalidSocket && session.Connect(a) && [&] {
    for (int i = 0; i < 150; ++i) {
      if (session.Snapshot().state == ClientSessionState::Connected) return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return false;
  }();
  check("B connects to the host through the relay", connected, session.Snapshot().status + " / " + session.Snapshot().lastError);
  for (int i = 0; i < sec * 10 && sink.frames.load() == 0; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
  const uint64_t f1 = sink.frames.load();
  check("B gets the picture (the host is B's now)", f1 > 0, std::to_string(f1) + " frames");
  std::this_thread::sleep_for(std::chrono::seconds(sec));
  const uint64_t f2 = sink.frames.load();
  const bool still = session.Snapshot().state == ClientSessionState::Connected;
  check("B is still connected and receiving at the end", still && f2 > f1,
        std::to_string(f1) + " -> " + std::to_string(f2) + " frames");
  const uint64_t firstMs = sink.firstUs.load() > t0 ? (sink.firstUs.load() - t0) / 1000 : 0;
  say("NOTE  B first frame " + std::to_string(firstMs) + " ms after connect; frames " + std::to_string(f2));
  if (!outPath.empty()) {
    std::ofstream(outPath) << "connected=" << connected << " frames=" << f2 << " firstMs=" << firstMs << " still=" << still << "\n";
  }
  session.Disconnect();
  WSACleanup();
  say(std::string("\nka_relay_client_test: ") + (gFailures == 0 ? "PASS" : "FAIL") + "\nRESULT: " +
      (gFailures == 0 ? "PASS" : "FAIL"));
  return gFailures == 0 ? 0 : 1;
}
