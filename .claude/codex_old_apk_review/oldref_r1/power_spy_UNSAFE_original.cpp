
#include <windows.h>
#include <cstdio>
static int execs=0,inputs=0,posts=0;
static EXECUTION_STATE WINAPI powerSpy(EXECUTION_STATE x){++execs;return x;}
static UINT WINAPI inputSpy(UINT n,LPINPUT,int){inputs+=n;return n;}
static BOOL WINAPI postSpy(HWND,UINT,WPARAM,LPARAM){++posts;return TRUE;}
#undef PostMessage
#define PostMessage postSpy
#define SendInput inputSpy
#define SetThreadExecutionState powerSpy
#include "host_log.hpp"
int main(){
 {remote60::native_poc::HostPowerKeepalive p(false);p.SetStreaming(true,true);p.SetStreaming(false);}
 printf("OFF exec=%d input=%d post=%d\n",execs,inputs,posts);bool off=execs==0&&inputs==0&&posts==0;
 {remote60::native_poc::HostPowerKeepalive p(true);p.SetStreaming(true,true);p.SetStreaming(true,true);p.SetStreaming(false);}
 printf("ON RECORD ONLY exec=%d input=%d post=%d\n",execs,inputs,posts);return off&&execs==4&&inputs==2&&posts==1?0:1;}
