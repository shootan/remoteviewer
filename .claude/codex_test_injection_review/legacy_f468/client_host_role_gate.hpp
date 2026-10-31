#pragma once

// What the host is (its pong's capture flags, and whether a pong has said so yet) and the window-list
// request that waits for it -- one state, one transition at a time (prelogin-ui r11, U5). Portable: the
// Android client core shares it (native_video_client_session).
//
// r10 kept them apart -- an atomic "role known", an atomic flags word and an atomic "deferred" -- and the
// two threads raced: the app's RequestWindowList read "not known", the control thread's first pong then set
// it known and found nothing deferred, and only then did the request mark itself deferred and post "waiting
// for the host". No later pong is a first pong, so it waited for ever. And the flags were read before
// "known": an old 0 with a new "known" looked like a logged-on host.
//
// Here the role is ONE word (known in bit 32, the flags below), so a reader sees a pair that was true
// together; and the request's decision, its "deferred" mark and the UI it posts happen under the same lock
// as the first pong's role change, its consumption of that mark and the UI it posts -- whichever comes
// second sees what the first did, and its post is the last.
//
// Lock order: this lock, then (inside the posts) the session's own. Reset takes no lock (the session calls
// it under its own lock, with its worker stopped -- no pong can be in flight).

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>

namespace remote60::native_poc {

class HostRoleGate {
 public:
  struct Role {
    bool known = false;
    uint32_t flags = 0;
  };
  enum class ListRequest { Send, Deferred, NotAtLogonScreen };
  enum class FirstPong { NotFirst, SendList, AnswerNotHere, Quiet };

  Role Load() const {
    const uint64_t v = word_.load(std::memory_order_acquire);
    return Role{(v >> 32) != 0, static_cast<uint32_t>(v)};
  }

  // The app asks for the window list. `post(decision)` runs inside the transition (the UI it shows is
  // ordered with the pong's). `stopPoint` (tests) runs between the role read and the commit.
  ListRequest RequestList(uint32_t logonScreenFlag, const std::function<void(ListRequest)>& post,
                          const std::function<void()>& stopPoint = {}) {
    std::lock_guard<std::mutex> lock(mu_);
    const Role r = Load();
    if (stopPoint) stopPoint();
    const ListRequest d = !r.known                         ? ListRequest::Deferred
                          : (r.flags & logonScreenFlag) != 0 ? ListRequest::NotAtLogonScreen
                                                             : ListRequest::Send;
    if (d == ListRequest::Deferred) deferred_.store(true, std::memory_order_release);
    if (post) post(d);
    return d;
  }

  // A pong arrived with `flags`. The first of the session decides the deferred request: a logged-on host
  // gets the list asked for (also the connect-time one, r9), a host at the logon screen answers a deferred
  // request "not here", or nothing was asked. `post(outcome)` runs inside the transition.
  FirstPong Pong(uint32_t flags, uint32_t logonScreenFlag, const std::function<void(FirstPong)>& post) {
    std::lock_guard<std::mutex> lock(mu_);
    const bool wasKnown = Load().known;
    word_.store((uint64_t{1} << 32) | flags, std::memory_order_release);
    if (wasKnown) return FirstPong::NotFirst;
    const bool deferred = deferred_.exchange(false, std::memory_order_acq_rel);
    const FirstPong o = (flags & logonScreenFlag) == 0 ? FirstPong::SendList
                        : deferred                     ? FirstPong::AnswerNotHere
                                                       : FirstPong::Quiet;
    if (post) post(o);
    return o;
  }

  bool Deferred() const { return deferred_.load(std::memory_order_acquire); }

  void Reset() {
    word_.store(0, std::memory_order_release);
    deferred_.store(false, std::memory_order_release);
  }

 private:
  std::mutex mu_;
  std::atomic<uint64_t> word_{0};
  std::atomic<bool> deferred_{false};
};

}  // namespace remote60::native_poc
