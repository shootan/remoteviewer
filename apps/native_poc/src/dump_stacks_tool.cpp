// remote60_dump_stacks: read a host minidump with the Windows Debugger Engine (dbgeng.dll, part of
// Windows) and print every thread's symbolised stack, plus which threads are waiting on a lock.
//
//   remote60_dump_stacks.exe <hang-pidN.dmp> <dir with GNLinkStream.exe + GNLinkStream.pdb> [--msdl]
//
// encoder-rc-budget r9 ⑥ (test-support; never shipped). dbgeng owns the unwinder, the module list and
// the symbol lookup -- the same engine WinDbg/cdb use -- so this needs no debugger installed: dbgeng.dll
// ships with Windows. The symbol path is the given directory (the host's PDB, read by RSDS GUID/Age) and,
// only with --msdl, the Microsoft public symbol server (network!) for ntdll/kernelbase/d3d11 names and
// exact unwind data of another machine's system DLLs. Without --msdl, system frames print as
// module+offset; the host's own frames print with names either way.
//
// Lock waiters: a thread whose top frames are in NtWaitForAlertByThreadId / RtlAcquireSRWLock* /
// RtlpWaitOnCriticalSection / WaitForSingleObject is reported as WAITING with that function. The owner of
// an SRW lock (what std::mutex is on MSVC) is not recorded in the lock word in a readable way, so the
// owner is not asserted -- the stacks of the non-waiting threads are where to look.
//
// Exit 0 when the dump opened, the host module's PDB was matched (SymType PDB) and at least one frame
// resolved to a GNLinkStream symbol; 1 otherwise.
#include <windows.h>
#include <dbghelp.h>  // SYMOPT_* flags
#include <dbgeng.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#pragma comment(lib, "dbgeng.lib")
#pragma comment(lib, "ole32.lib")

