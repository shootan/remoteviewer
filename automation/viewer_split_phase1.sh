#!/usr/bin/env bash
# Viewer split refactor Phase 1: the file-scope globals become members of feature state structs.
#   1-0  constants -> viewer_constants.hpp (moved verbatim out of viewer_globals.hpp)
#   1-1  SessionState gSession        1-6  PickerState gPicker
#   1-2  FrameBuffer gFrameBuf        1-7  SelectionGateState gSel
#   1-3  PresentStats gPresent        1-8  InputState gInput
#   1-4  ClientMetricsState gMetrics  1-9  RemoteCursorState gCursor
#   1-5  ControlChannelState gControl 1-10 UiResources gUi
# Every step is a mechanical rename (automation/rename_outside_strings.pl: whole words, never inside
# string literals, never after . -> ::) of `gOld` to `gInstance.member`; the state header (written by
# hand, initialisers copied from viewer_globals.cpp) is checked against the dropped definitions; the
# extern/definition lines leave viewer_globals.hpp/.cpp. Gate per step: build + viewer e2e. Run from
# the repo root on a clean tree; pass a step number to start from it (0..10).
set -euo pipefail
S=apps/native_poc/src
M=$S/native_video_client_main.cpp
GH=$S/viewer_globals.hpp
GC=$S/viewer_globals.cpp
FROM=${1:-0}
git diff --quiet HEAD || { echo "tree not clean"; exit 1; }
T=/tmp/v1; rm -rf $T; mkdir -p $T
STATE_HEADERS="viewer_constants.hpp viewer_session_state.hpp viewer_frame_buffer.hpp viewer_present_stats.hpp viewer_client_metrics.hpp viewer_control_state.hpp viewer_picker_state.hpp viewer_selection_gate.hpp viewer_input_state.hpp viewer_remote_cursor.hpp viewer_ui_resources.hpp"

