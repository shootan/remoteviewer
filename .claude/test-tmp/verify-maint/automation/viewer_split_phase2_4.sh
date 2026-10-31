#!/usr/bin/env bash
# Viewer split refactor Phase 2-4: the control thread lambda of main() becomes class ControlClient
# (viewer_control_client.hpp/.cpp). Commit 1: Run() and fetch_one_thumbnail() with verbatim bodies
# (re-indented); main() keeps `controlThread = std::thread([&]() { control.Run(); })`. Commit 2: the
# response switch's Pong / WindowList / WindowSelected / InputAck cases become handle_* members with
# verbatim bodies (the `response.<field>` reads become the parameter). Run from the repo root.
set -euo pipefail
S=apps/native_poc/src
M=$S/native_video_client_main.cpp
C=apps/native_poc/CMakeLists.txt
H=$S/viewer_control_client.hpp
CPP=$S/viewer_control_client.cpp
git diff --quiet HEAD || { echo "tree not clean"; exit 1; }
T=/tmp/v24; rm -rf $T; mkdir -p $T
tr -d '\r' < "$M" > $T/main.txt
O=$T/main.txt
lineof() { local n; n=$(grep -n -- "$1" $O | cut -d: -f1 | head -1); [ -n "$n" ] || { echo "anchor not found: $1"; exit 1; }; echo "$n"; }

A=$(lineof '^      controlThread = std::thread(\[&\]() {$')
Z=$(awk -v s="$A" 'NR>s && /^      \}\);$/ {print NR; exit}' $O)
FA=$(lineof '^        auto fetch_one_thumbnail = \[&\](remote60::native_poc::ControlLink& link) -> int {$')
FZ=$(awk -v s="$FA" 'NR>s && /^        \};$/ {print NR; exit}' $O)
echo "control lambda $A..$Z, fetch_one_thumbnail $FA..$FZ"
# the lambda's leading comment (5 lines above the fetch lambda) documents fetch_one_thumbnail
[ "$(sed -n "$((A + 1))p" $O)" = '        // Fetch one queued preview over the control socket. Runs between scheduler' ] || { echo "comment anchor"; exit 1; }

cat > $T/hpp.txt <<'EOF'
#pragma once

// The viewer's control thread: drives the control scheduler over the TCP control socket or the
// UDP control tunnel and applies the host's replies.
//
// Role:    Run() is the former controlThread lambda of main(): build the ControlLink, then loop
//          NextAction -> execute_control_action -> apply the reply (pong: host capture meta, RTT and
//          clock telemetry; window list; monitor list; window selected; input ack), fetching one
//          picker thumbnail per idle turn; on link failure mark control disconnected and clear the
//          selection.
// Thread:  control only. Owns the ControlLink and the scheduler (gControl.scheduler); writes
//          gControl.connected / host capture meta / reportedSecure, gPicker.windowPanel and thumbs;
//          reads the request states the UI/recv threads fill.
// Input:   gControl request states, the host's replies.
// Output:  control messages on the wire; picker/thumbnail state; log lines.
// Callers: main() (controlThread = std::thread([&]{ control.Run(); })).
//
// Bodies are the lambda bodies of native_video_client_main.cpp, verbatim (viewer split refactor
// Phase 2-4); the captured state (args, startInPicker, controlSock) are members with the same names.

#include "viewer_args.hpp"
#include "viewer_common.hpp"
#include "viewer_globals.hpp"

namespace remote60::native_poc::viewer {

class ControlClient {
 public:
  ControlClient(const Args& args, bool startInPicker) : args(args), startInPicker(startInPicker) {}
  // The TCP control socket when the host was dialled directly (INVALID_SOCKET on the UDP tunnel).
  // Set by main() before the thread starts; main() still owns and closes it at shutdown.
  SOCKET controlSock = INVALID_SOCKET;
  // The thread body (formerly the controlThread lambda).
  void Run();

 private:
  const Args& args;
  const bool startInPicker;

  // Fetch one queued preview over the control socket. Runs between scheduler
  // actions on the same strict request/response pipeline, one card per call so a
  // large backlog cannot starve input events. Only invoked when the host advertised
  // the capability, because an older host would drain the request and never reply.
  // Returns: 1 fetched, 0 nothing to do, -1 socket failure (stream desynced).
  int fetch_one_thumbnail(remote60::native_poc::ControlLink& link);
};

}  // namespace remote60::native_poc::viewer
EOF
cat > $T/cpp_head.txt <<'EOF'
// See viewer_control_client.hpp. Bodies are the controlThread lambda of native_video_client_main.cpp,
// verbatim (viewer split refactor Phase 2-4).

#include "viewer_control_client.hpp"

#include <iostream>
#include <memory>
#include <sstream>

#include "viewer_env_util.hpp"
#include "viewer_log.hpp"
#include "viewer_picker.hpp"

