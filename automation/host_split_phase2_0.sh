#!/usr/bin/env bash
# Host split refactor Phase 2-0: move the twelve state structs (and their helper types) out of
# native_video_host_main.cpp into one header per future class, verbatim. Run from the repo root.
set -euo pipefail
F=apps/native_poc/src/native_video_host_main.cpp
S=apps/native_poc/src
cp "$F" /tmp/host_main_before_2_0.cpp

# range NAME -> prints "start end" where start is the first line of the contiguous // comment block
# above "struct NAME {" and end is the closing "};" (plus one trailing blank line if present).
range() {
  local s c e
  s=$(grep -n -m1 "^struct $1 {" "$F" | cut -d: -f1)
  c=$((s - 1))
  while sed -n "${c}p" "$F" | grep -q '^//'; do c=$((c - 1)); done
  c=$((c + 1))
  e=$(awk -v s="$s" 'NR>s && /^};$/ {print NR; exit}' "$F")
  if sed -n "$((e + 1))p" "$F" | grep -q '^$'; then e=$((e + 1)); fi
  echo "$c $e"
}

emit_header() {  # emit_header FILE TITLE "includes..." RANGES...
  local out=$1 title=$2 incs=$3; shift 3
  {
    echo '#pragma once'
    echo
    echo "// $title"
    echo '//'
    echo '// Host split refactor Phase 2-0: this state moved out of native_video_host_main.cpp verbatim so'
    echo '// it can be read on its own; the struct comment below documents role and thread ownership.'
    echo '// Phase 2 turns it into the class that owns the matching main() lambdas.'
    echo
    printf '%s\n' "$incs"
    echo
    echo 'namespace remote60::native_poc {'
    echo
    for r in "$@"; do
      set -- $r
      sed -n "$1,$2p" "$F"
    done
    echo '}  // namespace remote60::native_poc'
  } > "$out"
}

# --- compute every range up front (line numbers are only valid before any deletion) ---
R_FG=$(range FrameGatingState)
R_RC=$(range RateControlState)
R_KI=$(range KickState)
R_CM=$(range ClientMetricsSnapshot)
R_DB=$(range DesktopBackendState)
R_WD=$(range WatchdogState)
R_IR=$(range InputRouterState)
R_ES=$(range EncodedSendItem)
R_SN=$(range SenderState)
R_SC=$(range SocketCloser)
R_SS=$(range SessionState)
R_NV=$(range Nv12PendingRelease)
R_EN=$(range EncoderState)
R_HS=$(range HostStats)
R_BF=$(range BootstrapFrameCache)
R_CS=$(range CaptureState)
# main-loop phase enum + watchdog exit codes (comment .. last constexpr, plus trailing blank)
P1=$(grep -n -m1 "^// Phase of the host's main capture/encode loop" "$F" | cut -d: -f1)
P2=$(grep -n -m1 '^constexpr unsigned int kExitDxgiWorkerWedge = 44;' "$F" | cut -d: -f1)
if sed -n "$((P2 + 1))p" "$F" | grep -q '^$'; then P2=$((P2 + 1)); fi
R_PH="$P1 $P2"

emit_header $S/host_frame_gate.hpp      'Static-frame gating state (FrameGatingState).' \
  $'#include <cstdint>\n#include <memory>\n#include <vector>' "$R_FG"
emit_header $S/host_abr.hpp             'ABR profile ladder + M9 level ladder state (RateControlState).' \
  $'#include <cstdint>' "$R_RC"
emit_header $S/host_kick.hpp            'Trailing-edge kick / static refresh state (KickState).' \
  $'#include <cstdint>' "$R_KI"
emit_header $S/host_client_metrics.hpp  'Viewer-reported metrics + keyframe requests (ClientMetricsSnapshot).' \
  $'#include <atomic>\n#include <cstdint>' "$R_CM"
emit_header $S/host_backend_policy.hpp  'Desktop capture backend policy state (DesktopBackendState).' \
  $'#include <atomic>\n#include <cstdint>\n\n#include "host_capture_device.hpp"' "$R_DB"
emit_header $S/host_watchdog.hpp        'Main-loop phase, watchdog exit codes and watchdog state (WatchdogState).' \
  $'#include <atomic>\n#include <cstdint>' "$R_PH" "$R_WD"
emit_header $S/host_input_router.hpp    'Viewer input routing state (InputRouterState).' \
  $'#include <atomic>\n#include <climits>\n#include <cstdint>\n\n#include "host_input_inject.hpp"\n#include "host_window_enum.hpp"\n#include "secure_input_broker.hpp"' "$R_IR"
emit_header $S/host_encoded_sender.hpp  'Encoded-frame sender queue/thread state (EncodedSendItem, SenderState).' \
  $'#include <winsock2.h>\n#include <windows.h>\n\n#include <atomic>\n#include <condition_variable>\n#include <cstdint>\n#include <deque>\n#include <mutex>\n#include <thread>\n#include <vector>\n\n#include "poc_protocol.hpp"' "$R_ES" "$R_SN"
