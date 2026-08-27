#!/usr/bin/perl
# Viewer split refactor Phase 2-10 / 2-11 / 3: main() becomes a ViewerContext plus a function list.
#   viewer_context.hpp   -- ViewerContext: the main() locals the startup functions and threads share
#   viewer_startup.hpp/.cpp -- apply_latency_priority, apply_dpi_awareness, load_config,
#                              validate_codec_transport, apply_initial_state, create_window_and_toolbar,
#                              init_decoder, open_media_socket, connect_media_socket,
#                              attach_control_tunnel_and_log, connect_control, start_receiver,
#                              run_message_pump (each a verbatim block of main(); failures return the
#                              original exit code)
#   viewer_shutdown.cpp  -- shutdown_viewer (the teardown block, verbatim order)
# The blocks are cut by exact anchors from native_video_client_main.cpp; inside them the former locals
# read ctx.<name> (rename_outside_strings.pl --code-only: strings and comments untouched). A miss
# dies before any file is written.
use strict;
use warnings;

my $S = 'apps/native_poc/src';
my $T = '/tmp/v3'; mkdir $T unless -d $T;
sub slurp { my ($f) = @_; open(my $h, '<:raw', $f) or die "open $f: $!"; local $/; my $s = <$h>; close $h; $s =~ s/\r\n/\n/g; return $s; }
sub spew  { my ($f, $s) = @_; $s =~ s/\r?\n/\r\n/g; open(my $h, '>:raw', $f) or die "write $f: $!"; print $h $s; close $h; }
sub must  { my ($ok, $what) = @_; die "anchor failed: $what\n" unless $ok; print "ok: $what\n"; }

my $M = slurp("$S/native_video_client_main.cpp");
my ($pre, $body) = $M =~ /^(.*?\nint main\(int argc, char\*\* argv\) \{\n)(.*)\z/s or die "main anchor";

# ---- cut the blocks (exact first/last lines), in order ----
sub block {  # name, first-line regex, last-line regex -> the text between (inclusive); removed from $body
  my ($name, $first, $last) = @_;
  $body =~ s/(^$first\n.*?^$last\n)//ms or die "block $name";
  print "ok: block $name (" . ($1 =~ tr/\n//) . " lines)\n";
  return $1;
}
my %b;
$b{priority}  = block('priority', qr/  \/\/ Decoder\/present deadlines should not lose their timeslice to ordinary background work\./, qr/  \}/);
$b{dpi}       = block('dpi', qr/  \/\/ Without this the OS bitmap-stretches the whole window on a scaled display, which blurs/, qr/  \}/);
$b{config}    = block('config', qr/  const Args args = parse_args\(argc, argv\);/, qr/  gControl\.keyframeRequests\.Reset\(\);/);
$b{validate}  = block('validate', qr/  dec\.useRaw = \(args\.codec == "raw"\);/, qr/    return 13;\n  \}/);
$b{initial}   = block('initial', qr/  gSession\.overlayConfig\.host = args\.host;/, qr/  gInput\.activeTouchDown\.store\(false, std::memory_order_relaxed\);/);
$b{winsock}   = block('winsock', qr/  remote60::native_poc::WinsockScope ws;/, qr/    return 1;\n  \}/);
$b{window}    = block('window', qr/  if \(!create_window\(\)\) \{/, qr/    push_session_toolbar_state\(\);\n  \}/);
$b{decoder}   = block('decoder', qr/  gate\.waitForKeyFrame = dec\.useH264;/, qr/      \}\n    \}\n  \}/);
$b{open}      = block('open', qr/  \/\/ Reaching the host through the directory replaces the address entirely: the socket comes back/, qr/    return 3;\n  \}/);
$b{connect}   = block('connect', qr/  if \(dec\.transport == VideoTransport::Tcp\) \{/, qr/      return 6;\n    \}\n  \}/);
$b{tunnel}    = block('tunnel', qr/  \/\/ No second port to dial means the directory path: control tunnels through the socket the/, qr/            << " sndbuf=" << effectiveSendBuf << " bytes\\n";/);
$b{control}   = block('control', qr/  SOCKET controlSock = INVALID_SOCKET;/, qr/      std::cout << "\[native-video-client\] control unavailable port=" << args\.controlPort << "\\n";\n    \}\n  \}/);
$b{receiver}  = block('receiver', qr/  const uint64_t startUs = qpc_now_us\(\);/, qr/  std::thread recvThread\(\[&\]\(\) \{ receiver\.Run\(\); \}\);/);
$b{pump}      = block('pump', qr/  MSG msg\{\};/, qr/    if \(!hadMessage\) \{\n      Sleep\(5\);\n    \}\n  \}/);
$b{shutdown}  = block('shutdown', qr/  gSession\.running = false;\n  gSession\.inputEnabled = false;/, qr/    if \(dec\.mfStarted\) MFShutdown\(\);\n  \}/);
# what remains of main() must be only the stream setup lines and the done/return
(my $rest = $body) =~ s/\n\n+/\n/g;
must($rest eq "  std::cout.setf(std::ios::unitbuf);\n  std::cerr.setf(std::ios::unitbuf);\n  std::cout << \"[native-video-client] done\\n\";\n  return 0;\n}\n", "main() fully consumed (leftover: $rest)");

