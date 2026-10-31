// Review-only fixture: actual candidate capacity helper bodies; registry/profile I/O is in-memory.
#include <cstdio>
#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>
namespace remote60::native_poc::shell {
inline int recent_load(const std::string&) { return 0; }
inline std::vector<std::string> recent_for(int,const std::string&,const std::string&) { return {"C","B","A"}; }
}
namespace kept { constexpr size_t kMaxKept=3; }
struct Child { unsigned long long activeOp=0; std::string state="ready"; };
struct Registry {
  std::map<std::string,Child> children;
  std::vector<std::string> HostIds() const { std::vector<std::string> r; for(auto& p:children) r.push_back(p.first); return r; }
  const Child* Find(const std::string& id) const { auto i=children.find(id); return i==children.end()?nullptr:&i->second; }
  void Release(const std::string& id,const std::string&) { children.erase(id); }
};
std::unique_ptr<Registry> gManaged=std::make_unique<Registry>();
struct Slot { int ownerEndEvent=0; int cancelEvent=0; };
std::map<std::string,Slot> gViewerByHost;
std::vector<Slot> gReplacedViewers;
namespace remote60::native_poc::viewer { inline bool viewer_request_cancel(int){return true;} }
void post_host_status(const std::string&,const std::string&,const std::string&){}
std::string gStagingHost;
std::mutex gStateMu;
std::string gAccountId="account";
std::string recent_path(){return {};}
std::string pc_origin(){return {};}
void log_line(const std::string&){}
void post_keep_state(const std::string&,const std::string&){}
std::set<std::string> logical_pcs(const std::string& except = {}) {
  std::set<std::string> out;
  for (const auto& entry : gViewerByHost) out.insert(entry.first);
  if (gManaged)
    for (const auto& id : gManaged->HostIds()) out.insert(id);
  out.erase(gStagingHost);  // r3 (Codex R1): the reserved candidate -- ordinary or kept -- is not one of the three
  out.erase(except);
  return out;
}
bool pc_visible(const std::string& hostId) {
  if (gViewerByHost.count(hostId)) return true;
  const auto* c = gManaged ? gManaged->Find(hostId) : nullptr;
  return c && (c->activeOp != 0 || c->state == "active");
}
std::string standby_victim(const std::string& except) {
  if (!gManaged) return {};
  std::string account;
  { std::lock_guard<std::mutex> lock(gStateMu); account = gAccountId; }
  using namespace remote60::native_poc::shell;
  const std::vector<std::string> recent = recent_for(recent_load(recent_path()), pc_origin(), account);
  std::string victim;
  size_t victimRank = 0;
  for (const auto& id : gManaged->HostIds()) {
    if (id == except || id == gStagingHost || pc_visible(id)) continue;
    size_t rank = recent.size();  // not in the recent list: the oldest
    for (size_t i = 0; i < recent.size(); ++i)
      if (recent[i] == id) rank = i;
    if (victim.empty() || rank > victimRank) { victim = id; victimRank = rank; }
  }
  return victim;
}
void settle_pc_count(const std::string& keep) {
  while (logical_pcs().size() > kept::kMaxKept) {
    const std::string victim = standby_victim(keep);
    if (victim.empty()) return;  // all on screen: nothing to let go (the start was refused if it came to that)
    log_line("kept PCs: making room after host=" + keep + " succeeded, releasing host=" + victim);
    gManaged->Release(victim, "evicted");
    post_keep_state(victim, "none");
  }
}
bool reserve_for_new_pc(const std::string& hostId, std::string* refusal) {
  if (gStagingHost == hostId) return true;                                  // its own reservation (a replacement)
  // r4 (Codex R1): a reservation HOLDS its place until it succeeds or is given back -- even when the PC it would
  // have let go is released meanwhile. So it is counted as a PC here.
  const size_t occupied = logical_pcs(hostId).size() + (gStagingHost.empty() ? 0u : 1u);
  if (occupied < kept::kMaxKept) return true;                               // room without anybody going
  if (!gStagingHost.empty()) { *refusal = "another PC is being opened"; return false; }
  if (standby_victim(hostId).empty()) { *refusal = "three PCs are in use"; return false; }
  gStagingHost = hostId;
  log_line("kept PCs: host=" + hostId + " reserved beside three PCs; one standby goes only when it has connected");
  return true;
}
void release_reservation(const std::string& hostId, const char* why) {
  if (gStagingHost.empty() || gStagingHost != hostId) return;
  gStagingHost.clear();
  log_line(std::string("kept PCs: reservation of host=") + hostId + " released (" + why + ")");
}
void commit_reservation(const std::string& hostId) {
  if (gStagingHost == hostId) {
    gStagingHost.clear();
    log_line("kept PCs: reservation of host=" + hostId + " committed (it connected)");
  }
  settle_pc_count(hostId);
  // r4 (Codex R1): never confirmed as a fourth. If no standby could be let go, this one is ended by its owner (the
  // fence: an established session closes through its own teardown) and the user is told.
  if (logical_pcs().size() > kept::kMaxKept) {
    log_line("kept PCs: host=" + hostId + " would be a fourth PC with none standing by; it is closed");
    const auto o = gViewerByHost.find(hostId);
    if (o != gViewerByHost.end()) {
      (void)remote60::native_poc::viewer::viewer_request_cancel(o->second.ownerEndEvent);
      (void)remote60::native_poc::viewer::viewer_request_cancel(o->second.cancelEvent);
    } else if (gManaged && gManaged->Find(hostId)) {
      gManaged->Release(hostId, "a fourth PC");
      post_keep_state(hostId, "none");
    }
    post_host_status("error", "PC 3대가 이미 열려 있어 이 PC 를 닫았습니다. 쓰지 않는 창을 닫은 뒤 다시 눌러 주세요.", hostId);
  }
}
std::mutex gConnectedMu;
std::set<uint64_t> gEndedBeforePump,gPumpEnded;
void note_pump_end(uint64_t op) {
  if (op == 0) return;
  std::lock_guard<std::mutex> lock(gConnectedMu);
  if (gEndedBeforePump.erase(op) == 0) gPumpEnded.insert(op);  // the exit has not been decided yet
}
void note_exit_decided(uint64_t op) {
  std::lock_guard<std::mutex> lock(gConnectedMu);
  if (gPumpEnded.erase(op) == 0) gEndedBeforePump.insert(op);  // the pump is still reading
}

