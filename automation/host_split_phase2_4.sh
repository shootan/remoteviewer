#!/usr/bin/env bash
# Host split refactor Phase 2-4: (a) group the RAII / WinRT / D3D capture locals of main() into
# CaptureResources (same declaration order => same destruction order), (b) move ten capture lambdas
# into CaptureState member functions (bodies verbatim + alias prelude) in host_capture_session.cpp.
# Run once from the repo root on a tree where the lambdas still exist.
set -euo pipefail
S=apps/native_poc/src
F=$S/native_video_host_main.cpp
H=$S/host_capture_session.hpp
C=apps/native_poc/CMakeLists.txt
cp "$F" /tmp/host_main_before_2_4.cpp

lambda_range() {  # NAME -> "start end"
  local s ind e
  s=$(grep -n -m1 -E "^ *(const )?auto $1 = \[" "$F" | cut -d: -f1)
  ind=$(sed -n "${s}p" "$F" | sed -E 's/^( *).*/\1/' | wc -c); ind=$((ind - 1))
  e=$(awk -v s="$s" -v ind="$ind" 'NR>s && $0 ~ ("^" sprintf("%" ind "s","") "};?$") {print NR; exit}' "$F")
  echo "$s $e $ind"
}
# header_end START -> line number of the lambda header's last line (the one ending with "{")
header_end() { awk -v s="$1" 'NR>=s && /\{$/ {print NR; exit}' "$F"; }
# own_params START HEND -> "params|ret" parsed from the lambda header
own_params() {
  sed -n "$1,$2p" "$F" | tr '\n' ' ' | sed -E 's/ +/ /g' |
    perl -ne 'if (/\[&\]\((.*)\)\s*(?:->\s*(.+?))?\s*\{\s*$/) { my ($p,$r)=($1,$2//"void"); $p =~ s/^\s+|\s+$//g; print "$p|$r\n"; } else { print "PARSE_FAIL|void\n"; }'
}

NAMES="create_staging publish_captured_texture attach_frame_arrived detach_capture_session restart_capture_session_impl restore_previous_target flush_capture_pipeline_state log_first_sent_generation kick_try_fill effective_queue_wait_timeout_us"
declare -A MEMBER PRE HS HE HIND HHEAD
MEMBER[create_staging]=CreateStaging;                 PRE[create_staging]='CaptureResources& res, EncoderState& encoder, bool useH264'
MEMBER[publish_captured_texture]=PublishCapturedTexture; PRE[publish_captured_texture]='CaptureResources& res'
MEMBER[attach_frame_arrived]=AttachFrameArrived;      PRE[attach_frame_arrived]='CaptureResources& res, SessionState& clientSession, std::atomic<bool>& stop, winrt::event_token& token'
MEMBER[detach_capture_session]=DetachCaptureSession;  PRE[detach_capture_session]='CaptureResources& res, winrt::event_token& token'
MEMBER[restart_capture_session_impl]=RestartCaptureSessionImpl; PRE[restart_capture_session_impl]='CaptureResources& res, DesktopBackendState& backend, SessionState& clientSession, EncoderState& encoder, std::atomic<bool>& stop, bool useH264, winrt::Windows::Graphics::Capture::GraphicsCaptureItem& item, winrt::event_token& token'
MEMBER[restore_previous_target]=RestorePreviousTarget; PRE[restore_previous_target]='winrt::Windows::Graphics::Capture::GraphicsCaptureItem& item'
MEMBER[flush_capture_pipeline_state]=FlushCapturePipelineState; PRE[flush_capture_pipeline_state]='CaptureResources& res, FrameGatingState& frameGating, HostStats& stats'
MEMBER[log_first_sent_generation]=LogFirstSentGeneration; PRE[log_first_sent_generation]='CaptureResources& res, HostStats& stats'
MEMBER[kick_try_fill]=KickTryFill;                   PRE[kick_try_fill]='SessionState& clientSession, KickState& kick'
MEMBER[effective_queue_wait_timeout_us]=EffectiveQueueWaitTimeoutUs; PRE[effective_queue_wait_timeout_us]='EncoderState& encoder'

for n in $NAMES; do
  set -- $(lambda_range "$n"); HS[$n]=$1; HE[$n]=$2; HIND[$n]=$3; HHEAD[$n]=$(header_end "$1")
  echo "$n: $1..$2 (indent $3, header ends $(header_end "$1"))"
done

