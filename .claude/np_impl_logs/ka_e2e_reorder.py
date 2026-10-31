p = r'C:\work\gnlink-wt\ka-pc\apps\native_poc\src\ka_pc_managed_e2e_test.cpp'
b = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in b
s = b.replace('\r\n', '\n')

def cut(a_start, a_end):
    global s
    i = s.index(a_start)
    j = s.index(a_end, i)
    block = s[i:j]
    s = s[:i] + s[j:]
    return block

# 1. hosts: start only KA-A at first; KA-OLD later (one machine = one host record: the same machine id)
s = s.replace('''  wchar_t oldStream[1024] = {};
  const bool haveOld = GetEnvironmentVariableW(L"REMOTE60_KA_OLD_STREAM", oldStream, 1024) > 0;
  Child hostOld;
  if (haveOld) check("host KA-OLD (the published 0.2.163) starts", startHost(hostOld, oldStream, "KA-OLD"));
''', '''  // KA-OLD runs only after KA-A has ended: every host process on this machine registers under the same machine
  // id, so two at once would be one host record that the later one takes over.
  wchar_t oldStream[1024] = {};
  const bool haveOld = GetEnvironmentVariableW(L"REMOTE60_KA_OLD_STREAM", oldStream, 1024) > 0;
  Child hostOld;
''')
s = s.replace('''    std::vector<DirectoryHostEntry> hosts;
    if (!session.empty() && directory_list_hosts(url, session, &hosts, &err)) {
      for (const auto& h : hosts) {
        if (h.hostName == "KA-A" && h.online) idA = h.hostId;
        if (h.hostName == "KA-OLD" && h.online) idOld = h.hostId;
      }
    }
    if (!idA.empty() && (!haveOld || !idOld.empty())) break;
    Sleep(500);
  }
  check("both hosts are online in the directory", !idA.empty() && (!haveOld || !idOld.empty()), "A=" + idA + " old=" + idOld);''',
'''    std::vector<DirectoryHostEntry> hosts;
    if (!session.empty() && directory_list_hosts(url, session, &hosts, &err)) {
      for (const auto& h : hosts)
        if (h.hostName == "KA-A" && h.online) idA = h.hostId;
    }
    if (!idA.empty()) break;
    Sleep(500);
  }
  check("KA-A is online in the directory", !idA.empty(), "A=" + idA);''')

# 2. move the old-host block to the end (after the job close), preceded by ending host A and starting OLD
old_block = cut('  // ---------------------------------------------------------------- the unsupported Host\n',
                '  // ---------------------------------------------------------------- the message fence\n')
old_block = old_block.replace('''  if (haveOld && !idOld.empty()) {''', '''  hostA.End();
  if (haveOld) {
    check("host KA-OLD (the published 0.2.163) starts, after KA-A ended", startHost(hostOld, oldStream, "KA-OLD"));
    const uint64_t until = now_ms() + 30000;
    while (now_ms() < until && idOld.empty()) {
      std::vector<DirectoryHostEntry> hosts;
      if (directory_list_hosts(url, session, &hosts, &err))
        for (const auto& h : hosts)
          if (h.hostName == "KA-OLD" && h.online) idOld = h.hostId;
      if (idOld.empty()) Sleep(500);
    }
    check("KA-OLD is online in the directory", !idOld.empty(), "old=" + idOld);
  }
  if (haveOld && !idOld.empty()) {''')
anchor = '''  hostA.End();
  hostOld.End();
  server.End();'''
assert s.count(anchor) == 1
s = s.replace(anchor, old_block + '''  hostOld.End();
  server.End();''')
open(p, 'wb').write((s.replace('\n', '\r\n') if crlf else s).encode('utf-8'))
print('ok')