namespace remote60::native_poc {
struct ControlLink {};
int reviewClipboardWrites=0;
bool send_clipboard_update(ControlLink&,uint32_t,const std::u16string&,uint64_t,uint64_t){++reviewClipboardWrites;return true;}
}
uint64_t qpc_now_us(){return 1;}
constexpr unsigned kMsgPushClipboardNow=1;
void PostMessageW(void*,unsigned,int,int){} // review spy, no OS call
struct ClipPolicy { bool TakeInitialPush(){return false;} };
struct ClipFixture {
 std::atomic<bool> enabled{true},hostSupports{true}; ClipPolicy policy;
 std::mutex mu; bool hasPending=true; std::u16string pendingText=u"review"; uint64_t pendingHash=1; uint32_t nextSeq=0;
};
struct ClipImageFixture { int Pump(remote60::native_poc::ControlLink&){return 0;} bool TakeFallbackText(std::u16string*){return false;} };
struct FileCopyFixture { int Pump(remote60::native_poc::ControlLink&){return 0;} };
struct InputQueueFixture { int count=1; bool Empty(){return count==0;} };
struct UdpFixture { bool TxPending(){return false;} };
struct ControlFixture {
 ClipFixture clipboard;ClipImageFixture clipImage;FileCopyFixture fileCopy;
 InputQueueFixture inputQueue; std::atomic<bool> inputInFlight{false}; UdpFixture udpControl;
};
struct SessionFixture { void* hwnd=nullptr; bool ownerFenced=true; bool inputEnabled=false; };
struct ContextFixture { ControlFixture control; SessionFixture session; };
struct ControlClient {
 ContextFixture& ctx;
 int pump_paste(remote60::native_poc::ControlLink&){return 0;}
 int poll_host_clipboard(remote60::native_poc::ControlLink&,bool){return 0;}
 int pump_clipboard_sync(remote60::native_poc::ControlLink&);
};
int ControlClient::pump_clipboard_sync(remote60::native_poc::ControlLink& link) {
  auto& clip = ctx.control.clipboard;
  if (!clip.enabled.load(std::memory_order_relaxed) ||
      !clip.hostSupports.load(std::memory_order_relaxed)) {
    return 0;
  }
  // Paste on demand first: a key is being held for its answer.
  {
    const int p = pump_paste(link);
    if (p != 0) return p;
  }
  // Once per session, ask the UI thread for whatever is on the clipboard now and send it. This is
  // what makes the machine the user just connected from the source of truth: the host's clipboard
  // may be older, and silently pulling it over the top of a fresh copy is the bug this prevents.
  if (clip.policy.TakeInitialPush() && ctx.session.hwnd) {
    PostMessageW(ctx.session.hwnd, kMsgPushClipboardNow, 0, 0);
  }
  // Clipboard image v1: an image copy's offer / cancel / status exchange, one per idle turn.
  {
    const int r = ctx.control.clipImage.Pump(link);
    if (r != 0) return r;
    // File copy (t-zdmsd4gb): offer / 700 ms paste query / prepare, one per idle turn.
    const int f = ctx.control.fileCopy.Pump(link);
    if (f != 0) return f;
    // Paste on demand: an image the host refused is a failed paste, not a cue to send its text
    // instead -- the user pasted the image, and something else must not arrive in its place (Q3).
    std::u16string fallback;
    (void)ctx.control.clipImage.TakeFallbackText(&fallback);
  }
  // (a) A local clipboard change the UI thread left pending goes first -- the user copying on the
  // viewer expecting to paste on the host is the interactive case, so it should not wait behind the
  // poll.
  std::u16string outText;
  uint64_t outHash = 0;
  uint32_t seq = 0;
  bool haveOutbound = false;
  {
    std::lock_guard<std::mutex> lock(clip.mu);
    if (clip.hasPending) {
      outText = std::move(clip.pendingText);
      outHash = clip.pendingHash;
      seq = ++clip.nextSeq;
      clip.hasPending = false;
      clip.pendingText.clear();
      haveOutbound = true;
    }
  }
  if (haveOutbound) {
    if (!remote60::native_poc::send_clipboard_update(link, seq, outText, outHash, qpc_now_us())) {
      return -1;
    }
    return 1;
  }
  // (b) Otherwise poll the host for a change, on an interval -- the host cannot push, so this is the
  // only way host -> viewer text arrives.
  return poll_host_clipboard(link, false);
}
bool review_failed_release(){
 ContextFixture ctx; int applicationAcks=0;
 do {
    if (!ctx.control.inputQueue.Empty()) ctx.control.inputInFlight.store(true);
    struct InFlightDone {
      std::atomic<bool>* f;
      ~InFlightDone() { f->store(false); }
    } inFlightDone{&ctx.control.inputInFlight};

ctx.control.inputQueue.count=0;
const bool actionOk=false;
if(!actionOk) break;
++applicationAcks;
}while(false);
const bool delivered = ctx.control.inputQueue.Empty() && !ctx.control.inputInFlight.load() &&
                               !ctx.control.udpControl.TxPending();
std::printf("REVIEW failed_last_release app_acks=%d delivered_claim=%d\n",applicationAcks,delivered);
return applicationAcks==0 && delivered;
}

