#include "native_video_client_shared_core.hpp"
#include <cstring>
#include <cstdio>
#include <vector>
using namespace remote60::native_poc;
static std::vector<uint8_t> packet(uint32_t seq, bool key) {
  UdpVideoChunkHeader h{};
  h.magic=kMagic; h.kind=static_cast<uint16_t>(UdpPacketKind::VideoChunk); h.size=sizeof(h);
  h.seq=seq; h.codec=static_cast<uint16_t>(UdpCodec::H264); h.flags=key?1:0;
  h.width=2; h.height=2; h.payloadSize=4; h.chunkSize=4; h.chunkStride=4;
  h.chunkIndex=0; h.chunkCount=1; h.streamGeneration=1;
  std::vector<uint8_t> bytes(sizeof(h)+4);
  std::memcpy(bytes.data(),&h,sizeof(h));
  return bytes;
}
int main() {
  UdpH264FrameAssembler a;
  auto first=packet(1,true); auto r1=a.PushDatagram(first.data(),first.size());
  // Sender replaces a queued delta #2 with a new IDR #3. No network loss is needed.
  auto recovery=packet(3,true); auto r3=a.PushDatagram(recovery.data(),recovery.size());
  const bool reproduced=r1.disposition==UdpH264AssemblyDisposition::Completed &&
      r3.disposition==UdpH264AssemblyDisposition::Completed &&
      (r3.frame.header.flags&1) && r3.droppedPreviousIncomplete;
  std::printf("KEY_GAP: completed=%d key=%d droppedPreviousIncomplete=%d networkLoss=0\n",
      r3.disposition==UdpH264AssemblyDisposition::Completed,(r3.frame.header.flags&1)!=0,
      r3.droppedPreviousIncomplete);
  std::printf("Receiver lines 173-176 will call discontinuity handler before decoding this complete key. %s\n",
      reproduced?"REPRODUCED":"NOT REPRODUCED");
  return reproduced?0:1;
}