emit_header $S/host_session.hpp         'Client session sockets / directory / epoch state (SocketCloser, SessionState).' \
  $'#include <winsock2.h>\n#include <windows.h>\n\n#include <atomic>\n#include <condition_variable>\n#include <cstdint>\n#include <mutex>\n#include <string>\n#include <thread>\n\n#include "directory_client.hpp"\n#include "native_socket.hpp"\n#include "udp_control_channel.hpp"' "$R_SC" "$R_SS"
emit_header $S/host_encoder_manager.hpp 'Encoder management state (Nv12PendingRelease, EncoderState).' \
  $'#include <atomic>\n#include <cstdint>\n#include <deque>\n#include <string>\n\n#include "mf_h264_codec.hpp"' "$R_NV" "$R_EN"
emit_header $S/host_stats.hpp           'Host pipeline statistics accumulators (HostStats).' \
  $'#include <atomic>\n#include <cstdint>' "$R_HS"
emit_header $S/host_capture_session.hpp 'Capture session state (BootstrapFrameCache, CaptureState).' \
  $'#include <windows.h>\n\n#include <winrt/Windows.Graphics.h>\n\n#include <atomic>\n#include <cstdint>\n#include <limits>\n#include <memory>\n#include <mutex>\n#include <optional>\n#include <string>\n#include <vector>\n\n#include "capture_cadence_gate.hpp"\n#include "host_capture_device.hpp"\n#include "host_window_enum.hpp"' "$R_BF" "$R_CS"

# --- delete the blocks from host_main, highest range first ---
for r in "$R_CS" "$R_BF" "$R_HS" "$R_EN" "$R_NV" "$R_SS" "$R_SC" "$R_SN" "$R_ES" "$R_IR" "$R_WD" "$R_DB" "$R_CM" "$R_KI" "$R_RC" "$R_FG" "$R_PH"; do
  set -- $r
  sed -i "$1,$2d" "$F"
done

# --- includes + using-declarations in host_main ---
sed -i 's|^#include "host_input_inject.hpp"$|#include "host_input_inject.hpp"\n#include "host_frame_gate.hpp"\n#include "host_abr.hpp"\n#include "host_kick.hpp"\n#include "host_client_metrics.hpp"\n#include "host_backend_policy.hpp"\n#include "host_watchdog.hpp"\n#include "host_input_router.hpp"\n#include "host_encoded_sender.hpp"\n#include "host_session.hpp"\n#include "host_encoder_manager.hpp"\n#include "host_stats.hpp"\n#include "host_capture_session.hpp"|' "$F"
sed -i '/^using remote60::native_poc::apply_input_text_message;$/a\
// State structs extracted to their own headers (Phase 2-0); each becomes a class in Phase 2.\
using remote60::native_poc::FrameGatingState;\
using remote60::native_poc::RateControlState;\
using remote60::native_poc::KickState;\
using remote60::native_poc::ClientMetricsSnapshot;\
using remote60::native_poc::DesktopBackendState;\
using remote60::native_poc::MainLoopPhase;\
using remote60::native_poc::kExitMainLoopWatchdog;\
using remote60::native_poc::kExitDxgiWorkerWedge;\
using remote60::native_poc::WatchdogState;\
using remote60::native_poc::InputRouterState;\
using remote60::native_poc::EncodedSendItem;\
using remote60::native_poc::SenderState;\
using remote60::native_poc::SessionState;\
using remote60::native_poc::Nv12PendingRelease;\
using remote60::native_poc::EncoderState;\
using remote60::native_poc::HostStats;\
using remote60::native_poc::BootstrapFrameCache;\
using remote60::native_poc::CaptureState;' "$F"

# --- pure-move check: removed lines vs header bodies ---
(diff /tmp/host_main_before_2_0.cpp "$F" || true) | grep '^<' | sed 's/^< //' | grep -v '^$' | sort > /tmp/removed.txt
cat $S/host_frame_gate.hpp $S/host_abr.hpp $S/host_kick.hpp $S/host_client_metrics.hpp $S/host_backend_policy.hpp \
    $S/host_watchdog.hpp $S/host_input_router.hpp $S/host_encoded_sender.hpp $S/host_session.hpp \
    $S/host_encoder_manager.hpp $S/host_stats.hpp $S/host_capture_session.hpp \
  | grep -v -E '^(#pragma once|#include |namespace remote60::native_poc \{|\}  // namespace remote60::native_poc|// Host split refactor Phase 2-0|// it can be read on its own|// Phase 2 turns it into|//$)$' \
  | grep -v -E '^// (Static-frame gating state|ABR profile ladder|Trailing-edge kick / static refresh state|Viewer-reported metrics \+ keyframe requests \(ClientMetricsSnapshot\)|Desktop capture backend policy state|Main-loop phase, watchdog exit codes|Viewer input routing state|Encoded-frame sender queue/thread state|Client session sockets|Encoder management state|Host pipeline statistics accumulators|Capture session state \(BootstrapFrameCache)' \
  | grep -v '^$' | sort > /tmp/body.txt
echo "removed=$(wc -l < /tmp/removed.txt) headers=$(wc -l < /tmp/body.txt)"
if diff /tmp/removed.txt /tmp/body.txt; then echo "PURE MOVE: IDENTICAL"; else echo "PURE MOVE: DIFF ABOVE"; fi
echo "host_main now $(wc -l < "$F") lines; structs left at file scope: $(grep -c '^struct ' "$F" || true)"
