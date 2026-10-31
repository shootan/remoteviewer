#!/usr/bin/env bash
# Host split refactor Phase 3.5a: split host_main_loop.cpp (one 3.7k-line TU) into one file per
# stage plus one for the helpers, verbatim. Each new file repeats the include preamble and wraps
# its functions in the same namespace. Run once from the repo root.
set -euo pipefail
S=apps/native_poc/src
P=$S/host_main_loop.cpp
C=apps/native_poc/CMakeLists.txt
NS=$(grep -n -m1 '^namespace remote60::native_poc {$' "$P" | cut -d: -f1)
sed -n "1,$((NS - 1))p" "$P" | grep -v -E '^// (See host_main_loop.hpp|helper lambdas of|each stage body|results, per-iteration|The alias lines)' > /tmp/loop_preamble.txt

# function ranges: "^(bool|void|Flow) name(" .. first "^}$" after it
fn_range() { local s e; s=$(grep -n -m1 -E "^(bool|void|Flow) $1\(" "$P" | cut -d: -f1); e=$(awk -v s="$s" 'NR>s && /^}$/ {print NR; exit}' "$P"); echo "$s $e"; }
emit() {  # emit FILE TITLE FN...
  local out=$1 title=$2; shift 2
  {
    echo "// $title"
    echo '//'
    echo '// Host split refactor Phase 3.5: moved verbatim out of host_main_loop.cpp so each stage reads on its'
    echo '// own; see host_main_loop.hpp for the loop, HostContext / TickContext and Flow.'
    echo
    cat /tmp/loop_preamble.txt
    echo 'namespace remote60::native_poc {'
    echo
    for fn in "$@"; do
      set -- $(fn_range "$fn")
      sed -n "$1,$2p" "$P"
      echo
    done
    echo '}  // namespace remote60::native_poc'
  } > "$out"
}
emit $S/host_loop_helpers.cpp        'Main-loop helpers: capture restart, cursor forwarder, TCP data reconnect, window selection.' restart_capture_session pump_cursor_forward reconnect_tcp_data_session apply_selected_window_capture
emit $S/host_stage_time_limit.cpp    'Stage 1: seconds limit and sender barrier recovery.' stage_time_limit
emit $S/host_stage_backend.cpp       'Stage 2: desktop backend request, demotion recovery and climb-back promotion.' stage_backend
emit $S/host_stage_stream_active.cpp 'Stage 3: stream active/idle transitions (idle detach, reattach).' stage_stream_active
emit $S/host_stage_runtime_tune.cpp  'Stage 4: runtime encoder configuration requests from the viewer.' stage_runtime_tune
emit $S/host_stage_selection.cpp     'Stage 5: monitor / capture-mode / window selection and window rebind.' stage_selection
emit $S/host_stage_geometry.cpp      'Stage 6: WGC content-size settle and capture size change.' stage_geometry
emit $S/host_stage_watchdogs.cpp     'Stage 7: callback-stall and frozen-ring capture watchdogs.' stage_watchdogs
emit $S/host_stage_pace.cpp          'Stage 8: raw-mode tick pacing.' stage_pace
emit $S/host_stage_pop_frame.cpp     'Stage 9: trailing kick, static refresh and frame pop.' stage_pop_frame
emit $S/host_stage_gate_static.cpp   'Stage 10: static-frame gating and stale-frame guards.' stage_gate_static
emit $S/host_stage_encode_send.cpp   'Stage 11: raw send / H.264 encode and sender enqueue.' stage_encode_send
emit $S/host_stage_stats.cpp         'Stage 12: 1s stats tick, readback drain watchdog, ABR / M9 decisions.' stage_stats

# verification: every function line of the old TU appears exactly once across the new files
cat $S/host_loop_helpers.cpp $S/host_stage_*.cpp | awk '/^namespace remote60::native_poc \{$/ {on=1; next} /^\}  \/\/ namespace remote60::native_poc$/ {on=0} on' | grep -v '^$' | sort > /tmp/split_body.txt
awk '/^namespace remote60::native_poc \{$/ {on=1; next} /^\}  \/\/ namespace remote60::native_poc$/ {on=0} on' "$P" | grep -v '^$' | grep -v -E '^// -{10,}$|^// (Helpers the loop calls|The twelve stages of one main-loop tick)' | sort > /tmp/orig_body.txt
echo "orig=$(wc -l < /tmp/orig_body.txt) split=$(wc -l < /tmp/split_body.txt)"
diff /tmp/orig_body.txt /tmp/split_body.txt && echo "SPLIT: IDENTICAL"

git rm -q "$P"
sed -i 's|^  src/host_main_loop.cpp$|  src/host_loop_helpers.cpp\n  src/host_stage_time_limit.cpp\n  src/host_stage_backend.cpp\n  src/host_stage_stream_active.cpp\n  src/host_stage_runtime_tune.cpp\n  src/host_stage_selection.cpp\n  src/host_stage_geometry.cpp\n  src/host_stage_watchdogs.cpp\n  src/host_stage_pace.cpp\n  src/host_stage_pop_frame.cpp\n  src/host_stage_gate_static.cpp\n  src/host_stage_encode_send.cpp\n  src/host_stage_stats.cpp|' "$C"
wc -l $S/host_loop_helpers.cpp $S/host_stage_*.cpp | sort -rn | head -14
