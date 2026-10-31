import re

base = 'apps/android_direct_client/app/src/main/cpp/'


def load(p):
    s = open(p, encoding='utf-8', newline='').read()
    return s.replace('\r\n', '\n'), '\r\n' in s


def save(p, s, crlf):
    open(p, 'w', encoding='utf-8', newline='').write(s.replace('\n', '\r\n') if crlf else s)


# ---------------------------------------------------------------- the published state carries its binding
p = 'apps/native_poc/src/client_decode_snapshot.hpp'
s, crlf = load(p)
m = re.search(r'struct ClientDecodeState \{\n', s)
assert m
s = s[:m.end()] + '  uint64_t binding = 0;  // keepalive-multi r1: whose decode this is (the active binding when published)\n' + s[m.end():]
save(p, s, crlf)

# ---------------------------------------------------------------- header
p = base + 'android_video_decoder.hpp'
s, crlf = load(p)
old_pub = s[s.index('  void OnEncodedH264Frame(remote60::native_poc::UdpH264AssembledFrame&& frame) override;'):s.index('  void AbortWindowSelection();\n') + len('  void AbortWindowSelection();\n')]
new_pub = '''  // keepalive-multi r1: one decoder and one Surface, used by one PC connection at a time -- its BINDING (a
  // process-unique token for: entry handle, connection generation, activation, Surface). Each PC's controller talks
  // to this sink through its own adapter carrying the token it was bound with; every call below checks that token
  // against the current binding INSIDE this sink's lock, before it touches anything (no check-then-call gap). A
  // parked or closed PC's late callback -- a reset, a frame, a selection answer, a stats drain -- does nothing.
  // kAnyBinding: the direct (unbound) caller of the virtual methods (tests, single-session use) -- always the owner.
  static constexpr uint64_t kAnyBinding = ~0ull;
  // A new binding: the codec, the selection gate and every count start over, for this token only. 0 = nobody.
  void Bind(uint64_t token);
  // Ends `token`'s binding if it is still the current one (true); a stale token changes nothing.
  bool Unbind(uint64_t token);
  uint64_t Binding();

  void OnEncodedH264Frame(remote60::native_poc::UdpH264AssembledFrame&& frame) override {
    OnEncodedH264FrameAs(kAnyBinding, std::move(frame));
  }
  void OnVideoStreamReset() override { OnVideoStreamResetAs(kAnyBinding); }
  void OnVideoDiscontinuity() override { OnVideoDiscontinuityAs(kAnyBinding); }
  bool ConsumeDecoderKeyframeRequest() override { return ConsumeDecoderKeyframeRequestAs(kAnyBinding); }
  bool DrainPresentationStats(remote60::native_poc::ClientPresentationStats* out) override {
    return DrainPresentationStatsAs(kAnyBinding, out);
  }
  bool DrainDecodeStats(remote60::native_poc::ClientDecodeStats* out) override {
    return DrainDecodeStatsAs(kAnyBinding, out);
  }
  uint64_t DecodeGeneration() override { return DecodeGenerationAs(kAnyBinding); }
  void OnDecodeEpisodeStart() override { OnDecodeEpisodeStartAs(kAnyBinding); }
  void OnWindowSelectionControlResult(const remote60::native_poc::ControlWindowSelectedMessage& msg) override {
    OnWindowSelectionControlResultAs(kAnyBinding, msg);
  }
  void PrepareForWindowSelection(uint64_t selectionGeneration) {
    PrepareForWindowSelectionAs(kAnyBinding, selectionGeneration);
  }
  // t-970r4zgo: the selection a select request is queued under, and its answer for that selection.
  uint64_t CurrentSelectionTag() override { return CurrentSelectionTagAs(kAnyBinding); }
  uint64_t KeyframeOwedFor() override { return KeyframeOwedForAs(kAnyBinding); }
  void OnWindowSelectionControlResultFor(const remote60::native_poc::ControlWindowSelectedMessage& msg,
                                         uint64_t requestTag) override {
    OnWindowSelectionControlResultForAs(kAnyBinding, msg, requestTag);
  }
  void AbortWindowSelection() { AbortWindowSelectionAs(kAnyBinding); }

  // The same, for `token` only (a non-owner gets the empty answer: no frame taken, nothing reset, false / 0).
  void OnEncodedH264FrameAs(uint64_t token, remote60::native_poc::UdpH264AssembledFrame&& frame);
  void OnVideoStreamResetAs(uint64_t token);
  void OnVideoDiscontinuityAs(uint64_t token);
  bool ConsumeDecoderKeyframeRequestAs(uint64_t token);
  bool DrainPresentationStatsAs(uint64_t token, remote60::native_poc::ClientPresentationStats* out);
  bool DrainDecodeStatsAs(uint64_t token, remote60::native_poc::ClientDecodeStats* out);
  uint64_t DecodeGenerationAs(uint64_t token);
  void OnDecodeEpisodeStartAs(uint64_t token);
  void OnWindowSelectionControlResultAs(uint64_t token, const remote60::native_poc::ControlWindowSelectedMessage& msg);
  bool PrepareForWindowSelectionAs(uint64_t token, uint64_t selectionGeneration);
  uint64_t CurrentSelectionTagAs(uint64_t token);
  uint64_t KeyframeOwedForAs(uint64_t token);
  void OnWindowSelectionControlResultForAs(uint64_t token, const remote60::native_poc::ControlWindowSelectedMessage& msg,
                                           uint64_t requestTag);
  bool AbortWindowSelectionAs(uint64_t token);
'''
s = s.replace(old_pub, new_pub)
s = s.replace('  void PumpCodecLocked();\n', '  bool OwnerLocked(uint64_t token) const { return token == kAnyBinding || token == binding_; }\n  void ResetForBindingLocked();\n  void PumpCodecLocked();\n', 1)
s = s.replace('  std::mutex mu_;\n', '  std::mutex mu_;\n  uint64_t binding_ = 0;  // keepalive-multi r1: under mu_\n', 1)
save(p, s, crlf)

