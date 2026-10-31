
#include <cstdio>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <cstring>
namespace remote60::native_poc {
struct ControlLink {};
int reviewClipboardWrites=0;
bool send_clipboard_update(ControlLink&,uint32_t,const std::u16string&,uint64_t,uint64_t){++reviewClipboardWrites;return true;}
}
uint64_t qpc_now_us();
constexpr unsigned kMsgPushClipboardNow=1;
void PostMessageW(void*,unsigned,int,int){} // record-only review stub
struct ClipPolicy { bool TakeInitialPush(){return false;} };
struct ClipFixture {
 std::atomic<bool> enabled{true},hostSupports{true}; ClipPolicy policy;
 std::mutex mu; bool hasPending=true; std::u16string pendingText=u"review"; uint64_t pendingHash=1; uint32_t nextSeq=0;
};
struct ClipImageFixture { int Pump(remote60::native_poc::ControlLink&){return 0;} bool TakeFallbackText(std::u16string*){return false;} };
struct FileCopyFixture { int Pump(remote60::native_poc::ControlLink&){return 0;} };
struct InputQueueFixture { int count=0; bool Empty(){return count==0;} void DropAllButReleases(uint32_t* d,uint32_t* k){*d=0;*k=0;} };
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

using ClientInputQueue=InputQueueFixture;
using ClipboardSyncState=ClipFixture;
void* SecureZeroMemory(void* p,size_t n){return std::memset(p,0,n);}
inline uint32_t owner_fence_cancel_pending(ClientInputQueue& queue, ClipboardSyncState& clip, std::mutex& credMu,
                                           std::wstring& credPassword, std::atomic<bool>& credRequested) {
  uint32_t dropped = 0, keptReleases = 0;
  queue.DropAllButReleases(&dropped, &keptReleases);
  clip.enabled.store(false, std::memory_order_release);  // no send, no poll from here on
  {
    std::lock_guard<std::mutex> lock(clip.mu);
    if (!clip.pendingText.empty()) SecureZeroMemory(&clip.pendingText[0], clip.pendingText.size() * sizeof(char16_t));
    clip.pendingText.clear();
    clip.hasPending = false;
  }
  credRequested.store(false, std::memory_order_release);
  {
    std::lock_guard<std::mutex> lock(credMu);
    if (!credPassword.empty()) SecureZeroMemory(&credPassword[0], credPassword.size() * sizeof(wchar_t));
    credPassword.clear();
  }
  return dropped;
}


ContextFixture* active=nullptr;
std::mutex credentialMu;
std::wstring credential=L"review-fixture-only";
std::atomic<bool> credentialRequested{true};
bool fencedBeforeSend=false;
uint64_t qpc_now_us(){
 // Scheduling seam: the UI fences after the control thread took the pending text into its local string,
 // before send_clipboard_update starts. The real clock can be pre-empted at the same boundary.
 if(active){
  owner_fence_cancel_pending(active->control.inputQueue,active->control.clipboard,credentialMu,credential,credentialRequested);
  active->session.ownerFenced=true;active->session.inputEnabled=false;
  fencedBeforeSend=true;
 }
 return 1;
}
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

int main(){
 ContextFixture ctx;ctx.session.ownerFenced=false;ctx.session.inputEnabled=true;active=&ctx;
 ControlClient client{ctx};remote60::native_poc::ControlLink recordingLink;
 int result=client.pump_clipboard_sync(recordingLink);
 bool reproduced=fencedBeforeSend&&!ctx.control.clipboard.enabled.load()&&remote60::native_poc::reviewClipboardWrites==1;
 std::printf("REVIEW fence_before_wire=%d sync_enabled=%d pending_shared=%d post_fence_clipboard_writes=%d result=%d\n",
  fencedBeforeSend,ctx.control.clipboard.enabled.load(),ctx.control.clipboard.hasPending,remote60::native_poc::reviewClipboardWrites,result);
 std::puts("REVIEW actual_Host_Viewer_network_input_power_clipboard_APIs=0");
 return reproduced?0:1;
}
