R = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/'


def edit(name, reps):
    p = R + name
    s = open(p, encoding='utf-8', newline='').read()
    crlf = '\r\n' in s
    if crlf:
        s = s.replace('\r\n', '\n')
    for a, b in reps:
        assert s.count(a) == 1, (name, a[:90], s.count(a))
        s = s.replace(a, b)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'w', encoding='utf-8', newline='').write(s)
    print('ok', name)


edit('e2e_injection_guard.hpp', [
    # ---- the checked reads
    ('''// One whole line, said: 1 = exactly `want` up to its line end''', '''// r8 N1: how a read of a host's output ended. Eof only for a CONFIRMED end of output -- ReadFile TRUE with 0 bytes,
// or FALSE with ERROR_BROKEN_PIPE (an anonymous pipe whose last writer is gone; a joined reader is not that proof).
// Any other failure is Error (its code kept), after however many bytes came; a read stopped by its owner is Cut.
enum class E2eReadEnd : int { Running = 0, Eof = 1, Error = 2, Cut = 3 };

inline E2eReadEnd e2e_read_to_end(HANDLE h, const std::function<bool()>& keepGoing,
                                  const std::function<void(const char*, size_t)>& append, DWORD* err) {
  if (err) *err = 0;
  if (!h || h == INVALID_HANDLE_VALUE) {
    if (err) *err = ERROR_INVALID_HANDLE;
    return E2eReadEnd::Error;
  }
  char buf[8192];
  for (;;) {
    if (keepGoing && !keepGoing()) return E2eReadEnd::Cut;
    DWORD got = 0;
    if (!ReadFile(h, buf, sizeof(buf), &got, nullptr)) {
      const DWORD e = GetLastError();
      if (got > 0) append(buf, got);  // ERROR_MORE_DATA and the like hand over part of a message: kept, still an error
      if (e == ERROR_BROKEN_PIPE) return E2eReadEnd::Eof;
      if (err) *err = e;
      return E2eReadEnd::Error;
    }
    if (got == 0) return E2eReadEnd::Eof;
    append(buf, got);
  }
}

inline const char* e2e_read_end_name(E2eReadEnd e) {
  switch (e) {
    case E2eReadEnd::Running: return "still reading";
    case E2eReadEnd::Eof: return "end of output";
    case E2eReadEnd::Error: return "read error";
    case E2eReadEnd::Cut: return "read cut by its owner";
  }
  return "?";
}

// r8 N1: a launch's FINAL output: complete only when its whole text was read to a confirmed end of output.
struct E2eFinalOutput {
  bool complete = false;
  std::string text;
  std::string why;  // when not complete: what was not confirmed
};

// The final snapshot of a log file (the producer already ended): open, then read to EOF -- an open failure (a missing
// file) or a read error part-way is NOT complete (never an empty or partial text taken as the whole output).
inline E2eFinalOutput e2e_final_file(const std::wstring& path) {
  E2eFinalOutput f;
  HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    f.why = "its log could not be opened (error " + std::to_string(GetLastError()) + ")";
    return f;
  }
  DWORD err = 0;
  const E2eReadEnd end = e2e_read_to_end(h, nullptr, [&](const char* p, size_t n) { f.text.append(p, n); }, &err);
  CloseHandle(h);
  f.complete = end == E2eReadEnd::Eof;
  if (!f.complete) {
    f.why = std::string("its log: ") + e2e_read_end_name(end) + " (error " + std::to_string(err) + ") after " +
            std::to_string(f.text.size()) + " bytes";
  }
  return f;
}

// The final snapshot of a pipe a reader thread drained: complete only when that reader reached Eof.
inline E2eFinalOutput e2e_final_pipe(E2eReadEnd end, DWORD err, std::string text) {
  E2eFinalOutput f;
  f.text = std::move(text);
  f.complete = end == E2eReadEnd::Eof;
  if (!f.complete) {
    f.why = std::string("its pipe: ") + e2e_read_end_name(end) + " (error " + std::to_string(err) + ") after " +
            std::to_string(f.text.size()) + " bytes";
  }
  return f;
}

// One whole line, said: 1 = exactly `want` up to its line end'''),
    # ---- the final verdict (pure) after e2e_injection_contradicted
    ('''// Ends a job's processes and says whether that was confirmed''', '''// r8 N1: the final judgement of a launch, all of: its producer's end confirmed AND its output read to a confirmed end
// AND that final snapshot still holds the initial OFF lines (input + power, whole) AND nothing contradicts them.
// Empty = accepted; otherwise why it is not. (Nothing confirmed is never taken as "nothing said".)
inline std::string e2e_final_verdict(bool producerEnded, const E2eFinalOutput& fin) {
  if (!producerEnded) return "the producer's end is not confirmed (its output may still grow)";
  if (!fin.complete) return "the final output is not known complete: " + fin.why;
  std::string why;
  const int st = e2e_injection_state(fin.text, &why);
  if (st < 0) return why;
  if (st == 0) {
    return "the final output (" + std::to_string(fin.text.size()) +
           " bytes) does not hold the initial OFF lines (input injection + power management, whole)";
  }
  if (e2e_injection_contradicted(fin.text, &why)) return why;
  return {};
}

// r8 N2: the same for a launch that ends in a STARTUP FAULT before its input path is configured (the fatal test's
// --prelogin worker throws at startup_configure_from_env): its input-state line may legitimately be absent. Then:
// its end confirmed, its output complete, power management OFF said whole, and no contradiction anywhere (an input
// line that is present must be the explicit OFF; injectionEnabled=1 never). *inputSaid: whether the input line was there.
inline std::string e2e_startup_fault_verdict(bool producerEnded, const E2eFinalOutput& fin, bool* inputSaid) {
  if (inputSaid) *inputSaid = false;
  if (!producerEnded) return "the producer's end is not confirmed (its output may still grow)";
  if (!fin.complete) return "the final output is not known complete: " + fin.why;
  std::string why;
  if (e2e_injection_contradicted(fin.text, &why)) return why;
  const int power = e2e_power_state(fin.text, &why);
  if (power != 1) {
    return "the final output (" + std::to_string(fin.text.size()) + " bytes) does not say power management OFF, whole";
  }
  if (inputSaid) *inputSaid = e2e_whole_line(fin.text, "input injection ", e2e_injection_off_line()) == 1;
  return {};
}

// Ends a job's processes and says whether that was confirmed'''),
    # ---- the monitor
    ('''//   3. Finalize(outputComplete) -- AFTER the producer ended and the reader reached its end of output: the final
//      snapshot judged; output not complete (an end or EOF not confirmed) is INVALID too.''', '''//   3. Finalize(producerEnded, final) -- AFTER the producer ended and its output was read to its end: the final
//      snapshot (given, not re-read) judged by e2e_final_verdict -- the end, the EOF and the OFF lines each confirmed,
//      else INVALID (r8 N1).
// The job is BORROWED: its owner keeps it open until Finalize returned (or the monitor is gone); the monitor drops it
// at Finalize / destruction, so nothing later acts on a closed (possibly reused) handle value.'''),
    ('''    std::function<std::string()> output;  // released at Finalize / destruction (the producer's owner may go)
    HANDLE job = nullptr;''', '''    std::function<std::string()> output;  // released at Finalize / destruction (the producer's owner may go)
    HANDLE job = nullptr;                  // borrowed; dropped at Finalize / destruction (r8 N1)
    size_t finalBytes = 0;'''),
    ('''    if (!st_->finalized) st_->abandoned = true;  // never judged on its complete output
    st_->output = nullptr;
  }''', '''    if (!st_->finalized) st_->abandoned = true;  // never judged on its complete output
    st_->output = nullptr;
    st_->job = nullptr;  // r8 N1: the owner may close it after this
  }'''),
    ('''  // the producer has ended and its output was read to the end (outputComplete) -- the final snapshot judged
  void Finalize(bool outputComplete) {
    StopPolling();
    {
      std::lock_guard<std::mutex> lock(st_->mu);
      if (st_->finalized) return;  // once
    }
    if (!outputComplete) {
      e2e_injection_run_invalid(st_->where.c_str(), "the final output is not known complete (the host's end or the "
                                "reader's end of output not confirmed)", st_->job);
    }
    Judge(*st_);
    std::lock_guard<std::mutex> lock(st_->mu);
    st_->finalized = true;
    st_->output = nullptr;
  }''', '''  // r8 N1: the producer's end (confirmed or not) and its final output (complete or not) -- judged together; the job
  // must still be open here (its owner closes it after)
  void Finalize(bool producerEnded, const E2eFinalOutput& final) {
    StopPolling();
    {
      std::lock_guard<std::mutex> lock(st_->mu);
      if (st_->finalized) return;  // once
    }
    const std::string why = e2e_final_verdict(producerEnded, final);
    if (!why.empty()) e2e_injection_run_invalid(st_->where.c_str(), "final judgement: " + why, st_->job);
    std::lock_guard<std::mutex> lock(st_->mu);
    st_->finalized = true;
    st_->finalBytes = final.text.size();
    st_->output = nullptr;
    st_->job = nullptr;
  }'''),
    ('''  int finalized = 0, live = 0;
  for (auto& st : v) {
    bool fin = false, aband = false;
    std::string where;
    {
      std::lock_guard<std::mutex> lock(st->mu);
      fin = st->finalized;
      aband = st->abandoned;
      where = st->where;
    }
    if (aband) e2e_injection_run_invalid(where.c_str(), "a launch was never judged on its complete output", st->job);
    if (fin) {
      ++finalized;''', '''  int finalized = 0, live = 0;
  size_t finalBytes = 0;
  for (auto& st : v) {
    bool fin = false, aband = false;
    std::string where;
    HANDLE job = nullptr;
    {
      std::lock_guard<std::mutex> lock(st->mu);
      fin = st->finalized;
      aband = st->abandoned;
      where = st->where;
      job = st->job;  // null once finalized / abandoned (r8 N1: never a closed handle)
      if (fin) finalBytes += st->finalBytes;
    }
    if (aband) e2e_injection_run_invalid(where.c_str(), "a launch was never judged on its complete output", job);
    if (fin) {
      ++finalized;'''),
    ('''  std::printf("PASS  input / power state: no contradiction during the run (%zu launch(es): %d judged on their complete "
              "output, %d still running judged now)\\n", v.size(), finalized, live);''', '''  std::printf("PASS  input / power state: no contradiction during the run (%zu launch(es): %d judged on their complete "
              "output -- end, EOF and OFF lines confirmed, %zu bytes --, %d still running judged now)\\n", v.size(), finalized,
              finalBytes, live);'''),
])
