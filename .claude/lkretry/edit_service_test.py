import sys
p = sys.argv[1]
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')

helper = 'uint16_t st(UnlockStage s) { return static_cast<uint16_t>(s); }'
add_helper = '''// lk-ux-retry r1: the host's LockCredSupersede (kind 9) for `requestId` of the control session `cookie`.
SecureUnlockResult supersede(Conn& c, Os& os, uint32_t requestId, uint64_t cookie = 0xC00C1E) {
  SecureUnlockQuery q{};
  q.kind = static_cast<uint16_t>(SecureUnlockKind::LockCredSupersede);
  q.requestId = requestId;
  q.clientSessionCookie = cookie;
  const sus::UnlockPrimitives p = os.prims();
  sus::HandleLockCredSupersede(c.us, q, p, c.writer());
  SecureUnlockResult r{};
  if (c.last.size() == sizeof(r)) std::memcpy(&r, c.last.data(), sizeof(r));
  return r;
}

'''
assert helper in s
s = s.replace(helper, add_helper + helper, 1)

cap_old = '    check("...and one that cannot lists nothing", r.features == 0);'
cap_new = cap_old + '''
    sus::HandleCapabilities(q, os.prims(), [&](const void* d, size_t n) {
      out.assign(static_cast<const uint8_t*>(d), static_cast<const uint8_t*>(d) + n);
    });
    std::memcpy(&r, out.data(), sizeof(r));
    check("lk-ux-retry: the service that types also lists LockCredRetryV1 (it can end a waiting credential)",
          (r.features & remote60::native_poc::kSecureUnlockFeatureLockCredRetryV1) != 0);'''
assert cap_old in s
s = s.replace(cap_old, cap_new, 1)

anchor = '  std::printf("' + chr(92) + 'nRESULT: %s'
assert anchor in s
block = open(sys.argv[2], 'rb').read().decode('utf-8').replace('\r\n', '\n')
s = s.replace(anchor, block + anchor, 1)
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok crlf=%s' % crlf)
