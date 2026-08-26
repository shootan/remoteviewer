#!/usr/bin/env bash
# Host split refactor Phase 2-5: encoder-management lambdas of main() become EncoderState member
# functions (host_encoder_manager.cpp, bodies verbatim + alias), the AU timeline anchor moves into
# EncoderState and the four UDP pacing env values into SenderState. Run once from the repo root.
set -euo pipefail
S=apps/native_poc/src
F=$S/native_video_host_main.cpp
HE=$S/host_encoder_manager.hpp
HS=$S/host_encoded_sender.hpp
C=apps/native_poc/CMakeLists.txt
cp "$F" /tmp/host_main_before_2_5.cpp

lambda_range() {
  local s ind e
  s=$(grep -n -m1 -E "^ *(const )?auto $1 = \[" "$F" | cut -d: -f1)
  ind=$(sed -n "${s}p" "$F" | sed -E 's/^( *).*/\1/' | wc -c); ind=$((ind - 1))
  e=$(awk -v s="$s" -v ind="$ind" 'NR>s && $0 ~ ("^" sprintf("%" ind "s","") "};?$") {print NR; exit}' "$F")
  echo "$s $e $ind"
}
header_end() { awk -v s="$1" 'NR>=s && /\{$/ {print NR; exit}' "$F"; }

set -- $(lambda_range resetHostTimelineAnchors);      RA_S=$1; RA_E=$2
set -- $(lambda_range apply_encoder_target);          AT_S=$1; AT_E=$2; AT_H=$(header_end "$AT_S")
set -- $(lambda_range apply_confirmed_capture_geometry); CG_S=$1; CG_E=$2; CG_H=$(header_end "$CG_S")
set -- $(lambda_range apply_capture_ui_quality_mode); QM_S=$1; QM_E=$2; QM_H=$(header_end "$QM_S")
echo "reset $RA_S..$RA_E target $AT_S..$AT_E(h$AT_H) geometry $CG_S..$CG_E(h$CG_H) quality $QM_S..$QM_E(h$QM_H)"

# rewrites applied to the moved bodies and to main()
CALLSED='s/\bresetHostTimelineAnchors()/encoder.ResetTimelineAnchors(capture)/g; s/\bapply_encoder_target(/encoder.ApplyTarget(capture, res, frameGating, inputRouter, sender, /g; s/\bapply_confirmed_capture_geometry(/encoder.ApplyConfirmedCaptureGeometry(capture, res, frameGating, inputRouter, sender, /g; s/\bapply_capture_ui_quality_mode(/encoder.ApplyCaptureUiQualityMode(capture, res, frameGating, inputRouter, sender, rate, useH264, /g'
printf 'auTimelineOriginUs\tencoder.auTimelineOriginUs\nnoPacingH264\tsender.noPacingH264\nudpPacePeakPercent\tsender.udpPacePeakPercent\nudpPacePeakFloorBps\tsender.udpPacePeakFloorBps\nudpKeyframePacePeakBps\tsender.udpKeyframePacePeakBps\n' > /tmp/map25.txt

body() { sed -n "$1,$2p" "$F" | sed 's/^  //' | sed "$CALLSED" | perl automation/rename_outside_strings.pl /tmp/map25.txt; }
body $((AT_H + 1)) $((AT_E - 1)) > /tmp/b_at.txt
body $((CG_H + 1)) $((CG_E - 1)) > /tmp/b_cg.txt
body $((QM_H + 1)) $((QM_E - 1)) > /tmp/b_qm.txt

