p = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/host_standby_admission_e2e_test.cpp'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')
anchor = """    check("...two relay leases bound and ready, no session", count_of(srv.log(), "standby ready") >= 2 &&
          count_of(rh.log(), "first session") == 0, rh.tail());
"""
assert s.count(anchor) == 1
block = r'''    // keepalive-r2 FINDING (작업용2 report_ka_pc_impl_r1.md): the same device leases again right after its last
    // admission (a crash, an end-all, an eviction) -- seen there: unanswered until 30 s had passed.
    std::printf("  -- the same device again at once: after a crash (no release), and after a release --\n");
    {
      auto randX = [](uint8_t* b, size_t n) { for (size_t i = 0; i < n; ++i) b[i] = static_cast<uint8_t>(0xA9); return true; };
      MemGenStore rsX;
      GenerationAllocator rgX(rsX, randX);
      auto parkWithin = [&](StandbyLease& l, uint32_t ms) {
        return pump_until(l, [&] { return l.state() == StandbyLease::State::Parked; }, ms);
      };
      bool first = false;
      {
        StandbyLease x1(rgX, features, relayCap, mono_ms);
        first = x1.Open(&err) && (x1.Start(), parkWithin(x1, 30000));
        check("device X parks through the relay", first, last_events(x1));
      }  // gone without a release: a crash -- its host entry and its relay lease stay behind
      const size_t readyBefore = count_of(srv.log(), "standby ready");
      StandbyLease x2(rgX, features, relayCap, mono_ms);
      const uint64_t t0 = mono_ms();
      const bool again = x2.Open(&err) && (x2.Start(), parkWithin(x2, 15000));
      const uint64_t tookAgain = mono_ms() - t0;
      check("...the same device again at once after a crash: parked within 10 s (a new admission, its own relay lease)",
            again && tookAgain < 10000,
            "took " + std::to_string(tookAgain) + " ms state=" + std::to_string(static_cast<int>(x2.state())) + " | " +
                last_events(x2) + " | relay: " + srv.tail() + " | host: " + rh.tail());
      x2.Release();
      StandbyLease x3(rgX, features, relayCap, mono_ms);
      const uint64_t t1 = mono_ms();
      const bool third = x3.Open(&err) && (x3.Start(), parkWithin(x3, 15000));
      const uint64_t tookThird = mono_ms() - t1;
      check("...and again at once after a release: parked within 10 s", third && tookThird < 10000,
            "took " + std::to_string(tookThird) + " ms | " + last_events(x3) + " | relay: " + srv.tail() + " | host: " +
                rh.tail());
      check("...each one its own relay lease (ready twice more)", count_of(srv.log(), "standby ready") >= readyBefore + 2);
      x3.Release();
    }
'''
s = s.replace(anchor, anchor + block)
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
