#!/usr/bin/env bash
# Host split refactor Phase 3: the 3,060-line main loop of native_video_host_main.cpp becomes twelve
# stage functions (host_main_loop.cpp) driven by a ~20-line loop; the four remaining big lambdas and
# the file-scope tuning constants move with it. Bodies are verbatim apart from: (a) loop-level
# continue/break/return -> Flow results, (b) per-iteration locals -> TickContext fields, (c) calls of
# the moved lambdas gaining the HostContext argument. Run once from the repo root.
set -euo pipefail
S=apps/native_poc/src
F=$S/native_video_host_main.cpp
C=apps/native_poc/CMakeLists.txt
W=/tmp/host_main_work_3.cpp
cp "$F" /tmp/host_main_before_3.cpp
cp "$F" "$W"

LS=$(grep -n -m1 '^  while (!stop.load()) {$' "$F" | cut -d: -f1)
LE=$(awk -v s="$LS" 'NR>s && /^  }$/ {print NR; exit}' "$F")
inloop() { awk -v s="$LS" -v e="$LE" -v p="$1" 'NR>=s && NR<=e && index($0,p)==1 {print NR; exit}' "$F"; }
inloop_sub() { awk -v s="$LS" -v e="$LE" -v p="$1" 'NR>=s && NR<=e && index($0,p) {print NR; exit}' "$F"; }
B1=$((LS + 1))
B2=$(inloop '    if (backend.reqPending.exchange(false, std::memory_order_acq_rel)) {')
B3=$(inloop '    const bool streamActive = ')
B4=$(inloop '    if (useH264 && encoder.tunePending.exchange(false, std::memory_order_acq_rel)) {')
B5=$(inloop_sub 'Switching screens is the same operation as switching to desktop mode')
B6=$(inloop_sub 'WGC ContentSize settle + main-thread pool recreate')
B7=$(inloop '    if (capture.sessionReady.load(std::memory_order_acquire) &&')
B8=$(inloop '    if (paceByTick) {')
B9=$(inloop '    std::shared_ptr<std::vector<uint8_t>> payload;')
B10=$(inloop '    if (!servedBootstrap && frameGating.enabled && useH264 && payload && !payload->empty()) {')
B11=$(inloop '    if (useRaw) {')
B12=$(inloop '    const uint64_t t = qpc_now_us();')
echo "loop $LS..$LE stages start: $B1 $B2 $B3 $B4 $B5 $B6 $B7 $B8 $B9 $B10 $B11 $B12"
for b in $B2 $B3 $B4 $B5 $B6 $B7 $B8 $B9 $B10 $B11 $B12; do [ -n "$b" ] || { echo "anchor missing"; exit 1; }; done
DEPTHS=$(perl automation/loop_depth_at.pl "$LS" "$LE" $B1 $B2 $B3 $B4 $B5 $B6 $B7 $B8 $B9 $B10 $B11 $B12 $LE < "$F")
echo "$DEPTHS"; echo "$DEPTHS" | grep -q -E 'd=[02-9]' && { echo "boundary not at depth 1"; exit 1; }

# --- (a) loop-level exits -> Flow (on the work copy, absolute lines) ---
for n in $(perl automation/loop_exits.pl "$LS" "$LE" < "$F" | awk -F: '{print $1}'); do
  sed -i -E "${n}s/\bcontinue;/return Flow::Continue;/; ${n}s/\bbreak;/return Flow::Break;/; ${n}s/\breturn ([0-9]+);/{ hx.exitCode = \1; return Flow::Return; }/" "$W"
