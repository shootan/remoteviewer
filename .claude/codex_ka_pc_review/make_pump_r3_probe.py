from pathlib import Path
import subprocess
s=subprocess.check_output(['git','-C','C:/work/gnlink-wt/ka-pc','show','5a1fb45:apps/native_poc/src/client_shell_main.cpp']).decode('utf-8')
i=s.index('  struct EndGuard {')
j=s.index('  } endGuard{operation};',i)+len('  } endGuard{operation};')
guard=s[i:j]
i=s.index('    if (pumpDone && WaitForSingleObject(pumpDone.get(), 2000)')
j=s.index('\n    }',i)+len('\n    }')
wait=s[i:j]
cpp=r'''// Review only: exact fixed candidate pump EndGuard and timeout branch, with a scheduler seam.
#include <windows.h>
#include <cstdio>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
std::mutex gConnectedMu;
std::set<unsigned long long> gEndedBeforePump;
HANDLE timedOut, pumpFinished;
DWORD review_wait(HANDLE h,DWORD ms){
  const DWORD result=WaitForSingleObject(h,ms);
  if(result==WAIT_TIMEOUT){
    SetEvent(timedOut);
    if(WaitForSingleObject(pumpFinished,5000)!=WAIT_OBJECT_0) ExitProcess(3);
  }
  return result;
}
#define WaitForSingleObject review_wait
void exit_wait(std::shared_ptr<void> pumpDone,unsigned long long operation){
__WAIT__
}
#undef WaitForSingleObject
int main(){
  timedOut=CreateEventW(nullptr,TRUE,FALSE,nullptr);
  pumpFinished=CreateEventW(nullptr,TRUE,FALSE,nullptr);
  std::shared_ptr<void> pumpDone(CreateEventW(nullptr,TRUE,FALSE,nullptr),[](void* h){CloseHandle(h);});
  const unsigned long long operation=17;
  std::thread pump([&]{
    {
__GUARD__
      if(WaitForSingleObject(timedOut,5000)!=WAIT_OBJECT_0) ExitProcess(4);
    } // exact EndGuard has erased the op; the product then signals pumpDone
    SetEvent(pumpDone.get());
    SetEvent(pumpFinished);
  });
  exit_wait(pumpDone,operation);
  pump.join();
  bool retained;
  {std::lock_guard<std::mutex> lock(gConnectedMu);retained=gEndedBeforePump.count(operation)!=0;}
  std::printf("REVIEW pump_done=%d marker_retained_after_pump_end=%d\n",
      WaitForSingleObject(pumpDone.get(),0)==WAIT_OBJECT_0,retained);
  CloseHandle(timedOut);CloseHandle(pumpFinished);
  return retained?0:1; // 0 = reproduced, not product PASS
}
'''.replace('__WAIT__',wait).replace('__GUARD__',guard)
Path('D:/remote/remote/.claude/codex_ka_pc_review/pump_r3_probe.cpp').write_text(cpp,encoding='utf-8',newline='\n')
print('extracted exact EndGuard and timeout branch')
