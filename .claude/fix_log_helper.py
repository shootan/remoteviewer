import io

BS = chr(92)
p = 'apps/native_poc/src/update_credential_hop_helper.cpp'
d = io.open(p, encoding='utf-8').read()

bad_start = d.find('  fprintf(file, "%02d:%02d:%02d [cred-hop] %s')
assert bad_start > 0, 'fprintf'
bad_end = d.find('  fclose(file);', bad_start)
assert bad_end > bad_start
good = ('  fprintf(file, "%02d:%02d:%02d [cred-hop] %s' + BS + 'n", now.wHour, now.wMinute,\n'
        '          now.wSecond, text.c_str());\n')
d = d[:bad_start] + good + d[bad_end:]
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('helper fprintf repaired')

# ---------------------------------------------------------------- the test
p = 'apps/native_poc/src/update_credential_hops_test.cpp'
d = io.open(p, encoding='utf-8').read()

pairs = [
 ('''/** Each process writes its own file; the test reads them together. */
std::string read_all(const std::wstring& base) {
  return read_one(base + L".bootstrap") + read_one(base + L".worker");
}''',
  '''/** Each process writes its own file; the test reads them together. */
std::string read_all(const std::wstring& base) {
  return read_one(base + L".bootstrap") + read_one(base + L".worker");
}

/** True when the fixture credential is nowhere in a file that was actually written to disk. */
bool file_is_clean(const std::wstring& path) {
  return read_one(path).find("fixture-token-not-a-real-credential") == std::string::npos;
}'''),
 ('''    Child bootstrap;
    const std::wstring command = L"\\"" + helper + L"\\" --role bootstrap --in \\"" + pipeName +
                                 L"\\" --child \\"" + helper + L"\\" --record \\"" + recordPath +
                                 L"\\" --deadline 8000";
    check("the bootstrap starts", start(command, &bootstrap));''',
  '''    const std::wstring logPath = dir + L"\\\\ok.log";
    Child bootstrap;
    const std::wstring command = L"\\"" + helper + L"\\" --role bootstrap --in \\"" + pipeName +
                                 L"\\" --child \\"" + helper + L"\\" --record \\"" + recordPath +
                                 L"\\" --log \\"" + logPath + L"\\" --deadline 8000";
    check("the bootstrap starts", start(command, &bootstrap));'''),
 ('''      check("the credential is in no command line and no environment block", !leaked);
    }
  }''',
  '''      check("the credential is in no command line and no environment block", !leaked);
    }

    // A log file, written to disk from the same error strings an updater's log is written from,
    // and then read back. "The message does not quote it" and "the file does not contain it" are
    // different statements, and only the second one is about a file.
    check("both processes wrote a log",
          !read_one(logPath + L".bootstrap").empty() && !read_one(logPath + L".worker").empty());
    check("...and the credential is in neither",
          file_is_clean(logPath + L".bootstrap") && file_is_clean(logPath + L".worker"));
    check("...while the log still says what happened",
          read_one(logPath + L".worker").find("credential received") != std::string::npos);
  }'''),
 ('''    const std::wstring recordPath = dir + L"\\\\noack.txt";''',
  '''    const std::wstring recordPath = dir + L"\\\\noack.txt";
    const std::wstring noackLog = dir + L"\\\\noack.log";'''),
 ('''                                 L"\\" --deadline 1500 --child-never-reads";''',
  '''                                 L"\\" --log \\"" + noackLog +
                                 L"\\" --deadline 1500 --child-never-reads";'''),
 ('''    check("...and the failure is recorded as a failure",
          record.find("bootstrap-serve-failed=") != std::string::npos);''',
  '''    check("...and the failure is recorded as a failure",
          record.find("bootstrap-serve-failed=") != std::string::npos);
    // The failure path writes the reason to the log. That reason must not carry the value.
    check("...and that log line does not quote the credential",
          file_is_clean(noackLog + L".bootstrap"));'''),
 ('''      DeleteFileW((dir + L"\\\\" + name + L".bootstrap").c_str());
      DeleteFileW((dir + L"\\\\" + name + L".worker").c_str());
    }''',
  '''      DeleteFileW((dir + L"\\\\" + name + L".bootstrap").c_str());
      DeleteFileW((dir + L"\\\\" + name + L".worker").c_str());
    }
    for (const wchar_t* name : {L"ok.log", L"noack.log"}) {
      DeleteFileW((dir + L"\\\\" + name + L".bootstrap").c_str());
      DeleteFileW((dir + L"\\\\" + name + L".worker").c_str());
    }'''),
]
for i, (old, new) in enumerate(pairs):
    assert d.count(old) == 1, 'pair %d: %s' % (i, old[:60])
    d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('test updated')
