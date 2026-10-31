#!/usr/bin/env bash
# Host split refactor Phase 2-3: move the encoded-sender thread body, pump_udp_hello and the two
# session-epoch lambdas out of main() into member functions (host_encoded_sender.cpp /
# host_session.hpp), verbatim. Run once from the repo root on a tree where the lambdas still exist.
set -euo pipefail
S=apps/native_poc/src
F=$S/native_video_host_main.cpp
C=apps/native_poc/CMakeLists.txt
cp "$F" /tmp/host_main_before_2_3.cpp

lambda_range() {  # lambda_range NAME -> "start end" (closing "};" at the same indentation)
  local s ind e
  s=$(grep -n -m1 -E "^ *(const )?auto $1 = \[" "$F" | cut -d: -f1)
  ind=$(sed -n "${s}p" "$F" | sed -E 's/^( *).*/\1/' | wc -c); ind=$((ind - 1))
  e=$(awk -v s="$s" -v ind="$ind" 'NR>s && $0 ~ ("^" sprintf("%" ind "s","") "};?$") {print NR; exit}' "$F")
  echo "$s $e"
}

R_SS=$(lambda_range start_encoded_sender)
R_PH=$(lambda_range pump_udp_hello)
R_BE=$(lambda_range begin_session_epoch)
R_AC=$(lambda_range await_control_ready)
set -- $R_SS; SS_S=$1; SS_E=$2
set -- $R_PH; PH_S=$1; PH_E=$2
set -- $R_BE; BE_S=$1; BE_E=$2
set -- $R_AC; AC_S=$1; AC_E=$2
echo "start_encoded_sender $SS_S..$SS_E pump_udp_hello $PH_S..$PH_E begin_session_epoch $BE_S..$BE_E await_control_ready $AC_S..$AC_E"

# Bodies (inner lines), de-indented by two.
sed -n "$((SS_S + 1)),$((SS_E - 1))p" "$F" | sed 's/^  //' > /tmp/b_ss.txt
sed -n "$((PH_S + 1)),$((PH_E - 1))p" "$F" | sed 's/^  //' > /tmp/b_ph.txt
sed -n "$((BE_S + 1)),$((BE_E - 1))p" "$F" | sed 's/^  //' > /tmp/b_be.txt
sed -n "$((AC_S + 1)),$((AC_E - 1))p" "$F" | sed 's/^  //' > /tmp/b_ac.txt

# --- host_encoded_sender.cpp: StartThread + PumpUdpHello ---
{
  echo '// See host_encoded_sender.hpp for the module summary. The bodies below are the former'
  echo '// start_encoded_sender / pump_udp_hello lambdas of native_video_host_main.cpp, moved verbatim'
  echo '// (host split refactor Phase 2-3). "sender" aliases *this so the moved text reads unchanged.'
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
  echo '#include <mutex>'
  echo '#include <thread>'
  echo
  echo '#include "host_args.hpp"'
  echo '#include "host_encoded_sender.hpp"'
  echo '#include "host_encoder_manager.hpp"'
  echo '#include "host_net_io.hpp"'
  echo '#include "host_session.hpp"'
  echo '#include "native_video_transport.hpp"'
  echo '#include "poc_protocol.hpp"'
  echo '#include "time_utils.hpp"'
  echo
  echo 'namespace remote60::native_poc {'
  echo
  echo 'void SenderState::StartThread(VideoTransport transport, bool useH264, const Args& args,'
  echo '                              SessionState& clientSession) {'
  echo '  SenderState& sender = *this;'
  cat /tmp/b_ss.txt
  echo '}'
  echo
  echo 'void SenderState::PumpUdpHello(VideoTransport transport, EncoderState& encoder) {'
  echo '  SenderState& sender = *this;'
  cat /tmp/b_ph.txt
  echo '}'
  echo
  echo '}  // namespace remote60::native_poc'
} > $S/host_encoded_sender.cpp

