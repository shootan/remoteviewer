#!/usr/bin/env bash
# Host split refactor Phase 2-12 (HostRuntime assembly): move the linear startup blocks of main(), the
# DXGI-worker / main-loop watchdog starters and the shutdown sequence out of native_video_host_main.cpp
# into host_startup_*.cpp / host_shutdown.cpp, verbatim (alias preludes recomputed per function).
# main() keeps: the state declarations in the monolith's order (= the destruction order), the
# HostContext assembly, the startup call sequence, the 15-line loop and the tail.
# The two directory-Hello lambdas become SessionState members (host_session.hpp).
# Run once from the repo root on the 6b3bb60 tree (main = 1,952 lines).
set -euo pipefail
S=apps/native_poc/src
M=$S/native_video_host_main.cpp
C=apps/native_poc/CMakeLists.txt
git diff --quiet HEAD -- "$M" $S/host_session.hpp $S/host_main_loop.hpp "$C" || { echo "tree not clean"; exit 1; }
[ "$(wc -l < "$M")" = 1952 ] || { echo "unexpected main length $(wc -l < "$M")"; exit 1; }
cp "$M" /tmp/main_before.cpp
O=/tmp/main_before.cpp
L() { sed -n "$1,$2p" "$O"; }   # a line range of the ORIGINAL main
INC_END=$(grep -n -m1 '^#include "host_main_loop.hpp"$' "$O" | cut -d: -f1)
USING_RCS=$(grep -n -m1 '^using remote60::native_poc::restart_capture_session;$' "$O" | cut -d: -f1)
[ -n "$INC_END" ] && [ -n "$USING_RCS" ] || { echo "anchor lines not found"; exit 1; }
echo "include block ends $INC_END, restart_capture_session using at $USING_RCS"

HN="args useH264 useRaw transport stop guardStaleEncoded guardStalePreEncode paceByTick startUs nextTickUs captureWindowRebindIntervalUs nextCaptureWindowCheckUs streamActiveApplied streamActiveSinceUs poppedNv12Slot poppedNv12Generation powerKeepalive item token windowSelectionTxn frameGating rate kick clientMetrics backend watchdog inputRouter sender clientSession encoder stats capture res"
aliases() { for n in $HN; do grep -q -E "(^|[^.A-Za-z0-9_>])$n\b" "$1" && echo "  auto& $n = hx.$n;"; done; return 0; }
emit_fn() {  # emit_fn RET NAME EXTRA_PARAMS BODYFILE
  local ret=$1 name=$2 extra=$3 body=$4
  echo "$ret $name(HostContext& hx$extra) {"
  aliases "$body"
  cat "$body"
  [ "$ret" = int ] && echo "  return 0;"
  echo "}"
}

