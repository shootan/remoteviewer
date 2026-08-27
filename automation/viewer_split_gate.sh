#!/usr/bin/env bash
# Viewer split refactor gates A (+C): build the viewer (and optional extra targets) with the real
# exit code propagated, then optionally run the viewer e2e. Non-zero on any failure, so it is safe
# to chain "gate.sh && git commit".
#
# usage: automation/viewer_split_gate.sh [--e2e] [--target T ...]
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
E2E=0
TARGETS=(remote60_native_video_client_poc)
while [ $# -gt 0 ]; do
  case "$1" in
    --e2e) E2E=1 ;;
    --target) TARGETS+=("$2"); shift ;;
    *) echo "unknown arg $1"; exit 2 ;;
  esac
  shift
done
echo "== gate A: build ${TARGETS[*]}"
cmake --build "$ROOT/build-local" --config Release --target "${TARGETS[@]}" 2>&1 \
  | grep -E "error|warning C|\.exe$|-> " | grep -v "C4715: 'remote60::native_poc::viewer::WndProc'" | head -40
RC=${PIPESTATUS[0]}
if [ "$RC" -ne 0 ]; then echo "GATE A: BUILD FAILED (exit $RC)"; exit 1; fi
echo "GATE A: build ok"
if [ "$E2E" -eq 1 ]; then
  bash "$ROOT/automation/viewer_split_e2e.sh" | grep -E "FAIL|ALL PASS|E2E|logs:"
  RC=${PIPESTATUS[0]}
  if [ "$RC" -ne 0 ]; then echo "GATE C: E2E FAILED"; exit 1; fi
fi
echo "GATES PASSED"
