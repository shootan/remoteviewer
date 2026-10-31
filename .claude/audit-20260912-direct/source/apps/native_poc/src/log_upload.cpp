// See log_upload.hpp.

#include "log_upload.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <ctime>
#include <deque>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>

#include "directory_client.hpp"
#include "env_util.hpp"

namespace remote60::native_poc {
namespace {

// A tiny always-on trace the uploader writes itself, to %LOCALAPPDATA%\GNLink\log_upload.diag.
// The uploader runs inside the shell process, whose own stdout/stderr is captured nowhere -- so
// when uploads fail there is otherwise no trace at all, which is exactly the hole that made the
// first "logs stop after one batch" bug un-diagnosable. Kept append-only and tiny (a line per
// flush that did something), so it never itself becomes the disk problem it reports on.
//
// Never a token: the diag is world-readable for the user and gets pasted into bug reports.
void diag(const std::string& text) {
  static std::mutex diagMu;
  const std::string base = env_string_or_empty("LOCALAPPDATA");
  if (base.empty()) return;
  std::lock_guard<std::mutex> lk(diagMu);
  const std::string path = base + "\\GNLink\\log_upload.diag";
  std::FILE* f = std::fopen(path.c_str(), "a");
  if (!f) return;
  std::time_t t = std::time(nullptr);
  std::tm tm{};
  localtime_s(&tm, &t);
  char stamp[24]{};
  std::strftime(stamp, sizeof(stamp), "%m-%d %H:%M:%S ", &tm);
  std::fputs(stamp, f);
  std::fputs(text.c_str(), f);
  std::fputc('\n', f);
  std::fclose(f);
}

uint64_t steady_now_us() {
  return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                   std::chrono::steady_clock::now().time_since_epoch())
                                   .count());
}

struct QueuedLine {
  std::string stream;
  std::string text;
};

// A batch that failed once and is waiting: for its retry delay, or for a new token.
struct HeldBatch {
  std::string stream;
  std::string body;
  uint32_t attempts = 0;        // sends already made
  uint64_t firstAttemptUs = 0;  // steady; the age bound counts from here
  uint64_t dueUs = 0;           // earliest next attempt; 0 = as soon as a token allows
  bool awaitingToken = false;   // held by a 401, not by the retry schedule
};

// Bound on what a 401 or an outage may keep in memory besides the line queue: a few batches,
// oldest discarded first. The queue cap already bounds the lines behind them.
constexpr size_t kHeldMaxBatches = 4;

enum class Outcome { Ok, Auth, Transient, Permanent };

Outcome classify(bool sent, uint32_t status) {
  if (!sent) return Outcome::Transient;  // unreachable / no http answer
  if (status >= 200 && status < 300) return Outcome::Ok;
  if (status == 401) return Outcome::Auth;
  if (status == 408 || status == 429 || status >= 500) return Outcome::Transient;
  return Outcome::Permanent;  // 400, 403, 404, 413 ...: the server's final word on this body
}

// How long the final drain may keep starting requests. Not a total: a request already in flight
// is waited out on top of this. Picked so a close is over within a couple of seconds of the last
// send that can still succeed, rather than however long the queue happens to be.
constexpr uint64_t kStopDrainBudgetMs = 3000;

const char* outcome_name(Outcome o) {
  switch (o) {
    case Outcome::Ok: return "ok";
    case Outcome::Auth: return "auth";
    case Outcome::Transient: return "transient";
    case Outcome::Permanent: return "permanent";
  }
  return "?";
}