rename_files() {  # every viewer translation unit except the hand-written state headers
  local f
  for f in $M $S/viewer_*.cpp $S/viewer_*.hpp; do
    local skip=0 h
    for h in $STATE_HEADERS; do [ "$f" = "$S/$h" ] && skip=1; done
    [ $skip -eq 1 ] || echo "$f"
  done
}
mapfile_from() {  # stdin "old new" lines -> tab separated map file $1
  sed -E 's/^([A-Za-z0-9_]+) +/\1\t/' > "$1"
}
drop_extern() {  # $1 old global: remove its extern line from viewer_globals.hpp
  perl -0pi -e 's~^extern [^\r\n]*\b'"$1"'\b[^\r\n]*\r\n~~m or die "extern '"$1"'"' "$GH"
}
drop_def() {  # $1 old global: remove its (single-line) definition from viewer_globals.cpp; tolerate already-gone
  if grep -qE "^[^ /].*\b$1\b" "$GC"; then
    perl -0pi -e 's~^[^\r\n /][^\r\n]*\b'"$1"'\b[^\r\n]*\r\n~~m or die "def '"$1"'"' "$GC"
  fi
  grep -qw "$1" "$GC" && { echo "definition of $1 still present"; exit 1; } || true
}
drop_type() {  # $1 struct name: remove `struct NAME {` .. `};` from viewer_globals.hpp and verify the new header has it verbatim
  local name=$1 hdr=$2
  git show HEAD:$GH | tr -d '\r' | awk "/^struct $name \\{\$/,/^};\$/" > $T/type_$name.txt
  [ -s $T/type_$name.txt ] || { echo "type $name not in HEAD globals"; exit 1; }
  perl -0pi -e 's~^struct '"$name"' \{\r\n.*?^\};\r\n~~ms or die "drop type '"$name"'"' "$GH"
  tr -d '\r' < "$hdr" > $T/hdr.txt
  perl -e 'local $/; open(A,"<",$ARGV[0]); $a=<A>; open(B,"<",$ARGV[1]); $b=<B>; exit(index($b,$a) >= 0 ? 0 : 1)' $T/type_$name.txt $T/hdr.txt \
    || { echo "type $name not verbatim in $hdr"; exit 1; }
  echo "ok: type $name moved verbatim to $hdr"
}
wire_instance() {  # $1 header, $2 struct, $3 instance
  grep -q "#include \"$1\"" "$GH" || perl -0pi -e 's~(#include "viewer_common\.hpp"\r\n)~$1#include "'"$1"'"\r\n~ or die "globals include"' "$GH"
  perl -0pi -e 's~(\r\n\}  // namespace remote60::native_poc::viewer\r\n)$~\r\nextern '"$2 $3"';$1~ or die "globals extern"' "$GH"
  perl -0pi -e 's~(\r\n\}  // namespace remote60::native_poc::viewer\r\n)$~'"$2 $3"';$1~ or die "globals def"' "$GC"
}
rename_all() {  # $1 mapfile
  local f
  for f in $(rename_files); do
    perl automation/rename_outside_strings.pl "$1" < "$f" > $T/renamed.tmp && cp $T/renamed.tmp "$f"
  done
}
verify_gone() {  # $1 mapfile: no old identifier may remain outside the state headers
  local old bad=0
  while IFS=$'\t' read -r old new; do
    # a word not preceded by . -> :: or an identifier char (the renamed member names may equal the old bare names)
    if grep -nP '(?<![.\w>:])'"$old"'\b' $(rename_files) ; then echo "still referenced: $old"; bad=1; fi
  done < "$1"
  [ $bad -eq 0 ]
}
commit_step() {  # $1 step, $2 title, $3 body, rest files
  local step=$1 title=$2 body=$3; shift 3
  git add "$@"
  git commit -q -F - <<EOF
refactor(viewer): Phase $step — $title

$body
Gates: build exit 0, viewer e2e ALL PASS.

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
EOF
  git log -1 --format='%h %s'
}
apply_struct() {  # $1 step, $2 header, $3 struct, $4 instance, $5 mapfile, $6 types (space list), $7 title, $8 body
  local step=$1 hdr=$2 st=$3 inst=$4 map=$5 types=$6 title=$7 body=$8
  local old new t
  while IFS=$'\t' read -r old new; do
    if [[ "$old" == g* ]]; then drop_extern "$old"; drop_def "$old"; fi
  done < "$map"
  for t in $types; do drop_type "$t" "$S/$hdr"; done
  wire_instance "$hdr" "$st" "$inst"
  rename_all "$map"
  verify_gone "$map"
  bash automation/viewer_split_gate.sh --e2e
  commit_step "$step" "$title" "$body" "$M" $S/viewer_*.cpp $S/viewer_*.hpp
}

