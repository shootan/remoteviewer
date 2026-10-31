p = r'C:\work\gnlink-wt\ka-pc\apps\native_poc\src\ka_pc_shell_e2e_test.cpp'
s = open(p, encoding='utf-8').read()

def rep(a, c, cnt=1):
    global s
    assert s.count(a) == cnt, (s.count(a), a[:90])
    s = s.replace(a, c)

# ---------------- R1 (r4): a reservation keeps its place when the PC it would have let go is released
rep('''            "...a cancelled fourth: nobody let go, C not kept");
      DWORD pidC = 0;
      wait_for([&] { return (pidC = ordinary_pid(H("KA-C").id)) != 0 && pidC != pidC0; }, 10000);''',
'''            "...a cancelled fourth: nobody let go, C not kept");
      // r4 (Codex R1): C (still connecting) holds the reservation. A -- a standby C would have let go -- is released
      // by the user. E is pressed: the free place is C's, so E is refused.
      {
        check(press_release("KA-A"), "R1: release KA-A while C holds the reservation");
        check_after([&] { return wait_card("KA-A", "연결 가능", 10000); }, "R1: A not kept", [&] { return card_state("KA-A"); });
        const size_t eFrom = client_log().size();
        check(tap("KA-E"), "R1: tap KA-E");
        check(log_since(eFrom, "refused host=" + H("KA-E").id + " (connect): another PC is being opened", 3000) &&
                  ordinary_pid(H("KA-E").id) == 0,
              "R1: ...E refused -- the place A left is the reserved C's, not E's");
      }
      DWORD pidC = 0;
      wait_for([&] { return (pidC = ordinary_pid(H("KA-C").id)) != 0 && pidC != pidC0; }, 10000);''')
rep('''      check(client_log().substr(logBefore).find("releasing host=") == std::string::npos &&
                standing_by("KA-A") && standing_by("KA-B") && standing_by("KA-D"),
            "...and the three kept PCs are all still kept (no eviction for a failed fourth)");''',
'''      check(client_log().substr(logBefore).find("releasing host=") == std::string::npos &&
                standing_by("KA-B") && standing_by("KA-D"),
            "...and the kept PCs are still kept (no eviction for a failed fourth)");''')

# ---------------- R4 (r4): an ORDINARY viewer's exit while a kept PC is being opened
rep('''      (void)admittedD;  // an activation of D's existing lease, not a new lease: no wait needed
      H("KA-D").proc.End();  // D's host goes away; D's lease has not noticed yet
      const size_t logFrom = client_log().size();
      check(tap("KA-D"), "tap kept KA-D right after its host went away");''',
'''      (void)admittedD;  // an activation of D's existing lease, not a new lease: no wait needed
      // r4 (Codex R4, mixed): an ORDINARY E is open; kept D is being opened (its host gone, so it takes a while);
      // E's window is closed -- E's exit status must not end D's "connecting" mark
      check(tap("KA-E"), "R4: tap KA-E (an ordinary session)");
      DWORD pidE = 0;
      wait_for([&] { return (pidE = ordinary_pid(H("KA-E").id)) != 0; }, 10000);
      check(pidE && ordinary_connected(pidE, 40000), "R4: E is open");
      H("KA-D").proc.End();  // D's host goes away; D's lease has not noticed yet
      const size_t logFrom = client_log().size();
      check(tap("KA-D"), "tap kept KA-D right after its host went away");
      pump(300);
      check(close_window_of(pidE), "R4: close E's window while D is being opened");
      check(wait_for([&] { return ordinary_pid(H("KA-E").id) == 0; }, 15000), "R4: E's viewer ended (its exit reached the shell)");
      pump(300);
      const bool dStillOpening = client_log().substr(logFrom).find("kept PC session ended host=" + H("KA-D").id) != std::string::npos ||
                                 card_state("KA-D") == "연결하는 중…";
      check_after([&] { return dStillOpening; }, "R4: ...and D's card still says '연결하는 중…' (or D had already ended)",
                  [&] { return card_state("KA-D"); });''')

