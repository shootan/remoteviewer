#!/usr/bin/env bash
# Host split refactor Phase 2-12b: prune native_video_host_main.cpp's anonymous namespace down to what
# main() still names. After 2-12 the file is includes + ~230 using-declarations + ~65 lines of stale
# "X moved to Y" notes + the RUN_STAGE macro + a 123-line main(); only ~40 of the usings are used.
# Keeps: every `using remote60::...::Name;` whose Name appears in a code line (comments stripped) at or
# after `}  // namespace`; the includes (untouched); the RUN_STAGE macro; main(). Drops: the rest of the
# usings, the two `using namespace winrt::...` lines (main() spells WinRT names out), the json_profile
# alias, the REMOTE60_NATIVE_ENCODED_EXPERIMENT macro (now read only in host_startup_config.cpp) and
# the stale notes (replaced by one line). Run once from the repo root.
set -euo pipefail
S=apps/native_poc/src
M=$S/native_video_host_main.cpp
git diff --quiet HEAD -- "$M" || { echo "main.cpp not clean"; exit 1; }
cp "$M" /tmp/main_212b_before.cpp
NS=$(grep -n -m1 '^namespace {$' "$M" | cut -d: -f1)
NE=$(grep -n -m1 '^}  // namespace$' "$M" | cut -d: -f1)
[ -n "$NS" ] && [ -n "$NE" ] && [ "$NS" -lt "$NE" ] || { echo "anonymous namespace not found"; exit 1; }
sed -n "$NE,\$p" "$M" | grep -v -E '^ *//' | sed -E 's://.*$::' > /tmp/main_212b_code.txt
{
  sed -n "1,${NS}p" "$M"
  echo '// Only what main() itself names. Everything else that used to live in this file is in the host_*'
  echo '// modules (see docs/호스트_분할_리팩터_계획.md for the map); the startup / shutdown functions are in'
  echo '// host_startup.hpp, the loop stages in host_main_loop.hpp.'
  sed -n "$((NS + 1)),$((NE - 1))p" "$M" | grep -E '^using remote60::(native_poc|host)::[A-Za-z_0-9]+;$' | while read -r line; do
    name=$(echo "$line" | sed -E 's/^using [a-z0-9_:]+::([A-Za-z_0-9]+);$/\1/')
    grep -q -E "(^|[^A-Za-z0-9_:])$name\b" /tmp/main_212b_code.txt && echo "$line" || true
  done
  sed -n "${NE},\$p" "$M"
} > /tmp/main_212b_new.cpp
mv /tmp/main_212b_new.cpp "$M"
echo "usings kept: $(grep -c '^using remote60::' "$M") (was $(grep -c '^using remote60::' /tmp/main_212b_before.cpp))"
# main() itself must be byte-identical
diff <(awk '/^int main\(/ {on=1} on' /tmp/main_212b_before.cpp) <(awk '/^int main\(/ {on=1} on' "$M") && echo "main(): IDENTICAL"
wc -l "$M"
