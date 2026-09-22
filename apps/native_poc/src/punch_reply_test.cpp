#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

// Whether the host answers a punch, and -- more to the point -- when it stops.
//
// The change this covers makes the host reply to punches so that a viewer on the same LAN sees a
// datagram from the private candidate before the relay's 2500 ms grace expires. Replying to
// strangers is the sort of thing that is fine until it is not, so most of this file is about the
// bounds rather than the reply: a window only a directory event can open, budgets inside it, and a
// source map that cannot be exhausted.
//
// Pure logic, no sockets. The product path -- ConsumeUdpPacket through the real send_ seam -- is
// covered in directory_retry_test.cpp, which already has that wiring.

#include <windows.h>

#include <cstdio>
#include <string>
#include <vector>

#include "punch_reply.hpp"

using namespace remote60::native_poc;

namespace {

int gChecks = 0;
int gFailures = 0;

void check(const char* what, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : "  ",
              detail.c_str());
}

PunchReplySource src(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint16_t port) {
  // Network order, the way it arrives in a sockaddr_in.
  PunchReplySource out;
  out.ipNetworkOrder = (a) | (b << 8) | (c << 16) | (d << 24);
  out.portNetworkOrder = static_cast<uint16_t>((port >> 8) | (port << 8));
  return out;
}

const PunchReplySource kNoAddr{};

/** The packet arguments for a well-formed punch, so the cases below read as what they vary. */
PunchReplyDecision decide_ok(const PunchReplyState& state, const PunchReplySource& from,
                             uint64_t nowMs, const PunchReplySource& directory = kNoAddr,
                             const PunchReplySource& self = kNoAddr) {
  return punch_reply_decide(state, from, directory, self, true, nowMs);
}

}  // namespace

