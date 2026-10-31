#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "capture_runtime.hpp"
#include "poc_protocol.hpp"
#include "time_utils.hpp"

namespace {

using remote60::native_poc::AckMessage;
using remote60::native_poc::FrameTickMessage;
using remote60::native_poc::MessageHeader;
using remote60::native_poc::MessageType;
using remote60::native_poc::qpc_now_us;

struct WinsockScope {
  bool ok = false;
  WinsockScope() {
    WSADATA wsa{};
    ok = (WSAStartup(MAKEWORD(2, 2), &wsa) == 0);
  }
  ~WinsockScope() {
    if (ok) WSACleanup();
  }
};

struct Args {
  std::string targetIp = "127.0.0.1";
  uint16_t targetPort = 41000;
  uint16_t bindPort = 0;
  uint32_t fps = 120;
  uint32_t seconds = 0;  // 0 means infinite
  int captureProbeSec = 0;
};

bool parse_u32(const char* s, uint32_t* out) {
  if (!s || !out) return false;
  char* end = nullptr;
  const unsigned long v = std::strtoul(s, &end, 10);
  if (!end || *end != '\0') return false;
  *out = static_cast<uint32_t>(v);
  return true;
}

Args parse_args(int argc, char** argv) {
  Args a;
  for (int i = 1; i < argc; ++i) {
    const std::string k = argv[i];
    auto next = [&](uint32_t* out) {
      if (i + 1 >= argc) return false;
      return parse_u32(argv[++i], out);
    };
    if (k == "--target" && i + 1 < argc) {
      a.targetIp = argv[++i];
    } else if (k == "--port") {
      uint32_t v = 0;
      if (next(&v)) a.targetPort = static_cast<uint16_t>(std::min<uint32_t>(v, 65535));
    } else if (k == "--bind-port") {
      uint32_t v = 0;
      if (next(&v)) a.bindPort = static_cast<uint16_t>(std::min<uint32_t>(v, 65535));
    } else if (k == "--fps") {
      uint32_t v = 0;
      if (next(&v)) a.fps = std::clamp<uint32_t>(v, 1, 240);
    } else if (k == "--seconds") {
      uint32_t v = 0;
      if (next(&v)) a.seconds = v;
    } else if (k == "--capture-probe") {
      uint32_t v = 0;
      if (next(&v)) a.captureProbeSec = static_cast<int>(std::clamp<uint32_t>(v, 1, 300));
      else a.captureProbeSec = 5;
    }
  }
  return a;
}

void run_capture_probe(int sec) {
  const auto s = remote60::host::run_capture_loop_seconds(sec);
  std::cout << "[native-host-poc] capture_probe sec=" << sec
            << " detail=" << s.detail
            << " frames=" << s.frames
            << " callbacks=" << s.callbacks
            << " fps=" << s.fps
            << " timelineFps=" << s.timelineFps
            << " convertedFrames=" << s.convertedFrames
            << " convertFailures=" << s.convertFailures
            << "\n";
}

}  // namespace