# ================= 1-0 constants =================
if [ "$FROM" -le 0 ]; then
  # the constants and their attached comments leave viewer_globals.hpp
  for k in kInputPolicyForceBlock kCatchupLagDropUs kCatchupResumeKeyLagUs kDecodeQueueLagDropUs kDecodeQueueLagResumeUs kStaleCaptureDropUs kUserFeedbackLagWarnUs kUserFeedbackGapWarnUs kUserFeedbackMinIntervalUs kKeyframeRequestMinIntervalUsDefault kKeyframeRequestTokenRefillUsDefault kKeyframeRequestTokenCapacityDefault kCatchupReenterMinIntervalUsDefault kCongestionRecoverMinUsDefault kCongestionRecoveryTimeoutUsDefault kUdpControlReadTimeoutMs kPickerPressNone kPickerSelectMinShownUs kMsgRevealStreamView kThumbRefreshUs kCursorOverlayTimerId kRemoteCursorStaleUs kCursorOverlaySize kRuntimeBitrateMin kRuntimeBitrateMax kRuntimeBitrateStep kRuntimeKeyintMin kRuntimeKeyintMax; do
    line=$(git show HEAD:$GH | tr -d '\r' | grep -E "^constexpr [^ ]+ $k\b")
    [ -n "$line" ] || { echo "constant $k missing in HEAD"; exit 1; }
    grep -qxF "$line" <(tr -d '\r' < $S/viewer_constants.hpp) || { echo "constant $k not verbatim in viewer_constants.hpp: $line"; exit 1; }
    perl -0pi -e 's~^constexpr [^\r\n]*\b'"$k"'\b[^\r\n]*\r\n~~m or die "constexpr '"$k"'"' "$GH"
  done
  perl -0pi -e 's~// Catch-up defaults tuned for software codec path: avoid runaway multi-second lag,\r\n// but still clamp perceived latency quickly for interactive remote use\.\r\n~~ or die "c1"; s~// Long enough that a slow host answering a window list is not mistaken for a dead link\.\r\n~~ or die "c2"; s~// Picker mis-click guard\. In the field a frozen-looking stream had the user frantically clicking;\r\n// one UP landed on the first window card and silently switched the capture to another window\.\r\n// A selection now requires DOWN and UP on the SAME target and a picker that has been visible for\r\n// at least 300ms \(kPickerSelectMinShownUs\), so a click that started before the picker appeared --\r\n// or that merely ends on a card -- cannot select\. ~0 = nothing pressed; 0 = desktop is a valid id\.\r\n~~ or die "c3"; s~// Posted to the video window when the first selected frame is ready, so the toolbar \(a window of\r\n// its own, whose show/hide must run on the UI thread\) is revealed on the thread that owns it\.\r\n~~ or die "c4"' "$GH"
  perl -0pi -e 's~(#include "viewer_common\.hpp"\r\n)~$1#include "viewer_constants.hpp"\r\n~ or die "include"' "$GH"
  grep -c "^constexpr" "$GH" | grep -qx 0 || { echo "constants remain in globals.hpp"; exit 1; }
  bash automation/viewer_split_gate.sh --e2e
  commit_step 1-0 "tuning constants to viewer_constants.hpp (verbatim)" "The 28 kXxx constants (with their comments) leave viewer_globals.hpp so the Phase 1 state headers can use them as default member initialisers; each line checked identical to HEAD." "$GH" $S/viewer_constants.hpp
fi

# ================= 1-1 SessionState gSession =================
if [ "$FROM" -le 1 ]; then
  mapfile_from $T/map1 <<'EOF'
gRunning gSession.running
gSock gSession.sock
gHwnd gSession.hwnd
gWindowW gSession.windowW
gWindowH gSession.windowH
gInputEnabled gSession.inputEnabled
gRelayPath gSession.relayPath
gRequestedMonitorId gSession.requestedMonitorId
gInputEventsSent gSession.inputEventsSent
gOverlayConfig gSession.overlayConfig
gLogMu gSession.logMu
nextToolbarPushUs gSession.nextToolbarPushUs
EOF
  [ "$(grep -lw nextToolbarPushUs $M $S/viewer_*.cpp $S/viewer_*.hpp | wc -l)" = 1 ] || { echo "nextToolbarPushUs not unique to main.cpp"; exit 1; }
  perl -0pi -e 's~      static uint64_t nextToolbarPushUs = 0;\r\n~~ or die "static nextToolbarPushUs"' "$M"
  apply_struct 1-1 viewer_session_state.hpp SessionState gSession $T/map1 "OverlayConfigSnapshot" \
    "SessionState gSession (running/sock/hwnd/windowW/H/inputEnabled/relayPath/requestedMonitorId/inputEventsSent/overlayConfig/logMu)" \
    "Mechanical rename of 11 globals to gSession.<member> (rename_outside_strings.pl: whole words, not in string literals); OverlayConfigSnapshot moves into the state header verbatim; the message pump's static nextToolbarPushUs becomes a member (same zero initial value)."
fi

# ================= 1-2 FrameBuffer gFrameBuf =================
if [ "$FROM" -le 2 ]; then
  mapfile_from $T/map2 <<'EOF'
