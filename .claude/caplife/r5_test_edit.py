p = r'C:\work\gnlink-wt\apk-decoder-liveness\apps\native_poc\src\android_decoder_liveness_test.cpp'
N = '\r\n'
s = open(p, 'rb').read().decode('utf-8')
anchor = '  std::printf("\\nandroid_decoder_liveness_test: %d checks, %d failed\\nRESULT: %s\\n", gChecks, gFailures,'
assert s.count(anchor) == 1
add = r'''  std::puts("\n--- [14] r5 N3 (Codex src r4's schedule): a failed create, then only deltas -- one deadline, one budget");
  {
    AndroidVideoDecoderSink sink;
    sink.SetSurface(env, sa());
    sink.PrepareForWindowSelection(95);
    sink.OnWindowSelectionControlResultFor(answer(303), 95);
    {
      std::lock_guard<std::mutex> lk(fake_ndk::mu());
      fake_ndk::controls().createFails = true;
    }
    auto failures = [&] { return field(sink.DebugStatus(), "codecFail"); };
    MaxUs rx;
    timed(rx, [&] { sink.OnEncodedH264Frame(frame(303, true, 0xF0)); });  // SPS/PPS + IDR: its create fails
    for (int i = 0; i < 40; ++i) {                                        // then 40 deltas, 50 ms apart (2 s)
      timed(rx, [&] { sink.OnEncodedH264Frame(frame(303, false, 0x33)); });
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    const uint64_t in2s = failures();
    // the frames go on (a delta every 100 ms, an IDR every second) until the budget is spent
    const auto t0 = Clock::now();
    bool terminal = false;
    for (int i = 0; us_since(t0) < 30000000 && !terminal; ++i) {
      timed(rx, [&] { sink.OnEncodedH264Frame(frame(303, (i % 10) == 0, (i % 10) == 0 ? 0xF1 : 0x34)); });
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      terminal = sink.DebugStatus().find(" codecState=failed_terminal") != std::string::npos;
    }
    const int64_t terminalAfterMs = us_since(t0) / 1000;
    const uint64_t atTerminal = failures();
    // terminal: the create could succeed now, frames keep coming -- still nothing tries
    {
      std::lock_guard<std::mutex> lk(fake_ndk::mu());
      fake_ndk::controls().createFails = false;
    }
    const int createdBefore = snap().created;
    for (int i = 0; i < 20; ++i) {
      timed(rx, [&] { sink.OnEncodedH264Frame(frame(303, (i % 10) == 0, (i % 10) == 0 ? 0xF2 : 0x35)); });
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    const uint64_t afterTerminal = failures();
    const bool noTryWhileTerminal = snap().created == createdBefore && afterTerminal == atTerminal &&
                                    sink.DebugStatus().find(" codec=off") != std::string::npos;
    // the explicit way back: a new surface
    const size_t q0 = snap().queued;
    sink.SetSurface(env, sb());
    sink.OnEncodedH264Frame(frame(303, true, 0xF3));
    const bool back = wait_until([&] { auto q = queued_from(q0); return !q.empty() && q.front().first == 0xF3 &&
                                                                    q.front().second == sb(); }, 3000);
    check("[14] RX never waited", rx.v < kCallBoundUs, rx.str());
    check("[14] in the first 2 s (1 IDR + 40 deltas) at most 3 creates were tried (the first + the 1 s deadline)",
          in2s != ~0ull && in2s <= 3, "codecFail=" + std::to_string(in2s));
    check("[14] the budget ends in TERMINAL: 1 + 20 tries, about 20 s, frames coming all along",
          terminal && atTerminal == 21 && terminalAfterMs > 15000,
          "codecFail=" + std::to_string(atTerminal) + " after " + std::to_string(terminalAfterMs) + " ms");
    check("[14] terminal means no try at all: frames (IDRs too) make no codec even once one could be made",
          noTryWhileTerminal, "codecFail=" + std::to_string(afterTerminal));
    check("[14] a new surface starts over: its IDR goes into a codec on it", back);
    sink.SetSurface(env, nullptr);
  }

'''.replace('\n', N)
s = s.replace(anchor, add + anchor)
a = '// cap for every frame (N2), [13] a window that could not be had (D1). NEGATIVE: the 7a79b09 sink + gate.'
assert s.count(a) == 1
s = s.replace(a, a + N + '// r5 (Codex src r4 N3): [14] a failed create then only deltas -- one shared retry deadline and budget, then' + N +
              '// terminal (no try at all) until a new surface. NEGATIVE: the 0a108b3 sink (41 creates in 2 s).')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