struct UploaderState {
  std::mutex mu;
  std::condition_variable cv;
  std::deque<QueuedLine> queue;
  size_t queuedBytes = 0;
  std::deque<HeldBatch> held;
  size_t heldBytes = 0;
  // Counters (LogUploadStatus).
  uint64_t sentBatches = 0;
  uint64_t failedSends = 0;
  uint64_t retriedBatches = 0;
  uint64_t discardedBatches = 0;
  uint64_t droppedLines = 0;
  uint64_t discardedLines = 0;
  uint32_t lastStatus = 0;
  uint64_t lastOkUs = 0;
  uint64_t lastRejectUs = 0;
  bool running = false;
  bool stopping = false;
  // Latched by log_upload_shutdown(): the process is going away. Checked under the same lock that
  // starts the worker, so a configure racing the close cannot slip a second thread past the join.
  bool shutdown = false;
  // When the final drain must stop starting new requests. A stop is not free -- the worker sends
  // what it can and every send is a blocking http_post -- so without this the wait is as long as
  // the queue is deep times the http timeout, on whichever thread called stop.
  uint64_t stopDeadlineUs = 0;
  std::thread worker;

  LogUploadConfig config;
  std::string host;
  uint16_t port = 0;
  bool secure = false;
  std::string device;
  std::string headers;      // the auth + identity headers for the CURRENT credentials
  bool credentials = false;
  bool authRejected = false;
  // Who the queued lines belong to and where they go: account identity + normalised server
  // host:port + device. A change (or a sign-out) bumps ownerEpoch; a send carries the epoch it
  // was made under, and an answer for a previous epoch may not touch anything -- not the pause
  // state, not the hold, and its body is never re-sent under the new owner's credentials.
  std::string ownerKey;
  uint64_t ownerEpoch = 0;
  uint64_t foreignAnswersDiscarded = 0;
  uint64_t configGeneration = 0;  // bumped by every credential change; a send remembers its own
  uint64_t wakeSeq = 0;           // bumped whenever the worker should re-read its config now
  std::function<void()> authRejectedCallback;
  uint64_t lastTransientDiagUs = 0;
  uint64_t workerCycles = 0;
};

UploaderState& state() {
  static UploaderState s;
  return s;
}

/** One http header block, minus the per-request stream header. */
std::string build_headers(const LogUploadConfig& config, const std::string& device) {
  std::ostringstream os;
  if (!config.sessionToken.empty()) {
    os << "Authorization: Bearer " << config.sessionToken << "\r\n";
  } else {
    os << "x-host-token: " << config.hostToken << "\r\n";
  }
  os << "x-log-device: " << device << "\r\n";
  return os.str();
}

// Requires s.mu. Drops the queue and the held batches; the caller says why.
void discard_all_locked(UploaderState& s, uint64_t* outLines, uint64_t* outBatches) {
  *outLines = s.queue.size();
  *outBatches = s.held.size();
  s.discardedLines += s.queue.size();
  s.discardedBatches += s.held.size();
  s.queue.clear();
  s.queuedBytes = 0;
  s.held.clear();
  s.heldBytes = 0;
}

// Requires s.mu. Keeps the hold bounded: oldest out.
void hold_locked(UploaderState& s, HeldBatch batch) {
  while (s.held.size() >= kHeldMaxBatches) {
    s.heldBytes -= (std::min)(s.heldBytes, s.held.front().body.size());
    s.held.pop_front();
    ++s.discardedBatches;
  }
  s.heldBytes += batch.body.size();
  s.held.push_back(std::move(batch));
}

struct SendJob {
  std::string stream;
  std::string body;
  std::string headers;
  std::string host;
  uint16_t port = 0;
  bool secure = false;
  uint64_t ownerEpoch = 0;
  uint64_t configGeneration = 0;
  uint32_t attempts = 0;        // before this send
  uint64_t firstAttemptUs = 0;  // 0 = first send
};

std::string owner_key(const std::string& identity, const std::string& host, uint16_t port,
                      bool secure, const std::string& device) {
  std::string h = host;
  for (auto& c : h) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  // The scheme is part of the destination, not decoration on it. The same host:port over http and
  // over https is two different servers as far as anything queued here is concerned -- one of
  // them may not even be listening -- so a scheme change is an owner change, and what is queued
  // for the old owner does not silently follow.
  return identity + "|" + (secure ? "https://" : "http://") + h + ":" + std::to_string(port) +
         "|" + device;
}

