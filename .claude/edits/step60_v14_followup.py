import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit
S = r'D:\remote\remote\apps\native_poc\src'
NL = chr(92) + 'n'

# --- assembler: a bounded tombstone for the identity the caller gave up on ----------------------
edit(S + r'\native_video_client_shared_core.hpp', [
("  // Gives up exactly the incomplete assembly (generation, seq) the caller judged -- never an\n"
 "  // \"oldest\" by arrival or sequence order -- because its repair is over (A04 rule). False, and\n"
 "  // nothing removed, when it is not held or completed meanwhile (the give-up is cancelled). The\n"
 "  // next delivery reports the seq gap exactly as an expired hold does.\n"
 "  bool GiveUpIncomplete(uint64_t generation, uint32_t seq);\n",
 "  // Gives up exactly the incomplete assembly (generation, seq) the caller judged -- never an\n"
 "  // \"oldest\" by arrival or sequence order -- because its repair is over (A04 rule). False, and\n"
 "  // nothing removed, when it is not held or completed meanwhile (the give-up is cancelled). The\n"
 "  // next delivery reports the seq gap exactly as an expired hold does.\n"
 "  //\n"
 "  // The identity is remembered (a tombstone), because the host may still be sending that AU's\n"
 "  // chunks: without it the next late chunk re-creates the very assembly that was just abandoned\n"
 "  // and blocks the head again, so the caller gives the same AU up over and over (measured: the\n"
 "  // same seq abandoned 3 times, and the good AU behind it never delivered). `nowUs` stamps the\n"
 "  // tombstone; 0 (the legacy overload's clock) means it only ages out by count.\n"
 "  bool GiveUpIncomplete(uint64_t generation, uint32_t seq, uint64_t nowUs = 0);\n"
 "  // True while (generation, seq) is tombstoned: its chunks are ignored as stale traffic.\n"
 "  bool IsAbandoned(uint64_t generation, uint32_t seq, uint64_t nowUs = 0) const;\n"),
("  std::deque<Assembly> assemblies_;\n  bool deliveredAny_ = false;\n  uint32_t lastDeliveredSeq_ = 0;",
 "  // A04: identities given up on. Deliberately NOT expressed by moving lastDeliveredSeq_ forward:\n"
 "  // that would claim the AU was delivered, change what the next delivery reports as a sequence\n"
 "  // gap (and with it the IDR recovery contract), and swallow AUs that were never judged. A\n"
 "  // tombstone blocks exactly the judged (generation, seq) and nothing else -- another generation\n"
 "  // restarts the seq space and never matches one. Bounded both ways: at most kAbandonedMax\n"
 "  // entries (oldest dropped first) and kAbandonedTtlUs of age when the caller passes a clock, so\n"
 "  // a long session cannot accumulate them; Reset() (a new session / decoder resync) clears them.\n"
 "  struct AbandonedAu {\n"
 "    uint64_t generation = 0;\n"
 "    uint32_t seq = 0;\n"
 "    uint64_t atUs = 0;  // 0 = no clock: ages out by count only\n"
 "  };\n"
 "  static constexpr size_t kAbandonedMax = 16;\n"
 "  static constexpr uint64_t kAbandonedTtlUs = 5'000'000;\n"
 "  std::deque<AbandonedAu> abandoned_;\n\n"
 "  std::deque<Assembly> assemblies_;\n  bool deliveredAny_ = false;\n  uint32_t lastDeliveredSeq_ = 0;"),
])
edit(S + r'\native_video_client_shared_core.cpp', [
("bool UdpH264FrameAssembler::GiveUpIncomplete(uint64_t generation, uint32_t seq) {\n"
 "  auto it = std::find_if(assemblies_.begin(), assemblies_.end(), [&](const Assembly& a) {\n"
 "    return a.seq == seq && a.header.streamGeneration == generation;\n"
 "  });\n"
 "  if (it == assemblies_.end() || it->complete) return false;  // gone, or completed meanwhile: cancelled\n"
 "  assemblies_.erase(it);\n"
 "  return true;\n"
 "}\n",
 "bool UdpH264FrameAssembler::IsAbandoned(uint64_t generation, uint32_t seq, uint64_t nowUs) const {\n"
 "  for (const AbandonedAu& a : abandoned_) {\n"
 "    if (a.seq != seq || a.generation != generation) continue;\n"
 "    if (nowUs != 0 && a.atUs != 0 && nowUs > a.atUs + kAbandonedTtlUs) return false;  // aged out\n"
 "    return true;\n"
 "  }\n"
 "  return false;\n"
 "}\n\n"
 "bool UdpH264FrameAssembler::GiveUpIncomplete(uint64_t generation, uint32_t seq, uint64_t nowUs) {\n"
 "  auto it = std::find_if(assemblies_.begin(), assemblies_.end(), [&](const Assembly& a) {\n"
 "    return a.seq == seq && a.header.streamGeneration == generation;\n"
 "  });\n"
 "  if (it == assemblies_.end() || it->complete) return false;  // gone, or completed meanwhile: cancelled\n"
 "  assemblies_.erase(it);\n"
 "  // Remember it, so the chunks still on their way do not re-create it (see the header note).\n"
 "  while (abandoned_.size() >= kAbandonedMax) abandoned_.pop_front();\n"
 "  abandoned_.push_back(AbandonedAu{generation, seq, nowUs});\n"
 "  return true;\n"
 "}\n"),
("void UdpH264FrameAssembler::Reset() {\n  assemblies_.clear();\n  deliveredAny_ = false;\n  lastDeliveredSeq_ = 0;\n}",
 "void UdpH264FrameAssembler::Reset() {\n  assemblies_.clear();\n  abandoned_.clear();\n  deliveredAny_ = false;\n  lastDeliveredSeq_ = 0;\n}"),
("  if (deliveredAny_ && !sequence_is_newer(packet.seq, lastDeliveredSeq_)) {",
 "  if (IsAbandoned(packet.streamGeneration, packet.seq, nowUs)) {\n"
 "    // A04: this AU's repair was given up (its rounds and its grace were spent). Its late data or\n"
 "    // FEC chunks are stale traffic now -- re-creating the assembly would block the head again and\n"
 "    // make the caller abandon the same AU a second and third time, while the good AU behind it\n"
 "    // waits. Only this exact (generation, seq) is affected.\n"
 "    result.disposition = UdpH264AssemblyDisposition::Ignored;\n"
 "    result.reorderDetected = true;\n"
 "    return result;\n"
 "  }\n"
 "  if (deliveredAny_ && !sequence_is_newer(packet.seq, lastDeliveredSeq_)) {"),
])

# --- receiver: pass the clock, and count the give-ups where the test can see them ---------------
edit(S + r'\viewer_recv_stats.hpp', [
("  uint64_t udpNackChunkCount = 0;  // missing chunk indices they carried",
 "  uint64_t udpNackChunkCount = 0;  // missing chunk indices they carried\n"
 "  uint64_t udpStuckHeadGiveUps = 0;  // A04: incomplete heads abandoned by the terminal rule"),
])
edit(S + r'\viewer_video_receiver.cpp', [
("        if (why && assembler.GiveUpIncomplete(info.generation, info.seq)) {",
 "        if (why && assembler.GiveUpIncomplete(info.generation, info.seq, nowUs)) {"),
("          ++stuckHeadGiveUps;\n",
 "          ++stuckHeadGiveUps;\n          ++st.udpStuckHeadGiveUps;\n"),
])
print('V14 follow-up product ok')
