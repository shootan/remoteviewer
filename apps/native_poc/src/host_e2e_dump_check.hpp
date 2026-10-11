// Test-support (e2e harness only): read a host minidump written by the harness (hang-pid<N>.dmp) and
// print, per module, the PDB identity it was built with (RSDS GUID/Age) and whether dbghelp found a
// matching PDB in the given symbol directory; then walk every thread's stack and print symbolised
// frames. encoder-rc-budget r9 (Codex r8 contract, item 5): a dump's MDMP magic is not evidence that
// the stuck stack can be read -- this is.
//
//   remote60_fec_single_chunk_host_e2e_test.exe --dump-check <hang-pidN.dmp> --symbols <dir with exe+pdb>
//
// Exit 0 when the module list was read, the host module's PDB matched, and at least one thread walked
// to a frame that resolved to a symbol; 1 otherwise (the reasons are printed).
#pragma once

#include <windows.h>
#include <dbghelp.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#pragma comment(lib, "dbghelp.lib")

namespace remote60::native_poc::e2e {

struct DumpView {
  HANDLE file = INVALID_HANDLE_VALUE;
  HANDLE mapping = nullptr;
  const uint8_t* base = nullptr;
  uint64_t size = 0;
  ~DumpView() {
    if (base) UnmapViewOfFile(base);
    if (mapping) CloseHandle(mapping);
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
  }
  bool open(const std::wstring& path) {
    file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(file, &sz)) return false;
    size = static_cast<uint64_t>(sz.QuadPart);
    mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!mapping) return false;
    base = static_cast<const uint8_t*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0));
    return base != nullptr;
  }
};

// A module's image file mapped read-only, with its section table, so a virtual address inside the
// module can be served from the file: RVA -> file offset through the section headers. The dump has
// the stacks, not the code, and dbghelp's x64 unwinder asks for the module's unwind tables through
// the same ReadProcessMemory callback.
struct ImageFile {
  uint64_t base = 0, size = 0;
  HANDLE file = INVALID_HANDLE_VALUE;
  HANDLE mapping = nullptr;
  const uint8_t* data = nullptr;
  uint64_t fileSize = 0;
  const IMAGE_NT_HEADERS64* nt = nullptr;
  const IMAGE_SECTION_HEADER* sections = nullptr;
  bool open(const std::wstring& path, uint64_t b, uint64_t sz) {
    base = b;
    size = sz;
    file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER li{};
    GetFileSizeEx(file, &li);
    fileSize = static_cast<uint64_t>(li.QuadPart);
    mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!mapping) return false;
    data = static_cast<const uint8_t*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0));
    if (!data || fileSize < sizeof(IMAGE_DOS_HEADER)) return false;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(data);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || static_cast<uint64_t>(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS64) > fileSize) return false;
    nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(data + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) return false;
    sections = IMAGE_FIRST_SECTION(nt);
    return true;
  }
  void close() {
    if (data) UnmapViewOfFile(data);
    if (mapping) CloseHandle(mapping);
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    data = nullptr; mapping = nullptr; file = INVALID_HANDLE_VALUE;
  }
  // one byte range inside this module, from the file; false if it is not backed by file bytes
  bool read(uint64_t addr, void* out, uint32_t len) const {
    if (!nt || addr < base || addr + len > base + size) return false;
    const uint64_t rva = addr - base;
    uint64_t off = 0;
    if (rva + len <= nt->OptionalHeader.SizeOfHeaders) {
      off = rva;
    } else {
      bool found = false;
      for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        const IMAGE_SECTION_HEADER& sh = sections[i];
        const uint64_t va = sh.VirtualAddress;
        const uint64_t vsz = sh.Misc.VirtualSize ? sh.Misc.VirtualSize : sh.SizeOfRawData;
        if (rva >= va && rva + len <= va + vsz) {
          const uint64_t inSection = rva - va;
          if (inSection + len > sh.SizeOfRawData) {  // past the raw data: zero-filled in memory
            std::memset(out, 0, len);
            if (inSection < sh.SizeOfRawData) {
              const uint64_t have = sh.SizeOfRawData - inSection;
              if (sh.PointerToRawData + inSection + have <= fileSize) std::memcpy(out, data + sh.PointerToRawData + inSection, static_cast<size_t>(have));
            }
            return true;
          }
          off = sh.PointerToRawData + inSection;
          found = true;
          break;
        }
      }
      if (!found) return false;
    }
    if (off + len > fileSize) return false;
    std::memcpy(out, data + off, len);
    return true;
  }
};

