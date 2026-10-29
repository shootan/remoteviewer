#pragma once

// ka-pc-impl r1 (KA ③, proposal_ka_pc_r2.md §2/§3/§6/§11, Codex codex_ka_pc_design_r1.md): the client shell's
// managed standby viewers -- one GNLinkViewer per kept PC, each owning that PC's StandbyLease (O1).
//
// Ownership and lifetime (Codex §11):
//  - a child is created SUSPENDED, put in a job of its own (KILL_ON_JOB_CLOSE) and only then resumed; a child that
//    cannot be put in its job is terminated before it ever ran -- it never runs unowned. The job handle stays in the
//    shell (never inherited): when the shell's last handle closes -- also on a crash -- the OS ends the child. One
//    job per child, so ending one PC's viewer never touches the other two.
//  - two pipes per child (managed_viewer_ipc.hpp); the child's ends are the only handles in its inherit list besides
//    its stdout/stderr pipe and NUL, and the shell closes its copies of them after the launch (EOF = the child died).
//  - every message from a child is checked against the child's key, incarnation and the owner epoch it was started
//    under; a child of an older owner (a signed-out account) can say nothing that reaches the current UI.
//  - ending: Release / Shutdown are sent at once; the bounded wait (then TerminateJobObject on its own job) runs on
//    a thread, never on the UI thread. A finished reaper is joined at the next ending (ka-pc-impl r2, Codex N5) -- not
//    only at the shell's exit -- so evicting and preparing PCs all day does not pile up threads.
//
// UI thread only, except where a method says otherwise. Events reach the UI through the `post` callback.

#include <windows.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "managed_viewer_ipc.hpp"

namespace remote60::native_poc::shell {

struct ManagedLaunch {
  std::wstring exe;               // GNLinkViewer.exe
  std::wstring workDir;
  std::wstring baseArgs;          // the ordinary session flags (no token, no host): transport, codec, initial view...
  std::string url;                // the directory
  std::string session;            // the token: sent in Init only
  uint64_t tokenVersion = 0;
  std::string hostId;
  std::string hostName;
  std::string key;                // origin + account + hostId (the PC key)
  std::string genDir;             // where the child keeps this key's generation store
  uint64_t ownerEpoch = 0;
};

struct ManagedEvent {
  std::string hostId;
  uint64_t incarnation = 0;
  uint64_t ownerEpoch = 0;
  bool exited = false;            // the child's process ended (exitCode); no message
  DWORD exitCode = 0;
  managed_ipc::Message message;
};

class ManagedViewers {
 public:
  using Post = std::function<void(std::function<void()>)>;
  using Log = std::function<void(const std::string&)>;
  // Pumps a child's stdout/stderr pipe into the viewer log (the shell's own pump); owns `read`.
  using PumpOutput = std::function<void(HANDLE read, uint32_t pid, const std::string& hostId)>;
  using OnEvent = std::function<void(const ManagedEvent&)>;

  struct Child {
    std::string hostId, hostName, key;
    uint64_t incarnation = 0;
    uint64_t ownerEpoch = 0;
    std::string state = "starting";   // the child's last State ("preparing", "ready", "active", ...)
    std::string reason;
    uint64_t nextOp = 0;
    uint64_t activeOp = 0;            // an Activate sent and its SessionEnded not yet seen
    bool ending = false;              // Release / Shutdown sent
    DWORD pid = 0;
    HANDLE process = nullptr;         // full launch handle (SYNCHRONIZE | TERMINATE are ours anyway)
    HANDLE job = nullptr;             // this child's own job
    std::shared_ptr<managed_ipc::Channel> channel;
    std::thread forwarder;            // channel -> UI events
    std::thread waiter;               // process exit -> UI event
  };

  ManagedViewers(Post post, Log log, PumpOutput pump, OnEvent onEvent)
      : post_(std::move(post)), log_(std::move(log)), pump_(std::move(pump)), onEvent_(std::move(onEvent)) {}
  ~ManagedViewers() { EndAllNow(); }

  const Child* Find(const std::string& hostId) const {
    auto it = children_.find(hostId);
    return it == children_.end() ? nullptr : it->second.get();
  }
  std::vector<std::string> HostIds() const {
    std::vector<std::string> out;
    for (const auto& kv : children_) out.push_back(kv.first);
    return out;
  }
  size_t size() const { return children_.size(); }

