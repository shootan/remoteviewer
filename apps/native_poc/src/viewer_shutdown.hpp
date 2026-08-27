#pragma once

// The viewer's teardown (viewer split refactor Phase 2-11): the block the former main() ran after the
// message pump, verbatim order. Body in viewer_shutdown.cpp.

#include "viewer_context.hpp"

namespace remote60::native_poc::viewer {

void shutdown_viewer(ViewerContext& ctx);

}  // namespace remote60::native_poc::viewer