# ---------------- bodies (original line ranges; three call-site renames, one thread-start rewrite) ----
B=/tmp/p212; rm -rf $B; mkdir -p $B
{ L 403 423; } > $B/process_setup.txt
{ L 428 432; L 438 464; L 468 471; L 475 492; L 495 506; L 510 515; L 520 556; } > $B/configure_from_env.txt
{ L 563 566; L 568 575; } > $B/resolve_transport.txt
{ L 577 651; } > $B/log_config.txt
{ L 653 659; L 662 667; L 669 675; } > $B/configure_session.txt
{ L 709 990; } | sed 's/authorize_directory_session(authToken, peer)/clientSession.AuthorizeDirectorySession(authToken, peer)/' > $B/connect_client.txt
{ L 1002 1004; L 1007 1031; } > $B/configure_control_state.txt
{ L 1037 1294; } | sed 's/classify_directory_hello(authToken, peer)/clientSession.ClassifyDirectoryHello(authToken, peer)/' > $B/start_control_threads.txt
{ L 1296 1315; L 1318 1333; } > $B/init_graphics.txt
{ L 1335 1412; L 1414 1443; } > $B/select_capture_target.txt
{ L 1445 1548; } > $B/configure_encode_geometry.txt
{ L 1555 1587; } > $B/init_encoder.txt
{ L 1590 1597; L 1599 1657; } | sed 's/^  std::thread dxgiWorkerWatchdog(\[&dxgiCaptureSession = res.dxgiCaptureSession, &dxgiWatchdogStop\]() {$/  dxgiWorkerWatchdog = std::thread([\&dxgiCaptureSession = res.dxgiCaptureSession, \&dxgiWatchdogStop]() {/' > $B/start_dxgi_watchdog.txt
grep -q '^  dxgiWorkerWatchdog = std::thread(' $B/start_dxgi_watchdog.txt || { echo "thread-start rewrite failed"; exit 1; }
{ L 1666 1691; L 1694 1705; L 1709 1739; } > $B/create_readback.txt
{ L 1744 1748; L 1768 1843; } | sed 's/restart_capture_session(host)/restart_capture_session(hx)/' > $B/start_capture.txt
{ L 1845 1888; } > $B/start_main_loop_watchdog.txt
{ L 1908 1949; } > $B/shutdown_host.txt
grep -c 'AuthorizeDirectorySession' $B/connect_client.txt | grep -q '^1$' || { echo "authorize rename failed"; exit 1; }
grep -c 'ClassifyDirectoryHello' $B/start_control_threads.txt | grep -q '^1$' || { echo "classify rename failed"; exit 1; }
grep -c 'restart_capture_session(hx)' $B/start_capture.txt | grep -q '^1$' || { echo "restart rename failed"; exit 1; }

# ---------------- preamble shared by the new TUs: main()'s include block + the macro + the usings ----
{
  L 1 "$INC_END"
  echo '#include "host_startup.hpp"'
  echo
  echo '#ifndef REMOTE60_NATIVE_ENCODED_EXPERIMENT'
  echo '#define REMOTE60_NATIVE_ENCODED_EXPERIMENT 0'
  echo '#endif'
  echo
  echo 'using namespace winrt::Windows::Graphics::Capture;'
  echo 'using namespace winrt::Windows::Graphics::DirectX::Direct3D11;'
  echo 'using remote60::host::DesktopCaptureBackend;'
  echo 'using remote60::host::DxgiDesktopCaptureConfig;'
  echo 'using remote60::host::DxgiDesktopCaptureSession;'
  echo
} > $B/preamble.txt
grep -q '^#include "host_main_loop.hpp"$' $B/preamble.txt || { echo "preamble missing host_main_loop.hpp"; exit 1; }

emit_tu() {  # emit_tu OUTFILE TITLE... (then reads function specs from stdin: RET NAME EXTRA BODY)
  local out=$1; shift
  {
    for t in "$@"; do echo "// $t"; done
    echo '//'
    echo '// Host split refactor Phase 2-12: moved verbatim out of main() (native_video_host_main.cpp); see'
    echo '// host_startup.hpp for the call order and HostContext (host_main_loop.hpp) for the shared state.'
    echo
    cat $B/preamble.txt
    echo 'namespace remote60::native_poc {'
    echo
    while IFS="|" read -r ret name extra body; do
      emit_fn "$ret" "$name" "$extra" "$B/$body.txt"
      echo
    done
    echo '}  // namespace remote60::native_poc'
  } > "$out"
}

emit_tu $S/host_startup_config.cpp \
  'Host startup 1/5: process setup, REMOTE60_NATIVE_* env -> state structs, transport, the startup log' \
  'lines, directory credentials, control-state defaults.' <<'EOF'
