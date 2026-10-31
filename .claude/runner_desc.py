import io

# ---------------------------------------------------------------- helper understands descriptors
p = 'apps/native_poc/src/update_credential_hop_helper.cpp'
d = io.open(p, encoding='utf-8').read()

old = '''#include "update_credential_channel.hpp"'''
new = '''#include "update_credential_channel.hpp"
#include "update_endpoint.hpp"'''
assert d.count(old) == 1, 'include'
d = d.replace(old, new, 1)

old2 = '''    record(recordPath, "worker-received=" + payload);
    return 0;'''
new2 = '''    // The same check updater_main makes: the frame and the arguments have to agree. The frame
    // arrived over a channel that proved who was listening; the arguments proved nothing, so
    // neither is trusted alone and a disagreement is the answer.
    const std::wstring expectUrl = value_of(args, L"--expect-url");
    if (!expectUrl.empty()) {
      UpdateEndpoint fromFrame;
      std::string decodeError;
      if (!decode_update_descriptor(payload, &fromFrame, &decodeError)) {
        log_line(logPath, "the credential frame did not parse: " + decodeError);
        record(recordPath, "worker-frame-bad=" + decodeError);
        return 6;
      }
      if (fromFrame.url != narrow(expectUrl)) {
        log_line(logPath, "the credential was issued for a different url than this run was given");
        record(recordPath, "worker-url-mismatch=1");
        return 6;
      }
      record(recordPath, "worker-owner=" + fromFrame.ownerKey + "/" +
                             std::to_string(fromFrame.ownerEpoch));
    }
    record(recordPath, "worker-received=" + payload);
    return 0;'''
assert d.count(old2) == 1, 'worker check'
d = d.replace(old2, new2, 1)

old3 = '''  if (has_flag(args, L"--child-never-reads")) command += L" --never-read";'''
new3 = '''  if (has_flag(args, L"--child-never-reads")) command += L" --never-read";
  if (!value_of(args, L"--expect-url").empty()) {
    command += L" --expect-url \\"" + value_of(args, L"--expect-url") + L"\\"";
  }'''
assert d.count(old3) == 1, 'forward'
d = d.replace(old3, new3, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok helper')
