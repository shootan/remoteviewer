p = r'C:\work\gnlink-wt\ka-pc\apps\native_poc\src\ka_pc_managed_e2e_test.cpp'
b = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in b
s = b.replace('\r\n', '\n')
i = s.index('// FINDING (10-28, reported): a NEW lease')
j = s.index('HWND viewer_window(DWORD pid) {')
s = s[:i] + '''// The shared FINDING of r1 (a NEW lease of the same PC key within ~30 s of its previous admission got no answer) was
// fixed in the shared code (ka-r3 a7773e6: the Host's punch trains no longer block its directory cycle) and merged
// here. The cases below therefore start the same PC key again AT ONCE -- after EndAll, after a crash -- and count it.
// (Before the fix this helper waited 31 s first.)
void wait_after_admission(const std::string& hostLog, uint64_t* lastAdmitAt) {
  (void)hostLog;
  (void)lastAdmitAt;
}

''' + s[j:]
a = 'std::printf("FINDING  a new viewer for the same PC right after a crash: %s' + chr(92) + 'n", got.c_str());'
assert s.count(a) == 1, s.count(a)
s = s.replace(a, '''check("a new viewer for the same PC right after a crash is ready (the shared FINDING, fixed in a7773e6)",
            got == "ready", got);''')
s = s.replace('// FINDING (reported, not counted): IMMEDIATELY after a crash',
              '// (r2: counted since the shared fix a7773e6) IMMEDIATELY after a crash')
open(p, 'wb').write((s.replace('\n', '\r\n') if crlf else s).encode('utf-8'))

p = r'C:\work\gnlink-wt\ka-pc\apps\native_poc\src\ka_pc_shell_e2e_test.cpp'
s = open(p, encoding='utf-8').read()
a = '''      // A again: its key was last admitted at the three conversions -- wait out the shared FINDING window first.
      while (GetTickCount64() < admittedABC + 31000) pump(500);'''
assert s.count(a) == 1
s = s.replace(a, '''      // A again, right after it was let go (evicted): with the shared fix (a7773e6) no wait is needed.
      (void)admittedABC;''')
a = '''// The shared standby FINDING (a new lease of the same PC key within ~30 s of its previous admission gets no answer)
// is respected: no PC key is re-leased here within 31 s of its last admission.'''
assert s.count(a) == 1
s = s.replace(a, '''// The shared standby FINDING of r1 (a new lease of the same PC key within ~30 s of its previous admission got no
// answer) is fixed in the shared code (a7773e6, merged): A is kept again right after its eviction, without a wait.''')
open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('ok')
