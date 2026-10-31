p = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/host_standby_table_test.cpp'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')
old = '''  std::printf("\\nRESULT: %s  (%d checks, %d failed)\\n", gFailures ? "FAILED" : "ALL PASS", gChecks, gFailures);'''
new = '''  std::printf("\\n[r4 N2: the replay proof is a digest; a late admission changes nothing]\\n");
  {
    StandbyProof d1{}, d1b{}, d2{};
    check("N2-a: the proof is SHA-256 of the capability: the same token gives the same digest, another a different one",
          StandbyProofDigest("cap-aaaa", 8, &d1) && StandbyProofDigest("cap-aaaa", 8, &d1b) && StandbyProofDigest("cap-aaab", 8, &d2) &&
              d1 == d1b && d1 != d2);
    StandbyTable n;
    StandbyEntry e11, got2;
    const StandbyProof capA = Proof(0xA10), capB = Proof(0xB11), capC = Proof(0xC11), capD = Proof(0xD12);
    check("N2-b: gen 11 (capability B) admitted first", n.Admit(Dev(7).id, 11, capB, 0, kA, now, &e11) == StandbyTable::AdmitResult::Admitted);
    check("N2-b: the late gen 10 with its UNUSED capability A: Stale -- the gen-11 entry and its secret stay",
          n.Admit(Dev(7).id, 10, capA, 0, kA2, now + 10, &got2) == StandbyTable::AdmitResult::Stale &&
              n.Find(e11.entryId) != nullptr && n.Find(e11.entryId)->secret == e11.secret && n.size() == 1);
    check("N2-b: the same gen 11 with ANOTHER proof: Stale too",
          n.Admit(Dev(7).id, 11, capC, 0, kA, now + 20, &got2) == StandbyTable::AdmitResult::Stale &&
              n.Find(e11.entryId)->secret == e11.secret);
    check("N2-b: ...while the gen-11 retransmit (same proof, same endpoint) is still a replay",
          n.ClassifyHello(Dev(7).id, 11, capB, kA, now + 30, nullptr) == StandbyTable::HelloClass::Replay);
    check("N2-b: a really new operation (gen 12, its own capability) replaces it",
          n.Admit(Dev(7).id, 12, capD, 0, kA, now + 40, &got2) == StandbyTable::AdmitResult::Admitted && got2.entryId != e11.entryId &&
              n.size() == 1);
    // wire: the refusal on the wire, and a digest that cannot be computed
    StandbyTable wn;
    int calls = 0;
    std::string good = "cap-n2-gen11";
    const auto auth2 = [&](const std::string& tok, const sockaddr_in&) { ++calls; return tok.rfind("cap-n2", 0) == 0; };
    UdpStandbyPacket h11 = Hello(8, 11, "cap-n2-gen11");
    auto o = standby_handle(wn, reinterpret_cast<const uint8_t*>(&h11), sizeof(h11), Addr(3000), false, auth2, now);
    const UdpStandbyPacket ack11 = o.reply;
    UdpStandbyPacket h10 = Hello(8, 10, "cap-n2-gen10");
    o = standby_handle(wn, reinterpret_cast<const uint8_t*>(&h10), sizeof(h10), Addr(3001), false, auth2, now + 10);
    check("N2-b wire: the late gen 10 is answered Superseded with NO secret; the gen-11 entry keeps its secret",
          o.action == StandbyAction::Reply && o.reply.status == kUdpStandbyStatusSuperseded && o.reply.entryId == 0 &&
              wn.Find(ack11.entryId) != nullptr &&
              std::memcmp(wn.Find(ack11.entryId)->secret.data(), ack11.entrySecret, 16) == 0);
    const int before = calls;
    UdpStandbyPacket h12 = Hello(8, 12, "cap-n2-gen12");
    const auto failing = [](const char*, size_t, StandbyProof*) { return false; };
    o = standby_handle(wn, reinterpret_cast<const uint8_t*>(&h12), sizeof(h12), Addr(3000), false, auth2, now + 20, 0, failing);
    check("N2-a wire: a digest that cannot be computed admits nothing and spends no capability (fail-closed)",
          o.action == StandbyAction::Silent && calls == before && wn.Find(ack11.entryId) != nullptr && wn.size() == 1);
  }

  std::printf("\\nRESULT: %s  (%d checks, %d failed)\\n", gFailures ? "FAILED" : "ALL PASS", gChecks, gFailures);'''
assert s.count(old) == 1, s.count(old)
s = s.replace(old, new)
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
