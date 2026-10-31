from pathlib import Path
import re,subprocess
source=subprocess.check_output(['git','-C','C:/work/gnlink-wt/ka-pc','show','5a1fb45:apps/native_poc/src/client_shell_main.cpp']).decode('utf-8')
fixture=Path('D:/remote/remote/.claude/codex_ka_pc_review/capacity_r2_probe.cpp').read_text(encoding='utf-8')
prefix=fixture[:fixture.index('std::set<std::string> logical_pcs(')]
names=('logical_pcs','pc_visible','standby_victim','settle_pc_count','reserve_for_new_pc','release_reservation','commit_reservation')
methods=[]
for name in names:
    m=re.search(r'^(?:std::set<std::string>|std::string|bool|void) '+name+r'\([^\n]*\) \{',source,re.M)
    assert m,name
    methods.append(source[m.start():source.index('\n}',m.start())+2])
driver=r'''
int main(){
  gManaged->children["A"]={};
  gViewerByHost["B"]=1;
  gViewerByHost["C"]=1;
  std::string why;
  const bool d=reserve_for_new_pc("D",&why);
  if(d) gViewerByHost["D"]=1;
  // release_kept_by_user(A): its own reservation is released, then its child is removed.
  release_reservation("A","released by the user");
  gManaged->Release("A","user");
  const bool e=reserve_for_new_pc("E",&why);
  if(e) gViewerByHost["E"]=1;
  commit_reservation("D");
  commit_reservation("E");
  const size_t count=logical_pcs().size();
  std::printf("REVIEW reserve_D=%d release_A=1 admit_E=%d final_logical_PCs=%zu staging_empty=%d\n",
      d,e,count,gStagingHost.empty());
  return d&&e&&count==4?0:1; // 0 = reproduced, not product PASS
}
'''
Path('D:/remote/remote/.claude/codex_ka_pc_review/capacity_r3_probe.cpp').write_text(prefix+'\n\n'.join(methods)+driver,encoding='utf-8',newline='\n')
print('extracted fixed candidate capacity helpers:',len(methods))