done
# --- (b) per-iteration locals -> TickContext (blank the declarations, keep timed/derived inits as assignments) ---
TCN="nowUs tickWaitUs payload seq w h stride streamGeneration captureUs callbackUs queuePushUs callbackIntervalUs captureIntervalUs captureClockSkewUs captureAgeAtCallbackUs captureD3DWaitUs captureCopyMapUs captureMemcpyUs captureUnmapWaitUs captureUnmapUs version nv12Slot nv12Generation nv12W nv12H queueWaitReason queueSelectStartUs servedBootstrap kickForcedKey queueWaitUs queueGapFrames queueDepthAtPop captureStampUs sendFailed queuePopUs queueSelectWaitUs frameAgeAtSelectUs captureToCallbackUs captureToQueueUs"
sed -i -E "${LS},${LE}s/^    const uint64_t nowUs = qpc_now_us\(\);$/    nowUs = qpc_now_us();/; ${LS},${LE}s/^    uint64_t tickWaitUs = 0;$/ /" "$W"
sed -i -E "${LS},${LE}s/^    std::shared_ptr<std::vector<uint8_t>> payload;$/ /; ${LS},${LE}s/^    (uint32_t|uint64_t|int32_t|bool) (seq|w|h|stride|streamGeneration|captureUs|callbackUs|queuePushUs|callbackIntervalUs|captureIntervalUs|captureClockSkewUs|captureAgeAtCallbackUs|captureD3DWaitUs|captureCopyMapUs|captureMemcpyUs|captureUnmapWaitUs|captureUnmapUs|version|nv12Slot|nv12Generation|nv12W|nv12H|queueWaitReason|servedBootstrap|kickForcedKey|sendFailed) = (0|-1|false);( *\/\/.*)?$/ /" "$W"
sed -i -E "${LS},${LE}s/^    const uint64_t queueSelectStartUs = qpc_now_us\(\);$/    queueSelectStartUs = qpc_now_us();/; ${LS},${LE}s/^  const uint64_t (queuePopUs|queueSelectWaitUs|frameAgeAtSelectUs|captureToCallbackUs|captureToQueueUs) =/  \1 =/; ${LS},${LE}s/^    const uint64_t (queueWaitUs|queueGapFrames|queueDepthAtPop|captureStampUs) =/    \1 =/; ${LS},${LE}s/^    static uint64_t lastUserFeedbackUs = 0;$/ /" "$W"
# --- (c) moved-lambda calls gain the context argument (loop + lambda bodies) ---
CALLSED='s/\brestart_capture_session()/restart_capture_session(hx)/g; s/\bapply_selected_window_capture(/apply_selected_window_capture(hx, /g; s/\breconnect_tcp_data_session(/reconnect_tcp_data_session(hx, /g; s/\bpump_cursor_forward(/pump_cursor_forward(hx, /g'
sed -i "${LS},${LE}{$CALLSED}" "$W"

lambda_range() {  # on the ORIGINAL file (lambdas sit before the loop; numbers identical in W)
  local s ind e
  s=$(grep -n -m1 -E "^ *(const )?auto $1 = \[" "$F" | cut -d: -f1)
  ind=$(sed -n "${s}p" "$F" | sed -E 's/^( *).*/\1/' | wc -c); ind=$((ind - 1))
  e=$(awk -v s="$s" -v ind="$ind" 'NR>s && $0 ~ ("^" sprintf("%" ind "s","") "};?$") {print NR; exit}' "$F")
  echo "$s $e"
}
header_end() { awk -v s="$1" 'NR>=s && /\{$/ {print NR; exit}' "$F"; }
set -- $(lambda_range restart_capture_session);       RC_S=$1; RC_E=$2; RC_H=$(header_end "$RC_S")
set -- $(lambda_range pump_cursor_forward);           PC_S=$1; PC_E=$2; PC_H=$(header_end "$PC_S")
set -- $(lambda_range reconnect_tcp_data_session);    RT_S=$1; RT_E=$2; RT_H=$(header_end "$RT_S")
set -- $(lambda_range apply_selected_window_capture); AW_S=$1; AW_E=$2; AW_H=$(header_end "$AW_S")
set -- $(lambda_range update_u64_max);                UM_S=$1; UM_E=$2
echo "lambdas: restart $RC_S..$RC_E pump $PC_S..$PC_E reconnect $RT_S..$RT_E select $AW_S..$AW_E u64max $UM_S..$UM_E"
for r in "$RC_S $RC_E" "$PC_S $PC_E" "$RT_S $RT_E" "$AW_S $AW_E"; do set -- $r; sed -i "$1,$2{$CALLSED}" "$W"; done

