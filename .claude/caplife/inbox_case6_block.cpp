  std::puts("\n--- [6] r2 metric: an IDR held for an input buffer ~250 ms is measured with ITS OWN enqueue time");
  {
    // Codex codex_apk_inbox_plan_src_r1.md N1 (its inbox_age_probe): IDR held (no input buffer), 250 ms, a delta,
    // 30 ms, buffers back -> the IDR queued ~280 ms after it arrived. r1 recorded ~33 ms (the delta's time).
    AndroidVideoDecoderSink sink;
    Rx rx{sink};
    const uint64_t g = start(sink, rx, 17, 107);
    auto ageMax = [&] {
      const std::string l = sink.LivenessTraceLine();
      const size_t at = l.find("ageQueue");
      if (at == std::string::npos) return -1LL;
      const size_t m = l.find("max=", at);
      return m == std::string::npos ? -1LL : std::atoll(l.c_str() + m + 4);
    };
    {
      std::lock_guard<std::mutex> lk(fake_ndk::mu());
      fake_ndk::controls().inputAvailable = false;
    }
    const auto t0 = Clock::now();
    rx.send(frame(g, true, 0x85));  // held in the pending slot
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    rx.send(frame(g, false, 0x0E));  // a later delta goes through the worker meanwhile
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    const size_t q0 = queued();
    {
      std::lock_guard<std::mutex> lk(fake_ndk::mu());
      fake_ndk::controls().inputAvailable = true;
    }
    const bool in = wait_until([&] {
      for (uint8_t t : tags_from(q0)) if (t == 0x85) return true;
      return false;
    }, 2000);
    const int64_t elapsedMs = us_since(t0) / 1000;
    const long long maxMs = ageMax();
    check("[6] the held IDR went in, and its queue age is its own (>= 250 ms, not the later delta's)",
          in && maxMs >= 250, "ageQueue max=" + std::to_string(maxMs) + "ms, the IDR waited ~" + std::to_string(elapsedMs) +
                                  "ms | " + sink.LivenessTraceLine().substr(0, 0));
    sink.SetSurface(env, nullptr);
  }

