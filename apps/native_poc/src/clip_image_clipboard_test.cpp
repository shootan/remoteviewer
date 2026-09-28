// clip-image direction A: the REAL clipboard, one side at a time, on a private window station
// (plan r2 ⑧: two-sided runs inject the clipboard at the OS boundary; this is the part they cannot
// cover). Never the interactive clipboard: the parent checks WinSta0's sequence number is unchanged.
//
// On the private station, the child exercises:
//   * the viewer's read: clip_image_read_snapshot picks "PNG" over CF_DIBV5/CF_DIB, copies the
//     same-copy text, records the sequence number, and closes the clipboard;
//   * the host's publish through the product path: HostClipboardHub::PublishImage on its monitor
//     thread -- "PNG" + CF_DIBV5 + text in one open, every pixel and alpha read back, and the text
//     recorded as applied so the change notification does not raise the hub's generation (echo);
//   * the sequence re-check: a publish whose expected sequence is stale leaves the clipboard as it was.
//
//   remote60_clip_image_clipboard_test --out <dir>          (parent)
//   remote60_clip_image_clipboard_test --child <resultfile> (on the private station)
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "clip_image_clipboard.hpp"
#include "clip_image_core.hpp"
#include "clip_image_wic.hpp"
#include "clipboard_monitor.hpp"

using namespace remote60::native_poc;