void|startup_log_config||log_config
void|startup_configure_session||configure_session
void|startup_configure_control_state||configure_control_state
EOF
# the first three functions of this TU have non-HostContext signatures; prepend them by hand
{
  awk '/^namespace remote60::native_poc \{$/ {print; print ""; exit} {print}' $S/host_startup_config.cpp
  echo 'void startup_process_setup() {'
  cat $B/process_setup.txt
  echo '}'
  echo
  emit_fn int startup_configure_from_env "" $B/configure_from_env.txt
  echo
  echo 'int resolve_transport(const Args& args, bool useRaw, bool useH264, VideoTransport& transport) {'
  cat $B/resolve_transport.txt
  echo '  return 0;'
  echo '}'
  echo
  awk '/^namespace remote60::native_poc \{$/ {on=1; getline; next} on' $S/host_startup_config.cpp
} > $B/config_full.cpp
mv $B/config_full.cpp $S/host_startup_config.cpp

emit_tu $S/host_startup_connect.cpp \
  'Host startup 2/5: the client connection -- TCP listen/accept, or UDP bind (port candidates, LAN' \
  'direct-dial listener, directory agent) + the Hello handshake; secure-input broker; socket buffers.' <<'EOF'
int|startup_connect_client||connect_client
EOF

emit_tu $S/host_startup_control.cpp \
  'Host startup 3/5: the control threads -- TCP control accept loop, UDP control channel + reader' \
  'thread (Hello / session epoch) + dispatcher thread.' <<'EOF'
void|startup_start_control_threads|, ControlSessionServer& controlServer|start_control_threads
EOF

emit_tu $S/host_startup_graphics.cpp \
  'Host startup 4/5: WinRT / WGC / Media Foundation / D3D bring-up, capture target (window criteria,' \
  'monitor, capture item, size), encode geometry + ABR / M9 ladders, H.264 encoder init.' <<'EOF'
int|startup_init_graphics||init_graphics
int|startup_select_capture_target||select_capture_target
void|startup_configure_encode_geometry||configure_encode_geometry
int|startup_init_encoder||init_encoder
EOF

emit_tu $S/host_startup_capture.cpp \
  'Host startup 5/5: DXGI capture-worker wedge watchdog, readback pipeline (publish callback + staging),' \
  'capture session start + timing/stats anchors + sender thread, main-loop liveness watchdog.' <<'EOF'
void|startup_start_dxgi_watchdog|, std::atomic<bool>& dxgiWatchdogStop, std::thread& dxgiWorkerWatchdog|start_dxgi_watchdog
int|startup_create_readback||create_readback
int|startup_start_capture||start_capture
void|startup_start_main_loop_watchdog||start_main_loop_watchdog
EOF

emit_tu $S/host_shutdown.cpp \
  'Host shutdown: stop the threads, detach capture, stop the readback worker and the sender, close the' \
  'sockets, shut the encoder / Media Foundation down.' <<'EOF'
void|shutdown_host||shutdown_host
EOF

