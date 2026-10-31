
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>
#include <cstdio>
#include <thread>
#include "viewer_owner_fence.hpp"
#include "input_macro.hpp"
#include "prelogin_input_gate.hpp"
using namespace remote60::native_poc;
using namespace remote60::native_poc::viewer;
struct Wire : ControlLink { int writes=0;bool accept=true;bool Read(void*,size_t)override{return false;}bool Write(const void*,size_t)override{if(accept)++writes;return accept;}bool EndMessage()override{return true;}bool Alive()const override{return true;} };
QueuedControlInputMessage event(ClientInputQueue& q,uint16_t kind,uint32_t vk){return make_control_input_event(q,kind,0,0,0,0,vk,1);}
int main(int argc,char**){
 bool ok=true;
 if(argc>1){
  _putenv_s("REMOTE60_VIEWER_TEST_REFUSE_RELEASE_WRITE_AFTER_FENCE","1");
  OwnerFenceGate g;ClientInputQueue q;fence_raise(g);q.Enqueue(event(q,6,16));ControlOutboundAction a;gate_take_release(g,q,1,&a);
  std::unique_ptr<ControlLink> inner=std::make_unique<Wire>();auto* w=static_cast<Wire*>(inner.get());FenceGatedLink link(inner,g);
  bool wrote=link.Write("r",1);auto why=gate_take_failure(g);gate_end_input(g,outcome_of_failure(why));
  ok=!wrote&&w->writes==0&&why==ExchangeFailure::HookRefused&&fence_verdict(g,q)==FenceVerdict::Failed;
  std::printf("REVIEW release_hook write_calls=%d hook_refused=%d failed=%d\n",w->writes,why==ExchangeFailure::HookRefused,ok);
  return ok?0:1;
 }
 _putenv_s("REMOTE60_VIEWER_TEST_REFUSE_RELEASE_WRITE_AFTER_FENCE","");
 {
  ClientInputQueue q;q.Enqueue(event(q,2,5));q.Enqueue(event(q,3,5));q.Enqueue(event(q,3,6));q.Enqueue(event(q,3,1));
  uint32_t dropped=0,kept=0;q.DropAllButReleases(&dropped,&kept);OwnerFenceGate g;fence_raise(g);ControlOutboundAction a;int x=0,left=0;
  while(gate_take_release(g,q,2,&a)){if(a.inputEvent.keyCode==5||a.inputEvent.keyCode==6)++x;else if(a.inputEvent.keyCode==1)++left;gate_end_input(g,InputOutcome::Acked);}
  bool pass=dropped==1&&kept==3&&x==2&&left==1&&fence_verdict(g,q)==FenceVerdict::Confirmed;ok&=pass;
  std::printf("REVIEW shared_queue_and_fence dropped_down=%u kept=%u X_ups=%d left_up=%d confirmed=%d\n",dropped,kept,x,left,pass);
 }
 {
  ClientInputQueue q;for(int i=0;i<256;++i)q.Enqueue(event(q,5,65));q.Enqueue(event(q,3,5));q.Enqueue(event(q,3,5));q.Enqueue(event(q,3,6));q.Enqueue(event(q,5,66));
  QueuedControlInputMessage m;int x=0,other=0;while(q.TryDequeue(&m)){if(m.inputEvent.kind==3&&(m.inputEvent.keyCode==5||m.inputEvent.keyCode==6))++x;else ++other;}
  ok&=x==2&&other==0;std::printf("REVIEW backpressure X_ups=%d others=%d duplicate_X_up_coalesced=%d\n",x,other,x==2&&other==0);
 }
 {
  ClientInputQueue q;q.Enqueue(event(q,3,5));q.Enqueue(event(q,3,6));q.Enqueue(event(q,5,65));std::this_thread::sleep_for(std::chrono::milliseconds(2100));
  QueuedControlInputMessage m;int x=0,downs=0;while(q.TryDequeue(&m)){if(m.inputEvent.kind==3)++x;else ++downs;}
  ok&=x==2&&downs==0;std::printf("REVIEW expiry_after_2s X_ups=%d expired_downs_sent=%d\n",x,downs);
 }
 {
  OwnerFenceGate g;ClientInputQueue q;ControlOutboundAction down;
  auto admit=gate_begin_action(g,&down,[&](ControlOutboundAction* a){a->kind=ControlOutboundActionKind::InputEvent;a->inputEvent=event(q,5,65).inputEvent;return true;});
  std::unique_ptr<ControlLink> inner=std::make_unique<Wire>();auto* w=static_cast<Wire*>(inner.get());FenceGatedLink link(inner,g);fence_raise(g);q.Enqueue(event(q,6,17));
  bool wrote=link.Write("d",1);auto why=gate_take_failure(g);gate_end_input(g,outcome_of_failure(why));bool pending=fence_verdict(g,q)==FenceVerdict::Pending;
  ControlOutboundAction up;bool sent=gate_take_release(g,q,3,&up)&&link.Write("u",1)&&link.EndMessage();gate_end_input(g,InputOutcome::Acked);FenceTally t;
  bool confirmed=fence_verdict(g,q,&t)==FenceVerdict::Confirmed;
  bool pass=admit==GateAdmit::Admitted&&!wrote&&why==ExchangeFailure::NotStarted&&pending&&sent&&confirmed&&t.acked==1&&t.cancelled==1&&t.failed==0&&w->writes==1;ok&=pass;
  std::printf("REVIEW cancelled_down pending=%d final_confirmed=%d ack=%llu cancelled=%llu failed=%llu PASS=%d\n",pending,confirmed,t.acked,t.cancelled,t.failed,pass);
 }
 {
  OwnerFenceGate g;std::unique_ptr<ControlLink> inner=std::make_unique<Wire>();FenceGatedLink link(inner,g);bool first=link.Write("a",1);fence_raise(g);bool second=link.Write("b",1);auto why=gate_take_failure(g);
  ok&=first&&!second&&why==ExchangeFailure::Broken;std::printf("REVIEW mid_message_refusal broken=%d\n",why==ExchangeFailure::Broken);
 }
 {
  InputMacro macro;std::string script=macro.Serialize()+"3 0 0 0 5 0 0\n";bool loaded=macro.LoadSerialized(script);
  auto steps=macro.Steps();bool roleAllows=prelogin::ViewerMaySendInputEvent(0,3,5);
  std::printf("REVIEW APK_ingress_macro_Xup loaded=%d key=%u normal_role_allows=%d (not an end_to_end APK test)\n",loaded,steps.empty()?0:steps[0].keyCode,roleAllows);
  ok&=loaded&&!steps.empty()&&steps[0].keyCode==5&&roleAllows;
 }
 std::printf("REVIEW overall=%s actual_Host_Viewer_network_input_power_clipboard_APIs=0\n",ok?"PASS":"FAIL");return ok?0:1;
}
