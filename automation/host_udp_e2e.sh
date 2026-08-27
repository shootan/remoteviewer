#!/usr/bin/env bash
# Host-side UDP control e2e on an isolated local host (the gate the host split refactor used):
# GNLinkStream on 127.0.0.1:44100/44101 + remote60_udp_control_e2e_test. Expects the 13-check ALL PASS.
# usage: automation/host_udp_e2e.sh
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/build-local/apps/native_poc/Release"
HOST="$BIN/GNLinkStream.exe"
E2E="$BIN/remote60_udp_control_e2e_test.exe"
[ -x "$HOST" ] || { echo "host exe missing: $HOST"; exit 2; }
[ -x "$E2E" ] || { echo "e2e exe missing: $E2E"; exit 2; }
OUT="$ROOT/build-local/_e2e/host_$(date +%Y%m%d_%H%M%S)"
mkdir -p "$OUT"
export REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE=1
export REMOTE60_NATIVE_ENCODER_TUNE_MODE=low_latency
"$HOST" --transport udp --codec h264 --bind-port 44100 --bind-address 127.0.0.1 --control-port 44101 --seconds 90 > "$OUT/host.log" 2>&1 &
HOST_PID=$!
sleep 2
kill -0 "$HOST_PID" 2>/dev/null || { echo "host exited early:"; tail -5 "$OUT/host.log"; exit 1; }
"$E2E" 127.0.0.1 44100 > "$OUT/e2e.log" 2>&1
RC=$?
kill "$HOST_PID" 2>/dev/null || true
sleep 1
kill -0 "$HOST_PID" 2>/dev/null && MSYS_NO_PATHCONV=1 taskkill /F /PID "$HOST_PID" >/dev/null 2>&1 || true
wait "$HOST_PID" 2>/dev/null || true
tail -3 "$OUT/e2e.log"
echo "logs: $OUT"
if [ "$RC" -eq 0 ] && grep -q "ALL PASS" "$OUT/e2e.log"; then echo "HOST UDP E2E: ALL PASS"; exit 0; fi
echo "HOST UDP E2E: FAIL (rc=$RC)"; exit 1
