p = r'D:\remote\remote\.claude\lkretry\ps_still_block.cpp.txt'
s = open(p, encoding='utf-8').read()
NL = chr(92) + 'n'
old = '''    // The viewer started in its picker; the worker's capture comes up behind it.
    Sleep(1500);
'''
new = '''    // The viewer started in its picker; the worker's capture comes up behind it.
    Sleep(1500);
    StillCover cover;
    const bool covered = !noCover && cover.Start();
    check("PS: the primary monitor is covered by a still window for this run (--no-cover: the desktop as it is)",
          noCover || covered);
    Sleep(1500);  // the cover is composed; whatever moved before is behind it now
'''
assert old in s
s = s.replace(old, new)
old = '''    check("PS: the worker never asked for WGC before login'''
assert old in s
s = s.replace(old, '''    cover.Stop();
''' + old)
old = '''              << " idle-detach=" << host.Count("capture detached (idle)") << "''' + NL + '''";
'''
assert old in s, 'idle line'
new = old + '''    // The host's own account of the run, for the evidence (stillness: content acquires per second).
    {
      std::istringstream lines(host.log());
      std::string line;
      while (std::getline(lines, line)) {
        for (const char* key : {"dxgi-acquire", "full-frame", "[desktop]", "bootstrap-cache", "stream-state", "capture detached",
                                "capture reattached", "stream active", "stream inactive", "trailing-edge kick",
                                "capture cannot continue", "desktop_backend="}) {
          if (line.find(key) != std::string::npos) {
            std::cout << "    host| " << line << "''' + NL + '''";
            break;
          }
        }
      }
    }
'''
s = s.replace(old, new)
open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('ok')
