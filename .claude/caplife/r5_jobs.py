import re

R = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/'
NS = 'remote60::native_poc::e2e::'

# ---- the helper
p = R + 'e2e_injection_guard.hpp'
s = open(p, encoding='utf-8').read()
a = '''// Rule 4: a host that took input while injection was on is a contradiction'''
assert s.count(a) == 1
s = s.replace(a, '''// r5 (Codex N2): a child started CREATE_SUSPENDED runs only once it is provably inside a kill-on-close job: the job
// exists, its limit really holds JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE (read back, not assumed from the setter), the
// child is assigned to it -- then, and only then, it is resumed. Any failure: the still-suspended child is ended
// through this test's own handle (no PID / name kill), waited for, its handles closed, and the test ends INVALID.
// So a later fail-closed exit (std::_Exit, no destructors) still takes every running child with the job.
inline void e2e_adopt_or_invalid(HANDLE job, PROCESS_INFORMATION& pi, const char* where) {
  std::string why;
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
  if (!job) {
    why = "no job object";
  } else if (!QueryInformationJobObject(job, JobObjectExtendedLimitInformation, &info, sizeof(info), nullptr) ||
             !(info.BasicLimitInformation.LimitFlags & JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE)) {
    why = "the job does not hold kill-on-close";
  } else if (!AssignProcessToJobObject(job, pi.hProcess)) {
    why = "the child could not be put in the job (" + std::to_string(GetLastError()) + ")";
  } else if (ResumeThread(pi.hThread) == static_cast<DWORD>(-1)) {
    why = "the child could not be resumed (" + std::to_string(GetLastError()) + ")";
  }
  if (why.empty()) return;
  if (pi.hProcess) {
    TerminateProcess(pi.hProcess, 1);
    WaitForSingleObject(pi.hProcess, 10000);
    CloseHandle(pi.hProcess);
    pi.hProcess = nullptr;
  }
  if (pi.hThread) {
    CloseHandle(pi.hThread);
    pi.hThread = nullptr;
  }
  std::printf("FAIL  SETUP %s: %s -- the child was ended before it ran\\nRESULT: INVALID\\n", where, why.c_str());
  std::fflush(stdout);
  std::_Exit(2);
}

// Rule 4: a host that took input while injection was on is a contradiction''')
open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('ok header')

SITES = {
    'os_input_record_e2e_test.cpp': [('job', 'hostPi', 'os_input_record')],
    'android_session_reboot_e2e_test.cpp': [('job', 'hostPi', 'android_session_reboot')],
    'bwe_observe_e2e_test.cpp': [('job', 'hostPi', 'bwe_observe')],
    'host_control_deadline_e2e_test.cpp': [('job', 'hostPi', 'host_control_deadline')],
    'host_two_dispatcher_e2e_test.cpp': [('job', 'hostPi', 'host_two_dispatcher')],
    'host_standby_admission_e2e_test.cpp': [('host->job', 'host->pi', 'host_standby_admission'), ('job', 'p', 'host_standby_admission (directory)')],
    'host_punch_reply_e2e_test.cpp': [('host->job', 'host->pi', 'host_punch_reply')],
    'host_migration_e2e_test.cpp': [('host->job', 'host->pi', 'host_migration')],
    'host_reboot_delegate_e2e_test.cpp': [('host->job', 'host->pi', 'host_reboot_delegate')],
}
for f, sites in SITES.items():
    p = R + f
    s = open(p, encoding='utf-8', newline='').read()
    crlf = '\r\n' in s
    if crlf:
        s = s.replace('\r\n', '\n')
    for job, pi, tag in sites:
        pat = re.compile(r'([ \t]*)AssignProcessToJobObject\(' + re.escape(job) + r', ' + re.escape(pi) + r'\.hProcess\);\n[ \t]*ResumeThread\(' + re.escape(pi) + r'\.hThread\);')
        m = pat.findall(s)
        assert len(m) == 1, (f, job, pi, len(m))
        s = pat.sub(lambda mm: mm.group(1) + '// r5 (Codex N2): resumed only inside a verified kill-on-close job, else INVALID\n' + mm.group(1) + NS + 'e2e_adopt_or_invalid(' + job + ', ' + pi + ', "' + tag + '");', s)
    if '#include "e2e_injection_guard.hpp"' not in s:
        last = list(re.finditer(r'^#include "[^"]+"\n', s, flags=re.M))[-1]
        s = s[:last.end()] + '#include "e2e_injection_guard.hpp"\n' + s[last.end():]
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'w', encoding='utf-8', newline='').write(s)
    print('ok', f)
