p = r'C:\work\gnlink-wt\ka-pc\apps\native_poc\src\ka_pc_shell_e2e_test.cpp'
s = open(p, encoding='utf-8').read()

def rep(a, c):
    global s
    assert s.count(a) == 1, (s.count(a), a[:80])
    s = s.replace(a, c)

# helpers: a late/stale message as the forwarder would deliver it, and the bridge entry a page message reaches
rep('''struct TestHost {''', '''// r2b (Codex N6 counter-cases): a message from a kept PC's viewer exactly as the shell's forwarder hands it to the UI
// (ManagedEvent -> on_managed_event), stamped with the identity given -- so a stale op, or a child that is no longer
// the current one, can be delivered at a chosen moment. The shell's own handler decides; nothing is bypassed.
struct ChildIdentity {
  std::string hostId, key;
  uint64_t inc = 0, epoch = 0;
};
ChildIdentity identity_of(const std::string& hostId) {
  ChildIdentity id;
  if (const auto* c = gManaged ? gManaged->Find(hostId) : nullptr) {
    id.hostId = hostId;
    id.key = c->key;
    id.inc = c->incarnation;
    id.epoch = c->ownerEpoch;
  }
  return id;
}
void deliver(const ChildIdentity& who, remote60::native_poc::managed_ipc::Type t,
             std::map<std::string, std::string> fields) {
  remote60::native_poc::shell::ManagedEvent e;
  e.hostId = who.hostId;
  e.incarnation = who.inc;
  e.ownerEpoch = who.epoch;
  e.message.type = t;
  e.message.fields = std::move(fields);
  e.message.fields["key"] = who.key;
  e.message.fields["inc"] = std::to_string(who.inc);
  e.message.fields["epoch"] = std::to_string(who.epoch);
  on_managed_event(e);
}
// The native side of a page's connect message (what the bridge hands handle_page_message), for a press the page
// itself would not offer at that moment (a card it shows as busy).
void page_connect(const std::string& hostId, const std::string& name) {
  handle_page_message("{\\"type\\":\\"connect\\",\\"hostId\\":\\"" + hostId + "\\",\\"hostName\\":\\"" + name + "\\"}");
}

struct TestHost {''')

# (a) stale ops while B, C, D are active
rep('''    capture_page(shots + L"3-three-active.png");
    check(log_has("picker-ready ms=", 1000), "the shell log has the activation's picker-ready with its op");''',
'''    capture_page(shots + L"3-three-active.png");
    check(log_has("picker-ready ms=", 1000), "the shell log has the activation's picker-ready with its op");
    // r2b (Codex N6): messages of an op this activation is not -- an older one and one never sent -- are dropped:
    // B stays '연결됨' with its activation, the status line is not touched
    {
      using remote60::native_poc::managed_ipc::Type;
      const ChildIdentity b = identity_of(H("KA-B").id);
      const uint64_t op = gManaged->Find(H("KA-B").id)->activeOp;
      const std::string statusBefore = status_line();
      const size_t logFrom = client_log().size();
      deliver(b, Type::SessionEnded, {{"op", std::to_string(op + 41)}, {"rc", "6"}, {"reason", "connect-failed"}, {"connected", "0"}});
      deliver(b, Type::SessionEnded, {{"op", "0"}, {"rc", "0"}, {"reason", "closed"}, {"connected", "1"}});
      deliver(b, Type::PickerReady, {{"op", std::to_string(op + 41)}, {"ms", "1"}});
      pump(800);
      const std::string slice = client_log().substr(logFrom);
      check(op != 0 && gManaged->Find(H("KA-B").id)->activeOp == op && card_state("KA-B") == "연결됨" &&
                status_line() == statusBefore,
            "N6: a SessionEnded / PickerReady of another op leaves B's activation as it is", "op=" + std::to_string(op));
      check(count_of(slice, "dropped (current op=" + std::to_string(op) + ")") == 3, "...each one dropped by op, and said",
            std::to_string(count_of(slice, "dropped (current op=")));
    }''')

