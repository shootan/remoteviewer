#pragma once

// A readiness signal the updater can trust. (updater-health-gate D1, item 2)
//
// Role:    what a relaunched Host claims about itself, and whether the updater should believe it.
// Thread:  none of its own; the judge is pure.
// Input:   a claim written by the Host, and what the updater knows about the attempt it started.
// Output:  accept, or which of the six reasons it was refused.
// Callers: the Host when it reaches real readiness; the updater's health wait.
//
// The gate used to read a line out of a log file, which meant it could be defeated by anything
// that stopped the line being written -- and was (D1 items 1 and 3). Those are fixed, and the log
// contract is kept because an already-installed updater reads it. This is the signal that does not
// depend on a log at all.
//
// What it refuses is the point. A file on disk survives the process that wrote it, so a claim is
// only worth something if it can be tied to THIS attempt and THIS process: the nonce the updater
// minted for the attempt, the version it expects, and the identity of the process it started --
// pid together with creation time, because a pid alone is reused. And a Host that said it was
// ready and then died is not ready, so liveness is part of the question rather than a separate
// one asked later.
//
// Nothing here logs a nonce. It is not a secret in the sense a token is -- it authenticates an
// attempt, not a principal -- but it is the one field that makes a replayed claim work, and a log
// is the easiest place to find one.

#include <cstdint>
#include <string>

namespace remote60::native_poc::update {

/** What a relaunched Host writes when it has actually reached readiness. */
struct ReadinessClaim {
  std::string nonce;        // echoed from the attempt the updater started
  std::string version;      // the Host's own product version
  uint32_t pid = 0;         // the Host's process id
  uint64_t createTimeQw = 0;  // its creation time, so a reused pid cannot stand in for it
  uint64_t writtenAtMs = 0;   // when the claim was written, in the updater's clock domain
  bool valid = false;         // false when parsing failed; every field above is then meaningless
};

/** What the updater knows about the attempt it is waiting on. */
struct ReadinessExpectation {
  std::string nonce;
  std::string version;
  uint32_t pid = 0;
  uint64_t createTimeQw = 0;
  uint64_t nowMs = 0;
  uint64_t maxAgeMs = 30000;  // the gate's own budget; a claim older than the attempt is not this one
  bool processAlive = true;   // asked at judging time, not when the claim was written
};

enum class ReadinessVerdict : uint8_t {
  Accept = 0,
  Malformed,     // nothing readable was there
  WrongNonce,    // a claim from another attempt, or a replay of an older one
  WrongVersion,  // the right attempt, the wrong build
  WrongProcess,  // the right attempt and build, a different process -- or a reused pid
  Stale,         // written too long ago to be about this attempt
  NotAlive,      // it said it was ready and is not running now
};

const char* readiness_verdict_name(ReadinessVerdict v);

/**
 * Whether this claim is this attempt's, and still true.
 *
 * The order is deliberate: identity first, then freshness, then liveness. A claim from another
 * attempt should be reported as that rather than as "stale", because the two point at different
 * problems -- one is a leftover file, the other is a Host taking too long.
 */
ReadinessVerdict readiness_judge(const ReadinessClaim& claim, const ReadinessExpectation& expect);

/** The on-disk form. One line per field, so a partial write cannot parse as a whole claim. */
std::string serialize_readiness(const ReadinessClaim& claim);
ReadinessClaim parse_readiness(const std::string& text);

/** The ticket the updater leaves for the Host it is about to start. */
struct AttemptTicket {
  std::string nonce;
  std::string version;
  bool valid = false;
};

/**
 * Where the two files live: beside host_app.log.
 *
 * Derived from the health log path the updater was already given rather than from an environment
 * variable, because the two processes do not share one. The Host runs as the user and the updater
 * runs elevated from the same user's session, so this directory is writable by one and readable by
 * the other; it is per-user, which is the trust boundary this rides on. Target validation is the
 * claim's own nonce, pid and creation time -- the directory only decides who can reach the file.
 */
std::wstring readiness_dir_from_log(const std::wstring& healthLogPath);
std::wstring attempt_ticket_path(const std::wstring& healthLogPath);
std::wstring readiness_claim_path(const std::wstring& healthLogPath);

bool write_attempt_ticket(const std::wstring& path, const AttemptTicket& ticket);
AttemptTicket read_attempt_ticket(const std::wstring& path);

/** Replaces the claim in one step, so a reader never sees a half-written one. */
bool write_claim_atomic(const std::wstring& path, const ReadinessClaim& claim);
ReadinessClaim read_claim(const std::wstring& path);
void remove_claim(const std::wstring& path);

/** A 16-hex attempt nonce from the system CSPRNG. Never logged. */
std::string mint_attempt_nonce();

}  // namespace remote60::native_poc::update
