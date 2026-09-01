// See log_upload.hpp.

#include "log_upload.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
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

struct QueuedLine {
  std::string stream;
  std::string text;
};

struct UploaderState {
  std::mutex mu;
  std::condition_variable cv;
  std::deque<QueuedLine> queue;
  size_t queuedBytes = 0;
  uint64_t droppedLines = 0;      // lines the queue could not hold
  uint64_t failedBatches = 0;     // batches the server did not take
  bool running = false;
  bool stopping = false;
  std::thread worker;

  LogUploadConfig config;
  std::string host;
  uint16_t port = 0;
  std::string headers;            // the auth + identity headers, built once
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

/** Sends one stream's worth of lines. Failures are counted, not retried: the disk copy stands. */
void send_batch(UploaderState& s, const std::string& stream, const std::string& body) {
  if (body.empty()) return;
  uint32_t status = 0;
  const std::string headers = s.headers + "x-log-stream: " + stream + "\r\n";
  const bool sent = directory::http_post(s.host, s.port, "/api/logs", "text/plain", headers, body,
                                         &status, nullptr);
  if (!sent || status < 200 || status >= 300) {
    std::lock_guard<std::mutex> lk(s.mu);
    ++s.failedBatches;
  }
}

void worker_loop() {
  UploaderState& s = state();
  for (;;) {
    std::vector<QueuedLine> batch;
    {
      std::unique_lock<std::mutex> lk(s.mu);
      s.cv.wait_for(lk, std::chrono::milliseconds(s.config.flushIntervalMs),
                    [&s] { return s.stopping || s.queuedBytes >= s.config.batchMaxBytes; });
      if (s.queue.empty()) {
        if (s.stopping) return;
        continue;
      }
      size_t taken = 0;
      while (!s.queue.empty() && taken < s.config.batchMaxBytes) {
        taken += s.queue.front().text.size() + 1;
        batch.push_back(std::move(s.queue.front()));
        s.queue.pop_front();
      }
      s.queuedBytes = s.queuedBytes > taken ? s.queuedBytes - taken : 0;
    }

    // Group by stream so each request carries one file's worth of lines; ordinary sessions only
    // ever have one or two streams in flight, so the map costs nothing.
    std::map<std::string, std::string> bodies;
    for (auto& line : batch) {
      std::string& body = bodies[line.stream];
      body.append(line.text);
      body.push_back('\n');
    }
    for (const auto& [stream, body] : bodies) send_batch(s, stream, body);
  }
}

}  // namespace

bool log_upload_start(const LogUploadConfig& config, std::string* outReason) {
  UploaderState& s = state();
  {
    std::lock_guard<std::mutex> lk(s.mu);
    if (s.running) return true;
  }

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
  if (!directory::parse_directory_url(config.directoryUrl, &host, &port, &parseError)) {
    if (outReason) *outReason = parseError;
    return false;
  }

  const std::string device = config.device.empty() ? directory::machine_id() : config.device;

  {
    std::lock_guard<std::mutex> lk(s.mu);
    s.config = config;
    s.host = host;
    s.port = port;
    s.headers = build_headers(config, device);
    s.stopping = false;
    s.running = true;
    s.worker = std::thread(worker_loop);
  }
  if (outReason) *outReason = "device=" + device + " -> " + host + ":" + std::to_string(port);
  return true;
}

void log_upload_enqueue(const char* stream, const std::string& line) {
  UploaderState& s = state();
  std::lock_guard<std::mutex> lk(s.mu);
  if (!s.running || s.stopping || line.empty()) return;

  // Drop from the front: when a log is overflowing, the end is the part worth keeping.
  while (s.queuedBytes + line.size() + 1 > s.config.queueCapBytes && !s.queue.empty()) {
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
    worker = std::move(s.worker);
  }
  s.cv.notify_all();
  if (worker.joinable()) worker.join();
  std::lock_guard<std::mutex> lk(s.mu);
  s.running = false;
}

bool log_upload_running() {
  UploaderState& s = state();
  std::lock_guard<std::mutex> lk(s.mu);
  return s.running && !s.stopping;
}

}  // namespace remote60::native_poc
