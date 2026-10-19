// prelogin-ui r11 (U5): the window-list request and the first pong, as one state (client_host_role_gate.hpp).
//
// The interleaving Codex named for r10: the app's RequestWindowList reads "role not known" and stops there;
// the control thread's first pong (a host at the logon screen) sets the role and finds nothing deferred;
// the request then marks itself deferred and shows "waiting for the host" -- and no later pong is a first
// pong, so it waits for ever. Here the request is stopped exactly there (the gate's stop point, between its
// role read and its commit) while the pong runs on its own thread.

#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>

#include "client_host_role_gate.hpp"

namespace n = remote60::native_poc;
using LR = n::HostRoleGate::ListRequest;
using FP = n::HostRoleGate::FirstPong;

namespace {
int gChecks = 0, gFailures = 0;
void check(const char* what, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : "  ", detail.c_str());
}
constexpr uint32_t kLogon = 0x2000;  // kCaptureFlagPreloginV1
constexpr uint32_t kOther = 0x0010;  // some other capability bit

// What the session shows and sends, in the order the posts ran.
struct Ui {
  std::mutex mu;
  std::string status;
  int listSent = 0;  // window-list requests that would go on the wire
  void show(const std::string& s) {
    std::lock_guard<std::mutex> l(mu);
    status = s;
  }
};
void post_request(Ui& ui, LR d) {
  if (d == LR::Send) {
    std::lock_guard<std::mutex> l(ui.mu);
    ++ui.listSent;
    return;
  }
  ui.show(d == LR::Deferred ? "waiting for the host" : "not available at the logon screen");
}
void post_pong(Ui& ui, FP o) {
  if (o == FP::SendList) {
    std::lock_guard<std::mutex> l(ui.mu);
    ++ui.listSent;
  } else if (o == FP::AnswerNotHere) {
    ui.show("not available at the logon screen");
  }
}
}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::printf("the window-list request and the first pong (prelogin-ui r11, U5)\n\n");
  {
    n::HostRoleGate gate;
    Ui ui;
    std::atomic<bool> atStop{false};
    FP pongOutcome = FP::NotFirst;
    std::thread pong;
    const LR d = gate.RequestList(
        kLogon, [&](LR x) { post_request(ui, x); },
        [&] {
          // stopped between the role read ("not known") and the commit; the first pong arrives now
          atStop = true;
          pong = std::thread([&] {
            pongOutcome = gate.Pong(kLogon | kOther, kLogon, [&](FP o) { post_pong(ui, o); });
          });
          std::this_thread::sleep_for(std::chrono::milliseconds(300));
        });
    pong.join();
    check("U5: the request stopped after reading \"not known\", the first pong (logon screen) arrived then",
          atStop.load() && d == LR::Deferred);
    check("U5: ...the pong answered that request (r10: it found nothing deferred)", pongOutcome == FP::AnswerNotHere,
          std::to_string(static_cast<int>(pongOutcome)));
    check("U5: ...the last thing shown is the answer, not \"waiting for the host\"",
          ui.status == "not available at the logon screen", ui.status);
    check("U5: ...nothing is left deferred, and no window-list request goes on the wire", !gate.Deferred() &&
                                                                                             ui.listSent == 0);
    const FP second = gate.Pong(kLogon, kLogon, [&](FP o) { post_pong(ui, o); });
    check("...a later pong is not a first pong (and changes nothing shown)",
          second == FP::NotFirst && ui.status == "not available at the logon screen");
  }
  {
    n::HostRoleGate gate;
    Ui ui;
    (void)gate.Pong(kLogon, kLogon, [&](FP o) { post_pong(ui, o); });
    const LR d = gate.RequestList(kLogon, [&](LR x) { post_request(ui, x); });
    check("asked after the pong said \"logon screen\": answered at once, not sent",
          d == LR::NotAtLogonScreen && ui.listSent == 0 && ui.status == "not available at the logon screen");
  }
  {
    n::HostRoleGate gate;
    Ui ui;
    const LR d = gate.RequestList(kLogon, [&](LR x) { post_request(ui, x); });
    const FP o = gate.Pong(kOther, kLogon, [&](FP x) { post_pong(ui, x); });
    check("a logged-on host: asked before the pong -> waits, the pong sends the list once",
          d == LR::Deferred && o == FP::SendList && ui.listSent == 1 && !gate.Deferred());
    check("...and asked again now: sent", gate.RequestList(kLogon, [&](LR x) { post_request(ui, x); }) == LR::Send &&
                                              ui.listSent == 2);
  }
  {
    // U5's second half: a reader must never see "known" with the flags of before (r10 read the flags first,
    // then "known": an old 0 with a new "known" is a logged-on host). One word: never.
    n::HostRoleGate gate;
    std::atomic<bool> stop{false};
    std::atomic<long> mixed{0}, seen{0};
    std::thread reader([&] {
      while (!stop) {
        const n::HostRoleGate::Role r = gate.Load();
        if (r.known && (r.flags & kLogon) == 0) ++mixed;
        if (r.known) ++seen;
      }
    });
    for (int i = 0; i < 200000; ++i) {
      gate.Reset();
      (void)gate.Pong(kLogon, kLogon, nullptr);
    }
    stop = true;
    reader.join();
    check("a reader never sees \"known\" without the flags that came with it (200000 sessions)",
          mixed == 0 && seen > 0, std::to_string(mixed.load()) + " mixed of " + std::to_string(seen.load()));
  }
  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", gFailures ? "FAILED" : "ALL PASS", gChecks, gFailures);
  return gFailures ? 1 : 0;
}
