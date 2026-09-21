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

#include "update_effects.hpp"

namespace remote60::native_poc::update {

/**
 * PIDs of every running process whose image name matches one of `imageNames` (case-insensitive,
 * file name only).
 *
 * Enumerating by name is unavoidable here -- there is no handle to inherit from a product this
 * process did not start -- but the result is a list of exact PIDs, and everything downstream
 * works from that list rather than from names.
 */
std::vector<ProcessTarget> enumerate_product_processes(const std::vector<std::wstring>& imageNames);

/**
 * Asks one process to close: WM_CLOSE to its top-level windows, then a console CTRL event for the
 * windowless ones.
 *
 * Never terminates. A process that ignores the request stays alive, Quiesce times out, and the
 * update is abandoned before anything on disk changes -- which is the correct outcome, because
 * whatever the product is busy with is worth more than an update.
 */
bool request_process_stop(const ProcessTarget& target);

/**
 * Asks the SCM to stop a service, by name.
 *
 * What the answer means, exactly: true when the SCM ACCEPTED the control (or when the service is
 * not installed, or was not running -- both are already the state being asked for). It does NOT
 * mean the service stopped. A service that accepts a STOP enters SERVICE_STOP_PENDING and may sit
 * there; whether its process actually went is a separate question, and on the update path it is
 * answered the same way it is for every other target -- by waiting on a handle to that process,
 * not by asking the SCM again.
 *
 * Declared here so the service case can be exercised against an ISOLATED fixture service rather
 * than against the product's. See update_service_stop_test.cpp.
 */
bool request_service_stop(const wchar_t* serviceName);

/** The images an update replaces, in the order the installer's payload lists them. */
const std::vector<std::wstring>& product_image_names();

/**
 * Everything a release replaces: the images above, the updater, and the two data files.
 *
 * Kept separate from product_image_names() because the two lists answer different questions --
 * "what must not be running while we replace it" and "what do we replace" -- and there is exactly
 * one file where the answers differ. GNLinkUpdater.exe has to be REPLACED (otherwise it becomes a
 * permanent maintenance binary frozen at its compile-time constants, which is the trap that ruled
 * out re-running the old installer) but must not be a STOP target: the running updater is a copy
 * outside installDir, so the installed one is not running, and anything else answering to that
 * name is not ours to close.
 *
 * The lists were the same one for a while, on the argument that sharing prevents drift. It did
 * not: GNLinkUpdater.exe was missing from the payload for every release, so the updater has never
 * once been updated by an update. The anti-drift property is kept by DERIVING this from that --
 * the stop list cannot gain an entry this one lacks -- which is the invariant that actually
 * matters. product_payload_list_contract() asserts it.
 */
std::vector<std::wstring> product_payload_names();

/** True when every stop target is also replaced. Nothing may be closed and then left stale. */
bool product_payload_list_contract();

}  // namespace remote60::native_poc::update
