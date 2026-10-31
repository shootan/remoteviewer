import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit
edit(r'D:\remote\remote\apps\native_poc\src\native_video_client_shared_core.hpp', [
("""    uint16_t highWater = 0;
    bool keyFrame = false;  // repairing the keyframe is allowed even while waiting for a keyframe
  };
""",
"""    uint16_t highWater = 0;
    bool keyFrame = false;  // repairing the keyframe is allowed even while waiting for a keyframe
    // When the AU's first datagram arrived (PushDatagram's nowUs; 0 on the legacy overload), so the
    // NACK graces run from the AU's own age rather than from when the scheduler first saw it.
    uint64_t firstPacketUs = 0;
  };
"""),
])
edit(r'D:\remote\remote\apps\native_poc\src\native_video_client_shared_core.cpp', [
("""      info->highWater = static_cast<uint16_t>(highWater);
      info->keyFrame = (a.header.flags & kEncodedFrameFlagKeyFrame) != 0;
""",
"""      info->highWater = static_cast<uint16_t>(highWater);
      info->keyFrame = (a.header.flags & kEncodedFrameFlagKeyFrame) != 0;
      info->firstPacketUs = a.firstPacketUs;
"""),
])
edit(r'D:\remote\remote\apps\native_poc\src\udp_video_nack.hpp', [
("""    if (info.seq != seq_ || info.generation != generation_) {
      // A different AU is now the blocker: restart the round schedule for it.
      seq_ = info.seq;
      generation_ = info.generation;
      firstUs_ = nowUs;
""",
"""    if (info.seq != seq_ || info.generation != generation_) {
      // A different AU is now the blocker: restart the round schedule for it. The graces run from
      // the AU's own first datagram when the assembler stamped it, so a poll that comes late
      // (a busy loop, a 25 ms receive timeout) does not push the tail grace out by that much.
      seq_ = info.seq;
      generation_ = info.generation;
      firstUs_ = (info.firstPacketUs != 0 && info.firstPacketUs <= nowUs) ? info.firstPacketUs : nowUs;
"""),
])
