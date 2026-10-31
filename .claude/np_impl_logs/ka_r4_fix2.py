import os
base = r'C:\work\gnlink-wt\ka-pc\apps\native_poc\src' + os.sep

def edit(name, pairs, crlf_keep=True):
    p = base + name
    b = open(p, 'rb').read().decode('utf-8')
    crlf = '\r\n' in b
    s = b.replace('\r\n', '\n')
    for a, c in pairs:
        assert s.count(a) == 1, (name, s.count(a), a[:90])
        s = s.replace(a, c)
    open(p, 'wb').write((s.replace('\n', '\r\n') if crlf else s).encode('utf-8'))

edit('client_shell_main.cpp', [(
'''  const auto it = gViewerByHost.find(hostId);
  if (it == gViewerByHost.end() || it->second.operation != operation) return;
  recent_touch_host(hostId);
  commit_reservation(hostId);
}''', '''  const auto it = gViewerByHost.find(hostId);
  if (it == gViewerByHost.end() || it->second.operation != operation) return;
  recent_touch_host(hostId);
  // r4 (decision 1 / R4): the card that was being connected says so now -- its "connecting" mark ends (a press on
  // it is then "already open", not a second attempt). As the kept PC's picker-ready does.
  const auto name = gHostNames.find(hostId);
  post_host_status("idle", (name == gHostNames.end() ? hostId : name->second) + " 연결됨", hostId);
  commit_reservation(hostId);
}''')])

edit('ka_pc_shell_e2e_test.cpp', [(
'''      // the connect is posted from the output pump to the UI thread: give the UI its turn
      wait_for([] { return recent_for(recent_load(recent_path()), pc_origin(), "tester").size() >= 3; }, 5000);''',
'''      // the connect is posted from the output pump to the UI thread: give the UI its turn (the list already has R1's
      // entries, so wait for THIS order's newest one)
      wait_for([&] {
        const auto r = recent_for(recent_load(recent_path()), pc_origin(), "tester");
        return !r.empty() && r[0] == H("KA-C").id;
      }, 5000);'''), (
'''        const std::string hb = H("KA-B").proc.Text().substr(hostBFrom);
        const size_t up = hb.find("[input-timing]");''', '''        // the host ends its side when the viewer's close (or its silence) reaches it -- wait for that line
        wait_for([&] { return H("KA-B").proc.Text().substr(hostBFrom).find("udp control session ended") != std::string::npos; }, 15000);
        const std::string hb = H("KA-B").proc.Text().substr(hostBFrom);
        const size_t up = hb.find("[input-timing]");'''), (
'''        check(ups > 0 && up != std::string::npos && ended != std::string::npos && hb.find("eventKind=6") < ended,
              "R3: ...the host logged the key-ups (eventKind=6) BEFORE the session ended",
              std::to_string(ups) + " key-up(s); host injection off");''', '''        check(ups > 0 && up != std::string::npos && ended != std::string::npos && hb.find("eventKind=6") < ended &&
                  hb.find("injectionEnabled=1") == std::string::npos,
              "R3: ...the host logged the key-ups (eventKind=6) BEFORE the session ended (injection off)",
              std::to_string(ups) + " key-up(s)");''')])
print('ok')