# ---------------------------------------------------------------- implementation
p = base + 'android_video_decoder.cpp'
s, crlf = load(p)


def as_def(sig_old, sig_new, guard_after_lock, ret_default):
    """Renames a definition and puts the owner check right after its first LockedState."""
    global s
    i = s.index(sig_old)
    s = s[:i] + sig_new + s[i + len(sig_old):]
    j = s.index('LockedState lock(this);', i)
    k = j + len('LockedState lock(this);')
    ind = s[s.rindex('\n', 0, j) + 1:j]
    s = s[:k] + '\n' + ind + 'if (!OwnerLocked(token)) return' + (' ' + ret_default if ret_default else '') + ';' + s[k:]


as_def('void AndroidVideoDecoderSink::OnEncodedH264Frame(remote60::native_poc::UdpH264AssembledFrame&& frame) {',
       'void AndroidVideoDecoderSink::OnEncodedH264FrameAs(uint64_t token, remote60::native_poc::UdpH264AssembledFrame&& frame) {',
       True, '')
as_def('void AndroidVideoDecoderSink::OnVideoStreamReset() {', 'void AndroidVideoDecoderSink::OnVideoStreamResetAs(uint64_t token) {', True, '')
as_def('void AndroidVideoDecoderSink::OnVideoDiscontinuity() {', 'void AndroidVideoDecoderSink::OnVideoDiscontinuityAs(uint64_t token) {', True, '')
as_def('bool AndroidVideoDecoderSink::DrainPresentationStats(\n    remote60::native_poc::ClientPresentationStats* out) {',
       'bool AndroidVideoDecoderSink::DrainPresentationStatsAs(\n    uint64_t token, remote60::native_poc::ClientPresentationStats* out) {', True, 'false')
