base = r'C:\work\gnlink-wt\apk-decoder-liveness\apps\android_direct_client\app\src\main\cpp'


def edit(name, reps):
    p = base + '\\' + name
    raw = open(p, 'rb').read()
    crlf = b'\r\n' in raw
    s = raw.decode('utf-8').replace('\r\n', '\n')
    for a, b in reps:
        assert s.count(a) == 1, (name, s.count(a), a[:90])
        s = s.replace(a, b)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'wb').write(s.encode('utf-8'))


edit('android_video_decoder.cpp', [
# enqueue: the AU's own record (gen, seq, time) in a ring; the inbox peak before and after the trim
("""  REMOTE60_LT(item.enqueueUs = LtNowUs());
  item.frame = std::move(frame);
  inbox_.push_back(std::move(item));
  REMOTE60_LT({
    LtMax(ltInboxHwFrames_, inbox_.size());
    LtMax(ltInboxHwBytes_, inboxBytes_);
  });""", """  REMOTE60_LT({
    item.enqueueUs = LtNowUs();
    // apk-decoder-inbox r2 (Codex codex_apk_inbox_plan_src_r1.md N1): the AU's own enqueue time, by its identity
    // (stream generation + AU seq) -- looked up when THAT AU goes into the codec, wherever it waited (inbox, the
    // pending slot). r1 used the time of whatever frame was processed last: a held IDR's 286 ms read as 33 ms.
    LtEnq& e = ltEnq_[frame.header.seq % kLtEnqRing];
    e.gen = frame.header.streamGeneration;
    e.seq = frame.header.seq;
    e.us = item.enqueueUs;
  });
  item.frame = std::move(frame);
  inbox_.push_back(std::move(item));
  REMOTE60_LT({  // r2: the peak INCLUDING the frame just added, before the trim (may be 9 / over 4 MiB for an instant)
    LtMax(ltInboxPeakPreFrames_, inbox_.size());
    LtMax(ltInboxPeakPreBytes_, inboxBytes_);
  });"""),
("""      log_info(line);
    }
  }
  workCv_.notify_all();
}""", """      log_info(line);
    }
  }
  REMOTE60_LT({  // r2: what stays admitted after the trim (never over the caps)
    LtMax(ltInboxHwFrames_, inbox_.size());
    LtMax(ltInboxHwBytes_, inboxBytes_);
  });
  workCv_.notify_all();
}"""),
# r1's per-pass "current frame" time: gone
("""  REMOTE60_LT(ltCurEnqueueUs_ = item.enqueueUs);  // its age when it is queued into the codec (directly from here)
""", ""),
("""  REMOTE60_LT(if (ltCurEnqueueUs_) {
    LtAge(ltAgeQueue_, LtNowUs() - ltCurEnqueueUs_);
    ltCurEnqueueUs_ = 0;  // a held frame queued later has no enqueue time here: not counted
  });""", """  REMOTE60_LT({
    // r2: THIS AU's enqueue time (by identity). A bootstrap replay is the same AU queued again: not an arrival, not
    // counted. An AU whose record the ring has lost (64+ AUs later) is counted as unmatched -- never another's time.
    if (queueingReplay_) {
      ltAgeQueueReplay_.fetch_add(1, std::memory_order_relaxed);
    } else {
      LtEnq& e = ltEnq_[frame.header.seq % kLtEnqRing];
      if (e.us != 0 && e.seq == frame.header.seq && e.gen == frame.header.streamGeneration) {
        LtAge(ltAgeQueue_, LtNowUs() - e.us);
        e.us = 0;  // one AU, one age
      } else {
        ltAgeQueueUnmatched_.fetch_add(1, std::memory_order_relaxed);
      }
    }
  });"""),
# labels
("""  std::snprintf(more, sizeof(more), " | inboxHw=%llu/%lluB passFramesMax=%llu passSvcMax=%llums rxLockWaitMax=%llums "
                "rxLockHoldMax=%llums",
                static_cast<unsigned long long>(ltInboxHwFrames_.load(std::memory_order_relaxed)),
                static_cast<unsigned long long>(ltInboxHwBytes_.load(std::memory_order_relaxed)),""",
 """  // r2 labels: inboxHw = admitted after the trim; inboxPeakPre = including the frame just added, before the trim;
  // rxLockScope = from taking mu_ to the end of the call's scope (an upper bound of the hold: it includes what runs
  // after the unlock in the same scope)
  std::snprintf(more, sizeof(more), " | inboxHw=%llu/%lluB inboxPeakPre=%llu/%lluB passFramesMax=%llu passSvcMax=%llums "
                "rxLockWaitMax=%llums rxLockScopeMax=%llums",
                static_cast<unsigned long long>(ltInboxHwFrames_.load(std::memory_order_relaxed)),
                static_cast<unsigned long long>(ltInboxHwBytes_.load(std::memory_order_relaxed)),
                static_cast<unsigned long long>(ltInboxPeakPreFrames_.load(std::memory_order_relaxed)),
                static_cast<unsigned long long>(ltInboxPeakPreBytes_.load(std::memory_order_relaxed)),"""),
("""  char more[256];""", """  char more[320];"""),
("""  out += " | agePop " + hist(ltAgePop_) + " | ageQueue " + hist(ltAgeQueue_) + " | api";""",
 """  char aq[96];
  std::snprintf(aq, sizeof(aq), " unmatched=%llu replay=%llu",
                static_cast<unsigned long long>(ltAgeQueueUnmatched_.load(std::memory_order_relaxed)),
                static_cast<unsigned long long>(ltAgeQueueReplay_.load(std::memory_order_relaxed)));
  out += " | agePop " + hist(ltAgePop_) + " | ageQueueAU " + hist(ltAgeQueue_) + aq + " | api";"""),
("""  static const char* kApiNames[kApiKinds] = {"other", "create", "stopDel", "name", "deqIn", "queueIn",""",
 """  // r2: "createCfgStart" is ONE span (create + configure + start), not three calls measured apart
  static const char* kApiNames[kApiKinds] = {"other", "createCfgStart", "stopDel", "name", "deqIn", "queueIn","""),
])

edit('android_video_decoder.hpp', [
("""  uint64_t ltPassFrames_ = 0;    // the worker's own (under mu_)
  uint64_t ltPassStartUs_ = 0;
  uint64_t ltCurEnqueueUs_ = 0;""", """  uint64_t ltPassFrames_ = 0;    // the worker's own (under mu_)
  uint64_t ltPassStartUs_ = 0;
  // r2: each AU's enqueue time by its identity (stream generation + seq), under mu_ (RX writes, the worker reads)
  struct LtEnq {
    uint64_t gen = 0;
    uint32_t seq = 0;
    uint64_t us = 0;
  };
  static constexpr size_t kLtEnqRing = 64;
  LtEnq ltEnq_[kLtEnqRing];
  std::atomic<uint64_t> ltAgeQueueUnmatched_{0};
  std::atomic<uint64_t> ltAgeQueueReplay_{0};
  std::atomic<uint64_t> ltInboxPeakPreFrames_{0};
  std::atomic<uint64_t> ltInboxPeakPreBytes_{0};"""),
])
print('ok')
