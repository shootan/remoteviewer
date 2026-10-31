// Review-only fixed header with ALL three relevant OS effects redirected to recorders.
#include <windows.h>
#include <cstdio>
static int executionCalls=0,sendCalls=0,mouseEvents=0,broadcasts=0;
EXECUTION_STATE record_execution(EXECUTION_STATE flags){++executionCalls;return ES_CONTINUOUS;}
UINT record_send(UINT n,LPINPUT events,int){++sendCalls;for(UINT i=0;i<n;++i)mouseEvents+=events[i].type==INPUT_MOUSE;return n;}
BOOL record_post(HWND hwnd,UINT msg,WPARAM w,LPARAM l){
  if(hwnd==HWND_BROADCAST&&msg==WM_SYSCOMMAND&&w==SC_MONITORPOWER&&l==-1)++broadcasts;
  return TRUE;
}
#define SetThreadExecutionState record_execution
#define SendInput record_send
#define PostMessageW record_post
#include "host_log_0fb.hpp"
#undef SetThreadExecutionState
#undef SendInput
#undef PostMessageW
int main(){
  int ctorCalls=0;
  {
    remote60::native_poc::HostPowerKeepalive power;
    ctorCalls=executionCalls;
    power.SetStreaming(true,true);
    power.SetStreaming(true,true); // same state: no second wake
    power.SetStreaming(false);
  }
  std::printf("REVIEW ctor_execution_calls=%d total_execution_calls=%d send_input_calls=%d mouse_events=%d monitor_broadcasts=%d actual_OS_calls=0\n",
      ctorCalls,executionCalls,sendCalls,mouseEvents,broadcasts);
  return ctorCalls==1&&executionCalls==4&&sendCalls==1&&mouseEvents==2&&broadcasts==1?0:1;
}
