import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit

HPP = r'D:\remote\remote\apps\native_poc\src\native_video_client_shared_core.hpp'
CPP = r'D:\remote\remote\apps\native_poc\src\native_video_client_shared_core.cpp'

edit(HPP, [
(
"""enum class UdpH264AssemblyDisposition : uint8_t {
  Ignored = 0,
  Partial,
  Completed,
  Malformed,
  Dropped,
};
""",
"""enum class UdpH264AssemblyDisposition : uint8_t {
  Ignored = 0,
  Partial,
  Completed,
  Malformed,
  Dropped,
  // In-order hold on (ConfigureInOrderHold): this datagram completed its AU, which now waits in
  // sequence order for PopDelivery instead of being handed out here. (Windows NACK wiring.)
  Queued,
};
"""),
(
"""class UdpH264FrameAssembler {
 public:
  void Reset();
  UdpH264AssemblyStepResult PushDatagram(const uint8_t* data, size_t len);
""",
"""class UdpH264FrameAssembler {
 public:
  void Reset();
  UdpH264AssemblyStepResult PushDatagram(const uint8_t* data, size_t len);
  // Same, stamping the AU's first arrival with `nowUs` (the in-order hold clock below).
  UdpH264AssemblyStepResult PushDatagram(const uint8_t* data, size_t len, uint64_t nowUs);

  // --- In-order delivery hold (video NACK) ---
  // Without it a completed AU is delivered the moment it completes and every OLDER incomplete AU
  // is discarded with it (the seq gap then reads as loss). At 60 fps the next P completes ~16 ms
  // after a lost chunk, before a NACK round (25 ms grace) can bring the chunk back -- so NACK only
  // ever helped a sparse (static-screen) stream. With maxHoldUs > 0, PushDatagram returns Queued
  // for a completed AU and PopDelivery hands AUs out in sequence order: a completed AU waits behind
  // an older incomplete one for up to maxHoldUs (its retransmit may still land); past that the
  // older AU is dropped and delivery resumes carrying droppedPreviousIncomplete. maxHoldUs = 0
  // keeps the legacy immediate delivery (the Android path is unchanged). `maxConcurrent` bounds
  // the assemblies held (legacy 3; a hold of ~120 ms at 60 fps needs ~8).
  void ConfigureInOrderHold(uint64_t maxHoldUs, size_t maxConcurrent);
  bool InOrderHoldEnabled() const { return holdMaxUs_ > 0; }
  // Deliver the next AU in sequence order if one is ready, or if the incomplete AU ahead of it has
  // outlived its hold. `repairNonKey` false: an IDR is awaited, so an incomplete NON-key head is
  // not worth waiting for and is released at once (an incomplete keyframe still is -- it is the
  // only recovery point). Call after every datagram and on every receive timeout, until false.
  bool PopDelivery(uint64_t nowUs, bool repairNonKey, UdpH264AssemblyStepResult* out);
  size_t PendingCount() const { return assemblies_.size(); }
"""),
(
"""    std::vector<uint8_t> parityReceived;
    // Which layout this frame's parity uses, learned from the first parity packet to arrive.
    uint8_t parityInterleaved = 0;
  };

  std::deque<Assembly> assemblies_;
  bool deliveredAny_ = false;
  uint32_t lastDeliveredSeq_ = 0;
};
""",
"""    std::vector<uint8_t> parityReceived;
    // Which layout this frame's parity uses, learned from the first parity packet to arrive.
    uint8_t parityInterleaved = 0;
    // In-order hold bookkeeping: when the first datagram of this AU arrived (the hold clock), whether
    // every data chunk is in (awaiting PopDelivery), and how many chunks parity repaired.
    uint64_t firstPacketUs = 0;
    bool complete = false;
    uint32_t fecRecoveredChunks = 0;
  };

  UdpH264AssemblyStepResult DeliverAssembly(Assembly& assembly);

  std::deque<Assembly> assemblies_;
  bool deliveredAny_ = false;
  uint32_t lastDeliveredSeq_ = 0;
  uint64_t holdMaxUs_ = 0;      // 0 = legacy immediate delivery
  size_t maxConcurrent_ = 3;    // kMaxConcurrentVideoAssemblies unless ConfigureInOrderHold raised it
};
"""),
])

