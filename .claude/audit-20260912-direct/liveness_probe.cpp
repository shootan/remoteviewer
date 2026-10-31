#include <cstdio>
#include "viewer_recv_liveness.hpp"
using namespace remote60::native_poc::viewer;
int main() {
  SessionLivenessConfig cfg;
  SessionLivenessSample s;
  s.nowUs=100000000; s.stage=RecvStage::Recv; s.stageEnterUs=99999000;
  s.controlConnected=true; s.lastDatagramUs=99999000;
  s.lastPublishUs=1000000; s.lastAssembledUs=1000000;
  auto a=evaluate_session_liveness(s,cfg);
  std::printf("control_alive_video_absent_99s stalled=%d silent=%d dead=%d\n",a.recvStalled,a.linkSilent,a.sessionDead);
  s.stage=RecvStage::Decode; s.stageEnterUs=1000000;
  auto b=evaluate_session_liveness(s,cfg);
  std::printf("decode_stalled_99s_control_alive stalled=%d dead=%d\n",b.recvStalled,b.sessionDead);
  s.stage=RecvStage::Recv; s.stageEnterUs=99999000;
  s.controlConnected=false; s.controlGoneSinceUs=0;
  auto c=evaluate_session_liveness(s,cfg);
  std::printf("control_never_observed_connected dead=%d\n",c.sessionDead);
  s.controlGoneSinceUs=90000000;
  auto d=evaluate_session_liveness(s,cfg);
  std::printf("control_gone_10s_video_absent dead=%d\n",d.sessionDead);
  bool ok=!a.sessionDead && !a.linkSilent && b.recvStalled && !b.sessionDead && !c.sessionDead && d.sessionDead;
  std::puts(ok?"PROBE CONFIRMED (policy gaps, not product health PASS)":"PROBE FAILED");
  return ok?0:1;
}
