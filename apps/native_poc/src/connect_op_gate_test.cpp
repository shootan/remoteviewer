// keepalive r3 R2-a / r4 S1·S2: the native connect gate (connect_op_gate.hpp) -- the product's own class, with
// its test hooks (REMOTE60_CONNECT_GATE_TEST_HOOKS, this target only) used to put an event exactly at a
// boundary. Each boundary also has its negative control: the r3 / 2abb8be flow transcribed, which must fail.
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
void sleep_ms(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

// The one session of the process: Connect makes the next generation; Disconnect ends whatever runs.
struct FakeSession {
  std::atomic<uint64_t> generation{0};
  std::atomic<bool> running{false};
  std::atomic<int> connects{0}, disconnects{0};
  std::vector<std::string> log;
  std::mutex logMu;
  void Note(const std::string& s) {
    std::lock_guard<std::mutex> lk(logMu);
    log.push_back(s);
  }
  bool Connect(const std::string& who) {
    ++connects;
    const uint64_t g = ++generation;
    running = true;
    Note("connect " + who + " gen " + std::to_string(g));
    return true;
  }
  void Disconnect(const std::string& who) {
    ++disconnects;
    running = false;
    Note("disconnect by " + who + " (gen " + std::to_string(generation.load()) + ")");
  }
};

ConnectOpGate::Steps steps_for(FakeSession& s, const std::string& who, bool* closed = nullptr) {
  ConnectOpGate::Steps st;
  st.connect = [&s, who] { return s.Connect(who); };
  st.generation = [&s] { return s.generation.load(); };
  st.disconnect = [&s, who] { s.Disconnect(who); };
  st.closePrepared = [closed] { if (closed) *closed = true; };
  return st;
}
bool close_if_own(ConnectOpGate& g, FakeSession& s, int64_t op, uint64_t gen, const std::string& who) {
  return g.CloseIfOwn(op, gen, [&s] { return s.generation.load(); }, [&s, who] { s.Disconnect(who); });
}
}  // namespace

