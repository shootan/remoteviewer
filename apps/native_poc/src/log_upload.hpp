#pragma once

// Ships log lines to the directory service so all three machines land in one place.
//
// Role:    a process-wide sink the shells hand every line they already write to disk, which a
//          background thread batches into POST /api/logs.
// Thread:  Enqueue is called from whatever thread produced the line (the pipe readers, the UI)
//          and never blocks on the network -- it takes a mutex, pushes, and returns. One worker
//          thread owns the socket.
// Input:   lines, plus the directory url and whichever token the caller already holds.
// Output:  batched http requests; nothing on the caller's path.
// Callers: client_shell_main (viewer/client lines), host_app_main (host lines).
//
// The file on disk is written either way. Upload is the copy that can be lost -- a queue that
// filled, a server that was down -- and losing it must never cost a line locally, which is why
// this is a second sink rather than a replacement for the first.

#include <cstddef>
#include <cstdint>
#include <string>

namespace remote60::native_poc {

struct LogUploadConfig {
  std::string directoryUrl;   // http://host[:port]; the same one the session already uses
  std::string sessionToken;   // account session -> Authorization: Bearer
  std::string hostToken;      // host registration -> x-host-token (used when there is no session)
  std::string device;         // empty = machine_id()
  // A queue this size holds roughly a minute of ordinary logging. Past it the oldest lines go,
  // because the interesting part of a log that is overflowing is its end, not its beginning.
  size_t queueCapBytes = 4u * 1024u * 1024u;
  uint32_t flushIntervalMs = 2000;
  size_t batchMaxBytes = 192u * 1024u;  // the server refuses more than 512KB per request
};

/**
 * Starts the uploader, or reports why it cannot run.
 *
 * Returns false when it is switched off, when there is no url, or when neither token is present
 * -- all of which are ordinary states rather than failures, so callers log the reason and carry
 * on writing to disk.
 */
bool log_upload_start(const LogUploadConfig& config, std::string* outReason);

/** Queues one line under `stream` ("viewer", "client", "host"). Never blocks; may drop. */
void log_upload_enqueue(const char* stream, const std::string& line);

/** Flushes what it can and stops the worker. Safe to call when never started. */
void log_upload_stop();

/** True once a start succeeded; lets callers skip building strings they cannot send. */
bool log_upload_running();

}  // namespace remote60::native_poc