// Requires s.mu. Picks the next thing to send: a held batch that is due, else a fresh batch
// from the queue. False when there is nothing (or nothing may go yet).
bool next_job_locked(UploaderState& s, uint64_t nowUs, SendJob* job) {
  if (!s.credentials || s.authRejected) return false;
  for (auto it = s.held.begin(); it != s.held.end(); ++it) {
    if (it->awaitingToken || it->dueUs <= nowUs) {
      job->stream = std::move(it->stream);
      job->body = std::move(it->body);
      job->attempts = it->attempts;
      job->firstAttemptUs = it->firstAttemptUs;
      s.heldBytes -= (std::min)(s.heldBytes, job->body.size());
      s.held.erase(it);
      break;
    }
  }
  if (job->body.empty()) {
    if (s.queue.empty()) return false;
    // One stream per request: take lines of the front line's stream, in order, up to the
    // batch size; the other streams follow on the next pass.
    const std::string stream = s.queue.front().stream;
    size_t taken = 0;
    std::string body;
    for (auto it = s.queue.begin(); it != s.queue.end() && taken < s.config.batchMaxBytes;) {
      if (it->stream != stream) {
        ++it;
        continue;
      }
      taken += it->text.size() + 1;
      body.append(it->text);
      body.push_back('\n');
      it = s.queue.erase(it);
    }
    s.queuedBytes = s.queuedBytes > taken ? s.queuedBytes - taken : 0;
    job->stream = stream;
    job->body = std::move(body);
    job->attempts = 0;
    job->firstAttemptUs = 0;
  }
  job->headers = s.headers + "x-log-stream: " + job->stream + "\r\n";
  job->host = s.host;
  job->port = s.port;
  job->secure = s.secure;
  job->ownerEpoch = s.ownerEpoch;
  job->configGeneration = s.configGeneration;
  return true;
}

