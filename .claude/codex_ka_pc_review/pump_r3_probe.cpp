// Review only: exact fixed candidate pump EndGuard and timeout branch, with a scheduler seam.
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
    if (pumpDone && WaitForSingleObject(pumpDone.get(), 2000) != WAIT_OBJECT_0) {
      std::lock_guard<std::mutex> lock(gConnectedMu);
      gEndedBeforePump.insert(operation);
    }
}
#undef WaitForSingleObject
int main(){
  timedOut=CreateEventW(nullptr,TRUE,FALSE,nullptr);
  pumpFinished=CreateEventW(nullptr,TRUE,FALSE,nullptr);
  std::shared_ptr<void> pumpDone(CreateEventW(nullptr,TRUE,FALSE,nullptr),[](void* h){CloseHandle(h);});
  const unsigned long long operation=17;
  std::thread pump([&]{
    {
  struct EndGuard {
    uint64_t op;
    ~EndGuard() {
      std::lock_guard<std::mutex> lock(gConnectedMu);
      gEndedBeforePump.erase(op);
    }
  } endGuard{operation};
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