# ---------------- the Activate-not-sent case moves to B (A was released above)
rep('''      wait_for([&] { const auto* k = gManaged->Find(H("KA-A").id); return k && k->state == "ready"; }, 20000);
      const auto* a = gManaged->Find(H("KA-A").id);
      check(a && a->state == "ready", "KA-A is kept and ready", a ? a->state : "(none)");
      if (a) const_cast<remote60::native_poc::shell::ManagedViewers::Child*>(a)->channel->Close();
      const size_t logFrom = client_log().size();
      page_connect(H("KA-A").id, "KA-A");
      const auto* after = gManaged->Find(H("KA-A").id);
      check(client_log().substr(logFrom).find("kept PC activation host=" + H("KA-A").id + " could not be sent") != std::string::npos,
            "N6: the Activate could not be sent, and the shell says so in its log");
      check(after == nullptr || after->activeOp == 0, "...no activation is left behind (activeOp 0)");
      check(wait_for([] { return status_line().find("여는 요청을 보내지 못했습니다") != std::string::npos; }, 3000),
            "...and the status line tells the user", status_line());
      check(ordinary_pid(H("KA-A").id) == 0, "...no ordinary viewer was started beside the lease");''',
'''      wait_for([&] { const auto* k = gManaged->Find(H("KA-B").id); return k && k->state == "ready"; }, 20000);
      const auto* a = gManaged->Find(H("KA-B").id);
      check(a && a->state == "ready", "KA-B is kept and ready", a ? a->state : "(none)");
      if (a) const_cast<remote60::native_poc::shell::ManagedViewers::Child*>(a)->channel->Close();
      const size_t logFrom = client_log().size();
      page_connect(H("KA-B").id, "KA-B");
      const auto* after = gManaged->Find(H("KA-B").id);
      check(client_log().substr(logFrom).find("kept PC activation host=" + H("KA-B").id + " could not be sent") != std::string::npos,
            "N6: the Activate could not be sent, and the shell says so in its log");
      check(after == nullptr || after->activeOp == 0, "...no activation is left behind (activeOp 0)");
      check(wait_for([] { return status_line().find("여는 요청을 보내지 못했습니다") != std::string::npos; }, 3000),
            "...and the status line tells the user", status_line());
      check(ordinary_pid(H("KA-B").id) == 0, "...no ordinary viewer was started beside the lease");
      wait_for([&] { return gManaged->Find(H("KA-B").id) == nullptr; }, 15000);  // its viewer ends (shell gone)''')

# ---------------- N4 + R3 + decision 1
rep('''      // B's standby is released by the user, then B is connected as an ordinary session and left open
      check(press_release("KA-B"), "release KA-B");
      wait_card("KA-B", "연결 가능", 10000);
      check(tap("KA-B"), "tap KA-B: an ordinary session, left open");
      DWORD pidB = 0;
      wait_for([&] { return (pidB = ordinary_pid(H("KA-B").id)) != 0; }, 10000);
      check(pidB && ordinary_connected(pidB, 40000), "KA-B's ordinary viewer is connected");''',
'''      // B (no longer kept) is connected as an ordinary session and left open. r4 (Codex R3): this viewer's sender is
      // held for 800 ms after the owner fence (test build only) -- the fence must wait for its releases.
      if (card_has_release("KA-B")) press_release("KA-B");
      wait_card("KA-B", "연결 가능", 15000);
      SetEnvironmentVariableW(L"REMOTE60_VIEWER_TEST_HOLD_SEND_AFTER_FENCE_MS", L"800");
      check(tap("KA-B"), "tap KA-B: an ordinary session, left open");
      DWORD pidB = 0;
      wait_for([&] { return (pidB = ordinary_pid(H("KA-B").id)) != 0; }, 10000);
      SetEnvironmentVariableW(L"REMOTE60_VIEWER_TEST_HOLD_SEND_AFTER_FENCE_MS", nullptr);
      check(pidB && ordinary_connected(pidB, 40000), "KA-B's ordinary viewer is connected");
      pump(800);
      // r4 (Codex decision 1): B is open -- a plain press brings it forward and says so; nothing is replaced
      {
        const size_t tFrom = client_log().size();
        check(tap("KA-B"), "decision 1: press KA-B again while it is open");
        check(log_since(tFrom, "connect: host=" + H("KA-B").id + " is open", 3000) && ordinary_pid(H("KA-B").id) == pidB,
              "decision 1: ...shown, not replaced (same viewer)", status_line());
        // and the connect's cancel -- what a replacement sends when the shell still saw it connecting -- reaches
        // nothing in an established session
        const auto slot = gViewerByHost.find(H("KA-B").id);
        if (slot != gViewerByHost.end()) remote60::native_poc::viewer::viewer_request_cancel(slot->second.cancelEvent);
        pump(1500);
        HANDLE pb = OpenProcess(SYNCHRONIZE, FALSE, pidB);
        const bool alive = pb && WaitForSingleObject(pb, 0) == WAIT_TIMEOUT;
        if (pb) CloseHandle(pb);
        check(alive && lines_of_pid(viewer_log(), pidB).find("no effect (session established)") != std::string::npos,
              "decision 1: the connect's cancel does not close an established session (it says 'no effect')");
      }
      const size_t hostBFrom = H("KA-B").proc.Text().size();''')
