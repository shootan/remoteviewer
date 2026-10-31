
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
// Protocol/storage fixtures expose the exact fields used by the fixed gate helpers; no OS or network API.
enum class MessageType { ControlInputEvent,ControlPhysicalKey,ControlInputText,ControlInputAck };
enum class ControlOutboundActionKind { None,InputEvent,InputText,PhysicalKey,WindowSelect };
struct Input { uint16_t kind=0;uint32_t keyCode=0;uint64_t clientSendQpcUs=0; };
struct Physical { bool down=false;uint32_t vk=0,scanCode=0;uint16_t flags=0;uint64_t clientSendQpcUs=0; };
struct ControlInputAckMessage { uint64_t fixture=0; };
struct ControlOutboundAction { ControlOutboundActionKind kind=ControlOutboundActionKind::None;Input inputEvent;Physical physicalKey;MessageType expectedResponseType{};uint16_t expectedResponseSize=0;uint64_t inputGeneratedUs=0; };
struct QueuedControlInputMessage { MessageType type=MessageType::ControlInputEvent;Input inputEvent;Physical physicalKey;uint64_t generatedUs=0; };
class ClientInputQueue {
 mutable std::mutex mu;std::deque<QueuedControlInputMessage> q;
 public:
 void Enqueue(uint16_t kind,uint32_t key){std::lock_guard<std::mutex> l(mu);QueuedControlInputMessage m;m.inputEvent.kind=kind;m.inputEvent.keyCode=key;q.push_back(m);}
 bool TryDequeue(QueuedControlInputMessage* m){std::lock_guard<std::mutex> l(mu);if(q.empty())return false;*m=q.front();q.pop_front();return true;}
 bool Empty()const{std::lock_guard<std::mutex> l(mu);return q.empty();}
};
class ControlLink { public:virtual ~ControlLink()=default;virtual bool Read(void*,size_t)=0;virtual bool Write(const void*,size_t)=0;virtual bool EndMessage()=0;virtual bool Alive()const=0; };
struct OwnerFenceGate {
  mutable std::mutex mu;
  bool fenced = false;           // the owner ended this session: only a release may start a write
  bool releaseExchange = false;  // the exchange the control thread is running is a counted release
  bool inFlight = false;         // an input taken off the queue whose exchange has not ended
  uint64_t taken = 0, acked = 0, failed = 0, skipped = 0;      // input exchanges: started / InputAck / not / not sent
  uint64_t taken0 = 0, acked0 = 0, failed0 = 0, skipped0 = 0;  // as they stood when the fence was raised
};
// The shared core's release rule (native_video_client_shared_core.cpp release_identity), on an outbound action: a key
// up, a mouse button up (left/right/middle), a physical key up.
inline bool action_is_release(const ControlOutboundAction& a) {
  if (a.kind == ControlOutboundActionKind::InputEvent && a.inputEvent.keyCode <= 255) {
    if (a.inputEvent.kind == 6) return true;
    if (a.inputEvent.kind == 3 && (a.inputEvent.keyCode == 1 || a.inputEvent.keyCode == 2 || a.inputEvent.keyCode == 4))
      return true;
  }
  return a.kind == ControlOutboundActionKind::PhysicalKey && !a.physicalKey.down && a.physicalKey.vk <= 255 &&
         a.physicalKey.scanCode <= 255;
}
inline bool action_is_input(const ControlOutboundAction& a) {
  return a.kind == ControlOutboundActionKind::InputEvent || a.kind == ControlOutboundActionKind::InputText ||
         a.kind == ControlOutboundActionKind::PhysicalKey;
}

inline bool gate_is_fenced(const OwnerFenceGate& g) {
  std::lock_guard<std::mutex> lock(g.mu);
  return g.fenced;
}

// The fence: raised once, with its baseline taken in the same critical section.
inline void fence_raise(OwnerFenceGate& g) {
  std::lock_guard<std::mutex> lock(g.mu);
  if (g.fenced) return;
  g.fenced = true;
  g.taken0 = g.taken;
  g.acked0 = g.acked;
  g.failed0 = g.failed;
  g.skipped0 = g.skipped;
}

// Normal path (control thread): `take` runs the scheduler's choice (it may dequeue an input) INSIDE the gate's
// critical section; the result is admitted or refused there too. Refused: a non-release after the fence -- it is
// dropped, never written. Admitted inputs are counted (taken, in flight); a release's exchange may write after a fence.
enum class GateAdmit { None, Admitted, Refused };
template <typename Take>
inline GateAdmit gate_begin_action(OwnerFenceGate& g, ControlOutboundAction* out, Take take) {
  std::lock_guard<std::mutex> lock(g.mu);
  if (!take(out)) return GateAdmit::None;
  const bool input = action_is_input(*out);
  const bool release = input && action_is_release(*out);
  if (g.fenced && !release) {
    g.releaseExchange = false;
    return GateAdmit::Refused;
  }
  g.releaseExchange = release;
  if (input) {
    g.inFlight = true;
    ++g.taken;
  }
  return GateAdmit::Admitted;
}