# --- declarations in host_encoded_sender.hpp (before the closing "};" of SenderState) ---
cat > /tmp/decl_ss.txt <<'EOF'

  // --- behaviour (Phase 2-3: former main() lambdas start_encoded_sender / pump_udp_hello;
  //     bodies in host_encoded_sender.cpp) ---
  // Start the sender thread (UDP + H.264 only). It dequeues AUs, paces them onto the wire and
  // drives the media barrier; see the thread body for the full policy.
  void StartThread(VideoTransport transport, bool useH264, const Args& args, SessionState& clientSession);
  // Consume a reader-thread peer change: swap the peer and roll the media session over in one
  // transaction (drop backlog, hold deltas until an IDR, bump the epoch, force a keyframe).
  void PumpUdpHello(VideoTransport transport, EncoderState& encoder);
EOF
s=$(grep -n -m1 '^struct SenderState {' $S/host_encoded_sender.hpp | cut -d: -f1)
e=$(awk -v s="$s" 'NR>s && /^};$/ {print NR; exit}' $S/host_encoded_sender.hpp)
sed -i "$((e - 1))r /tmp/decl_ss.txt" $S/host_encoded_sender.hpp
sed -i 's|^#include "poc_protocol.hpp"$|#include "native_video_transport.hpp"\n#include "poc_protocol.hpp"|' $S/host_encoded_sender.hpp
sed -i 's|^namespace remote60::native_poc {$|namespace remote60::native_poc {\n\nstruct Args;\nstruct EncoderState;\nstruct SessionState;|' $S/host_encoded_sender.hpp

# --- SessionState::BeginEpoch / AwaitControlReady (inline in host_session.hpp) ---
{
  echo
  echo '  // --- behaviour (Phase 2-3: former main() lambdas begin_session_epoch / await_control_ready) ---'
  echo '  // Open a new session epoch for a just-connected client; returns the epoch to wait on.'
  echo '  uint64_t BeginEpoch() {'
  echo '    SessionState& clientSession = *this;'
  sed 's/^/  /' /tmp/b_be.txt
  echo '  }'
  echo '  // Block until the control channel has served that epoch (or the host stops).'
  echo '  void AwaitControlReady(uint64_t epoch) {'
  echo '    SessionState& clientSession = *this;'
  sed 's/^/  /' /tmp/b_ac.txt
  echo '  }'
} > /tmp/decl_sess.txt
s=$(grep -n -m1 '^struct SessionState {' $S/host_session.hpp | cut -d: -f1)
e=$(awk -v s="$s" 'NR>s && /^};$/ {print NR; exit}' $S/host_session.hpp)
sed -i "$((e - 1))r /tmp/decl_sess.txt" $S/host_session.hpp

# --- host_main: delete the four lambdas (highest first) and rewrite the call sites ---
for r in "$SS_S $SS_E" "$PH_S $PH_E" "$AC_S $AC_E" "$BE_S $BE_E"; do set -- $r; echo "$1 $2"; done | sort -rn | while read a b; do sed -i "${a},${b}d" "$F"; done
sed -i 's/\bstart_encoded_sender();/sender.StartThread(transport, useH264, args, clientSession);/; s/\bpump_udp_hello();/sender.PumpUdpHello(transport, encoder);/; s/\bbegin_session_epoch()/clientSession.BeginEpoch()/; s/\bawait_control_ready(epoch);/clientSession.AwaitControlReady(epoch);/' "$F"
sed -i 's|^  src/host_control_session.cpp$|  src/host_control_session.cpp\n  src/host_encoded_sender.cpp|' "$C"

echo "== leftovers:"; grep -n -E '\b(start_encoded_sender|pump_udp_hello|begin_session_epoch|await_control_ready)\b' "$F" | cut -c1-100 || true
echo "== new call sites:"; grep -n -E 'sender\.(StartThread|PumpUdpHello)\(|clientSession\.(BeginEpoch|AwaitControlReady)\(' "$F" | cut -c1-110
echo "== pure-move checks:"
awk '/^void SenderState::StartThread/ {on=1; getline; getline; next} on && /^}$/ {exit} on' $S/host_encoded_sender.cpp | diff - /tmp/b_ss.txt && echo "StartThread IDENTICAL"
awk '/^void SenderState::PumpUdpHello/ {on=1; getline; next} on && /^}$/ {exit} on' $S/host_encoded_sender.cpp | diff - /tmp/b_ph.txt && echo "PumpUdpHello IDENTICAL"
echo "host_main now $(wc -l < "$F") lines"
