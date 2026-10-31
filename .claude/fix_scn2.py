import io

BS = chr(92)
p = 'apps/native_poc/src/updater_scenarios_test.cpp'
d = io.open(p, encoding='utf-8').read()

# a sweep for anything running out of the temp install directory
old = '''/** Names this file uses. Nothing here can name a product binary. */'''
new = '''/**
 * Every process running an image under `dir`, and how many there are of each.
 *
 * The scenarios start real processes, and a shell-routed launch hands off asynchronously -- so
 * the pid it eventually gets is sometimes not available when the launch returns. Counting by
 * image afterwards is the reliable way to ask "what is running", and it is also the question the
 * assertions actually want: not "did we track it" but "is there exactly one of it".
 *
 * Scoped to the temp directory, so nothing outside this test is ever counted or touched.
 */
std::vector<std::pair<std::wstring, DWORD>> running_under(const std::wstring& dir) {
  std::vector<std::pair<std::wstring, DWORD>> found;
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) return found;
  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  if (Process32FirstW(snapshot, &entry)) {
    do {
      HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
      if (!h) continue;
      wchar_t image[MAX_PATH]{};
      DWORD size = MAX_PATH;
      if (QueryFullProcessImageNameW(h, 0, image, &size)) {
        const std::wstring path = image;
        if (path.size() > dir.size() && _wcsnicmp(path.c_str(), dir.c_str(), dir.size()) == 0) {
          found.push_back({path, entry.th32ProcessID});
        }
      }
      CloseHandle(h);
    } while (Process32NextW(snapshot, &entry));
  }
  CloseHandle(snapshot);
  return found;
}

/** Stops everything running out of `dir`. Only this test's temp tree is ever passed here. */
void stop_everything_under(const std::wstring& dir) {
  for (const auto& pair : running_under(dir)) {
    HANDLE h = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pair.second);
    if (!h) continue;
    TerminateProcess(h, 0);
    WaitForSingleObject(h, 3000);
    CloseHandle(h);
  }
}

/** Names this file uses. Nothing here can name a product binary. */'''
assert old in d, 'helper anchor'
d = d.replace(old, new, 1)
d = d.replace('#include <windows.h>', '#include <windows.h>\n\n#include <tlhelp32.h>', 1)
d = d.replace('#include <fstream>', '#include <fstream>\n#include <utility>', 1)

# Result gains a per-image count; the cleanup sweeps the whole install directory
old = '''  struct Result {
    UpdateOutcome outcome;
    int liveAfter = 0;      // processes this run started that are still alive
    bool backupsLeft = false;
    std::string hostBytes;
  };'''
new = '''  struct Result {
    UpdateOutcome outcome;
    /** How many processes are running each payload image afterwards. */
    int hostInstances = 0;
    int clientInstances = 0;
    bool backupsLeft = false;
    std::string hostBytes;
  };'''
assert old in d, 'Result'
d = d.replace(old, new, 1)

old = '''    r.hostBytes = read_file(install + L"@@" + kHostName);
    for (DWORD pid : *started) {
      HANDLE h = OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE, FALSE, pid);
      if (!h) continue;
      if (WaitForSingleObject(h, 0) == WAIT_TIMEOUT) {
        ++r.liveAfter;
        TerminateProcess(h, 0);  // this test cleans up after itself
        WaitForSingleObject(h, 2000);
      }
      CloseHandle(h);
    }
    return r;'''.replace('@@', BS * 2)
new = '''    r.hostBytes = read_file(install + L"@@" + kHostName);
    // Counted by image rather than by tracked pid: a shell-routed launch hands off
    // asynchronously, so the pid is sometimes not known when the launch returns -- and what the
    // assertions want is "how many of it are running", not "did we manage to note it down".
    for (const auto& pair : running_under(install)) {
      if (pair.first.find(kHostName) != std::wstring::npos) ++r.hostInstances;
      if (pair.first.find(kClientName) != std::wstring::npos) ++r.clientInstances;
    }
    (void)started;
    // Everything this scenario left running goes, so the next one starts from a directory nobody
    // is holding open. Without this a leftover process makes the NEXT scenario fail its swap,
    // which is what happened and looked like a defect in the swap.
    stop_everything_under(install);
    return r;'''.replace('@@', BS * 2)
assert old in d, 'result tail'
d = d.replace(old, new, 1)

# scenario 4's process assertion: after a rollback the restored build SHOULD be running
old = '''    check("4 new build unhealthy: nothing this run started is still alive", r.liveAfter == 0,
          std::to_string(r.liveAfter));'''
new = '''    // The restored build is started again, so something running is the CORRECT outcome here --
    // what must not happen is two of it. The first version of this asserted nothing was running,
    // which would have been satisfied by a rollback that left the machine down.
    check("4 new build unhealthy: the restored build is running", r.hostInstances >= 1,
          std::to_string(r.hostInstances));
    check("4 new build unhealthy: and exactly one of it, not the new one as well",
          r.hostInstances == 1, std::to_string(r.hostInstances));'''
assert old in d, 'scenario 4'
d = d.replace(old, new, 1)

old = '''    check("6 quiesce incomplete: nothing was started, so nothing is left running",
          r.liveAfter == 0, std::to_string(r.liveAfter));'''
new = '''    check("6 quiesce incomplete: the stubborn process was not duplicated",
          r.hostInstances <= 1, std::to_string(r.hostInstances));'''
assert old in d, 'scenario 6'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('scenario accounting fixed')