rep('''      check(code == 0 && client_log().substr(logBefore).find("did not close in time") == std::string::npos,
            "...exit 0, not ended by the shell's handle", "exit=" + std::to_string(code));''',
'''      check(code == 0 && client_log().substr(logBefore).find("did not close in time") == std::string::npos,
            "...exit 0, not ended by the shell's handle", "exit=" + std::to_string(code));
      // r4 (Codex R3): the releases reached the host BEFORE the session ended -- with the sender held 800 ms
      {
        const std::string vb = lines_of_pid(viewer_log(), pidB);
        const size_t ack = vb.find("owner fence: releases acknowledged by the host after ");
        unsigned long waited = 0;
        if (ack != std::string::npos) waited = std::strtoul(vb.c_str() + ack + std::strlen("owner fence: releases acknowledged by the host after "), nullptr, 10);
        check(ack != std::string::npos && waited >= 800,
              "R3: the fence waited for its releases (sender held 800 ms): acknowledged by the host, then closed",
              "waited " + std::to_string(waited) + " ms");
        const std::string hb = H("KA-B").proc.Text().substr(hostBFrom);
        const size_t up = hb.find("[input-timing]");
        size_t ups = 0;
        for (size_t at = hb.find("eventKind=6"); at != std::string::npos; at = hb.find("eventKind=6", at + 1)) ++ups;
        const size_t ended = hb.find("udp control session ended");
        check(ups > 0 && up != std::string::npos && ended != std::string::npos && hb.find("eventKind=6") < ended,
              "R3: ...the host logged the key-ups (eventKind=6) BEFORE the session ended",
              std::to_string(ups) + " key-up(s); host injection off");
      }''')

# ---------------- R2 (r4): pump end and exit, both orders
rep('''      check(read_file(recent_path()) == recentBefore && client_log().substr(logFrom).find("committed") == std::string::npos &&
                client_log().substr(logFrom).find("making room") == std::string::npos,
            "R2: the old owner's late connect notice changes nothing for the new owner (recent list, PC count)");
    }''', '''      check(read_file(recent_path()) == recentBefore && client_log().substr(logFrom).find("committed") == std::string::npos &&
                client_log().substr(logFrom).find("making room") == std::string::npos,
            "R2: the old owner's late connect notice changes nothing for the new owner (recent list, PC count)");
      // r4 (Codex R2): the pump's end and the exit's decision, in both orders, through the shell's own two functions
      // (the same lock): nothing is left behind for either op, and a connected line after the exit is not counted
      const uint64_t opA = 900000001, opB = 900000002;
      note_exit_decided(opA);  // the exit's wait ran out first
      bool refusedLate = false;
      {
        std::lock_guard<std::mutex> lock(gConnectedMu);
        refusedLate = gEndedBeforePump.count(opA) != 0;  // the pump's late "connected" line is then not counted
      }
      note_pump_end(opA);
      note_pump_end(opB);      // the pump ended first
      note_exit_decided(opB);
      bool clean = false;
      {
        std::lock_guard<std::mutex> lock(gConnectedMu);
        clean = !gEndedBeforePump.count(opA) && !gPumpEnded.count(opA) && !gEndedBeforePump.count(opB) && !gPumpEnded.count(opB);
      }
      check(refusedLate && clean, "R2: pump end / exit in either order: a late line refused, nothing left behind");
    }''')
open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('ok')