// Fenced path (control thread): the next queued RELEASE, taken and counted in one critical section. Anything else
// still queued (there should be none: the fence dropped it) is dropped here, never sent. `betweenForTest` (null in
// the product) runs between the dequeue and the count, still inside the critical section -- a test's stall there.
inline bool gate_take_release(OwnerFenceGate& g, ClientInputQueue& queue, uint64_t nowUs, ControlOutboundAction* out,
                              const std::function<void()>* betweenForTest = nullptr) {
  std::lock_guard<std::mutex> lock(g.mu);
  for (;;) {
    QueuedControlInputMessage m{};
    if (!queue.TryDequeue(&m)) return false;
    ControlOutboundAction a{};
    a.expectedResponseType = MessageType::ControlInputAck;
    a.expectedResponseSize = static_cast<uint16_t>(sizeof(ControlInputAckMessage));  // as expected_message_size
    a.inputGeneratedUs = m.generatedUs;
    if (m.type == MessageType::ControlInputEvent) {
      a.kind = ControlOutboundActionKind::InputEvent;
      a.inputEvent = m.inputEvent;
      a.inputEvent.clientSendQpcUs = nowUs;
    } else if (m.type == MessageType::ControlPhysicalKey) {
      a.kind = ControlOutboundActionKind::PhysicalKey;
      a.physicalKey = m.physicalKey;
      a.physicalKey.clientSendQpcUs = nowUs;
    } else {
      continue;  // text: never a release
    }
    if (!action_is_release(a)) continue;  // a down / move / wheel: dropped, never sent
    if (betweenForTest && *betweenForTest) (*betweenForTest)();
    g.releaseExchange = true;
    g.inFlight = true;
    ++g.taken;
    *out = a;
    return true;
  }
}

enum class InputOutcome { Acked, Failed, Skipped };
// The end of an admitted input exchange (control thread).
inline void gate_end_input(OwnerFenceGate& g, InputOutcome o) {
  std::lock_guard<std::mutex> lock(g.mu);
  g.inFlight = false;
  g.releaseExchange = false;
  if (o == InputOutcome::Acked) ++g.acked;
  else if (o == InputOutcome::Failed) ++g.failed;
  else ++g.skipped;
}
inline void gate_end_other(OwnerFenceGate& g) {
  std::lock_guard<std::mutex> lock(g.mu);
  g.releaseExchange = false;
}

// May this control-channel write start now? (FenceGatedLink, under the lock)
inline bool gate_write_allowed(OwnerFenceGate& g) {
  std::lock_guard<std::mutex> lock(g.mu);
  if (!g.fenced) return true;
  if (!g.releaseExchange) return false;
#if defined(REMOTE60_VIEWER_KA_TEST_SEAM)
  // TEST BUILD ONLY (GNLinkViewerKaTest): REMOTE60_VIEWER_TEST_REFUSE_RELEASE_WRITE_AFTER_FENCE=1 -- the FIRST release
  // write after the fence is refused: no byte of it leaves (a write the link refused), so its exchange fails and the
  // host never acknowledges it. Not an after-the-fact flip of a result (r5's hook was that, and proved nothing about
  // delivery).
  {
    static bool armed = [] {
      const char* v = std::getenv("REMOTE60_VIEWER_TEST_REFUSE_RELEASE_WRITE_AFTER_FENCE");
      return v && *v == '1';
    }();
    if (armed) {
      armed = false;
      return false;
    }
  }
#endif
  return true;
}
// May a bulk (clipboard image / file) datagram go now? Never after the fence.
inline bool gate_bulk_send_allowed(OwnerFenceGate& g) { return !gate_is_fenced(g); }

enum class FenceVerdict { Pending, Confirmed, Failed };
// The verdict, all of it read in ONE critical section with the takes and ends above.
inline FenceVerdict fence_verdict(const OwnerFenceGate& g, const ClientInputQueue& queue) {
  std::lock_guard<std::mutex> lock(g.mu);
  if (g.failed > g.failed0) return FenceVerdict::Failed;
  if (!queue.Empty() || g.inFlight) return FenceVerdict::Pending;
  return (g.acked - g.acked0) + (g.skipped - g.skipped0) >= (g.taken - g.taken0) ? FenceVerdict::Confirmed
                                                                                : FenceVerdict::Pending;
}