as_def('void AndroidVideoDecoderSink::OnDecodeEpisodeStart() {', 'void AndroidVideoDecoderSink::OnDecodeEpisodeStartAs(uint64_t token) {', True, '')
as_def('bool AndroidVideoDecoderSink::ConsumeDecoderKeyframeRequest() {', 'bool AndroidVideoDecoderSink::ConsumeDecoderKeyframeRequestAs(uint64_t token) {', True, 'false')
as_def('uint64_t AndroidVideoDecoderSink::KeyframeOwedFor() {', 'uint64_t AndroidVideoDecoderSink::KeyframeOwedForAs(uint64_t token) {', True, '0')
as_def('uint64_t AndroidVideoDecoderSink::CurrentSelectionTag() {', 'uint64_t AndroidVideoDecoderSink::CurrentSelectionTagAs(uint64_t token) {', True, '0')
as_def('void AndroidVideoDecoderSink::OnWindowSelectionControlResultFor(\n    const remote60::native_poc::ControlWindowSelectedMessage& msg, uint64_t requestTag) {',
       'void AndroidVideoDecoderSink::OnWindowSelectionControlResultForAs(\n    uint64_t token, const remote60::native_poc::ControlWindowSelectedMessage& msg, uint64_t requestTag) {', True, '')
as_def('void AndroidVideoDecoderSink::PrepareForWindowSelection(uint64_t selectionGeneration) {',
       'bool AndroidVideoDecoderSink::PrepareForWindowSelectionAs(uint64_t token, uint64_t selectionGeneration) {', True, 'false')
as_def('void AndroidVideoDecoderSink::AbortWindowSelection() {', 'bool AndroidVideoDecoderSink::AbortWindowSelectionAs(uint64_t token) {', True, 'false')
# the two that now return bool: true at their ends
s = s.replace('''  std::snprintf(line, sizeof(line), "selection prepare localGen=%llu",
                static_cast<unsigned long long>(gate_.pending()));
  log_info(line);
}''', '''  std::snprintf(line, sizeof(line), "selection prepare localGen=%llu",
                static_cast<unsigned long long>(gate_.pending()));
  log_info(line);
  return true;
}''')
s = s.replace('''  decoderKeyframeRequest_ = true;
  log_info("selection aborted");
}''', '''  decoderKeyframeRequest_ = true;
  log_info("selection aborted");
  return true;
}''')

# the untagged selection answer: owner check where it reads the tag, then the tagged one for the same token
old = '''void AndroidVideoDecoderSink::OnWindowSelectionControlResult(
    const remote60::native_poc::ControlWindowSelectedMessage& msg) {
  // Untagged: taken as the answer to whatever selection is armed (the old behaviour).
  uint64_t tag = 0;
  {
    LockedState lock(this);
    tag = gate_.pending();
  }
  OnWindowSelectionControlResultFor(msg, tag);
}'''
new = '''void AndroidVideoDecoderSink::OnWindowSelectionControlResultAs(
    uint64_t token, const remote60::native_poc::ControlWindowSelectedMessage& msg) {
  // Untagged: taken as the answer to whatever selection is armed (the old behaviour).
  uint64_t tag = 0;
  {
    LockedState lock(this);
    if (!OwnerLocked(token)) return;
    tag = gate_.pending();
  }
  OnWindowSelectionControlResultForAs(token, msg, tag);  // checks the owner again, under the lock it mutates in
}'''
assert s.count(old) == 1
s = s.replace(old, new)