namespace {

struct OutputSink : public IDebugOutputCallbacks {
  std::string text;
  STDMETHOD(QueryInterface)(REFIID iid, PVOID* out) override {
    if (iid == __uuidof(IUnknown) || iid == __uuidof(IDebugOutputCallbacks)) { *out = this; return S_OK; }
    *out = nullptr;
    return E_NOINTERFACE;
  }
  STDMETHOD_(ULONG, AddRef)() override { return 1; }
  STDMETHOD_(ULONG, Release)() override { return 1; }
  STDMETHOD(Output)(ULONG, PCSTR t) override { text += t; return S_OK; }
};

std::wstring widen(const char* s) { return std::wstring(s, s + std::strlen(s)); }

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::printf("usage: remote60_dump_stacks <dump.dmp> <symbol dir with exe+pdb> [--msdl] [--host-module GNLinkStream.exe]\n");
    return 2;
  }
  const char* dumpPath = argv[1];
  const char* symDir = argv[2];
  bool msdl = false;
  std::string hostModule = "GNLinkStream";
  for (int i = 3; i < argc; ++i) {
    if (std::strcmp(argv[i], "--msdl") == 0) msdl = true;
    if (std::strcmp(argv[i], "--host-module") == 0 && i + 1 < argc) {
      hostModule = argv[++i];
      const size_t dot = hostModule.rfind('.');
      if (dot != std::string::npos) hostModule = hostModule.substr(0, dot);
    }
  }

  IDebugClient5* client = nullptr;
  HRESULT hr = DebugCreate(__uuidof(IDebugClient5), reinterpret_cast<void**>(&client));
  if (FAILED(hr) || !client) {
    std::printf("DUMP-STACKS: DebugCreate failed hr=0x%08lx (dbgeng.dll missing?)\n", hr);
    return 1;
  }
  IDebugControl4* control = nullptr;
  IDebugSymbols3* symbols = nullptr;
  client->QueryInterface(__uuidof(IDebugControl4), reinterpret_cast<void**>(&control));
  client->QueryInterface(__uuidof(IDebugSymbols3), reinterpret_cast<void**>(&symbols));
  if (!control || !symbols) {
    std::printf("DUMP-STACKS: dbgeng interfaces unavailable\n");
    return 1;
  }
  OutputSink sink;
  client->SetOutputCallbacks(&sink);

  std::string symPath = symDir;
  if (msdl) symPath += ";srv*" + std::string(getenv("TEMP") ? getenv("TEMP") : ".") + "\\symcache*https://msdl.microsoft.com/download/symbols";
  symbols->SetSymbolPath(symPath.c_str());
  symbols->SetImagePath(symDir);  // the host image beside its PDB; system images are read from this machine
  symbols->SetSymbolOptions(SYMOPT_UNDNAME | SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS | SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_NO_PROMPTS);

  hr = client->OpenDumpFileWide(widen(dumpPath).c_str(), 0);
  if (FAILED(hr)) {
    std::printf("DUMP-STACKS: OpenDumpFile failed hr=0x%08lx %s\n", hr, dumpPath);
    return 1;
  }
  hr = control->WaitForEvent(0, INFINITE);
  if (FAILED(hr)) {
    std::printf("DUMP-STACKS: WaitForEvent failed hr=0x%08lx\n", hr);
    return 1;
  }
  std::printf("DUMP-STACKS: %s\nDUMP-STACKS: symbol path %s\n", dumpPath, symPath.c_str());

  auto run = [&](const char* cmd) -> std::string {
    sink.text.clear();
    control->Execute(DEBUG_OUTCTL_THIS_CLIENT | DEBUG_OUTCTL_NOT_LOGGED, cmd, DEBUG_EXECUTE_DEFAULT | DEBUG_EXECUTE_NO_REPEAT);
    return sink.text;
  };

  // the host module: does its PDB match? dbgeng names a module by its file name with '.' -> '_'.
  // every thread's stack (this is what loads the symbols; the module question comes after)
  const std::string stacks = run("~*kvn 48");
  std::printf("%s", stacks.c_str());

  // the host module: does its PDB match? The engine lists the module as GNLinkStream_exe until a reload
  // names it GNLinkStream -- ask for the first, then the second. Deferred symbols are forced with `ld`.
  std::string dbgName = hostModule + "_exe";
  (void)run(("ld " + dbgName).c_str());
  std::string lm = run(("lmvm " + dbgName).c_str());
  if (lm.find("Image path") == std::string::npos) {
    dbgName = hostModule;
    (void)run(("ld " + dbgName).c_str());
    lm = run(("lmvm " + dbgName).c_str());
  }
  std::printf("%s", lm.c_str());
  // lmvm prints "(private pdb symbols)  <path>" (or "(pdb symbols)") on the module line when the PDB was
  // opened and matched; a mismatch says so in words.
  const bool pdbMatched = lm.find("pdb symbols") != std::string::npos && lm.find("MISMATCH") == std::string::npos &&
                          lm.find("Loaded symbol image file") != std::string::npos;
  std::printf("DUMP-STACKS: host module %s pdb-matched=%d\n", dbgName.c_str(), pdbMatched ? 1 : 0);
  const bool hostFrames = stacks.find(hostModule + "_exe!remote60::") != std::string::npos ||
                          stacks.find(hostModule + "!remote60::") != std::string::npos;

  // waiters: a line-by-line scan of the stacks for the wait primitives
  std::printf("DUMP-STACKS: lock/wait summary (per thread, the first wait primitive on its stack)\n");
  {
    size_t pos = 0;
    std::string currentThread;
    bool reportedForThread = false;
    while (pos < stacks.size()) {
      const size_t eol = stacks.find('\n', pos);
      const std::string line = stacks.substr(pos, eol == std::string::npos ? std::string::npos : eol - pos);
      pos = eol == std::string::npos ? stacks.size() : eol + 1;
      if (line.find(" Id: ") != std::string::npos && line.find("Suspend:") != std::string::npos) {  // ".  N  Id: pid.tid Suspend: ..." header
        currentThread = line;
        reportedForThread = false;
        continue;
      }
      if (reportedForThread) continue;
      static const char* const kWaits[] = {"NtWaitForAlertByThreadId", "RtlAcquireSRWLockExclusive", "RtlAcquireSRWLockShared",
                                           "RtlpWaitOnCriticalSection", "RtlEnterCriticalSection", "NtWaitForSingleObject",
                                           "NtWaitForMultipleObjects", "WaitForSingleObjectEx", "SleepConditionVariableSRW",
                                           "NtDelayExecution", "NtRemoveIoCompletion", "NtWaitForWorkViaWorkerFactory",
                                           "GetQueuedCompletionStatus"};
      for (const char* w : kWaits) {
        if (line.find(w) != std::string::npos) {
          std::printf("  %s\n    WAITING in %s\n", currentThread.c_str(), w);
          reportedForThread = true;
          break;
        }
      }
    }
  }

  client->SetOutputCallbacks(nullptr);
  client->EndSession(DEBUG_END_PASSIVE);
  symbols->Release();
  control->Release();
  client->Release();
  const bool ok = pdbMatched && hostFrames;
  std::printf("DUMP-STACKS: pdb-matched=%d host-frames=%d -> %s\n", pdbMatched ? 1 : 0, hostFrames ? 1 : 0, ok ? "OK" : "INCOMPLETE");
  return ok ? 0 : 1;
}