void worker_loop() {
  UploaderState& s = state();
  diag("worker started host=" + s.host + ":" + std::to_string(s.port));
  uint64_t cycles = 0;
  uint64_t flushes = 0;
  uint64_t seenWake = 0;
  uint64_t lastIdleDiagUs = 0;
  constexpr uint64_t kIdleAliveDiagIntervalUs = 60'000'000;  // one "idle alive" line per minute at most
  for (;;) {
    SendJob job;
    bool finalPass = false;
    {
      std::unique_lock<std::mutex> lk(s.mu);
      // A configure / clear bumps wakeSeq so a worker asleep on the old cadence re-reads its
      // config now instead of at the end of the old interval. A full batch wakes the worker only
      // when it may actually send: paused on a 401 (or without credentials) the queue can sit at
      // or past batchMaxBytes for as long as the pause lasts, and a wake condition that stayed
      // true made this loop spin on a core and write an "idle alive" diag line every 30 cycles --
      // 772 KB/s of diag, measured (A01, stabilization audit U3 / probe Q1). While paused the
      // worker sleeps the flush interval like an idle one; the new token's configure bumps
      // wakeSeq and it drains at once.
      s.cv.wait_for(lk, std::chrono::milliseconds(s.config.flushIntervalMs), [&s, seenWake] {
        return s.stopping || s.wakeSeq != seenWake ||
               (s.credentials && !s.authRejected && s.queuedBytes >= s.config.batchMaxBytes);
      });
      seenWake = s.wakeSeq;
      ++cycles;
      s.workerCycles = cycles;
      finalPass = s.stopping;
      const uint64_t nowUs = steady_now_us();
      if (finalPass && s.stopDeadlineUs != 0 && nowUs >= s.stopDeadlineUs) {
        // The drain has had its budget. Whoever called stop is blocked in join() -- on the UI
        // thread, in the case this exists for -- and a server that accepts and never answers
        // would otherwise hold the window open for one http timeout per queued batch.
        uint64_t lines = 0, batches = 0;
        discard_all_locked(s, &lines, &batches);
        diag("worker exiting (stop budget spent) sent=" + std::to_string(s.sentBatches) +
             " discardedLines=" + std::to_string(lines) +
             " heldDiscarded=" + std::to_string(batches));
        return;
      }
      if (!next_job_locked(s, nowUs, &job)) {
        if (s.stopping) {
          uint64_t lines = 0, batches = 0;
          discard_all_locked(s, &lines, &batches);  // paused with no way to send them
          diag("worker exiting (stop) sent=" + std::to_string(s.sentBatches) +
               " discardedLines=" + std::to_string(lines) + " heldDiscarded=" + std::to_string(batches));
          return;
        }
        // A quiet minute still proves the worker is alive, which is the thing the first bug hid.
        // On the clock, not per cycle: a cycle count is only a minute when the loop sleeps its
        // interval, and the one time it did not (A01) this line was the disk flood.
        if (lastIdleDiagUs == 0) lastIdleDiagUs = nowUs;
        if (nowUs - lastIdleDiagUs >= kIdleAliveDiagIntervalUs) {
          lastIdleDiagUs = nowUs;
          diag("idle alive cycles=" + std::to_string(cycles) + " sent=" + std::to_string(s.sentBatches) +
               " dropped=" + std::to_string(s.droppedLines) + " held=" + std::to_string(s.held.size()) +
               (s.authRejected ? " authRejected=1" : "") + (s.credentials ? "" : " credentials=0"));
        }
        // Nothing to send: back to the timed wait above (never an immediate re-run).
        continue;
      }
    }

    // ---- the send, outside the lock ----
    uint32_t status = 0;
    const bool sent = directory::http_post(job.host, job.port, job.secure, "/api/logs",
                                           "text/plain", job.headers, job.body, &status, nullptr);
    const Outcome outcome = classify(sent, status);
    const uint64_t nowUs = steady_now_us();
    std::function<void()> callback;
    {
      std::lock_guard<std::mutex> lk(s.mu);
      if (job.ownerEpoch != s.ownerEpoch) {
        // A late answer for a batch whose owner is gone (another account, another server or
        // device, a sign-out): whatever it says -- 401, 5xx, even 200 -- it describes a session
        // that no longer exists. Discard the batch; the current owner's state is untouched.
        ++s.foreignAnswersDiscarded;
        ++s.discardedBatches;
        diag("late answer for a previous owner discarded status=" + std::to_string(status) + " reached=" +
             (sent ? "1" : "0") + " stream=" + job.stream + " bytes=" + std::to_string(job.body.size()));
        continue;
      }
      s.lastStatus = status;
      if (job.attempts > 0) ++s.retriedBatches;
      ++flushes;
      switch (outcome) {
        case Outcome::Ok:
          ++s.sentBatches;
          s.lastOkUs = nowUs;
          // First batch and then every ~20th: enough to prove batches keep flowing without spamming.
          if (s.sentBatches == 1 || s.sentBatches % 20 == 0) {
            diag("flushed sent=" + std::to_string(s.sentBatches) + " failed=" + std::to_string(s.failedSends) +
                 " discarded=" + std::to_string(s.discardedBatches) + " dropped=" + std::to_string(s.droppedLines));
          }
          break;
        case Outcome::Auth: {
          ++s.failedSends;
          s.lastRejectUs = nowUs;
          HeldBatch h;
          h.stream = job.stream;
          h.body = std::move(job.body);
          h.attempts = job.attempts;  // a 401 does not spend the retry budget
          h.firstAttemptUs = job.firstAttemptUs ? job.firstAttemptUs : nowUs;
          h.awaitingToken = true;
          if (finalPass) {
            ++s.discardedBatches;
          } else {
            hold_locked(s, std::move(h));
          }
          if (job.configGeneration != s.configGeneration) {
            // The token was replaced while this request was in flight: the answer is about the
            // old one. The held batch goes out with the new token on the next pass.
            break;
          }
          if (!s.authRejected) {
            s.authRejected = true;
            callback = s.authRejectedCallback;
            diag("auth rejected status=401 stream=" + job.stream + " bytes=" + std::to_string(job.body.size()) +
                 " -> paused until a new token; held=" + std::to_string(s.held.size()));
          }
          break;
        }
        case Outcome::Transient: {
          ++s.failedSends;
          const uint32_t attempts = job.attempts + 1;
          const uint64_t firstUs = job.firstAttemptUs ? job.firstAttemptUs : nowUs;
          const uint64_t ageMs = (nowUs - firstUs) / 1000;
          const bool budgetLeft = attempts < s.config.retryMaxAttempts && ageMs < s.config.retryMaxAgeMs;
          if (budgetLeft && !finalPass) {
            HeldBatch h;
            h.stream = job.stream;
            h.body = std::move(job.body);
            h.attempts = attempts;
            h.firstAttemptUs = firstUs;
            uint64_t delayMs = s.config.retryBaseDelayMs;
            for (uint32_t i = 1; i < attempts && delayMs < (1u << 20); ++i) delayMs *= 2;
            h.dueUs = nowUs + delayMs * 1000;
            hold_locked(s, std::move(h));
            if (nowUs - s.lastTransientDiagUs >= 10'000'000) {
              s.lastTransientDiagUs = nowUs;
              diag("send FAILED stream=" + job.stream + " reached=" + (sent ? "1" : "0") +
                   " status=" + std::to_string(status) + " host=" + job.host + ":" + std::to_string(job.port) +
                   " -> retry " + std::to_string(attempts) + "/" + std::to_string(s.config.retryMaxAttempts) +
                   " in " + std::to_string(delayMs) + "ms");
            }
          } else {
            ++s.discardedBatches;
            diag("send FAILED stream=" + job.stream + " reached=" + (sent ? "1" : "0") +
                 " status=" + std::to_string(status) + " -> gave up attempts=" + std::to_string(attempts) +
                 " ageMs=" + std::to_string(ageMs) + (finalPass ? " (stopping)" : ""));
          }
          break;
        }
        case Outcome::Permanent:
          ++s.failedSends;
          ++s.discardedBatches;
          diag("send REJECTED stream=" + job.stream + " status=" + std::to_string(status) +
               " bytes=" + std::to_string(job.body.size()) + " -> discarded (" + outcome_name(outcome) + ")");
          break;
      }
    }
    if (callback) callback();
  }
}

}  // namespace