HN="args useH264 useRaw transport stop guardStaleEncoded guardStalePreEncode paceByTick startUs nextTickUs captureWindowRebindIntervalUs nextCaptureWindowCheckUs streamActiveApplied streamActiveSinceUs poppedNv12Slot poppedNv12Generation powerKeepalive item token windowSelectionTxn frameGating rate kick clientMetrics backend watchdog inputRouter sender clientSession encoder stats capture res lastUserFeedbackUs"
emit_fn() {  # emit_fn SIGNATURE_LINE BODYFILE TAIL_LINE  (aliases only for names the body uses; tc aliases only when the signature has a TickContext)
  echo "$1"
  for n in $HN; do grep -q -E "(^|[^.A-Za-z0-9_>])$n\b" "$2" && echo "  auto& $n = hx.$n;"; done
  if [[ "$1" == *"TickContext& tc"* ]]; then for n in $TCN; do grep -q -E "(^|[^.A-Za-z0-9_>])$n\b" "$2" && echo "  auto& $n = tc.$n;"; done; fi
  cat "$2"
  [ -n "$3" ] && echo "$3"
  echo '}'
  echo
}
stage_body() { sed -n "$1,$2p" "$W" | sed -E 's/^  //'; }   # loop body indent 4 -> 2
lambda_body() { sed -n "$1,$2p" "$W" | sed -E 's/^  //'; }

{
  echo '// See host_main_loop.hpp for the module summary. Everything below is the former main loop and'
  echo '// helper lambdas of native_video_host_main.cpp, moved verbatim (host split refactor Phase 3):'
  echo '// each stage body is unchanged apart from loop-level continue/break/return becoming Flow'
  echo '// results, per-iteration locals living in TickContext, and the moved helpers taking the context.'
  echo '// The alias lines at the top of each function bind the old local names to the context.'
  echo
  echo '#include <winsock2.h>'
  echo '#include <ws2tcpip.h>'
  echo '#include <windows.h>'
  echo '#ifdef min'
  echo '#undef min'
  echo '#endif'
  echo '#ifdef max'
  echo '#undef max'
  echo '#endif'
  echo
  echo '#include <d3d11.h>'
  echo '#include <dxgi1_2.h>'
  echo '#include <mfapi.h>'
  echo '#include <wrl/client.h>'
  echo
  echo '#include <winrt/Windows.Foundation.h>'
  echo '#include <winrt/Windows.Graphics.Capture.h>'
  echo '#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>'
  echo '#include <winrt/base.h>'
  echo
  echo '#include <algorithm>'
  echo '#include <atomic>'
  echo '#include <chrono>'
  echo '#include <cstdint>'
  echo '#include <cstdio>'
  echo '#include <cstring>'
  echo '#include <iostream>'
  echo '#include <limits>'
  echo '#include <memory>'
  echo '#include <mutex>'
  echo '#include <string>'
  echo '#include <thread>'
  echo '#include <vector>'
  echo
  echo '#include "capture_backend_dxgi.hpp"'
  echo '#include "d3d_capture_readback.hpp"'
  echo '#include "encode_resolution_ladder.hpp"'
  echo '#include "gdi_capture_process.hpp"'
  echo '#include "host_abr.hpp"'
  echo '#include "host_args.hpp"'
  echo '#include "host_backend_policy.hpp"'
  echo '#include "host_bgra_scale.hpp"'
  echo '#include "host_bottleneck.hpp"'
  echo '#include "host_capture_device.hpp"'
  echo '#include "host_capture_session.hpp"'
  echo '#include "host_client_metrics.hpp"'
  echo '#include "host_control_session.hpp"'
  echo '#include "host_encoded_sender.hpp"'
  echo '#include "host_encoder_manager.hpp"'
  echo '#include "host_frame_gate.hpp"'
  echo '#include "host_frame_state.hpp"'
  echo '#include "host_gpu_scaler.hpp"'
  echo '#include "host_input_inject.hpp"'
  echo '#include "host_input_router.hpp"'
  echo '#include "host_kick.hpp"'
  echo '#include "host_log.hpp"'
  echo '#include "host_main_loop.hpp"'
  echo '#include "host_net_io.hpp"'
  echo '#include "host_session.hpp"'
  echo '#include "host_stats.hpp"'
  echo '#include "host_string_util.hpp"'
  echo '#include "host_watchdog.hpp"'
  echo '#include "host_window_enum.hpp"'
  echo '#include "mf_h264_codec.hpp"'
  echo '#include "native_video_transport.hpp"'
  echo '#include "poc_protocol.hpp"'
  echo '#include "time_utils.hpp"'
  echo
  echo 'using namespace winrt::Windows::Graphics::Capture;'
  echo 'using namespace winrt::Windows::Graphics::DirectX::Direct3D11;'
  echo 'using remote60::host::DesktopCaptureBackend;'
  echo 'using remote60::host::DxgiDesktopCaptureConfig;'
  echo 'using remote60::host::DxgiDesktopCaptureSession;'
  echo
  echo 'namespace remote60::native_poc {'
  echo
  echo '// ---------------------------------------------------------------------------------------------'
  echo '// Helpers the loop calls (former main() lambdas).'
  echo '// ---------------------------------------------------------------------------------------------'
  echo
  lambda_body $((RC_H + 1)) $((RC_E - 1)) > /tmp/b_rc.txt
  emit_fn 'bool restart_capture_session(HostContext& hx) {' /tmp/b_rc.txt ''
  lambda_body $((PC_H + 1)) $((PC_E - 1)) > /tmp/b_pc.txt
  emit_fn 'void pump_cursor_forward(HostContext& hx, uint64_t nowUs) {' /tmp/b_pc.txt ''
  lambda_body $((RT_H + 1)) $((RT_E - 1)) > /tmp/b_rt.txt
  emit_fn 'bool reconnect_tcp_data_session(HostContext& hx, const char* reason) {' /tmp/b_rt.txt ''
  lambda_body $((AW_H + 1)) $((AW_E - 1)) > /tmp/b_aw.txt
  emit_fn 'bool apply_selected_window_capture(HostContext& hx, uint64_t requestedWindowId, uint64_t nowUs,
                                   uint32_t* outFlags, uint64_t* outWindowId,
                                   uint64_t* outStreamGeneration,
                                   std::string* outReason, std::string* outTitle) {' /tmp/b_aw.txt ''
  echo '// ---------------------------------------------------------------------------------------------'
  echo '// The twelve stages of one main-loop tick, in call order.'
  echo '// ---------------------------------------------------------------------------------------------'
  echo
  i=0
  for spec in "stage_time_limit $B1 $((B2 - 1))" "stage_backend $B2 $((B3 - 1))" "stage_stream_active $B3 $((B4 - 1))" "stage_runtime_tune $B4 $((B5 - 1))" "stage_selection $B5 $((B6 - 1))" "stage_geometry $B6 $((B7 - 1))" "stage_watchdogs $B7 $((B8 - 1))" "stage_pace $B8 $((B9 - 1))" "stage_pop_frame $B9 $((B10 - 1))" "stage_gate_static $B10 $((B11 - 1))" "stage_encode_send $B11 $((B12 - 1))" "stage_stats $B12 $((LE - 1))"; do
    set -- $spec; i=$((i + 1))
    stage_body "$2" "$3" > /tmp/b_stage_$i.txt
    emit_fn "Flow $1(HostContext& hx, TickContext& tc) {" /tmp/b_stage_$i.txt '  return Flow::Next;'
  done
  echo '}  // namespace remote60::native_poc'
} > $S/host_main_loop.cpp

