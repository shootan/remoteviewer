#pragma once

// ka-pc-impl r1 (KA ③, proposal_ka_pc_r2.md, Codex codex_ka_pc_design_r1.md -- O1): the managed standby viewer.
// The client shell starts one GNLinkViewer per kept PC with --managed-standby and two private pipes. This process
// then owns that PC's StandbyLease (its own socket, entry and Ping) and NOTHING else: no window, no decoder, no MF,
// no D3D, no picker, no input channel -- until the shell says Activate (the user tapped the PC). An activation is
// one ordinary viewer session run on the lease's socket (run_session); when it ends -- the window's X, the host
// gone, another device taking over -- its threads have joined, the lease is told SessionOver and goes on by Ping on
// the same socket, and the window and decoder are gone again (Codex ②). A session that ended is never activated
// again on its own (Codex ⑤): a new activation is a new tap.

#include <functional>

#include "viewer_context.hpp"

namespace remote60::native_poc::viewer {

// `load` fills a fresh context from the command line (load_config + checks + initial state); `run` is one session.
int run_managed_standby(const std::function<bool(ViewerContext&)>& load, const std::function<int(ViewerContext&)>& run);

}  // namespace remote60::native_poc::viewer
