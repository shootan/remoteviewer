import sys, shutil
p = 'C:/work/gnlink-wt/apk-prelogin-select/apps/native_poc/src/native_video_client_session.cpp'
bak = 'D:/remote/remote/.claude/caplife/apk/session_cpp.r3.bak'
if sys.argv[1] == 'apply':
    shutil.copyfile(p, bak)
    s = open(p, 'rb').read().decode('utf-8')
    old = 'const auto result = encodedFrameSink_->CommitLocalSelectionFor(tag, [&](uint64_t generation) {'
    new = ('const auto result = [&](const std::function<void(uint64_t)>& commitFn) {  /* MUTANT r2 shape: read, then record outside the sink */\n'
           '    if (encodedFrameSink_->CurrentSelectionTag() != tag) return ClientEncodedFrameSink::LocalCommit::Abandoned;\n'
           '    const uint64_t g = encodedFrameSink_->LocalSelectionBoundFor(tag);\n'
           '    if (g == 0) return ClientEncodedFrameSink::LocalCommit::Unbound;\n'
           '    commitFn(g);\n'
           '    return ClientEncodedFrameSink::LocalCommit::Committed;\n'
           '  }([&](uint64_t generation) {')
    assert s.count(old) == 1
    open(p, 'wb').write(s.replace(old, new).encode('utf-8'))
else:
    shutil.copyfile(bak, p)
print(sys.argv[1], 'ok')
