import io, re
def read(p): return io.open(p, encoding='utf-8').read()
def write(p, s):
    crlf = (b'\r\n' in open(p, 'rb').read())
    io.open(p, 'w', encoding='utf-8', newline=('\r\n' if crlf else '')).write(s)
def rep(path, old, new, count=1):
    s = read(path); n = s.count(old)
    assert n == count, f"{path}: expected {count}, found {n} for: {old[:70]!r}"
    write(path, s.replace(old, new)); print("edited", path)

H='apps/native_poc/src/mf_h264_codec.hpp'; C='apps/native_poc/src/mf_h264_codec.cpp'
# 1. header: provenance on AU / decoded frame, setter + lockstep deque on both classes
if 'pendingInputSynthetic_' in read(H):
    print('header already edited; skipping')
else:
  rep(H, """struct H264AccessUnit {
  std::vector<uint8_t> bytes;
  bool keyFrame = false;
  int64_t sampleTimeHns = 0;
  bool sampleTimeFromOutput = false;
};""", """struct H264AccessUnit {
  std::vector<uint8_t> bytes;
  bool keyFrame = false;
  int64_t sampleTimeHns = 0;
  bool sampleTimeFromOutput = false;
  // Provenance of the INPUT that produced this AU (host kick / static refresh = synthetic). Carried
  // through the accepted-input FIFO next to the timestamp, because an async MFT returns an older
  // input's AU during the current call -- the call's own flag would land on the wrong AU. (0.2.97)
  bool synthetic = false;
};""")
if False: rep(H, """  int64_t sampleTimeHns = 0;
  bool sampleTimeFromOutput = false;
  std::vector<uint8_t> bytes;
  // Hardware decoders expose their NV12 output as a D3D11 surface.""", """  int64_t sampleTimeHns = 0;
  bool sampleTimeFromOutput = false;
  // Provenance of the input AU this output came from (wire synthetic flag), mapped through the
  // pending-input FIFO like the timestamp, so a delayed decoder output keeps its own flag. (0.2.97)
  bool synthetic = false;
  std::vector<uint8_t> bytes;
  // Hardware decoders expose their NV12 output as a D3D11 surface.""")
if False: rep(H, """  const char* backend_name() const { return backendName_; }
  bool using_hardware() const { return usingHardware_; }
  void shutdown();

 private:
  bool configure_types();""", """  const char* backend_name() const { return backendName_; }
  bool using_hardware() const { return usingHardware_; }
  // Provenance of the NEXT input (kick / static refresh = true); rides the accepted-input FIFO so
  // each AU reports the flag of the input that produced it. (0.2.97)
  void set_next_input_synthetic(bool synthetic) { nextInputSynthetic_ = synthetic; }
  void shutdown();

 private:
  bool configure_types();""")
if False: rep(H, """  std::deque<int64_t> pendingInputSampleTimesHns_;
  std::vector<uint8_t> sequenceHeaderAnnexb_;""", """  std::deque<int64_t> pendingInputSampleTimesHns_;
  std::deque<bool> pendingInputSynthetic_;  // lockstep with pendingInputSampleTimesHns_ (0.2.97)
  bool nextInputSynthetic_ = false;
  std::vector<uint8_t> sequenceHeaderAnnexb_;""")
if False: rep(H, """  const char* backend_name() const { return backendName_; }
  bool using_hardware() const { return usingHardware_; }
  void reset();
  void shutdown();""", """  const char* backend_name() const { return backendName_; }
  bool using_hardware() const { return usingHardware_; }
  // Provenance of the NEXT input AU (wire synthetic flag); mapped to the decoded frame through the
  // pending-input FIFO like its timestamp. (0.2.97)
  void set_next_input_synthetic(bool synthetic) { nextInputSynthetic_ = synthetic; }
  void reset();
  void shutdown();""")
