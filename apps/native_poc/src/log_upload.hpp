#pragma once

// Ships log lines to the directory service so all three machines land in one place.
//
// Role:    a process-wide sink the shells hand every line they already write to disk, which a
//          background thread batches into POST /api/logs.
// Thread:  Enqueue is called from whatever thread produced the line (the pipe readers, the UI)
//          and never blocks on the network -- it takes a mutex, pushes, and returns. One worker
//          thread owns the socket. Configure / clear_credentials / status take the same mutex
//          and return at once; only stop() joins.
// Input:   lines, plus the directory url and whichever token the caller currently holds.
// Output:  batched http requests; nothing on the caller's path.
// Callers: client_shell_main (viewer/client lines), host_app_main (host lines).
//
// The file on disk is written either way. Upload is the copy that can be lost -- a queue that
// filled, a server that was down -- and losing it must never cost a line locally, which is why
// this is a second sink rather than a replacement for the first.
//
// Credentials change while the process lives: the host re-registers (the server retires the old
// host token that instant), the client logs in again, the server restarts and forgets every
// session. The uploader therefore has to be RE-POINTED, not started once -- the 2026-09-07 field
// run lost 2,395 host batches to a token the server had rotated four minutes after start, because
// the second configure was a no-op (P10). What it does with a rejected token is bounded and
// visible: hold what it has, stop sending, report "sign in again", resume when a token arrives.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace remote60::native_poc {

struct LogUploadConfig {
  std::string directoryUrl;   // http://host[:port]; the same one the session already uses
  std::string sessionToken;   // account session -> Authorization: Bearer
  std::string hostToken;      // host registration -> x-host-token (used when there is no session)
  std::string device;         // empty = machine_id()
  // Whose lines these are, as the caller knows it (the account). Together with the server
  // (host:port) and the device it forms the OWNER of what is queued: when any of the three
  // changes, whatever is still queued -- and any answer still in flight -- belonged to the
  // previous owner and is discarded rather than sent under the new credentials to the new place.
  std::string identity;
  // A queue this size holds roughly a minute of ordinary logging. Past it the oldest lines go,
  // because the interesting part of a log that is overflowing is its end, not its beginning.
  size_t queueCapBytes = 4u * 1024u * 1024u;
  uint32_t flushIntervalMs = 2000;
  size_t batchMaxBytes = 192u * 1024u;  // the server refuses more than 512KB per request
  // Bounded retry for failures that are not the server's final word (unreachable, 5xx, 408,
  // 429): a batch is sent at most retryMaxAttempts times, retryBaseDelayMs * 2^(n-1) apart, and
  // given up on once it is retryMaxAgeMs old. 401 is not retried -- it waits for a new token.
  uint32_t retryMaxAttempts = 3;
  uint32_t retryBaseDelayMs = 2000;
  uint32_t retryMaxAgeMs = 60000;
};

/** A snapshot for status text and diagnostics. Counters are process lifetime. */
struct LogUploadStatus {
  bool running = false;
  bool credentials = false;       // false until configured, and after clear_credentials
  bool authRejected = false;      // the server answered 401 to the current token: paused
  uint64_t sentBatches = 0;
  uint64_t failedSends = 0;       // every unreachable / non-2xx send, retries included
  uint64_t retriedBatches = 0;    // sends that were a retry of an earlier failure
  uint64_t discardedBatches = 0;  // given up: permanent status, retry budget, age, hold bound
  uint64_t droppedLines = 0;      // queue overflow (oldest first) or no credentials to send with
  uint64_t discardedLines = 0;    // identity change / credentials cleared
  uint64_t heldBatches = 0;       // waiting for a retry slot or for a new token
  uint64_t foreignAnswersDiscarded = 0;  // late answers for a previous owner (account/server/device/sign-out)
  uint32_t lastStatus = 0;        // http status of the last send (0 = unreachable)
  uint64_t lastOkUs = 0;          // steady-clock microseconds of the last accepted batch
  uint64_t lastRejectUs = 0;      // ... of the last 401
  uint64_t workerCycles = 0;      // worker loop iterations (wake-ups); a spin shows here first
};

/**
 * Starts the uploader, or re-points a running one at the given url / token / identity.
 *
 * Returns false when it is switched off, when there is no url, or when neither token is present
 * -- all of which are ordinary states rather than failures, so callers log the reason and carry
 * on writing to disk. A running uploader keeps its queue when only the token changed (same
 * owner: identity, server and device) and discards it when the owner changed. A 401 pause ends
 * here: held batches go out with the new token first. Calling it again with identical values is
 * a no-op.
 */
bool log_upload_configure(const LogUploadConfig& config, std::string* outReason);

/**
 * Signed out: forget the credentials, discard what is queued (its owner is gone) and stop
 * sending. The worker stays; the next configure resumes it. Never blocks on the network.
 */
void log_upload_clear_credentials(const char* reason);

/** Queues one line under `stream` ("viewer", "client", "host"). Never blocks; may drop. */
void log_upload_enqueue(const char* stream, const std::string& line);

/** Sends what it can once, stops the worker and resets the state. Safe when never started. */
void log_upload_stop();

/** True once a configure succeeded; lets callers skip building strings they cannot send. */
bool log_upload_running();

LogUploadStatus log_upload_status();

/**
 * Invoked on the worker thread each time the token transitions to rejected (401), so a UI can
 * say "sign in again" without polling. Pass an empty function to remove it.
 */
void log_upload_set_auth_rejected_callback(std::function<void()> callback);

}  // namespace remote60::native_poc