# ---------------- host_startup.hpp ----------------
{
  cat <<'EOF'
#pragma once

// Host startup / shutdown sequence: the former linear blocks of main(), one function each, in call order.
//
// Role:    Everything main() did before the loop (env config, transport, sockets + handshake, control
//          threads, D3D/WGC/MF bring-up, capture target, encode geometry, encoder init, watchdogs,
//          readback pipeline, capture start) and after it (shutdown). Functions returning int give
//          the exit code main() used to return at that point (0 = go on).
// Thread:  main thread; the start_* functions spawn the control / reader / watchdog threads.
// Input:   HostContext (the state structs main() declares first), plus the few objects the loop never
//          sees (ControlSessionServer, the DXGI watchdog thread + flag); WinsockScope stays in main().
// Output:  configured state, running threads, exit codes.
// Callers: native_video_host_main.cpp only.
//
// Host split refactor Phase 2-12 (HostRuntime assembly): bodies moved verbatim from main().

#include <atomic>
#include <thread>

#include "host_args.hpp"
#include "host_control_session.hpp"
#include "host_main_loop.hpp"
#include "native_video_transport.hpp"

namespace remote60::native_poc {

// Stops and joins the DXGI worker watchdog thread. Declared in main() right after the thread, so it
// runs before res.dxgiCaptureSession (whose progress block the thread reads) is destroyed.
EOF
  L 1658 1664 | sed -E 's/^  //'
  echo '};'
  cat <<'EOF'

void startup_process_setup();                          // log timestamp prefix, latency priority
int startup_configure_from_env(HostContext& hx);       // REMOTE60_NATIVE_* env -> state structs; codec check (11)
int resolve_transport(const Args& args, bool useRaw, bool useH264, VideoTransport& transport);  // (15, 16)
void startup_log_config(HostContext& hx);              // "waiting client" / pacing / input-injection lines
void startup_configure_session(HostContext& hx);       // directory credentials, media bind port
int startup_connect_client(HostContext& hx);           // TCP accept or UDP bind + Hello handshake (2..5)
void startup_configure_control_state(HostContext& hx); // backend request, key-request bucket, input target
void startup_start_control_threads(HostContext& hx, ControlSessionServer& controlServer);
int startup_init_graphics(HostContext& hx);            // WinRT apartment, WGC check (6), MFStartup (12), D3D (7)
int startup_select_capture_target(HostContext& hx);    // window criteria, monitor, capture item, size (8, 9)
void startup_configure_encode_geometry(HostContext& hx);  // encode size, ABR / M9 ladders, frame intervals
int startup_init_encoder(HostContext& hx);             // H.264 encoder init (13), WinRT D3D device
void startup_start_dxgi_watchdog(HostContext& hx, std::atomic<bool>& dxgiWatchdogStop,
                                 std::thread& dxgiWorkerWatchdog);
int startup_create_readback(HostContext& hx);          // publish callback, readback pipeline (10)
int startup_start_capture(HostContext& hx);            // capture session start (10), timing / stats anchors, sender thread
void startup_start_main_loop_watchdog(HostContext& hx);
void shutdown_host(HostContext& hx);                   // stop threads, detach capture, close sockets, MFShutdown

}  // namespace remote60::native_poc
EOF
} > $S/host_startup.hpp

# ---------------- SessionState: DirectoryHello enum + the two former lambdas ----------------
H=$S/host_session.hpp
SS=$(grep -n -m1 '^struct SessionState {$' "$H" | cut -d: -f1)
SE=$(awk -v s="$SS" 'NR>s && /^};$/ {print NR; exit}' "$H")
{
  sed -n "1,$((SS - 1))p" "$H"
  L 676 678
  L 679 679
  echo
  sed -n "${SS},$((SE - 1))p" "$H"
  echo
  echo '  // --- directory Hello classification (Phase 2-12: former main() lambdas classify_directory_hello /'
  echo '  // authorize_directory_session) ---'
  echo '  DirectoryHello ClassifyDirectoryHello(const std::string& token, const sockaddr_in& peer) {'
  echo '    SessionState& clientSession = *this;'
  L 682 701
  echo '  }'
  echo '  bool AuthorizeDirectorySession(const std::string& token, const sockaddr_in& peer) {'
  L 705 705 | sed 's/classify_directory_hello(token, peer)/ClassifyDirectoryHello(token, peer)/'
  echo '  }'
  sed -n "${SE},\$p" "$H"
} > $B/session.hpp
mv $B/session.hpp "$H"
grep -q '^#include <winsock2.h>' "$H" || echo "WARN: host_session.hpp has no winsock2 include (sockaddr_in)"

# ---------------- HostContext::transport becomes a reference (resolved after assembly) ----------------
sed -i 's|^  const VideoTransport transport;$|  const VideoTransport\& transport;  // bound to main()'"'"'s, which resolve_transport() fills after assembly (2-12)|' $S/host_main_loop.hpp
grep -q 'const VideoTransport& transport;' $S/host_main_loop.hpp || { echo "HostContext edit failed"; exit 1; }