# --- header: constants (file-scope + main-local), Flow, HostContext, TickContext, declarations ---
K1=$(grep -n -m1 '^constexpr bool kInputPolicyForceBlock' "$F" | cut -d: -f1)
K2=$(grep -n -m1 '^constexpr uint32_t kKeyReqTokenCapacityDefault' "$F" | cut -d: -f1)
sed -n "${K1},${K2}p" "$F" > /tmp/kconst.txt
MAINK="kDesktopBackendRetryMinUs kDesktopBackendRetryMaxUs kDesktopDefaultStableUs kDesktopDefaultProbeIntervalUs kEncodeRefitSettleUs kWgcContentSettleUs kCaptureIdleDetachDelayUs kCaptureReattachRetryMinUs kCaptureReattachRetryMaxUs"
: > /tmp/mainconst.txt
for k in $MAINK; do grep -m1 -E "^  constexpr uint64_t $k = " "$F" | sed 's/^  //' >> /tmp/mainconst.txt; done
{
  echo '#pragma once'
  echo
  echo '// Host main loop: the per-tick stages of the capture -> encode -> send pipeline, the context they'
  echo '// share, and the tuning constants.'
  echo '//'
  echo '// Role:    HostContext is the set of references main() assembles once (args, flags, the twelve'
  echo '//          state structs, the capture resources, the few remaining main() locals); TickContext'
  echo '//          holds the per-iteration values that used to be locals of the loop body. Each'
  echo '//          stage_* function is one former section of the loop body, in call order; it returns'
  echo '//          Flow::Continue / Break / Return where the old code said continue / break / return.'
  echo '// Thread:  main encode loop only; the stages touch other threads only through the state structs.'
  echo '// Callers: native_video_host_main.cpp (RUN_STAGE sequence inside while (!stop)).'
  echo '//'
  echo '// Host split refactor Phase 3: bodies moved verbatim from native_video_host_main.cpp.'
  echo
  echo '#include <winsock2.h>'
  echo '#include <windows.h>'
  echo
  echo '#include <winrt/Windows.Graphics.Capture.h>'
  echo
  echo '#include <atomic>'
  echo '#include <cstdint>'
  echo '#include <memory>'
  echo '#include <string>'
  echo '#include <vector>'
  echo
  echo '#include "host_abr.hpp"'
  echo '#include "host_args.hpp"'
  echo '#include "host_backend_policy.hpp"'
  echo '#include "host_capture_session.hpp"'
  echo '#include "host_client_metrics.hpp"'
  echo '#include "host_control_session.hpp"'
  echo '#include "host_encoded_sender.hpp"'
  echo '#include "host_encoder_manager.hpp"'
  echo '#include "host_frame_gate.hpp"'
  echo '#include "host_input_router.hpp"'
  echo '#include "host_kick.hpp"'
  echo '#include "host_log.hpp"'
  echo '#include "host_session.hpp"'
  echo '#include "host_stats.hpp"'
  echo '#include "host_watchdog.hpp"'
  echo '#include "native_video_transport.hpp"'
  echo
  echo 'namespace remote60::native_poc {'
  echo
  echo '// --- tuning constants (formerly file-scope / main()-local constexprs of native_video_host_main.cpp) ---'
  cat /tmp/kconst.txt
  cat /tmp/mainconst.txt
  echo
  echo '// What a stage asks the loop to do next.'
  echo 'enum class Flow { Next, Continue, Break, Return };'
  echo
  echo '// Everything the loop stages and the helper functions reach from main(). Members are named'
  echo '// exactly like the main() locals they alias so the moved bodies read unchanged.'
  echo 'struct HostContext {'
  echo '  const Args& args;'
  echo '  const bool useH264;'
  echo '  const bool useRaw;'
  echo '  const VideoTransport transport;'
  echo '  std::atomic<bool>& stop;'
  echo '  const bool guardStaleEncoded;'
  echo '  const bool guardStalePreEncode;'
  echo '  const bool paceByTick;'
  echo '  uint64_t& startUs;'
  echo '  uint64_t& nextTickUs;'
  echo '  uint64_t& captureWindowRebindIntervalUs;'
  echo '  uint64_t& nextCaptureWindowCheckUs;'
  echo '  bool& streamActiveApplied;'
  echo '  uint64_t& streamActiveSinceUs;'
  echo '  int32_t& poppedNv12Slot;'
  echo '  uint64_t& poppedNv12Generation;'
  echo '  HostPowerKeepalive& powerKeepalive;'
  echo '  winrt::Windows::Graphics::Capture::GraphicsCaptureItem& item;'
  echo '  winrt::event_token& token;'
  echo '  WindowSelectionTxn& windowSelectionTxn;'
  echo '  FrameGatingState& frameGating;'
  echo '  RateControlState& rate;'
  echo '  KickState& kick;'
  echo '  ClientMetricsSnapshot& clientMetrics;'
  echo '  DesktopBackendState& backend;'
  echo '  WatchdogState& watchdog;'
  echo '  InputRouterState& inputRouter;'
  echo '  SenderState& sender;'
  echo '  SessionState& clientSession;'
  echo '  EncoderState& encoder;'
  echo '  HostStats& stats;'
  echo '  CaptureState& capture;'
  echo '  CaptureResources& res;'
  echo '  // Owned here (formerly a function-static inside the loop): user-feedback log rate limit.'
  echo '  uint64_t lastUserFeedbackUs = 0;'
  echo '  // Set by a stage that returns Flow::Return; main() returns it.'
  echo '  int exitCode = 0;'
  echo '};'
  echo
  echo '// Per-iteration values of one tick (formerly locals declared along the loop body). Constructed'
  echo '// fresh every iteration, so the defaults below are exactly the old initialisers.'
  echo 'struct TickContext {'
  echo '  uint64_t nowUs = 0;'
  echo '  uint64_t tickWaitUs = 0;'
  echo '  std::shared_ptr<std::vector<uint8_t>> payload;'
  echo '  uint32_t seq = 0;'
  echo '  uint32_t w = 0;'
  echo '  uint32_t h = 0;'
  echo '  uint32_t stride = 0;'
  echo '  uint64_t streamGeneration = 0;'
  echo '  uint64_t captureUs = 0;'
  echo '  uint64_t callbackUs = 0;'
  echo '  uint64_t queuePushUs = 0;'
  echo '  uint64_t callbackIntervalUs = 0;'
  echo '  uint64_t captureIntervalUs = 0;'
  echo '  uint64_t captureClockSkewUs = 0;'
  echo '  uint64_t captureAgeAtCallbackUs = 0;'
  echo '  uint64_t captureD3DWaitUs = 0;'
  echo '  uint64_t captureCopyMapUs = 0;'
  echo '  uint64_t captureMemcpyUs = 0;'
  echo '  uint64_t captureUnmapWaitUs = 0;'
  echo '  uint64_t captureUnmapUs = 0;'
  echo '  uint64_t version = 0;'
  echo '  int32_t nv12Slot = -1;'
  echo '  uint64_t nv12Generation = 0;'
  echo '  uint32_t nv12W = 0;'
  echo '  uint32_t nv12H = 0;'
  echo '  uint32_t queueWaitReason = 0;  // 0: normal, 1: timeout, 2: no-work'
  echo '  uint64_t queueSelectStartUs = 0;'
  echo '  bool servedBootstrap = false;'
  echo '  bool kickForcedKey = false;    // true only when this kick must open a closed media barrier (IDR)'
  echo '  uint64_t queuePopUs = 0;'
  echo '  uint64_t queueSelectWaitUs = 0;'
  echo '  uint64_t frameAgeAtSelectUs = 0;'
  echo '  uint64_t captureToCallbackUs = 0;'
  echo '  uint64_t captureToQueueUs = 0;'
  echo '  uint64_t queueWaitUs = 0;'
  echo '  uint64_t queueGapFrames = 0;'
  echo '  uint64_t queueDepthAtPop = 0;'
  echo '  uint64_t captureStampUs = 0;'
  echo '  bool sendFailed = false;'
  echo '};'
  echo
  echo '// Helpers (former main() lambdas).'
  echo 'bool restart_capture_session(HostContext& hx);'
  echo 'void pump_cursor_forward(HostContext& hx, uint64_t nowUs);'
  echo 'bool reconnect_tcp_data_session(HostContext& hx, const char* reason);'
  echo 'bool apply_selected_window_capture(HostContext& hx, uint64_t requestedWindowId, uint64_t nowUs,'
  echo '                                   uint32_t* outFlags, uint64_t* outWindowId,'
  echo '                                   uint64_t* outStreamGeneration,'
  echo '                                   std::string* outReason, std::string* outTitle);'
  echo
  echo '// The twelve stages of one tick, in call order.'
  echo 'Flow stage_time_limit(HostContext& hx, TickContext& tc);    // seconds limit, barrier recovery'
  echo 'Flow stage_backend(HostContext& hx, TickContext& tc);       // backend request, demotion/promotion'
  echo 'Flow stage_stream_active(HostContext& hx, TickContext& tc); // stream active/idle transitions'
  echo 'Flow stage_runtime_tune(HostContext& hx, TickContext& tc);  // runtime encoder config requests'
  echo 'Flow stage_selection(HostContext& hx, TickContext& tc);     // monitor / capture-mode / window selection'
  echo 'Flow stage_geometry(HostContext& hx, TickContext& tc);      // WGC content-size settle, size change'
  echo 'Flow stage_watchdogs(HostContext& hx, TickContext& tc);     // callback-stall + frozen-ring watchdogs'
  echo 'Flow stage_pace(HostContext& hx, TickContext& tc);          // raw-mode tick pacing'
  echo 'Flow stage_pop_frame(HostContext& hx, TickContext& tc);     // trailing kick, static refresh, frame pop'
  echo 'Flow stage_gate_static(HostContext& hx, TickContext& tc);   // static-frame gating, stale guards'
  echo 'Flow stage_encode_send(HostContext& hx, TickContext& tc);   // raw send / H.264 encode + enqueue'
  echo 'Flow stage_stats(HostContext& hx, TickContext& tc);         // 1s stats tick, drain watchdog, ABR/M9'
  echo
  echo '}  // namespace remote60::native_poc'
} > $S/host_main_loop.hpp

