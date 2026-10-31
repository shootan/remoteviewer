p = r'C:\work\gnlink-wt\ka-pc\apps\native_poc\src\ka_pc_managed_e2e_test.cpp'
b = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in b
s = b.replace('\r\n', '\n')
a = '''    a.Send(Type::Activate, {{"op", "7"}});
    Sleep(20);
    a.Send(Type::Release, {{"reason", "test"}});'''
assert s.count(a) == 1
s = s.replace(a, '''    // Back to back: the Release is already queued when the activation starts. (20 ms later was measured to be too
    // late here -- the warm lease's handshake had already connected, and the fence then closed an ACTIVE session:
    // the other case, run in the shell e2e.)
    a.Send(Type::Activate, {{"op", "7"}});
    a.Send(Type::Release, {{"reason", "test"}});''')
open(p, 'wb').write((s.replace('\n', '\r\n') if crlf else s).encode('utf-8'))

p = r'C:\work\gnlink-wt\ka-pc\apps\native_poc\src\ka_pc_shell_e2e_test.cpp'
s = open(p, encoding='utf-8').read()
def rep(a, c):
    global s
    assert s.count(a) == 1, (s.count(a), a[:80])
    s = s.replace(a, c)
rep('''    {
      using namespace remote60::native_poc::shell;
      const auto recent = recent_for(recent_load(recent_path()), pc_origin(), "tester");
      const std::string order = recent.size() >= 3 ? recent[0] + "," + recent[1] + "," + recent[2] : "(short)";''',
'''    {
      using namespace remote60::native_poc::shell;
      // the connect is posted from the output pump to the UI thread: give the UI its turn
      wait_for([] { return recent_for(recent_load(recent_path()), pc_origin(), "tester").size() >= 3; }, 5000);
      const auto recent = recent_for(recent_load(recent_path()), pc_origin(), "tester");
      const std::string order = recent.size() >= 3 ? recent[0] + "," + recent[1] + "," + recent[2] : "(short)";''')
rep('''      H("KA-D").proc.End();  // D's host goes away; D's lease has not noticed yet
      check(tap("KA-D"), "tap kept KA-D right after its host went away");
      check(log_has("kept PC session ended host=" + H("KA-D").id, 60000), "D's activation ended");
      const std::string l = client_log();
      const size_t at = l.rfind("kept PC session ended host=" + H("KA-D").id);''',
'''      H("KA-D").proc.End();  // D's host goes away; D's lease has not noticed yet
      const size_t logFrom = client_log().size();
      check(tap("KA-D"), "tap kept KA-D right after its host went away");
      const std::string endedTag = "kept PC session ended host=" + H("KA-D").id;
      check(wait_for([&] { return client_log().substr(logFrom).find(endedTag) != std::string::npos; }, 60000),
            "D's activation ended");
      const std::string l = client_log().substr(logFrom);
      const size_t at = l.find(endedTag);''')
rep('''      check(code == 0 && client_log().substr(logBefore).find("ended by its handle") == std::string::npos,''',
'''      check(code == 0 && client_log().substr(logBefore).find("did not close in time") == std::string::npos,''')
open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('ok')
