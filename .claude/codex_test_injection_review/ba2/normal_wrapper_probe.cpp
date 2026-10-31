
#define NOMINMAX
#include <windows.h>
#include <imm.h>
#include <cstdio>
static DWORD wantedError=0;static int calls=0;static DWORD lastA=0,lastB=0,lastC=0;
static HIMC lastImc=nullptr;static BOOL lastFlag=0;static EXECUTION_STATE lastExec=0;
BOOL fake_attach(DWORD a,DWORD b,BOOL c){++calls;lastA=a;lastB=b;lastFlag=c;SetLastError(wantedError);return 7;}
BOOL fake_open(HIMC h,BOOL b){++calls;lastImc=h;lastFlag=b;SetLastError(wantedError);return FALSE;}
BOOL fake_conversion(HIMC h,DWORD a,DWORD b){++calls;lastImc=h;lastA=a;lastB=b;SetLastError(wantedError);return 9;}
EXECUTION_STATE fake_execution(EXECUTION_STATE f){++calls;lastExec=f;SetLastError(wantedError);return 0;}
#define AttachThreadInput fake_attach
#define ImmSetOpenStatus fake_open
#define ImmSetConversionStatus fake_conversion
#define SetThreadExecutionState fake_execution
#include "os_input_effects.hpp"
#undef AttachThreadInput
#undef ImmSetOpenStatus
#undef ImmSetConversionStatus
#undef SetThreadExecutionState
struct Pattern { BSTR seen=nullptr; HRESULT SetValue(BSTR b){++calls;seen=b;SetLastError(wantedError);return E_ACCESSDENIED;} };
int main(){
 namespace o=remote60::native_poc::os_input;
 static_assert(!o::kRecordOnly);bool ok=true;auto imc=reinterpret_cast<HIMC>(static_cast<uintptr_t>(0x1234));
 wantedError=11;ok&=o::attach_thread_input(2,3,TRUE)==7&&GetLastError()==11&&lastA==2&&lastB==3&&lastFlag==TRUE;
 wantedError=12;ok&=o::imm_set_open_status(imc,FALSE)==0&&GetLastError()==12&&lastImc==imc&&lastFlag==FALSE;
 wantedError=13;ok&=o::imm_set_conversion_status(imc,4,5)==9&&GetLastError()==13&&lastImc==imc&&lastA==4&&lastB==5;
 wantedError=14;ok&=o::set_thread_execution_state(ES_CONTINUOUS)==0&&GetLastError()==14&&lastExec==ES_CONTINUOUS;
 wchar_t text[]=L"review";Pattern p;wantedError=15;
 ok&=o::uia_set_value(&p,text)==E_ACCESSDENIED&&GetLastError()==15&&p.seen==text;
 SetLastError(16);{o::DispatchContext dispatch("input",17,18);}ok&=GetLastError()==16&&calls==5;
 std::printf("REVIEW ba2_normal_wrappers=%s fake_delegate_calls=%d actual_input_power_IME_UIA_writes=0\n",ok?"PASS":"FAIL",calls);
 return ok?0:1;
}
