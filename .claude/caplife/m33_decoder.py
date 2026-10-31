base = 'C:/work/gnlink-wt/feat-apk-0233/apps/android_direct_client/app/src/main/cpp/'

def load(n):
    s = open(base + n, 'rb').read().decode('utf-8')
    return s, ('\r\n' in s)

def save(n, s, crlf):
    if crlf:
        s = s.replace('\r\n', '\n').replace('\n', '\r\n')
    open(base + n, 'wb').write(s.encode('utf-8'))

# ---- cpp: the local-selection functions become owner-checked (*As) like every other selection call
s, crlf = load('android_video_decoder.cpp')
s = s.replace('\r\n', '\n')
a = s.index('<<<<<<< HEAD\n')
m = s.index('=======\n', a)
b = s.index('>>>>>>> 6d85ce2\n', m)
head = s[a + len('<<<<<<< HEAD\n'):m]
theirs = s[m + len('=======\n'):b]
local = head[:head.index('void AndroidVideoDecoderSink::PrepareForWindowSelection(uint64_t selectionGeneration) {')]
for old, new in [
    ('bool AndroidVideoDecoderSink::BeginLocalSelectionFor(uint64_t tag) {\n  LockedState lock(this);\n',
     '// keepalive-r2 merge: the local selection (apk-0232) owner-checked like every selection call (*As, keepalive-multi).\n'
     'bool AndroidVideoDecoderSink::BeginLocalSelectionForAs(uint64_t token, uint64_t tag) {\n  LockedState lock(this);\n  if (!OwnerLocked(token)) return false;\n'),
    ('uint64_t AndroidVideoDecoderSink::LocalSelectionBoundFor(uint64_t tag) {\n  LockedState lock(this);\n',
     'uint64_t AndroidVideoDecoderSink::LocalSelectionBoundForAs(uint64_t token, uint64_t tag) {\n  LockedState lock(this);\n  if (!OwnerLocked(token)) return 0;\n'),
    ('remote60::native_poc::ClientEncodedFrameSink::LocalCommit AndroidVideoDecoderSink::CommitLocalSelectionFor(\n'
     '    uint64_t tag, const std::function<void(uint64_t generation)>& commit) {\n  LockedState lock(this);\n',
     'remote60::native_poc::ClientEncodedFrameSink::LocalCommit AndroidVideoDecoderSink::CommitLocalSelectionForAs(\n'
     '    uint64_t token, uint64_t tag, const std::function<void(uint64_t generation)>& commit) {\n  LockedState lock(this);\n'
     '  if (!OwnerLocked(token)) return LocalCommit::Abandoned;  // not on screen: nothing of it completes\n'),
    ('void AndroidVideoDecoderSink::RefuseSelectionFor(uint64_t tag) {\n  LockedState lock(this);\n',
     'void AndroidVideoDecoderSink::RefuseSelectionForAs(uint64_t token, uint64_t tag) {\n  LockedState lock(this);\n  if (!OwnerLocked(token)) return;\n'),
]:
    assert local.count(old) == 1, old[:60]
    local = local.replace(old, new)
s = s[:a] + local + theirs + s[b + len('>>>>>>> 6d85ce2\n'):]
assert '<<<<<<<' not in s
save('android_video_decoder.cpp', s, crlf)

# ---- hpp: theirs, plus the local-selection overrides (any binding) and their *As forms
s, crlf = load('android_video_decoder.hpp')
s = s.replace('\r\n', '\n')
a = s.index('<<<<<<< HEAD\n')
m = s.index('=======\n', a)
b = s.index('>>>>>>> 6d85ce2\n', m)
theirs = s[m + len('=======\n'):b]
merged = theirs.replace('''  void AbortWindowSelection() { AbortWindowSelectionAs(kAnyBinding); }
''', '''  void AbortWindowSelection() { AbortWindowSelectionAs(kAnyBinding); }
  // apk-0232 r1/r3: a desktop pick at a host at the logon screen (no answer comes): the armed selection continues as a
  // local one, bound by its own first IDR that reaches the codec (SelectionAckGate::BeginLocal); its completion is
  // judged and recorded under the state Prepare/Abort take. keepalive-r2 merge: owner-checked (*As) as below.
  bool BeginLocalSelectionFor(uint64_t tag) override { return BeginLocalSelectionForAs(kAnyBinding, tag); }
  uint64_t LocalSelectionBoundFor(uint64_t tag) override { return LocalSelectionBoundForAs(kAnyBinding, tag); }
  void RefuseSelectionFor(uint64_t tag) override { RefuseSelectionForAs(kAnyBinding, tag); }
  LocalCommit CommitLocalSelectionFor(uint64_t tag, const std::function<void(uint64_t generation)>& commit) override {
    return CommitLocalSelectionForAs(kAnyBinding, tag, commit);
  }
''')
merged = merged.replace('''  bool AbortWindowSelectionAs(uint64_t token);
''', '''  bool AbortWindowSelectionAs(uint64_t token);
  bool BeginLocalSelectionForAs(uint64_t token, uint64_t tag);
  uint64_t LocalSelectionBoundForAs(uint64_t token, uint64_t tag);
  void RefuseSelectionForAs(uint64_t token, uint64_t tag);
  LocalCommit CommitLocalSelectionForAs(uint64_t token, uint64_t tag, const std::function<void(uint64_t generation)>& commit);
''')
assert merged != theirs
s = s[:a] + merged + s[b + len('>>>>>>> 6d85ce2\n'):]
save('android_video_decoder.hpp', s, crlf)

# ---- the per-PC adapter forwards the local selection with its token
s, crlf = load('entry_sink_adapter.hpp')
s = s.replace('\r\n', '\n')
old = '''    if (const uint64_t t = Token()) decoder_->OnWindowSelectionControlResultForAs(t, msg, requestTag);
  }
'''
assert s.count(old) == 1
s = s.replace(old, old + '''  // apk-0232 local selection (keepalive-r2 merge): only for the PC on screen; a PC not bound selects nothing.
  bool BeginLocalSelectionFor(uint64_t tag) override {
    const uint64_t t = Token();
    return t != 0 && decoder_->BeginLocalSelectionForAs(t, tag);
  }
  uint64_t LocalSelectionBoundFor(uint64_t tag) override {
    const uint64_t t = Token();
    return t != 0 ? decoder_->LocalSelectionBoundForAs(t, tag) : 0;
  }
  void RefuseSelectionFor(uint64_t tag) override {
    if (const uint64_t t = Token()) decoder_->RefuseSelectionForAs(t, tag);
  }
  LocalCommit CommitLocalSelectionFor(uint64_t tag, const std::function<void(uint64_t generation)>& commit) override {
    const uint64_t t = Token();
    return t != 0 ? decoder_->CommitLocalSelectionForAs(t, tag, commit) : LocalCommit::Abandoned;
  }
''')
save('entry_sink_adapter.hpp', s, crlf)
print('ok')
