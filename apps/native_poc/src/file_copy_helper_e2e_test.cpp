// The clipboard helper, end to end, without touching the user's clipboard or files.
// (file-copy-helper r1, plan §5 "Medium 시험 기동")
//
// This process is the HOST: it makes the pipe with HelperLink exactly as the product will (random
// name, DACL for the user, Medium label, nonce, Job) and starts the real GNLinkClipHelper.exe --
// but as THIS user with CreateProcessW (token = nullptr), on a private, unnamed window station
// whose clipboard is its own. What that does not prove is stated in file_copy_helper_host.hpp:
// the elevated launch (TokenLinkedToken + CreateProcessWithTokenW) is the one prepared UAC run.
//
// Re-launched with --consumer on that station, this process is also the paste consumer: the
// destination folder's IDropTarget driven as Explorer's Paste drives it (DragEnter, Drop), the
// shell's own copy engine running in the consumer -- the same path the probes measured. Its
// results come back in a file.
//
// Covered: token refusal on a non-elevated caller; the pure handshake rule; a live handshake
// refused for a client that is not the launched process; R->P (StatFiles / Pin / ReadLocal /
// Unpin: identity, exclusions, a writer refused while pinned, Replaced, SharingViolation, the
// lease releasing the file); P->R (publish -> paste -> descriptor confirmed -> 256 KiB reads ->
// files identical -> PasteEnd); the host refusing a Read (partial file removed by the shell,
// paste ended by idle); a consumer that reads the same file twice in one operation; a
// synchronous consumer refused; the consumer dying mid-copy (helper not wedged); Clear; Shutdown;
// the pipe dropped -> the helper clears the clipboard and exits 0.
//
// Build: remote60_file_copy_helper_e2e_test (CMake). Tags: session-privilege(window station),
// filesystem(scratch), COM. Needs GNLinkClipHelper.exe beside it.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ole2.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shldisp.h>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "file_copy_helper_host.hpp"
#include "file_copy_pipe.hpp"
#include "test_scratch_dir.hpp"
#include "e2e_station_lock.hpp"

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "advapi32.lib")

using namespace remote60::native_poc::file_copy;
namespace ts = remote60::native_poc::test_support;

