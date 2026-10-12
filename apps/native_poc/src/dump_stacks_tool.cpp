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
#include <urlmon.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "host_e2e_dump_check.hpp"  // ImageFile: the RSDS record of a downloaded image

#pragma comment(lib, "dbgeng.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "urlmon.lib")

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

// --msdl: the Windows dbgeng in System32 has no symsrv.dll, so "srv*" in a path does nothing for it. The
// public symbol server is plain HTTP with a fixed layout, so the tool fetches what the engine needs itself:
//   images  <server>/<name>/<TimeDateStamp:08X><SizeOfImage:X>/<name>      (ntdll.dll, d3d11.dll, ...)
//   PDBs    <server>/<pdb>/<GUID:32 hex><Age:X>/<pdb>                     (from the image's RSDS record)
// into <cache>\<name>\<id>\, then points .exepath/.sympath at those directories. Microsoft modules only
// come back; vendor DLLs (nvwgf2umx) 404 and are skipped -- they stay module+offset.
struct Fetched { std::wstring imageDirs; std::wstring pdbDirs; unsigned images = 0, pdbs = 0, skipped = 0; };
Fetched fetch_from_msdl(IDebugSymbols3* symbols, const std::wstring& cache) {
  Fetched out;
  ULONG loaded = 0, unloaded = 0;
  if (FAILED(symbols->GetNumberModules(&loaded, &unloaded))) return out;
  CreateDirectoryW(cache.c_str(), nullptr);
  for (ULONG i = 0; i < loaded; ++i) {
    DEBUG_MODULE_PARAMETERS mp{};
    if (FAILED(symbols->GetModuleParameters(1, nullptr, i, &mp))) continue;
    wchar_t name[MAX_PATH]{};
    if (FAILED(symbols->GetModuleNameStringWide(DEBUG_MODNAME_IMAGE, i, 0, name, MAX_PATH, nullptr))) continue;
    const std::wstring full = name;
    const size_t cut = full.find_last_of(L"\\/");
    const std::wstring base = cut == std::wstring::npos ? full : full.substr(cut + 1);
    wchar_t id[32];
    std::swprintf(id, 32, L"%08X%X", mp.TimeDateStamp, mp.Size);
    const std::wstring dir = cache + L"\\" + base + L"\\" + id;
    const std::wstring file = dir + L"\\" + base;
    if (GetFileAttributesW(file.c_str()) == INVALID_FILE_ATTRIBUTES) {
      CreateDirectoryW((cache + L"\\" + base).c_str(), nullptr);
      CreateDirectoryW(dir.c_str(), nullptr);
      const std::wstring url = L"https://msdl.microsoft.com/download/symbols/" + base + L"/" + id + L"/" + base;
      const HRESULT hr = URLDownloadToFileW(nullptr, url.c_str(), file.c_str(), 0, nullptr);
      if (FAILED(hr) || GetFileAttributesW(file.c_str()) == INVALID_FILE_ATTRIBUTES) {
        DeleteFileW(file.c_str());
        ++out.skipped;
        continue;
      }
    }
    ++out.images;
    out.imageDirs += dir + L";";
    // the PDB named in the image
    remote60::native_poc::e2e::ImageFile img;
    if (img.open(file, 0, 0)) {
      const IMAGE_DATA_DIRECTORY& dd = img.nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
      img.base = 0;
      img.size = img.nt->OptionalHeader.SizeOfImage;
      std::vector<uint8_t> ddir(dd.Size);
      if (dd.VirtualAddress && dd.Size >= sizeof(IMAGE_DEBUG_DIRECTORY) && img.read(dd.VirtualAddress, ddir.data(), dd.Size)) {
        const auto* e = reinterpret_cast<const IMAGE_DEBUG_DIRECTORY*>(ddir.data());
        for (size_t k = 0; k < dd.Size / sizeof(IMAGE_DEBUG_DIRECTORY); ++k) {
          if (e[k].Type != IMAGE_DEBUG_TYPE_CODEVIEW || e[k].SizeOfData < 24 || e[k].PointerToRawData + e[k].SizeOfData > img.fileSize) continue;
          const uint8_t* cv = img.data + e[k].PointerToRawData;
          if (std::memcmp(cv, "RSDS", 4) != 0) continue;
          GUID g{};
          uint32_t age = 0;
          std::memcpy(&g, cv + 4, sizeof(GUID));
          std::memcpy(&age, cv + 20, 4);
          std::string pdbA(reinterpret_cast<const char*>(cv + 24), strnlen(reinterpret_cast<const char*>(cv + 24), e[k].SizeOfData - 24));
          const size_t pc = pdbA.find_last_of("\\/");
          const std::wstring pdb = widen((pc == std::string::npos ? pdbA : pdbA.substr(pc + 1)).c_str());
          wchar_t pid[64];
          std::swprintf(pid, 64, L"%08lX%04X%04X%02X%02X%02X%02X%02X%02X%02X%02X%X", g.Data1, g.Data2, g.Data3, g.Data4[0], g.Data4[1], g.Data4[2],
                        g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7], age);
          const std::wstring pdir = cache + L"\\" + pdb + L"\\" + pid;
          const std::wstring pfile = pdir + L"\\" + pdb;
          if (GetFileAttributesW(pfile.c_str()) == INVALID_FILE_ATTRIBUTES) {
            CreateDirectoryW((cache + L"\\" + pdb).c_str(), nullptr);
            CreateDirectoryW(pdir.c_str(), nullptr);
            const std::wstring purl = L"https://msdl.microsoft.com/download/symbols/" + pdb + L"/" + pid + L"/" + pdb;
            const HRESULT hr = URLDownloadToFileW(nullptr, purl.c_str(), pfile.c_str(), 0, nullptr);
            if (FAILED(hr) || GetFileAttributesW(pfile.c_str()) == INVALID_FILE_ATTRIBUTES) { DeleteFileW(pfile.c_str()); break; }
          }
          ++out.pdbs;
          out.pdbDirs += pdir + L";";
          break;
        }
      }
    }
    img.close();
  }
  return out;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::printf("usage: remote60_dump_stacks <dump.dmp> <symbol dir with exe+pdb> [--msdl] [--host-module GNLinkStream.exe] [--cmd \"<dbgeng command>\"]...\n");
    return 2;
  }
  const char* dumpPath = argv[1];
  const char* symDir = argv[2];
  bool msdl = false;
  std::string hostModule = "GNLinkStream";
  std::vector<std::string> extraCmds;  // ad-hoc debugger commands, run after the stacks, output printed verbatim
  for (int i = 3; i < argc; ++i) {
    if (std::strcmp(argv[i], "--msdl") == 0) msdl = true;
    if (std::strcmp(argv[i], "--cmd") == 0 && i + 1 < argc) extraCmds.push_back(argv[++i]);
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

  // r11: with --msdl the Microsoft server is used for IMAGES too (.exepath srv*): a dump written on another
  // machine carries only module names/timestamps/sizes, and without the matching ntdll/kernelbase/d3d11
  // image bytes the engine can neither unwind through them nor name their functions -- that is why the
  // Home4 stacks stopped at "ntdll+0xa0fd4". NVIDIA's nvwgf2umx is not on that server: export names only.
  const std::string cache = std::string(getenv("TEMP") ? getenv("TEMP") : ".") + "\\symcache";
  std::string symPath = symDir;
  symbols->SetSymbolPath(symPath.c_str());
  std::string imgPath = symDir;
  symbols->SetImagePath(imgPath.c_str());  // the host image beside its PDB; the server's images are added after the dump is open
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
  if (msdl) {
    // the dump is open: its module list says which images/PDBs to fetch; then the engine is pointed at them
    const Fetched f = fetch_from_msdl(symbols, widen(cache.c_str()));
    std::string imgs(f.imageDirs.begin(), f.imageDirs.end()), pdbs(f.pdbDirs.begin(), f.pdbDirs.end());
    if (!imgs.empty()) imgPath += ";" + imgs;
    if (!pdbs.empty()) symPath += ";" + pdbs;
    symbols->SetImagePath(imgPath.c_str());
    symbols->SetSymbolPath(symPath.c_str());
    symbols->Reload("");
    std::printf("DUMP-STACKS: msdl fetch: images=%u pdbs=%u skipped(not on the server)=%u cache=%s\n", f.images, f.pdbs, f.skipped, cache.c_str());
  }
  std::printf("DUMP-STACKS: %s\nDUMP-STACKS: symbol path %s\nDUMP-STACKS: image path %s\n", dumpPath, symPath.substr(0, 400).c_str(),
              imgPath.substr(0, 400).c_str());

  auto run = [&](const char* cmd) -> std::string {
    sink.text.clear();
    control->Execute(DEBUG_OUTCTL_THIS_CLIENT | DEBUG_OUTCTL_NOT_LOGGED, cmd, DEBUG_EXECUTE_DEFAULT | DEBUG_EXECUTE_NO_REPEAT);
    return sink.text;
  };

  // the host module: does its PDB match? dbgeng names a module by its file name with '.' -> '_'.
  // what kind of dump this is (full memory or not decides whether lock owners can be read)
  {
    const std::string dd = run(".dumpdebug");
    const size_t f = dd.find("Flags");
    std::printf("DUMP-STACKS: dump flags: %s\n", f == std::string::npos ? "?" : dd.substr(f, 240).c_str());
  }
  // every thread's stack (this is what loads the symbols; the module question comes after)
  const std::string stacks = run("~*kvn 48");
  std::printf("%s", stacks.c_str());
  for (const std::string& c : extraCmds) std::printf("DUMP-STACKS: --cmd %s ->\n%s", c.c_str(), run(c.c_str()).c_str());
  // r11: the system modules the stacks pass through -- did the engine get their images/symbols?
  for (const char* m : {"ntdll", "kernelbase", "d3d11", "nvwgf2umx"}) {
    const std::string lmx = run((std::string("lmvm ") + m).c_str());
    const size_t a = lmx.find("start");
    std::printf("%s", (a == std::string::npos ? lmx : lmx.substr(a)).substr(0, 900).c_str());
  }
  // r11: lock owners. !locks (ntsdexts) walks the process's critical sections -- it needs their memory,
  // which a normal dump has only when a stack referenced it (full-memory dumps always). Printed as is.
  // Critical-section owners. The in-box dbgeng has no ntsdexts (!locks / !cs fail to load), so the tool
  // reads the RTL_CRITICAL_SECTION itself. A contended EnterCriticalSection waits in
  // RtlpWaitOnCriticalSection -> RtlpWaitOnAddress(&cs->LockCount), and in the
  // RtlpEnterCriticalSectionContended frame the unwinder recovers rsi == cs (nonvolatile; both Home4
  // dumps: rsi = cs, rcx = &cs->LockCount = cs+8). CS layout: +0x00 DebugInfo, +0x08 LockCount:long
  // (bit0 clear = locked), +0x0c RecursionCount, +0x10 OwningThread (thread id). A fast dump has that
  // memory when a stack slot pointed at it (MiniDumpWithIndirectlyReferencedMemory) -- both Home4 dumps
  // did; a full dump always. The owner is mapped to a thread index of this dump and its top frames shown.
  {
    std::printf("DUMP-STACKS: critical-section owners (RTL_CRITICAL_SECTION = rsi of the waiter's RtlpEnterCriticalSectionContended frame)\n");
    struct ThreadInfo { int idx; unsigned long tid; std::vector<std::string> frames; };
    std::vector<ThreadInfo> threads;
    auto thread_header = [](const std::string& line, int* idx, unsigned long* tid) {
      const size_t idp = line.find(" Id: ");
      if (idp == std::string::npos || line.find("Suspend:") == std::string::npos) return false;
      const size_t dot = line.find('.', idp);
      *tid = dot == std::string::npos ? 0 : std::strtoul(line.c_str() + dot + 1, nullptr, 16);
      *idx = -1;
      for (size_t k = 0; k < idp; ++k) if (std::isdigit(static_cast<unsigned char>(line[k]))) { *idx = std::atoi(line.c_str() + k); break; }
      return true;
    };
    {
      size_t pos = 0;
      while (pos < stacks.size()) {
        const size_t eol = stacks.find('\n', pos);
        const std::string line = stacks.substr(pos, eol == std::string::npos ? std::string::npos : eol - pos);
        pos = eol == std::string::npos ? stacks.size() : eol + 1;
        int idx; unsigned long tid;
        if (thread_header(line, &idx, &tid)) { threads.push_back({idx, tid, {}}); continue; }
        if (!threads.empty() && line.size() > 2 && std::isdigit(static_cast<unsigned char>(line[0])) && std::isdigit(static_cast<unsigned char>(line[1]))) {
          const size_t site = line.rfind(" : ");
          threads.back().frames.push_back(site == std::string::npos ? line : line.substr(site + 3));
        }
      }
    }
    auto parse_reg = [](const std::string& text, const char* name) -> unsigned long long {
      const std::string key = std::string(name) + "=";
      const size_t at = text.find(key);
      return at == std::string::npos ? 0 : std::strtoull(text.c_str() + at + key.size(), nullptr, 16);
    };
    unsigned found = 0, unreadable = 0;
    for (const ThreadInfo& t : threads) {
      int contended = -1;
      for (size_t f = 0; f < t.frames.size(); ++f)
        if (t.frames[f].find("ntdll!RtlpEnterCriticalSectionContended+") != std::string::npos) { contended = static_cast<int>(f); break; }
      if (contended < 0) continue;
      ++found;
      char cmd[96];
      std::snprintf(cmd, sizeof(cmd), "~%ds", t.idx);
      run(cmd);
      std::snprintf(cmd, sizeof(cmd), ".frame /r %d", contended);
      const unsigned long long rsi = parse_reg(run(cmd), "rsi");
      if (rsi < 0x10000 || (rsi & 7) != 0) { std::printf("  thread #%d: contended frame but rsi not a pointer (0x%llx)\n", t.idx, rsi); ++unreadable; continue; }
      const unsigned long long cs = rsi;
      std::snprintf(cmd, sizeof(cmd), "dq 0x%llx L4", cs);
      const std::string dq = run(cmd);
      unsigned long long q[4] = {0, 0, 0, 0};
      size_t n = 0, p2 = 0;
      while (n < 4) {
        const size_t tick = dq.find('`', p2);
        if (tick == std::string::npos || tick < 8) break;
        std::string tok = dq.substr(tick - 8, 17);
        tok.erase(std::remove(tok.begin(), tok.end(), '`'), tok.end());
        const bool isAddress = dq.compare(tick + 9, 2, "  ") == 0;  // the address column is followed by two spaces
        if (!isAddress) q[n++] = std::strtoull(tok.c_str(), nullptr, 16);
        p2 = tick + 1;
      }
      // what this thread itself waits inside (first frame that is not ntdll)
      std::string waiterSite;
      for (const std::string& fr : t.frames) if (fr.rfind("ntdll!", 0) != 0 && fr.rfind("d3d11!", 0) != 0) { waiterSite = fr.substr(0, 110); break; }
      if (dq.find("????") != std::string::npos || n < 3) {
        std::printf("  thread #%d (tid 0x%lx) waits on CS 0x%llx [%s]: CS memory not in this dump -> owner unknown\n", t.idx, t.tid, cs, waiterSite.c_str());
        ++unreadable;
        continue;
      }
      const long lockCount = static_cast<long>(static_cast<int32_t>(q[1] & 0xFFFFFFFFull));
      const unsigned long recursion = static_cast<unsigned long>(q[1] >> 32);
      const unsigned long ownerTid = static_cast<unsigned long>(q[2] & 0xFFFFFFFFull);
      const ThreadInfo* owner = nullptr;
      for (const ThreadInfo& o : threads) if (o.tid == ownerTid) owner = &o;
      std::printf("  thread #%d (tid 0x%lx) waits on CS 0x%llx [%s]\n    DebugInfo=0x%llx LockCount=%ld (%s) RecursionCount=%lu OwningThread=0x%lx -> ", t.idx, t.tid, cs,
                  waiterSite.c_str(), q[0], lockCount, (lockCount & 1) ? "free?!" : "locked", recursion, ownerTid);
      if (owner) {
        std::printf("thread #%d, which is in:\n", owner->idx);
        unsigned shown = 0;
        for (const std::string& fr : owner->frames) { if (shown++ >= 10) break; std::printf("      %s\n", fr.substr(0, 150).c_str()); }
      } else if (ownerTid == 0) {
        std::printf("no owner (free at dump time)\n");
      } else {
        std::printf("tid not among this dump's threads\n");
      }
    }
    if (found == 0) std::printf("  (no thread is inside a contended EnterCriticalSection in this dump)\n");
    else std::printf("  %u waiter(s), %u without a readable CS\n", found, unreadable);
  }

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
  bool pdbMatched = lm.find("pdb symbols") != std::string::npos && lm.find("MISMATCH") == std::string::npos &&
                    lm.find("Loaded symbol image file") != std::string::npos;
  // The engine's own check is not enough for a fast dump: it has no module headers, so the engine maps
  // whatever <symbol dir>\GNLinkStream.exe it is given, takes that file's PDB, and reports "(private pdb
  // symbols)" -- host frames then carry the names of a different build (the Home4 baseline dumps read with
  // the candidate's exe showed std::thread::join / vector<bool> where the stacks were really in the
  // readback pipeline). The dump's module record keeps TimeDateStamp/SizeOfImage; compare them with the
  // PE header of the exe that was actually used. A disagreement makes every host frame untrustworthy.
  {
    ULONG idx = 0;
    ULONG64 base = 0;
    DEBUG_MODULE_PARAMETERS mp{};
    const std::string exeName = hostModule + ".exe";
    if (SUCCEEDED(symbols->GetModuleByModuleName(exeName.c_str(), 0, &idx, &base)) ||
        SUCCEEDED(symbols->GetModuleByModuleName(hostModule.c_str(), 0, &idx, &base)) ||
        SUCCEEDED(symbols->GetModuleByModuleName(dbgName.c_str(), 0, &idx, &base))) {
      (void)symbols->GetModuleParameters(1, &base, 0, &mp);
    }
    const size_t lsi = lm.find("Loaded symbol image file: ");
    std::string usedImage;
    if (lsi != std::string::npos) {
      const size_t e = lm.find('\n', lsi);
      usedImage = lm.substr(lsi + 26, e == std::string::npos ? std::string::npos : e - lsi - 26);
      while (!usedImage.empty() && (usedImage.back() == '\r' || usedImage.back() == ' ')) usedImage.pop_back();
    }
    if (usedImage.find('\\') == std::string::npos && usedImage.find('/') == std::string::npos) usedImage = std::string(symDir) + "\\" + exeName;
    remote60::native_poc::e2e::ImageFile img;
    if (mp.TimeDateStamp != 0 && img.open(widen(usedImage.c_str()), 0, 0)) {
      const uint32_t fileStamp = img.nt->FileHeader.TimeDateStamp;
      const uint32_t fileSize = img.nt->OptionalHeader.SizeOfImage;
      const bool same = fileStamp == mp.TimeDateStamp && fileSize == mp.Size;
      std::printf("DUMP-STACKS: host image identity: dump TimeDateStamp=%08lX SizeOfImage=%lX, used image %s TimeDateStamp=%08lX SizeOfImage=%lX -> %s\n",
                  mp.TimeDateStamp, mp.Size, usedImage.c_str(), static_cast<unsigned long>(fileStamp), static_cast<unsigned long>(fileSize),
                  same ? "SAME BUILD" : "DIFFERENT BUILD: host frame names below are NOT this binary's; give the exe+pdb of the build that was running");
      if (!same) pdbMatched = false;
    } else {
      std::printf("DUMP-STACKS: host image identity: not compared (dump module params %s, image %s)\n", mp.TimeDateStamp ? "ok" : "missing", usedImage.c_str());
    }
    img.close();
  }
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
