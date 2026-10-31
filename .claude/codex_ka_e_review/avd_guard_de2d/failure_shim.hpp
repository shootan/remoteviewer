
#include <windows.h>
#include <cstdio>
inline BOOL fake_create(LPCWSTR,LPWSTR,LPSECURITY_ATTRIBUTES,LPSECURITY_ATTRIBUTES,BOOL,DWORD,LPVOID,LPCWSTR,LPSTARTUPINFOW,LPPROCESS_INFORMATION p){
 p->hProcess=(HANDLE)(ULONG_PTR)0x1111;p->hThread=(HANDLE)(ULONG_PTR)0x2222;p->dwProcessId=111;p->dwThreadId=222;
 std::puts("shim: NO child created");return TRUE;
}
inline BOOL fake_assign(HANDLE,HANDLE){std::puts("shim: Assign FAILED");return FALSE;}
inline BOOL fake_terminate(HANDLE,UINT){std::puts("shim: Terminate FAILED");return FALSE;}
inline DWORD fake_wait(HANDLE,DWORD){std::puts("shim: wait TIMED OUT");return WAIT_TIMEOUT;}
#define CreateProcessW fake_create
#define AssignProcessToJobObject fake_assign
#define TerminateProcess fake_terminate
#define WaitForSingleObject fake_wait