# --- update_u64_max -> inline function in host_stats.hpp ---
cat > /tmp/u64.txt <<'EOF'

// Monotonic max for an atomic counter (formerly the update_u64_max lambda in main()).
inline void update_u64_max(std::atomic<uint64_t>& target, const uint64_t value) {
  auto old = target.load(std::memory_order_relaxed);
  while (value > old && !target.compare_exchange_weak(old, value, std::memory_order_release, std::memory_order_relaxed)) {
  }
}
EOF
hs=$(grep -n -m1 '^struct HostStats {' $S/host_stats.hpp | cut -d: -f1)
sed -i "$((hs - 1))r /tmp/u64.txt" $S/host_stats.hpp

# --- main(): rebuild around the loop (edits from the bottom up) ---
cat > /tmp/loop.txt <<'EOF'
  // One tick = the twelve stages of host_main_loop.cpp, in order. A stage that used to
  // `continue`/`break`/`return` from the loop body reports it through Flow.
  while (!stop.load()) {
    TickContext tc;
    RUN_STAGE(stage_time_limit);
    RUN_STAGE(stage_backend);
    RUN_STAGE(stage_stream_active);
    RUN_STAGE(stage_runtime_tune);
    RUN_STAGE(stage_selection);
    RUN_STAGE(stage_geometry);
    RUN_STAGE(stage_watchdogs);
    RUN_STAGE(stage_pace);
    RUN_STAGE(stage_pop_frame);
    RUN_STAGE(stage_gate_static);
    RUN_STAGE(stage_encode_send);
    RUN_STAGE(stage_stats);
  }
