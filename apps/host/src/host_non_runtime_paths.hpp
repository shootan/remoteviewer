#pragma once

#include <optional>

namespace remote60::host {

// Handles non-runtime host paths (probes + legacy signaling bootstrap path).
// Returns exit code when handled, std::nullopt when caller should continue runtime dispatch.
std::optional<int> run_non_runtime_path(int argc, char** argv);

}  // namespace remote60::host