# call-site rewrite used both in main() and inside the moved bodies
CALLSED='s/\bcreate_staging(/capture.CreateStaging(res, encoder, useH264, /g; s/\bpublish_captured_texture(/capture.PublishCapturedTexture(res, /g; s/\battach_frame_arrived()/capture.AttachFrameArrived(res, clientSession, stop, token)/g; s/\bdetach_capture_session()/capture.DetachCaptureSession(res, token)/g; s/\brestart_capture_session_impl()/capture.RestartCaptureSessionImpl(res, backend, clientSession, encoder, stop, useH264, item, token)/g; s/\brestore_previous_target()/capture.RestorePreviousTarget(item)/g; s/\bflush_capture_pipeline_state(/capture.FlushCapturePipelineState(res, frameGating, stats, /g; s/\blog_first_sent_generation(/capture.LogFirstSentGeneration(res, stats, /g; s/\bkick_try_fill(/capture.KickTryFill(clientSession, kick, /g; s/\beffective_queue_wait_timeout_us()/capture.EffectiveQueueWaitTimeoutUs(encoder)/g'
RAII="d3d ctx d3dContextMu fl gpuScaler frame captureReadback capturePublishFn inspectable d3dDevice pool session dxgiCaptureSession gdiCaptureProcess"

# --- generate the .cpp ---
{
  echo '// See host_capture_session.hpp for the module summary. The member bodies below are the former'
  echo '// capture lambdas of native_video_host_main.cpp, moved verbatim (host split refactor Phase 2-4):'
  echo '// "capture" aliases *this and the RAII objects are aliased from CaptureResources so the moved'
  echo '// text reads unchanged. Cross-calls between them were rewritten to member calls.'
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
  echo '#include <d3d11.h>'
  echo '#include <dxgi1_2.h>'
  echo '#include <windows.graphics.capture.interop.h>'
  echo '#include <windows.graphics.directx.direct3d11.interop.h>'
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
  echo '#include <cstring>'
  echo '#include <iostream>'
  echo '#include <memory>'
  echo '#include <mutex>'
  echo '#include <string>'
  echo '#include <thread>'
  echo '#include <vector>'
  echo
  echo '#include "capture_backend_dxgi.hpp"'
  echo '#include "d3d_capture_readback.hpp"'
  echo '#include "gdi_capture_process.hpp"'
  echo '#include "host_backend_policy.hpp"'
  echo '#include "host_bgra_scale.hpp"'
  echo '#include "host_capture_device.hpp"'
  echo '#include "host_capture_session.hpp"'
  echo '#include "host_encoder_manager.hpp"'
  echo '#include "host_frame_gate.hpp"'
  echo '#include "host_input_inject.hpp"'
  echo '#include "host_kick.hpp"'
  echo '#include "host_session.hpp"'
  echo '#include "host_stats.hpp"'
  echo '#include "host_string_util.hpp"'
  echo '#include "host_window_enum.hpp"'
  echo '#include "time_utils.hpp"'
  echo
  echo 'using namespace winrt::Windows::Graphics::Capture;'
  echo 'using namespace winrt::Windows::Graphics::DirectX::Direct3D11;'
  echo
  echo 'namespace remote60::native_poc {'
  echo
  for n in $NAMES; do
    s=${HS[$n]}; e=${HE[$n]}; ind=${HIND[$n]}; hh=${HHEAD[$n]}
    pr=$(own_params "$s" "$hh"); own=${pr%%|*}; ret=${pr##*|}
    if [ "$own" = "PARSE_FAIL" ]; then echo "PARSE_FAIL for $n" >&2; exit 1; fi
    params="${PRE[$n]}"; [ -n "$own" ] && params="$params, $own"
    sed -n "$((hh + 1)),$((e - 1))p" "$F" | sed -E "s/^ {$ind}//" > /tmp/body_$n.txt
    sed -i "$CALLSED" /tmp/body_$n.txt
    echo "$ret CaptureState::${MEMBER[$n]}($params) {"
    echo '  CaptureState& capture = *this;'
    if [[ "${PRE[$n]}" == *"CaptureResources& res"* ]]; then
      for r in $RAII; do
        if grep -q -E "\b$r\b" /tmp/body_$n.txt; then echo "  auto& $r = res.$r;"; fi
      done
    fi
    cat /tmp/body_$n.txt
    echo '}'
    echo
  done
  echo '}  // namespace remote60::native_poc'
} > $S/host_capture_session.cpp

# --- header: CaptureResources struct + member declarations ---
cat > /tmp/res_struct.txt <<'EOF'

// Capture resources (Phase 2-4): the RAII / WinRT / D3D objects the capture path owns -- the D3D
// device + immediate context (shared with the GPU scaler under d3dContextMu), the readback ring and
// its publish callback, the WinRT interop device, the WGC frame pool + session, and the DXGI / GDI
// backend sessions. Declared in the same order as the former main() locals so their destruction
// order is unchanged; main() still creates the device / interop device into them at the original
// points and owns the capture item, the FrameArrived event token and the DXGI worker watchdog.
// thread: main loop creates/destroys; the readback worker and capture callbacks use frame,
// captureReadback and the D3D context (under d3dContextMu) while a session is attached.
struct CaptureResources {
  Microsoft::WRL::ComPtr<ID3D11Device> d3d;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> ctx;
  std::mutex d3dContextMu;
  D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
  GpuBgraScaler gpuScaler;
  // FrameState precedes the pipeline so the worker's publish callback never outlives what it
  // writes into.
  FrameState frame;
  D3dCaptureReadbackPipeline captureReadback;
  D3dCaptureReadbackPipeline::PublishFn capturePublishFn;
  winrt::com_ptr<::IInspectable> inspectable;
  winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice d3dDevice{nullptr};
  winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool pool{nullptr};
  winrt::Windows::Graphics::Capture::GraphicsCaptureSession session{nullptr};
  remote60::host::DxgiDesktopCaptureSession dxgiCaptureSession;
  GdiCaptureProcess gdiCaptureProcess;
};
EOF
cat > /tmp/res_decls.txt <<'EOF'

  // --- behaviour (Phase 2-4: former main() capture lambdas; bodies in host_capture_session.cpp) ---
  // (Re)create the staging/readback ring for a srcW x srcH capture surface.
  bool CreateStaging(CaptureResources& res, EncoderState& encoder, bool useH264, uint32_t srcW, uint32_t srcH);
  // Hand a captured texture to the readback ring (capture callback thread).
  void PublishCapturedTexture(CaptureResources& res, ID3D11Texture2D* src, uint64_t callbackUs,
                              uint64_t sourceCaptureUs, uint64_t captureAgeAtCallbackUs,
                              uint64_t captureClockSkewUs, uint64_t sourceIntervalUs);
  // Subscribe the WGC FrameArrived callback on the current pool.
  void AttachFrameArrived(CaptureResources& res, SessionState& clientSession, std::atomic<bool>& stop,
                          winrt::event_token& token);
  // Tear down the current capture attachment (WGC pool/session, DXGI or GDI backend).
  void DetachCaptureSession(CaptureResources& res, winrt::event_token& token);
  // Full restart: detach, resolve the backend, rebuild the pool/readback, reattach.
  bool RestartCaptureSessionImpl(CaptureResources& res, DesktopBackendState& backend, SessionState& clientSession,
                                 EncoderState& encoder, std::atomic<bool>& stop, bool useH264,
                                 winrt::Windows::Graphics::Capture::GraphicsCaptureItem& item, winrt::event_token& token);
  // Roll a failed window selection back to the previous target.
  void RestorePreviousTarget(winrt::Windows::Graphics::Capture::GraphicsCaptureItem& item);
  // Drop everything queued for the encoder after a target/backend/size change.
  void FlushCapturePipelineState(CaptureResources& res, FrameGatingState& frameGating, HostStats& stats, const char* reason);
  void LogFirstSentGeneration(CaptureResources& res, HostStats& stats, const char* path, uint64_t streamGeneration,
                              uint64_t sendStartUs, uint64_t captureStampUs, uint32_t width, uint32_t height);
  // Serve the cached bootstrap frame for a trailing kick / static refresh, if still valid.
  bool KickTryFill(SessionState& clientSession, KickState& kick, std::shared_ptr<std::vector<uint8_t>>& outPayload,
                   uint32_t& outW, uint32_t& outH, uint32_t& outStride, uint64_t nowUs);
  uint64_t EffectiveQueueWaitTimeoutUs(EncoderState& encoder);
EOF
s=$(grep -n -m1 '^struct CaptureState {' "$H" | cut -d: -f1)
e=$(awk -v s="$s" 'NR>s && /^};$/ {print NR; exit}' "$H")
sed -i "$((e - 1))r /tmp/res_decls.txt" "$H"
sed -i "$((s - 1))r /tmp/res_struct.txt" "$H"   # struct CaptureResources before CaptureState
# forward declarations + includes the new members need
sed -i 's|^namespace remote60::native_poc {$|namespace remote60::native_poc {\n\nstruct DesktopBackendState;\nstruct EncoderState;\nstruct FrameGatingState;\nstruct HostStats;\nstruct KickState;\nstruct SessionState;|' "$H"
sed -i 's|^#include <winrt/Windows.Graphics.h>$|#include <d3d11.h>\n#include <wrl/client.h>\n\n#include <winrt/Windows.Graphics.h>\n#include <winrt/Windows.Graphics.Capture.h>\n#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>|' "$H"
sed -i 's|^#include "capture_cadence_gate.hpp"$|#include "capture_backend_dxgi.hpp"\n#include "capture_cadence_gate.hpp"\n#include "d3d_capture_readback.hpp"\n#include "gdi_capture_process.hpp"\n#include "host_frame_state.hpp"\n#include "host_gpu_scaler.hpp"|' "$H"

# --- main(): delete the lambda definitions (highest first), rewrite call sites ---
for n in $NAMES; do echo "${HS[$n]} ${HE[$n]}"; done | sort -rn | while read a b; do sed -i "${a},${b}d" "$F"; done
sed -i "$CALLSED" "$F"

# --- main(): CaptureResources grouping (rename RAII locals, main() part only) ---
: > /tmp/map24.txt; for r in $RAII; do printf '%s\tres.%s\n' "$r" "$r" >> /tmp/map24.txt; done
m=$(grep -n -m1 '^int main(int argc' "$F" | cut -d: -f1)
head -n $((m - 1)) "$F" > /tmp/hm_head.cpp; tail -n +$m "$F" | perl automation/rename_outside_strings.pl /tmp/map24.txt > /tmp/hm_tail.cpp; cat /tmp/hm_head.cpp /tmp/hm_tail.cpp > "$F"
d=$(grep -n -m1 '^  Microsoft::WRL::ComPtr<ID3D11Device> res\.d3d;$' "$F" | cut -d: -f1)
sed -i -E '/^  Microsoft::WRL::ComPtr<ID3D11Device> res\.d3d;$/d; /^  Microsoft::WRL::ComPtr<ID3D11DeviceContext> res\.ctx;$/d; /^  std::mutex res\.d3dContextMu;$/d; /^  D3D_FEATURE_LEVEL res\.fl = D3D_FEATURE_LEVEL_11_0;$/d; /^  GpuBgraScaler res\.gpuScaler;$/d; /^  FrameState res\.frame;$/d; /^  remote60::native_poc::D3dCaptureReadbackPipeline res\.captureReadback;$/d; /^  remote60::native_poc::D3dCaptureReadbackPipeline::PublishFn res\.capturePublishFn;$/d; /^  winrt::com_ptr<::IInspectable> res\.inspectable;$/d; /^  Direct3D11CaptureFramePool res\.pool\{nullptr\};$/d; /^  GraphicsCaptureSession res\.session\{nullptr\};$/d; /^  DxgiDesktopCaptureSession res\.dxgiCaptureSession;$/d; /^  GdiCaptureProcess res\.gdiCaptureProcess;$/d' "$F"
sed -i 's/^  auto res\.d3dDevice = res\.inspectable\.as<IDirect3DDevice>();$/  res.d3dDevice = res.inspectable.as<IDirect3DDevice>();/' "$F"
sed -i 's/\[&res\.dxgiCaptureSession, &dxgiWatchdogStop\]/[\&dxgiCaptureSession = res.dxgiCaptureSession, \&dxgiWatchdogStop]/' "$F"
printf '  // RAII / WinRT / D3D capture objects (CaptureResources, Phase 2-4); created below at the same points as before.\n  CaptureResources res;\n' > /tmp/inst.txt
sed -i "$((d - 1))r /tmp/inst.txt" "$F"
sed -i '/^using remote60::native_poc::CaptureState;$/a\
using remote60::native_poc::CaptureResources;' "$F"
sed -i 's|^  src/host_encoded_sender.cpp$|  src/host_encoded_sender.cpp\n  src/host_capture_session.cpp|' "$C"

echo "== leftover lambda names in main:"; grep -n -E '\b(create_staging|publish_captured_texture|attach_frame_arrived|detach_capture_session|restart_capture_session_impl|restore_previous_target|flush_capture_pipeline_state|log_first_sent_generation|kick_try_fill|effective_queue_wait_timeout_us)(' "$F" | cut -c1-100 || true
echo "== leftover RAII decl-form lines:"; grep -n -E '^  (Microsoft::WRL::ComPtr<ID3D11Device(Context)?>|std::mutex|D3D_FEATURE_LEVEL|GpuBgraScaler|FrameState|remote60::native_poc::D3dCaptureReadbackPipeline(::PublishFn)?|winrt::com_ptr<::IInspectable>|Direct3D11CaptureFramePool|GraphicsCaptureSession|DxgiDesktopCaptureSession|GdiCaptureProcess) res\.' "$F" | cut -c1-100 || true
echo "res. uses in main: $(grep -c 'res\.' "$F"); CaptureResources res at: $(grep -n '^  CaptureResources res;' "$F" | cut -d: -f1)"
echo "host_main now $(wc -l < "$F") lines; cpp $(wc -l < $S/host_capture_session.cpp) lines"
