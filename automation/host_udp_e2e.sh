#!/usr/bin/env bash
# Host-side UDP control e2e on an isolated local host (the gate the host split refactor used):
# GNLinkStream on 127.0.0.1 + remote60_udp_control_e2e_test. Expects ALL PASS from the client
# plus the host-log lines that prove each control request reached the main loop.
#
# Two legs. The second one turns the 1Hz static refresh OFF
# (REMOTE60_NATIVE_STATIC_REFRESH_MS=0), which is the only configuration that actually tests the
# "a keyframe request completes without a new captured frame" contract: with the refresh on, a
# request that the kick failed to drain still gets carried by the next refresh within a second, so
# the leg passes either way and hides the bug. (Ledger H-26b.)
# usage: automation/host_udp_e2e.sh
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/build-local/apps/native_poc/Release"
HOST="$BIN/GNLinkStream.exe"
E2E="$BIN/remote60_udp_control_e2e_test.exe"
[ -x "$HOST" ] || { echo "host exe missing: $HOST"; exit 2; }
[ -x "$E2E" ] || { echo "e2e exe missing: $E2E"; exit 2; }
OUT_ROOT="$ROOT/build-local/_e2e/host_$(date +%Y%m%d_%H%M%S)"
mkdir -p "$OUT_ROOT"
export REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE=1
export REMOTE60_NATIVE_ENCODER_TUNE_MODE=low_latency

# The host-side proof that each control request actually reached the main loop and was acted on --
# every one of them is a MainLoopMailbox round trip. Without this the suite passed whether or not
# the loop ever saw them.
# "<grep -E pattern>@@<label>". The separator is @@ and not | because the patterns themselves
# use | for alternation.
HOST_LOG_CHECKS=(
  "runtime-config-applied seq=@@runtime tune applied"
  "monitor-select applied id=@@monitor select applied"
  "desktop-backend-(applied|stored) seq=@@desktop backend request applied or stored"
  "keyframe-request-consumed reason=@@keyframe request consumed"
)

run_leg() {
  local name="$1" port="$2" ctlPort="$3" refreshMs="$4"
  local out="$OUT_ROOT/$name"
  mkdir -p "$out"
  echo "--- leg: $name (staticRefreshMs=$refreshMs) ---"
  REMOTE60_NATIVE_STATIC_REFRESH_MS="$refreshMs" \
    "$HOST" --transport udp --codec h264 --bind-port "$port" --bind-address 127.0.0.1 \
            --control-port "$ctlPort" --seconds 90 > "$out/host.log" 2>&1 &
  local hostPid=$!
  sleep 2
  if ! kill -0 "$hostPid" 2>/dev/null; then
    echo "host exited early:"; tail -5 "$out/host.log"; return 1
  fi
  "$E2E" 127.0.0.1 "$port" > "$out/e2e.log" 2>&1
  local rc=$?
  kill "$hostPid" 2>/dev/null || true
  sleep 1
  kill -0 "$hostPid" 2>/dev/null && MSYS_NO_PATHCONV=1 taskkill /F /PID "$hostPid" >/dev/null 2>&1 || true
  wait "$hostPid" 2>/dev/null || true
  tail -3 "$out/e2e.log"

  local ok=1
  local entry pattern label
  for entry in "${HOST_LOG_CHECKS[@]}"; do
    pattern="${entry%%@@*}"
    label="${entry#*@@}"
    if grep -qE "$pattern" "$out/host.log"; then
      echo "PASS  host[$name]: $label"
    else
      echo "FAIL  host[$name]: $label"
      ok=0
    fi
  done
  if [ "$rc" -eq 0 ] && grep -q "ALL PASS" "$out/e2e.log" && [ "$ok" -eq 1 ]; then
    return 0
  fi
  echo "leg $name FAILED (rc=$rc hostLog=$ok)"
  return 1
}

FAILED=0
run_leg default 44100 44101 1000 || FAILED=1
run_leg refresh-off 44200 44201 0 || FAILED=1

echo "logs: $OUT_ROOT"
if [ "$FAILED" -eq 0 ]; then echo "HOST UDP E2E: ALL PASS"; exit 0; fi
echo "HOST UDP E2E: FAIL"; exit 1