# lock-free reads: the binding travels in the published copy, compared inside the same consistent read
old = '''uint64_t AndroidVideoDecoderSink::DecodeGeneration() { return decodeSnapshot_.generation(); }'''
new = '''uint64_t AndroidVideoDecoderSink::DecodeGenerationAs(uint64_t token) {
  if (token == kAnyBinding) return decodeSnapshot_.generation();
  remote60::native_poc::ClientDecodeState s;
  if (!decodeSnapshot_.Read(&s) || s.binding != token) return 0;  // not this binding's decoder: nothing of it
  return s.generation;
}'''
assert s.count(old) == 1
s = s.replace(old, new)
old = '''bool AndroidVideoDecoderSink::DrainDecodeStats(remote60::native_poc::ClientDecodeStats* out) {
  if (!out) return false;
  remote60::native_poc::ClientDecodeState s;
  if (!decodeSnapshot_.Read(&s)) return false;  // a writer kept overlapping: no copy this window'''
new = '''bool AndroidVideoDecoderSink::DrainDecodeStatsAs(uint64_t token, remote60::native_poc::ClientDecodeStats* out) {
  if (!out) return false;
  remote60::native_poc::ClientDecodeState s;
  if (!decodeSnapshot_.Read(&s)) return false;  // a writer kept overlapping: no copy this window
  // keepalive-multi r1: another PC's decode is not this PC's report (checked in the same consistent copy).
  if (token != kAnyBinding && s.binding != token) return false;'''
assert s.count(old) == 1
s = s.replace(old, new)

# publish: the binding in the copy, and in the generation key (a new binding is a new measurement)
old = '''  uint64_t key = reinterpret_cast<uintptr_t>(window_);'''
new = '''  uint64_t key = reinterpret_cast<uintptr_t>(window_);
  key = key * 1000003ULL ^ binding_;  // keepalive-multi r1: a new binding starts a new measurement'''
assert s.count(old) == 1
s = s.replace(old, new)
old = '''  remote60::native_poc::ClientDecodeState s;
  s.valid = valid;'''
new = '''  remote60::native_poc::ClientDecodeState s;
  s.binding = binding_;
  s.valid = valid;'''
assert s.count(old) == 1
s = s.replace(old, new)

# Bind / Unbind / Binding
old = 'void AndroidVideoDecoderSink::OnVideoStreamResetAs(uint64_t token) {'
new = '''// keepalive-multi r1: everything a binding owned goes with it -- the codec and its reference pictures, the
// selection gate (an answer or an owed key of the last PC is not this one's), the counts, the presentation window, a
// pending key request. A new binding starts like a new stream (D1: its codec takes no delta before an IDR).
void AndroidVideoDecoderSink::ResetForBindingLocked() {
  ResetCodecLocked();
  configuredWidth_ = 0;
  configuredHeight_ = 0;
  outputWidth_ = 0;
  outputHeight_ = 0;
  inputFrameCount_ = 0;
  outputFrameCount_ = 0;
  gate_.Reset();
  readySelectionGeneration_ = 0;
  latestInputStreamGeneration_ = 0;
  latestOutputStreamGeneration_ = 0;
  lastOutputPresentationUs_ = 0;
  staleFrameDropCount_ = 0;
  csd0_.clear();
  csd1_.clear();
  presentGapsUs_.clear();
  presentScheduledCount_ = 0;
  presentImmediateCount_ = 0;
  decoderKeyframeRequest_ = false;
}

void AndroidVideoDecoderSink::Bind(uint64_t token) {
  LockedState lock(this);
  binding_ = token;
  ResetForBindingLocked();
  char line[96];
  std::snprintf(line, sizeof(line), "decoder bound binding=%llu", static_cast<unsigned long long>(token));
  log_info(line);
}

bool AndroidVideoDecoderSink::Unbind(uint64_t token) {
  LockedState lock(this);
  if (token == 0 || binding_ != token) return false;  // a stale unbind (a closed PC's late call) changes nothing
  binding_ = 0;
  ResetForBindingLocked();
  log_info("decoder unbound");
  return true;
}

uint64_t AndroidVideoDecoderSink::Binding() {
  LockedState lock(this);
  return binding_;
}

void AndroidVideoDecoderSink::OnVideoStreamResetAs(uint64_t token) {'''
assert s.count(old) == 1
s = s.replace(old, new)
save(p, s, crlf)
print('ok')
