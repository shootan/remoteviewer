p = r'C:\work\gnlink-wt\ka-pc\apps\native_poc\src\ka_pc_shell_e2e_test.cpp'
s = open(p, encoding='utf-8').read()

def rep(a, c, cnt=1):
    global s
    assert s.count(a) == cnt, (s.count(a), a[:90])
    s = s.replace(a, c)

# five hosts
rep('''  for (const char* name : {"KA-A", "KA-B", "KA-C", "KA-D"}) {''', '''  for (const char* name : {"KA-A", "KA-B", "KA-C", "KA-D", "KA-E"}) {''')
rep('''    check(online == hosts.size(), "the four hosts are four online PCs of the account", std::to_string(online) + "/4");''',
    '''    check(online == hosts.size(), "the five hosts are five online PCs of the account", std::to_string(online) + "/5");''')
rep('''          "the four PCs are listed, each '연결 가능'");''', '''          "the five PCs are listed, each '연결 가능'");''')
rep('''bool wait_card(const std::string& name, const std::string& text, unsigned ms) {''', '''// "대기 중" -- with or without "· 이전 연결 정리 중일 수 있음" (returning)
bool standing_by(const std::string& name) { return card_state(name).rfind("대기 중", 0) == 0; }
bool wait_card(const std::string& name, const std::string& text, unsigned ms) {''')

# R1 at the start: one reservation for every candidate
rep('''    // ======================================================== N1: each PC's own first conversion, any close order''',
'''    // ======================================================== R1 (r3): ONE reservation beside three PCs
    {
      // A standing by, B and C open: three logical PCs
      check(tap("KA-A"), "R1: tap KA-A");
      DWORD pa = 0;
      wait_for([&] { return (pa = ordinary_pid(H("KA-A").id)) != 0; }, 10000);
      check(pa && ordinary_connected(pa, 40000) && close_window_of(pa), "R1: A connected, and its window closed");
      check_after([&] { return wait_card("KA-A", "대기 중", 45000); }, "R1: A standing by", [&] { return card_state("KA-A"); });
      for (const char* n : {"KA-B", "KA-C"}) {
        check(tap(n), std::string("R1: tap ") + n);
        DWORD pid = 0;
        wait_for([&] { return (pid = ordinary_pid(H(n).id)) != 0; }, 10000);
        check(pid && ordinary_connected(pid, 40000), std::string("R1: ") + n + " open");
      }
      // D, then A (the only standby), then E -- pressed one after the other in ONE turn of the page, so all three
      // reach the shell while D is still connecting
      const size_t logFrom = client_log().size();
      js(L"(function(){var c=function(n){var r=" + card_expr("@") + L";}; return true;})()");  // (warm-up, no-op)
      js(L"(function(){function card(n){return Array.from(document.querySelectorAll('.hostRow')).find(function(r){"
         L"var e=r.querySelector('.name');return e&&e.textContent===n;}).querySelector('.host');}"
         L"card('KA-D').click();card('KA-A').click();card('KA-E').click();return true;})()");
      pump(500);
      const std::string slice = client_log().substr(logFrom);
      check(slice.find("host=" + H("KA-D").id + " reserved beside three PCs") != std::string::npos,
            "R1: D (beside A standing by, B and C open) took the one reservation");
      check(slice.find("kept PC activation host=" + H("KA-A").id + " refused") != std::string::npos,
            "R1: ...opening A -- the last standby that could make room for D -- is refused while D is reserved");
      check(slice.find("refused host=" + H("KA-E").id + " (connect): another PC is being opened") != std::string::npos &&
                ordinary_pid(H("KA-E").id) == 0,
            "R1: ...and E is refused ('다른 PC 를 여는 중'): no second candidate on the same standby");
      DWORD pd = 0;
      wait_for([&] { return (pd = ordinary_pid(H("KA-D").id)) != 0; }, 10000);
      check(pd && ordinary_connected(pd, 40000), "R1: D connected");
      check(log_has("reservation of host=" + H("KA-D").id + " committed", 10000) &&
                log_has("making room after host=" + H("KA-D").id + " succeeded, releasing host=" + H("KA-A").id, 5000),
            "R1: ...then, and only then, its reservation was committed and A let go");
      check(count_of(client_log().substr(logFrom), "releasing host=") == 1, "R1: ...exactly one PC let go");
      const size_t logE = client_log().size();
      check(tap("KA-E"), "R1: tap E with B, C, D all open");
      pump(1500);
      check(client_log().substr(logE).find("refused host=" + H("KA-E").id + " (connect): three PCs are in use") != std::string::npos &&
                ordinary_pid(H("KA-E").id) == 0,
            "R1: ...E refused ('PC 3대가 모두 열려'): never a fourth window", status_line());
      // back to nothing kept: B, C, D closed (each kept), then released by the user -- the next part starts clean
      for (const char* n : {"KA-B", "KA-C", "KA-D"}) close_window_of(ordinary_pid(H(n).id));
      for (const char* n : {"KA-B", "KA-C", "KA-D"}) {
        wait_for([&] { return card_has_release(n); }, 45000);
        press_release(n);
        wait_card(n, "연결 가능", 15000);
      }
      check(card_state("KA-A") == "연결 가능" && card_state("KA-B") == "연결 가능" && card_state("KA-C") == "연결 가능" &&
                card_state("KA-D") == "연결 가능",
            "R1: (set back: nothing kept)");
    }

    // ======================================================== N1: each PC's own first conversion, any close order''')