# ---------------- new main() ----------------
{
  L 1 "$INC_END"
  echo '#include "host_startup.hpp"'
  L "$((INC_END + 1))" "$USING_RCS"
  cat <<'EOF'
using remote60::native_poc::DxgiWatchdogJoiner;
using remote60::native_poc::startup_process_setup;
using remote60::native_poc::startup_configure_from_env;
using remote60::native_poc::resolve_transport;
using remote60::native_poc::startup_log_config;
using remote60::native_poc::startup_configure_session;
using remote60::native_poc::startup_connect_client;
using remote60::native_poc::startup_configure_control_state;
using remote60::native_poc::startup_start_control_threads;
using remote60::native_poc::startup_init_graphics;
using remote60::native_poc::startup_select_capture_target;
using remote60::native_poc::startup_configure_encode_geometry;
using remote60::native_poc::startup_init_encoder;
using remote60::native_poc::startup_start_dxgi_watchdog;
using remote60::native_poc::startup_create_readback;
using remote60::native_poc::startup_start_capture;
using remote60::native_poc::startup_start_main_loop_watchdog;
using remote60::native_poc::shutdown_host;
EOF
  L "$((USING_RCS + 1))" 399
  L 400 402
  echo '  startup_process_setup();'
  echo
  L 425 427
  L 433 437
  L 465 467
  L 472 474
  L 493 494
  L 507 509
  L 516 519
  L 558 558
  L 567 567
  L 660 661
  L 992 1001
  L 1005 1006
  L 1033 1035
  L 1316 1317
  L 1413 1413
  L 1549 1550
  L 1589 1589
  echo '  // DXGI capture-worker wedge watchdog thread (started by startup_start_dxgi_watchdog once the'
  echo '  // encoder is up); the joiner stops and joins it before res.dxgiCaptureSession is destroyed.'
  L 1598 1598
  echo '  std::thread dxgiWorkerWatchdog;'
  echo '  DxgiWatchdogJoiner dxgiWatchdogJoiner{&dxgiWatchdogStop, &dxgiWorkerWatchdog};'
  L 1692 1692
  L 1707 1708
  L 1750 1765
  echo
  echo "  // Startup, in the monolith's order (host_startup.hpp). Each step is one former block of main();"
  echo '  // the ones that can fail return the exit code main() used to return at that point.'
  echo '  if (const int rc = startup_configure_from_env(host)) return rc;'
  L 559 562
  echo '  if (const int rc = resolve_transport(args, useRaw, useH264, transport)) return rc;'
  echo '  startup_log_config(host);'
  echo '  startup_configure_session(host);'
  echo '  if (const int rc = startup_connect_client(host)) return rc;'
  echo '  startup_configure_control_state(host);'
  echo '  startup_start_control_threads(host, controlServer);'
  echo '  if (const int rc = startup_init_graphics(host)) return rc;'
  echo '  if (const int rc = startup_select_capture_target(host)) return rc;'
  echo '  startup_configure_encode_geometry(host);'
  echo '  if (const int rc = startup_init_encoder(host)) return rc;'
  echo '  startup_start_dxgi_watchdog(host, dxgiWatchdogStop, dxgiWorkerWatchdog);'
  echo '  if (const int rc = startup_create_readback(host)) return rc;'
  echo '  if (const int rc = startup_start_capture(host)) return rc;'
  echo '  startup_start_main_loop_watchdog(host);'
  echo
  L 1890 1906
  echo
  echo '  shutdown_host(host);'
  L 1950 1952
} > $B/main_new.cpp
mv $B/main_new.cpp "$M"

# ---------------- CMake ----------------
sed -i 's|^  src/host_loop_helpers.cpp$|  src/host_loop_helpers.cpp\n  src/host_startup_config.cpp\n  src/host_startup_connect.cpp\n  src/host_startup_control.cpp\n  src/host_startup_graphics.cpp\n  src/host_startup_capture.cpp\n  src/host_shutdown.cpp|' "$C"
grep -q 'src/host_shutdown.cpp' "$C" || { echo "CMake edit failed"; exit 1; }