edit(CPP, [
(
"""constexpr size_t kMaxConcurrentVideoAssemblies = 3;

""",
""""""),
(
"""void UdpH264FrameAssembler::Reset() {
  assemblies_.clear();
  deliveredAny_ = false;
  lastDeliveredSeq_ = 0;
}
""",
"""void UdpH264FrameAssembler::Reset() {
  assemblies_.clear();
  deliveredAny_ = false;
  lastDeliveredSeq_ = 0;
}

void UdpH264FrameAssembler::ConfigureInOrderHold(uint64_t maxHoldUs, size_t maxConcurrent) {
  holdMaxUs_ = maxHoldUs;
  maxConcurrent_ = std::max<size_t>(1, maxConcurrent);
}

// Hand one assembled AU out: the completion block PushDatagram used to run inline, so the legacy
// immediate path and the in-order hold path deliver byte-identical results.
UdpH264AssemblyStepResult UdpH264FrameAssembler::DeliverAssembly(Assembly& assembly) {
  UdpH264AssemblyStepResult result{};
  result.packetSeq = assembly.seq;
  result.expectedSeq = deliveredAny_ ? lastDeliveredSeq_ + 1u : assembly.seq;
  assembly.header.payloadSize = assembly.payloadSize;
  result.disposition = UdpH264AssemblyDisposition::Completed;
  result.frame.header = assembly.header;
  result.frame.payload = std::move(assembly.payload);
  result.fecRecovered = assembly.fecRecoveredChunks > 0;
  result.fecRecoveredChunks = assembly.fecRecoveredChunks;
  if (deliveredAny_ && assembly.seq != lastDeliveredSeq_ + 1u) {
    result.droppedPreviousIncomplete = true;
  }
  deliveredAny_ = true;
  lastDeliveredSeq_ = assembly.seq;
  assemblies_.erase(std::remove_if(assemblies_.begin(), assemblies_.end(),
                                   [&](const Assembly& item) {
                                     return !sequence_is_newer(item.seq, lastDeliveredSeq_);
                                   }),
                    assemblies_.end());
  return result;
}

bool UdpH264FrameAssembler::PopDelivery(uint64_t nowUs, bool repairNonKey,
                                        UdpH264AssemblyStepResult* out) {
  if (!out) return false;
  while (!assemblies_.empty()) {
    // Sequence order, not arrival order: a reordered network can start a newer AU first.
    auto oldest = std::min_element(assemblies_.begin(), assemblies_.end(),
                                   [](const Assembly& a, const Assembly& b) {
                                     return sequence_is_newer(b.seq, a.seq);
                                   });
    if (oldest->complete) {
      *out = DeliverAssembly(*oldest);
      return true;
    }
    const bool anyComplete = std::any_of(assemblies_.begin(), assemblies_.end(),
                                         [](const Assembly& a) { return a.complete; });
    if (!anyComplete) return false;  // nothing behind it to release anyway
    const bool keyFrame = (oldest->header.flags & kEncodedFrameFlagKeyFrame) != 0;
    const bool worthHolding = (repairNonKey || keyFrame) &&
                              holdMaxUs_ > 0 && nowUs < oldest->firstPacketUs + holdMaxUs_;
    if (worthHolding) return false;  // its retransmit may still land
    // Hold expired (or not worth it): give the AU up. The next delivery then carries the seq gap
    // as droppedPreviousIncomplete, exactly as the legacy path reported it.
    assemblies_.erase(oldest);
  }
  return false;
}
"""),
(
"""UdpH264AssemblyStepResult UdpH264FrameAssembler::PushDatagram(const uint8_t* data, size_t len) {
  UdpH264AssemblyStepResult result{};
  if (!data || len < sizeof(UdpVideoChunkHeader)) return result;
""",
"""UdpH264AssemblyStepResult UdpH264FrameAssembler::PushDatagram(const uint8_t* data, size_t len) {
  return PushDatagram(data, len, 0);
}

UdpH264AssemblyStepResult UdpH264FrameAssembler::PushDatagram(const uint8_t* data, size_t len,
                                                              uint64_t nowUs) {
  UdpH264AssemblyStepResult result{};
  if (!data || len < sizeof(UdpVideoChunkHeader)) return result;
"""),
(
"""  if (assemblyIt == assemblies_.end()) {
    if (assemblies_.size() >= kMaxConcurrentVideoAssemblies) {
      assemblies_.pop_front();
      result.droppedPreviousIncomplete = true;
    }
    Assembly created{};
    created.seq = packet.seq;
""",
"""  if (assemblyIt == assemblies_.end()) {
    if (assemblies_.size() >= maxConcurrent_) {
      if (holdMaxUs_ > 0) {
        // The oldest by sequence is the stuck head (complete ones were already popped).
        assemblies_.erase(std::min_element(assemblies_.begin(), assemblies_.end(),
                                           [](const Assembly& a, const Assembly& b) {
                                             return sequence_is_newer(b.seq, a.seq);
                                           }));
      } else {
        assemblies_.pop_front();
      }
      result.droppedPreviousIncomplete = true;
    }
    Assembly created{};
    created.seq = packet.seq;
    created.firstPacketUs = nowUs;
"""),
(
"""    assembly.received[missingIndex] = 1;
    ++assembly.receivedCount;
    result.fecRecovered = true;
    ++result.fecRecoveredChunks;
  }
""",
"""    assembly.received[missingIndex] = 1;
    ++assembly.receivedCount;
    result.fecRecovered = true;
    ++result.fecRecoveredChunks;
    ++assembly.fecRecoveredChunks;
  }
"""),
(
"""  if (assembly.receivedCount != assembly.chunkCount) {
    result.disposition = UdpH264AssemblyDisposition::Partial;
    return result;
  }

  assembly.header.payloadSize = assembly.payloadSize;
  result.disposition = UdpH264AssemblyDisposition::Completed;
  result.frame.header = assembly.header;
  result.frame.payload = std::move(assembly.payload);
  if (deliveredAny_ && packet.seq != lastDeliveredSeq_ + 1u) {
    result.droppedPreviousIncomplete = true;
  }
  deliveredAny_ = true;
  lastDeliveredSeq_ = packet.seq;
  assemblies_.erase(std::remove_if(assemblies_.begin(), assemblies_.end(),
                                   [&](const Assembly& item) {
                                     return !sequence_is_newer(item.seq, lastDeliveredSeq_);
                                   }),
                    assemblies_.end());
  return result;
}
""",
"""  if (assembly.receivedCount != assembly.chunkCount) {
    result.disposition = UdpH264AssemblyDisposition::Partial;
    return result;
  }

  if (holdMaxUs_ > 0) {
    // In-order hold: complete, but PopDelivery decides when it goes out.
    assembly.complete = true;
    result.disposition = UdpH264AssemblyDisposition::Queued;
    return result;
  }
  const bool droppedByEviction = result.droppedPreviousIncomplete;
  const bool fecRecovered = result.fecRecovered;
  const uint32_t fecRecoveredChunks = result.fecRecoveredChunks;
  result = DeliverAssembly(assembly);
  result.packetChunkOffset = packet.chunkOffset;
  result.droppedPreviousIncomplete = result.droppedPreviousIncomplete || droppedByEviction;
  result.fecRecovered = fecRecovered;
  result.fecRecoveredChunks = fecRecoveredChunks;
  return result;
}
"""),
])
