// keepalive r3 R2-a / r4 S1·S2: the native connect gate (connect_op_gate.hpp) -- the product's own class, with
// its test hooks (REMOTE60_CONNECT_GATE_TEST_HOOKS, this target only) used to put an event exactly at a
// boundary. Each boundary also has its negative control: the r3 / 2abb8be flow transcribed, which must fail.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "connect_op_gate.hpp"
#include "directory_rendezvous.hpp"

#if defined(_WIN32)
#include <windows.h>
#endif

using remote60::native_poc::ConnectOpGate;
using remote60::native_poc::DirectoryRendezvous;

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
  // r5 T1: Connect and Disconnect both handle the one worker thread (assign / joinable+join). `inside` counts
  // the calls in it at once (the product has no lock of its own there); `teardowns` the Disconnects that
  // found a running session (a second one would be a second join of the same thread).
  std::atomic<int> inside{0}, maxInside{0}, teardowns{0};
  int holdMs = 0;  // how long a Connect / Disconnect stays inside
  std::vector<std::string> log;
  std::mutex logMu;
  void Note(const std::string& s) {
    std::lock_guard<std::mutex> lk(logMu);
    log.push_back(s);
  }
  void Enter() {
    const int n = ++inside;
    int m = maxInside.load();
    while (n > m && !maxInside.compare_exchange_weak(m, n)) {
    }
  }
  bool Connect(const std::string& who) {
    Enter();
    ++connects;
    const uint64_t g = ++generation;
    running = true;
    Note("connect " + who + " gen " + std::to_string(g));
    if (holdMs) sleep_ms(holdMs);
    --inside;
    return true;
  }
  void Disconnect(const std::string& who) {
    Enter();
    ++disconnects;
    if (running) {
      ++teardowns;
      if (holdMs) sleep_ms(holdMs);
    }
    running = false;
    Note("disconnect by " + who + " (gen " + std::to_string(generation.load()) + ")");
    --inside;
  }
};

// r5 T1: the shared rendezvous socket as the steps see it -- in use inside a step, closed by whoever.
struct FakeRendezvous {
  std::atomic<bool> open{false}, inUse{false};
  std::atomic<int> closes{0}, closedWhileInUse{0};
  void Close() {
    if (inUse) ++closedWhileInUse;
    if (open.exchange(false)) ++closes;
  }
};

// The r4 nativeDisconnect, transcribed (the negative controls): both closes called directly, beside the gate.
void r4_native_disconnect(FakeSession& s, FakeRendezvous& rv) {
  s.Disconnect("r4 nativeDisconnect");
  rv.Close();
}
// r5: the same JNI call -- CloseExplicit, the rendezvous half on a thread the caller does not wait for.
int64_t r5_native_disconnect(ConnectOpGate& g, FakeSession& s, FakeRendezvous& rv, std::thread* async) {
  return g.CloseExplicit([&s] { s.Disconnect("explicit"); }, [&rv] { rv.Close(); },
                         [async](const std::function<void()>& fn) { *async = std::thread(fn); });
}

