p = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/standby_lease.hpp'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')
i = s.index("  // ② e (AVD ke_r3): the session connect()s its duplicate to its peer")
j = s.index("  // Another handle to the same socket (the same local endpoint); closing one leaves the other.")
s = s[:i] + s[j:]


def rep(a, b):
    global s
    assert s.count(a) == 1, (a[:80], s.count(a))
    s = s.replace(a, b)


rep("""    // the session connect()ed its duplicate to its peer: that stays on the shared socket unless dissolved
    if (!DissolvePeer(socket_)) peerTied_ = true;
""", """    // ② e (AVD ke_r3, ke_r4): the session connect()ed its duplicate to its peer, and that association is the SHARED
    // socket's (a POSIX dup, a WSADuplicateSocket duplicate alike). It is NOT dissolved: Windows cannot un-connect a UDP
    // socket (INADDR_ANY:0 -> WSAEADDRNOTAVAIL, AF_UNSPEC -> WSAEAFNOSUPPORT), and on Linux connect(AF_UNSPEC) also
    // gives up an automatically bound port -- the lease would come back on ANOTHER endpoint, which its relay lease does
    // not know (ke_r4: the next activation went nowhere). Tied, it keeps its endpoint, and its peer is still its own
    // target (Pings, an activation and a release go there). Before it aims anywhere else -- an observation for a new
    // capability, which is a new lease at the relay anyway -- it opens a fresh socket (ObserveFrom).
    peerTied_ = true;
""")
rep("  bool peerTied_ = false;         // a session's connect() could not be dissolved: reopen before aiming elsewhere\n",
    "  bool peerTied_ = false;         // a session connect()ed this socket to its peer: reopen before aiming elsewhere\n")
rep("""        // still tied to a session's peer (Windows): a fresh socket for the new capability (no read is running: the
        // pump starts none while an intent waits, and the one in flight has ended)""",
    """        // still tied to a session's peer: a fresh socket for the new capability (no read is running: the pump starts
        // none while an intent waits, and the one in flight has ended)""")
assert 'DissolvePeer' not in s
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))

t = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/standby_lease_test.cpp'
u = open(t, 'rb').read().decode('utf-8')
a = '"...an observe to ANOTHER address from the lease works (POSIX: the association dissolved; Windows: a fresh socket)"'
assert u.count(a) == 1
u = u.replace(a, '"...an observe to ANOTHER address from the lease works (a fresh socket: the tied one keeps its endpoint)"')
open(t, 'wb').write(u.encode('utf-8'))

m = 'D:/remote/remote/.claude/caplife/ka2n12_mut.py'
v = open(m, encoding='utf-8').read()
a = """    'noReopenWhenTied': ('standby_lease.hpp', [("    if (!DissolvePeer(socket_)) peerTied_ = true;", "    (void)DissolvePeer(socket_);  // MUTANT: a tied socket is not noted")]),"""
assert v.count(a) == 1
v = v.replace(a, """    'noReopenWhenTied': ('standby_lease.hpp', [("    peerTied_ = true;\\n", "    // MUTANT: a tied socket is not noted\\n")]),""")
open(m, 'w', encoding='utf-8').write(v)
print('ok')