if False: rep(H, """  uint64_t missingOutputTimestampCount_ = 0;
  std::deque<int64_t> pendingInputSampleTimesHns_;
  uint32_t d3dManagerResetToken_ = 0;""", """  uint64_t missingOutputTimestampCount_ = 0;
  std::deque<int64_t> pendingInputSampleTimesHns_;
  std::deque<bool> pendingInputSynthetic_;  // lockstep with pendingInputSampleTimesHns_ (0.2.97)
  bool nextInputSynthetic_ = false;
  uint32_t d3dManagerResetToken_ = 0;""")

# 2. codec.cpp: lockstep at every clear / pop / push site
s = read(C)
n_clear = s.count("pendingInputSampleTimesHns_.clear();"); assert n_clear == 5, n_clear
s = re.sub(r"^(\s*)pendingInputSampleTimesHns_\.clear\(\);", r"\1pendingInputSampleTimesHns_.clear();\n\1pendingInputSynthetic_.clear();", s, flags=re.M)
n_pop = s.count("pendingInputSampleTimesHns_.pop_front();"); assert n_pop == 5, n_pop
s = re.sub(r"^(\s*)pendingInputSampleTimesHns_\.pop_front\(\);", r"\1pendingInputSampleTimesHns_.pop_front();\n\1if (!pendingInputSynthetic_.empty()) pendingInputSynthetic_.pop_front();", s, flags=re.M)
n_push = s.count("pendingInputSampleTimesHns_.push_back(sampleTime);"); assert n_push == 2, n_push
s = s.replace("pendingInputSampleTimesHns_.push_back(sampleTime);", "pendingInputSampleTimesHns_.push_back(sampleTime);\n  pendingInputSynthetic_.push_back(nextInputSynthetic_);")
write(C, s); print("codec.cpp lockstep applied")
# encoder AU consume: read the flag before the pop
rep(C, """        int64_t normalizedAuSampleTimeHns = sampleTime;
        if (!pendingInputSampleTimesHns_.empty()) {
          normalizedAuSampleTimeHns = pendingInputSampleTimesHns_.front();
          pendingInputSampleTimesHns_.pop_front();
          if (!pendingInputSynthetic_.empty()) pendingInputSynthetic_.pop_front();
        } else {""", """        int64_t normalizedAuSampleTimeHns = sampleTime;
        bool auSynthetic = nextInputSynthetic_;  // FIFO empty -> this call's input (old behaviour)
        if (!pendingInputSampleTimesHns_.empty()) {
          normalizedAuSampleTimeHns = pendingInputSampleTimesHns_.front();
          if (!pendingInputSynthetic_.empty()) auSynthetic = pendingInputSynthetic_.front();
          pendingInputSampleTimesHns_.pop_front();
          if (!pendingInputSynthetic_.empty()) pendingInputSynthetic_.pop_front();
        } else {""")
rep(C, """        au.sampleTimeHns = normalizedAuSampleTimeHns;
        au.sampleTimeFromOutput = sampleTimeFromOutput;""", """        au.sampleTimeHns = normalizedAuSampleTimeHns;
        au.sampleTimeFromOutput = sampleTimeFromOutput;
        au.synthetic = auSynthetic;""")
# decoder consume
rep(C, """        int64_t mappedInputSampleTimeHns = 0;
        if (!pendingInputSampleTimesHns_.empty()) {
          mappedInputSampleTimeHns = pendingInputSampleTimesHns_.front();
          pendingInputSampleTimesHns_.pop_front();
          if (!pendingInputSynthetic_.empty()) pendingInputSynthetic_.pop_front();
        }""", """        int64_t mappedInputSampleTimeHns = 0;
        bool mappedSynthetic = nextInputSynthetic_;  // FIFO empty -> this call's input
        if (!pendingInputSampleTimesHns_.empty()) {
          mappedInputSampleTimeHns = pendingInputSampleTimesHns_.front();
          if (!pendingInputSynthetic_.empty()) mappedSynthetic = pendingInputSynthetic_.front();
          pendingInputSampleTimesHns_.pop_front();
          if (!pendingInputSynthetic_.empty()) pendingInputSynthetic_.pop_front();
        }""")