// A ControlLink whose writes ask the gate first. Follows the link it was given even when that is replaced.
class FenceGatedLink : public ControlLink {
 public:
  FenceGatedLink(std::unique_ptr<ControlLink>& inner, OwnerFenceGate& gate) : inner_(&inner), gate_(gate) {}
  bool Read(void* out, size_t len) override { return *inner_ && (*inner_)->Read(out, len); }
  bool Write(const void* data, size_t len) override {
    if (!gate_write_allowed(gate_)) return false;  // never started
    return *inner_ && (*inner_)->Write(data, len);
  }
  bool EndMessage() override { return *inner_ && (*inner_)->EndMessage(); }
  bool Alive() const override { return *inner_ && (*inner_)->Alive(); }

 private:
  std::unique_ptr<ControlLink>* inner_;
  OwnerFenceGate& gate_;
};


struct RecordingLink : ControlLink { int writes=0; bool Read(void*,size_t)override{return false;}bool Write(const void*,size_t)override{++writes;return true;}bool EndMessage()override{return true;}bool Alive()const override{return true;} };
int main(){
 bool closed=false,drop=false,abort=false,copyBlocked=false;
 { // Prior F1 schedule, now serialized by the actual gate helpers.
  OwnerFenceGate g;ClientInputQueue q;fence_raise(g);q.Enqueue(6,16);
  std::atomic<bool> reached{false};std::function<void()> stall=[&]{reached=true;std::this_thread::sleep_for(std::chrono::milliseconds(100));};
  ControlOutboundAction a;std::thread sender([&]{gate_take_release(g,q,1,&a,&stall);});
  while(!reached)std::this_thread::yield();auto v=fence_verdict(g,q);sender.join();
  gate_end_input(g,InputOutcome::Failed);closed=v==FenceVerdict::Pending&&fence_verdict(g,q)==FenceVerdict::Failed;
  std::printf("REVIEW prior_F1_handoff_closed=%d\n",closed);
 }
 { // Fresh X-button ups, queued AFTER the UI's one-time DropAllButReleases (as the product does).
  OwnerFenceGate g;ClientInputQueue q;fence_raise(g);q.Enqueue(3,5);q.Enqueue(3,6);
  ControlOutboundAction a;bool took=gate_take_release(g,q,1,&a);auto verdict=fence_verdict(g,q);
  drop=!took&&q.Empty()&&verdict==FenceVerdict::Confirmed;
  std::printf("REVIEW X1_X2_ups_taken=%d queue_empty=%d verdict_confirmed=%d host_acks=%llu\n",took,q.Empty(),verdict==FenceVerdict::Confirmed,(unsigned long long)g.acked);
 }
 { // A normal down was admitted before the fence, but no bytes have been written yet.
  OwnerFenceGate g;ClientInputQueue q;ControlOutboundAction down;
  auto a=gate_begin_action(g,&down,[](ControlOutboundAction* out){out->kind=ControlOutboundActionKind::InputEvent;out->inputEvent.kind=5;out->inputEvent.keyCode=65;return true;});
  std::unique_ptr<ControlLink> inner=std::make_unique<RecordingLink>();auto* wire=static_cast<RecordingLink*>(inner.get());FenceGatedLink link(inner,g);
  fence_raise(g);q.Enqueue(6,17); // another key WAS held: its required up is waiting
  bool actionOk=link.Write(&down.inputEvent,sizeof(down.inputEvent));
  gate_end_input(g,actionOk?InputOutcome::Acked:InputOutcome::Failed); // exact caller outcome mapping for !actionOk
  auto verdict=fence_verdict(g,q);
  abort=a==GateAdmit::Admitted&&!actionOk&&wire->writes==0&&!q.Empty()&&verdict==FenceVerdict::Failed;
  std::printf("REVIEW benign_down_refusal bytes_writes=%d held_up_pending=%d verdict_failed=%d\n",wire->writes,!q.Empty(),verdict==FenceVerdict::Failed);
 }

 { // The previously acquired non-release clipboard copy now has a final gated write.
  OwnerFenceGate g;std::unique_ptr<ControlLink> inner=std::make_unique<RecordingLink>();auto* wire=static_cast<RecordingLink*>(inner.get());FenceGatedLink link(inner,g);
  std::string copied="review-fixture";fence_raise(g);bool sent=link.Write(copied.data(),copied.size());
  copyBlocked=!sent&&wire->writes==0;
  std::printf("REVIEW prior_F2_local_copy_blocked=%d writes=%d\n",copyBlocked,wire->writes);
 }
 std::puts("REVIEW actual_Host_Viewer_network_input_power_clipboard_APIs=0");
 return closed&&drop&&abort&&copyBlocked?0:1;
}