gFrame gFrameBuf.frame
gLastPresentedVersion gFrameBuf.lastPresentedVersion
gLastPresentedCaptureUs gFrameBuf.lastPresentedCaptureUs
gPaintQueued gFrameBuf.paintQueued
gPaintCoalescedCount gFrameBuf.paintCoalescedCount
gOverwriteBeforePresentCount gFrameBuf.overwriteBeforePresentCount
gCatchupSuppressUntilUs gFrameBuf.catchupSuppressUntilUs
EOF
  apply_struct 1-2 viewer_frame_buffer.hpp FrameBuffer gFrameBuf $T/map2 "SharedFrame" \
    "FrameBuffer gFrameBuf (frame/lastPresentedVersion/lastPresentedCaptureUs/paintQueued/paintCoalescedCount/overwriteBeforePresentCount/catchupSuppressUntilUs)" \
    "Mechanical rename of 7 globals to gFrameBuf.<member>; SharedFrame moves into the state header verbatim."
fi

# ================= 1-3 PresentStats gPresent =================
if [ "$FROM" -le 3 ]; then
  mapfile_from $T/map3 <<'EOF'
gD3dPresentSuccessCount gPresent.d3dPresentSuccessCount
gD3dPresentFailCount gPresent.d3dPresentFailCount
gGdiFallbackPresentedCount gPresent.gdiFallbackPresentedCount
gFallbackInitFailCount gPresent.fallbackInitFailCount
gFallbackRenderFailCount gPresent.fallbackRenderFailCount
gFallbackNv12ConvertFailCount gPresent.fallbackNv12ConvertFailCount
gTraceEvery gPresent.traceEvery
gTraceMax gPresent.traceMax
gPresentFrameIntervalUs gPresent.presentFrameIntervalUs
gTracePresentPrinted gPresent.tracePresentPrinted
gTraceRecvPrinted gPresent.traceRecvPrinted
hasPresentedAtLeastOneFrame gPresent.hasPresentedAtLeastOneFrame
lastPresentUs gPresent.lastPresentUs
lastUserFeedbackUs gPresent.lastUserFeedbackUs
lastUserFeedbackOverwrite gPresent.lastUserFeedbackOverwrite
EOF
  for id in hasPresentedAtLeastOneFrame lastPresentUs lastUserFeedbackUs lastUserFeedbackOverwrite; do
    [ "$(grep -lw $id $M $S/viewer_*.cpp $S/viewer_*.hpp | grep -v viewer_present_stats.hpp)" = "$S/viewer_window_proc.cpp" ] || { echo "$id not unique to window_proc"; exit 1; }
  done
  W=$S/viewer_window_proc.cpp
  perl -0pi -e 's~      static bool hasPresentedAtLeastOneFrame = false;\r\n~~ or die "s1"; s~        static uint64_t lastPresentUs = 0;\r\n        static uint64_t lastUserFeedbackUs = 0;\r\n        static uint64_t lastUserFeedbackOverwrite = 0;\r\n~~ or die "s2"' "$W"
  apply_struct 1-3 viewer_present_stats.hpp PresentStats gPresent $T/map3 "" \
    "PresentStats gPresent (present counters, trace switches, WM_PAINT bookkeeping)" \
    "Mechanical rename of 11 globals to gPresent.<member>; WM_PAINT's four function statics (hasPresentedAtLeastOneFrame, lastPresentUs, lastUserFeedbackUs, lastUserFeedbackOverwrite) become members with the same zero/false initial values and the same UI-thread-only use."
fi

# ================= 1-4 ClientMetricsState gMetrics =================
if [ "$FROM" -le 4 ]; then
  mapfile_from $T/map4 <<'EOF'
