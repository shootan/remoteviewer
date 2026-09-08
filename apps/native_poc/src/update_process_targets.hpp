#pragma once

// Finding and asking the product's processes to stop, by image name.
//
// This is the one part of the updater that can reach a real, running GNLinkHost.exe, so it is
// kept in its own translation unit and the test binary does not link it. The isolation is not a
// convention a test has to honour -- the symbols are simply not present in the test build, so
// nothing there can call them by accident.
//
// Design 3.2: no `taskkill /T`, no tree kill, no forced termination. The payload images are
// enumerated, each instance is asked to close, and whether it did is decided by waiting on that
// exact process. Whose child a process is has no bearing on any of it.

#include <cstdint>
#include <string>
#include <vector>

namespace remote60::native_poc::update {

/**
 * PIDs of every running process whose image name matches one of `imageNames` (case-insensitive,
 * file name only).
 *
 * Enumerating by name is unavoidable here -- there is no handle to inherit from a product this
 * process did not start -- but the result is a list of exact PIDs, and everything downstream
 * works from that list rather than from names.
 */
std::vector<uint32_t> enumerate_product_processes(const std::vector<std::wstring>& imageNames);

/**
 * Asks one process to close: WM_CLOSE to its top-level windows, then a console CTRL event for the
 * windowless ones.
 *
 * Never terminates. A process that ignores the request stays alive, Quiesce times out, and the
 * update is abandoned before anything on disk changes -- which is the correct outcome, because
 * whatever the product is busy with is worth more than an update.
 */
bool request_process_stop(uint32_t pid);

/** The images an update replaces, in the order the installer's payload lists them. */
const std::vector<std::wstring>& product_image_names();

}  // namespace remote60::native_poc::update