int main(int argc, char** argv) {
  const Args args = parse_args(argc, argv);
  if (args.captureProbeSec > 0) {
    run_capture_probe(args.captureProbeSec);
    return 0;
  }

  WinsockScope ws;
  if (!ws.ok) {
    std::cerr << "[native-host-poc] WSAStartup failed\n";
    return 1;
  }

  SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sock == INVALID_SOCKET) {
    std::cerr << "[native-host-poc] socket create failed\n";
    return 2;
  }

  u_long nonBlocking = 1;
  ioctlsocket(sock, FIONBIO, &nonBlocking);

  sockaddr_in local{};
  local.sin_family = AF_INET;
  local.sin_port = htons(args.bindPort);
  local.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bind(sock, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) != 0) {
    std::cerr << "[native-host-poc] bind failed port=" << args.bindPort << "\n";
    closesocket(sock);
    return 3;
  }

  sockaddr_in target{};
  target.sin_family = AF_INET;
  target.sin_port = htons(args.targetPort);
  if (inet_pton(AF_INET, args.targetIp.c_str(), &target.sin_addr) != 1) {
    std::cerr << "[native-host-poc] invalid --target " << args.targetIp << "\n";
    closesocket(sock);
    return 4;
  }

  const uint64_t frameIntervalUs = std::max<uint64_t>(1, 1000000ULL / args.fps);
  const uint64_t startUs = qpc_now_us();
  uint64_t nextSendUs = startUs;
  uint64_t statAtUs = startUs + 1000000ULL;

  uint32_t seq = 1;
  uint64_t sent = 0;
  uint64_t acked = 0;
  uint64_t droppedAck = 0;
  uint64_t rttSumUs = 0;
  uint64_t rttMaxUs = 0;
  std::unordered_map<uint32_t, uint64_t> inflight;
  inflight.reserve(4096);

  std::cout << "[native-host-poc] target=" << args.targetIp << ":" << args.targetPort
            << " bindPort=" << args.bindPort
            << " fps=" << args.fps
            << " seconds=" << args.seconds
            << "\n";

  while (true) {
    const uint64_t nowUs = qpc_now_us();
    if (args.seconds > 0 && nowUs >= (startUs + static_cast<uint64_t>(args.seconds) * 1000000ULL)) {
      break;
    }

    if (nowUs >= nextSendUs) {
      FrameTickMessage m{};
      m.header.magic = remote60::native_poc::kMagic;
      m.header.type = static_cast<uint16_t>(MessageType::FrameTick);
      m.header.size = static_cast<uint16_t>(sizeof(m));
      m.seq = seq++;
      m.sendQpcUs = nowUs;

      const int n = sendto(sock, reinterpret_cast<const char*>(&m), sizeof(m), 0,
                           reinterpret_cast<const sockaddr*>(&target), sizeof(target));
      if (n == sizeof(m)) {
        ++sent;
        inflight[m.seq] = nowUs;
      }
      nextSendUs += frameIntervalUs;
      if (nextSendUs + frameIntervalUs * 4 < nowUs) nextSendUs = nowUs + frameIntervalUs;
    }

    while (true) {
      sockaddr_in from{};
      int fromLen = sizeof(from);
      std::uint8_t buf[512]{};
      const int n = recvfrom(sock, reinterpret_cast<char*>(buf), sizeof(buf), 0,
                             reinterpret_cast<sockaddr*>(&from), &fromLen);
      if (n <= 0) break;
      if (n < static_cast<int>(sizeof(MessageHeader))) continue;
      const auto* h = reinterpret_cast<const MessageHeader*>(buf);
      if (h->magic != remote60::native_poc::kMagic) continue;
      if (h->type != static_cast<uint16_t>(MessageType::Ack)) continue;
      if (n < static_cast<int>(sizeof(AckMessage))) continue;
      const auto* a = reinterpret_cast<const AckMessage*>(buf);
      auto it = inflight.find(a->seq);
      if (it == inflight.end()) {
        ++droppedAck;
        continue;
      }
      const uint64_t rttUs = (nowUs >= it->second) ? (nowUs - it->second) : 0;
      inflight.erase(it);
      ++acked;
      rttSumUs += rttUs;
      rttMaxUs = std::max(rttMaxUs, rttUs);
    }

    if (nowUs >= statAtUs) {
      const uint64_t avgRttUs = (acked > 0) ? (rttSumUs / acked) : 0;
      std::cout << "[native-host-poc] sent=" << sent
                << " acked=" << acked
                << " inflight=" << inflight.size()
                << " ackDrop=" << droppedAck
                << " avgRttUs=" << avgRttUs
                << " maxRttUs=" << rttMaxUs
                << "\n";
      sent = 0;
      acked = 0;
      droppedAck = 0;
      rttSumUs = 0;
      rttMaxUs = 0;
      statAtUs += 1000000ULL;
    }

    Sleep(1);
  }

  closesocket(sock);
  std::cout << "[native-host-poc] done\n";
  return 0;
}
