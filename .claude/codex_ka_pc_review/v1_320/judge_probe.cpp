
#include <cstdio>
#include "ka_pc_e2e_picker_judge.hpp"
namespace k=ka_pc_e2e;
int main(){int n=0,bad=0;auto ck=[&](const char* name,bool ok){++n;bad+=!ok;std::printf("%s %s\n",ok?"PASS":"FAIL",name);};
 const std::wstring root=L"D:/remote/remote/.claude/test-tmp/ka-managed-r3-1/run-33672-611534937/";
 int round=0;for(auto file:{L"A-round1-picker.bmp",L"A-round2-picker.bmp"}){
  k::Image win;bool load=k::load_bmp(root+file,&win);k::Image c=load?k::crop(win,1,win.h-861,1585,win.h-1):k::Image{};
  RECT card=++round==1?RECT{24,78,320,293}:RECT{24,78,268,260};
  ck("real white client rejected",load&&!k::picker_visible_why(c,card).empty());
 }
 k::Image pick;pick.w=600;pick.h=400;pick.px.assign(600*400,k::kPickerBackground);RECT card{24,60,320,290};
 for(int y=60;y<290;y++)for(int x=24;x<320;x++)pick.px[y*600+x]=(x==24||x==319||y==60||y==289)?k::kCardFrame:((x/9+y/7)%3==0?0x1C2230u:((x/9+y/7)%3==1?0x6E7682u:0xECEFF3u));
 ck("synthetic picker accepted",k::picker_visible_why(pick,card).empty());
 ck("picker rejected as video",!k::video_frame_why(pick).empty());
 k::Image presented;bool loaded=k::load_bmp(root+L"A-round1-presented.bmp",&presented);ck("existing video interior accepted",loaded&&k::video_frame_why(presented).empty());
 std::printf("%d checks, %d failed\n",n,bad);return bad?1:0;}
