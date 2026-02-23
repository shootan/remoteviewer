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
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

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
  uint16_t listenPort = 41000;
  uint32_t seconds = 0;  // 0 means infinite
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
    if (k == "--port" && i + 1 < argc) {
      uint32_t v = 0;
      if (parse_u32(argv[++i], &v)) a.listenPort = static_cast<uint16_t>(std::min<uint32_t>(v, 65535));
    } else if (k == "--seconds" && i + 1 < argc) {
      uint32_t v = 0;
      if (parse_u32(argv[++i], &v)) a.seconds = v;
    }
  }
  return a;
}

}  // namespace

int main(int argc, char** argv) {
  const Args args = parse_args(argc, argv);

  WinsockScope ws;
  if (!ws.ok) {
    std::cerr << "[native-client-poc] WSAStartup failed\n";
    return 1;
  }

  SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sock == INVALID_SOCKET) {
    std::cerr << "[native-client-poc] socket create failed\n";
    return 2;
  }

  u_long nonBlocking = 1;
  ioctlsocket(sock, FIONBIO, &nonBlocking);

  sockaddr_in local{};
  local.sin_family = AF_INET;
  local.sin_port = htons(args.listenPort);
  local.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bind(sock, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) != 0) {
    std::cerr << "[native-client-poc] bind failed port=" << args.listenPort << "\n";
    closesocket(sock);
    return 3;
  }

  const uint64_t startUs = qpc_now_us();
  uint64_t statAtUs = startUs + 1000000ULL;
  uint64_t recvCount = 0;
  uint64_t ackSent = 0;
  uint64_t estOneWaySumUs = 0;
  uint64_t estOneWayMaxUs = 0;
  uint32_t lastSeq = 0;
  uint64_t seqGap = 0;

  std::cout << "[native-client-poc] listenPort=" << args.listenPort
            << " seconds=" << args.seconds
            << "\n";

  while (true) {
    const uint64_t nowUs = qpc_now_us();
    if (args.seconds > 0 && nowUs >= (startUs + static_cast<uint64_t>(args.seconds) * 1000000ULL)) {
      break;
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
      if (h->type != static_cast<uint16_t>(MessageType::FrameTick)) continue;
      if (n < static_cast<int>(sizeof(FrameTickMessage))) continue;
      const auto* m = reinterpret_cast<const FrameTickMessage*>(buf);

      ++recvCount;
      if (lastSeq > 0 && m->seq > (lastSeq + 1)) seqGap += (m->seq - lastSeq - 1);
      lastSeq = m->seq;

      uint64_t estOneWayUs = 0;
      const uint64_t recvUs = qpc_now_us();
      if (recvUs >= m->sendQpcUs) estOneWayUs = (recvUs - m->sendQpcUs);
      estOneWaySumUs += estOneWayUs;
      estOneWayMaxUs = std::max(estOneWayMaxUs, estOneWayUs);

      AckMessage ack{};
      ack.header.magic = remote60::native_poc::kMagic;
      ack.header.type = static_cast<uint16_t>(MessageType::Ack);
      ack.header.size = static_cast<uint16_t>(sizeof(ack));
      ack.seq = m->seq;
      ack.hostSendQpcUs = m->sendQpcUs;
      ack.clientRecvQpcUs = recvUs;
      const int wr = sendto(sock, reinterpret_cast<const char*>(&ack), sizeof(ack), 0,
                            reinterpret_cast<const sockaddr*>(&from), fromLen);
      if (wr == sizeof(ack)) ++ackSent;
    }

    if (nowUs >= statAtUs) {
      const uint64_t avgOneWayUs = (recvCount > 0) ? (estOneWaySumUs / recvCount) : 0;
      std::cout << "[native-client-poc] recv=" << recvCount
                << " ackSent=" << ackSent
                << " seqGap=" << seqGap
                << " estOneWayUs(avg/max)=" << avgOneWayUs << "/" << estOneWayMaxUs
                << "\n";
      recvCount = 0;
      ackSent = 0;
      seqGap = 0;
      estOneWaySumUs = 0;
      estOneWayMaxUs = 0;
      statAtUs += 1000000ULL;
    }

    Sleep(1);
  }

  closesocket(sock);
  std::cout << "[native-client-poc] done\n";
  return 0;
}