# ---- per-block declaration rewrites (locals -> context members) ----
must($b{config} =~ s/  const Args args = parse_args\(argc, argv\);\n  DecoderState dec;\n/  ctx.args = parse_args(argc, argv);\n/, 'config: args/dec');
must($b{config} =~ s/  FrameGateState gate;\n//, 'config: gate');
must($b{config} =~ s/  const uint32_t udpSimDropPm = env_u32_clamped\(/  ctx.udpSimDropPm = env_u32_clamped(/, 'config: sim pm');
must($b{config} =~ s/  const uint32_t udpSimDropSeed = env_u32_clamped\(/  ctx.udpSimDropSeed = env_u32_clamped(/, 'config: sim seed');
must($b{initial} =~ s/  bool startInStreamView;\n//, 'initial: startInStreamView decl');
must($b{initial} =~ s/  const bool startInPicker = !startInStreamView;\n/  ctx.startInPicker = !ctx.startInStreamView;\n/, 'initial: startInPicker');
must($b{open} =~ s/  std::string directoryPunchToken;\n  Args resolvedArgs = args;\n/  ctx.resolvedArgs = ctx.args;\n/, 'open: punch/resolved decls');
must($b{control} =~ s/  SOCKET controlSock = INVALID_SOCKET;\n  std::thread controlThread;\n  ControlClient control\(args, startInPicker\);\n/  ctx.control.emplace(ctx.args, ctx.startInPicker);\n/, 'control: decls');
must($b{control} =~ s/  bool controlReady = gControl\.overUdp\.load\(std::memory_order_acquire\);\n/  ctx.controlReady = gControl.overUdp.load(std::memory_order_acquire);\n/, 'control: ready');
must($b{control} =~ s/      control\.controlSock = controlSock;\n      controlThread = std::thread\(\[&\]\(\) \{ control\.Run\(\); \}\);\n/      ctx.control->controlSock = ctx.controlSock;\n      ctx.controlThread = std::thread([&ctx]() { ctx.control->Run(); });\n/, 'control: thread');
must($b{receiver} =~ s/  const uint64_t startUs = qpc_now_us\(\);\n  VideoReceiver receiver\(args, dec, gate, startUs, udpSimDropPm, udpSimDropSeed\);\n  std::thread recvThread\(\[&\]\(\) \{ receiver\.Run\(\); \}\);\n/  ctx.startUs = qpc_now_us();\n  ctx.receiver.emplace(ctx.args, ctx.dec, ctx.gate, ctx.startUs, ctx.udpSimDropPm, ctx.udpSimDropSeed);\n  ctx.recvThread = std::thread([&ctx]() { ctx.receiver->Run(); });\n/, 'receiver: start');
# the validate block's returns stay (10/12/13); the window block returns 2; decoder 11; open 3; connect 4/5/6
must($b{window} =~ s/    return 2;\n/    return 2;\n/, 'window: return 2 present');

# ---- rename the former locals to ctx.<name> (code only) ----
open(my $mf, '>', "$T/map.txt") or die;
print $mf join('', map { "$_\tctx.$_\n" } qw(args dec gate udpSimDropPm udpSimDropSeed startInStreamView startInPicker directoryPunchToken resolvedArgs controlSock controlReady controlThread startUs recvThread));
close $mf;
sub ctxify {
  my ($name, $text) = @_;
  open(my $in, '>', "$T/in.txt") or die; print $in $text; close $in;
  my $out = `perl automation/rename_outside_strings.pl --code-only $T/map.txt < $T/in.txt`;
  die "rename failed for $name" if $? != 0;
  return $out;
}
for my $k (qw(config validate initial window decoder open connect tunnel control receiver pump shutdown)) { $b{$k} = ctxify($k, $b{$k}); }
# the toolbar callbacks capture nothing and read gSession/gPicker: unchanged. In the window block the
# only ctx use is startInStreamView -> already renamed.

# ---- assemble ----
sub fn { my ($sig, $text, $tail) = @_; (my $body = $text) =~ s/\n$//; return "$sig {\n$body\n" . ($tail // '') . "}\n\n"; }
my $startup = <<'EOF';
// The viewer's startup sequence, one function per step, in the order main() calls them; each step
// is the corresponding block of the former main() verbatim, its locals now ViewerContext members.
// A step that can fail returns the exit code main() used to return there (0 = go on).
// (viewer split refactor Phase 2-10 / 3)

#include "viewer_startup.hpp"

#include <iostream>
#include <vector>

#include "viewer_env_util.hpp"
#include "viewer_globals.hpp"
#include "viewer_input_forward.hpp"
#include "viewer_picker.hpp"
#include "viewer_window_proc.hpp"

namespace remote60::native_poc::viewer {

EOF
$startup .= fn('void apply_latency_priority()', $b{priority});
$startup .= fn('void apply_dpi_awareness()', $b{dpi});
$startup .= fn('void load_config(ViewerContext& ctx, int argc, char** argv)', $b{config});
$startup .= fn('int validate_codec_transport(ViewerContext& ctx)', $b{validate}, "  return 0;\n");
$startup .= fn('void apply_initial_state(ViewerContext& ctx)', $b{initial});
$startup .= fn('int create_window_and_toolbar(ViewerContext& ctx)', $b{window}, "  return 0;\n");
$startup .= fn('int init_decoder(ViewerContext& ctx)', $b{decoder}, "  return 0;\n");
$startup .= fn('int open_media_socket(ViewerContext& ctx)', $b{open}, "  return 0;\n");
$startup .= fn('int connect_media_socket(ViewerContext& ctx)', $b{connect}, "  return 0;\n");
$startup .= fn('void attach_control_tunnel_and_log(ViewerContext& ctx)', $b{tunnel});
$startup .= fn('void connect_control(ViewerContext& ctx)', $b{control});
$startup .= fn('void start_receiver(ViewerContext& ctx)', $b{receiver});
$startup .= fn('void run_message_pump(ViewerContext& ctx)', $b{pump});
$startup .= "}  // namespace remote60::native_poc::viewer\n";
spew("$S/viewer_startup.cpp", $startup);

my $shutdown = <<'EOF';
// The viewer's teardown, verbatim from the former main(): stop, wake the control thread, stop the
// macro engine, close the sockets, join, release the frame and the decoder, MFShutdown.
// (viewer split refactor Phase 2-11)

#include "viewer_shutdown.hpp"

#include "viewer_globals.hpp"

namespace remote60::native_poc::viewer {

EOF
$shutdown .= fn('void shutdown_viewer(ViewerContext& ctx)', $b{shutdown});
$shutdown .= "}  // namespace remote60::native_poc::viewer\n";
spew("$S/viewer_shutdown.cpp", $shutdown);

spew("$S/viewer_context.hpp", <<'EOF');
#pragma once

// What main() owns for the life of the session and hands to the startup steps and the two threads
// (viewer split refactor Phase 3). Declaration order is destruction order: the thread objects and the
// receiver / control client (which reference args / dec / gate) go before the state they use.
//
// The ten process-wide state structs (viewer_globals.hpp: gSession, gFrameBuf, ...) stay where they
// are for now; folding them in here means threading a context through every module -- Phase 4.

#include <optional>
#include <string>
#include <thread>

#include "viewer_args.hpp"
#include "viewer_common.hpp"
#include "viewer_control_client.hpp"
#include "viewer_decoder_state.hpp"
#include "viewer_frame_gate_state.hpp"
#include "viewer_video_receiver.hpp"

namespace remote60::native_poc::viewer {

struct ViewerContext {
  Args args;                        // the command line
  Args resolvedArgs;                // the directory path replaces host/port/controlPort
  std::string directoryPunchToken;  // capability from /api/connect, carried in the UDP hello
  DecoderState dec;
  FrameGateState gate;
  uint32_t udpSimDropPm = 0;        // REMOTE60_NATIVE_UDP_SIM_DROP_PM
  uint32_t udpSimDropSeed = 0;      // REMOTE60_NATIVE_UDP_SIM_DROP_SEED
  bool startInStreamView = false;   // --initial-view / REMOTE60_NATIVE_START_STREAM_VIEW
  bool startInPicker = false;
  SOCKET controlSock = INVALID_SOCKET;  // the TCP control socket (direct hosts); main closes it
  bool controlReady = false;
  uint64_t startUs = 0;             // session start, for --seconds
  std::optional<ControlClient> control;
  std::optional<VideoReceiver> receiver;
  std::thread controlThread;
  std::thread recvThread;
};

}  // namespace remote60::native_poc::viewer
EOF

spew("$S/viewer_startup.hpp", <<'EOF');
#pragma once

// The viewer's startup steps, in call order (viewer split refactor Phase 2-10 / 3). Each is the
// corresponding block of the former main() verbatim; a step that can fail returns the exit code
// main() returned there (0 = go on). Bodies in viewer_startup.cpp.
//
// Thread: main, before the control / recv threads exist (connect_control / start_receiver start them).

#include "viewer_context.hpp"

namespace remote60::native_poc::viewer {

void apply_latency_priority();                          // ABOVE_NORMAL unless REMOTE60_NATIVE_NORMAL_PRIORITY
void apply_dpi_awareness();                             // per-monitor v2, before any window exists
void load_config(ViewerContext& ctx, int argc, char** argv);   // parse_args + the REMOTE60_NATIVE_* switches
int validate_codec_transport(ViewerContext& ctx);       // 10 / 12 / 13
void apply_initial_state(ViewerContext& ctx);           // overlay config, initial view, request/gate resets
int create_window_and_toolbar(ViewerContext& ctx);      // 2
int init_decoder(ViewerContext& ctx);                   // 11 (MFStartup, optional DXGI decode surface)
int open_media_socket(ViewerContext& ctx);              // 3 (directory path or a fresh socket)
int connect_media_socket(ViewerContext& ctx);           // 4 / 5 / 6 (socket options, connect, UDP hello)
void attach_control_tunnel_and_log(ViewerContext& ctx); // control over the media socket; the connected/limiter/buffer logs
void connect_control(ViewerContext& ctx);               // TCP control socket, input channel, the control thread
void start_receiver(ViewerContext& ctx);                // the recv thread
void run_message_pump(ViewerContext& ctx);              // the UI loop until running clears or --seconds elapse

}  // namespace remote60::native_poc::viewer
EOF

spew("$S/viewer_shutdown.hpp", <<'EOF');
#pragma once

// The viewer's teardown (viewer split refactor Phase 2-11): the block the former main() ran after the
// message pump, verbatim order. Body in viewer_shutdown.cpp.

#include "viewer_context.hpp"

namespace remote60::native_poc::viewer {

void shutdown_viewer(ViewerContext& ctx);

}  // namespace remote60::native_poc::viewer
EOF

# ---- main.cpp ----
my $main = <<'EOF';
// GNLinkViewer: the viewer's main(). The session is a ViewerContext (viewer_context.hpp) driven
// through the startup steps of viewer_startup.hpp in order, the message pump, and shutdown_viewer;
// every step is a verbatim block of the monolith's main(). Exit codes are the ones the monolith
// returned at the same points. (viewer split refactor Phase 3)

#include "viewer_common.hpp"
#include "viewer_context.hpp"
#include "viewer_shutdown.hpp"
#include "viewer_startup.hpp"

#include <iostream>

using namespace remote60::native_poc::viewer;

int main(int argc, char** argv) {
  std::cout.setf(std::ios::unitbuf);
  std::cerr.setf(std::ios::unitbuf);
  apply_latency_priority();
  apply_dpi_awareness();

  ViewerContext ctx;
  load_config(ctx, argc, argv);
  if (const int rc = validate_codec_transport(ctx)) return rc;
  apply_initial_state(ctx);

EOF
$main .= $b{winsock};
$main .= <<'EOF';

  if (const int rc = create_window_and_toolbar(ctx)) return rc;
  if (const int rc = init_decoder(ctx)) return rc;
  if (const int rc = open_media_socket(ctx)) return rc;
  if (const int rc = connect_media_socket(ctx)) return rc;
  attach_control_tunnel_and_log(ctx);
  connect_control(ctx);
  start_receiver(ctx);

  run_message_pump(ctx);

  shutdown_viewer(ctx);
  std::cout << "[native-video-client] done\n";
  return 0;
}
EOF
spew("$S/native_video_client_main.cpp", $main);

my $C = slurp('apps/native_poc/CMakeLists.txt');
must($C =~ s/(  src\/viewer_globals\.cpp\n)/$1  src\/viewer_startup.cpp\n  src\/viewer_shutdown.cpp\n/, 'cmake');
spew('apps/native_poc/CMakeLists.txt', $C);
print "done\n";
