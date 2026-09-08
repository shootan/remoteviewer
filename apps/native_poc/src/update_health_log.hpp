#pragma once

// Reading only what a run wrote, and nothing a previous run left behind.
//
// A log file holds every run the product has ever had, including the version an update just
// replaced. A health check that reads the whole file will find a perfectly good banner from last
// week and conclude that the new build came up -- which is not a subtle failure, it is the check
// reporting success in exactly the case it exists to catch.
//
// So evidence is bounded: the size of the log is recorded before anything is relaunched, and only
// bytes appended after that mark are ever looked at. That is a two-line idea and it is the whole
// reason a health check means anything, so it lives where it can be tested rather than inside the
// production translation unit that starts processes.

#include <cstdint>
#include <string>

namespace remote60::native_poc::update {

/**
 * Size of a file in bytes, or 0 when it cannot be read.
 *
 * 0 for a missing file is the right answer here rather than an error: a log that does not exist
 * yet has nothing in it, and everything written from now on is new.
 */
uint64_t file_size_or_zero(const std::wstring& path);

/**
 * Bytes of `path` from `offset` to the end. Empty when there are none or it cannot be read.
 *
 * Opened shared for reading and writing, because the product is writing to this file while it is
 * being read -- opening it exclusively would be the health check preventing the evidence it is
 * waiting for.
 *
 * Reads at most `maxBytes`, so a log that grew unexpectedly is not pulled into memory in full.
 */
std::string read_from_offset(const std::wstring& path, uint64_t offset,
                             uint64_t maxBytes = 1u << 20);

}  // namespace remote60::native_poc::update