int main() {
  std::printf("punch_reply_test\n");

  const PunchReplySource client = src(192, 168, 20, 16, 60420);
  const PunchReplySource directory = src(175, 207, 45, 151, 29181);
  const PunchReplySource self = src(211, 218, 222, 1, 60420);

  // ------------------------------------------------------------------ the window
  {
    PunchReplyState state;
    check("a host nobody has asked about answers nothing",
          !decide_ok(state, client, 1000).reply,
          punch_reply_reason_name(decide_ok(state, client, 1000).reason));
    check("...and says the window is closed, not that the budget ran out",
          decide_ok(state, client, 1000).reason == PunchReplyReason::Closed);

    const bool opened = punch_reply_note_directory_event(&state, 1000);
    check("a directory event opens the window", opened);
    check("...and the host then answers", decide_ok(state, client, 1000).reply);
    check("...still, just before it expires",
          decide_ok(state, client, 1000 + kPunchReplyWindowMs - 1).reply);
    check("...and not a millisecond after",
          !decide_ok(state, client, 1000 + kPunchReplyWindowMs).reply);
    check("...where the reason is again closed",
          decide_ok(state, client, 1000 + kPunchReplyWindowMs).reason == PunchReplyReason::Closed);
  }

  {
    // THE rearming rule. A client that keeps punching must not keep the host answering: only a
    // directory event may open or extend the window, and a punch from a client is not one.
    PunchReplyState state;
    punch_reply_note_directory_event(&state, 0);
    uint64_t now = 0;
    for (int i = 0; i < 20; ++i) {
      now += 500;  // a punch every 500ms for ten seconds
      const PunchReplyDecision d = decide_ok(state, client, now);
      if (d.reply) punch_reply_note_reply(&state, client, now);
    }
    check("a stream of client punches does not extend the window",
          !decide_ok(state, client, kPunchReplyWindowMs + 1).reply,
          punch_reply_reason_name(decide_ok(state, client, kPunchReplyWindowMs + 1).reason));
    check("...and the host is closed to everyone, not just to this client",
          !punch_reply_window_open(state, kPunchReplyWindowMs + 1));
  }

  {
    // A second directory event inside an open window refreshes it without resetting the budget
    // to zero mid-attempt... and one AFTER it expired starts a clean window.
    PunchReplyState state;
    punch_reply_note_directory_event(&state, 0);
    punch_reply_note_reply(&state, client, 0);
    const bool reopened = punch_reply_note_directory_event(&state, 100);
    check("a second directory event inside the window is not a new window", !reopened);
    check("...and does not clear what has already been spent", state.repliesInWindow == 1);

    const bool fresh = punch_reply_note_directory_event(&state, 100 + kPunchReplyWindowMs + 1);
    check("a directory event after expiry opens a new one", fresh);
    check("...with the budget back", state.repliesInWindow == 0);
  }

  // ------------------------------------------------------------------ the budgets
  {
    PunchReplyState state;
    punch_reply_note_directory_event(&state, 0);
    uint64_t now = 0;
    uint32_t replies = 0;
    for (uint32_t i = 0; i < kPunchReplyPerSource + 10; ++i) {
      now += 100;  // spread out, so the per-second ceiling is not what stops it
      const PunchReplyDecision d = decide_ok(state, client, now);
      if (d.reply) {
        ++replies;
        punch_reply_note_reply(&state, client, now);
      }
    }
    check("one source gets exactly its allowance", replies == kPunchReplyPerSource,
          std::to_string(replies) + " of " + std::to_string(kPunchReplyPerSource));
    check("...and the refusal after it is a budget refusal",
          decide_ok(state, client, now + 100).reason == PunchReplyReason::Budget);

    // A different source in the same window is unaffected: one peer cannot starve another.
    check("a different source still has its own allowance",
          decide_ok(state, src(192, 168, 20, 77, 5000), now + 100).reply);
  }

  {
    // The per-second ceiling, with enough distinct sources that no single source budget fires.
    PunchReplyState state;
    punch_reply_note_directory_event(&state, 0);
    uint32_t replies = 0;
    for (uint32_t i = 0; i < kPunchReplyPerSec + 20; ++i) {
      const PunchReplySource who = src(10, 0, static_cast<uint32_t>(i / 250),
                                       static_cast<uint32_t>(i % 250), 40000 + i);
      const PunchReplyDecision d = decide_ok(state, who, 500);  // all in the same second
      if (d.reply) {
        ++replies;
        punch_reply_note_reply(&state, who, 500);
      }
    }
    check("no more than the per-second ceiling in one second", replies == kPunchReplyPerSec,
          std::to_string(replies) + " of " + std::to_string(kPunchReplyPerSec));
    check("...and the next second is allowed again",
          decide_ok(state, src(10, 1, 1, 1, 41000), 1600).reply);
  }

  {
    // The whole-window ceiling, reached across many sources and many seconds.
    PunchReplyState state;
    punch_reply_note_directory_event(&state, 0);
    uint32_t replies = 0;
    uint64_t now = 0;
    for (uint32_t i = 0; i < kPunchReplyPerWindow + 50; ++i) {
      now += 20;  // 50/s at most, and the window is 10s, so the window ceiling is what bites
      const PunchReplySource who = src(10, 1, static_cast<uint32_t>(i / 250),
                                       static_cast<uint32_t>(i % 250), 40000 + i);
      const PunchReplyDecision d = decide_ok(state, who, now);
      if (d.reply) {
        ++replies;
        punch_reply_note_reply(&state, who, now);
      }
    }
    check("no more than the window ceiling in one window", replies <= kPunchReplyPerWindow,
          std::to_string(replies) + " of " + std::to_string(kPunchReplyPerWindow));
    check("...and it is the window ceiling that says so",
          decide_ok(state, src(10, 9, 9, 9, 40000), now + 20).reason == PunchReplyReason::Budget);
  }

  {
    // The source map is fixed, and full is not the same as broken: the least recently seen entry
    // is reused, so a flood of new addresses costs the real peer its count but never locks it out.
    PunchReplyState state;
    punch_reply_note_directory_event(&state, 0);
    uint64_t now = 0;
    for (size_t i = 0; i < kPunchReplySources * 3; ++i) {
      ++now;
      const PunchReplySource who = src(10, 2, static_cast<uint32_t>(i / 250),
                                       static_cast<uint32_t>(i % 250), 40000 + static_cast<uint16_t>(i));
      if (decide_ok(state, who, now).reply) punch_reply_note_reply(&state, who, now);
    }
    size_t used = 0;
    for (size_t i = 0; i < kPunchReplySources; ++i) used += state.sources[i].used ? 1 : 0;
    check("the source map never grows past its ceiling", used <= kPunchReplySources,
          std::to_string(used) + " of " + std::to_string(kPunchReplySources));
    // A second later, so the per-second ceiling the flood just saturated is not what answers
    // this. What is being checked is the MAP: a full map must not lock out a new peer, and the
    // first version of this check could not tell the two ceilings apart.
    check("...and a peer arriving after the flood is still answered",
          decide_ok(state, client, now + 1100).reply,
          punch_reply_reason_name(decide_ok(state, client, now + 1100).reason));
  }

  // ------------------------------------------------------------------ what we refuse to answer
  {
    PunchReplyState state;
    punch_reply_note_directory_event(&state, 0);

    check("the directory's own wake is not answered",
          !punch_reply_decide(state, directory, directory, self, true, 100).reply);
    check("...and says so as a source refusal",
          punch_reply_decide(state, directory, directory, self, true, 100).reason ==
              PunchReplyReason::Source);

    check("our own observed tuple is not answered",
          !punch_reply_decide(state, self, directory, self, true, 100).reply);
    check("...and is named as ourselves, not as a bad source",
          punch_reply_decide(state, self, directory, self, true, 100).reason ==
              PunchReplyReason::Self);

    check("a multicast source is not answered",
          !decide_ok(state, src(224, 0, 0, 1, 5000), 100).reply);
    check("a reserved 240/4 source is not answered",
          !decide_ok(state, src(240, 1, 2, 3, 5000), 100).reply);
    check("the broadcast address is not answered",
          !decide_ok(state, src(255, 255, 255, 255, 5000), 100).reply);
    check("0.0.0.0 is not answered", !decide_ok(state, src(0, 0, 0, 0, 5000), 100).reply);
    check("port 0 is not answered", !decide_ok(state, src(192, 168, 1, 5, 0), 100).reply);
    check("an ordinary private source IS answered", decide_ok(state, client, 100).reply);
  }

  {
    // The packet itself. magic and kind alone are not "well formed" -- the length and the
    // protocol version have to agree too, or we are answering something we do not understand.
    PunchReplyState state;
    punch_reply_note_directory_event(&state, 0);
    check("a punch that is not exactly the right length is not answered",
          !punch_reply_decide(state, client, directory, self, false, 100).reply);
    check("...and says the packet was the problem",
          punch_reply_decide(state, client, directory, self, false, 100).reason ==
              PunchReplyReason::Malformed);

    check("length must be exact, not merely sufficient",
          !punch_reply_packet_ok(65, 64, 1, 1, 2, 2, 3, 3));
    check("a wrong magic is refused", !punch_reply_packet_ok(64, 64, 9, 1, 2, 2, 3, 3));
    check("a wrong kind is refused", !punch_reply_packet_ok(64, 64, 1, 1, 8, 2, 3, 3));
    check("a wrong protocol version is refused", !punch_reply_packet_ok(64, 64, 1, 1, 2, 2, 9, 3));
    check("all four agreeing is well formed", punch_reply_packet_ok(64, 64, 1, 1, 2, 2, 3, 3));
  }

  // ------------------------------------------- two hosts wired to each other (the reflector case)
  {
    // The case that makes a reply policy dangerous: two hosts that both answer punches, pointed
    // at each other. Each reply is a punch, which is an arriving punch for the other. Without a
    // bound this runs until something gives.
    //
    // Simulated at the decision level, with each side's reply fed to the other exactly as a
    // datagram would be. What is being checked is that it STOPS, and where.
    const PunchReplySource a = src(192, 168, 20, 50, 43000);
    const PunchReplySource b = src(192, 168, 20, 51, 43000);

    PunchReplyState left, right;
    punch_reply_note_directory_event(&left, 0);  // only the left host was woken

    uint64_t now = 0;
    int exchanges = 0;
    bool inFlightToLeft = true;  // one punch arrives at the left host to start it off
    bool inFlightToRight = false;
    for (int i = 0; i < 10000 && (inFlightToLeft || inFlightToRight); ++i) {
      ++now;
      const bool toLeft = inFlightToLeft;
      const bool toRight = inFlightToRight;
      inFlightToLeft = inFlightToRight = false;
      if (toLeft) {
        const PunchReplyDecision d = punch_reply_decide(left, b, kNoAddr, a, true, now);
        if (d.reply) {
          punch_reply_note_reply(&left, b, now);
          ++exchanges;
          inFlightToRight = true;
        }
      }
      if (toRight) {
        const PunchReplyDecision d = punch_reply_decide(right, a, kNoAddr, b, true, now);
        if (d.reply) {
          punch_reply_note_reply(&right, a, now);
          ++exchanges;
          inFlightToLeft = true;
        }
      }
    }
    check("two hosts answering each other stop", exchanges < 10000,
          std::to_string(exchanges) + " exchanges");
    // The right host was never woken, so it never answers at all: the exchange dies on its first
    // hop. That is the window doing the work, before any budget is consulted.
    check("...on the first hop, because only one of them had a window",
          exchanges == 1, std::to_string(exchanges) + " exchanges");
    check("...and the silent host recorded no replies", right.replied == 0);
  }

  {
    // The same pair, but BOTH woken -- the worst case the budget has to hold. It still has to
    // stop, and it has to stop at the per-source allowance.
    const PunchReplySource a = src(192, 168, 20, 50, 43000);
    const PunchReplySource b = src(192, 168, 20, 51, 43000);

    PunchReplyState left, right;
    punch_reply_note_directory_event(&left, 0);
    punch_reply_note_directory_event(&right, 0);

    uint64_t now = 0;
    int exchanges = 0;
    bool inFlightToLeft = true;
    bool inFlightToRight = false;
    for (int i = 0; i < 100000 && (inFlightToLeft || inFlightToRight); ++i) {
      ++now;
      const bool toLeft = inFlightToLeft;
      const bool toRight = inFlightToRight;
      inFlightToLeft = inFlightToRight = false;
      if (toLeft) {
        const PunchReplyDecision d = punch_reply_decide(left, b, kNoAddr, a, true, now);
        if (d.reply) {
          punch_reply_note_reply(&left, b, now);
          ++exchanges;
          inFlightToRight = true;
        }
      }
      if (toRight) {
        const PunchReplyDecision d = punch_reply_decide(right, a, kNoAddr, b, true, now);
        if (d.reply) {
          punch_reply_note_reply(&right, a, now);
          ++exchanges;
          inFlightToLeft = true;
        }
      }
    }
    check("two woken hosts answering each other still stop", exchanges < 100000,
          std::to_string(exchanges) + " exchanges");
    check("...at the per-source allowance, one side then the other",
          exchanges == static_cast<int>(kPunchReplyPerSource) * 2,
          std::to_string(exchanges) + " exchanges, expected " +
              std::to_string(kPunchReplyPerSource * 2));
    check("...and neither side exceeded its own budget",
          left.replied == kPunchReplyPerSource && right.replied == kPunchReplyPerSource,
          std::to_string(left.replied) + " / " + std::to_string(right.replied));
  }

  // ============== the per-source ceiling is a ceiling, and not one per residency in the map
  //
  // The first version evicted the least recently used entry when the map filled up, so a source
  // that had spent its 25 could be pushed out by 64 others and come back as a fresh entry with 25
  // more. Inside one window the only real limit was the global 200 -- which is not what the
  // number 25 claims. Eviction is gone: full means a source we are not already tracking is
  // refused, and the map is cleared only when a window opens.
  {
    PunchReplyState state;
    uint64_t now = 1000;
    punch_reply_note_directory_event(&state, now);

    const PunchReplySource a = src(192, 168, 20, 16, 50000);

    // 25 milliseconds a step: well inside the window, and 40 a second against a ceiling of 50,
    // so the per-second budget never joins in and confuses which limit is being measured.
    auto spend = [&](const PunchReplySource& from, int attempts) {
      int replied = 0;
      for (int i = 0; i < attempts; ++i) {
        now += 25;
        if (decide_ok(state, from, now).reply) {
          punch_reply_note_reply(&state, from, now);
          ++replied;
        }
      }
      return replied;
    };

    const int aFirst = spend(a, 30);
    check("a source gets its 25 and no more", aFirst == static_cast<int>(kPunchReplyPerSource),
          std::to_string(aFirst) + " replies from 30 punches");

    // Fill the rest of the map. One slot is already A's, so 63 others take it to full.
    int othersAnswered = 0;
    for (int i = 0; i < 63; ++i) {
      const PunchReplySource other =
          src(10, 0, static_cast<uint32_t>(i / 250), static_cast<uint32_t>(i % 250 + 1), 40000);
      othersAnswered += spend(other, 1);
    }
    check("...and 63 other sources fill the map, each answered once", othersAnswered == 63,
          std::to_string(othersAnswered) + " answered");
    check("...which is the map full", punch_reply_sources_full(state));

    // The case the eviction made possible. A is still tracked, still spent.
    const int aAgain = spend(a, 10);
    check("a spent source stays spent once the map is full", aAgain == 0,
          std::to_string(aAgain) + " replies, expected 0");

    // And the newcomer is refused rather than displacing anybody.
    const PunchReplySource newcomer = src(203, 0, 113, 9, 41000);
    now += 25;
    const PunchReplyDecision full = decide_ok(state, newcomer, now);
    check("...and the 65th source is refused rather than evicting one",
          !full.reply && full.reason == PunchReplyReason::Budget,
          punch_reply_reason_name(full.reason));
    check("...with the window still open, so this is a map limit and not an expiry",
          punch_reply_window_open(state, now));
    check("...and the global budget still had room, so it is not that either",
          state.repliesInWindow < kPunchReplyPerWindow,
          std::to_string(state.repliesInWindow) + " of " + std::to_string(kPunchReplyPerWindow));

    // A new window forgets everyone. That is where a source's 25 comes back -- deliberately,
    // because a new directory event means somebody asked to connect again.
    now += kPunchReplyWindowMs + 1;
    punch_reply_note_directory_event(&state, now);
    check("a new window clears the map", !punch_reply_sources_full(state));
    const int aAfter = spend(a, 30);
    check("...and the same source gets 25 again in it",
          aAfter == static_cast<int>(kPunchReplyPerSource),
          std::to_string(aAfter) + " replies");
  }

  // ===================== a flood of strangers costs the peer its slot, but never its count
  {
    // The other half of the same decision, stated as the trade it is. With eviction, 64 punches
    // from made-up addresses RENEWED a spent source. Without it, they can deny a newcomer a slot
    // for the rest of the window -- ten seconds -- and that is the price.
    PunchReplyState state;
    uint64_t now = 5000;
    punch_reply_note_directory_event(&state, now);

    const PunchReplySource peer = src(192, 168, 20, 16, 50000);
    int peerReplies = 0;
    for (int i = 0; i < 25; ++i) {
      now += 25;
      if (decide_ok(state, peer, now).reply) {
        punch_reply_note_reply(&state, peer, now);
        ++peerReplies;
      }
    }
    // Seventy, not sixty-three: enough to fill the map AND push the peer out of it. Sixty-three
    // only fills it, and under the old eviction the peer was still tracked -- so a count that
    // stopped there watched the defect happen without touching it.
    for (int i = 0; i < 70; ++i) {
      now += 25;
      const PunchReplySource stranger =
          src(198, 51, 100, static_cast<uint32_t>(i + 1), 40000);
      if (decide_ok(state, stranger, now).reply) punch_reply_note_reply(&state, stranger, now);
    }
    now += 25;
    check("the flood cannot renew the peer that already spent its share",
          !decide_ok(state, peer, now).reply,
          std::to_string(peerReplies) + " replies before the flood");
  }

  std::printf("\n%s  (%d checks, %d failed)\n",
              gFailures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILED", gChecks, gFailures);
  return gFailures == 0 ? 0 : 1;
}
