import io

BS = chr(92)
p = 'apps/native_poc/src/update_relaunch_test.cpp'
d = io.open(p, encoding='utf-8').read()

d = d.replace('''#include "update_health.hpp"
#include "update_relaunch_plan.hpp"

#include <iostream>
#include <string>
#include <vector>''', '''#include "update_health.hpp"
#include "update_health_log.hpp"
#include "update_relaunch_plan.hpp"

#include <windows.h>

#include <fstream>
#include <iostream>
#include <string>
#include <vector>''', 1)

d = d.replace('''std::string narrow(const std::wstring& s) { return std::string(s.begin(), s.end()); }

}  // namespace''', '''std::string narrow(const std::wstring& s) { return std::string(s.begin(), s.end()); }

std::wstring temp_log_path() {
  wchar_t base[MAX_PATH]{};
  GetTempPathW(MAX_PATH, base);
  wchar_t path[MAX_PATH]{};
  swprintf(path, MAX_PATH, L"%sgnlink-healthtest-%lu.log", base, GetCurrentProcessId());
  return path;
}

void append_line(const std::wstring& path, const std::string& line) {
  std::ofstream out(path, std::ios::binary | std::ios::app);
  out << line << "@@n";
}

}  // namespace'''.replace('@@', BS), 1)

anchor = '''  std::cout << (gFailures == 0 ? "RESULT: ALL PASS  (" : "RESULT: FAILED  (") << gChecks'''
block = '''  // ---------------------------------------------------------------- evidence has to be new
  //
  // The failure this whole arrangement exists to prevent: a log already contains a health report
  // from every previous run, including the version the update just replaced. A check that reads
  // the file reports success in exactly the case it was written to catch.

  {
    const std::wstring log = temp_log_path();
    DeleteFileW(log.c_str());

    // A log that does not exist yet has nothing in it, and everything from now on is new.
    check("a missing log marks at zero", file_size_or_zero(log) == 0);
    check("and reading it yields nothing", read_from_offset(log, 0).empty());

    // The previous version ran, and ran fine. This is the trap.
    append_line(log, "09-08 11:00:00 [host-app] starting the streaming host (tune=low_latency)");
    append_line(log, "09-08 11:00:01 [host-app] health version=0.2.104 directory=ok");

    // Reading the whole file finds it, which is exactly why nothing reads the whole file.
    {
      const HealthSignals whole = scan_health_report(read_from_offset(log, 0));
      std::string detail;
      check("read from the start, the OLD run looks healthy for the OLD version",
            judge_health(whole, "0.2.104", &detail) == HealthVerdict::Healthy, detail);
    }

    // The mark is taken here -- before anything is relaunched.
    const uint64_t mark = file_size_or_zero(log);
    check("the mark is where the log ended", mark > 0, std::to_string(mark));

    {
      // Nothing new yet. THIS is the assertion that matters: the old report must not be able to
      // satisfy the check, not even for the version it names.
      const std::string fresh = read_from_offset(log, mark);
      check("nothing appended reads as nothing", fresh.empty(), fresh);
      const HealthSignals signals = scan_health_report(fresh);
      std::string detail;
      check("so the previous run's report is not evidence for this one",
            judge_health(signals, "0.2.105", &detail) == HealthVerdict::Waiting, detail);
      check("and not for its own version either",
            judge_health(signals, "0.2.104", &detail) == HealthVerdict::Waiting, detail);
    }

    {
      // The new build comes up, but reports the version it replaced -- something is still running
      // the old files. Distinguished from "not yet", because waiting cannot fix it.
      append_line(log, "09-08 12:00:00 [host-app] health version=0.2.104 directory=ok");
      const HealthSignals signals = scan_health_report(read_from_offset(log, mark));
      std::string detail;
      check("a fresh report of the old version is a failure, not a delay",
            judge_health(signals, "0.2.105", &detail) == HealthVerdict::WrongVersion, detail);
    }

    {
      // And now the real thing.
      append_line(log, "09-08 12:00:02 [host-app] health version=0.2.105 directory=pending");
      const HealthSignals pending = scan_health_report(read_from_offset(log, mark));
      std::string detail;
      check("the new version reporting pending means keep waiting",
            judge_health(pending, "0.2.105", &detail) == HealthVerdict::Waiting, detail);

      append_line(log, "09-08 12:00:05 [host-app] health version=0.2.105 directory=ok");
      const HealthSignals ok = scan_health_report(read_from_offset(log, mark));
      check("and once the directory answers, it is healthy",
            judge_health(ok, "0.2.105", &detail) == HealthVerdict::Healthy, detail);
    }

    {
      // A log that was rotated away under us is smaller than the mark. Its remaining bytes belong
      // to a different file, so they are not this run's evidence.
      const uint64_t beyond = file_size_or_zero(log) + 1000;
      check("a mark past the end reads as nothing", read_from_offset(log, beyond).empty());
    }

    {
      // The product writes to this file while the check reads it. A check that opened it
      // exclusively would be preventing the evidence it is waiting for.
      HANDLE writer = CreateFileW(log.c_str(), FILE_APPEND_DATA,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
      check("the log can be held open for writing", writer != INVALID_HANDLE_VALUE);
      const HealthSignals signals = scan_health_report(read_from_offset(log, mark));
      std::string detail;
      check("and it is still readable while held",
            judge_health(signals, "0.2.105", &detail) == HealthVerdict::Healthy, detail);
      if (writer != INVALID_HANDLE_VALUE) CloseHandle(writer);
    }

    DeleteFileW(log.c_str());
  }

''' + anchor
assert anchor in d, 'tail anchor'
d = d.replace(anchor, block, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('health log regressions added')