int main(){
 std::string why;
 gManaged->children["A"]={};gViewerByHost["B"]={};gViewerByHost["C"]={};
 bool d=reserve_for_new_pc("D",&why);if(d)gViewerByHost["D"]={};
 release_reservation("A","user");gManaged->Release("A","user");
 bool e=reserve_for_new_pc("E",&why);commit_reservation("D");
 bool r1closed=d&&!e&&logical_pcs().size()==3;
 std::printf("REVIEW previous_R1 reserve_D=%d admit_E=%d logical_PCs=%zu closed=%d\n",d,e,logical_pcs().size(),r1closed);
 gManaged->children.clear();gViewerByHost.clear();gStagingHost.clear();
 gViewerByHost["A"]={};gViewerByHost["B"]={};gViewerByHost["C"]={};
 // Exact replacement registry transition; old A is established and cancel does not end it.
 auto previous=gViewerByHost.find("A");
 gReplacedViewers.push_back(previous->second);
 gViewerByHost.erase(previous);
 // Replacement CreateProcess failed: no new A slot was inserted.
 bool allowD=reserve_for_new_pc("D",&why);if(allowD)gViewerByHost["D"]={};commit_reservation("D");
 size_t actualDistinct=logical_pcs().size()+1; // the fixture's still-open old A
 bool hidden=allowD&&logical_pcs().size()==3&&actualDistinct==4;
 std::printf("REVIEW replaced_live_A=1 admit_D=%d logical_PCs=%zu actual_distinct_PCs=%zu\n",allowD,logical_pcs().size(),actualDistinct);
 bool pump=true;
 for(uint64_t op=1;op<=1000;++op){
  if(op%2){note_pump_end(op);note_exit_decided(op);}else{note_exit_decided(op);note_pump_end(op);}
  pump&=gPumpEnded.empty()&&gEndedBeforePump.empty();
 }
 std::printf("REVIEW pump_exit_both_orders=%s retained=%zu\n",pump?"PASS":"FAIL",gPumpEnded.size()+gEndedBeforePump.size());
 bool badAck=review_failed_release();
 ContextFixture clipCtx;ControlClient client{clipCtx};remote60::native_poc::ControlLink sink;
 int clipResult=client.pump_clipboard_sync(sink);
 bool clipboard=clipCtx.session.ownerFenced&&remote60::native_poc::reviewClipboardWrites==1;
 std::printf("REVIEW fenced=%d input_enabled=%d pending_clipboard_writes=%d result=%d\n",clipCtx.session.ownerFenced,clipCtx.session.inputEnabled,remote60::native_poc::reviewClipboardWrites,clipResult);
 std::puts("REVIEW actual_Host_Viewer_input_power_clipboard_APIs=0");
 return r1closed&&hidden&&pump&&badAck&&clipboard?0:1; // 0 = reproduced findings + closed checks, not app PASS
}