rep('''      js(L"(function(){var c=function(n){var r=" + card_expr("@") + L";}; return true;})()");  // (warm-up, no-op)
''', '')

# R4 real path inside the failed-fourth section; the 'standing by' checks there accept returning
rep('''      // C's host goes away; C (released, so not kept) is tapped: its connect fails
      H("KA-C").proc.End();
      const size_t logBefore = client_log().size();
      check(tap("KA-C"), "tap KA-C, whose host is gone, beside three kept PCs");
      DWORD pidC0 = 0;
      wait_for([&] { return (pidC0 = ordinary_pid(H("KA-C").id)) != 0; }, 10000);''', '''      // C's host goes away; C (released, so not kept) is tapped: its connect fails
      H("KA-C").proc.End();
      // r3 (Codex R4): A is opened first; while C is still connecting, A's window is closed -- A's own, valid
      // SessionEnded arrives and must not end C's "connecting" mark
      check(tap("KA-A"), "R4: tap kept KA-A");
      check_after([&] { return wait_card("KA-A", "연결됨", 30000); }, "R4: A is open ('연결됨')", [&] { return card_state("KA-A"); });
      const DWORD activeA = managed_pid(H("KA-A").id);
      const size_t logBefore = client_log().size();
      check(tap("KA-C"), "tap KA-C, whose host is gone, beside three kept PCs");
      DWORD pidC0 = 0;
      wait_for([&] { return (pidC0 = ordinary_pid(H("KA-C").id)) != 0; }, 10000);
      check(close_window_of(activeA), "R4: close A's session window while C is connecting");
      check(log_has("kept PC session ended host=" + H("KA-A").id + " op=", 15000), "R4: A's session ended (its SessionEnded reached the shell)");
      pump(500);
      check_after([] { return card_state("KA-C") == "연결하는 중…"; }, "R4: ...and C's card still says '연결하는 중…'",
                  [] { return card_state("KA-C"); });''')
rep('''      check(client_log().substr(logBefore).find("releasing host=") == std::string::npos &&
                client_log().substr(logBefore).find("first conversion to standby host=" + H("KA-C").id) == std::string::npos &&
                card_state("KA-A") == "대기 중" && card_state("KA-B") == "대기 중" && card_state("KA-D") == "대기 중",
            "...a cancelled fourth: nobody let go, C not kept");''', '''      check(client_log().substr(logBefore).find("releasing host=") == std::string::npos &&
                client_log().substr(logBefore).find("first conversion to standby host=" + H("KA-C").id) == std::string::npos &&
                standing_by("KA-A") && standing_by("KA-B") && standing_by("KA-D"),
            "...a cancelled fourth: nobody let go, C not kept");''')
