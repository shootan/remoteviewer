from pathlib import Path
import re
source=Path('C:/work/gnlink-wt/ka-pc/apps/native_poc/src/client_shell_main.cpp').read_text(encoding='utf-8')
names=('logical_pcs','pc_visible','standby_victim','settle_pc_count','room_for_new_pc')
methods=[]
for name in names:
    m=re.search(r'^(?:std::set<std::string>|std::string|bool|void) '+name+r'\([^\n]*\) \{',source,re.M)
    assert m,name
    methods.append(source[m.start():source.index('\n}',m.start())+2])
cpp=r'''// Review-only fixture: actual candidate capacity helper bodies; registry/profile I/O is in-memory.
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
__METHODS__
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
'''.replace('__METHODS__','\n\n'.join(methods))
Path('D:/remote/remote/.claude/codex_ka_pc_review/capacity_r2_probe.cpp').write_text(cpp,encoding='utf-8',newline='\n')
print('extracted actual helper bodies:',len(methods))
