import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit
F = r'D:\remote\remote\apps\native_poc\src\viewer_udp_recovery_test.cpp'
# The delayed NACK answer must not outlive the FakeHost (a detached thread did): the reader
# thread keeps a due-list and sends when due, so Stop() joining the reader ends everything.
edit(F, [
("          if (plan_.nackAnswerDelayMs > 0) {\n"
 "            // A slow link: the answer leaves later, from a helper thread so the reader keeps reading.\n"
 "            std::vector<uint16_t> only(nack.missing, nack.missing + count);\n"
 "            const uint32_t delayMs = plan_.nackAnswerDelayMs;\n"
 "            std::thread([this, au, only, delayMs]() {\n"
 "              sleep_ms(delayMs);\n"
 "              if (stop_.load()) return;\n"
 "              SendChunks(au, only.data(), static_cast<uint16_t>(only.size()));\n"
 "            }).detach();\n"
 "            retransmitChunks_ += count;\n"
 "            continue;\n"
 "          }\n",
 "          if (plan_.nackAnswerDelayMs > 0) {\n"
 "            // A slow link: the answer leaves later. Queued for the reader loop itself (below), so it\n"
 "            // never outlives this object.\n"
 "            DelayedAnswer d;\n"
 "            d.dueUs = qpc_now_us() + static_cast<uint64_t>(plan_.nackAnswerDelayMs) * 1000ULL;\n"
 "            d.au = au;\n"
 "            d.only.assign(nack.missing, nack.missing + count);\n"
 "            delayed_.push_back(std::move(d));\n"
 "            retransmitChunks_ += count;\n"
 "            continue;\n"
 "          }\n"),
("      if (n <= 0) continue;\n      if (n >= static_cast<int>(sizeof(UdpHelloPacket))) {",
 "      if (n <= 0) {\n        FlushDelayedAnswers();\n        continue;\n      }\n      FlushDelayedAnswers();\n      if (n >= static_cast<int>(sizeof(UdpHelloPacket))) {"),
("  LossPlan plan_;\n  SOCKET sock_ = INVALID_SOCKET;",
 "  struct DelayedAnswer {\n    uint64_t dueUs = 0;\n    CachedAu au;\n    std::vector<uint16_t> only;\n  };\n"
 "  std::deque<DelayedAnswer> delayed_;  // reader thread only\n"
 "  void FlushDelayedAnswers() {\n"
 "    const uint64_t nowUs = qpc_now_us();\n"
 "    while (!delayed_.empty() && delayed_.front().dueUs <= nowUs) {\n"
 "      DelayedAnswer d = std::move(delayed_.front());\n"
 "      delayed_.pop_front();\n"
 "      SendChunks(d.au, d.only.data(), static_cast<uint16_t>(d.only.size()));\n"
 "    }\n"
 "  }\n"
 "  LossPlan plan_;\n  SOCKET sock_ = INVALID_SOCKET;"),
])
print('fakehost delayed answer via reader loop ok')