bool log_upload_configure(const LogUploadConfig& config, std::string* outReason) {
  UploaderState& s = state();

  // Off by an explicit switch only: the whole point is that logs arrive without anyone asking.
  const std::string enabled = env_string_or_empty("REMOTE60_LOG_UPLOAD");
  if (enabled == "0" || enabled == "off" || enabled == "false") {
    if (outReason) *outReason = "disabled by REMOTE60_LOG_UPLOAD";
    return false;
  }
  if (config.directoryUrl.empty()) {
    if (outReason) *outReason = "no directory url";
    return false;
  }
  if (config.sessionToken.empty() && config.hostToken.empty()) {
    if (outReason) *outReason = "no session or host token yet";
    return false;
  }

  std::string host;
  uint16_t port = 0;
  std::string parseError;
  bool secure = false;
  if (!directory::parse_directory_url(config.directoryUrl, &host, &port, &parseError, &secure)) {
    if (outReason) *outReason = parseError;
    return false;
  }
  const std::string device = config.device.empty() ? directory::machine_id() : config.device;
  const std::string headers = build_headers(config, device);
  const char* auth = config.sessionToken.empty() ? "host-token" : "bearer";
  const std::string ownerKey = owner_key(config.identity, host, port, secure, device);

  std::string reason;
  {
    std::lock_guard<std::mutex> lk(s.mu);
    if (s.shutdown) {
      // Under the same lock as the start below, on purpose: checking earlier would leave a window
      // in which this call passes the check, the shutdown latches and joins, and then this one
      // starts a worker nobody will ever join.
      if (outReason) *outReason = "shutting down";
      return false;
    }
    if (s.running && !s.stopping) {
      // Same owner = same account, same server (host:port) and same device: only then does a new
      // token inherit the queue and the held batches. Anything else is a different destination for
      // what is queued, and what is queued is discarded.
      const bool sameIdentity = s.credentials && s.ownerKey == ownerKey;
      const bool sameCredentials = sameIdentity && s.headers == headers;
      if (sameCredentials && sameIdentity) {
        // Same owner, same token: only the tunables may differ (a caller adjusting the cadence).
        s.config = config;
        ++s.wakeSeq;
        s.cv.notify_all();
        if (outReason) *outReason = "unchanged";
        return true;
      }
      uint64_t lines = 0, batches = 0;
      if (!sameIdentity) {
        discard_all_locked(s, &lines, &batches);
        ++s.ownerEpoch;  // answers still in flight for the previous owner are discarded on arrival
      }
      const bool wasRejected = s.authRejected;
      s.config = config;
      s.host = host;
      s.port = port;
      s.secure = secure;
      s.device = device;
      s.headers = headers;
      s.ownerKey = ownerKey;
      s.credentials = true;
      s.authRejected = false;
      ++s.configGeneration;
      for (auto& h : s.held) {
        if (h.awaitingToken) h.dueUs = 0;  // the new token is what it was waiting for
      }
      reason = std::string(sameIdentity ? "token replaced" : "owner changed") + " auth=" + auth +
               " device=" + device + " -> " + host + ":" + std::to_string(port) +
               (sameIdentity ? "" : " discardedLines=" + std::to_string(lines) + " discardedBatches=" +
                                        std::to_string(batches)) +
               (wasRejected ? " (resumes after 401, held=" + std::to_string(s.held.size()) + ")" : "");
      ++s.wakeSeq;
      s.cv.notify_all();
    } else {
      s.config = config;
      s.host = host;
      s.port = port;
      s.secure = secure;
      s.device = device;
      s.headers = headers;
      s.ownerKey = ownerKey;
      ++s.ownerEpoch;
      s.credentials = true;
      s.authRejected = false;
      s.stopping = false;
      s.running = true;
      ++s.configGeneration;
      s.worker = std::thread(worker_loop);
      reason = std::string("started auth=") + auth + " device=" + device + " -> " + host + ":" +
               std::to_string(port);
    }
  }
  diag("configured " + reason + " identity=" + (config.identity.empty() ? "-" : config.identity));
  if (outReason) *outReason = reason;
  return true;
}