{
  echo '// See host_encoder_manager.hpp for the module summary. The member bodies below are the former'
  echo '// apply_encoder_target / apply_confirmed_capture_geometry / apply_capture_ui_quality_mode lambdas'
  echo '// of native_video_host_main.cpp, moved verbatim (host split refactor Phase 2-5): "encoder"'
  echo '// aliases *this so the text reads unchanged; cross-calls were rewritten to member calls.'
  echo
  echo '#include <winsock2.h>'
  echo '#include <windows.h>'
  echo '#ifdef min'
  echo '#undef min'
  echo '#endif'
  echo '#ifdef max'
  echo '#undef max'
  echo '#endif'
  echo
  echo '#include <algorithm>'
  echo '#include <atomic>'
  echo '#include <cstdint>'
  echo '#include <iostream>'
  echo
  echo '#include "encode_resolution_ladder.hpp"'
  echo '#include "host_abr.hpp"'
  echo '#include "host_bgra_scale.hpp"'
  echo '#include "host_capture_session.hpp"'
  echo '#include "host_encoded_sender.hpp"'
  echo '#include "host_encoder_manager.hpp"'
  echo '#include "host_frame_gate.hpp"'
  echo '#include "host_input_router.hpp"'
  echo '#include "host_net_io.hpp"'
  echo
  echo 'namespace remote60::native_poc {'
  echo
  echo 'bool EncoderState::ApplyTarget(CaptureState& capture, CaptureResources& res, FrameGatingState& frameGating,'
  echo '                               InputRouterState& inputRouter, SenderState& sender, uint32_t targetW,'
  echo '                               uint32_t targetH, uint32_t targetFps, uint32_t targetBitrate,'
  echo '                               uint32_t targetKeyint) {'
  echo '  EncoderState& encoder = *this;'
  cat /tmp/b_at.txt
  echo '}'
  echo
  echo 'void EncoderState::ApplyConfirmedCaptureGeometry(CaptureState& capture, CaptureResources& res,'
  echo '                                                 FrameGatingState& frameGating, InputRouterState& inputRouter,'
  echo '                                                 SenderState& sender, uint32_t newW, uint32_t newH,'
  echo '                                                 const char* reason, bool allowWindowOverride) {'
  echo '  EncoderState& encoder = *this;'
  cat /tmp/b_cg.txt
  echo '}'
  echo
  echo 'bool EncoderState::ApplyCaptureUiQualityMode(CaptureState& capture, CaptureResources& res,'
  echo '                                             FrameGatingState& frameGating, InputRouterState& inputRouter,'
  echo '                                             SenderState& sender, RateControlState& rate, bool useH264,'
  echo '                                             bool overviewMode, uint64_t nowUs) {'
  echo '  EncoderState& encoder = *this;'
  cat /tmp/b_qm.txt
  echo '}'
  echo
  echo '}  // namespace remote60::native_poc'
} > $S/host_encoder_manager.cpp

# --- header: field + declarations ---
cat > /tmp/decl_en.txt <<'EOF'
  // AU timeline anchor (paired with CaptureState::timelineOriginUs; -1 = not yet anchored).
  int64_t auTimelineOriginUs = -1;

  // --- behaviour (Phase 2-5: former main() lambdas; bodies in host_encoder_manager.cpp except the
  //     one-liner) ---
  // Forget both timeline anchors so the next frame re-anchors capture vs AU timing.
  void ResetTimelineAnchors(CaptureState& capture) {
    capture.timelineOriginUs = -1;
    auTimelineOriginUs = -1;
  }
  // The single choke point every encoder parameter change goes through (runtime tune, capture-UI
  // overview/focus, ABR/M9 refit): fits the box to the source aspect, rebuilds or re-tunes the MFT,
  // publishes the input domain and the UDP pacing budget.
  bool ApplyTarget(CaptureState& capture, CaptureResources& res, FrameGatingState& frameGating,
                   InputRouterState& inputRouter, SenderState& sender, uint32_t targetW, uint32_t targetH,
                   uint32_t targetFps, uint32_t targetBitrate, uint32_t targetKeyint);
  // A confirmed source-size change: re-fit the encode target to the new geometry immediately.
  void ApplyConfirmedCaptureGeometry(CaptureState& capture, CaptureResources& res, FrameGatingState& frameGating,
                                     InputRouterState& inputRouter, SenderState& sender, uint32_t newW,
                                     uint32_t newH, const char* reason, bool allowWindowOverride = false);
  // Capture-UI overview (lower bitrate/fps/size) vs focus mode, derived from the live ceilings.
  bool ApplyCaptureUiQualityMode(CaptureState& capture, CaptureResources& res, FrameGatingState& frameGating,
                                 InputRouterState& inputRouter, SenderState& sender, RateControlState& rate,
                                 bool useH264, bool overviewMode, uint64_t nowUs);