EOF
sed -i "${LS},${LE}d" "$F"
sed -i "$((LS - 1))r /tmp/loop.txt" "$F"
sed -i "${AW_S},${AW_E}d" "$F"
sed -i "${RT_S},${RT_E}d" "$F"
sed -i "${PC_S},${PC_E}d" "$F"
# startup-block rewrites (these lines lie between the restart lambda and the pump lambda)
sed -i 's/^  const uint64_t startUs = qpc_now_us();$/  startUs = qpc_now_us();/; s/^  uint64_t nextTickUs = startUs;$/  nextTickUs = startUs;/; s/^  const bool paceByTick = useRaw;$/ /; s/^  const uint64_t captureWindowRebindIntervalUs =$/  captureWindowRebindIntervalUs =/; s/^  uint64_t nextCaptureWindowCheckUs = startUs + captureWindowRebindIntervalUs;$/  nextCaptureWindowCheckUs = startUs + captureWindowRebindIntervalUs;/; s/^  bool streamActiveApplied = true;$/ /' "$F"
sed -i '/^  \/\/ Trailing-edge kick \/ static refresh \/ selection-first-keyframe state (KickState, Phase 1-8)\.$/d; /^  KickState kick;$/d' "$F"
sed -i 's/\brestart_capture_session();/restart_capture_session(host);/' "$F"
sed -i "${RC_S},${RC_E}d" "$F"
cat > /tmp/hostctx.txt <<'EOF'

  // Main-loop timing / pacing values that the stages share (declared here so HostContext can bind
  // them; startUs and the derived values are stamped below, right where they used to be declared).
  uint64_t startUs = 0;
  uint64_t nextTickUs = 0;
  const bool paceByTick = useRaw;
  uint64_t captureWindowRebindIntervalUs = 0;
  uint64_t nextCaptureWindowCheckUs = 0;
  bool streamActiveApplied = true;
  // Everything the loop stages and helpers reach (HostContext, Phase 3). Assembled once; the members
  // are references to the objects above, named like them.
  HostContext host{args, useH264, useRaw, transport, stop, guardStaleEncoded, guardStalePreEncode,
                   paceByTick, startUs, nextTickUs, captureWindowRebindIntervalUs,
                   nextCaptureWindowCheckUs, streamActiveApplied, streamActiveSinceUs, poppedNv12Slot,
                   poppedNv12Generation, powerKeepalive, item, token, windowSelectionTxn, frameGating,
                   rate, kick, clientMetrics, backend, watchdog, inputRouter, sender, clientSession,
                   encoder, stats, capture, res};
