import sys
p = sys.argv[1]
s = open(p, encoding='utf-8', newline='').read()
start = s.index('bool DirectoryRendezvous::PunchAny(')
end = s.index('}  // namespace remote60::native_poc')
new = r'''bool punch_answer_is_valid(const char* data, int n) {
  if (!data || n != static_cast<int>(sizeof(UdpHelloPacket))) return false;
  UdpHelloPacket p{};
  std::memcpy(&p, data, sizeof(p));
  return p.magic == kMagic && p.size == sizeof(UdpHelloPacket) && p.version >= kUdpProtocolVersion &&
         p.kind == static_cast<uint16_t>(UdpPacketKind::Punch);
}

bool DirectoryRendezvous::PunchAny(const std::vector<RendezvousCandidate>& candidates,
                                   uint32_t budgetMs, RendezvousCandidate* outChosen,
                                   std::string* outError,
                                   const std::atomic<bool>* stop) {
  return PunchAny(candidates, budgetMs, PunchRaceOptions{}, outChosen, nullptr, outError, stop);
}

bool DirectoryRendezvous::PunchAny(const std::vector<RendezvousCandidate>& candidates,
                                   uint32_t budgetMs, const PunchRaceOptions& options,
                                   RendezvousCandidate* outChosen, PunchRaceReport* outReport,
                                   std::string* outError,
                                   const std::atomic<bool>* stop) {
  PunchRaceReport localReport;
  PunchRaceReport& report = outReport ? *outReport : localReport;
  report = PunchRaceReport{};
  report.decision = "error";
  if (socket_ == kInvalidSocket) {
    if (outError) *outError = "no prepared socket; observe first";
    return false;
  }
  if (candidates.empty()) {
    if (outError) *outError = "no candidates";
    return false;
  }

  // Resolve once. A candidate we cannot resolve is dropped rather than failing the whole
  // attempt: one bad entry must not cost the others their chance.
  std::vector<sockaddr_in> targets;
  std::vector<const RendezvousCandidate*> resolved;
  for (const RendezvousCandidate& candidate : candidates) {
    sockaddr_in addr{};
    if (!resolve_udp(candidate.ip, candidate.port, &addr)) continue;
    targets.push_back(addr);
    resolved.push_back(&candidate);
  }
  report.candidatesResolved = static_cast<uint32_t>(targets.size());
  if (targets.empty()) {
    if (outError) *outError = "no candidate address could be resolved";
    return false;
  }

  UdpHelloPacket packet{};
  packet.kind = static_cast<uint16_t>(UdpPacketKind::Punch);

  // hello-delay-fix r1: the send clock is separate from what the receive returns. A round goes out
  // at most once per kPunchIntervalMs, however the wait in between ends. Before, a receive that
  // failed AT ONCE -- on Windows the ICMP Port Unreachable an earlier punch to an unreachable
  // candidate earned comes back as WSAECONNRESET on the next recvfrom -- started the next round
  // immediately; that round's punches earned more of them, and the loop fed itself: ~290 rounds in
  // a few milliseconds were measured, and the replies and errors it left queued on the socket then
  // held up the hello's ack for seconds (reconnect-hello-delay r1). An answer from a candidate is
  // still taken the moment it arrives.
  using Clock = std::chrono::steady_clock;
  // np-impl r1 (contract 3): T0 is taken just before the first punch leaves, and the commit point
  // and every reported time count from it.
  const auto t0 = Clock::now();
  const auto deadline = t0 + std::chrono::milliseconds(budgetMs);
  const bool early = options.relayCommitMs > 0;
  const auto commitAt = t0 + std::chrono::milliseconds(options.relayCommitMs);
  const auto sinceT0 = [t0](Clock::time_point at) {
    return static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(at - t0).count());
  };
  int heldRelay = -1;  // early mode: index of the relay answer kept aside
  auto nextSend = t0;
  uint32_t readsThisRound = 0;
  char scratch[512];
  const auto stopped = [stop]() { return stop && stop->load(std::memory_order_acquire); };
  // The one place a choice is made, so it is made once, and only if nobody has asked to stop first
  // (contract 4: a cancel outranks any choice).
  const auto choose = [&](size_t i, const char* decision) {
    if (stopped()) {
      report.decision = "cancelled";
      if (outError) *outError = "cancelled";
      return false;
    }
    report.chosenMs = sinceT0(Clock::now());
    report.decision = decision;
    if (outChosen) *outChosen = *resolved[i];
    return true;
  };
  const auto noteStranger = [&](const sockaddr_in& from, int n, bool validPunch, Clock::time_point at) {
    ++report.strangerDatagrams;
    for (auto& s : report.strangers) {
      if (s.ipv4NetworkOrder == from.sin_addr.s_addr && s.port == ntohs(from.sin_port)) {
        ++s.datagrams;
        if (validPunch) ++s.validPunches;
        return;
      }
    }
    if (report.strangers.size() >= PunchRaceReport::kMaxStrangers) {
      ++report.strangersUntracked;
      return;
    }
    PunchRaceReport::Stranger s;
    s.ipv4NetworkOrder = from.sin_addr.s_addr;
    s.port = ntohs(from.sin_port);
    s.datagrams = 1;
    s.validPunches = validPunch ? 1 : 0;
    s.firstMs = sinceT0(at);
    s.firstBytes = static_cast<uint32_t>(n);
    report.strangers.push_back(s);
  };
  for (;;) {
    const auto now = Clock::now();
    // Checked before every send and every wait, and no wait is longer than one interval: a cancel
    // is noticed as soon as it was before.
    if (stopped()) {
      report.decision = "cancelled";
      if (outError) *outError = "cancelled";
      return false;
    }
    // np-impl r1 (contract 4): at the commit point a relay answer already in hand is taken. Before it,
    // a direct answer still wins over it; with none in hand the race simply goes on.
    if (heldRelay >= 0 && now >= commitAt) return choose(static_cast<size_t>(heldRelay), "relay");
    if (now >= deadline) break;
    if (now >= nextSend) {
      // One round hits every candidate before waiting, so a reply from any of them is caught by
      // the same wait rather than after the others have had their turn.
      for (const sockaddr_in& target : targets) {
        (void)sendto(socket_, reinterpret_cast<const char*>(&packet), sizeof(packet), 0,
                     reinterpret_cast<const sockaddr*>(&target), sizeof(target));
      }
      ++report.rounds;
      // From now, not from the missed slot: a late round never turns into a burst.
      nextSend = now + std::chrono::milliseconds(kPunchIntervalMs);
      readsThisRound = 0;
    }
    // The wait ends at the next round, the budget -- and, while a relay answer is held, the commit
    // point, so the choice is made on time rather than up to one interval late.
    auto waitUntil = (std::min)(nextSend, deadline);
    if (heldRelay >= 0 && commitAt < waitUntil) waitUntil = commitAt;
    if (readsThisRound >= kPunchReadsPerRound) {
      // A round's worth of receives already came back at once: wait out the rest of it rather
      // than spin (in steps, so a stop is still seen within one).
      const auto pause = (std::min)(waitUntil - Clock::now(), std::chrono::duration_cast<Clock::duration>(
                                                                  std::chrono::milliseconds(kPunchPauseStepMs)));
      if (pause > Clock::duration::zero()) std::this_thread::sleep_for(pause);
      continue;
    }
    const auto waitMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(waitUntil - Clock::now()).count();
    (void)set_recv_timeout(socket_, static_cast<uint32_t>((std::max)(static_cast<long long>(1), waitMs)));

    sockaddr_in from{};
    int fromLen = static_cast<int>(sizeof(from));
    int err = 0;
    const int n = punch_recvfrom(socket_, scratch, static_cast<int>(sizeof(scratch)), &from, &fromLen, &err);
    ++readsThisRound;
    if (n > 0) {
      const auto at = Clock::now();
      const bool valid = punch_answer_is_valid(scratch, n);
      size_t match = targets.size();
      for (size_t i = 0; i < targets.size(); ++i) {
        if (from.sin_addr.s_addr == targets[i].sin_addr.s_addr && from.sin_port == targets[i].sin_port) {
          match = i;
          break;
        }
      }
      if (match == targets.size()) {
        // np-impl r1 (contract 1): not from a candidate. Counted, never answered, never chosen.
        noteStranger(from, n, valid, at);
        continue;
      }
      if (!valid) {
        // From a candidate, but not a punch answer: garbage, a stray, a wrong version -- not "ready".
        ++report.invalidFromCandidate;
        continue;
      }
      const bool isRelay = resolved[match]->kind == "relay";
      if (isRelay) {
        if (report.firstRelayMs < 0) report.firstRelayMs = sinceT0(at);
        if (early && at < commitAt) {
          // Kept aside: a direct answer before the commit point still wins over it.
          if (heldRelay < 0) heldRelay = static_cast<int>(match);
          report.relayHeld = true;
          continue;
        }
        return choose(match, early ? "relay-after-commit" : "relay");
      }
      if (report.firstDirectMs < 0) report.firstDirectMs = sinceT0(at);
      return choose(match, "direct");
    }
    if (!punch_receive_is_transient(n, err)) {
      // A socket that cannot receive at all does not come back by punching faster or longer.
      if (outError) *outError = "punch receive failed (error " + std::to_string(err) + ")";
      return false;
    }
  }

  // The budget ran out. A relay answer held in early mode cannot be here (the commit point lies inside
  // the budget whenever relayCommitMs < budgetMs; when it does not, a held answer is still taken now).
  if (heldRelay >= 0) return choose(static_cast<size_t>(heldRelay), "relay");
  report.decision = "none";
  if (outError) *outError = "no candidate answered";
  return false;
}

'''
new = new.replace('\n', '\r\n')
s = s[:start] + new + s[end:]
open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
