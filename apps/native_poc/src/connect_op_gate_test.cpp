// keepalive r3 R2-a: the native connect adoption gate (connect_op_gate.hpp), with a stale-making event put at
// each of its boundaries -- and the 2abb8be flow transcribed as the negative control (the app checked its
// operation once, before the JNI call; the native side then connected unconditionally).
#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "connect_op_gate.hpp"

using remote60::native_poc::ConnectOpGate;

namespace {
int gPass = 0, gFail = 0;
void check(const char* what, bool ok) {
  (ok ? gPass : gFail)++;
  std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
}

// A session: Connect makes the next generation; Disconnect ends whatever runs.
struct FakeSession {
  uint64_t generation = 0;
  bool running = false;
  int connects = 0, disconnects = 0;
  std::vector<std::string> log;
  bool Connect(const std::string& who) {
    ++connects;
    ++generation;
    running = true;
    log.push_back("connect " + who + " gen " + std::to_string(generation));
    return true;
  }
  void Disconnect(const std::string& who) {
    ++disconnects;
    running = false;
    log.push_back("disconnect by " + who);
  }
};

ConnectOpGate::Steps steps_for(FakeSession& s, const std::string& who, bool* closed,
                               std::function<void(const char*)> stage = nullptr) {
  ConnectOpGate::Steps st;
  st.connect = [&s, who] { return s.Connect(who); };
  st.generation = [&s] { return s.generation; };
  st.disconnect = [&s, who] { s.Disconnect(who); };
  st.closePrepared = [closed] { if (closed) *closed = true; };
  st.stage = std::move(stage);
  return st;
}

// 2abb8be: the operation was checked by the app before the JNI call only; Connect was unconditional and the
// generation was read afterwards from the global session.
uint64_t old_flow(ConnectOpGate& g, int64_t op, FakeSession& s, std::function<void()> duringConnect) {
  if (!g.IsCurrent(op)) return 0;  // Kotlin stillMine("before_native_connect")
  s.Connect("old");
  if (duringConnect) duringConnect();
  return s.generation;  // nativeConnectionGeneration() afterwards
}
}  // namespace

int main() {
  std::printf("[1] the current operation is adopted, with the generation it made\n");
  {
    ConnectOpGate g;
    FakeSession s;
    g.SetCurrent(1);
    bool closed = false;
    const uint64_t gen = g.Adopt(1, steps_for(s, "op1", &closed));
    check("adopted: generation 1", gen == 1 && s.running && !closed && g.StaleRefused() == 0);
  }

  std::printf("[2] stale before adoption (sign-out after the Kotlin check / during the punch / at the hand-over)\n");
  {
    ConnectOpGate g;
    FakeSession s;
    g.SetCurrent(1);
    bool closed = false;
    g.SetCurrent(2);  // invalidated
    const uint64_t gen = g.Adopt(1, steps_for(s, "op1", &closed));
    check("not adopted, Connect never ran, the prepared socket closed", gen == 0 && s.connects == 0 && closed);
    // the same, made stale exactly at the adoption boundary
    FakeSession s2;
    g.SetCurrent(3);
    bool closed2 = false;
    const uint64_t gen2 = g.Adopt(3, steps_for(s2, "op3", &closed2, [&](const char* at) {
      if (std::string(at) == "before_adopt") g.SetCurrent(4);  // exit / stop right there
    }));
    check("stale at the boundary itself: not adopted, no Connect", gen2 == 0 && s2.connects == 0 && closed2);
  }

  std::printf("[3] stale WHILE Connect runs (sign-out / exit just after the native connect began)\n");
  {
    ConnectOpGate g;
    FakeSession s;
    g.SetCurrent(1);
    const uint64_t gen = g.Adopt(1, steps_for(s, "op1", nullptr, [&](const char* at) {
      if (std::string(at) == "after_connect") g.SetCurrent(2);
    }));
    check("NOT ADOPTED: 0 back, the connection it made is disconnected before Adopt returns",
          gen == 0 && s.connects == 1 && s.disconnects == 1 && !s.running && g.StaleRefused() == 1);
    // negative control: the old flow adopts it
    ConnectOpGate g0;
    FakeSession s0;
    g0.SetCurrent(1);
    const uint64_t old = old_flow(g0, 1, s0, [&] { g0.SetCurrent(2); });
    check("NEGATIVE (2abb8be flow): the stale operation's connection is left running and claimed", old != 0 && s0.running);
  }

  std::printf("[4] a new operation during the old one's Connect: only the new one's connection survives\n");
  {
    ConnectOpGate g;
    FakeSession s;
    g.SetCurrent(1);
    std::atomic<bool> inConnect{false}, release{false};
    uint64_t genA = 99, genB = 0;
    ConnectOpGate::Steps a = steps_for(s, "A", nullptr);
    a.connect = [&] {
      s.Connect("A");
      inConnect.store(true);
      while (!release.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
      return true;
    };
    std::thread ta([&] { genA = g.Adopt(1, a); });
    while (!inConnect.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const auto t0 = std::chrono::steady_clock::now();
    g.SetCurrent(2);  // the user tapped another PC: B is current -- this must not wait for A's Connect
    const auto waitedMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    std::thread tb([&] { genB = g.Adopt(2, steps_for(s, "B", nullptr)); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    const bool bWaited = s.connects == 1;  // B's Connect has not run while A's is in progress
    release.store(true);
    ta.join();
    tb.join();
    check("cancelling does not wait for a running Connect", waitedMs < 20);
    check("the adoptions are serialised (B's Connect waited for A's)", bWaited);
    check("A is not adopted and its connection was closed before B connected",
          genA == 0 && s.log.size() == 3 && s.log[0] == "connect A gen 1" && s.log[1] == "disconnect by A" &&
              s.log[2] == "connect B gen 2");
    check("B is adopted with ITS generation (2), not a global read", genB == 2 && s.running);
  }

  std::printf("\nconnect_op_gate_test: %d passed, %d failed\nRESULT: %s\n", gPass, gFail, gFail == 0 ? "PASS" : "FAIL");
  return gFail == 0 ? 0 : 1;
}
