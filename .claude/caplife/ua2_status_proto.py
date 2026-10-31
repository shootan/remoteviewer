src = 'C:\\work\\gnlink-wt\\fix-capture-lifecycle\\apps\\native_poc\\src\\'

def edit(name, pairs):
    p = src + name
    s = open(p, 'rb').read().decode('utf-8')
    crlf = '\r\n' in s
    s = s.replace('\r\n', '\n')
    for old, new in pairs:
        assert s.count(old) == 1, (name, s.count(old), old[:90])
        s = s.replace(old, new)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'wb').write(s.encode('utf-8'))

edit('poc_protocol.hpp', [
    ('  ControlLockCredRetryChallengeRequest = 89,\n',
     '''  ControlLockCredRetryChallengeRequest = 89,
  // capture-lifecycle r1 (UA-2, Codex §1; number assigned by the verifier): host -> viewer, the answer to a
  // ControlDesktopBackendRequest (34) that set kDesktopBackendRequestFlagStatusV1 -- what this connection's
  // session asked for, what may run for it and what runs now. Only ever an ANSWER (the control channel is
  // request/response): a client follows a later change by asking again (backend 0 = ask only). Sent only by a
  // host with kCaptureFlagDesktopBackendStatusV1, only to a request that asked for it.
  ControlDesktopBackendStatus = 90,
'''),
    ('constexpr uint32_t kCaptureFlagLockCredRetryV1 = 0x80000u;\n',
     '''constexpr uint32_t kCaptureFlagLockCredRetryV1 = 0x80000u;
// capture-lifecycle r1 (UA-2, assigned by the verifier): a capture backend asked for applies to THIS session
// only (a new session starts from the host's default), and a request may ask for its answer
// (ControlDesktopBackendStatus 90; backend 0 = ask only). Without it a client cannot know either.
constexpr uint32_t kCaptureFlagDesktopBackendStatusV1 = 0x100000u;
'''),
    ('''struct ControlDesktopBackendRequestMessage {
  MessageHeader header{};
  uint32_t seq = 0;
  uint16_t backend = 0;  // 1:dxgi, 2:wgc
  uint16_t flags = 0;
  uint64_t clientSendQpcUs = 0;
};
''',
     '''struct ControlDesktopBackendRequestMessage {
  MessageHeader header{};
  uint32_t seq = 0;
  uint16_t backend = 0;  // 1:dxgi, 2:wgc; 0 = ask only (with kDesktopBackendRequestFlagStatusV1, to a 0x100000 host)
  uint16_t flags = 0;    // kDesktopBackendRequestFlag*
  uint64_t clientSendQpcUs = 0;
};
// capture-lifecycle r1: answer this request with ControlDesktopBackendStatus (90). Set only toward a host with
// kCaptureFlagDesktopBackendStatusV1 -- an older host answers 34 with nothing, and a client waiting would stall.
constexpr uint16_t kDesktopBackendRequestFlagStatusV1 = 0x1u;

// host -> viewer (90): the backend of this session, as the host's main loop last applied it.
struct ControlDesktopBackendStatusMessage {
  MessageHeader header{};
  uint32_t seq = 0;        // the request's (34) seq
  uint16_t requested = 0;  // what this session asked for (or the host's default): 1 dxgi, 2 wgc, 3 gdi
  uint16_t effective = 0;  // what may run for that (the DXGI quarantine applied)
  uint16_t active = 0;     // what runs now
  uint16_t flags = 0;      // kDesktopBackendStatusFlag*
  uint32_t changeCount = 0;  // bumps on every change of the four fields above (in this host process)
  uint32_t reserved = 0;
  uint64_t hostSendQpcUs = 0;
};
static_assert(sizeof(ControlDesktopBackendStatusMessage) == 40, "desktop backend status wire drift");
constexpr uint16_t kDesktopBackendStatusFlagSessionScoped = 0x1u;  // the choice ends with this session
constexpr uint16_t kDesktopBackendStatusFlagDxgiQuarantined = 0x2u;
constexpr uint16_t kDesktopBackendStatusFlagWindowTarget = 0x4u;   // a window is captured: WGC whatever was asked
constexpr uint16_t kDesktopBackendStatusFlagBeforeLogin = 0x8u;    // the pre-login worker: DXGI only, requests ignored
constexpr uint16_t kDesktopBackendStatusFlagNotYetApplied = 0x10u; // the request was not applied in time: as it is now
'''),
])
print('ok')