gClientMetrics gMetrics.client
gOverlayMetricsMu gMetrics.overlayMu
gOverlayMetrics gMetrics.overlay
EOF
  apply_struct 1-4 viewer_client_metrics.hpp ClientMetricsState gMetrics $T/map4 "ClientRuntimeMetrics OverlayMetricSample OverlayMetricAverages" \
    "ClientMetricsState gMetrics (client/overlayMu/overlay)" \
    "Mechanical rename of 3 globals to gMetrics.<member>; ClientRuntimeMetrics and the overlay sample types move into the state header verbatim."
fi

# ================= 1-5 ControlChannelState gControl =================
if [ "$FROM" -le 5 ]; then
  mapfile_from $T/map5 <<'EOF'
gControlScheduler gControl.scheduler
gKeyframeRequests gControl.keyframeRequests
gRuntimeTuneState gControl.runtimeTune
gStreamStateControl gControl.streamState
gCaptureModeRequests gControl.captureModeRequests
gInputQueueState gControl.inputQueue
gUdpControl gControl.udpControl
gControlOverUdp gControl.overUdp
gControlConnected gControl.connected
gCaptureOverviewMode gControl.captureOverviewMode
gHostCaptureTargetPid gControl.hostCaptureTargetPid
gHostCaptureTargetFlags gControl.hostCaptureTargetFlags
gHostCaptureRebindCount gControl.hostCaptureRebindCount
gHostCaptureTargetHwnd gControl.hostCaptureTargetHwnd
gHostCaptureMetaUpdatedUs gControl.hostCaptureMetaUpdatedUs
gHostCaptureMetaMu gControl.hostCaptureMetaMu
gHostCaptureTargetProcess gControl.hostCaptureTargetProcess
gHostCaptureTargetTitle gControl.hostCaptureTargetTitle
reportedSecure gControl.reportedSecure
EOF
  [ "$(grep -lw reportedSecure $M $S/viewer_*.cpp $S/viewer_*.hpp | grep -v viewer_control_state.hpp)" = "$M" ] || { echo "reportedSecure not unique to main.cpp"; exit 1; }
  perl -0pi -e 's~                    static bool reportedSecure = false;\r\n~~ or die "static reportedSecure"' "$M"
  # the two multi-line definitions
  perl -0pi -e 's~KeyframeRequestState gKeyframeRequests\{\r\n    kKeyframeRequestMinIntervalUsDefault,\r\n    kKeyframeRequestTokenRefillUsDefault,\r\n    kKeyframeRequestTokenCapacityDefault\};\r\n~~ or die "def gKeyframeRequests"; s~RuntimeTuneState gRuntimeTuneState\{\r\n    300000,\r\n    30000000,\r\n    250000,\r\n    1,\r\n    240\};\r\n~~ or die "def gRuntimeTuneState"' "$GC"
  apply_struct 1-5 viewer_control_state.hpp ControlChannelState gControl $T/map5 "" \
    "ControlChannelState gControl (scheduler, request states, udp tunnel, connection, host capture meta)" \
    "Mechanical rename of 18 globals to gControl.<member> (the keyframe limiter and runtime tune keep their constructor arguments as default member initialisers); the control loop's static reportedSecure becomes a member with the same false initial value."
fi

# ================= 1-6 PickerState gPicker =================
if [ "$FROM" -le 6 ]; then
  mapfile_from $T/map6 <<'EOF'
gWindowPanelState gPicker.windowPanel
gWindowPickerVisible gPicker.visible
gWindowPickerToggleDown gPicker.toggleDown
gGridScrollRow gPicker.gridScrollRow
gPickerShownAtUs gPicker.shownAtUs
gPickerPressTargetId gPicker.pressTargetId
gMacroButtonDown gPicker.macroButtonDown
gThumbMu gPicker.thumbMu
gThumbs gPicker.thumbs
gThumbFetchQueue gPicker.thumbFetchQueue
gHostSupportsThumbnails gPicker.hostSupportsThumbnails
EOF
  apply_struct 1-6 viewer_picker_state.hpp PickerState gPicker $T/map6 "WindowThumb" \
    "PickerState gPicker (panel model, visibility, gesture latches, scroll, thumbnails)" \
    "Mechanical rename of 11 globals to gPicker.<member>; WindowThumb moves into the state header verbatim."
