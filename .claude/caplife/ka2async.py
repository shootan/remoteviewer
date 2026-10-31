base = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/'

def edit(rel, pairs):
    p = base + rel
    s = open(p, 'rb').read().decode('utf-8')
    crlf = '\r\n' in s
    s = s.replace('\r\n', '\n')
    for old, new in pairs:
        assert s.count(old) == 1, (rel, s.count(old), old[:80])
        s = s.replace(old, new)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'wb').write(s.encode('utf-8'))

# ---- lease: an asynchronous capability (the APK: Kotlin does the directory HTTP)
edit('standby_lease.hpp', [
    ('''  int lostCount() const { return lost_; }''', '''  int lostCount() const { return lost_; }
  // ② e: the lease waits for a capability its CapabilityFn could not give now (the APK: the directory call is the app's,
  // Kotlin) -- the owner observes from the lease's socket (ObserveFrom), gets one, and hands it in (Provide).
  bool needsCapability() const { return needsCapability_; }'''),
    ('''  // Start (or restart) the admission with a capability from CapabilityFn.
  void Start() { NeedCapability(); }''', '''  // Start (or restart) the admission with a capability from CapabilityFn.
  void Start() { NeedCapability(); }

  // The directory observation from THIS lease's socket (the relay binds a standby lease to that endpoint).
  bool ObserveFrom(const std::string& host, uint16_t port, const std::string& token) {
    return socket_ != kInvalidSocket && !handedOff_ && standby_observe_on(socket_, host, port, token);
  }

  // A capability the owner got for this lease (after needsCapability()): aim, then the admission.
  bool Provide(const StandbyCapability& cap) {
    if (!needsCapability_ || cap.token.empty() || cap.port == 0) return false;
    needsCapability_ = false;
    ip_ = cap.ip;
    port_ = cap.port;
    Apply(client_.Admit(cap.token, now_()));
    return true;
  }'''),
    ('''    if (socket_ == kInvalidSocket || !capability_ || !capability_(socket_, &cap) || cap.token.empty() || cap.port == 0) {
      events_.push_back("no capability now");
      return;
    }''', '''    if (socket_ == kInvalidSocket || !capability_ || !capability_(socket_, &cap) || cap.token.empty() || cap.port == 0) {
      events_.push_back("no capability now");
      needsCapability_ = true;  // the owner may hand one in (Provide); the entry waits for it -- nothing is sent
      return;
    }
    needsCapability_ = false;'''),
    ('''  bool handedOff_ = false;
  int lost_ = 0;''', '''  bool handedOff_ = false;
  bool needsCapability_ = false;
  int lost_ = 0;'''),
    # the observe datagram lives here now (standby_directory.hpp uses it too)
    ('''struct StandbyCapability {''', '''// One observation from socket `s`: "OBSERVE <token>" to the directory's observe endpoint until an answer (the same
// datagram the rendezvous sends, on this socket). true = the directory recorded this endpoint for `token`.
inline bool standby_observe_on(SocketHandle s, const std::string& host, uint16_t port, const std::string& token) {
  sockaddr_in to{};
  to.sin_family = AF_INET;
  to.sin_port = htons(port);
  if (inet_pton(AF_INET, host.c_str(), &to.sin_addr) != 1) {
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res) return false;
    to.sin_addr = reinterpret_cast<sockaddr_in*>(res->ai_addr)->sin_addr;
    freeaddrinfo(res);
  }
  const std::string probe = "OBSERVE " + token;
  for (int attempt = 0; attempt < 6; ++attempt) {
    sendto(s, probe.data(), static_cast<int>(probe.size()), 0, reinterpret_cast<const sockaddr*>(&to), sizeof(to));
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
    while (std::chrono::steady_clock::now() < until) {
      fd_set rd;
      FD_ZERO(&rd);
      FD_SET(s, &rd);
      timeval tv{0, 50000};
      if (select(static_cast<int>(s) + 1, &rd, nullptr, nullptr, &tv) <= 0) continue;
      char buf[512];
      sockaddr_in from{};
      socklen_t fl = sizeof(from);
      const int n = static_cast<int>(recvfrom(s, buf, sizeof(buf), 0, reinterpret_cast<sockaddr*>(&from), &fl));
      if (n > 0 && from.sin_addr.s_addr == to.sin_addr.s_addr && from.sin_port == to.sin_port) return true;
    }
  }
  return false;
}

struct StandbyCapability {'''),
    ('''#include <cstdint>
#include <cstring>
#include <functional>''', '''#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>'''),
])

