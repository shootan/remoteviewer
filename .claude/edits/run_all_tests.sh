#!/usr/bin/env bash
# Runs every *_test.exe in the Release tree, one line per test: name, exit code, last PASS/FAIL line.
cd /d/remote/remote || exit 2
out=.claude/${1:-test_sweep_default}.txt
: > "$out"
for exe in build-local/apps/native_poc/Release/*_test.exe; do
  name=$(basename "$exe" .exe)
  start=$(date +%s)
  log=$(timeout 300 "$exe" 2>&1)
  code=$?
  if [ "$code" != "0" ]; then printf "%s
" "$log" > ".claude/${1:-test_sweep_default}_fail_${name}.log"; fi
  dur=$(( $(date +%s) - start ))
  last=$(printf '%s\n' "$log" | grep -E "PASS|FAIL|passed|failed" | tail -1 | cut -c1-110)
  printf '%-52s exit=%-3s %4ss  %s\n' "$name" "$code" "$dur" "$last" >> "$out"
done
cat "$out"
echo "--- summary: $(grep -c 'exit=0 ' "$out") ok / $(grep -vc 'exit=0 ' "$out") not ok"