// The dump's memory ranges (64- and 32-bit lists) plus the module images on disk, for StackWalk64's
// ReadProcessMemory callback. Dump memory wins; an address inside a module that the dump did not save
// is served from the image file (unwind tables, import thunks).
struct DumpMemoryIndex {
  struct Range { uint64_t start = 0, size = 0; const uint8_t* data = nullptr; };
  std::vector<Range> ranges;
  std::vector<ImageFile> images;
  ~DumpMemoryIndex() { for (ImageFile& f : images) f.close(); }
  bool read(uint64_t addr, void* out, uint32_t len) const {
    for (const Range& r : ranges) {
      if (addr >= r.start && addr + len <= r.start + r.size) {
        std::memcpy(out, r.data + (addr - r.start), len);
        return true;
      }
    }
    for (const ImageFile& f : images) if (f.read(addr, out, len)) return true;
    return false;
  }
};
inline DumpMemoryIndex* g_dumpMemory = nullptr;
inline BOOL CALLBACK dump_read_memory(HANDLE, DWORD64 addr, PVOID buf, DWORD size, LPDWORD read) {
  if (!g_dumpMemory || !g_dumpMemory->read(addr, buf, size)) { if (read) *read = 0; return FALSE; }
  if (read) *read = size;
  return TRUE;
}

struct DumpModule {
  std::wstring name;
  uint64_t base = 0, size = 0;
  uint32_t timeDateStamp = 0;
  bool hasRsds = false;
  GUID guid{};
  uint32_t age = 0;
  std::string pdbName;
};

