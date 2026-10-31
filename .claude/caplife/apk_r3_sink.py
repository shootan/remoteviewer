wt = 'C:/work/gnlink-wt/apk-prelogin-select/apps/'
def edit(rel, pairs):
    p = wt + rel
    s = open(p, 'rb').read().decode('utf-8')
    crlf = '\r\n' in s
    s = s.replace('\r\n', '\n')
    for old, new in pairs:
        assert s.count(old) == 1, (rel, s.count(old), old[:90])
        s = s.replace(old, new)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'wb').write(s.encode('utf-8'))

edit('android_direct_client/app/src/main/cpp/android_video_decoder.hpp', [
    ('  void RefuseSelectionFor(uint64_t tag) override;\n',
     '  void RefuseSelectionFor(uint64_t tag) override;\n'
     '  LocalCommit CommitLocalSelectionFor(uint64_t tag, const std::function<void(uint64_t generation)>& commit) override;\n'),
])
edit('android_direct_client/app/src/main/cpp/android_video_decoder.cpp', [
    ('''void AndroidVideoDecoderSink::RefuseSelectionFor(uint64_t tag) {''',
     '''// apk-0232 r3 (Codex N1): judged and recorded under the state Prepare/Abort (the UI's JNI calls) take -- a cancel
// cannot finish between "still this selection, bound" and the panel's record.
remote60::native_poc::ClientEncodedFrameSink::LocalCommit AndroidVideoDecoderSink::CommitLocalSelectionFor(
    uint64_t tag, const std::function<void(uint64_t generation)>& commit) {
  LockedState lock(this);
  if (tag == 0 || gate_.pending() != tag) return LocalCommit::Abandoned;
  const uint64_t generation = gate_.LocalBoundFor(tag);
  if (generation == 0) return LocalCommit::Unbound;
  commit(generation);
  return LocalCommit::Committed;
}

void AndroidVideoDecoderSink::RefuseSelectionFor(uint64_t tag) {'''),
])
print('ok')
