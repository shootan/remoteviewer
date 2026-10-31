#include <iostream>
#include <string>
#include <cwctype>
#include "input_macro.hpp"
#include "macro_shell_bridge.hpp"
#include "extracted_flags.hpp"
using namespace remote60::native_poc;
int main() {
  InputMacro m;
  ControlInputEventMessage down{}; down.kind=2; down.keyCode=1; down.buttons=1;
  ControlInputEventMessage up=down; up.kind=3; up.buttons=0;
  m.StartRecording(1000); m.RecordEvent(down,1000); m.RecordEvent(up,2000); m.StopRecording();
  MacroPlaybackOptions opts;
  if(!m.StartPlayback(opts,3000,1)) return 2;
  MacroStep event{};
  bool first=m.PollDueStep(3001,&event);
  bool remoteButtonHeld=first && event.kind==2;
  m.StopPlayback();
  bool producedRelease=m.PollDueStep(5000,&event);
  if(producedRelease && event.kind==3) remoteButtonHeld=false;
  std::cout << "macro cancel after DOWN: first=" << first << " next=" << producedRelease
            << " modeledRemoteButtonHeld=" << remoteButtonHeld << '\n';
  for(const char* name: {"safe", "CON", "CON.txt", "COM9", "LPT9.txt"})
    std::cout << "macro_name_is_valid(" << name << ")=" << macro_name_is_valid(name) << '\n';
  for(const wchar_t* cmd: {L"\"C:\\Downloads\\GNLinkSetup.exe\"",L"\"C:\\Downloads\\GNLinkSetup-signed.exe\"",L"\"C:\\Downloads\\GNLinkSetup.exe\" /something"})
    std::wcout << L"installer gSilent(" << cmd << L")=" << (has_flag(cmd,L"/s")||has_flag(cmd,L"-s")) << L'\n';
  return (remoteButtonHeld && macro_name_is_valid("CON.txt") && has_flag(L"GNLinkSetup-signed.exe",L"-s")) ? 0 : 3;
}