void log_upload_clear_credentials(const char* reason) {
  UploaderState& s = state();
  uint64_t lines = 0, batches = 0;
  {
    std::lock_guard<std::mutex> lk(s.mu);
    if (!s.running) return;
    discard_all_locked(s, &lines, &batches);
    s.credentials = false;
    s.authRejected = false;
    s.headers.clear();
    s.ownerKey.clear();
    ++s.ownerEpoch;  // a late answer for anything sent before the sign-out is discarded
    s.config.sessionToken.clear();
    s.config.hostToken.clear();
    ++s.configGeneration;
    ++s.wakeSeq;
    s.cv.notify_all();
  }
  diag(std::string("credentials cleared (") + (reason ? reason : "-") + ") discardedLines=" +
       std::to_string(lines) + " discardedBatches=" + std::to_string(batches));
}

void log_upload_enqueue(const char* stream, const std::string& line) {
  UploaderState& s = state();
  std::lock_guard<std::mutex> lk(s.mu);
  if (!s.running || s.stopping || line.empty()) return;
  if (!s.credentials) {
    // Nobody to send it as: a line produced while signed out must not ride the next account's
    // token. The disk copy has it.
    ++s.droppedLines;
    return;
  }
  // Drop from the front: when a log is overflowing, the end is the part worth keeping. Held
  // batches count against the same cap so a long 401 stays bounded.
  while (s.queuedBytes + s.heldBytes + line.size() + 1 > s.config.queueCapBytes && !s.queue.empty()) {
    const size_t freed = s.queue.front().text.size() + 1;
    s.queue.pop_front();
    s.queuedBytes = s.queuedBytes > freed ? s.queuedBytes - freed : 0;
    ++s.droppedLines;
  }
  s.queue.push_back(QueuedLine{stream ? stream : "log", line});
  s.queuedBytes += line.size() + 1;
  if (s.queuedBytes >= s.config.batchMaxBytes) s.cv.notify_one();
}

