#!/usr/bin/env bash
# Viewer split refactor, gate C: drive the real GNLinkViewer.exe against an isolated local host.
# The host-side UDP e2e (remote60_udp_control_e2e_test) never touches the viewer code, so this
# is the only automated check that the viewer still connects, decodes, presents and exits.
#
# usage: automation/viewer_split_e2e.sh [--skip-raw] [--stream-seconds N]
# Runs three sessions on 127.0.0.1:44100/44101 (env REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE=1,
# REMOTE60_NATIVE_ENCODER_TUNE_MODE=low_latency):
#   C-1 stream view, udp/h264, input channel on  -> connects, decodes, prints 1s stats, exits 0
#   C-2 picker view, udp/h264                     -> stream-state active=0 sent, window list answered
#   C-3 tcp/raw regression                        -> raw frames flow, exits 0
# Logs: build-local/_e2e/<run>/{host,viewer}.log. Exit 0 only when every check passes.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/build-local/apps/native_poc/Release"
HOST="$BIN/GNLinkStream.exe"
VIEWER="$BIN/GNLinkViewer.exe"
PORT=44100
CTRL=44101
STREAM_SECONDS=10
PICKER_SECONDS=6
RAW_SECONDS=5
SKIP_RAW=0
while [ $# -gt 0 ]; do
  case "$1" in
    --skip-raw) SKIP_RAW=1 ;;
    --stream-seconds) STREAM_SECONDS="$2"; shift ;;
    *) echo "unknown arg $1"; exit 2 ;;
  esac
  shift
done
[ -x "$HOST" ] || { echo "host exe missing: $HOST"; exit 2; }
[ -x "$VIEWER" ] || { echo "viewer exe missing: $VIEWER"; exit 2; }
OUT="$ROOT/build-local/_e2e/$(date +%Y%m%d_%H%M%S)"
mkdir -p "$OUT"
export REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE=1
export REMOTE60_NATIVE_ENCODER_TUNE_MODE=low_latency
unset REMOTE60_NATIVE_START_STREAM_VIEW

FAILS=0
fail() { echo "  FAIL: $*"; FAILS=$((FAILS + 1)); }
pass() { echo "  ok:   $*"; }

HOST_PID=""
start_host() {  # $1 transport, $2 codec, $3 seconds, $4 logname
  "$HOST" --transport "$1" --codec "$2" --bind-port $PORT --bind-address 127.0.0.1 \
    --control-port $CTRL --seconds "$3" > "$OUT/$4" 2>&1 &
  HOST_PID=$!
  sleep 2
  if ! kill -0 "$HOST_PID" 2>/dev/null; then
    echo "host exited early:"; tail -5 "$OUT/$4"; return 1
  fi
  return 0
}
stop_host() {
  if [ -n "$HOST_PID" ] && kill -0 "$HOST_PID" 2>/dev/null; then
    kill "$HOST_PID" 2>/dev/null || true
    sleep 1
    kill -0 "$HOST_PID" 2>/dev/null && MSYS_NO_PATHCONV=1 taskkill /F /PID "$HOST_PID" >/dev/null 2>&1 || true
  fi
  wait "$HOST_PID" 2>/dev/null || true
  HOST_PID=""
}
trap stop_host EXIT

run_viewer() {  # $1 logname, rest = args ; sets VIEWER_RC
  local log="$OUT/$1"; shift
  "$VIEWER" "$@" > "$log" 2>&1
  VIEWER_RC=$?
}

# ---------------- C-1 stream view ----------------
echo "== C-1 stream view (udp/h264, ${STREAM_SECONDS}s) =="
if start_host udp h264 $((STREAM_SECONDS + 15)) host_c1.log; then
  run_viewer viewer_c1.log --host 127.0.0.1 --port $PORT --control-port $CTRL --transport udp \
    --codec h264 --enable-input-channel --initial-view stream --fps-hint 60 --seconds "$STREAM_SECONDS"
  L="$OUT/viewer_c1.log"
  [ "$VIEWER_RC" -eq 0 ] && pass "exit 0" || fail "exit code $VIEWER_RC"
  grep -q "control connected transport=tcp" "$L" && pass "control connected" || fail "no 'control connected'"
  STATS=$(grep -c "recvFrames=" "$L")
  [ "$STATS" -ge 5 ] && pass "stats lines $STATS" || fail "stats lines $STATS < 5"
  DEC=$(grep -o "decodedFrames=[0-9]*" "$L" | cut -d= -f2 | awk '{s+=$1} END {print s+0}')
  [ "$DEC" -ge 30 ] && pass "decodedFrames total $DEC" || fail "decodedFrames total $DEC < 30"
  CONG=$(grep -c "\[congestion\] state=congested" "$L")
  [ "$CONG" -eq 0 ] && pass "no congested transitions" || fail "$CONG congested transitions"
  grep -q "\[native-video-client\] done" "$L" && pass "clean 'done'" || fail "no 'done' line"
  grep -q "\[present\] seq=" "$L" && pass "presented frames" || fail "no present lines"
else
  fail "C-1 host start"
fi
stop_host

# ---------------- C-2 picker view ----------------
echo "== C-2 picker view (udp/h264, ${PICKER_SECONDS}s) =="
if start_host udp h264 $((PICKER_SECONDS + 15)) host_c2.log; then
  run_viewer viewer_c2.log --host 127.0.0.1 --port $PORT --control-port $CTRL --transport udp \
    --codec h264 --enable-input-channel --initial-view targets --fps-hint 60 --seconds "$PICKER_SECONDS"
  L="$OUT/viewer_c2.log"
  [ "$VIEWER_RC" -eq 0 ] && pass "exit 0" || fail "exit code $VIEWER_RC"
  grep -q "control connected transport=tcp" "$L" && pass "control connected" || fail "no 'control connected'"
  grep -q "\[control\] stream-state seq=.* active=0" "$L" && pass "stream-state active=0 sent" || fail "no stream-state active=0"
  grep -q "\[control\] window-list seq=" "$L" && pass "window list answered" || fail "no window-list reply"
  grep -q "\[native-video-client\] done" "$L" && pass "clean 'done'" || fail "no 'done' line"
else
  fail "C-2 host start"
fi
stop_host

# ---------------- C-3 tcp raw ----------------
if [ "$SKIP_RAW" -eq 0 ]; then
  echo "== C-3 tcp/raw (${RAW_SECONDS}s) =="
  if start_host tcp raw $((RAW_SECONDS + 15)) host_c3.log; then
    run_viewer viewer_c3.log --host 127.0.0.1 --port $PORT --control-port $CTRL --transport tcp \
      --codec raw --initial-view stream --seconds "$RAW_SECONDS"
    L="$OUT/viewer_c3.log"
    [ "$VIEWER_RC" -eq 0 ] && pass "exit 0" || fail "exit code $VIEWER_RC"
    DEC=$(grep -o "decodedFrames=[0-9]*" "$L" | cut -d= -f2 | awk '{s+=$1} END {print s+0}')
    [ "$DEC" -gt 0 ] && pass "raw decodedFrames total $DEC" || fail "no raw frames decoded"
    grep -q "\[native-video-client\] done" "$L" && pass "clean 'done'" || fail "no 'done' line"
  else
    fail "C-3 host start"
  fi
  stop_host
fi

echo "== logs: $OUT"
if [ "$FAILS" -eq 0 ]; then echo "VIEWER E2E: ALL PASS"; exit 0; fi
echo "VIEWER E2E: $FAILS FAIL(S)"; exit 1