inline std::string guid_str(const GUID& g) {
  char b[64];
  std::snprintf(b, sizeof(b), "%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X", g.Data1, g.Data2, g.Data3, g.Data4[0],
                g.Data4[1], g.Data4[2], g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
  return b;
}
inline std::string narrow(const std::wstring& w) {
  std::string s;
  for (wchar_t c : w) s.push_back(c < 128 ? static_cast<char>(c) : '?');
  return s;
}
inline std::wstring base_name(const std::wstring& path) {
  const size_t p = path.find_last_of(L"\\/");
  return p == std::wstring::npos ? path : path.substr(p + 1);
}

inline int dump_check(const std::wstring& dumpPath, const std::wstring& symbolDir, const std::wstring& hostModuleName, bool verbose = false) {
  DumpView view;
  if (!view.open(dumpPath)) {
    std::printf("DUMP-CHECK: cannot open %s (gle=%lu)\n", narrow(dumpPath).c_str(), GetLastError());
    return 1;
  }
  if (view.size < 4 || std::memcmp(view.base, "MDMP", 4) != 0) {
    std::printf("DUMP-CHECK: not a minidump (no MDMP signature)\n");
    return 1;
  }
  PVOID streamBase = nullptr;
  ULONG streamSize = 0;
  MINIDUMP_DIRECTORY* dir = nullptr;
  auto stream = [&](MINIDUMP_STREAM_TYPE t) -> const void* {
    streamBase = nullptr;
    streamSize = 0;
    if (!MiniDumpReadDumpStream(const_cast<uint8_t*>(view.base), t, &dir, &streamBase, &streamSize)) return nullptr;
    return streamBase;
  };

  // ---- modules and their PDB identity
  std::vector<DumpModule> modules;
  const auto* modList = static_cast<const MINIDUMP_MODULE_LIST*>(stream(ModuleListStream));
  if (!modList) {
    std::printf("DUMP-CHECK: no ModuleListStream\n");
    return 1;
  }
  for (ULONG32 i = 0; i < modList->NumberOfModules; ++i) {
    const MINIDUMP_MODULE& m = modList->Modules[i];
    DumpModule dm;
    dm.base = m.BaseOfImage;
    dm.size = m.SizeOfImage;
    dm.timeDateStamp = m.TimeDateStamp;
    if (m.ModuleNameRva && m.ModuleNameRva + 4 <= view.size) {
      const auto* ms = reinterpret_cast<const MINIDUMP_STRING*>(view.base + m.ModuleNameRva);
      dm.name.assign(ms->Buffer, ms->Length / sizeof(wchar_t));
    }
    if (m.CvRecord.Rva && m.CvRecord.DataSize >= 24 && m.CvRecord.Rva + m.CvRecord.DataSize <= view.size) {
      const uint8_t* cv = view.base + m.CvRecord.Rva;
      if (std::memcmp(cv, "RSDS", 4) == 0) {
        dm.hasRsds = true;
        std::memcpy(&dm.guid, cv + 4, sizeof(GUID));
        std::memcpy(&dm.age, cv + 20, 4);
        dm.pdbName.assign(reinterpret_cast<const char*>(cv + 24), strnlen(reinterpret_cast<const char*>(cv + 24), m.CvRecord.DataSize - 24));
      }
    }
    modules.push_back(dm);
  }
  std::printf("DUMP-CHECK: %s modules=%u\n", narrow(dumpPath).c_str(), static_cast<unsigned>(modules.size()));

  // ---- memory for the stack walk
  DumpMemoryIndex memory;
  if (const auto* m64 = static_cast<const MINIDUMP_MEMORY64_LIST*>(stream(Memory64ListStream))) {
    uint64_t rva = m64->BaseRva;
    for (ULONG64 i = 0; i < m64->NumberOfMemoryRanges; ++i) {
      const MINIDUMP_MEMORY_DESCRIPTOR64& d = m64->MemoryRanges[i];
      if (rva + d.DataSize <= view.size) memory.ranges.push_back({d.StartOfMemoryRange, d.DataSize, view.base + rva});
      rva += d.DataSize;
    }
  }
  if (const auto* m32 = static_cast<const MINIDUMP_MEMORY_LIST*>(stream(MemoryListStream))) {
    for (ULONG32 i = 0; i < m32->NumberOfMemoryRanges; ++i) {
      const MINIDUMP_MEMORY_DESCRIPTOR& d = m32->MemoryRanges[i];
      if (d.Memory.Rva + d.Memory.DataSize <= view.size)
        memory.ranges.push_back({d.StartOfMemoryRange, d.Memory.DataSize, view.base + d.Memory.Rva});
    }
  }
  g_dumpMemory = &memory;

  // ---- symbols: a fake process handle; modules loaded by name+base+size with their CV record so dbghelp
  //      looks the PDB up by GUID/Age in the symbol directory (and the image by name, for unwind data).
  HANDLE sym = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(0x5A5A1));
  // no deferred loads: SymGetModuleInfo must answer with the PDB actually opened (GUID/Age), not "deferred"
  SymSetOptions(SYMOPT_UNDNAME | SYMOPT_LOAD_LINES | SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_EXACT_SYMBOLS);
  // the symbol directory first (exe + pdb of the host), then the system directories so ntdll/kernel32/
  // d3d11 unwind data is found by name (dbghelp searches images along the symbol path too)
  wchar_t sysDir[MAX_PATH]{};
  GetSystemDirectoryW(sysDir, MAX_PATH);
  std::wstring searchPath = symbolDir.empty() ? std::wstring() : symbolDir + L";";
  searchPath += sysDir;
  if (!SymInitializeW(sym, searchPath.c_str(), FALSE)) {
    std::printf("DUMP-CHECK: SymInitialize failed gle=%lu\n", GetLastError());
    return 1;
  }
  bool hostPdbMatched = false;
  uint64_t hostBase = 0;
  unsigned loadFailures = 0;
  for (DumpModule& dm : modules) {
    const std::wstring shortName = base_name(dm.name);
    // The image: the dump's own path if it exists here (this machine's dumps), else the symbol directory's
    // copy (another machine's dumps), else the bare name with the CV record handed over so dbghelp can
    // still find the PDB by GUID/Age (DBHHEADER_CVMISC wants a MODLOAD_CVMISC header before the record).
    std::wstring imagePath = dm.name;
    if (GetFileAttributesW(imagePath.c_str()) == INVALID_FILE_ATTRIBUTES) {
      const std::wstring inSymbols = symbolDir.empty() ? std::wstring() : symbolDir + L"\\" + shortName;
      imagePath = (!inSymbols.empty() && GetFileAttributesW(inSymbols.c_str()) != INVALID_FILE_ATTRIBUTES) ? inSymbols : shortName;
    }
    const bool haveImage = GetFileAttributesW(imagePath.c_str()) != INVALID_FILE_ATTRIBUTES;
    MODLOAD_DATA mld{};
    std::vector<uint8_t> blob;
    if (dm.hasRsds && !haveImage) {
      const size_t cvSize = 24 + dm.pdbName.size() + 1;
      blob.resize(sizeof(MODLOAD_CVMISC) + cvSize);
      auto* hdr = reinterpret_cast<MODLOAD_CVMISC*>(blob.data());
      hdr->oCV = sizeof(MODLOAD_CVMISC);
      hdr->cCV = cvSize;
      hdr->oMisc = 0;
      hdr->cMisc = 0;
      hdr->dtImage = dm.timeDateStamp;
      hdr->cImage = static_cast<DWORD>(dm.size);
      uint8_t* cv = blob.data() + sizeof(MODLOAD_CVMISC);
      std::memcpy(cv, "RSDS", 4);
      std::memcpy(cv + 4, &dm.guid, sizeof(GUID));
      std::memcpy(cv + 20, &dm.age, 4);
      std::memcpy(cv + 24, dm.pdbName.c_str(), dm.pdbName.size() + 1);
      mld.ssize = sizeof(mld);
      mld.ssig = DBHHEADER_CVMISC;
      mld.data = blob.data();
      mld.size = static_cast<DWORD>(blob.size());
    }
    const DWORD64 loaded = SymLoadModuleExW(sym, nullptr, imagePath.c_str(), nullptr, dm.base, static_cast<DWORD>(dm.size),
                                            (dm.hasRsds && !haveImage) ? &mld : nullptr, 0);
    const DWORD loadGle = loaded ? 0 : GetLastError();
    if (haveImage) {
      ImageFile img;
      if (img.open(imagePath, dm.base, dm.size)) memory.images.push_back(img);  // the struct is copied; handles stay owned by the index
      else img.close();
    }
    IMAGEHLP_MODULEW64 mi{};
    mi.SizeOfStruct = sizeof(mi);
    const bool infoOk = SymGetModuleInfoW64(sym, dm.base, &mi) != 0;
    const bool isHost = _wcsicmp(shortName.c_str(), hostModuleName.c_str()) == 0;
    const bool pdbMatch = infoOk && mi.SymType == SymPdb && dm.hasRsds && IsEqualGUID(mi.PdbSig70, dm.guid) && mi.PdbAge == dm.age;
    if (!loaded) ++loadFailures;
    if (isHost || pdbMatch || !dm.hasRsds || !loaded || verbose) {
      std::printf("DUMP-CHECK: module %s base=0x%llx size=0x%llx rsds=%s age=%u pdb=%s image=%s -> loaded=%d gle=%lu symType=%d pdbMatch=%d loadedPdb=%s%s\n",
                  narrow(shortName).c_str(), (unsigned long long)dm.base, (unsigned long long)dm.size,
                  dm.hasRsds ? guid_str(dm.guid).c_str() : "-", dm.age, dm.pdbName.c_str(), narrow(imagePath).c_str(), loaded ? 1 : 0, loadGle,
                  infoOk ? (int)mi.SymType : -1, pdbMatch ? 1 : 0, infoOk ? narrow(mi.LoadedPdbName).c_str() : "-", isHost ? " [HOST]" : "");
    }
    if (isHost) {
      hostPdbMatched = pdbMatch;
      hostBase = dm.base;
    }
  }
  if (hostBase == 0) std::printf("DUMP-CHECK: host module %s not in the dump\n", narrow(hostModuleName).c_str());
  std::printf("DUMP-CHECK: modules that dbghelp could not load: %u of %u\n", loadFailures, static_cast<unsigned>(modules.size()));

  // ---- threads
  const auto* threads = static_cast<const MINIDUMP_THREAD_LIST*>(stream(ThreadListStream));
  if (!threads) {
    std::printf("DUMP-CHECK: no ThreadListStream\n");
    SymCleanup(sym);
    return 1;
  }
  unsigned resolvedFrames = 0, hostFrames = 0, threadsWalked = 0;
  for (ULONG32 t = 0; t < threads->NumberOfThreads; ++t) {
    const MINIDUMP_THREAD& th = threads->Threads[t];
    if (th.ThreadContext.Rva == 0 || th.ThreadContext.DataSize < sizeof(CONTEXT) || th.ThreadContext.Rva + th.ThreadContext.DataSize > view.size) {
      std::printf("DUMP-CHECK: thread %lu: no usable context\n", th.ThreadId);
      continue;
    }
    CONTEXT ctx{};
    std::memcpy(&ctx, view.base + th.ThreadContext.Rva, sizeof(CONTEXT));
    STACKFRAME64 frame{};
    frame.AddrPC.Offset = ctx.Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = ctx.Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = ctx.Rsp;
    frame.AddrStack.Mode = AddrModeFlat;
    std::printf("DUMP-CHECK: thread %lu rip=0x%llx rsp=0x%llx\n", th.ThreadId, (unsigned long long)ctx.Rip, (unsigned long long)ctx.Rsp);
    ++threadsWalked;
    unsigned framesThisThread = 0, hostFramesThisThread = 0;
    for (int depth = 0; depth < 48; ++depth) {
      if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, sym, reinterpret_cast<HANDLE>(static_cast<uintptr_t>(th.ThreadId)), &frame, &ctx,
                       dump_read_memory, SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) {
        if (depth == 0) std::printf("  (StackWalk64 failed at the first frame, gle=%lu)\n", GetLastError());
        break;
      }
      if (frame.AddrPC.Offset == 0) break;
      ++framesThisThread;
      const DWORD64 modBase = SymGetModuleBase64(sym, frame.AddrPC.Offset);
      if (modBase == hostBase) ++hostFramesThisThread;
      std::wstring modName = L"?";
      for (const DumpModule& dm : modules) if (dm.base == modBase) { modName = base_name(dm.name); break; }
      alignas(SYMBOL_INFOW) uint8_t symBuf[sizeof(SYMBOL_INFOW) + 512 * sizeof(wchar_t)]{};
      auto* si = reinterpret_cast<SYMBOL_INFOW*>(symBuf);
      si->SizeOfStruct = sizeof(SYMBOL_INFOW);
      si->MaxNameLen = 512;
      DWORD64 disp = 0;
      if (SymFromAddrW(sym, frame.AddrPC.Offset, &disp, si)) {
        std::printf("  #%02d 0x%016llx %s!%s+0x%llx\n", depth, (unsigned long long)frame.AddrPC.Offset, narrow(modName).c_str(),
                    narrow(si->Name).c_str(), (unsigned long long)disp);
        ++resolvedFrames;
        if (modBase == hostBase) ++hostFrames;
      } else {
        std::printf("  #%02d 0x%016llx %s+0x%llx\n", depth, (unsigned long long)frame.AddrPC.Offset, narrow(modName).c_str(),
                    (unsigned long long)(modBase ? frame.AddrPC.Offset - modBase : frame.AddrPC.Offset));
      }
    }
    std::printf("  (frames=%u hostFrames=%u)\n", framesThisThread, hostFramesThisThread);
  }
  SymCleanup(sym);
  g_dumpMemory = nullptr;
  const bool ok = hostPdbMatched && threadsWalked > 0 && hostFrames > 0;
  std::printf("DUMP-CHECK: threads=%u resolvedFrames=%u hostFrames=%u hostPdbMatch=%d -> %s\n", threadsWalked, resolvedFrames, hostFrames,
              hostPdbMatched ? 1 : 0, ok ? "OK" : "INCOMPLETE");
  return ok ? 0 : 1;
}

}  // namespace remote60::native_poc::e2e