# (b) a late event of a released child while another PC is being opened
rep('''      check(card_state("KA-B") == "대기 중" && card_state("KA-D") == "대기 중", "B and D are untouched");
      capture_page(shots + L"4-released.png");
    }''', '''      check(card_state("KA-B") == "대기 중" && card_state("KA-D") == "대기 중", "B and D are untouched");
      capture_page(shots + L"4-released.png");
    }
    // r2b (Codex N6): while B is being opened, late messages of C's ended viewer arrive (its old incarnation) -- a
    // SessionEnded that would say "failed", a State that would say "ready", its exit. None of it reaches the page.
    {
      using remote60::native_poc::managed_ipc::Type;
      check(tap("KA-B"), "tap kept KA-B");
      for (const auto& t : {Type::SessionEnded, Type::State}) {
        if (t == Type::SessionEnded) deliver(releasedC, t, {{"op", "1"}, {"rc", "6"}, {"reason", "connect-failed"}, {"connected", "0"}});
        else deliver(releasedC, t, {{"state", "ready"}, {"lost", "0"}});
      }
      remote60::native_poc::shell::ManagedEvent gone;
      gone.hostId = releasedC.hostId;
      gone.incarnation = releasedC.inc;
      gone.ownerEpoch = releasedC.epoch;
      gone.exited = true;
      gone.exitCode = 0;
      on_managed_event(gone);
      check_after([&] { return wait_card("KA-B", "연결됨", 30000); }, "N6: B opens as usual ('연결됨')", [&] { return card_state("KA-B"); });
      check(card_state("KA-C") == "연결 가능" && status_line().find("실패") == std::string::npos,
            "...C's late messages changed nothing (C not kept, no failure said)", card_state("KA-C") + " / " + status_line());
      check(close_window_of(managed_pid(H("KA-B").id)), "close B's session window");
      check_after([&] { return wait_card("KA-B", "대기 중", 30000); }, "B back to '대기 중'", [&] { return card_state("KA-B"); });
    }''')
rep('''    // ======================================================== N3: 연결 해제
    {
      const DWORD pidC = managed_pid(H("KA-C").id);''', '''    // ======================================================== N3: 연결 해제
    const ChildIdentity releasedC = identity_of(H("KA-C").id);  // r2b: its messages are delivered late, below
    {
      const DWORD pidC = managed_pid(H("KA-C").id);''')

# (d) a cancelled fourth (C's host gone): a second press replaces the attempt, the first is called off
rep('''      check(tap("KA-C"), "tap KA-C, whose host is gone, beside three kept PCs");
      DWORD pidC = 0;
      wait_for([&] { return (pidC = ordinary_pid(H("KA-C").id)) != 0; }, 10000);
      pump(45000);  // longer than a connect to a live host takes here''', '''      check(tap("KA-C"), "tap KA-C, whose host is gone, beside three kept PCs");
      DWORD pidC0 = 0;
      wait_for([&] { return (pidC0 = ordinary_pid(H("KA-C").id)) != 0; }, 10000);
      // r2b (Codex N2, a CANCELLED fourth): while that first attempt is still connecting, C is pressed again (the
      // bridge's connect for C; the page itself shows the busy card) -- the shell calls the first attempt off and
      // starts another. The called-off one must cost nobody and must not be kept.
      pump(2500);
      page_connect(H("KA-C").id, "KA-C");
      HANDLE p0 = pidC0 ? OpenProcess(SYNCHRONIZE, FALSE, pidC0) : nullptr;
      const bool calledOff = p0 && wait_for([&] { return WaitForSingleObject(p0, 0) == WAIT_OBJECT_0; }, 15000);
      if (p0) CloseHandle(p0);
      check(calledOff && lines_of_pid(viewer_log(), pidC0).find("connect cancelled") != std::string::npos,
            "N2: the first attempt for C was called off (its log: connect cancelled) and ended");
      pump(1000);
      check(client_log().substr(logBefore).find("releasing host=") == std::string::npos &&
                client_log().substr(logBefore).find("first conversion to standby host=" + H("KA-C").id) == std::string::npos &&
                card_state("KA-A") == "대기 중" && card_state("KA-B") == "대기 중" && card_state("KA-D") == "대기 중",
            "...a cancelled fourth: nobody let go, C not kept");
      DWORD pidC = 0;
      wait_for([&] { return (pidC = ordinary_pid(H("KA-C").id)) != 0 && pidC != pidC0; }, 10000);
      pump(45000);  // longer than a connect to a live host takes here''')

# (c) an Activate that cannot be sent (A's channel broken) -- after the take-failure case
rep('''      capture_page(shots + L"5-failed-activation.png");
    }''', '''      capture_page(shots + L"5-failed-activation.png");
    }
    // r2b (Codex N6): an Activate that cannot be sent -- the shell's channel to A's viewer is closed (as when that
    // pipe breaks) and A is pressed at once, before anything else runs on this thread. No activation may be left
    // behind and the user is told; nothing pretends to open.
    {
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
      check(ordinary_pid(H("KA-A").id) == 0, "...no ordinary viewer was started beside the lease");
    }''')
open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('ok')
