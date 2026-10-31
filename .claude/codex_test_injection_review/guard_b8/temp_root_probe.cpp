
#include <windows.h>
#include <cstdio>
#include <string>
int wmain(int argc,wchar_t** argv){
 if(argc!=2)return 2;wchar_t got[32768]{};DWORD n=GetTempPathW(32768,got);if(!n||n>=32768)return 3;
 std::wstring a(got),b(argv[1]); auto slash=[](wchar_t c){return c==92||c==47;}; while(!a.empty()&&slash(a.back()))a.pop_back(); while(!b.empty()&&slash(b.back()))b.pop_back();
 bool ok=_wcsicmp(a.c_str(),b.c_str())==0;
 std::printf("REVIEW Win32_GetTempPath_matches_private_repo_root=%d\n",ok);
 return ok?0:4;
}