EOF
s=$(grep -n -m1 '^struct EncoderState {' "$HE" | cut -d: -f1)
e=$(awk -v s="$s" 'NR>s && /^};$/ {print NR; exit}' "$HE")
sed -i "$((e - 1))r /tmp/decl_en.txt" "$HE"
sed -i 's|^namespace remote60::native_poc {$|namespace remote60::native_poc {\n\nstruct InputRouterState;\nstruct RateControlState;\nstruct SenderState;|' "$HE"

cat > /tmp/decl_sn.txt <<'EOF'
  // UDP pacing env config (REMOTE60_NATIVE_H264_NO_PACING / UDP_PACE_PEAK_* / UDP_KEYFRAME_PACE_PEAK_BPS),
  // fixed after startup; EncoderState::ApplyTarget derives the live pacing budget from these.
  bool noPacingH264 = false;
  uint32_t udpPacePeakPercent = 0;
  uint32_t udpPacePeakFloorBps = 0;
  uint32_t udpKeyframePacePeakBps = 0;
EOF
s=$(grep -n -m1 '^  // Config (REMOTE60_NATIVE_SENDER_MAX_CADENCE_HOLD_US), fixed after startup\.$' "$HS" | cut -d: -f1)
sed -i "$((s - 1))r /tmp/decl_sn.txt" "$HS"

# --- main(): delete the four lambdas (highest first), rename the five locals, fix their decls ---
for r in "$QM_S $QM_E" "$CG_S $CG_E" "$AT_S $AT_E" "$RA_S $RA_E"; do set -- $r; echo "$1 $2"; done | sort -rn | while read a b; do sed -i "${a},${b}d" "$F"; done
sed -i "$CALLSED" "$F"
m=$(grep -n -m1 '^int main(int argc' "$F" | cut -d: -f1)
head -n $((m - 1)) "$F" > /tmp/hm_head.cpp; tail -n +$m "$F" | perl automation/rename_outside_strings.pl /tmp/map25.txt > /tmp/hm_tail.cpp; cat /tmp/hm_head.cpp /tmp/hm_tail.cpp > "$F"
sed -i '/^  int64_t encoder\.auTimelineOriginUs = -1;$/d' "$F"
sed -i 's/^  const bool sender\.noPacingH264 = /  sender.noPacingH264 = /; s/^  const uint32_t sender\.udpPacePeakPercent =$/  sender.udpPacePeakPercent =/; s/^  const uint32_t sender\.udpPacePeakFloorBps = env_u32_clamped($/  sender.udpPacePeakFloorBps = env_u32_clamped(/; s/^  const uint32_t sender\.udpKeyframePacePeakBps = env_u32_clamped($/  sender.udpKeyframePacePeakBps = env_u32_clamped(/' "$F"
sed -i 's|^  src/host_capture_session.cpp$|  src/host_capture_session.cpp\n  src/host_encoder_manager.cpp|' "$C"
# SenderState must be declared before the pacing env reads that now live in it: move the instance
# (and its one-line comment) up to just before the first of them. Default-constructed, no deps.
sed -i '/^  \/\/ Encoded-frame sender queue\/thread, UDP peer, media barrier, wire counters (SenderState, Phase 1-2)\.$/d; /^  SenderState sender;$/d' "$F"
p=$(grep -n -m1 '^  sender\.noPacingH264 = ' "$F" | cut -d: -f1)
printf '  // Encoded-frame sender queue/thread, UDP peer, media barrier, wire counters (SenderState, Phase 1-2).\n  SenderState sender;\n' > /tmp/sinst.txt
sed -i "$((p - 1))r /tmp/sinst.txt" "$F"

echo "== leftovers:"; grep -n -E '\b(resetHostTimelineAnchors|apply_encoder_target|apply_confirmed_capture_geometry|apply_capture_ui_quality_mode)\b' "$F" | cut -c1-100 || true
echo "== decl-form leftovers:"; grep -n -E '^  (const )?(bool|uint32_t|int64_t) (sender|encoder)\.' "$F" | cut -c1-100 || true
echo "== sender decl site:"; grep -n -E '^  sender\.(noPacingH264|udpPacePeakPercent|udpPacePeakFloorBps|udpKeyframePacePeakBps) ' "$F" | cut -c1-100
echo "== SenderState sender declared at: $(grep -n '^  SenderState sender;' "$F" | cut -d: -f1) (must be before the lines above)"
echo "host_main now $(wc -l < "$F") lines"
