#pragma once

// ka-pc-impl r1 (KA ③, proposal_ka_pc_r2.md §3/§11): the private channel between the client shell (GNLinkClient) and
// one managed GNLinkViewer it started for one PC. Two anonymous pipes, one per direction; only the child's ends are
// inherited (the shell's allowlist), so EOF on either side means the other process is gone.
//
// A frame is a 8-byte header -- payload length (u32, little endian), version (u16, kVersion), type (u16) -- and a
// payload of "key=value\n" lines (UTF-8; '%', '\n' and '\r' in a value are %-escaped). A frame longer than
// kMaxPayload, of another version, or that does not parse ends the channel: nothing half-read is acted on.
// Each side reads on its own thread into a bounded queue and writes from its own thread out of a bounded queue, so a
// stuck peer never holds the shell's UI thread, a lease's pump or a logout. A full queue ends the channel too.
//
// The session token travels only in Init / TokenRenewed frames on this channel -- never argv, environment or a log
// line (Codex §4). Every message carries the PC key, the child's incarnation and the owner epoch; the shell drops
// any that does not match what it holds now, and activation-scoped ones carry their opId as well.

#include <windows.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>

namespace remote60::native_poc::managed_ipc {

constexpr uint16_t kVersion = 1;
constexpr uint32_t kMaxPayload = 16 * 1024;
constexpr size_t kMaxQueue = 64;

enum class Type : uint16_t {
  // shell -> viewer
  Init = 1,          // url, session, tokenVersion, hostId, hostName, key, inc, epoch, genDir
  Activate = 2,      // op
  Park = 3,          // op: close the visible session (the same as the window's X)
  Release = 4,       // reason: end the standby (Release the lease) and exit
  TokenRenewed = 5,  // session, tokenVersion
  Shutdown = 6,      // deadlineMs: the shell is going (logout, exit, account change)
  // viewer -> shell
  Hello = 101,         // inc
  State = 102,         // state, lost, entry, reason
  NeedToken = 103,     // tokenVersion (the one it has)
  PickerReady = 104,   // op, ms (since Activate)
  Presented = 105,     // op, ms
  SessionEnded = 106,  // op, rc, reason
  Bye = 107,           // reason
};

struct Message {
  Type type = Type::Init;
  std::map<std::string, std::string> fields;
  std::string get(const std::string& k) const {
    auto it = fields.find(k);
    return it == fields.end() ? std::string() : it->second;
  }
  uint64_t num(const std::string& k) const {
    const std::string v = get(k);
    uint64_t n = 0;
    for (char c : v) {
      if (c < '0' || c > '9') return 0;
      n = n * 10 + static_cast<uint64_t>(c - '0');
    }
    return n;
  }
};

inline std::string escape_value(const std::string& v) {
  std::string out;
  for (char c : v) {
    if (c == '%') out += "%25";
    else if (c == '\n') out += "%0A";
    else if (c == '\r') out += "%0D";
    else out += c;
  }
  return out;
}

inline bool unescape_value(const std::string& v, std::string* out) {
  out->clear();
  for (size_t i = 0; i < v.size(); ++i) {
    if (v[i] != '%') { out->push_back(v[i]); continue; }
    if (i + 2 >= v.size()) return false;
    const std::string h = v.substr(i + 1, 2);
    if (h == "25") out->push_back('%');
    else if (h == "0A") out->push_back('\n');
    else if (h == "0D") out->push_back('\r');
    else return false;
    i += 2;
  }
  return true;
}

inline std::string encode(const Message& m) {
  std::string payload;
  for (const auto& kv : m.fields) payload += kv.first + "=" + escape_value(kv.second) + "\n";
  std::string frame(8, '\0');
  const uint32_t len = static_cast<uint32_t>(payload.size());
  for (int i = 0; i < 4; ++i) frame[i] = static_cast<char>((len >> (8 * i)) & 0xFF);
  frame[4] = static_cast<char>(kVersion & 0xFF);
  frame[5] = static_cast<char>(kVersion >> 8);
  const uint16_t t = static_cast<uint16_t>(m.type);
  frame[6] = static_cast<char>(t & 0xFF);
  frame[7] = static_cast<char>(t >> 8);
  return frame + payload;
}

// Parses a payload into fields; false on anything malformed (a line without '=', an empty key, a bad escape).
inline bool parse_payload(const std::string& payload, Message* m) {
  size_t pos = 0;
  while (pos < payload.size()) {
    const size_t nl = payload.find('\n', pos);
    if (nl == std::string::npos) return false;
    const std::string line = payload.substr(pos, nl - pos);
    const size_t eq = line.find('=');
    if (eq == std::string::npos || eq == 0) return false;
    std::string value;
    if (!unescape_value(line.substr(eq + 1), &value)) return false;
    m->fields[line.substr(0, eq)] = value;
    pos = nl + 1;
  }
  return true;
}

// One side of the channel. Owns its two handles (read end of one pipe, write end of the other).
class Channel {
 public:
  Channel() = default;
  ~Channel() { Close(); }
  Channel(const Channel&) = delete;
  Channel& operator=(const Channel&) = delete;