s = read(C)
anchor = "          DecodedFrameNv12 frame{};"
i = s.index(anchor, s.index("bool mappedSynthetic = nextInputSynthetic_;"))
s = s[:i] + anchor + "\n          frame.synthetic = mappedSynthetic;" + s[i+len(anchor):]
write(C, s); print("decoder frame.synthetic set")

# 3. h264.cpp: provenance + watermark at hand-off (Codex 16); drop the post-encode note
rep('apps/native_poc/src/host_stage_encode_send_h264.cpp',
"""    const uint64_t encodeInputUs = captureStampUs;
""", """    const uint64_t encodeInputUs = captureStampUs;
    // Provenance rides the encoder's accepted-input FIFO so the wire synthetic flag lands on the AU
    // this input produces (an async MFT emits an older input's AU during this call). The stamp is
    // also the floor for the next real frame's stamp (KickState::ClampRealStamp) from the moment it
    // is handed over -- an input the MFT accepted but whose call failed later still sits in its
    // timeline. (0.2.97)
    encoder.codec.set_next_input_synthetic(servedBootstrap);
    kick.NoteEncoderStamp(encodeInputUs, servedBootstrap);
""")
rep('apps/native_poc/src/host_stage_encode_send_h264.cpp',
"""  encoder.outputSamplesTotal += encodeStats.processOutputSamples;
  // The stamp the MFT just accepted (real or synthetic) is the floor for the next real frame's
  // stamp -- see KickState::ClampRealStamp (0.2.97).
  kick.NoteEncoderStamp(encodeInputUs, servedBootstrap);
  if (!servedBootstrap) {""", """  encoder.outputSamplesTotal += encodeStats.processOutputSamples;
  if (!servedBootstrap) {""")

# 4. h264_au.cpp: wire flag from AU provenance; clamp telemetry in user-feedback
rep('apps/native_poc/src/host_stage_encode_send_h264_au.cpp',
"""// A kick / static-refresh frame is the cached picture re-encoded, not new content; tell the
// viewer so it keeps the frame out of its latency and congestion arithmetic. (F-10 / H-13.)
if (servedBootstrap) hdr.flags |= kEncodedFrameFlagSynthetic;""",
"""// A kick / static-refresh frame is the cached picture re-encoded, not new content; tell the
// viewer so it keeps the frame out of its latency and congestion arithmetic. (F-10 / H-13.)
// The flag is the AU's own provenance (accepted-input FIFO), not this call's servedBootstrap: an
// async MFT emits the held REAL frame during the kick call and the kick's AU during the next real
// call, so the call flag marked the wrong frames. (0.2.97)
if (au.synthetic) hdr.flags |= kEncodedFrameFlagSynthetic;""")
A='apps/native_poc/src/host_stage_encode_send_h264_au.cpp'
s = read(A)
old = '              << " frameCaptureUs=" << captureStampUs\n'
n = s.count(old); assert n == 2, n
s = s.replace(old, old + '              << " stampClampUs=" << tc.captureStampClampUs\n')
write(A, s); print("feedback stampClampUs added")
rep('apps/native_poc/src/host_main_loop.hpp', """  uint64_t captureStampUs = 0;
  bool sendFailed = false;""", """  uint64_t captureStampUs = 0;
  uint64_t captureStampClampUs = 0;  // how far captureStampUs was moved past a synthetic stamp (0.2.97)
  bool sendFailed = false;""")
rep('apps/native_poc/src/host_stage_gate_static.cpp', """      const uint64_t behindUs = clampedStampUs - captureStampUs;
      ++kick.stampClampCount;""", """      const uint64_t behindUs = clampedStampUs - captureStampUs;
      tc.captureStampClampUs = behindUs;
      ++kick.stampClampCount;""")

