p = r'C:\work\gnlink-wt\ka-pc\apps\native_poc\src\client_shell_main.cpp'
b = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in b
s = b.replace('\r\n', '\n')

def rep(a, c):
    global s
    assert s.count(a) == 1, (s.count(a), a[:100])
    s = s.replace(a, c)

rep('''  HANDLE testTerminate = nullptr;
#endif
};
std::map<std::string, ViewerCancelSlot> gViewerByHost;''', '''  HANDLE testTerminate = nullptr;
#endif
  // ka-pc-impl r5 (Codex R1-a): the PC this viewer is for -- kept with the slot so a viewer that a replacement moved
  // to gReplacedViewers still counts as that PC (last, so older brace-initialisers keep their meaning).
  std::string hostId;
};
std::map<std::string, ViewerCancelSlot> gViewerByHost;''')
rep('''  gViewerByHost[request.hostId].ownerEndEvent = ownerEndEvent;''', '''  gViewerByHost[request.hostId].ownerEndEvent = ownerEndEvent;
  gViewerByHost[request.hostId].hostId = request.hostId;''')
rep('''std::set<std::string> logical_pcs(const std::string& except = {}) {
  std::set<std::string> out;
  for (const auto& entry : gViewerByHost) out.insert(entry.first);''', '''std::set<std::string> logical_pcs(const std::string& except = {}) {
  std::set<std::string> out;
  for (const auto& entry : gViewerByHost) out.insert(entry.first);
  // r5 (Codex R1-a): a viewer a replacement kept (it had in fact connected) is that PC, open -- whether or not the
  // replacement started. Counted per PC: two viewers of one PC are one PC.
  for (const auto& r : gReplacedViewers)
    if (!r.hostId.empty()) out.insert(r.hostId);''')
rep('''bool pc_visible(const std::string& hostId) {
  if (gViewerByHost.count(hostId)) return true;''', '''bool pc_visible(const std::string& hostId) {
  if (gViewerByHost.count(hostId)) return true;
  for (const auto& r : gReplacedViewers)
    if (r.hostId == hostId) return true;  // r5 (Codex R1-a): a kept-on viewer of this PC is on screen''')
open(p, 'wb').write((s.replace('\n', '\r\n') if crlf else s).encode('utf-8'))
print('ok')
