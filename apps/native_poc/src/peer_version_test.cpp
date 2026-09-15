#include "peer_version.hpp"
#include <iostream>
#include <vector>
#include <thread>

using namespace remote60::native_poc;
struct VersionLink : ControlLink {
  std::vector<unsigned char> incoming, outgoing;
  size_t at = 0;
  bool ended = false;
  bool Read(void* out, size_t size) override {
    if (at + size > incoming.size()) return false;
    std::memcpy(out, incoming.data() + at, size); at += size; return true;
  }
  bool Write(const void* data, size_t size) override {
    const auto* p = static_cast<const unsigned char*>(data);
    outgoing.insert(outgoing.end(), p, p + size); return true;
  }
  bool EndMessage() override { ended = true; return true; }
  bool Alive() const override { return true; }
  void Reply(ControlVersionMessage m) {
    const auto* p = reinterpret_cast<const unsigned char*>(&m);
    incoming.assign(p, p + sizeof(m)); at = 0;
  }
};

int main() {
  // These existing wire layouts must not change when product-version exchange is added.
  static_assert(sizeof(UdpHelloPacket) == 49);
  static_assert(sizeof(ControlPingMessage) == 20);
  static_assert(sizeof(ControlPongMessage) == 184);
  VersionLink old;
  std::string peer;
  if (!exchange_peer_version(old, false, 7, &peer) || !old.outgoing.empty() ||
      peer != "unknown-unsupported") return 1;
  VersionLink link;
  auto response = make_version_message(MessageType::ControlVersionResponse, 7);
  link.Reply(response);
  if (!exchange_peer_version(link, true, 7, &peer) || peer != local_product_version() ||
      !link.ended || link.outgoing.size() != sizeof(ControlVersionMessage)) return 1;
  ControlVersionMessage sent{};
  std::memcpy(&sent, link.outgoing.data(), sizeof(sent));
  if (sent.header.type != static_cast<uint16_t>(MessageType::ControlVersionRequest) || sent.seq != 7 ||
      reported_peer_version(sent.productVersion) != local_product_version()) return 1;
  response.seq = 8; link.Reply(response);
  if (exchange_peer_version(link, true, 7, &peer)) return 1;
  response.seq = 7; response.header.size = sizeof(MessageHeader); link.Reply(response);
  if (exchange_peer_version(link, true, 7, &peer)) return 1;
  char invalid[32]{}; std::strcpy(invalid, "0.2.126\nfake=1");
  if (reported_peer_version(invalid) != "unknown-invalid") return 1;
  std::memset(invalid, '1', sizeof(invalid));
  if (reported_peer_version(invalid) != "unknown-invalid") return 1;
  // A real loopback TCP exchange through the production ControlLink implementation.
  WSADATA wsa{};
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;
  SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  sockaddr_in address{}; address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
      listen(listener, 1) != 0) return 1;
  int addressSize = sizeof(address);
  getsockname(listener, reinterpret_cast<sockaddr*>(&address), &addressSize);
  SOCKET client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (connect(client, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) return 1;
  SOCKET server = accept(listener, nullptr, nullptr);
  DWORD timeout = 1000;
  setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
  setsockopt(server, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
  bool serverOk = false;
  std::thread responder([&] {
    TcpControlLink wire(server);
    ControlVersionMessage request{};
    if (!wire.Read(&request, sizeof(request))) return;
    if (request.header.type != static_cast<uint16_t>(MessageType::ControlVersionRequest) ||
        reported_peer_version(request.productVersion) != local_product_version()) return;
    auto reply = make_version_message(MessageType::ControlVersionResponse, request.seq);
    std::memset(reply.productVersion, 0, sizeof(reply.productVersion));
    std::strcpy(reply.productVersion, "9.8.7");
    serverOk = wire.Write(&reply, sizeof(reply)) && wire.EndMessage();
  });
  TcpControlLink wire(client);
  const bool clientOk = exchange_peer_version(wire, true, 42, &peer) && peer == "9.8.7";
  responder.join();
  closesocket(server); closesocket(client); closesocket(listener); WSACleanup();
  if (!clientOk || !serverOk) return 1;
  std::cout << "PASS: gated old-peer compatibility, bilateral version bytes, framed reply validation, log sanitization\n";
  return 0;
}
