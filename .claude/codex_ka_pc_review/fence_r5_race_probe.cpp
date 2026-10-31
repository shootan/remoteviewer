#include <atomic>
#include <cstdint>
#include <cstdio>
struct InputExchangeCounters {
  std::atomic<uint64_t> taken{0};   // an input action whose exchange was started
  std::atomic<uint64_t> acked{0};   // ... that ended with the host's InputAck
  std::atomic<uint64_t> failed{0};  // ... that ended any other way (a failed exchange, a wrong reply)
};
struct FenceCounterSnapshot {
  uint64_t taken = 0, acked = 0, failed = 0;
};

inline FenceCounterSnapshot snapshot(const InputExchangeCounters& c) {
  return FenceCounterSnapshot{c.taken.load(), c.acked.load(), c.failed.load()};
}

enum class FenceVerdict { Pending, Confirmed, Failed };

// `atFence`: taken BEFORE the fence queued its releases. Confirmed only when nothing is queued, nothing is in flight,
// and every exchange started since the fence was acknowledged by the host (an exchange already in flight at the fence
// may be acknowledged too, hence >=). Failed as soon as any exchange since the fence did not end in an InputAck.
inline FenceVerdict fence_release_verdict(const FenceCounterSnapshot& atFence, const FenceCounterSnapshot& now,
                                          bool queueEmpty, bool inFlight) {
  if (now.failed > atFence.failed) return FenceVerdict::Failed;
  if (!queueEmpty || inFlight) return FenceVerdict::Pending;
  return (now.acked - atFence.acked) >= (now.taken - atFence.taken) ? FenceVerdict::Confirmed : FenceVerdict::Pending;
}


// A scheduling seam after the timer's atomic read: the sender sets inFlight and dequeues its last release,
// then is paused before taken++. No Host, network, input, window or OS clipboard API.
static bool realInFlight=false,queueEmpty=false,senderAdvanced=false;
struct FlightRead {
 bool load(){
  bool observed=realInFlight;
  senderAdvanced=true; realInFlight=true; queueEmpty=true;
  return observed;
 }
};
struct QueueRead { bool Empty(){return queueEmpty;} };
struct Control { InputExchangeCounters inputExchanges; FlightRead inputInFlight; QueueRead inputQueue; };
struct Session { uint64_t fenceTaken0=0,fenceAcked0=0,fenceFailed0=0; };
struct Context { Control control;Session session; };
int main(){
 Context ctx;
const FenceVerdict v = fence_release_verdict(
            FenceCounterSnapshot{ctx.session.fenceTaken0, ctx.session.fenceAcked0, ctx.session.fenceFailed0},
            snapshot(ctx.control.inputExchanges), ctx.control.inputQueue.Empty(), ctx.control.inputInFlight.load());
 bool reproduced=v==FenceVerdict::Confirmed && realInFlight && ctx.control.inputExchanges.acked.load()==0;
 std::printf("REVIEW verdict_confirmed=%d sender_in_flight=%d sender_dequeued_last=%d app_acks=%llu\n",
  v==FenceVerdict::Confirmed,realInFlight,senderAdvanced,(unsigned long long)ctx.control.inputExchanges.acked.load());
 std::puts("REVIEW actual_Host_Viewer_input_power_clipboard_APIs=0");
 return reproduced?0:1; // 0 = reproduced, not app PASS
}