fi

# ================= 1-7 SelectionGateState gSel =================
if [ "$FROM" -le 7 ]; then
  mapfile_from $T/map7 <<'EOF'
gSelectionPending gSel.pending
gSelectionAwaitingAck gSel.awaitingAck
gSelectionExpectedGeneration gSel.expectedGeneration
gSelectionEpoch gSel.epoch
gActiveStreamGeneration gSel.activeStreamGeneration
gSelectionReadyGeneration gSel.readyGeneration
gSelectionReadyEpoch gSel.readyEpoch
gSelectionRevealPosted gSel.revealPosted
EOF
  apply_struct 1-7 viewer_selection_gate.hpp SelectionGateState gSel $T/map7 "" \
    "SelectionGateState gSel (pending/awaitingAck/expectedGeneration/epoch/activeStreamGeneration/ready*/revealPosted)" \
    "Mechanical rename of the 8 selection-gate atomics to gSel.<member>; the protocol comment moves to the state header."
fi

# ================= 1-8 InputState gInput =================
if [ "$FROM" -le 8 ]; then
  mapfile_from $T/map8 <<'EOF'
gMouseButtons gInput.mouseButtons
gLastInputVideoX gInput.lastVideoX
gLastInputVideoY gInput.lastVideoY
gForwardedKeyDown gInput.forwardedKeyDown
gSuppressMouseUntilUs gInput.suppressMouseUntilUs
gActiveTouchPointerId gInput.activeTouchPointerId
gActiveTouchDown gInput.activeTouchDown
gInputMacro gInput.macro
EOF
  apply_struct 1-8 viewer_input_state.hpp InputState gInput $T/map8 "" \
    "InputState gInput (mouse buttons, last video point, key memory, touch, macro engine)" \
    "Mechanical rename of 8 globals to gInput.<member>."
fi

# ================= 1-9 RemoteCursorState gCursor =================
if [ "$FROM" -le 9 ]; then
  mapfile_from $T/map9 <<'EOF'
gRemoteCursorX gCursor.x
gRemoteCursorY gCursor.y
gRemoteCursorCapW gCursor.capW
gRemoteCursorCapH gCursor.capH
gRemoteCursorGeneration gCursor.generation
gRemoteCursorVisible gCursor.visible
gRemoteCursorUpdateUs gCursor.updateUs
gCursorOverlayHwnd gCursor.overlayHwnd
EOF
  apply_struct 1-9 viewer_remote_cursor.hpp RemoteCursorState gCursor $T/map9 "" \
    "RemoteCursorState gCursor (sample + overlay window)" \
    "Mechanical rename of 8 globals to gCursor.<member>."
fi

# ================= 1-10 UiResources gUi =================
if [ "$FROM" -le 10 ]; then
  mapfile_from $T/map10 <<'EOF'
gUiFont gUi.font
gUiTitleFont gUi.titleFont
gUiDpi gUi.dpi
gNv12Renderer gUi.nv12Renderer
EOF
  perl -0pi -e 's~  static std::unordered_map<COLORREF, HBRUSH> cache;\r\n  return cache;\r\n~  return gUi.brushCache;\r\n~ or die "brush cache"' $S/viewer_gdi_util.cpp
  apply_struct 1-10 viewer_ui_resources.hpp UiResources gUi $T/map10 "" \
    "UiResources gUi (fonts, dpi, brush cache, NV12 presenter)" \
    "Mechanical rename of 4 globals to gUi.<member>; brush_cache() returns the member map instead of its function static (same lifetime: process-wide, destroyed by destroy_cached_gdi_objects)."
fi
echo "Phase 1 structs done; remaining externs in viewer_globals.hpp: $(grep -c '^extern' $GH)"