namespace {
FILE* g_out = stdout;
int g_failed = 0;
int g_checks = 0;
void check(const std::string& what, bool ok) {
  ++g_checks;
  if (!ok) ++g_failed;
  std::fprintf(g_out, "%s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
  std::fflush(g_out);
}

uint32_t px(uint32_t x, uint32_t y) {
  const uint8_t a = static_cast<uint8_t>((x * 29 + y * 7) & 0xFF);
  return (static_cast<uint32_t>(a) << 24) | ((x * 5 & 0xFF) << 16) | ((y * 11 & 0xFF) << 8) | ((x ^ y) & 0xFF);
}

std::vector<uint8_t> make_v5(uint32_t w, uint32_t h) {
  std::vector<uint8_t> b(sizeof(BITMAPV5HEADER) + static_cast<size_t>(w) * h * 4);
  BITMAPV5HEADER hdr{};
  hdr.bV5Size = sizeof(hdr);
  hdr.bV5Width = static_cast<LONG>(w);
  hdr.bV5Height = static_cast<LONG>(h);
  hdr.bV5Planes = 1;
  hdr.bV5BitCount = 32;
  hdr.bV5Compression = BI_BITFIELDS;
  hdr.bV5RedMask = 0x00FF0000;
  hdr.bV5GreenMask = 0x0000FF00;
  hdr.bV5BlueMask = 0x000000FF;
  hdr.bV5AlphaMask = 0xFF000000;
  hdr.bV5CSType = LCS_sRGB;
  std::memcpy(b.data(), &hdr, sizeof(hdr));
  auto* rows = reinterpret_cast<uint32_t*>(b.data() + sizeof(hdr));
  for (uint32_t y = 0; y < h; ++y)
    for (uint32_t x = 0; x < w; ++x) rows[(h - 1 - y) * w + x] = px(x, y);
  return b;
}

bool open_retry() {
  for (int i = 0; i < 50; ++i) {
    if (OpenClipboard(nullptr)) return true;
    Sleep(20);
  }
  return false;
}

bool put_formats(const std::vector<std::pair<UINT, std::vector<uint8_t>>>& formats) {
  if (!open_retry()) return false;
  EmptyClipboard();
  bool ok = true;
  for (const auto& f : formats) {
    HGLOBAL g = clip_global_copy(f.second.data(), f.second.size());
    ok = ok && g && SetClipboardData(f.first, g);
  }
  CloseClipboard();
  return ok;
}

std::vector<uint8_t> utf16_bytes(const std::u16string& s) {
  std::vector<uint8_t> b((s.size() + 1) * 2, 0);
  std::memcpy(b.data(), s.data(), s.size() * 2);
  return b;
}

std::vector<uint8_t> read_format(UINT f) {
  std::vector<uint8_t> out;
  if (!open_retry()) return out;
  if (HANDLE h = GetClipboardData(f)) {
    const auto* p = static_cast<const uint8_t*>(GlobalLock(h));
    if (p) out.assign(p, p + GlobalSize(h));
    GlobalUnlock(h);
  }
  CloseClipboard();
  return out;
}

int run_child(const wchar_t* resultFile) {
  g_out = _wfopen(resultFile, L"w");
  if (!g_out) return 3;
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  const UINT pngFormat = RegisterClipboardFormatW(L"PNG");
  const uint32_t W = 45, H = 31;
  const auto v5 = make_v5(W, H);
  const std::u16string text = u"station caption \xD55C";
  // ---- the viewer's read
  check("an image copy (CF_DIBV5 + text) is put on the private clipboard",
        put_formats({{CF_DIBV5, v5}, {CF_UNICODETEXT, utf16_bytes(text)}}));
  check("an image is available", clip_image_available());
  ClipSnapshot snap;
  const DWORD seqNow = GetClipboardSequenceNumber();
  check("the snapshot reads the DIB, the same-copy text and the sequence number",
        clip_image_read_snapshot(nullptr, &snap) == ClipSnapshotResult::Ok && snap.kind == ClipSnapshotKind::Dib &&
            snap.text == text && snap.sequence == seqNow && snap.bytes.size() >= v5.size());
  // The same copy's text over the text limit (Codex review ④): left out BEFORE it is copied -- the
  // image still goes -- and one unit under the limit still comes along.
  {
    std::u16string big(static_cast<size_t>(kClipboardTextMaxUtf16) + 1, u'x');
    check("an image copy with text one unit over the limit is put on the private clipboard",
          put_formats({{CF_DIBV5, v5}, {CF_UNICODETEXT, utf16_bytes(big)}}));
    ClipSnapshot s2;
    check("...the image is read and the over-limit text is left out",
          clip_image_read_snapshot(nullptr, &s2) == ClipSnapshotResult::Ok && s2.kind == ClipSnapshotKind::Dib &&
              s2.text.empty());
    big.resize(kClipboardTextMaxUtf16);
    check("text exactly at the limit is put on the private clipboard",
          put_formats({{CF_DIBV5, v5}, {CF_UNICODETEXT, utf16_bytes(big)}}));
    ClipSnapshot s3;
    check("...comes along whole", clip_image_read_snapshot(nullptr, &s3) == ClipSnapshotResult::Ok &&
                                       s3.text.size() == kClipboardTextMaxUtf16);
    check("the image copy with the short text is put back for the steps below",
          put_formats({{CF_DIBV5, v5}, {CF_UNICODETEXT, utf16_bytes(text)}}));
  }
  check("the clipboard is closed again after the snapshot (another open succeeds at once)",
        OpenClipboard(nullptr) && CloseClipboard());
  std::vector<uint8_t> png;
  uint32_t w = 0, h = 0;
  check("the snapshot's DIB encodes to PNG", clip_dib_to_png(v5.data(), v5.size(), &png, &w, &h) == ClipWicResult::Ok);
  check("an application's own \"PNG\" format is preferred over its DIB",
        put_formats({{pngFormat, png}, {CF_DIBV5, v5}}) && clip_image_read_snapshot(nullptr, &snap) == ClipSnapshotResult::Ok &&
            snap.kind == ClipSnapshotKind::Png && snap.bytes.size() >= png.size() &&
            std::memcmp(snap.bytes.data(), png.data(), png.size()) == 0 && snap.text.empty());
  check("text alone is not an image", put_formats({{CF_UNICODETEXT, utf16_bytes(u"only text")}}) && !clip_image_available() &&
                                          clip_image_read_snapshot(nullptr, &snap) == ClipSnapshotResult::NoImage);
  // ---- the host's publish, through the product path
  HostClipboardHub hub;
  check("the clipboard hub starts on the private station", hub.Start());
  Sleep(100);
  const uint64_t genBefore = hub.Get().generation;
  HGLOBAL dib = nullptr;
  clip_png_to_dibv5(png.data(), png.size(), W, H, &dib);
  const uint64_t expect = GetClipboardSequenceNumber();
  const ClipPublishResult pub = hub.PublishImage(expect, clip_global_copy(png.data(), png.size()), dib, text);
  check("PublishImage: published", pub == ClipPublishResult::Published);
  Sleep(300);  // let the change notification the write provoked reach the monitor
  const auto pngBack = read_format(pngFormat);
  check("\"PNG\" on the clipboard is the transferred PNG, byte for byte",
        pngBack.size() >= png.size() && std::memcmp(pngBack.data(), png.data(), png.size()) == 0);
  const auto dibBack = read_format(CF_DIBV5);
  const DibInfo info = validate_dib(dibBack.data(), dibBack.size());
  bool pixels = info.ok() && info.width == W && info.height == H && info.bitCount == 32;
  if (pixels) {
    const auto* rows = dibBack.data() + info.pixelOffset;
    for (uint32_t y = 0; y < H && pixels; ++y)
      for (uint32_t x = 0; x < W && pixels; ++x) {
        const uint32_t row = info.topDown ? y : (H - 1 - y);
        uint32_t v;
        std::memcpy(&v, rows + (static_cast<size_t>(row) * W + x) * 4, 4);
        pixels = v == px(x, y);
      }
  }
  check("CF_DIBV5 on the clipboard has every pixel and alpha", pixels);
  const auto textBack = read_format(CF_UNICODETEXT);
  check("the same-copy text was published with it",
        textBack.size() >= (text.size() + 1) * 2 && std::memcmp(textBack.data(), text.data(), text.size() * 2) == 0);
  check("echo guard: the publish did not raise the hub's generation (not served back as the host's copy)",
        hub.Get().generation == genBefore);
  // ---- the sequence re-check
  const auto before = read_format(pngFormat);
  const DWORD seqBefore = GetClipboardSequenceNumber();
  HGLOBAL dib2 = nullptr;
  clip_png_to_dibv5(png.data(), png.size(), W, H, &dib2);
  const ClipPublishResult stale = hub.PublishImage(seqBefore - 1, clip_global_copy(png.data(), png.size()), dib2, u"");
  check("a publish whose sequence is stale is superseded", stale == ClipPublishResult::Superseded);
  check("...and the clipboard is exactly as it was (sequence unchanged)", GetClipboardSequenceNumber() == seqBefore &&
                                                                              read_format(pngFormat) == before);
  // ---- the monitor still hears genuine changes on this station
  put_formats({{CF_UNICODETEXT, utf16_bytes(u"a genuine host copy")}});
  Sleep(300);
  check("a genuine local copy afterwards does raise the generation", hub.Get().generation == genBefore + 1);
  hub.Stop();
  CoUninitialize();
  std::fprintf(g_out, "\nCHILD RESULT: %s  (%d checks, %d failed)\n", g_failed ? "FAILED" : "PASSED", g_checks, g_failed);
  std::fclose(g_out);
  return g_failed ? 1 : 0;
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
  if (argc >= 3 && wcscmp(argv[1], L"--child") == 0) return run_child(argv[2]);
  std::wstring outDir = L".";
  if (argc >= 3 && wcscmp(argv[1], L"--out") == 0) outDir = argv[2];
  // The bound itself, no clipboard: nothing past limit + 1 units is looked at.
  {
    std::wstring s(10, L'a');
    check("bounded length: a short text is its length", clip_bounded_text_length(s.c_str(), s.size() + 1, 100) == 10);
    std::wstring over(8, L'b');  // no NUL within limit + 1 = 6 units
    check("bounded length: over the limit reports limit + 1 without reading further",
          clip_bounded_text_length(over.data(), 1000000, 5) == 6);
    check("bounded length: no NUL within a small allocation stops at the allocation",
          clip_bounded_text_length(over.data(), 4, 100) == 4);
    check("bounded length: exactly the limit is kept", clip_bounded_text_length(s.c_str(), s.size() + 1, 10) == 10);
  }
  const DWORD interactiveBefore = GetClipboardSequenceNumber();
  HWINSTA ws = CreateWindowStationW(nullptr, 0, WINSTA_ALL_ACCESS, nullptr);  // unnamed: medium may not name one
  if (!ws) {
    std::printf("FAIL  a private window station (err %lu)\n", GetLastError());
    return 1;
  }
  wchar_t name[256] = L"";
  DWORD len = 0;
  GetUserObjectInformationW(ws, UOI_NAME, name, sizeof(name), &len);
  HWINSTA orig = GetProcessWindowStation();
  SetProcessWindowStation(ws);
  HDESK dk = CreateDesktopW(L"Default", nullptr, nullptr, 0, GENERIC_ALL, nullptr);
  SetProcessWindowStation(orig);
  std::wstring desktop = std::wstring(name) + L"\\Default";
  wchar_t self[MAX_PATH];
  GetModuleFileNameW(nullptr, self, MAX_PATH);
  const std::wstring result = outDir + L"\\clip_station_child.txt";
  DeleteFileW(result.c_str());
  std::wstring cmd = L"\"" + std::wstring(self) + L"\" --child \"" + result + L"\"";
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.lpDesktop = desktop.data();
  PROCESS_INFORMATION pi{};
  const bool started = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi) != FALSE;
  DWORD code = 99;
  if (started) {
    WaitForSingleObject(pi.hProcess, 60000);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
  }
  check("the child ran on the private station", started && dk != nullptr);
  if (FILE* f = _wfopen(result.c_str(), L"r")) {
    char line[512];
    while (std::fgets(line, sizeof(line), f)) {
      std::printf("  child: %s", line);
      if (std::strncmp(line, "PASS", 4) == 0) ++g_checks;
      if (std::strncmp(line, "FAIL", 4) == 0) {
        ++g_checks;
        ++g_failed;
      }
    }
    std::fclose(f);
  }
  check("the child exited 0", code == 0);
  const DWORD interactiveAfter = GetClipboardSequenceNumber();
  check("the interactive clipboard (WinSta0) did not move: " + std::to_string(interactiveBefore) + " -> " +
            std::to_string(interactiveAfter),
        interactiveBefore == interactiveAfter);
  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", g_failed ? "FAILED" : "PASSED", g_checks, g_failed);
  return g_failed ? 1 : 0;
}
