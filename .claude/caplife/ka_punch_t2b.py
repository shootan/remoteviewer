p = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/host_agent_punch_test.cpp'
s = open(p, encoding='utf-8').read()


def rep(a, b):
    global s
    assert s.count(a) == 1, (a[:80], s.count(a))
    s = s.replace(a, b)


reps = [("""  if (is_reparse(root)) { *why = "the test root is a link"; return false; }
  gRoot.clear();
  for (wchar_t ch : root) gRoot.push_back(static_cast<char>(ch));
  return true;
}""", """  // r4 (Codex T2-a): what is there now must BE a folder -- looked up, a directory, no link (a plain file of that name
  // used to pass: CreateDirectory said "already exists" and only the link bit was read)
  const DWORD rootAttr = GetFileAttributesW(root.c_str());
  if (rootAttr == INVALID_FILE_ATTRIBUTES || !(rootAttr & FILE_ATTRIBUTE_DIRECTORY) || (rootAttr & FILE_ATTRIBUTE_REPARSE_POINT)) {
    *why = "the test root is not a plain folder";
    return false;
  }
  // r4 (Codex T2-b): this run's own folder in it, made NEW (CreateDirectory fails on any existing name -- a folder, a
  // file, another run's): what is in it is this run's alone. Nothing that was already there is touched.
  for (int attempt = 0; attempt < 16; ++attempt) {
    const std::wstring run = root + L"\\\\run-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                             std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(attempt);
    if (CreateDirectoryW(run.c_str(), nullptr)) {
      if (is_reparse(run)) { *why = "this run's folder is a link"; return false; }
      gRoot.clear();
      for (wchar_t ch : run) gRoot.push_back(static_cast<char>(ch));
      return true;
    }
    if (GetLastError() != ERROR_ALREADY_EXISTS) break;
  }
  *why = "no new folder for this run could be made";
  return false;
}"""),
("// The one delete this test does: a file directly in its root, of this run's own name.",
 "// The one delete this test does: a file directly in THIS run's own new folder (gRoot is that folder)."),
("""  // this run's own file in the root (an unreachable directory never registers: the agent never writes it -- the name
  // is still unique and the delete still guarded)
  const std::string cache = gRoot + "\\\\host-agent-punch-" + std::to_string(GetCurrentProcessId()) + ".cache";
  delete_own_file(cache);
""", """  // in this run's own new folder: nothing to clear first (r4 T2-b -- no pre-delete of a name another run could hold)
  const std::string cache = gRoot + "\\\\host-agent.cache";
"""),
("""  delete_own_file(cache);
  std::printf("\\n%d checks, %d failures""", """  delete_own_file(cache);  // only if the agent wrote it; in this run's folder either way
  std::wstring wroot;
  for (char ch : gRoot) wroot.push_back(static_cast<wchar_t>(ch));
  RemoveDirectoryW(wroot.c_str());  // this run's folder, not recursive: anything unexpected in it stays
  std::printf("\\n%d checks, %d failures"""),
]
for a, b in reps:
    rep(a, b)
open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