namespace {

int gChecks = 0;
int gFailures = 0;

void check(const std::string& name, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", name.c_str(), detail.empty() ? "" : "  ", detail.c_str());
}

uint8_t content_byte(uint64_t i, uint32_t k) { return static_cast<uint8_t>((i * 131 + k * 7) & 0xFF); }

std::string narrow(const std::wstring& s) {
  std::string out;
  for (wchar_t c : s) out.push_back(c < 0x80 ? static_cast<char>(c) : '?');
  return out;
}
std::u16string u16(const std::wstring& s) { return std::u16string(s.begin(), s.end()); }

std::wstring self_path() {
  std::wstring p(32768, L'\0');
  const DWORD n = GetModuleFileNameW(nullptr, p.data(), static_cast<DWORD>(p.size()));
  p.resize(n);
  return p;
}
std::wstring dir_of(const std::wstring& p) {
  const size_t s = p.find_last_of(L"\\/");
  return s == std::wstring::npos ? L"" : p.substr(0, s + 1);
}

std::wstring arg_after(int argc, wchar_t** argv, const wchar_t* key, const wchar_t* def = L"") {
  for (int i = 1; i + 1 < argc; ++i) {
    if (wcscmp(argv[i], key) == 0) return argv[i + 1];
  }
  return def;
}
bool has_arg(int argc, wchar_t** argv, const wchar_t* key) {
  for (int i = 1; i < argc; ++i) {
    if (wcscmp(argv[i], key) == 0) return true;
  }
  return false;
}

bool write_file(const std::wstring& path, uint64_t size, uint32_t k) {
  HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  std::vector<uint8_t> buf(1 << 16);
  uint64_t off = 0;
  while (off < size) {
    const DWORD n = static_cast<DWORD>(std::min<uint64_t>(buf.size(), size - off));
    for (DWORD i = 0; i < n; ++i) buf[i] = content_byte(off + i, k);
    DWORD w = 0;
    if (!WriteFile(h, buf.data(), n, &w, nullptr) || w != n) {
      CloseHandle(h);
      return false;
    }
    off += n;
  }
  CloseHandle(h);
  return true;
}

std::vector<std::string> read_lines(const std::wstring& path) {
  std::vector<std::string> lines;
  std::ifstream in(path, std::ios::binary);
  std::string line;
  while (std::getline(in, line)) {
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
    lines.push_back(line);
  }
  return lines;
}

// ================================================================== the consumer (child process)
//
// Explorer's paste, on the private station: OleGetClipboard, then the destination folder's
// IDropTarget: DragEnter + Drop (what SHSimulateDrop does). Results are written to --result.
//   --mode drop   paste into --dest; wait until --expect-bytes are on disk and stable, or 20 s
//   --mode dup    inside one async operation, GetData(FileContents, 0) twice and read both fully
//   --mode sync   ask for the descriptor WITHOUT StartOperation: must be refused
//   --mode empty  report whether the clipboard offers FileGroupDescriptorW at all
//   --mode seek   inside one async operation, read file 0 by pieces with Seeks (forward, backward,
//                 from the end) and then fully again through a second stream; every piece is written
//                 to --dest as seek_<offset>.bin and the full read as full.bin (the caller compares)

FILE* gResult = nullptr;
void result(const char* fmt, ...) {
  if (!gResult) return;
  va_list ap;
  va_start(ap, fmt);
  std::vfprintf(gResult, fmt, ap);
  va_end(ap);
  std::fprintf(gResult, "\n");
  std::fflush(gResult);
}

UINT cf_desc() {
  static UINT f = RegisterClipboardFormatW(L"FileGroupDescriptorW");
  return f;
}
UINT cf_contents() {
  static UINT f = RegisterClipboardFormatW(L"FileContents");
  return f;
}

void pump_for(DWORD ms) {
  const DWORD until = GetTickCount() + ms;
  MSG msg;
  while (GetTickCount() < until) {
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    MsgWaitForMultipleObjects(0, nullptr, FALSE, 50, QS_ALLINPUT);
  }
}

uint64_t dir_bytes(const std::wstring& dest, int* files) {
  uint64_t total = 0;
  *files = 0;
  WIN32_FIND_DATAW fd;
  HANDLE f = FindFirstFileW((dest + L"\\*").c_str(), &fd);
  if (f == INVALID_HANDLE_VALUE) return 0;
  do {
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
    total += (static_cast<uint64_t>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
    ++*files;
  } while (FindNextFileW(f, &fd));
  FindClose(f);
  return total;
}

// Reports every file in `dest`: size, attributes, first byte that differs from the synthetic
// content of stream `k` (taken from the name "remote_<k>.bin" / "remote_b.txt" = 1).
void report_dest(const std::wstring& dest) {
  WIN32_FIND_DATAW fd;
  HANDLE f = FindFirstFileW((dest + L"\\*").c_str(), &fd);
  if (f == INVALID_HANDLE_VALUE) {
    result("dest empty");
    return;
  }
  int count = 0;
  do {
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
    ++count;
    const uint64_t sz = (static_cast<uint64_t>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
    uint32_t k = 0;
    if (wcscmp(fd.cFileName, L"remote_b.txt") == 0) k = 1;
    const std::wstring full = dest + L"\\" + fd.cFileName;
    HANDLE h = CreateFileW(full.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    long long firstBad = -1;
    uint64_t off = 0;
    std::vector<uint8_t> buf(1 << 20);
    DWORD got = 0;
    while (h != INVALID_HANDLE_VALUE && firstBad < 0 && ReadFile(h, buf.data(), static_cast<DWORD>(buf.size()), &got, nullptr) && got) {
      for (DWORD i = 0; i < got; ++i) {
        if (buf[i] != content_byte(off + i, k)) {
          firstBad = static_cast<long long>(off + i);
          break;
        }
      }
      off += got;
    }
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    result("file %ls size=%llu attrs=0x%lx mismatch=%lld", fd.cFileName, static_cast<unsigned long long>(sz),
           fd.dwFileAttributes, firstBad);
  } while (FindNextFileW(f, &fd));
  FindClose(f);
  result("dest files=%d", count);
}

int run_consumer(int argc, wchar_t** argv) {
  const std::wstring dest = arg_after(argc, argv, L"--dest");
  const std::wstring mode = arg_after(argc, argv, L"--mode", L"drop");
  const uint64_t expectBytes = _wcstoui64(arg_after(argc, argv, L"--expect-bytes", L"0").c_str(), nullptr, 10);
  const DWORD waitSec = static_cast<DWORD>(_wtoi(arg_after(argc, argv, L"--wait-sec", L"20").c_str()));
  gResult = _wfopen(arg_after(argc, argv, L"--result").c_str(), L"w");
  HRESULT hr = OleInitialize(nullptr);
  result("ole hr=0x%08lx", static_cast<unsigned long>(hr));
  IDataObject* dobj = nullptr;
  hr = OleGetClipboard(&dobj);
  result("getclipboard hr=0x%08lx", static_cast<unsigned long>(hr));
  bool hasDesc = false, hasContents = false;
  if (SUCCEEDED(hr) && dobj) {
    IEnumFORMATETC* en = nullptr;
    if (SUCCEEDED(dobj->EnumFormatEtc(DATADIR_GET, &en))) {
      FORMATETC fe;
      while (en->Next(1, &fe, nullptr) == S_OK) {
        if (fe.cfFormat == cf_desc()) hasDesc = true;
        if (fe.cfFormat == cf_contents()) hasContents = true;
      }
      en->Release();
    }
  }
  result("formats desc=%d contents=%d", hasDesc ? 1 : 0, hasContents ? 1 : 0);
  if (mode == L"empty" || FAILED(hr) || !dobj) {
    if (dobj) dobj->Release();
    OleUninitialize();
    return 0;
  }

  if (mode == L"sync") {
    // No StartOperation: the helper must refuse the descriptor rather than serve a synchronous
    // paste it cannot finish without hanging the consumer.
    FORMATETC fe{static_cast<CLIPFORMAT>(cf_desc()), nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM sm{};
    const HRESULT g = dobj->GetData(&fe, &sm);
    result("sync descriptor hr=0x%08lx refused=%d", static_cast<unsigned long>(g), FAILED(g) ? 1 : 0);
    if (SUCCEEDED(g)) ReleaseStgMedium(&sm);
    FORMATETC fc{static_cast<CLIPFORMAT>(cf_contents()), nullptr, DVASPECT_CONTENT, 0, TYMED_ISTREAM};
    const HRESULT c = dobj->GetData(&fc, &sm);
    result("sync contents hr=0x%08lx refused=%d", static_cast<unsigned long>(c), FAILED(c) ? 1 : 0);
    if (SUCCEEDED(c)) ReleaseStgMedium(&sm);
    dobj->Release();
    OleUninitialize();
    return 0;
  }

  if (mode == L"dup") {
    // A consumer that follows the async protocol by hand and asks for the same file twice in one
    // operation: both streams must read the same bytes.
    IDataObjectAsyncCapability* async = nullptr;
    hr = dobj->QueryInterface(IID_PPV_ARGS(&async));
    result("dup async qi hr=0x%08lx", static_cast<unsigned long>(hr));
    if (!async) {
      dobj->Release();
      OleUninitialize();
      return 0;
    }
    BOOL isAsync = FALSE;
    async->GetAsyncMode(&isAsync);
    hr = async->StartOperation(nullptr);
    result("dup start hr=0x%08lx async=%d", static_cast<unsigned long>(hr), isAsync);
    FORMATETC fe{static_cast<CLIPFORMAT>(cf_desc()), nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM sm{};
    hr = dobj->GetData(&fe, &sm);
    uint64_t size0 = 0;
    UINT items = 0;
    if (SUCCEEDED(hr)) {
      auto* g = static_cast<FILEGROUPDESCRIPTORW*>(GlobalLock(sm.hGlobal));
      items = g->cItems;
      size0 = (static_cast<uint64_t>(g->fgd[0].nFileSizeHigh) << 32) | g->fgd[0].nFileSizeLow;
      GlobalUnlock(sm.hGlobal);
      ReleaseStgMedium(&sm);
    }
    result("dup descriptor hr=0x%08lx items=%u size0=%llu", static_cast<unsigned long>(hr), items,
           static_cast<unsigned long long>(size0));
    auto read_stream = [&](IStream* s, std::vector<uint8_t>* out) {
      out->clear();
      LARGE_INTEGER zero{};
      s->Seek(zero, STREAM_SEEK_SET, nullptr);
      std::vector<uint8_t> buf(256 * 1024);
      for (;;) {
        ULONG got = 0;
        const HRESULT r = s->Read(buf.data(), static_cast<ULONG>(buf.size()), &got);
        if (FAILED(r)) return r;
        out->insert(out->end(), buf.begin(), buf.begin() + got);
        if (got < buf.size() || r == S_FALSE) return S_OK;
      }
    };
    FORMATETC fc{static_cast<CLIPFORMAT>(cf_contents()), nullptr, DVASPECT_CONTENT, 0, TYMED_ISTREAM};
    STGMEDIUM s1{}, s2{};
    const HRESULT h1 = dobj->GetData(&fc, &s1);
    const HRESULT h2 = dobj->GetData(&fc, &s2);
    std::vector<uint8_t> a, b;
    HRESULT r1 = E_FAIL, r2 = E_FAIL;
    if (SUCCEEDED(h1)) r1 = read_stream(s1.pstm, &a);
    if (SUCCEEDED(h2)) r2 = read_stream(s2.pstm, &b);
    bool synthetic = a.size() == size0;
    for (size_t i = 0; synthetic && i < a.size(); ++i) synthetic = a[i] == content_byte(i, 0);
    result("dup streams hr1=0x%08lx hr2=0x%08lx r1=0x%08lx r2=0x%08lx len1=%zu len2=%zu same=%d synthetic=%d",
           static_cast<unsigned long>(h1), static_cast<unsigned long>(h2), static_cast<unsigned long>(r1),
           static_cast<unsigned long>(r2), a.size(), b.size(), a == b ? 1 : 0, synthetic ? 1 : 0);
    if (SUCCEEDED(h1)) ReleaseStgMedium(&s1);
    if (SUCCEEDED(h2)) ReleaseStgMedium(&s2);
    hr = async->EndOperation(S_OK, nullptr, DROPEFFECT_COPY);
    result("dup end hr=0x%08lx", static_cast<unsigned long>(hr));
    async->Release();
    dobj->Release();
    OleUninitialize();
    return 0;
  }

  if (mode == L"seek") {
    IDataObjectAsyncCapability* async = nullptr;
    hr = dobj->QueryInterface(IID_PPV_ARGS(&async));
    if (!async) {
      result("seek async qi hr=0x%08lx", static_cast<unsigned long>(hr));
      dobj->Release();
      OleUninitialize();
      return 4;
    }
    hr = async->StartOperation(nullptr);
    result("seek start hr=0x%08lx", static_cast<unsigned long>(hr));
    FORMATETC fe{static_cast<CLIPFORMAT>(cf_desc()), nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM sm{};
    uint64_t size0 = 0;
    if (SUCCEEDED(dobj->GetData(&fe, &sm))) {
      auto* g = static_cast<FILEGROUPDESCRIPTORW*>(GlobalLock(sm.hGlobal));
      size0 = (static_cast<uint64_t>(g->fgd[0].nFileSizeHigh) << 32) | g->fgd[0].nFileSizeLow;
      GlobalUnlock(sm.hGlobal);
      ReleaseStgMedium(&sm);
    }
    const auto save = [&](const std::wstring& name, const std::vector<uint8_t>& b) {
      FILE* f = _wfopen((dest + L"\\" + name).c_str(), L"wb");
      if (!f) return;
      if (!b.empty()) std::fwrite(b.data(), 1, b.size(), f);
      std::fclose(f);
    };
    FORMATETC fc{static_cast<CLIPFORMAT>(cf_contents()), nullptr, DVASPECT_CONTENT, 0, TYMED_ISTREAM};
    STGMEDIUM s1{}, s2{};
    HRESULT h = dobj->GetData(&fc, &s1);
    int pieces = 0, failures = 0;
    const auto piece = [&](IStream* s, int64_t move, DWORD origin, ULONG len) {
      LARGE_INTEGER li{};
      li.QuadPart = move;
      ULARGE_INTEGER pos{};
      if (FAILED(s->Seek(li, origin, &pos))) {
        ++failures;
        return;
      }
      std::vector<uint8_t> b(len);
      ULONG got = 0;
      if (FAILED(s->Read(b.data(), len, &got))) {
        ++failures;
        return;
      }
      b.resize(got);
      save(L"seek_" + std::to_wstring(pos.QuadPart) + L".bin", b);
      ++pieces;
    };
    if (SUCCEEDED(h) && size0 > 400000) {
      piece(s1.pstm, 0, STREAM_SEEK_SET, 100000);                                     // from the start
      piece(s1.pstm, static_cast<int64_t>(size0 / 2), STREAM_SEEK_SET, 100000);       // jump forward
      piece(s1.pstm, -150000, STREAM_SEEK_CUR, 70000);                                // back
      piece(s1.pstm, -5000, STREAM_SEEK_END, 10000);                                  // near the end (short read)
      ReleaseStgMedium(&s1);
    }
    std::vector<uint8_t> full;
    const HRESULT h2 = dobj->GetData(&fc, &s2);
    if (SUCCEEDED(h2)) {
      std::vector<uint8_t> buf(256 * 1024);
      for (;;) {
        ULONG got = 0;
        const HRESULT r = s2.pstm->Read(buf.data(), static_cast<ULONG>(buf.size()), &got);
        if (FAILED(r)) {
          ++failures;
          break;
        }
        full.insert(full.end(), buf.begin(), buf.begin() + got);
        if (got < buf.size() || r == S_FALSE) break;
      }
      ReleaseStgMedium(&s2);
      save(L"full.bin", full);
    }
    hr = async->EndOperation(failures == 0 ? S_OK : E_FAIL, nullptr, DROPEFFECT_COPY);
    result("seek pieces=%d failures=%d full=%zu size0=%llu end hr=0x%08lx", pieces, failures, full.size(),
           static_cast<unsigned long long>(size0), static_cast<unsigned long>(hr));
    async->Release();
    dobj->Release();
    OleUninitialize();
    return failures == 0 && pieces == 4 ? 0 : 5;
  }

  // mode == drop: Explorer's paste into `dest`.
  PIDLIST_ABSOLUTE pidl = nullptr;
  hr = SHParseDisplayName(dest.c_str(), nullptr, &pidl, 0, nullptr);
  IShellFolder* parent = nullptr;
  PCUITEMID_CHILD child = nullptr;
  IDropTarget* dt = nullptr;
  if (SUCCEEDED(hr)) hr = SHBindToParent(pidl, IID_PPV_ARGS(&parent), &child);
  if (SUCCEEDED(hr)) hr = parent->GetUIObjectOf(nullptr, 1, &child, IID_IDropTarget, nullptr, reinterpret_cast<void**>(&dt));
  result("droptarget hr=0x%08lx", static_cast<unsigned long>(hr));
  if (FAILED(hr)) {
    dobj->Release();
    OleUninitialize();
    return 3;
  }
  DWORD effect = DROPEFFECT_COPY;
  POINTL pt{0, 0};
  const auto t0 = std::chrono::steady_clock::now();
  hr = dt->DragEnter(dobj, MK_LBUTTON, pt, &effect);
  effect = DROPEFFECT_COPY;
  hr = dt->Drop(dobj, MK_LBUTTON, pt, &effect);
  const double dropSec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  result("drop hr=0x%08lx effect=0x%lx seconds=%.3f", static_cast<unsigned long>(hr), effect, dropSec);
  // The copy continues on the shell's thread after Drop returns (async). Wait until the bytes
  // are on disk and stable, or the budget is out.
  // The copy engine sizes a file before it fills it, so "enough bytes on disk" alone is not "done":
  // an async data object also has to have seen its EndOperation.
  IDataObjectAsyncCapability* dropAsync = nullptr;
  (void)dobj->QueryInterface(IID_PPV_ARGS(&dropAsync));
  const DWORD until = GetTickCount() + waitSec * 1000;
  uint64_t last = 0;
  int stable = 0;
  while (GetTickCount() < until) {
    pump_for(250);
    int files = 0;
    const uint64_t now = dir_bytes(dest, &files);
    BOOL inOp = FALSE;
    if (dropAsync && FAILED(dropAsync->InOperation(&inOp))) inOp = FALSE;
    if (now == last && now >= expectBytes && expectBytes > 0 && !inOp) {
      if (++stable >= 6) break;  // 1.5 s unchanged with everything there
    } else {
      stable = 0;
    }
    last = now;
  }
  pump_for(500);
  report_dest(dest);
  if (dropAsync) dropAsync->Release();
  dt->Release();
  parent->Release();
  CoTaskMemFree(pidl);
  dobj->Release();
  OleUninitialize();
  return 0;
}

// ================================================================== the driver (this is the host)

struct Station {
  HWINSTA ws = nullptr;
  HDESK dk = nullptr;
  std::wstring desktop;  // "<station>\Default"
  bool Create() {
    ws = CreateWindowStationW(nullptr, 0, WINSTA_ALL_ACCESS, nullptr);
    if (!ws) return false;
    wchar_t name[256] = L"";
    DWORD len = 0;
    GetUserObjectInformationW(ws, UOI_NAME, name, sizeof(name), &len);
    HWINSTA orig = GetProcessWindowStation();
    SetProcessWindowStation(ws);
    dk = CreateDesktopW(L"Default", nullptr, nullptr, 0, GENERIC_ALL, nullptr);
    SetProcessWindowStation(orig);
    desktop = std::wstring(name) + L"\\Default";
    return dk != nullptr;
  }
};

struct Child {
  PROCESS_INFORMATION pi{};
  bool Start(const std::wstring& cmdline, const wchar_t* desktop) {
    std::vector<wchar_t> cmd(cmdline.begin(), cmdline.end());
    cmd.push_back(0);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    std::wstring d = desktop ? desktop : L"";
    if (!d.empty()) si.lpDesktop = d.data();
    return CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi) != FALSE;
  }
  DWORD Wait(DWORD ms) {
    const DWORD w = WaitForSingleObject(pi.hProcess, ms);
    DWORD code = 0xFFFFFFFF;
    if (w == WAIT_OBJECT_0) GetExitCodeProcess(pi.hProcess, &code);
    return code;
  }
  void Kill() {
    if (pi.hProcess) TerminateProcess(pi.hProcess, 124);
  }
  ~Child() {
    // A consumer still running when its case is over would keep the destination open and the
    // scratch directory undeletable; it has nothing left to prove.
    if (pi.hProcess && WaitForSingleObject(pi.hProcess, 0) == WAIT_TIMEOUT) {
      TerminateProcess(pi.hProcess, 125);
      WaitForSingleObject(pi.hProcess, 5000);
    }
    if (pi.hThread) CloseHandle(pi.hThread);
    if (pi.hProcess) CloseHandle(pi.hProcess);
  }
};

// What the driver serves for a paste: the offer's items (confirmed as-is), synthetic content,
// optional refusal at an offset, optional delay per read, optional hook after N reads.
struct PasteScript {
  std::vector<RemoteFileItem> items;
  uint64_t refuseIndex = UINT64_MAX;  // refuse reads of this file from refuseOffset on
  uint64_t refuseOffset = 0;
  DWORD delayMs = 0;
  std::function<void(uint32_t reads)> afterRead;
};

struct PasteLog {
  bool begun = false;
  bool ended = false;
  EndReason reason = EndReason::Ended;
  uint64_t pasteOp = 0;
  uint32_t reads = 0;
  uint32_t refused = 0;
  std::vector<PipeFrame> unexpected;
};

// Serves one paste on the link until PasteEnd arrives or `budgetMs` passes.
PasteLog serve_paste(HelperLink& link, uint64_t offerId, const PasteScript& script, DWORD budgetMs) {
  PasteLog log;
  const ULONGLONG deadline = GetTickCount64() + budgetMs;
  while (GetTickCount64() < deadline) {
    PipeFrame f;
    if (!link.Receive(&f, 500)) {
      const DWORD error = GetLastError();
      if (error != WAIT_TIMEOUT) {
        std::printf("      serve_paste: the link failed err=%lu\n", error);
        break;
      }
      if (!link.pipe_open() || !link.helper_alive()) break;
      continue;
    }
    if (f.type == PipeMsg::PasteBegin) {
      PasteBegin m;
      decode(f, &m);
      if (m.offerId != offerId) {
        log.unexpected.push_back(f);
        continue;
      }
      log.begun = true;
      log.pasteOp = m.pasteOp;
      PasteDescriptor d;
      d.offerId = m.offerId;
      d.pasteOp = m.pasteOp;
      d.status = Status::Ok;
      d.items = script.items;
      link.Send(encode(d));
    } else if (f.type == PipeMsg::ReadRequest) {
      ReadRequest m;
      decode(f, &m);
      ReadData d;
      d.offerId = m.offerId;
      d.pasteOp = m.pasteOp;
      d.fileIndex = m.fileIndex;
      d.offset = m.offset;
      if (m.offerId != offerId || m.fileIndex >= script.items.size()) {
        d.status = Status::UnknownId;
      } else if (m.fileIndex == script.refuseIndex && m.offset >= script.refuseOffset) {
        d.status = Status::ReadError;
        ++log.refused;
      } else {
        const uint64_t size = script.items[m.fileIndex].size;
        const uint64_t avail = m.offset >= size ? 0 : size - m.offset;
        const uint32_t n = static_cast<uint32_t>(std::min<uint64_t>(m.length, avail));
        d.status = Status::Ok;
        d.data.resize(n);
        for (uint32_t i = 0; i < n; ++i) d.data[i] = content_byte(m.offset + i, m.fileIndex);
      }
      if (script.delayMs) Sleep(script.delayMs);
      ++log.reads;
      link.Send(encode(d));
      if (script.afterRead) script.afterRead(log.reads);
    } else if (f.type == PipeMsg::PasteEnd) {
      PasteEnd m;
      decode(f, &m);
      if (m.offerId != offerId) {
        log.unexpected.push_back(f);
        continue;
      }
      log.ended = true;
      log.reason = m.reason;
      break;
    } else {
      log.unexpected.push_back(f);
    }
  }
  return log;
}

bool expect_publish_ok(HelperLink& link, uint64_t offerId, const std::vector<RemoteFileItem>& items, std::string* detail) {
  PublishRemoteFiles m;
  m.offerId = offerId;
  m.items = items;
  if (!link.Send(encode(m))) {
    *detail = "send failed";
    return false;
  }
  PipeFrame f;
  PublishResult r;
  if (!link.Receive(&f, 5000) || !decode(f, &r)) {
    *detail = "no PublishResult";
    return false;
  }
  *detail = std::string("status=") + status_name(r.status) + " count=" + std::to_string(r.count);
  return r.offerId == offerId && r.status == Status::Ok && r.count == items.size();
}

std::wstring tail_of(const std::wstring& path, size_t n) {
  const auto lines = read_lines(path);
  std::string out;
  for (size_t i = lines.size() > n ? lines.size() - n : 0; i < lines.size(); ++i) out += "      " + lines[i] + "\n";
  return std::wstring(out.begin(), out.end());
}

RemoteFileItem item(const char16_t* name, uint64_t size, uint32_t attrs = 0x20) {
  RemoteFileItem it;
  it.name = name;
  it.size = size;
  it.mtime = 133000000000000000ull;
  it.attributes = attrs;
  return it;
}

std::map<std::string, std::string> parse_result(const std::wstring& path, std::vector<std::string>* files) {
  std::map<std::string, std::string> kv;
  for (const auto& line : read_lines(path)) {
    if (line.rfind("file ", 0) == 0) {
      files->push_back(line);
      continue;
    }
    const size_t sp = line.find(' ');
    kv[line.substr(0, sp)] = sp == std::string::npos ? "" : line.substr(sp + 1);
  }
  return kv;
}

bool file_line(const std::vector<std::string>& files, const std::string& name, uint64_t size, std::string* found) {
  for (const auto& l : files) {
    if (l.find("file " + name + " ") == 0) {
      *found = l;
      return l.find("size=" + std::to_string(size) + " ") != std::string::npos && l.find("mismatch=-1") != std::string::npos;
    }
  }
  return false;
}

int run_driver() {
  std::printf("file_copy_helper_e2e_test -- the real helper, this user, a private window station\n");
  const std::wstring me = self_path();
  const std::wstring helperExe = dir_of(me) + L"GNLinkClipHelper.exe";
  check("GNLinkClipHelper.exe is beside this test", GetFileAttributesW(helperExe.c_str()) != INVALID_FILE_ATTRIBUTES, narrow(helperExe));
  const std::wstring root = ts::make_scratch_dir(L"file_copy_helper");
  check("a scratch directory inside the repository", !root.empty(), ts::scratch_root_problem());
  if (root.empty()) return 1;
  const std::wstring helperLog = root + L"\\helper.log";

  // ---------------------------------------------------------------- token: the product rule
  std::printf("\n--- the token rule ---\n");
  {
    HANDLE self = nullptr;
    OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_DUPLICATE, &self);
    DWORD session = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &session);
    std::wstring interactive;
    DWORD err = 0;
    check("the interactive user of this session is known (WTS)", interactive_session_user_sid(session, &interactive, &err),
          "err=" + std::to_string(err));
    std::wstring mine;
    check("...and it is this process's user", current_process_user_sid(&mine) && _wcsicmp(mine.c_str(), interactive.c_str()) == 0,
          narrow(mine) + " vs " + narrow(interactive));
    TOKEN_ELEVATION_TYPE type{};
    DWORD len = 0;
    GetTokenInformation(self, TokenElevationType, &type, sizeof(type), &len);
    HANDLE linked = nullptr;
    const TokenVerdict v = inspect_helper_token(self, session, interactive, &linked);
    if (type == TokenElevationTypeFull) {
      check("(this run is ELEVATED) the linked token passes every check", v.ok, v.why);
      if (linked) {
        check("...and it is Medium", token_integrity_rid(linked) == SECURITY_MANDATORY_MEDIUM_RID);
        CloseHandle(linked);
      }
    } else {
      check("A NON-ELEVATED CALLER IS REFUSED (its linked token would be the FULL admin one)",
            !v.ok && std::string(v.why) == (type == TokenElevationTypeLimited ? "caller-not-elevated" : "caller-no-split-token") && !linked,
            v.why);
      check("...a wrong interactive user is refused on an elevated caller only (here: refused earlier)", !v.ok);
    }
    CloseHandle(self);
    std::wstring dummy;
    check("an impossible session has no interactive user", !interactive_session_user_sid(0xFFFFFFF0u, &dummy, &err));
  }

  // ---------------------------------------------------------------- the handshake rule (pure)
  std::printf("\n--- the handshake rule ---\n");
  {
    std::array<uint8_t, kNonceBytes> nonce{};
    for (size_t i = 0; i < nonce.size(); ++i) nonce[i] = static_cast<uint8_t>(i * 3 + 1);
    Hello h;
    h.nonce = nonce;
    h.pid = 500;
    check("right pid + right nonce accepted", verify_hello(h, nonce, 500, 500).ok);
    check("a client that is not the launched pid is refused", !verify_hello(h, nonce, 501, 500).ok);
    check("a launched pid of 0 (nothing launched) is refused", !verify_hello(h, nonce, 500, 0).ok);
    Hello lie = h;
    lie.pid = 777;
    check("a Hello claiming another pid than the pipe client is refused", !verify_hello(lie, nonce, 500, 500).ok);
    Hello wrong = h;
    wrong.nonce[7] ^= 1;
    check("ONE BIT OFF IN THE NONCE IS REFUSED", std::string(verify_hello(wrong, nonce, 500, 500).why) == "nonce-mismatch");
    Hello ver = h;
    ver.version = 2;
    check("a wrong version is refused", !verify_hello(ver, nonce, 500, 500).ok);
  }

  // ---------------------------------------------------------------- the Medium launch rule (pure)
  std::printf("\n--- who may start the helper as itself ---\n");
  check("a limited (UAC-split) Medium process may", medium_launch_allowed(TokenElevationTypeLimited, SECURITY_MANDATORY_MEDIUM_RID));
  check("a plain (no UAC split) Medium process may", medium_launch_allowed(TokenElevationTypeDefault, SECURITY_MANDATORY_MEDIUM_RID));
  check("AN ELEVATED (FULL) PROCESS MAY NOT", !medium_launch_allowed(TokenElevationTypeFull, SECURITY_MANDATORY_HIGH_RID));
  check("...nor a full one that somehow reads Medium", !medium_launch_allowed(TokenElevationTypeFull, SECURITY_MANDATORY_MEDIUM_RID));
  check("...nor a High-integrity process without a split (UAC off, administrator)",
        !medium_launch_allowed(TokenElevationTypeDefault, SECURITY_MANDATORY_HIGH_RID));
  check("...nor SYSTEM", !medium_launch_allowed(TokenElevationTypeDefault, SECURITY_MANDATORY_SYSTEM_RID));
  check("a Low process may (it is not elevated; the pipe DACL decides the rest)",
        medium_launch_allowed(TokenElevationTypeDefault, SECURITY_MANDATORY_LOW_RID));

  Station station;
  check("a private window station with a Default desktop (its own clipboard)", station.Create(), narrow(station.desktop));
  std::wstring userSid;
  current_process_user_sid(&userSid);

  // ---------------------------------------------------------------- live handshake refusals
  std::printf("\n--- live handshake refusals ---\n");
  {
    // The launched process is the helper told a pipe that does not exist (its last --pipe wins),
    // so it never connects; THIS process connects instead with the right nonce. The PID check
    // must refuse it.
    HelperLink bad;
    std::string why;
    check("a second server pipe", bad.CreateServerPipe(userSid, &why), why);
    check("...launch a helper that cannot connect", bad.Launch(helperExe, nullptr, station.desktop.c_str(),
                                                               L"--pipe \\\\.\\pipe\\GNLinkClip-does-not-exist", &why), why);
    HANDLE client = CreateFileW(bad.pipe_name().c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    check("...this process can open the pipe (same user)", client != INVALID_HANDLE_VALUE, "err=" + std::to_string(GetLastError()));
    if (client != INVALID_HANDLE_VALUE) {
      Hello h;
      h.nonce = bad.nonce();
      h.pid = GetCurrentProcessId();
      std::vector<uint8_t> wire;
      encode_frame(encode(h), &wire);
      DWORD w = 0;
      WriteFile(client, wire.data(), static_cast<DWORD>(wire.size()), &w, nullptr);
    }
    const bool accepted = bad.AwaitHello(5000, &why);
    check("A CLIENT THAT IS NOT THE LAUNCHED PROCESS IS REFUSED, RIGHT NONCE OR NOT", !accepted && why == "client-pid-is-not-the-launched-helper",
          why);
    check("...and the pipe is closed on refusal", !bad.pipe_open());
    if (client != INVALID_HANDLE_VALUE) CloseHandle(client);
    bad.Close();
  }
  {
    // The launched helper exits before anyone connects and nobody else does: refused as such,
    // promptly, with the connection wait's storage settled (r3 ①).
    HelperLink bad;
    std::string why;
    bad.CreateServerPipe(userSid, &why);
    check("...launch a helper that exits at once (bad pipe, no client)",
          bad.Launch(helperExe, nullptr, station.desktop.c_str(), L"--pipe \\\\.\\pipe\\GNLinkClip-does-not-exist", &why), why);
    const ULONGLONG t0 = GetTickCount64();
    const bool accepted = bad.AwaitHello(15000, &why);
    const ULONGLONG took = GetTickCount64() - t0;
    check("A HELPER THAT EXITED BEFORE CONNECTING IS REFUSED AS SUCH, WITHOUT WAITING OUT THE TIMEOUT",
          !accepted && why == "helper-exited-before-connecting" && took < 10000, why + " in " + std::to_string(took) + " ms");
    bad.Close();
  }
  {
    // The real helper, told a wrong nonce (its last --nonce wins): refused, and it exits 4.
    HelperLink bad;
    std::string why;
    bad.CreateServerPipe(userSid, &why);
    std::array<uint8_t, kNonceBytes> wrong = bad.nonce();
    wrong[0] ^= 0x80;
    check("...launch the helper with a wrong nonce", bad.Launch(helperExe, nullptr, station.desktop.c_str(),
                                                                L"--nonce " + hex_encode(wrong.data(), wrong.size()), &why), why);
    const bool accepted = bad.AwaitHello(5000, &why);
    check("A WRONG NONCE FROM THE REAL HELPER IS REFUSED", !accepted && why == "nonce-mismatch", why);
    DWORD code = 0xFFFFFFFF;
    if (bad.helper_process() && WaitForSingleObject(bad.helper_process(), 5000) == WAIT_OBJECT_0) GetExitCodeProcess(bad.helper_process(), &code);
    check("...and the helper exits (no HelloAck) with 4", code == 4, "exit=" + std::to_string(code));
    bad.Close();
  }

  // ---------------------------------------------------------------- the helper, for real
  std::printf("\n--- the helper starts, proves itself, and is under the Job ---\n");
  HelperLink link;
  std::string why;
  check("server pipe for this user", link.CreateServerPipe(userSid, &why), why);
  check("...launched on the private station (Medium test launch)",
        link.Launch(helperExe, nullptr, station.desktop.c_str(), L"--idle-ms 3000 --log \"" + helperLog + L"\"", &why), why);
  const bool hello = link.AwaitHello(10000, &why);
  check("THE HELPER CONNECTS AND ITS HELLO CARRIES THE NONCE FROM ITS COMMAND LINE", hello, why);
  if (!hello) {
    std::wprintf(L"%s", tail_of(helperLog, 30).c_str());
    return 1;
  }
  check("...pid matches the launched process", link.helper_pid() != 0 && link.helper_alive());
  {
    // The Job: the helper is inside it (a process in a KILL_ON_JOB_CLOSE job cannot be outside).
    BOOL inJob = FALSE;
    check("...the helper is in a Job", IsProcessInJob(link.helper_process(), nullptr, &inJob) && inJob);
  }

  // ---------------------------------------------------------------- R->P: the user's files
  std::printf("\n--- R->P: StatFiles / Pin / ReadLocal / Unpin over the pipe ---\n");
  const std::wstring f1 = root + L"\\f1.bin";
  const std::wstring f2 = root + L"\\f2.txt";
  const std::wstring f3 = root + L"\\f3.bin";
  const std::wstring dir = root + L"\\adir";
  const std::wstring lnk = root + L"\\s.lnk";
  const uint64_t f1Size = 700 * 1024 + 3;
  check("fixtures", write_file(f1, f1Size, 0) && write_file(f2, 10, 1) && write_file(f3, 0, 2) && CreateDirectoryW(dir.c_str(), nullptr) &&
                        write_file(lnk, 3, 3));
  Stats stats;
  {
    StatFiles m;
    m.requestId = 11;
    m.paths = {u16(f1), u16(f2), u16(f3), u16(dir), u16(lnk), u16(root + L"\\none.bin"), u"\\\\.\\PhysicalDrive0"};
    link.Send(encode(m));
    PipeFrame f;
    const bool got = link.Receive(&f, 5000) && decode(f, &stats) && stats.requestId == 11 && stats.entries.size() == 7;
    check("Stats: 7 entries back", got);
    if (got) {
      check("f1/f2/f3 Ok with ids, names, sizes", stats.entries[0].status == Status::Ok && stats.entries[0].size == f1Size &&
                                                      stats.entries[0].name == u"f1.bin" && stats.entries[1].status == Status::Ok &&
                                                      stats.entries[2].status == Status::Ok && stats.entries[2].size == 0 &&
                                                      stats.entries[0].id != stats.entries[1].id);
      check("directory NotAFile / .lnk Excluded / missing NotFound / device BadPath",
            stats.entries[3].status == Status::NotAFile && stats.entries[4].status == Status::Excluded &&
                stats.entries[5].status == Status::NotFound && stats.entries[6].status == Status::BadPath);
    }
  }
  auto pin_entry = [&](const std::wstring& path, const StatEntry& st) {
    PinRequestEntry e;
    e.path = u16(path);
    e.expectedId = st.id;
    e.expectedSize = st.size;
    e.expectedMtime = st.mtime;
    return e;
  };
  auto do_pin = [&](uint64_t pinId, uint32_t leaseMs, std::vector<PinRequestEntry> entries, PinResult* out) {
    Pin m;
    m.pinId = pinId;
    m.leaseMs = leaseMs;
    m.entries = std::move(entries);
    link.Send(encode(m));
    PipeFrame f;
    return link.Receive(&f, 5000) && decode(f, out) && out->pinId == pinId;
  };
  auto do_read = [&](uint64_t pinId, uint32_t index, uint64_t offset, uint32_t length, LocalData* out) {
    ReadLocal m;
    m.pinId = pinId;
    m.fileIndex = index;
    m.offset = offset;
    m.length = length;
    link.Send(encode(m));
    PipeFrame f;
    return link.Receive(&f, 5000) && decode(f, out) && out->pinId == pinId;
  };
  auto do_unpin = [&](uint64_t pinId, Unpinned* out) {
    Unpin m;
    m.pinId = pinId;
    link.Send(encode(m));
    PipeFrame f;
    return link.Receive(&f, 5000) && decode(f, out);
  };
  if (stats.entries.size() == 7) {
    PinResult r;
    check("Pin f1 + f2 with the ids Stat gave", do_pin(1, 60000, {pin_entry(f1, stats.entries[0]), pin_entry(f2, stats.entries[1])}, &r) &&
                                                    r.entries.size() == 2 && r.entries[0].status == Status::Ok && r.entries[1].status == Status::Ok &&
                                                    r.entries[0].size == f1Size);
    uint64_t off = 0;
    bool ok = true;
    std::string detail;
    while (ok && off < f1Size) {
      LocalData d;
      ok = do_read(1, 0, off, kMaxChunkBytes, &d) && d.status == Status::Ok && !d.data.empty();
      for (size_t i = 0; ok && i < d.data.size(); ++i) ok = d.data[i] == content_byte(off + i, 0);
      if (!ok) detail = "at offset " + std::to_string(off) + (d.status != Status::Ok ? std::string(" ") + status_name(d.status) : "");
      off += d.data.size();
    }
    check("f1 READS BACK EXACTLY THROUGH THE PIPE IN 256 KiB CHUNKS", ok && off == f1Size, detail);
    LocalData d;
    check("a read at EOF is an empty Ok", do_read(1, 0, f1Size, 4096, &d) && d.status == Status::Ok && d.data.empty());
    check("a read past EOF is BadRequest", do_read(1, 0, f1Size + 1, 16, &d) && d.status == Status::BadRequest);
    HANDLE w = CreateFileW(f1.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    check("A WRITER IS REFUSED WHILE THE HELPER HOLDS THE PIN", w == INVALID_HANDLE_VALUE && GetLastError() == ERROR_SHARING_VIOLATION,
          "err=" + std::to_string(GetLastError()));
    if (w != INVALID_HANDLE_VALUE) CloseHandle(w);
    Unpinned u;
    check("Unpin releases 2", do_unpin(1, &u) && u.released == 2);
    w = CreateFileW(f1.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    check("...and the writer may open now", w != INVALID_HANDLE_VALUE);
    if (w != INVALID_HANDLE_VALUE) CloseHandle(w);

    // Replaced: f2 deleted and recreated.
    check("delete + recreate f2", DeleteFileW(f2.c_str()) && write_file(f2, 10, 1));
    check("Pin with the old id is Replaced", do_pin(2, 60000, {pin_entry(f2, stats.entries[1])}, &r) && r.entries[0].status == Status::Replaced);
    // A writer already there.
    HANDLE hold = CreateFileW(f1.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    check("Pin while a writer holds f1 is SharingViolation", hold != INVALID_HANDLE_VALUE &&
                                                                 do_pin(3, 60000, {pin_entry(f1, stats.entries[0])}, &r) &&
                                                                 r.entries[0].status == Status::SharingViolation);
    if (hold != INVALID_HANDLE_VALUE) CloseHandle(hold);
    // The lease: 1500 ms, then nothing for 3 s.
    check("Pin f1 with a 1500 ms lease", do_pin(4, 1500, {pin_entry(f1, stats.entries[0])}, &r) && r.entries[0].status == Status::Ok);
    Sleep(3200);
    const bool readAfter = do_read(4, 0, 0, 16, &d);
    check("A READ AFTER THE LEASE RAN OUT IS REFUSED (LeaseExpired or already swept)",
          readAfter && (d.status == Status::LeaseExpired || d.status == Status::UnknownId), status_name(d.status));
    w = CreateFileW(f1.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    check("...AND THE FILE IS FREE AGAIN", w != INVALID_HANDLE_VALUE, "err=" + std::to_string(GetLastError()));
    if (w != INVALID_HANDLE_VALUE) CloseHandle(w);
  }

  // ---------------------------------------------------------------- P->R: Explorer's paste
  std::printf("\n--- P->R: publish, paste through the shell, read from the host ---\n");
  const std::wstring resultFile = root + L"\\consumer.txt";
  auto run_consumer_child = [&](const std::wstring& mode, const std::wstring& dest, uint64_t expectBytes, DWORD waitSec, Child* child) {
    DeleteFileW(resultFile.c_str());
    std::wstring cmd = L"\"" + me + L"\" --consumer --mode " + mode + L" --dest \"" + dest + L"\" --result \"" + resultFile +
                       L"\" --expect-bytes " + std::to_wstring(expectBytes) + L" --wait-sec " + std::to_wstring(waitSec);
    return child->Start(cmd, station.desktop.c_str());
  };
  const std::vector<RemoteFileItem> twoFiles = {item(u"remote_0.bin", 1024 * 1024 + 7), item(u"remote_b.txt", 5)};
  {
    std::string detail;
    check("PublishRemoteFiles offer 7 (2 files)", expect_publish_ok(link, 7, twoFiles, &detail), detail);
    const std::wstring dest = root + L"\\dest7";
    CreateDirectoryW(dest.c_str(), nullptr);
    Child consumer;
    check("the paste consumer starts on the station", run_consumer_child(L"drop", dest, 1024 * 1024 + 12, 30, &consumer));
    PasteScript script;
    script.items = twoFiles;
    const PasteLog log = serve_paste(link, 7, script, 60000);
    const DWORD code = consumer.Wait(40000);
    std::vector<std::string> files;
    const auto kv = parse_result(resultFile, &files);
    std::string l0, l1;
    check("THE PASTE BEGAN (PasteBegin) AND WAS ANSWERED WITH THE CONFIRMED DESCRIPTOR", log.begun);
    check("THE SHELL READ EVERY BYTE FROM THE HOST THROUGH THE PIPE", log.reads >= 5, std::to_string(log.reads) + " ReadRequests");
    check("BOTH FILES LANDED IN THE DESTINATION, IDENTICAL TO WHAT THE HOST SERVED", file_line(files, "remote_0.bin", 1024 * 1024 + 7, &l0) &&
                                                                                       file_line(files, "remote_b.txt", 5, &l1),
          l0 + " | " + l1);
    check("...the drop returned quickly (async; the copy ran after it)", kv.count("drop") && kv.at("drop").find("hr=0x00000000") != std::string::npos,
          kv.count("drop") ? kv.at("drop") : "no drop line");
    check("PasteEnd(ended) arrived", log.ended && log.reason == EndReason::Ended, end_reason_name(log.reason));
    check("consumer exit 0", code == 0, "exit=" + std::to_string(code));
    check("nothing unexpected on the pipe", log.unexpected.empty(), std::to_string(log.unexpected.size()));
  }
  {
    std::printf("\n--- the host refuses a read half way: the shell discards the partial file ---\n");
    std::string detail;
    check("PublishRemoteFiles offer 8", expect_publish_ok(link, 8, twoFiles, &detail), detail);
    const std::wstring dest = root + L"\\dest8";
    CreateDirectoryW(dest.c_str(), nullptr);
    Child consumer;
    check("consumer starts", run_consumer_child(L"drop", dest, 0, 12, &consumer));
    PasteScript script;
    script.items = twoFiles;
    script.refuseIndex = 0;
    script.refuseOffset = 512 * 1024;
    const PasteLog log = serve_paste(link, 8, script, 40000);
    const DWORD code = consumer.Wait(30000);
    std::vector<std::string> files;
    parse_result(resultFile, &files);
    bool partialPresent = false;
    for (const auto& l : files) partialPresent = partialPresent || l.find("file remote_0.bin ") == 0;
    check("the refused read was answered ReadError", log.refused >= 1, std::to_string(log.refused));
    check("THE PASTE ENDED BY IDLE OR ERROR, NOT AS A SUCCESS", log.ended && (log.reason == EndReason::Idle || log.reason == EndReason::Error),
          log.ended ? end_reason_name(log.reason) : "no PasteEnd");
    check("no partial remote_0.bin is left in the destination (observed, as in probe T3a)", !partialPresent,
          partialPresent ? "a file is there" : "");
    check("consumer exited", code != 0xFFFFFFFF, "exit=" + std::to_string(code));
    (void)code;
  }
  {
    std::printf("\n--- the same file twice in one operation; a synchronous consumer refused ---\n");
    std::string detail;
    check("PublishRemoteFiles offer 9", expect_publish_ok(link, 9, twoFiles, &detail), detail);
    Child dup;
    check("dup consumer starts", run_consumer_child(L"dup", root, 0, 10, &dup));
    PasteScript script;
    script.items = twoFiles;
    const PasteLog log = serve_paste(link, 9, script, 40000);
    dup.Wait(20000);
    std::vector<std::string> files;
    const auto kv = parse_result(resultFile, &files);
    const std::string streams = kv.count("dup") ? "" : "";
    std::string dupLine;
    for (const auto& l : read_lines(resultFile)) {
      if (l.rfind("dup streams", 0) == 0) dupLine = l;
    }
    check("TWO STREAMS OF THE SAME FILE IN ONE OPERATION READ THE SAME, CORRECT BYTES",
          dupLine.find("same=1") != std::string::npos && dupLine.find("synthetic=1") != std::string::npos && dupLine.find("r1=0x00000000") != std::string::npos,
          dupLine);
    check("...and the operation ended (EndOperation -> PasteEnd ended)", log.ended && log.reason == EndReason::Ended, end_reason_name(log.reason));

    Child sync;
    check("sync consumer starts", run_consumer_child(L"sync", root, 0, 10, &sync));
    sync.Wait(20000);
    std::string descLine, contLine;
    for (const auto& l : read_lines(resultFile)) {
      if (l.rfind("sync descriptor", 0) == 0) descLine = l;
      if (l.rfind("sync contents", 0) == 0) contLine = l;
    }
    check("A CONSUMER WITHOUT StartOperation IS REFUSED THE DESCRIPTOR AND THE CONTENTS",
          descLine.find("refused=1") != std::string::npos && contLine.find("refused=1") != std::string::npos, descLine + " | " + contLine);
    PipeFrame stray;
    check("...and nothing was asked of the host for it", !link.Receive(&stray, 1500));
  }
  {
    std::printf("\n--- the consumer dies mid-copy: the helper is not wedged ---\n");
    std::string detail;
    check("PublishRemoteFiles offer 10", expect_publish_ok(link, 10, twoFiles, &detail), detail);
    const std::wstring dest = root + L"\\dest10";
    CreateDirectoryW(dest.c_str(), nullptr);
    Child consumer;
    check("consumer starts", run_consumer_child(L"drop", dest, 0, 30, &consumer));
    PasteScript script;
    script.items = twoFiles;
    script.delayMs = 300;
    script.afterRead = [&](uint32_t reads) {
      if (reads == 2) consumer.Kill();
    };
    const PasteLog log = serve_paste(link, 10, script, 40000);
    consumer.Wait(5000);
    check("THE PASTE ENDS (idle) AFTER THE CONSUMER IS GONE, WITHOUT ANYONE ENDING IT", log.ended && log.reason == EndReason::Idle,
          log.ended ? end_reason_name(log.reason) : "no PasteEnd");
    int files = 0;
    const uint64_t left = dir_bytes(dest, &files);
    std::printf("      (observed, not asserted: %d file(s), %llu bytes left in the destination by the dead consumer)\n", files,
                static_cast<unsigned long long>(left));
    check("the helper is still alive", link.helper_alive());
    // ...and serves the next paste normally.
    check("PublishRemoteFiles offer 11", expect_publish_ok(link, 11, twoFiles, &detail), detail);
    const std::wstring dest11 = root + L"\\dest11";
    CreateDirectoryW(dest11.c_str(), nullptr);
    Child again;
    check("consumer starts", run_consumer_child(L"drop", dest11, 1024 * 1024 + 12, 30, &again));
    PasteScript plain;
    plain.items = twoFiles;
    const PasteLog log2 = serve_paste(link, 11, plain, 60000);
    again.Wait(40000);
    std::vector<std::string> lines;
    parse_result(resultFile, &lines);
    std::string l0;
    check("A NEW PASTE AFTER THE DEAD ONE COMPLETES", log2.ended && log2.reason == EndReason::Ended && file_line(lines, "remote_0.bin", 1024 * 1024 + 7, &l0), l0);
  }
  {
    std::printf("\n--- Clear takes the offer off the clipboard ---\n");
    std::string detail;
    check("PublishRemoteFiles offer 12", expect_publish_ok(link, 12, twoFiles, &detail), detail);
    ClearRemoteFiles c;
    c.offerId = 12;
    link.Send(encode(c));
    Sleep(500);
    Child empty;
    check("consumer (empty check) starts", run_consumer_child(L"empty", root, 0, 5, &empty));
    empty.Wait(10000);
    std::vector<std::string> files;
    const auto kv = parse_result(resultFile, &files);
    check("AFTER Clear THE CLIPBOARD OFFERS NO FileGroupDescriptorW", kv.count("formats") && kv.at("formats") == "desc=0 contents=0",
          kv.count("formats") ? kv.at("formats") : "?");
  }
  {
    std::printf("\n--- a bad offer is refused before it reaches the clipboard ---\n");
    PublishRemoteFiles m;
    m.offerId = 13;
    m.items = {item(u"..\\evil.exe", 10)};
    link.Send(encode(m));
    PipeFrame f;
    PublishResult r;
    check("a name with a separator is Refused", link.Receive(&f, 5000) && decode(f, &r) && r.offerId == 13 && r.status == Status::Refused,
          status_name(r.status));
    m.items.assign(kMaxFiles + 1, item(u"x.bin", 1));
    // 101 items: the codec refuses it on the helper's side (malformed) -- no reply comes at all;
    // the helper stays alive. Sent as a raw frame to prove that.
    link.Send(encode(m));
    check("101 items: no PublishResult (refused by the codec), helper alive", !link.Receive(&f, 1500) && link.helper_alive());
  }
  {
    std::printf("\n--- Shutdown: the helper clears the clipboard and exits 0 ---\n");
    std::string detail;
    check("PublishRemoteFiles offer 14", expect_publish_ok(link, 14, twoFiles, &detail), detail);
    link.Send(encode_shutdown());
    DWORD code = 0xFFFFFFFF;
    if (WaitForSingleObject(link.helper_process(), 8000) == WAIT_OBJECT_0) GetExitCodeProcess(link.helper_process(), &code);
    check("THE HELPER EXITS 0 ON Shutdown", code == 0, "exit=" + std::to_string(code));
    Child empty;
    check("consumer (empty check) starts", run_consumer_child(L"empty", root, 0, 5, &empty));
    empty.Wait(10000);
    std::vector<std::string> files;
    const auto kv = parse_result(resultFile, &files);
    check("...AND THE CLIPBOARD OFFERS NO FILES AFTERWARDS", kv.count("formats") && kv.at("formats") == "desc=0 contents=0",
          kv.count("formats") ? kv.at("formats") : "?");
    link.Close();
  }
  {
    std::printf("\n--- the pipe dropped: the helper clears the clipboard and exits 0 on its own ---\n");
    HelperLink second;
    std::string detail;
    check("second helper: pipe", second.CreateServerPipe(userSid, &detail), detail);
    check("...launched", second.Launch(helperExe, nullptr, station.desktop.c_str(), L"--log \"" + helperLog + L"\"", &detail), detail);
    check("...hello", second.AwaitHello(10000, &detail), detail);
    check("PublishRemoteFiles offer 15", expect_publish_ok(second, 15, twoFiles, &detail), detail);
    second.ClosePipe();
    DWORD code = 0xFFFFFFFF;
    if (WaitForSingleObject(second.helper_process(), 8000) == WAIT_OBJECT_0) GetExitCodeProcess(second.helper_process(), &code);
    check("THE HELPER NOTICES THE BROKEN PIPE AND EXITS 0", code == 0, "exit=" + std::to_string(code));
    Child empty;
    check("consumer (empty check) starts", run_consumer_child(L"empty", root, 0, 5, &empty));
    empty.Wait(10000);
    std::vector<std::string> files;
    const auto kv = parse_result(resultFile, &files);
    check("...and no files are offered afterwards", kv.count("formats") && kv.at("formats") == "desc=0 contents=0",
          kv.count("formats") ? kv.at("formats") : "?");
    second.Close();
  }

  if (gFailures) {
    std::printf("\n--- helper log (tail) ---\n");
    std::wprintf(L"%s", tail_of(helperLog, 60).c_str());
  }
  if (station.dk) CloseDesktop(station.dk);
  if (station.ws) CloseWindowStation(station.ws);
  check("the host side orphaned no pending I/O in this whole run", orphaned_io_count() == 0, std::to_string(orphaned_io_count()));
  check("the scratch run directory is removed", ts::remove_scratch_run_dir());
  return 0;
}

}  // namespace

// ================================================================== the elevated check (UAC, once)
//
// The product path -- launch_file_copy_helper: this process's TokenLinkedToken checked against the
// interactive user, CreateProcessWithTokenW, the pipe's DACL + Medium label, the Job, the
// handshake -- and then one StatFiles round trip on a file of the test's own and Shutdown. Nothing
// is put on the clipboard. Meant to be run ONCE, elevated, from
// automation/file_copy_helper_elevated_check.ps1; it writes what it saw to --log and exits 0 only
// if every step held.
int run_elevated_check(int argc, wchar_t** argv) {
  const std::wstring logPath = arg_after(argc, argv, L"--log", L"file_copy_helper_elevated_check.log");
  FILE* log = _wfopen(logPath.c_str(), L"w");
  auto say = [&](const std::string& line) {
    std::printf("%s\n", line.c_str());
    if (log) {
      std::fprintf(log, "%s\n", line.c_str());
      std::fflush(log);
    }
  };
  int failures = 0;
  auto verdict = [&](const std::string& what, bool ok, const std::string& detail = {}) {
    if (!ok) ++failures;
    say(std::string(ok ? "PASS  " : "FAIL  ") + what + (detail.empty() ? "" : "  " + detail));
  };
  HANDLE self = nullptr;
  OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &self);
  TOKEN_ELEVATION_TYPE type{};
  DWORD len = 0;
  GetTokenInformation(self, TokenElevationType, &type, sizeof(type), &len);
  const DWORD rid = token_integrity_rid(self);
  CloseHandle(self);
  say("this process: elevationType=" + std::to_string(type) + " integrityRid=" + std::to_string(rid) + " pid=" +
      std::to_string(GetCurrentProcessId()));
  verdict("this run is elevated (TokenElevationTypeFull, High)", type == TokenElevationTypeFull && rid >= SECURITY_MANDATORY_HIGH_RID);
  const std::wstring helperExe = dir_of(self_path()) + L"GNLinkClipHelper.exe";
  {
    // The guard: from an elevated process the Medium test launch (token == nullptr) is refused.
    HelperLink guard;
    std::string why;
    std::wstring sid;
    current_process_user_sid(&sid);
    guard.CreateServerPipe(sid, &why);
    const bool started = guard.Launch(helperExe, nullptr, nullptr, L"", &why);
    verdict("Launch(token=nullptr) from an elevated process is refused", !started && why == "medium-launch-refused-caller-elevated", why);
    guard.Close();
  }
  HelperLink link;
  std::string why;
  const bool launched = launch_file_copy_helper(helperExe, &link, &why, 15000);
  verdict("launch_file_copy_helper (linked token -> pipe -> CreateProcessWithTokenW -> Hello)", launched, why);
  if (launched) {
    say("helper pid=" + std::to_string(link.helper_pid()) + " pipe=" + narrow(link.pipe_name()));
    // The helper's own integrity, read from its process token.
    HANDLE hp = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, link.helper_pid());
    HANDLE ht = nullptr;
    if (hp && OpenProcessToken(hp, TOKEN_QUERY, &ht)) {
      const DWORD helperRid = token_integrity_rid(ht);
      std::wstring helperUser, me;
      current_process_user_sid(&me);
      verdict("the helper runs at Medium integrity", helperRid == SECURITY_MANDATORY_MEDIUM_RID, "rid=" + std::to_string(helperRid));
      CloseHandle(ht);
    } else {
      verdict("the helper's token could be read", false, "err=" + std::to_string(GetLastError()));
    }
    if (hp) CloseHandle(hp);
    StatFiles m;
    m.requestId = 1;
    m.paths = {u16(self_path())};
    link.Send(encode(m));
    PipeFrame f;
    Stats stats;
    const bool got = link.Receive(&f, 5000) && decode(f, &stats) && stats.entries.size() == 1;
    verdict("StatFiles on this executable answered Ok through the pipe", got && stats.entries[0].status == Status::Ok,
            got ? status_name(stats.entries[0].status) : "no Stats");
    link.Send(encode_shutdown());
    DWORD code = 0xFFFFFFFF;
    if (WaitForSingleObject(link.helper_process(), 8000) == WAIT_OBJECT_0) GetExitCodeProcess(link.helper_process(), &code);
    verdict("the helper exits 0 on Shutdown", code == 0, "exit=" + std::to_string(code));
    link.Close();
  }
  say(failures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILED (" + std::to_string(failures) + ")");
  if (log) std::fclose(log);
  return failures == 0 ? 0 : 1;
}

int wmain(int argc, wchar_t** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  if (has_arg(argc, argv, L"--consumer")) return run_consumer(argc, argv);
  if (has_arg(argc, argv, L"--elevated-check")) return run_elevated_check(argc, argv);
  remote60::native_poc::e2e::StationLock stationLock;  // TEST ONLY: queue behind any other clipboard e2e (e2e_station_lock.hpp)
  if (!stationLock.Acquire("file_copy_helper_e2e_test")) return remote60::native_poc::e2e::StationLock::Busy("file_copy_helper_e2e_test");
  const int rc = run_driver();
  std::printf("\n%s  (%d checks, %d failed)\n", gFailures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILED", gChecks, gFailures);
  return (rc == 0 && gFailures == 0) ? 0 : 1;
}
