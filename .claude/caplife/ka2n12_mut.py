import sys, shutil
SRC = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/'
MUT = {
    'ackUnchecked': ('standby_lease.hpp', [("""      if (!udp_hello_ack_acceptable(candidate, n, /*requireDirectoryAuth=*/true) ||
          candidate.size != static_cast<uint16_t>(sizeof(UdpHelloPacket))) {
        return false;
      }""", "      // MUTANT: any HelloAck accepted")]),
    'sizeUnchecked': ('standby_lease.hpp', [("""      if (!udp_hello_ack_acceptable(candidate, n, /*requireDirectoryAuth=*/true) ||
          candidate.size != static_cast<uint16_t>(sizeof(UdpHelloPacket))) {""", """      if (!udp_hello_ack_acceptable(candidate, n, /*requireDirectoryAuth=*/true)) {  // MUTANT: declared size unchecked""")]),
    'noKeep': ('standby_runner.hpp', [("ActivationArgs(args, slot, op)", "ActivationArgs(args, nullptr, op)")]),
    'opIgnored': ('standby_lease.hpp', [("""  bool SessionOver(uint64_t op) {
    std::lock_guard<std::mutex> lock(mu_);
    if (op != op_ || !handedOff_""", """  bool SessionOver(uint64_t op) {
    std::lock_guard<std::mutex> lock(mu_);
    if (!handedOff_""")]),
    'handshakeIgnored': ('standby_lease.hpp', [("""  bool SessionOver(uint64_t op) {
    std::lock_guard<std::mutex> lock(mu_);
    if (op != op_ || !handedOff_ || handshaking_.load(std::memory_order_acquire)) return false;""", """  bool SessionOver(uint64_t op) {
    std::lock_guard<std::mutex> lock(mu_);
    if (op != op_ || !handedOff_) return false;""")]),
    'releaseNoCancel': ('standby_lease.hpp', [("""    if (handedOff_) {
      cancel_.store(true, std::memory_order_release);
      return;
    }""", """    if (handedOff_) {
      return;  // MUTANT
    }""")]),
    'staleHandshake': ('standby_lease.hpp', [("if (thisOp != op_ || !handedOff_ || cancel_.load(", "if (cancel_.load(")]),
    'eventsUnbounded': ('standby_lease.hpp', [("while (events_.size() > kEventsKept) events_.pop_front();", "// MUTANT unbounded")]),
    'perLeaseOp': ('standby_lease.hpp', [("const uint64_t thisOp = op_ = NextOp();", "const uint64_t thisOp = ++op_;")]),
    'n2aUnchecked': ('standby_lease.hpp', [("""        if (!valid()) {
          aborted = true;
          return;
        }
        e = f();""", "        e = f();  // MUTANT"), ("    if (done && valid()) {", "    if (done) {  // MUTANT")]),
    'n2aStepOnly': ('standby_lease.hpp', [("""        if (!valid()) {
          aborted = true;
          return;
        }
        e = f();""", "        e = f();  // MUTANT")]),
    'n2bNoIntent': ('standby_lease.hpp', [("""        while (intents_ > 0 || observing_) {
          if (now_() >= until) return;
          idle_.wait_for(lock, std::chrono::milliseconds(20));
        }""", "        // MUTANT: no intent priority")]),
    'noReopenWhenTied': ('standby_lease.hpp', [("    peerTied_ = true;\n", "    // MUTANT: a tied socket is not noted\n")]),
    'notShared': ('standby_lease.hpp', [("    args->preparedSocketShared = true;", "    // MUTANT: not marked shared")]),
    'syncNoUntie': ('standby_lease.hpp', [("""    if (!UntieForNewCapability()) {
      needsCapability_ = true;
      return;
    }""", "    // MUTANT: the synchronous path keeps the tied socket")]),
    'strayUncounted': ('standby_lease.hpp', [("""      if (stray) {
        ++strays_;
        lastStrayMs_ = now_();
      }""", "      // MUTANT: the tail is not counted")]),
    'pumpWhileHandedOff': ('standby_lease.hpp', [("if (socket_ == kInvalidSocket || handedOff_) return;", "if (socket_ == kInvalidSocket) return;")]),
}
m, act = sys.argv[1], sys.argv[2]
f, reps = MUT[m]
p = SRC + f
bak = 'D:/remote/remote/.claude/caplife/ka2n12_' + f + '.bak'
if act == 'apply':
    shutil.copyfile(p, bak)
    s = open(p, 'rb').read().decode('utf-8')
    crlf = '\r\n' in s
    s = s.replace('\r\n', '\n')
    for old, new in reps:
        assert s.count(old) == 1, (m, s.count(old))
        s = s.replace(old, new)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'wb').write(s.encode('utf-8'))
else:
    shutil.copyfile(bak, p)
print(m, act, 'ok')
