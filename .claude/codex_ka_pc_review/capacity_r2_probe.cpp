// Review-only fixture: actual candidate capacity helper bodies; registry/profile I/O is in-memory.
#include <cstdio>
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
std::map<std::string,int> gViewerByHost;
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
    for (const auto& id : gManaged->HostIds())
      if (id != gStagingHost) out.insert(id);
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

bool room_for_new_pc(const std::string& hostId) {
  if (logical_pcs(hostId).size() < kept::kMaxKept) return true;
  return !standby_victim(hostId).empty();
}
int main(){
  gManaged->children["A"]={};
  gViewerByHost["B"]=1;
  gViewerByHost["C"]=1;
  const bool d=room_for_new_pc("D");
  if(d) gViewerByHost["D"]=1; // begin_session records an ordinary start; it does not reserve gStagingHost
  const bool e=room_for_new_pc("E");
  if(e) gViewerByHost["E"]=1;
  settle_pc_count("D"); // on_ordinary_connected
  settle_pc_count("E");
  const size_t finalCount=logical_pcs().size();
  std::printf("REVIEW admit_D=%d admit_E=%d final_logical_PCs=%zu standby_A_remaining=%d staging_empty=%d\n",
      d,e,finalCount,gManaged->Find("A")!=nullptr,gStagingHost.empty());
  return d&&e&&finalCount==4?0:1; // 0 = reproduced, not product PASS
}