  // Starts the managed viewer of one PC. false (+why): nothing is left running.
  bool Launch(const ManagedLaunch& spec, std::string* why) {
    if (children_.count(spec.hostId)) { *why = "already kept"; return false; }
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE toChildRead = nullptr, toChildWrite = nullptr, fromChildRead = nullptr, fromChildWrite = nullptr;
    HANDLE outRead = nullptr, outWrite = nullptr;
    HANDLE nul = INVALID_HANDLE_VALUE;
    auto closeAll = [&] {
      for (HANDLE* h : {&toChildRead, &toChildWrite, &fromChildRead, &fromChildWrite, &outRead, &outWrite})
        if (*h) { CloseHandle(*h); *h = nullptr; }
      if (nul != INVALID_HANDLE_VALUE) { CloseHandle(nul); nul = INVALID_HANDLE_VALUE; }
    };
    if (!CreatePipe(&toChildRead, &toChildWrite, &sa, 1 << 16) || !CreatePipe(&fromChildRead, &fromChildWrite, &sa, 1 << 16) ||
        !CreatePipe(&outRead, &outWrite, &sa, 0)) {
      closeAll();
      *why = "pipes";
      return false;
    }
    // the shell's ends are never inherited
    SetHandleInformation(toChildWrite, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(fromChildRead, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);
    nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
    if (nul == INVALID_HANDLE_VALUE) { closeAll(); *why = "nul"; return false; }

    HANDLE inherit[4] = {outWrite, nul, toChildRead, fromChildWrite};
    SIZE_T attrSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);
    std::vector<char> attrBuf(attrSize);
    auto* attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrBuf.data());
    if (!InitializeProcThreadAttributeList(attrs, 1, 0, &attrSize) ||
        !UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit, sizeof(inherit), nullptr, nullptr)) {
      closeAll();
      *why = "attribute list";
      return false;
    }
    std::wstring cmd = L"\"" + spec.exe + L"\" " + spec.baseArgs + L" --managed-standby --ipc-read " +
                       std::to_wstring(reinterpret_cast<uintptr_t>(toChildRead)) + L" --ipc-write " +
                       std::to_wstring(reinterpret_cast<uintptr_t>(fromChildWrite));
    std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
    mutableCmd.push_back(L'\0');
    STARTUPINFOEXW six{};
    six.StartupInfo.cb = sizeof(six);
    six.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    six.StartupInfo.hStdOutput = outWrite;
    six.StartupInfo.hStdError = outWrite;
    six.StartupInfo.hStdInput = nul;
    six.lpAttributeList = attrs;
    PROCESS_INFORMATION pi{};
    SetEnvironmentVariableW(L"REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE", L"1");
    const BOOL launched = CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, TRUE,
                                         CREATE_SUSPENDED | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr,
                                         spec.workDir.empty() ? nullptr : spec.workDir.c_str(), &six.StartupInfo, &pi);
    const DWORD launchErr = launched ? 0 : GetLastError();
    DeleteProcThreadAttributeList(attrs);
    // the child holds its own copies now (or there is no child): the shell's copies of the child's ends go
    CloseHandle(toChildRead); toChildRead = nullptr;
    CloseHandle(fromChildWrite); fromChildWrite = nullptr;
    CloseHandle(outWrite); outWrite = nullptr;
    CloseHandle(nul); nul = INVALID_HANDLE_VALUE;
    if (!launched) {
      closeAll();
      *why = "launch failed err=" + std::to_string(launchErr);
      return false;
    }
    // A job of its own, before the child ever runs. No job, no child (Codex §11.4).
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    const bool jobbed = job && SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) &&
                        AssignProcessToJobObject(job, pi.hProcess);
    if (!jobbed) {
      TerminateProcess(pi.hProcess, 1);  // suspended: it never ran
      WaitForSingleObject(pi.hProcess, 2000);
      CloseHandle(pi.hThread);
      CloseHandle(pi.hProcess);
      if (job) CloseHandle(job);
      closeAll();
      *why = "could not be put in its job";
      return false;
    }
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);

    auto child = std::make_unique<Child>();
    child->hostId = spec.hostId;
    child->hostName = spec.hostName;
    child->key = spec.key;
    child->incarnation = ++incarnations_;
    child->ownerEpoch = spec.ownerEpoch;
    child->pid = pi.dwProcessId;
    child->process = pi.hProcess;
    child->job = job;
    child->channel = std::make_shared<managed_ipc::Channel>();
    child->channel->Start(fromChildRead, toChildWrite);
    fromChildRead = toChildWrite = nullptr;  // the channel owns them
    if (pump_) {
      HANDLE r = outRead;
      outRead = nullptr;
      pump_(r, pi.dwProcessId, spec.hostId);
    }
    closeAll();

    managed_ipc::Message init;
    init.type = managed_ipc::Type::Init;
    init.fields = {{"url", spec.url}, {"session", spec.session}, {"tokenVersion", std::to_string(spec.tokenVersion)},
                   {"hostId", spec.hostId}, {"hostName", spec.hostName}, {"key", spec.key},
                   {"inc", std::to_string(child->incarnation)}, {"epoch", std::to_string(spec.ownerEpoch)},
                   {"genDir", spec.genDir}};
    child->channel->Send(init);

    // channel -> UI, and process exit -> UI, each on its own thread; both stamp the child's identity
    const std::string hostId = spec.hostId;
    const uint64_t inc = child->incarnation, epoch = spec.ownerEpoch;
    auto channel = child->channel;
    Post post = post_;
    OnEvent onEvent = onEvent_;
    child->forwarder = std::thread([channel, post, onEvent, hostId, inc, epoch] {
      for (;;) {
        managed_ipc::Message m;
        if (!channel->Receive(&m, 500)) {
          if (!channel->open()) return;
          continue;
        }
        ManagedEvent e{hostId, inc, epoch, false, 0, m};
        post([onEvent, e] { onEvent(e); });
      }
    });
    HANDLE waitHandle = nullptr;
    DuplicateHandle(GetCurrentProcess(), pi.hProcess, GetCurrentProcess(), &waitHandle, SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, 0);
    child->waiter = std::thread([waitHandle, post, onEvent, hostId, inc, epoch] {
      if (!waitHandle) return;
      WaitForSingleObject(waitHandle, INFINITE);
      DWORD code = 0;
      GetExitCodeProcess(waitHandle, &code);
      CloseHandle(waitHandle);
      ManagedEvent e{hostId, inc, epoch, true, code, {}};
      post([onEvent, e] { onEvent(e); });
    });
    log_("managed viewer started host=" + spec.hostId + " pid=" + std::to_string(pi.dwProcessId) +
         " inc=" + std::to_string(child->incarnation));
    children_[spec.hostId] = std::move(child);
    return true;
  }

  // An event of the CURRENT child of that PC under the CURRENT owner? (late ones are dropped by the caller)
  Child* Current(const ManagedEvent& e, uint64_t currentEpoch) {
    auto it = children_.find(e.hostId);
    if (it == children_.end()) return nullptr;
    Child* c = it->second.get();
    if (c->incarnation != e.incarnation || c->ownerEpoch != e.ownerEpoch || e.ownerEpoch != currentEpoch) return nullptr;
    if (!e.exited && (e.message.get("key") != c->key || e.message.num("inc") != c->incarnation ||
                      e.message.num("epoch") != c->ownerEpoch))
      return nullptr;
    return c;
  }

  bool Activate(const std::string& hostId, uint64_t* op) {
    Child* c = Mutable(hostId);
    if (!c || c->ending || c->activeOp != 0) return false;
    const uint64_t next = c->nextOp + 1;
    // ka-pc-impl r2 (Codex N6): an Activate that could not be sent leaves no activation behind (no activeOp that
    // nothing will ever end).
    if (!c->channel->Send(Stamp(*c, managed_ipc::Type::Activate, {{"op", std::to_string(next)}}))) return false;
    c->nextOp = next;
    c->activeOp = next;
    *op = next;
    return true;
  }

  void TokenRenewed(const std::string& session, uint64_t version) {
    for (auto& kv : children_)
      kv.second->channel->Send(Stamp(*kv.second, managed_ipc::Type::TokenRenewed,
                                     {{"session", session}, {"tokenVersion", std::to_string(version)}}));
  }

  // One PC's viewer: Release (its lease goes), then the bounded wait and, if needed, its job is ended -- off the UI.
  void Release(const std::string& hostId, const std::string& reason) {
    auto it = children_.find(hostId);
    if (it == children_.end()) return;
    std::unique_ptr<Child> c = std::move(it->second);
    children_.erase(it);
    End(std::move(c), managed_ipc::Type::Release, reason);
  }

  // Every child (sign-out, account change, shell exit). The caller has ALREADY moved the owner epoch on (Codex: fence
  // first): what these children say from now on is dropped by Current(). Returns at once; the waits run elsewhere.
  void EndAll(const std::string& reason) {
    std::map<std::string, std::unique_ptr<Child>> snapshot;
    snapshot.swap(children_);
    for (auto& kv : snapshot) End(std::move(kv.second), managed_ipc::Type::Shutdown, reason);
  }

  // Shell exit: the reapers are joined, each bounded by its own wait -- the process ends right after.
  void JoinReapers() {
    std::vector<Reaper> reapers;
    {
      std::lock_guard<std::mutex> lock(reapMu_);
      reapers.swap(reapers_);
    }
    for (auto& r : reapers)
      if (r.thread.joinable()) r.thread.join();
  }

  // How many reaper threads are held (finished ones included until the next ending joins them). For tests.
  size_t ReaperCount() {
    std::lock_guard<std::mutex> lock(reapMu_);
    return reapers_.size();
  }

  void Forget(const std::string& hostId) {  // the child exited on its own: its handles go
    auto it = children_.find(hostId);
    if (it == children_.end()) return;
    std::unique_ptr<Child> c = std::move(it->second);
    children_.erase(it);
    End(std::move(c), managed_ipc::Type::Shutdown, "exited");
  }

 private:
  Child* Mutable(const std::string& hostId) {
    auto it = children_.find(hostId);
    return it == children_.end() ? nullptr : it->second.get();
  }

  static managed_ipc::Message Stamp(const Child& c, managed_ipc::Type t, std::map<std::string, std::string> f) {
    managed_ipc::Message m;
    m.type = t;
    m.fields = std::move(f);
    m.fields["key"] = c.key;
    m.fields["inc"] = std::to_string(c.incarnation);
    m.fields["epoch"] = std::to_string(c.ownerEpoch);
    return m;
  }

  // ka-pc-impl r2: the child's own teardown (owner fence, WM_CLOSE, its session's bounded joins of 3 s + 3 s, the
  // lease's Release) is given this long before its job is ended.
  static constexpr DWORD kEndMs = 8000;

  void End(std::unique_ptr<Child> c, managed_ipc::Type how, const std::string& reason) {
    c->ending = true;
    c->channel->Send(Stamp(*c, how, {{"reason", reason}, {"deadlineMs", std::to_string(kEndMs)}}));
    log_("managed viewer ending host=" + c->hostId + " pid=" + std::to_string(c->pid) + " reason=" + reason);
    Child* raw = c.release();
    std::lock_guard<std::mutex> lock(reapMu_);
    // Finished reapers first (Codex N5): joined now, not at the shell's exit.
    for (auto it = reapers_.begin(); it != reapers_.end();) {
      if (it->done->load()) {
        if (it->thread.joinable()) it->thread.join();
        it = reapers_.erase(it);
      } else {
        ++it;
      }
    }
    auto done = std::make_shared<std::atomic<bool>>(false);
    Reaper r;
    r.done = done;
    r.thread = std::thread([raw, log = log_, done] {
      std::unique_ptr<Child> owned(raw);
      const DWORD w = WaitForSingleObject(owned->process, kEndMs);
      if (w != WAIT_OBJECT_0) {
        TerminateJobObject(owned->job, 1);  // its own job: this child only
        WaitForSingleObject(owned->process, 2000);
        log("managed viewer did not end in " + std::to_string(kEndMs) + " ms; its job was ended host=" + owned->hostId);
      }
      owned->channel->Close();
      if (owned->forwarder.joinable()) owned->forwarder.join();
      if (owned->waiter.joinable()) owned->waiter.join();
      CloseHandle(owned->process);
      CloseHandle(owned->job);
      owned.reset();
      done->store(true);
    });
    reapers_.push_back(std::move(r));
  }

  void EndAllNow() {
    EndAll("shell gone");
    JoinReapers();
  }

  Post post_;
  Log log_;
  PumpOutput pump_;
  OnEvent onEvent_;
  uint64_t incarnations_ = 0;
  std::map<std::string, std::unique_ptr<Child>> children_;
  struct Reaper {
    std::thread thread;
    std::shared_ptr<std::atomic<bool>> done;
  };
  std::mutex reapMu_;
  std::vector<Reaper> reapers_;
};

}  // namespace remote60::native_poc::shell
