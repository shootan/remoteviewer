#define NOMINMAX
#include <windows.h>
#include <cstdio>
static DWORD wantedError=0;
static UINT sendResult=0,lastN=0;
static BOOL boolResult=0;
static int calls=0,lastCb=0,lastX=0,lastY=0;
static LPINPUT lastInput=nullptr;
static HWND lastHwnd=nullptr;
static WPARAM lastWp=0;
static LPARAM lastLp=0;
UINT fake_send(UINT n,LPINPUT in,int cb){++calls;lastN=n;lastInput=in;lastCb=cb;SetLastError(wantedError);return sendResult;}
BOOL fake_post(HWND h,UINT,WPARAM w,LPARAM l){++calls;lastHwnd=h;lastWp=w;lastLp=l;SetLastError(wantedError);return boolResult;}
BOOL fake_cursor(int x,int y){++calls;lastX=x;lastY=y;SetLastError(wantedError);return boolResult;}
#define SendInput fake_send
#define PostMessageW fake_post
#define SetCursorPos fake_cursor
#include "os_input_effects_d9.hpp"
#undef SendInput
#undef PostMessageW
#undef SetCursorPos
int main(){
  namespace n=remote60::native_poc::os_input;
  static_assert(!n::kRecordOnly);
  INPUT in[2]{};bool ok=true;
  wantedError=123;sendResult=1;
  auto a=n::send_input(2,in,sizeof(INPUT));auto ea=GetLastError();
  ok&=a==1&&ea==123&&lastN==2&&lastInput==in&&lastCb==sizeof(INPUT);
  wantedError=456;sendResult=0;auto b=n::send_input(2,in,sizeof(INPUT));auto eb=GetLastError();
  ok&=b==0&&eb==456;
  boolResult=7;wantedError=789;
  auto h=reinterpret_cast<HWND>(static_cast<uintptr_t>(0x1234));
  auto c=n::post_message(h,WM_CHAR,81,-7);auto ec=GetLastError();
  ok&=c==7&&ec==789&&lastHwnd==h&&lastWp==81&&lastLp==-7;
  boolResult=0;wantedError=10;auto d=n::set_cursor_pos(-3,4);auto ed=GetLastError();
  ok&=d==0&&ed==10&&lastX==-3&&lastY==4;
  boolResult=1;wantedError=11;auto e=n::set_cursor_pos(8,9);auto ee=GetLastError();
  ok&=e==1&&ee==11;
  SetLastError(12);auto f=n::broker_refused("review");auto ef=GetLastError();
  ok&=!f&&ef==12&&calls==5;
  std::printf("REVIEW default_wrapper_checks=%s api_delegate_calls=%d actual_input_calls=0\n",ok?"PASS":"FAIL",calls);
  return ok?0:1;
}