# ---------------- verification ----------------
# (1) every original main() line lands exactly once: new main body + moved bodies + SessionState additions
awk 'NR>=400 && NR<=1952' "$O" | grep -v '^ *$' | sort > $B/orig_sorted.txt
{
  awk '/^int main\(int argc, char\*\* argv\) \{$/ {on=1; next} on' "$M"
  cat $B/process_setup.txt $B/configure_from_env.txt $B/resolve_transport.txt $B/log_config.txt $B/configure_session.txt \
      $B/connect_client.txt $B/configure_control_state.txt $B/start_control_threads.txt $B/init_graphics.txt \
      $B/select_capture_target.txt $B/configure_encode_geometry.txt $B/init_encoder.txt $B/start_dxgi_watchdog.txt \
      $B/create_readback.txt $B/start_capture.txt $B/start_main_loop_watchdog.txt $B/shutdown_host.txt
  L 676 679; L 682 701
  L 1659 1664
} | grep -v '^ *$' | sort > $B/new_sorted.txt
echo "== multiset diff (orig vs new; expected: only the scaffolding / rename / lambda-wrapper lines):"
diff $B/orig_sorted.txt $B/new_sorted.txt || true
# (2) the moved bodies are exactly what the new TUs carry (modulo alias preludes)
for spec in "startup_process_setup process_setup" "startup_configure_from_env configure_from_env" "resolve_transport resolve_transport" \
            "startup_log_config log_config" "startup_configure_session configure_session" "startup_connect_client connect_client" \
            "startup_configure_control_state configure_control_state" "startup_start_control_threads start_control_threads" \
            "startup_init_graphics init_graphics" "startup_select_capture_target select_capture_target" \
            "startup_configure_encode_geometry configure_encode_geometry" "startup_init_encoder init_encoder" \
            "startup_start_dxgi_watchdog start_dxgi_watchdog" "startup_create_readback create_readback" \
            "startup_start_capture start_capture" "startup_start_main_loop_watchdog start_main_loop_watchdog" \
            "shutdown_host shutdown_host"; do
  set -- $spec
  f=$(grep -l -E "^(int|void) $1\(" $S/host_startup_*.cpp $S/host_shutdown.cpp)
  awk -v fn="$1" '$0 ~ "^(int|void) " fn "\\(" {on=1; next} on && /^}$/ {exit} on && /^  auto& [A-Za-z0-9_]+ = hx\.[A-Za-z0-9_]+;$/ {next} on && /^  return 0;$/ {next} on' "$f" | diff - $B/$2.txt > /dev/null && echo "$1: IDENTICAL" || { echo "$1: DIFFERS"; awk -v fn="$1" '$0 ~ "^(int|void) " fn "\\(" {on=1; next} on && /^}$/ {exit} on && /^  auto& [A-Za-z0-9_]+ = hx\.[A-Za-z0-9_]+;$/ {next} on && /^  return 0;$/ {next} on' "$f" | diff - $B/$2.txt | head -5; }
done
# (3) string literals: the old main.cpp's set == the new main.cpp + new TUs + host_session.hpp additions
grep -o '"\([^"\\]\|\\.\)*"' "$O" | sort -u > $B/lit_before.txt
cat "$M" $S/host_startup_*.cpp $S/host_shutdown.cpp "$H" | grep -o '"\([^"\\]\|\\.\)*"' | sort -u > $B/lit_after.txt
echo "== literals only in OLD main.cpp (must be empty):"; comm -23 $B/lit_before.txt $B/lit_after.txt
echo "== literals new (include names / this file's own comments only):"; comm -13 $B/lit_before.txt $B/lit_after.txt | grep -v -E '\.hpp"$'
wc -l "$M" $S/host_startup.hpp $S/host_startup_*.cpp $S/host_shutdown.cpp "$H"