long open_handles() {
#if defined(_WIN32)
  DWORD n = 0;
  GetProcessHandleCount(GetCurrentProcess(), &n);
  return static_cast<long>(n);
#else
  return 0;
#endif
}

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
    int connectsAtClose = -1;
    // r5: read at A's close, inside A's step. Read after it returned, B may already have connected (seen once
    // in 7 runs: a false FAIL of this check, not of the gate).
    const bool aClosed = g.CloseIfOwn(a, genA, [&s] { return s.generation.load(); }, [&] {
      connectsAtClose = s.connects;
      s.Disconnect("A's cleanup");
    });
    const bool bWaited = connectsAtClose == connectsBeforeB;  // B had not connected while A's step ran
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

  std::printf("[9] r5 T1: the user's explicit disconnect vs a stale operation's cleanup (CloseIfOwn on its thread)\n");
  {
    // A adopted; the user went elsewhere (A stale); A's cleanup thread is inside CloseIfOwn, closing A, when
    // the user's disconnect / exit arrives.
    for (int variant = 0; variant < 2; ++variant) {
      const bool r4 = variant == 1;
      ConnectOpGate g;
      FakeSession s;
      FakeRendezvous rv;
      s.holdMs = 60;
      const int64_t a = g.Next();
      const uint64_t genA = g.Adopt(a, steps_for(s, "A"));
      g.Next();
      std::thread explicitThread, async;
      std::atomic<bool> fired{false};
      g.testHook = [&](const char* at) {
        if (std::string(at) != "close_if_own_locked" || fired.exchange(true)) return;
        explicitThread = std::thread([&] {
          if (r4) {
            r4_native_disconnect(s, rv);
          } else {
            r5_native_disconnect(g, s, rv, &async);
          }
        });
        sleep_ms(20);  // the explicit close is on its way while the cleanup goes on to close
      };
      const bool closed = close_if_own(g, s, a, genA, "A's cleanup");
      explicitThread.join();
      if (async.joinable()) async.join();
      g.testHook = nullptr;
      if (!r4) {
        check("cleanup and explicit close: never both inside the session at once", s.maxInside == 1);
        check("the running session torn down exactly once (no second join)", s.teardowns == 1 && closed && !s.running);
      } else {
        check("NEGATIVE (r4 nativeDisconnect, beside the gate): both inside the session at once / torn down twice",
              s.maxInside >= 2 || s.teardowns >= 2);
      }
    }
  }

  std::printf("[10] r5 T1: explicit disconnect vs an adoption (Connect starting the worker)\n");
  {
    for (int variant = 0; variant < 2; ++variant) {
      const bool r4 = variant == 1;
      ConnectOpGate g;
      FakeSession s;
      FakeRendezvous rv;
      const int64_t b = g.Next();
      std::atomic<bool> inConnect{false}, release{false};
      ConnectOpGate::Steps sb = steps_for(s, "B");
      sb.connect = [&] {
        s.Enter();
        ++s.connects;
        ++s.generation;
        s.running = true;
        inConnect = true;
        while (!release) sleep_ms(1);
        --s.inside;
        return true;
      };
      uint64_t genB = 99;
      std::thread tb([&] { genB = g.Adopt(b, sb); });
      while (!inConnect) sleep_ms(1);
      std::thread async;
      std::atomic<long long> cancelMs{-1};
      std::atomic<bool> explicitDone{false};
      const auto t0 = std::chrono::steady_clock::now();
      if (!r4) {
        // the cancel (the number) is timed on its own: it must not wait for the Connect
        g.testHook = [&](const char* at) {
          if (std::string(at) == "close_explicit_cancelled") {
            cancelMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0)
                           .count();
          }
        };
      }
      std::thread te([&] {
        if (r4) {
          r4_native_disconnect(s, rv);
        } else {
          r5_native_disconnect(g, s, rv, &async);
        }
        explicitDone = true;
      });
      sleep_ms(50);
      const bool explicitWaited = !explicitDone;
      release = true;
      tb.join();
      te.join();
      if (async.joinable()) async.join();
      g.testHook = nullptr;
      if (!r4) {
        check("the explicit cancel does not wait for the running Connect",
              cancelMs >= 0 && cancelMs < 20 && !g.IsCurrent(b));
        check("its session close waited for the adoption to end: never inside together",
              explicitWaited && s.maxInside == 1);
        check("B went stale during Connect: not adopted, its own connection closed, torn down once",
              genB == 0 && !s.running && s.teardowns == 1);
        const uint64_t again = g.Adopt(b, steps_for(s, "B again"));
        check("AFTER THE EXPLICIT CLOSE, B IS NEVER ADOPTED AGAIN", again == 0 && !s.running && s.connects == 1);
      } else {
        check("NEGATIVE (r4 nativeDisconnect, beside the gate): Disconnect runs inside the Connect", s.maxInside >= 2);
      }
    }
    // a newer operation adopted between the explicit cancel and its close is not closed by it
    ConnectOpGate g;
    FakeSession s;
    FakeRendezvous rv;
    const int64_t a = g.Next();
    g.Adopt(a, steps_for(s, "A"));
    uint64_t genC = 0;
    g.testHook = [&](const char* at) {
      if (std::string(at) != "close_explicit_cancelled") return;
      g.testHook = nullptr;
      std::thread tc([&] {
        const int64_t c = g.Next();
        genC = g.Adopt(c, steps_for(s, "C"));
      });
      tc.join();
    };
    std::thread async;
    r5_native_disconnect(g, s, rv, &async);
    if (async.joinable()) async.join();
    check("a connect started right after the close's number (newer) is not closed by it",
          genC == 2 && s.running && s.generation == 2);
  }

  std::printf("[11] r5 T1: explicit disconnect vs a network step using / closing the rendezvous socket\n");
  {
    for (int variant = 0; variant < 2; ++variant) {
      const bool r4 = variant == 1;
      ConnectOpGate g;
      FakeSession s;
      FakeRendezvous rv;
      const int64_t a = g.Next();
      std::atomic<bool> inStep{false};
      std::thread ta([&] {
        ConnectOpGate::StepGuard step(g, a);
        g.ClaimRendezvous(a);
        rv.open = true;
        rv.inUse = true;
        inStep = true;
        for (int i = 0; i < 200 && !step.stopped(); ++i) sleep_ms(5);  // the punch's wait
        sleep_ms(30);                                                  // the rest of one recv timeout
        rv.inUse = false;
        if (step.stopped()) rv.Close();  // the product's punch step: stale -> close, no hand-over
      });
      while (!inStep) sleep_ms(1);
      std::thread async;
      const auto t0 = std::chrono::steady_clock::now();
      if (r4) {
        r4_native_disconnect(s, rv);
      } else {
        r5_native_disconnect(g, s, rv, &async);
      }
      const auto callMs =
          std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
      ta.join();
      if (async.joinable()) async.join();
      if (!r4) {
        check("the explicit disconnect returns without waiting for the step's network wait", callMs < 20);
        check("the socket is never closed while a step uses it; closed exactly once",
              rv.closedWhileInUse == 0 && rv.closes == 1 && !rv.open);
      } else {
        check("NEGATIVE (r4 nativeDisconnect, beside the gate): the socket is closed while the step uses it",
              rv.closedWhileInUse >= 1);
      }
    }
    // an idle observed socket (between the observe and the punch: the app's HTTP call) is closed by it
    ConnectOpGate g;
    FakeSession s;
    FakeRendezvous rv;
    const int64_t a = g.Next();
    {
      ConnectOpGate::StepGuard step(g, a);
      g.ClaimRendezvous(a);
      rv.open = true;
    }
    std::thread async;
    r5_native_disconnect(g, s, rv, &async);
    if (async.joinable()) async.join();
    check("an observed socket waiting for its punch is closed, once", rv.closes == 1 && !rv.open && !g.OwnsRendezvous(a));
    // ... but not a socket a newer operation observed with before the close's rendezvous half ran
    FakeRendezvous rv2;
    g.CloseExplicit(nullptr, [&] { rv2.Close(); }, [&](const std::function<void()>& fn) {
      const int64_t c = g.Next();
      {
        ConnectOpGate::StepGuard step(g, c);
        g.ClaimRendezvous(c);
        rv2.open = true;
      }
      fn();
    });
    check("a newer operation's observed socket is left alone", rv2.open && rv2.closes == 0);
  }

  std::printf("[12] r5 T1: a cancelled observe closes the socket it opened\n");
  {
    // The product's DirectoryRendezvous against a local address: stop is already set, so every call opens its
    // socket and returns "cancelled" at the first attempt. Opened and closed must match (process handles).
    DirectoryRendezvous rv;
    std::atomic<bool> stop{true};
    std::string observed, err;
    rv.Observe("127.0.0.1", 9, "t", &observed, &err, &stop);  // warm-up: Winsock's own handles
    const long before = open_handles();
    int cancelled = 0;
    for (int i = 0; i < 200; ++i) {
      err.clear();
      if (!rv.Observe("127.0.0.1", 9, "t", &observed, &err, &stop) && err == "cancelled") ++cancelled;
    }
    const long grew = open_handles() - before;
    std::printf("  200 cancelled observes: cancelled=%d, process handles %+ld\n", cancelled, grew);
    check("200 cancelled observes leave no socket behind (handles grew < 10)", cancelled == 200 && grew < 10);
  }

  std::printf("\nconnect_op_gate_test: %d passed, %d failed\nRESULT: %s\n", gPass, gFail, gFail == 0 ? "PASS" : "FAIL");
  return gFail == 0 ? 0 : 1;
}
