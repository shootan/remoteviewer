// Reuse the existing test's real FrameGate rig; do not edit product code.
#define main upstream_frame_gate_tests_main
#include "viewer_frame_gate_test.cpp"
#undef main

int main() {
  int reproduced = 0;
  {
    Rig r; FrameGateInputs in{}; FrameGateLag lag{};
    const uint64_t start = 1000000;
    r.feed(start, start, true, 0, false, &lag, &in);
    r.decoded(in);
    // A real decoder failure enters Congested and asks for one replacement IDR.
    r.fg.note_decode_failure(in, lag);
    const size_t before = r.sink.keyframeReasons.size();
    int dropped = 0;
    for (int n=1; n<=3600; ++n) {
      uint64_t now = start + n * 16667ULL;
      if (r.feed(now,now,false,start,false)==FrameGateVerdict::DropCongested) ++dropped;
    }
    const size_t retry = r.sink.keyframeReasons.size()-before;
    std::printf("CONGESTED: elapsedUs=60001200 Pframes=3600 dropped=%d retryRequests=%zu state=%d\n",
        dropped,retry,static_cast<int>(r.gate.congestionState));
    if (dropped==3600 && retry==0) ++reproduced;
  }
  {
    Rig r; FrameGateInputs in{}; FrameGateLag lag{};
    const uint64_t start=1000000;
    r.feed(start,start,true,0,false,&lag,&in); r.decoded(in);
    // Still screen: synthetics are rendered while the last REAL presented stamp stays old.
    for(int n=1;n<=20;++n) {
      uint64_t now=start+n*100000ULL;
      r.feed(now,now,false,start,false,&lag,&in,true); r.decoded(in);
    }
    // Fresh real frames after idle, with only 6 ms before a 60 Hz renderer can next paint.
    int last=0;
    for(int n=1;n<=3;++n) {
      uint64_t now=start+2000000ULL+n*2000ULL;
      const auto verdict=r.feed(now,now,false,start,false,&lag,&in,false);
      last=static_cast<int>(verdict);
      if(verdict==FrameGateVerdict::Decode) r.decoded(in);
    }
    std::printf("SYNTHETIC_IDLE: realBurstUs=6000 decodedBeforeBurstLag=0 computedLagUs=%llu reason1Requests=%d lastVerdict=%d\n",
        lag.decodeQueueLagEstimateUs,r.sink.requests(1),last);
    if(r.sink.requests(1)==1 && r.gate.congestionState==ClientCongestionState::Congested) ++reproduced;
  }
  std::printf("REPRODUCED %d/2 code paths\n",reproduced);
  return reproduced==2 ? 0 : 1;
}