EOF
a=$(grep -n -m1 '^  watchdog\.mainLoopProgressUs = qpc_now_us();$' "$F" | cut -d: -f1)
sed -i "${a}r /tmp/hostctx.txt" "$F"
sed -i "${UM_S},${UM_E}d" "$F"
for k in $MAINK; do sed -i -E "/^  constexpr uint64_t $k = /d" "$F"; done
sed -i "${K1},${K2}d" "$F"
printf '  // Trailing-edge kick / static refresh / selection-first-keyframe state (KickState, Phase 1-8).\n  KickState kick;\n' > /tmp/kick.txt
w=$(grep -n -m1 '^  WatchdogState watchdog;$' "$F" | cut -d: -f1)
sed -i "${w}r /tmp/kick.txt" "$F"
# includes, usings, RUN_STAGE macro
sed -i 's|^#include "host_control_session.hpp"$|#include "host_control_session.hpp"\n#include "host_main_loop.hpp"|' "$F"
KLIST=$(grep -o -E '^constexpr [a-z0-9_]+ (k[A-Za-z0-9]+)' /tmp/kconst.txt /tmp/mainconst.txt | awk '{print $3}' | tr '\n' ' ')
{ echo '// Tuning constants, loop context and stages extracted to host_main_loop.hpp/.cpp (Phase 3).'; for k in $KLIST; do echo "using remote60::native_poc::$k;"; done; echo 'using remote60::native_poc::Flow;'; echo 'using remote60::native_poc::HostContext;'; echo 'using remote60::native_poc::TickContext;'; echo 'using remote60::native_poc::restart_capture_session;'; echo 'using remote60::native_poc::update_u64_max;'; } > /tmp/usings.txt
u=$(grep -n -m1 '^using remote60::native_poc::ControlSessionServer;$' "$F" | cut -d: -f1)
sed -i "${u}r /tmp/usings.txt" "$F"
cat > /tmp/macro.txt <<'EOF'
// Run one main-loop stage and honour the flow it reports. Deliberately not a do/while(0) macro:
// the continue/break must act on the enclosing while (!stop) loop.
#define RUN_STAGE(fn)                                                       \
  {                                                                         \
    const remote60::native_poc::Flow f_ = fn(host, tc);                     \
    if (f_ == remote60::native_poc::Flow::Continue) continue;               \
    if (f_ == remote60::native_poc::Flow::Break) break;                     \
    if (f_ == remote60::native_poc::Flow::Return) return host.exitCode;     \
  }

EOF
m=$(grep -n -m1 '^int main(int argc' "$F" | cut -d: -f1)
sed -i "$((m - 1))r /tmp/macro.txt" "$F"
sed -i 's|^  src/host_encoder_manager.cpp$|  src/host_encoder_manager.cpp\n  src/host_main_loop.cpp|' "$C"

echo "== main: loop now:"; grep -n -A3 '^  while (!stop.load()) {$' "$F" | head -5
echo "== leftover lambda names in main:"; grep -n -E '\b(pump_cursor_forward|reconnect_tcp_data_session|apply_selected_window_capture|update_u64_max)\b' "$F" | cut -c1-100 || true
echo "== restart calls in main:"; grep -n 'restart_capture_session(' "$F" | cut -c1-100
echo "== HostContext at: $(grep -n '^  HostContext host{' "$F" | cut -d: -f1); KickState at: $(grep -n '^  KickState kick;' "$F" | cut -d: -f1); startUs stamp at: $(grep -n '^  startUs = qpc_now_us();' "$F" | cut -d: -f1)"
echo "host_main now $(wc -l < "$F") lines; host_main_loop.cpp $(wc -l < $S/host_main_loop.cpp) lines; hpp $(wc -l < $S/host_main_loop.hpp) lines"