# ---- standby_directory.hpp: the observe function moved to the lease header
p = base + 'standby_directory.hpp'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
t = s.replace('\r\n', '\n')
a = t.index('// One observation from socket `s`:')
b = t.index('struct StandbyDirectoryConfig {')
t = t[:a] + '// (standby_observe_on: standby_lease.hpp)\n\n' + t[b:]
if crlf:
    t = t.replace('\n', '\r\n')
open(p, 'wb').write(t.encode('utf-8'))

# ---- runner: needsCapability in the status; Observe / Provide under the lease's lock
edit('standby_runner.hpp', [
    ('''    bool handedOff = false;
  };''', '''    bool handedOff = false;
    bool needsCapability = false;  // ② e: the lease waits for a capability the app gets (Observe, then Provide)
  };'''),
    ('''    s.handedOff = slot->handedOff.load(std::memory_order_acquire);''', '''    s.handedOff = slot->handedOff.load(std::memory_order_acquire);
    s.needsCapability = slot->needsCapability.load(std::memory_order_acquire);'''),
    ('''  // The session made from TakeActivation ended''', '''  // ② e (the APK): the directory observation from this PC's lease socket, then the capability the app got for it.
  bool Observe(const std::string& key, const std::string& host, uint16_t port, const std::string& token) {
    std::shared_ptr<Slot> slot = Find(key);
    if (!slot) return false;
    std::lock_guard<std::mutex> l(slot->mu);
    return slot->lease->ObserveFrom(host, port, token);
  }
  bool Provide(const std::string& key, const StandbyCapability& cap) {
    std::shared_ptr<Slot> slot = Find(key);
    if (!slot) return false;
    std::lock_guard<std::mutex> l(slot->mu);
    const bool ok = slot->lease->Provide(cap);
    Publish(*slot);
    return ok;
  }

  // The session made from TakeActivation ended'''),
    ('''    std::atomic<int> lost{0};
  };''', '''    std::atomic<int> lost{0};
    std::atomic<bool> needsCapability{false};
  };'''),
    ('''    s.lost.store(s.lease->lostCount(), std::memory_order_release);
  }''', '''    s.lost.store(s.lease->lostCount(), std::memory_order_release);
    s.needsCapability.store(s.lease->needsCapability(), std::memory_order_release);
  }'''),
])

# ---- e2e runner phase: the asynchronous capability (as the APK will do it)
edit('host_standby_admission_e2e_test.cpp', [
    ('''    remote60::native_poc::StandbyRunner runner(gensR, features, [&](const std::string&) { return cap; }, mono_ms);
    check("the runner starts a lease for the PC (its own thread)", runner.Start("h-standby") && runner.size() == 1);''',
     '''    // as the APK does it: the lease has no capability source of its own -- it says it needs one, the app (Kotlin's
    // directory call) gets one and hands it in
    remote60::native_poc::StandbyRunner runner(
        gensR, features, [](const std::string&) { return remote60::native_poc::StandbyLease::CapabilityFn(); }, mono_ms);
    check("the runner starts a lease for the PC (its own thread)", runner.Start("h-standby") && runner.size() == 1);
    check("...it waits for a capability (none of its own), nothing sent",
          wait_until([&] { return runner.StatusOf("h-standby").needsCapability; }, 3000) &&
              runner.StatusOf("h-standby").state == StandbyLease::State::Idle);
    remote60::native_poc::StandbyCapability given;
    cap(remote60::native_poc::kInvalidSocket, &given);
    check("the app hands one in (Provide)", runner.Provide("h-standby", given) && !runner.StatusOf("h-standby").needsCapability);'''),
])
print('ok')
