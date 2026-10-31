
#include <cstdio>
#include <initializer_list>
#include "client_mouse_xbutton_fence.hpp"
namespace n=remote60::native_poc;
struct A {enum class K{InputEvent,Other};K kind=K::InputEvent;struct E{uint16_t kind=0;uint32_t keyCode=5;uint16_t buttons=0;}inputEvent;};
A edge(uint16_t k,uint32_t key=5){A a;a.inputEvent.kind=k;a.inputEvent.keyCode=key;return a;}
void sent_down(n::ClientMouseXPressLedger& l){uint16_t b=8;l.AdmitQueued(true,2,5,&b);auto d=edge(2);l.AdmitSendAction(true,&d);l.OnSentAction(d);}
int main(){
 bool ok=true;int checked=0;bool rejectedUpBug=false;
 for(bool entry:{true,false}){
  n::ClientMouseXPressLedger l;sent_down(l);uint16_t b=8;bool refused;
  if(entry)refused=!l.AdmitQueued(false,2,5,&b);
  else{bool admitted=l.AdmitQueued(true,2,5,&b);auto d=edge(2);refused=admitted&&!l.AdmitSendAction(false,&d);}
  bool owedBefore=l.SendOwed(5);b=9;bool move=l.AdmitQueued(true,1,0,&b)&&b==9;
  b=0;bool upEntry=l.AdmitQueued(true,3,5,&b);auto up=edge(3);bool upSend=l.AdmitSendAction(true,&up);bool owedBeforeAck=l.SendOwed(5);l.OnSentAction(up);bool paid=!l.SendOwed(5);
  bool pass=refused&&owedBefore&&move&&upEntry&&upSend&&owedBeforeAck&&paid;ok&=pass;++checked;
  std::printf("REVIEW owed_%s kept=%d up_entry=%d up_send=%d before_ACK=%d paid_after_ACK=%d PASS=%d\n",entry?"entry":"send",owedBefore,upEntry,upSend,owedBeforeAck,paid,pass);
 }
 {
  n::ClientMouseXPressLedger l;sent_down(l);uint16_t b=0;bool entry=l.AdmitQueued(false,3,5,&b);auto up=edge(3);bool send=l.AdmitSendAction(false,&up);bool kept=l.SendOwed(5);l.Reset();bool reset=!l.SendOwed(5)&&!l.SendDropped(5)&&!l.EntryRefused(5);
  bool pass=!entry&&!send&&kept&&reset;ok&=pass;++checked;std::printf("REVIEW noX_up_refused=%d debt_kept=%d Reset_clears=%d PASS=%d\n",!entry&&!send,kept,reset,pass);
 }
 {
  n::ClientMouseXPressLedger l;uint16_t b=8;bool d=l.AdmitQueued(false,2,5,&b);b=9;bool move=l.AdmitQueued(true,1,0,&b)&&b==1;b=0;bool up=l.AdmitQueued(true,3,5,&b);b=8;bool next=l.AdmitQueued(true,2,5,&b);
  bool pass=!d&&move&&!up&&next;ok&=pass;++checked;std::printf("REVIEW unknown_press_suppressed_until_up=%d\n",pass);
 }
 {
  n::ClientMouseXPressLedger l;uint16_t b=8;bool d=l.AdmitQueued(true,2,5,&b);b=0;bool u=l.AdmitQueued(true,3,5,&b);auto down=edge(2),up=edge(3);bool sd=l.AdmitSendAction(true,&down);l.OnSentAction(down);bool su=l.AdmitSendAction(true,&up);l.OnSentAction(up);
  bool pass=d&&u&&sd&&su&&!l.SendOwed(5);ok&=pass;++checked;std::printf("REVIEW rapid_entry_down_up_before_send=%d\n",pass);
 }

 {
  n::ClientMouseXPressLedger l;uint16_t b=8;
  bool downEntry=l.AdmitQueued(true,2,5,&b); // queued, not sent yet
  b=0;bool upEntry=l.AdmitQueued(false,3,5,&b); // capability changed before queue drained: up is refused
  auto down=edge(2);bool downSend=l.AdmitSendAction(true,&down); // capability back before old down is taken
  if(downSend)l.OnSentAction(down);
  rejectedUpBug=downEntry&&!upEntry&&downSend&&l.SendOwed(5);
  std::printf("REVIEW queued_down_then_refused_up down_entry=%d up_entry=%d later_down_send=%d debt_without_queued_up=%d\n",downEntry,upEntry,downSend,l.SendOwed(5));
 }
 std::printf("REVIEW %d_cases %s actual_Host_Viewer_AVD_JNI_network_input_power_clipboard_APIs=0\n",checked,ok?"PASS":"FAIL");return ok&&rejectedUpBug?0:1;
}