namespace remote60::native_poc::viewer {

EOF
{
  cat $T/cpp_head.txt
  echo "int ControlClient::fetch_one_thumbnail(remote60::native_poc::ControlLink& link) {"
  sed -n "$((FA + 1)),$((FZ - 1))p" $O | sed 's/^        //'
  echo "}"
  echo
  echo "void ControlClient::Run() {"
  sed -n "$((FZ + 1)),$((Z - 1))p" $O | sed 's/^        //'
  echo "}"
  echo
  echo "}  // namespace remote60::native_poc::viewer"
} > $T/cpp.txt
sed 's/$/\r/' $T/hpp.txt > "$H"
sed 's/$/\r/' $T/cpp.txt > "$CPP"
RANGES="$((FA + 1))-$((FZ - 1)),$((FZ + 1))-$((Z - 1))"

# ---- main.cpp: declare the client next to the thread, set the socket, run it on the thread ----
{
  sed -n "1,$((A - 1))p" $O
  echo "      control.controlSock = controlSock;"
  echo "      controlThread = std::thread([&]() { control.Run(); });"
  sed -n "$((Z + 1)),\$p" $O
} > $T/main_new.txt
perl -0pi -e 's/^  std::thread controlThread;\n/  std::thread controlThread;\n  ControlClient control(args, startInPicker);\n/m or die "controlThread decl"' $T/main_new.txt
sed 's/$/\r/' $T/main_new.txt > "$M"
perl -0pi -e 's/((?:#include "viewer_[a-z_0-9]+\.hpp"\r\n)+)/$1#include "viewer_control_client.hpp"\r\n/ or die "include anchor"' "$M"
perl -0pi -e 's/(  src\/viewer_globals\.cpp\r?\n)/$1  src\/viewer_control_client.cpp\r\n/ or die "cmake anchor"' "$C"

echo "=== gate B ==="
perl automation/viewer_split_check.pl --ignore-indent HEAD "$RANGES" "$CPP"
bash automation/viewer_split_gate.sh --e2e
git add "$M" "$C" "$H" "$CPP"
git commit -q -F - <<EOF
refactor(viewer): Phase 2-4 — the control thread lambda becomes class ControlClient (verbatim bodies)

main()'s 280-line controlThread lambda is now ControlClient::Run() in viewer_control_client.cpp,
with fetch_one_thumbnail() a member; bodies verbatim (source ranges $RANGES of the previous
revision, re-indented). The captured state (args, startInPicker, controlSock) are members with the
same names; main() sets controlSock, constructs the client next to the thread and runs it on the
same std::thread.
Gates: move check PASS, build exit 0, viewer e2e ALL PASS.

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
EOF
git log -1 --format='%h %s'

# ================= commit 2: the reply switch -> handle_* members =================
tr -d '\r' < "$CPP" > $T/cc.txt
perl -0pi -e '
  # Pong: body between "const auto& pong = response.pong;" and its "break;"
  s/(        case TcpControlResponseKind::Pong: \{\n)          const auto& pong = response\.pong;\n(.*?)\n          break;\n        \}\n/$1          handle_pong(action, response.pong);\n          break;\n        }\n/s or die "pong case";
  our $pong = $2;
  s/(        case TcpControlResponseKind::WindowList: \{\n)(.*?)\n          break;\n        \}\n/$1          handle_window_list(response.windowList);\n          break;\n        }\n/s or die "windowlist case";
  our $wl = $2;
  s/(        case TcpControlResponseKind::WindowSelected:\n)(.*?)\n          break;\n/$1          handle_window_selected(response.windowSelected);\n          break;\n/s or die "windowselected case";
  our $ws = $2;
  s/(        case TcpControlResponseKind::InputAck: \{\n)(.*?)\n          break;\n        \}\n/$1          handle_input_ack(response.inputAck);\n          break;\n        }\n/s or die "inputack case";
  our $ia = $2;
  for ($wl) { s/response\.windowList/windowList/g; }
  for ($ws) { s/response\.windowSelected/windowSelected/g; }
  for ($ia) { s/response\.inputAck/inputAck/g; }
  sub deindent { my ($s, $n) = @_; $s =~ s/^ {$n}//mg; return $s; }
  my $members = "void ControlClient::handle_pong(const ControlOutboundAction& action, const ControlPongMessage& pong) {\n" . deindent($pong, 8) . "\n}\n\n"
              . "void ControlClient::handle_window_list(const ControlWindowListMessage& windowList) {\n" . deindent($wl, 8) . "\n}\n\n"
              . "void ControlClient::handle_window_selected(const ControlWindowSelectedMessage& windowSelected) {\n" . deindent($ws, 8) . "\n}\n\n"
              . "void ControlClient::handle_input_ack(const ControlInputAckMessage& inputAck) {\n" . deindent($ia, 8) . "\n}\n\n";
  s/(\nvoid ControlClient::Run\(\) \{\n)/\n$members$1/ or die "insert members";
' $T/cc.txt
sed 's/$/\r/' $T/cc.txt > "$CPP"
perl -0pi -e 's/(  int fetch_one_thumbnail\(remote60::native_poc::ControlLink& link\);\r\n)/$1  \/\/ the reply switch of Run(), one member per reply kind (verbatim case bodies)\r\n  void handle_pong(const ControlOutboundAction& action, const ControlPongMessage& pong);\r\n  void handle_window_list(const ControlWindowListMessage& windowList);\r\n  void handle_window_selected(const ControlWindowSelectedMessage& windowSelected);\r\n  void handle_input_ack(const ControlInputAckMessage& inputAck);\r\n/ or die "hpp decls"' "$H"
grep -c "handle_pong\|handle_window_list\|handle_window_selected\|handle_input_ack" "$CPP" | grep -qx 8 || { echo "expected 8 handler mentions"; grep -n "handle_" "$CPP"; exit 1; }
bash automation/viewer_split_gate.sh --e2e
git add "$H" "$CPP"
git commit -q -F - <<'EOF'
refactor(viewer): Phase 2-4b — ControlClient reply switch split into handle_pong / window_list / window_selected / input_ack

The four case bodies of Run()'s reply switch become members with verbatim bodies; the only
textual change is `response.<field>` becoming the parameter. Monitor list stays a one-liner.
Gates: build exit 0, viewer e2e ALL PASS.

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
EOF
git log -1 --format='%h %s'
echo "main.cpp now $(wc -l < "$M") lines; control_client.cpp $(wc -l < "$CPP")"
