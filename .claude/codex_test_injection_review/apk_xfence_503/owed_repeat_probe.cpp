
#include <cstdio>
#include "client_mouse_xbutton_fence.hpp"
namespace n=remote60::native_poc;
struct A { enum class K { InputEvent,Other };K kind=K::InputEvent; struct E { uint16_t kind=0;uint32_t keyCode=5;uint16_t buttons=0; } inputEvent; };
A edge(uint16_t kind,uint32_t vk=5){A a;a.inputEvent.kind=kind;a.inputEvent.keyCode=vk;return a;}
void sent_down(n::ClientMouseXPressLedger& l){uint16_t b=8;l.AdmitQueued(true,2,5,&b);auto d=edge(2);l.AdmitSendAction(true,&d);l.OnSentAction(d);}
int main(){
 bool fixed=false,entryBug=false,sendBug=false,repeatBug=false;
 {
  n::ClientMouseXPressLedger l;uint16_t b=8;bool d=l.AdmitQueued(false,2,5,&b);b=9;bool mv=l.AdmitQueued(true,1,0,&b);bool masked=b==1;b=0;bool up=l.AdmitQueued(true,3,5,&b);b=8;bool next=l.AdmitQueued(true,2,5,&b);
  fixed=!d&&mv&&masked&&!up&&next;std::printf("REVIEW original_unknown_transition_closed=%d\n",fixed);
 }
 {
  n::ClientMouseXPressLedger l;sent_down(l);uint16_t b=8;bool repeat=l.AdmitQueued(false,2,5,&b);b=0;bool up=l.AdmitQueued(true,3,5,&b);
  entryBug=!repeat&&!up&&l.SendOwed(5);std::printf("REVIEW sent_down_repeat_rejected_at_entry repeat_accepted=%d supported_up_accepted=%d release_still_owed=%d\n",repeat,up,l.SendOwed(5));
 }
 {
  n::ClientMouseXPressLedger l;sent_down(l);uint16_t b=8;bool entry=l.AdmitQueued(true,2,5,&b);auto repeat=edge(2);bool send=l.AdmitSendAction(false,&repeat);
  bool owed=l.SendOwed(5),dropped=l.SendDropped(5);b=0;bool entryUp=l.AdmitQueued(true,3,5,&b);auto up=edge(3);bool sendUp=entryUp&&l.AdmitSendAction(true,&up);
  sendBug=entry&&!send&&!owed&&dropped&&entryUp&&!sendUp;
  std::printf("REVIEW sent_down_repeat_dropped_at_send owed_after_drop=%d dropped=%d supported_up_accepted=%d\n",owed,dropped,sendUp);
 }
 {
  n::ClientMouseXPressLedger l;uint16_t b=8;l.AdmitQueued(false,2,5,&b);b=8;bool duplicate=l.AdmitQueued(true,2,5,&b);
  repeatBug=duplicate&&!l.EntryRefused(5);std::printf("REVIEW refused_press_repeat_before_up accepted=%d refusal_cleared=%d\n",duplicate,!l.EntryRefused(5));
 }
 std::puts("REVIEW actual_Host_Viewer_AVD_JNI_network_input_power_clipboard_APIs=0");
 return fixed&&entryBug&&sendBug&&repeatBug?0:1; // 0 = original closure and counterexamples confirmed
}