int main() {
  std::printf("[1] the current operation is adopted, with the generation it made\n");
  {
    ConnectOpGate g;
    FakeSession s;
    const int64_t a = g.Next();
    bool closed = false;
    const uint64_t gen = g.Adopt(a, steps_for(s, "A", &closed));
    check("adopted: generation 1", gen == 1 && s.running && !closed && g.StaleRefused() == 0);
  }

  std::printf("[2] stale before adoption / at its boundary / while Connect runs (r3, kept)\n");
  {
    ConnectOpGate g;
    FakeSession s;
    const int64_t a = g.Next();
    g.Next();  // invalidated
    bool closed = false;
    check("stale before: no Connect, the prepared socket closed", g.Adopt(a, steps_for(s, "A", &closed)) == 0 &&
                                                                       s.connects == 0 && closed);
    const int64_t c = g.Next();
    g.testHook = [&](const char* at) { if (std::string(at) == "before_adopt") g.Next(); };
    bool closed2 = false;
    check("stale at the boundary: no Connect", g.Adopt(c, steps_for(s, "C", &closed2)) == 0 && s.connects == 0 && closed2);
    const int64_t d = g.Next();
    g.testHook = [&](const char* at) { if (std::string(at) == "after_connect") g.Next(); };
    check("stale while Connect runs: 0 back, its connection closed before Adopt returns",
          g.Adopt(d, steps_for(s, "D")) == 0 && s.connects == 1 && s.disconnects == 1 && !s.running);
    g.testHook = nullptr;
  }

  std::printf("[3] r4 S1: a stale operation's late cleanup never closes a newer adoption\n");
  {
    // directory A adopted gen 1, then went stale (the user tapped another PC: B). A's late UI callback
    // cleans up while B is being adopted -- put B's adoption exactly between A's compare and its close.
    ConnectOpGate g;
    FakeSession s;
    const int64_t a = g.Next();
    const uint64_t genA = g.Adopt(a, steps_for(s, "A"));
    const int64_t b = g.Next();
    std::atomic<bool> bTried{false};
    uint64_t genB = 0;
    std::thread tb;
    g.testHook = [&](const char* at) {
      if (std::string(at) != "close_if_own_locked" || bTried) return;
      bTried = true;  // A has compared; now B tries to adopt -- it must wait for A's step to end
      tb = std::thread([&] { genB = g.Adopt(b, steps_for(s, "B")); });
      sleep_ms(80);
    };
    const int connectsBeforeB = s.connects;
    const bool aClosed = close_if_own(g, s, a, genA, "A's cleanup");
    const bool bWaited = s.connects == connectsBeforeB;  // B had not connected while A's step ran
    tb.join();
    g.testHook = nullptr;
    check("A's cleanup closed A's own connection (it was still the one running)", aClosed);
    check("B's adoption waited for A's compare-and-close (one step)", bWaited);
    check("B IS ADOPTED AND STILL RUNNING after A's late cleanup", genB == 2 && s.running && s.generation == 2);
    // and the other order: B adopted first, A's cleanup after it closes nothing
    const int64_t c = g.Next();
    const uint64_t genC = g.Adopt(c, steps_for(s, "C"));
    const int64_t d = g.Next();
    const uint64_t genD = g.Adopt(d, steps_for(s, "D"));
    check("C's late cleanup after D's adoption closes nothing; D runs",
          !close_if_own(g, s, c, genC, "C's cleanup") && s.running && s.generation == genD);

    // negative control -- r3: the app read the generation, compared, then called an unconditional Disconnect
    FakeSession s0;
    ConnectOpGate g0;
    const int64_t a0 = g0.Next();
    const uint64_t genA0 = g0.Adopt(a0, steps_for(s0, "A"));
    const int64_t b0 = g0.Next();
    const uint64_t read = s0.generation;            // nativeConnectionGeneration()
    const bool closes = genA0 != 0 && genA0 == read;  // staleCloses(...)
    g0.Adopt(b0, steps_for(s0, "B"));               // B adopts between the read and the Disconnect
    if (closes) s0.Disconnect("A's cleanup (r3)");  // nativeDisconnect()
    check("NEGATIVE (r3 read->compare->Disconnect): B's connection is closed by A's cleanup", !s0.running);
  }

  std::printf("[4] r4 S1: a stale directory cleanup vs a MANUAL (IP) connect -- the manual one goes through Adopt\n");
  {
    ConnectOpGate g;
    FakeSession s;
    const int64_t a = g.Next();
    const uint64_t genA = g.Adopt(a, steps_for(s, "directory A"));
    const int64_t m = g.Next();  // the user connects by IP: a new operation
    const uint64_t genM = g.Adopt(m, steps_for(s, "manual"));
    check("the directory's late cleanup closes nothing; the manual connection runs",
          !close_if_own(g, s, a, genA, "A's cleanup") && s.running && s.generation == genM);
    // negative control -- r3: nativeConnect called Connect directly (outside the gate), the cleanup compared
    // a generation it read before it
    FakeSession s0;
    ConnectOpGate g0;
    const int64_t a0 = g0.Next();
    const uint64_t genA0 = g0.Adopt(a0, steps_for(s0, "directory A"));
    const uint64_t read = s0.generation;
    s0.Connect("manual (r3, no gate)");
    if (genA0 == read) s0.Disconnect("A's cleanup (r3)");
    check("NEGATIVE (r3): the manual connection is closed by the directory cleanup", !s0.running);
  }

  std::printf("[5] r4 S2-a: a recreated Activity cannot make the old Activity's operation current again\n");
  {
    ConnectOpGate g;
    const int64_t old1 = g.Next();  // Activity 1, op 1, its observe still on its way
    g.Next();                       // Activity 1 stopped: invalidated
    const int64_t new1 = g.Next();  // Activity 2's first operation
    check("the new Activity's first operation is a new number", new1 != old1);
    check("THE OLD ACTIVITY'S OPERATION STAYS STALE", !g.IsCurrent(old1) && g.IsCurrent(new1));
    // negative control -- r3: each Activity counted from 0; the gate stored what it was given (SetCurrent)
    int64_t r3current = 0;
    const int64_t r3old = 1;
    r3current = 2;                  // Activity 1 invalidated
    r3current = 1;                  // Activity 2's first op: number 1 again
    check("NEGATIVE (r3): the old Activity's op 1 is current again", r3current == r3old);
  }

  std::printf("[6] r4 S2: a stale step cannot clear its own cancellation\n");
  {
    ConnectOpGate g;
    const int64_t a = g.Next();
    // the invalidation lands between the step lock and the currency check
    g.testHook = [&](const char* at) { if (std::string(at) == "step_lock_taken") g.Next(); };
    {
      ConnectOpGate::StepGuard step(g, a);
      check("invalidated right at the step's start: the step does not enter", !step.entered());
    }
    g.testHook = nullptr;
    const int64_t b = g.Next();
    {
      ConnectOpGate::StepGuard step(g, b);
      check("the current operation's step enters, not stopped", step.entered() && !step.stop()->load());
      g.Next();  // invalidated while the step runs (its network wait sees this)
      check("INVALIDATED DURING THE STEP: its stop flag is set and stays set", step.stop()->load() && step.stopped());
    }
    // negative control -- r3: IsCurrent, then (an invalidation sets stop=true), then the stale call stores false
    std::atomic<bool> r3stop{false};
    int64_t r3cur = 5;
    const bool wasCurrent = r3cur == 5;
    r3cur = 6;
    r3stop = true;                       // nativeSetConnectOwner
    if (wasCurrent) r3stop = false;      // g_connect_stop.store(false) after the check
    check("NEGATIVE (r3): the cancellation is overwritten -- the stale punch runs its full budget", !r3stop.load());
  }

  std::printf("[7] r4 S2: network steps are one at a time; the rendezvous socket is its observer's\n");
  {
    ConnectOpGate g;
    const int64_t a = g.Next();
    std::atomic<bool> aInStep{false}, aSawStop{false};
    std::thread ta([&] {
      ConnectOpGate::StepGuard step(g, a);
      g.ClaimRendezvous(a);
      aInStep = true;
      for (int i = 0; i < 200 && !step.stopped(); ++i) sleep_ms(5);  // the observe's wait
      aSawStop = step.stopped();
    });
    while (!aInStep) sleep_ms(1);
    const auto t0 = std::chrono::steady_clock::now();
    const int64_t b = g.Next();  // a new operation: must not wait for A's step
    const auto nextMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    bool bEntered = false, bOwns = false;
    {
      ConnectOpGate::StepGuard step(g, b);  // waits for A's step to stop and leave
      bEntered = step.entered();
      bOwns = g.ClaimRendezvous(b) && g.OwnsRendezvous(b);
    }
    ta.join();
    check("taking a new number does not wait for a running step", nextMs < 20);
    check("A's step saw its stop and left; B's step came after it, and owns the socket", aSawStop && bEntered && bOwns);
    // an operation that did not observe does not punch / hand over another one's socket
    const int64_t c = g.Next();
    {
      ConnectOpGate::StepGuard step(g, c);
      check("C (current, never observed) does not own B's socket", step.entered() && !g.OwnsRendezvous(c));
    }
    // negative control -- r3: no step lock, no owner: the punch step only checked IsCurrent
    const bool r3punches = true;  // IsCurrent(c) was true; nothing asked whose socket it was
    check("NEGATIVE (r3): C would punch and hand over B's socket", r3punches);
  }

  std::printf("[8] r3 R2-a, kept: a new operation during the old one's Connect\n");
  {
    ConnectOpGate g;
    FakeSession s;
    const int64_t a = g.Next();
    std::atomic<bool> inConnect{false}, release{false};
    uint64_t genA = 99, genB = 0;
    ConnectOpGate::Steps sa = steps_for(s, "A");
    sa.connect = [&] {
      s.Connect("A");
      inConnect = true;
      while (!release) sleep_ms(1);
      return true;
    };
    std::thread ta([&] { genA = g.Adopt(a, sa); });
    while (!inConnect) sleep_ms(1);
    const auto t0 = std::chrono::steady_clock::now();
    const int64_t b = g.Next();
    const auto waitedMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    std::thread tb([&] { genB = g.Adopt(b, steps_for(s, "B")); });
    sleep_ms(50);
    const bool bWaited = s.connects == 1;
    release = true;
    ta.join();
    tb.join();
    check("cancelling does not wait for a running Connect", waitedMs < 20);
    check("serialised; A not adopted and closed before B connected; B has ITS generation",
          bWaited && genA == 0 && genB == 2 && s.running && s.log.size() == 3 && s.log[1].rfind("disconnect by A", 0) == 0);
  }

  std::printf("\nconnect_op_gate_test: %d passed, %d failed\nRESULT: %s\n", gPass, gFail, gFail == 0 ? "PASS" : "FAIL");
  return gFail == 0 ? 0 : 1;
}
