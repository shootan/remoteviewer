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
struct Slot { int ownerEndEvent=0; int cancelEvent=0; std::string hostId; };
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
  // r5 (Codex R1-a): a viewer a replacement kept (it had in fact connected) is that PC, open -- whether or not the
  // replacement started. Counted per PC: two viewers of one PC are one PC.
  for (const auto& r : gReplacedViewers)
    if (!r.hostId.empty()) out.insert(r.hostId);
  if (gManaged)
    for (const auto& id : gManaged->HostIds()) out.insert(id);
  out.erase(gStagingHost);  // r3 (Codex R1): the reserved candidate -- ordinary or kept -- is not one of the three
  out.erase(except);
  return out;
}

// On screen (or being opened): an ordinary viewer, or a kept PC with an activation. Never let go to make room.
bool pc_visible(const std::string& hostId) {
  if (gViewerByHost.count(hostId)) return true;
  for (const auto& r : gReplacedViewers)
    if (r.hostId == hostId) return true;  // r5 (Codex R1-a): a kept-on viewer of this PC is on screen
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


int main(){
 gViewerByHost["A"]={};gViewerByHost["A"].hostId="A";
 gViewerByHost["B"]={};gViewerByHost["C"]={};
 auto previous=gViewerByHost.find("A");gReplacedViewers.push_back(previous->second);gViewerByHost.erase(previous);
 std::string why;bool d=reserve_for_new_pc("D",&why);
 std::printf("REVIEW replaced_A_counted=%d visible=%d logical_PCs=%zu admit_D=%d\n",logical_pcs().count("A")!=0,pc_visible("A"),logical_pcs().size(),d);
 return logical_pcs().size()==3&&pc_visible("A")&&!d?0:1;
}