# 5. viewer receiver: decoder provenance
V='apps/native_poc/src/viewer_video_receiver_frame.cpp'
rep(V, """    bool pendingTimestampOverflow = false;
    if (!dec.decoder.decode_access_unit(*payloadPtr, keyFrame, inputSampleTimeHns, &outFrames,""",
"""    bool pendingTimestampOverflow = false;
    dec.decoder.set_next_input_synthetic(synthetic);  // rides the decoder's pending-input FIFO (0.2.97)
    if (!dec.decoder.decode_access_unit(*payloadPtr, keyFrame, inputSampleTimeHns, &outFrames,""")
rep(V, """    auto& decoded = outFrames.back();
""", """    auto& decoded = outFrames.back();
    // Provenance of THIS decoded frame (a decoder may return an older input's picture during this
    // call); the packet's flag `synthetic` stays the gate's input. (0.2.97)
    const bool decodedSynthetic = decoded.synthetic;
""")
rep(V, "      if (ctx.frameBuf.pacedPlayout && !synthetic) {", "      if (ctx.frameBuf.pacedPlayout && !decodedSynthetic) {")
rep(V, "      ctx.frameBuf.frame.synthetic = synthetic;", "      ctx.frameBuf.frame.synthetic = decodedSynthetic;")
rep(V, """    if (synthetic) {
      // Counted, shown, but not measured:""", """    if (decodedSynthetic) {
      // Counted, shown, but not measured:""")

# 6. host_kick_test: two late reals after one synthetic (Codex 21)
rep('apps/native_poc/src/host_kick_test.cpp', """  k.NoteEncoderStamp(late, false);
  expect(k.lastSyntheticStampUs == 10 * kSec + 150000, "stamp: a real note leaves the synthetic record alone");""",
"""  k.NoteEncoderStamp(late, false);
  expect(k.lastSyntheticStampUs == 10 * kSec + 150000, "stamp: a real note leaves the synthetic record alone");
  // a second late real frame (captured 10.08s, also published late) lands strictly after the first
  const uint64_t late2 = KickState::ClampRealStamp(10 * kSec + 80000, k.lastEncoderStampUs);
  expect(late2 == 10 * kSec + 150002, "stamp: consecutive late real frames keep strictly increasing stamps");
  k.NoteEncoderStamp(late2, false);""")
rep('apps/native_poc/src/host_kick_test.cpp', """  k.NoteEncoderStamp(5 * kSec, false);
  expect(k.lastEncoderStampUs == late, "stamp: the record only moves forward");""",
"""  k.NoteEncoderStamp(5 * kSec, false);
  expect(k.lastEncoderStampUs == late2, "stamp: the record only moves forward");""")

# 7. viewer gate test: synthetic IDR then an older real P decodes without recovery (Codex 10/21)
rep('apps/native_poc/src/viewer_frame_gate_test.cpp', """  CHECK(r.gate.latestCaptureSeenUs == t + 100 * kMs);
}

int main() {""", """  CHECK(r.gate.latestCaptureSeenUs == t + 100 * kMs);

  // Variant: the kick carried the IDR (closed barrier) -- the reference chain restarts at it, and a
  // late real P behind it (older stamp, dense) must still decode in order: no drop, no recovery.
  Rig r2;
  const uint64_t t0 = 9000 * kMs;
  CHECK(r2.feed(t0, t0, true, 0, false, nullptr, &in) == FrameGateVerdict::Decode);
  r2.decoded(in);
  const uint64_t tkey = t0 + 400 * kMs;  // synthetic IDR stamped with the kick time
  CHECK(r2.feed(tkey, tkey, true, t0, false, nullptr, &in, true) == FrameGateVerdict::Decode);
  r2.decoded(in);
  CHECK(!r2.gate.waitForKeyFrame);
  CHECK(r2.gate.latestCaptureSeenUs == t0);
  CHECK(r2.feed(tkey + 40 * kMs, t0 + 150 * kMs, false, t0, false, nullptr, &in) == FrameGateVerdict::Decode);
  r2.decoded(in);
  CHECK(r2.gate.staleReferenceRecoveryCount == 0);
  CHECK(r2.sink.requests(6) == 0);
}

int main() {""")
print("ALL EDITS OK")