void log_upload_stop() {
  UploaderState& s = state();
  std::thread worker;
  {
    std::lock_guard<std::mutex> lk(s.mu);
    if (!s.running) return;
    s.stopping = true;
    s.stopDeadlineUs = steady_now_us() + kStopDrainBudgetMs * 1000;
    worker = std::move(s.worker);
  }
  s.cv.notify_all();
  if (worker.joinable()) worker.join();
  std::lock_guard<std::mutex> lk(s.mu);
  s.running = false;
  s.stopping = false;
  s.credentials = false;
  s.authRejected = false;
  s.headers.clear();
  s.queue.clear();
  s.queuedBytes = 0;
  s.held.clear();
  s.heldBytes = 0;
  s.sentBatches = s.failedSends = s.retriedBatches = s.discardedBatches = 0;
  s.droppedLines = s.discardedLines = 0;
  s.foreignAnswersDiscarded = 0;
  s.ownerKey.clear();
  s.lastStatus = 0;
  s.lastOkUs = s.lastRejectUs = 0;
  s.lastTransientDiagUs = 0;
  s.workerCycles = 0;
  s.stopDeadlineUs = 0;
}

void log_upload_shutdown() {
  UploaderState& s = state();
  {
    std::lock_guard<std::mutex> lk(s.mu);
    s.shutdown = true;
  }
  // No lock held here: the worker takes s.mu on every cycle, so joining while holding it would
  // hang the process instead of ending it. The caller must likewise not hold a UI lock -- the
  // auth-rejected callback runs on this thread and marshals to the UI with PostMessage.
  log_upload_stop();
  {
    // After the join, not before. Clearing it earlier would not stop a callback already running;
    // once the worker is joined, nothing can start another, so this is when the caller's UI stops
    // being reachable from here.
    std::lock_guard<std::mutex> lk(s.mu);
    s.authRejectedCallback = nullptr;
  }
}

LogUploadShutdown::~LogUploadShutdown() { log_upload_shutdown(); }

bool log_upload_running() {
  UploaderState& s = state();
  std::lock_guard<std::mutex> lk(s.mu);
  return s.running && !s.stopping;
}

LogUploadStatus log_upload_status() {
  UploaderState& s = state();
  std::lock_guard<std::mutex> lk(s.mu);
  LogUploadStatus st;
  st.running = s.running && !s.stopping;
  st.credentials = s.credentials;
  st.authRejected = s.authRejected;
  st.sentBatches = s.sentBatches;
  st.failedSends = s.failedSends;
  st.retriedBatches = s.retriedBatches;
  st.discardedBatches = s.discardedBatches;
  st.droppedLines = s.droppedLines;
  st.discardedLines = s.discardedLines;
  st.heldBatches = s.held.size();
  st.foreignAnswersDiscarded = s.foreignAnswersDiscarded;
  st.lastStatus = s.lastStatus;
  st.lastOkUs = s.lastOkUs;
  st.lastRejectUs = s.lastRejectUs;
  st.workerCycles = s.workerCycles;
  return st;
}

void log_upload_set_auth_rejected_callback(std::function<void()> callback) {
  UploaderState& s = state();
  std::lock_guard<std::mutex> lk(s.mu);
  s.authRejectedCallback = std::move(callback);
}

}  // namespace remote60::native_poc