rep('''      check(client_log().substr(logBefore).find("releasing host=") == std::string::npos &&
                card_state("KA-A") == "대기 중" && card_state("KA-B") == "대기 중" && card_state("KA-D") == "대기 중",
            "...and the three kept PCs are all still kept (no eviction for a failed fourth)");''', '''      check(client_log().substr(logBefore).find("releasing host=") == std::string::npos &&
                standing_by("KA-A") && standing_by("KA-B") && standing_by("KA-D"),
            "...and the three kept PCs are all still kept (no eviction for a failed fourth)");''')
rep('''        wait_for([&] { return ordinary_pid(H("KA-C").id) == 0; }, 10000);
      }
    }''', '''        wait_for([&] { return ordinary_pid(H("KA-C").id) == 0; }, 10000);
      }
      check(log_has("reservation of host=" + H("KA-C").id + " released (ended without connecting)", 5000),
            "R1: C's reservation was released when it ended without connecting");
    }''')

# the A-ready precondition for the 'could not be sent' case: A may still be returning after R4
rep('''      const auto* a = gManaged->Find(H("KA-A").id);
      check(a && a->state == "ready", "KA-A is kept and ready", a ? a->state : "(none)");''', '''      wait_for([&] { const auto* k = gManaged->Find(H("KA-A").id); return k && k->state == "ready"; }, 20000);
      const auto* a = gManaged->Find(H("KA-A").id);
      check(a && a->state == "ready", "KA-A is kept and ready", a ? a->state : "(none)");''')

# R3 fence text; R2 after sign-out
rep('''      check(lines_of_pid(viewer_log(), pidB).find("owner fence: input and clipboard cut") != std::string::npos,
            "...through the owner fence (input and clipboard cut, then its own WM_CLOSE teardown)");''', '''      {
        const std::string vb = lines_of_pid(viewer_log(), pidB);
        const size_t at = vb.find("owner fence: held input released (modifierUps=");
        check(at != std::string::npos && vb.find("input and clipboard cut", at) != std::string::npos,
              "...through the owner fence: held input released FIRST, then input and clipboard cut, then its own teardown",
              at == std::string::npos ? "" : vb.substr(at, vb.find('\\n', at) - at).substr(0, 110));
      }''')
rep('''      js(L"document.getElementById('signOut').click();true");''', '''      oldEpoch = gOwnerEpoch.load();
      js(L"document.getElementById('signOut').click();true");''')
rep('''    // ======================================================== N4: sign-out ends the shell's own viewers with the fence
    {''', '''    // ======================================================== N4: sign-out ends the shell's own viewers with the fence
    uint64_t oldEpoch = 0;
    {''')
rep('''      capture_page(shots + L"6-signed-out.png");
      outside.End();
    }''', '''      capture_page(shots + L"6-signed-out.png");
      outside.End();
    }
    // ======================================================== R2 (r3): a late connect notice of the old owner
    {
      js(L"document.getElementById('account').value='tester';document.getElementById('password').value='e2e-pass-1234';"
         L"document.getElementById('signIn').click();true");
      check(wait_for([] { return js(L"!document.getElementById('hostsCard').classList.contains('hidden')") == "true"; }, 30000),
            "R2: signed in again (a new owner)");
      const std::string recentBefore = read_file(recent_path());
      const size_t logFrom = client_log().size();
      // the notice a viewer of the signed-out owner would send late: its op, a host, the OLD owner epoch
      on_ordinary_connected(gViewerOperation, H("KA-B").id, oldEpoch);
      on_ordinary_connected(gViewerOperation + 1, H("KA-E").id, oldEpoch);
      pump(500);
      check(read_file(recent_path()) == recentBefore && client_log().substr(logFrom).find("committed") == std::string::npos &&
                client_log().substr(logFrom).find("making room") == std::string::npos,
            "R2: the old owner's late connect notice changes nothing for the new owner (recent list, PC count)");
    }''')
open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('ok')
