import io

p = 'apps/native_poc/src/viewer_startup.cpp'
d = io.open(p, encoding='utf-8').read()
lines = d.split('\n')

# Locate by content rather than by a long literal: the region carries a C++ escape that is easy to
# get wrong twice over in a Python string.
start = None
for i, line in enumerate(lines):
    if line.strip() == 'std::string sessionToken = ctx.args.directorySession;':
        start = i
        break
assert start is not None, 'sessionToken line'
end = start
while lines[end].strip() != '}':
    end += 1
assert end - start < 12, 'block too long: %d' % (end - start)

replacement = [
    '    std::string sessionToken = ctx.args.directorySession;',
    '    // Where the directory says observations go. It rides on the login response, so the branch',
    '    // that reuses a cached session never sees it -- and on an https directory that would mean',
    '    // refusing to observe, so a reconnect would fail where a fresh connect succeeds. The health',
    '    // probe carries the same value and needs no session, so that path asks for it there.',
    '    remote60::native_poc::directory::ObserveEndpoint advertised;',
    '    if (sessionToken.empty()) {',
    '      if (!remote60::native_poc::directory_login(ctx.args.directoryUrl, ctx.args.directoryAccount,',
    '                                                 ctx.args.directoryPassword, &sessionToken,',
    '                                                 &directoryError, &advertised)) {',
    '        std::cerr << "[native-video-client] directory login failed: " << directoryError << "\\n";',
    '        if (ctx.dec.mfStarted) MFShutdown();',
    '        return 3;',
    '      }',
    '    } else {',
    '      // Absence is not an error: an older directory says nothing, and observe_port_for has a',
    '      // rule for exactly that.',
    '      std::string ignored;',
    '      remote60::native_poc::directory_observe_from_health(ctx.args.directoryUrl, &advertised,',
    '                                                          &ignored);',
    '    }',
]
lines[start:end + 1] = replacement
d = '\n'.join(lines)

old = '    request.hostId = hostId;'
assert old in d, 'request'
d = d.replace(old, old + '\n    request.advertised = advertised;', 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('viewer wired')