  void Start(HANDLE readEnd, HANDLE writeEnd) {
    read_ = readEnd;
    write_ = writeEnd;
    open_.store(true);
    reader_ = std::thread([this] { ReadLoop(); });
    writer_ = std::thread([this] { WriteLoop(); });
  }

  bool open() const { return open_.load(); }

  // Queued for the writer thread. false: the channel is closed or its queue is full (it is then closed).
  bool Send(const Message& m) {
    std::string frame = encode(m);
    if (frame.size() - 8 > kMaxPayload) return false;
    {
      std::lock_guard<std::mutex> lock(mu_);
      if (!open_.load()) return false;
      if (out_.size() >= kMaxQueue) {
        broken_ = "send queue full";
        open_.store(false);
        cv_.notify_all();
        return false;
      }
      out_.push_back(std::move(frame));
    }
    cv_.notify_all();
    return true;
  }

  // Waits up to `timeoutMs` for the next message. false: none in time, or the channel is closed and drained.
  bool Receive(Message* m, int timeoutMs) {
    std::unique_lock<std::mutex> lock(mu_);
    cv_.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&] { return !in_.empty() || !open_.load(); });
    if (in_.empty()) return false;
    *m = std::move(in_.front());
    in_.pop_front();
    return true;
  }

  std::string why() const {
    std::lock_guard<std::mutex> lock(mu_);
    return broken_;
  }

  // Ends both threads: the write end is closed (the peer sees EOF), the read end after the reader has stopped.
  //
  // ka-pc-impl r2 (Codex N5): bounded even when the peer stops reading. CancelSynchronousIo only cancels an I/O that
  // is ALREADY pending -- one call made a moment before the writer enters WriteFile finds nothing (ERROR_NOT_FOUND)
  // and the write that starts next blocks for good. So: past the drain deadline `abort_` is set (under mu_), every
  // WriteFile / ReadFile is started only after checking it, and the cancel is REPEATED until the thread says it is
  // done. Each thread can start at most one more I/O after abort is set, and that one is pending (cancellable) until
  // it ends, so the loop ends. A handle is closed only after the thread that used it has stopped -- never under it.
  void Close() {
    open_.store(false);
    cv_.notify_all();
    if (writer_.joinable()) {
      // The writer drains what is queued (a Bye, say) -- for up to a second.
      {
        std::unique_lock<std::mutex> lock(mu_);
        cv_.wait_for(lock, std::chrono::milliseconds(1000), [&] { return writerDone_; });
        abort_ = true;
      }
      cancel_until_done(writer_, &writerDone_);
      writer_.join();
    }
    if (write_) { CloseHandle(write_); write_ = nullptr; }
    if (reader_.joinable()) {
      {
        std::lock_guard<std::mutex> lock(mu_);
        abort_ = true;
      }
      cancel_until_done(reader_, &readerDone_);
      reader_.join();
    }
    if (read_) { CloseHandle(read_); read_ = nullptr; }
  }

 private:
  // Repeats CancelSynchronousIo on `t` until it reports done (see Close).
  void cancel_until_done(std::thread& t, bool* done) {
    for (;;) {
      {
        std::unique_lock<std::mutex> lock(mu_);
        if (cv_.wait_for(lock, std::chrono::milliseconds(10), [&] { return *done; })) return;
      }
      CancelSynchronousIo(t.native_handle());
    }
  }

  bool aborted() {
    std::lock_guard<std::mutex> lock(mu_);
    return abort_;
  }

  bool ReadExact(char* buf, DWORD n) {
    DWORD done = 0;
    while (done < n) {
      DWORD got = 0;
      if (aborted()) return false;
      if (!ReadFile(read_, buf + done, n - done, &got, nullptr) || got == 0) return false;
      done += got;
    }
    return true;
  }

  void ReadLoop() {
    struct Done {
      Channel* c;
      ~Done() {
        std::lock_guard<std::mutex> lock(c->mu_);
        c->readerDone_ = true;
        c->cv_.notify_all();
      }
    } readerDone{this};
    for (;;) {
      char header[8];
      if (!ReadExact(header, 8)) break;
      uint32_t len = 0;
      for (int i = 0; i < 4; ++i) len |= static_cast<uint32_t>(static_cast<uint8_t>(header[i])) << (8 * i);
      const uint16_t version = static_cast<uint16_t>(static_cast<uint8_t>(header[4]) | (static_cast<uint8_t>(header[5]) << 8));
      const uint16_t type = static_cast<uint16_t>(static_cast<uint8_t>(header[6]) | (static_cast<uint8_t>(header[7]) << 8));
      if (version != kVersion || len > kMaxPayload) { SetBroken("bad frame header"); break; }
      std::string payload(len, '\0');
      if (len && !ReadExact(&payload[0], len)) break;
      Message m;
      m.type = static_cast<Type>(type);
      if (!parse_payload(payload, &m)) { SetBroken("bad frame payload"); break; }
      std::lock_guard<std::mutex> lock(mu_);
      if (in_.size() >= kMaxQueue) { broken_ = "receive queue full"; open_.store(false); cv_.notify_all(); break; }
      in_.push_back(std::move(m));
      cv_.notify_all();
    }
    std::lock_guard<std::mutex> lock(mu_);
    if (broken_.empty()) broken_ = "peer closed";
    open_.store(false);
    cv_.notify_all();
  }

  void WriteLoop() {
    struct Done {
      Channel* c;
      ~Done() {
        std::lock_guard<std::mutex> lock(c->mu_);
        c->writerDone_ = true;
        c->cv_.notify_all();
      }
    } done{this};
    for (;;) {
      std::string frame;
      {
        std::unique_lock<std::mutex> lock(mu_);
        cv_.wait(lock, [&] { return !out_.empty() || !open_.load(); });
        if (out_.empty()) return;  // closed and drained
        frame = std::move(out_.front());
        out_.pop_front();
      }
      DWORD done = 0;
      while (done < frame.size()) {
        DWORD put = 0;
        if (aborted()) return;  // past the drain deadline: no new write starts
        if (!WriteFile(write_, frame.data() + done, static_cast<DWORD>(frame.size() - done), &put, nullptr) || put == 0) {
          SetBroken("write failed");
          return;
        }
        done += put;
      }
    }
  }

  void SetBroken(const char* why) {
    std::lock_guard<std::mutex> lock(mu_);
    if (broken_.empty()) broken_ = why;
    open_.store(false);
    cv_.notify_all();
  }

  HANDLE read_ = nullptr;
  HANDLE write_ = nullptr;
  std::atomic<bool> open_{false};
  mutable std::mutex mu_;
  std::condition_variable cv_;
  std::deque<Message> in_;
  std::deque<std::string> out_;
  std::string broken_;
  bool writerDone_ = false;
  bool readerDone_ = false;
  bool abort_ = false;
  std::thread reader_;
  std::thread writer_;
};

// The handle value printed on a command line ("--ipc-read 1234") back to a HANDLE this process holds.
inline HANDLE handle_from_arg(const std::string& v) {
  if (v.empty()) return nullptr;
  char* end = nullptr;
  const unsigned long long n = std::strtoull(v.c_str(), &end, 10);
  if (!end || *end != '\0' || n == 0) return nullptr;
  HANDLE h = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(n));
  DWORD flags = 0;
  return GetHandleInformation(h, &flags) ? h : nullptr;
}

}  // namespace remote60::native_poc::managed_ipc
