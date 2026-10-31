// The transfer bar's words and when it shows them (viewer_clip_transfer_bar.hpp) -- the functions the
// bar itself calls, no window. The window, the click and the host are viewer_clip_bar_e2e_test.
// `--shots <dir>` (helper-shell-token r2): the helper-failure lines in the product bar window, saved
// as PNG (display-capture: needs an interactive desktop).

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "poc_protocol.hpp"
#include "viewer_clip_transfer_bar.hpp"  // brings winsock2.h before windows.h
#include "clip_image_wic.hpp"

using namespace remote60::native_poc;

namespace {
int g_checks = 0, g_failed = 0;
void check(const char* name, bool ok) {
  ++g_checks;
  if (!ok) ++g_failed;
  std::printf("%s  %s\n", ok ? "PASS" : "FAIL", name);
}
bool has(const std::wstring& s, const std::wstring& p) { return s.find(p) != std::wstring::npos; }
ClipImageClient::Progress sending(uint64_t done, uint64_t total, uint64_t ms, uint64_t finished) {
  ClipImageClient::Progress p;
  p.active = true;
  p.bytesConfirmed = done;
  p.bytesTotal = total;
  p.elapsedMs = ms;
  p.finished = finished;
  return p;
}
ClipImageClient::Progress cancelling(ClipImageReason why, uint64_t finished) {
  ClipImageClient::Progress p;
  p.cancelling = true;
  p.cancellingWhy = static_cast<uint8_t>(why);
  p.finished = finished;
  return p;
}
ClipImageClient::Progress ended(ClipOutcome o, uint8_t detail, uint64_t finished, uint64_t total = 5u << 20,
                                uint64_t ms = 31000) {
  ClipImageClient::Progress p;
  p.finished = finished;
  p.outcome = o;
  p.detail = detail;
  p.bytesTotal = total;
  p.bytesConfirmed = total;
  p.elapsedMs = ms;
  return p;
}
std::wstring result_text(ClipOutcome o, uint8_t detail) {
  ClipBarView r;
  r.phase = ClipBarPhase::Result;
  r.bytesTotal = 5u << 20;
  r.elapsedMs = 31000;
  r.outcome = o;
  r.detail = detail;
  return clip_transfer_bar_text(r);
}

// helper-shell-token r2: the helper-failure lines in the PRODUCT bar window (clip_transfer_bar_create,
// its WndProc, its timer, its layout and paint), fed a progress snapshot instead of a live
// FileCopyClient. What is the product's: the window, the view, the words, the layout, the font.
// What is not: where the progress comes from (a fixture snapshot), the owner (a plain window of
// this test, not the viewer). Each case: the owner at a given client width, the bar's own pixels
// (PrintWindow) composited at its place, saved as PNG; the bar must be visible and not clamped
// (its width below the owner's client width minus the margin = the whole line drawn, no ellipsis).
LRESULT CALLBACK shot_owner_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
  if (m == WM_ERASEBKGND) {
    RECT r;
    GetClientRect(h, &r);
    HBRUSH b = CreateSolidBrush(RGB(70, 90, 120));
    FillRect(reinterpret_cast<HDC>(w), &r, b);
    DeleteObject(b);
    return 1;
  }
  return DefWindowProcW(h, m, w, l);
}

bool save_shot(HWND owner, HWND bar, const std::wstring& path, std::string* detail) {
  RECT vr{};
  GetWindowRect(owner, &vr);
  const int w = vr.right - vr.left, h = vr.bottom - vr.top;
  HDC screen = GetDC(nullptr);
  HDC mem = CreateCompatibleDC(screen);
  BITMAPINFO bi{};
  bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
  bi.bmiHeader.biWidth = w;
  bi.bmiHeader.biHeight = h;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  void* bits = nullptr;
  HBITMAP dib = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
  HGDIOBJ old = SelectObject(mem, dib);
  const BOOL okOwner = PrintWindow(owner, mem, PW_RENDERFULLCONTENT);
  RECT br{};
  GetWindowRect(bar, &br);
  const int bw = br.right - br.left, bh = br.bottom - br.top;
  HDC barDc = CreateCompatibleDC(screen);
  HBITMAP barBmp = CreateCompatibleBitmap(screen, bw, bh);
  HGDIOBJ oldBar = SelectObject(barDc, barBmp);
  const BOOL okBar = PrintWindow(bar, barDc, 0);
  BitBlt(mem, br.left - vr.left, br.top - vr.top, bw, bh, barDc, 0, 0, SRCCOPY);
  SelectObject(barDc, oldBar);
  DeleteObject(barBmp);
  DeleteDC(barDc);
  GdiFlush();
  std::vector<uint8_t> dibBytes(sizeof(BITMAPV5HEADER) + static_cast<size_t>(w) * h * 4);
  BITMAPV5HEADER v5{};
  v5.bV5Size = sizeof(v5);
  v5.bV5Width = w;
  v5.bV5Height = h;
  v5.bV5Planes = 1;
  v5.bV5BitCount = 32;
  v5.bV5Compression = BI_BITFIELDS;
  v5.bV5RedMask = 0x00FF0000;
  v5.bV5GreenMask = 0x0000FF00;
  v5.bV5BlueMask = 0x000000FF;
  v5.bV5AlphaMask = 0xFF000000;
  v5.bV5CSType = LCS_sRGB;
  std::memcpy(dibBytes.data(), &v5, sizeof(v5));
  const uint8_t* src = static_cast<const uint8_t*>(bits);
  for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
    uint32_t px;
    std::memcpy(&px, src + i * 4, 4);
    px |= 0xFF000000u;
    std::memcpy(dibBytes.data() + sizeof(v5) + i * 4, &px, 4);
  }
  SelectObject(mem, old);
  DeleteObject(dib);
  DeleteDC(mem);
  ReleaseDC(nullptr, screen);
  std::vector<uint8_t> png;
  uint32_t pw = 0, ph = 0;
  bool written = false;
  if (clip_dib_to_png(dibBytes.data(), dibBytes.size(), &png, &pw, &ph) == ClipWicResult::Ok) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    written = out.good();
  }
  if (detail) {
    *detail = "owner " + std::to_string(w) + "x" + std::to_string(h) + " bar " + std::to_string(bw) + "x" + std::to_string(bh) +
              " printOwner=" + std::to_string(okOwner) + " printBar=" + std::to_string(okBar) + " png=" + std::to_string(png.size());
  }
  return okOwner && okBar && written;
}

int run_shots(const std::wstring& dir) {
  CreateDirectoryW(dir.c_str(), nullptr);
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = shot_owner_proc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"GNLinkClipBarShotOwner";
  RegisterClassExW(&wc);
  struct Case {
    const char* name;
    bool here, missing;
    const wchar_t* expect;
  };
  const Case cases[] = {
      {"remote", false, false, L"원격 PC의 파일 복사 도우미를 시작하지 못했습니다. 설치 상태와 실행 권한을 확인해 주세요"},
      {"here", true, false, L"이 PC의 파일 복사 도우미를 시작하지 못했습니다. 설치 상태와 실행 권한을 확인해 주세요"},
      {"here_missing", true, true, L"이 PC의 GNLink 에 파일 복사 도우미가 없습니다. GNLink 를 업데이트하거나 다시 설치해 주세요"},
  };
  const int widths[] = {1280, 960};
  for (int cw : widths) {
    for (const Case& c : cases) {
      RECT r{0, 0, cw, 540};
      AdjustWindowRectEx(&r, WS_OVERLAPPEDWINDOW, FALSE, 0);
      HWND owner = CreateWindowExW(0, wc.lpszClassName, L"clip bar shot", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 40, 40,
                                   r.right - r.left, r.bottom - r.top, nullptr, nullptr, wc.hInstance, nullptr);
      int polls = 0;
      ClipTransferBarHooks hooks;
      hooks.progress = [] { return ClipImageClient::Progress{}; };
      hooks.fileProgress = [&] {
        FileCopyClient::Progress p;
        p.noHelper = ++polls > 1 ? 1 : 0;  // the first snapshot is the baseline: only a later failure is news
        p.noHelperHere = c.here;
        p.noHelperHereMissing = c.missing;
        return p;
      };
      clip_transfer_bar_create(owner, hooks);
      HWND bar = clip_transfer_bar_window();
      const auto t0 = GetTickCount64();
      MSG msg;
      while (GetTickCount64() - t0 < 1500) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
          TranslateMessage(&msg);
          DispatchMessageW(&msg);
        }
        if (clip_transfer_bar_current().isFile && IsWindowVisible(bar) && GetTickCount64() - t0 > 700) break;
        Sleep(15);
      }
      const std::wstring text = clip_transfer_bar_text(clip_transfer_bar_current());
      RECT client{}, br{};
      GetClientRect(owner, &client);
      GetWindowRect(bar, &br);
      const int dpi = static_cast<int>(GetDpiForWindow(owner));
      const int clamp = (std::max)(static_cast<int>(client.right) - MulDiv(16, dpi, 96), MulDiv(120, dpi, 96));
      const int barW = br.right - br.left;
      const std::wstring path = dir + L"\\bar_" + std::to_wstring(cw) + L"_" +
                                std::wstring(c.name, c.name + std::strlen(c.name)) + L".png";
      std::string detail;
      const bool saved = save_shot(owner, bar, path, &detail);
      const std::string label = std::string("shot ") + std::to_string(cw) + " " + c.name;
      check((label + ": the product bar is visible with the line").c_str(), IsWindowVisible(bar) && text == c.expect);
      check((label + ": the whole line fits (bar " + std::to_string(barW) + " < clamp " + std::to_string(clamp) + ", no ellipsis)").c_str(),
            barW > 0 && barW < clamp);
      check((label + ": saved (" + detail + ")").c_str(), saved);
      clip_transfer_bar_destroy();
      DestroyWindow(owner);
    }
  }
  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", g_failed ? "FAILED" : "PASSED", g_checks, g_failed);
  return g_failed ? 1 : 0;
}
}  // namespace

int main(int argc, char** argv) {
  if (argc >= 3 && std::strcmp(argv[1], "--shots") == 0) {
    const int n = MultiByteToWideChar(CP_ACP, 0, argv[2], -1, nullptr, 0);
    std::wstring dir(n > 0 ? n - 1 : 0, wchar_t{0});
    if (n > 0) MultiByteToWideChar(CP_ACP, 0, argv[2], -1, dir.data(), n);
    return run_shots(dir);
  }
  // ---- the words while sending
  ClipBarView v;
  v.phase = ClipBarPhase::Sending;
  v.bytesDone = 2254857;  // 43 % of 5 MiB
  v.bytesTotal = 5u << 20;
  v.elapsedMs = 4200;
  const std::wstring s = clip_transfer_bar_text(v);
  check("sending: percent, sizes in MB, seconds", s == L"원격 PC로 이미지 보내는 중 43% (2.2 / 5.0 MB) · 4초");
  check("sending offers Cancel", clip_transfer_bar_has_cancel(v));
  v.elapsedMs = 12000;
  const std::wstring slow = clip_transfer_bar_text(v);
  check("past 10 s the line says it is taking time (never 'instant')", has(slow, L"전송에 시간이 걸리고 있습니다"));
  check("...and does NOT claim the network is why (nothing measures why)", !has(slow, L"네트워크"));
  v.bytesDone = v.bytesTotal;
  check("sending never says 100 % (the host has not published yet)", has(clip_transfer_bar_text(v), L" 99% "));
  v.bytesDone = 0;
  v.bytesTotal = 0;
  check("sending with nothing known yet says 0 %", has(clip_transfer_bar_text(v), L" 0% "));

  // ---- cancelling: stopped here, the host's answer awaited
  ClipBarView c;
  c.phase = ClipBarPhase::Cancelling;
  c.cancellingWhy = static_cast<uint8_t>(ClipImageReason::User);
  check("the user's cancel says 'cancelling', not 'cancelled'", clip_transfer_bar_text(c) == L"이미지 보내기를 취소하는 중…");
  check("...with no Cancel button", !clip_transfer_bar_has_cancel(c));
  c.cancellingWhy = static_cast<uint8_t>(ClipImageReason::Superseded);
  check("a newer copy's cancel says it is switching", has(clip_transfer_bar_text(c), L"새로 복사한 내용으로 바꾸는 중"));

  // ---- the outcomes
  check("published: where it went, size and time",
        result_text(ClipOutcome::Published, 0) == L"이미지를 원격 PC 클립보드에 넣었습니다 (5.0 MB, 31초)");
  ClipBarView r;
  r.phase = ClipBarPhase::Result;
  check("a result offers no Cancel", !clip_transfer_bar_has_cancel(r));
  check("cancelled by the user (confirmed)",
        result_text(ClipOutcome::Cancelled, static_cast<uint8_t>(ClipImageReason::User)) == L"이미지 보내기를 취소했습니다");
  check("cancelled by a newer copy", has(result_text(ClipOutcome::Cancelled, static_cast<uint8_t>(ClipImageReason::Superseded)),
                                         L"새로 복사한 내용"));
  check("cancelled by the session ending", has(result_text(ClipOutcome::Cancelled, static_cast<uint8_t>(ClipImageReason::Session)),
                                               L"연결이 끊겨"));
  const std::wstring late = result_text(ClipOutcome::CancelTooLate, static_cast<uint8_t>(ClipImageReason::User));
  check("a cancel after the host published says it is there (not 'cancelled')",
        has(late, L"이미 원격 PC 클립보드에 들어갔습니다") && !has(late, L"취소했습니다"));
  const std::wstring unk = result_text(ClipOutcome::CancelUnconfirmed, static_cast<uint8_t>(ClipImageReason::User));
  check("a cancel the host never answered says it is not known", has(unk, L"확인하지 못했습니다") && !has(unk, L"취소했습니다"));
  check("the host's clipboard changed first", has(result_text(ClipOutcome::HostSuperseded, 0), L"원격 PC 클립보드가 먼저 바뀌어"));
  check("failed: says it failed and why",
        result_text(ClipOutcome::Failed, static_cast<uint8_t>(ClipImageReason::Stalled)) ==
            L"이미지를 보내지 못했습니다 (원격 PC가 응답하지 않음)");
  check("a copy too large to send says so",
        result_text(ClipOutcome::NotSent, static_cast<uint8_t>(ClipPackageResult::TooLarge)) == L"이미지가 너무 커서 보내지 않았습니다");
  check("an unreadable copy says so", has(result_text(ClipOutcome::NotSent, static_cast<uint8_t>(ClipPackageResult::ReadFailed)),
                                          L"읽지 못해 보내지 않았습니다"));
  check("an encode failure says so", has(result_text(ClipOutcome::NotSent, static_cast<uint8_t>(ClipPackageResult::EncodeFailed)),
                                         L"보내지 않았습니다"));
  check("the host refusing for size says so", has(result_text(ClipOutcome::Refused, static_cast<uint8_t>(ClipImageVerdict::TooLarge)),
                                                  L"크기 제한"));
  check("the host with sharing off says so", has(result_text(ClipOutcome::Refused, static_cast<uint8_t>(ClipImageVerdict::Disabled)),
                                                 L"클립보드 공유가 꺼져"));
  check("the host busy says so", has(result_text(ClipOutcome::Refused, static_cast<uint8_t>(ClipImageVerdict::Busy)), L"다른 이미지를 받는 중"));
  ClipBarView hidden;
  check("hidden says nothing", clip_transfer_bar_text(hidden).empty() && !clip_transfer_bar_has_cancel(hidden));

  // ---- when
  uint64_t seen = 0, until = 1;  // as the bar starts
  ClipBarView a = clip_transfer_bar_view(ended(ClipOutcome::Published, 0, 3), 1000000, &seen, &until);
  check("the first poll does not replay what ended before the bar existed", a.phase == ClipBarPhase::Hidden);
  a = clip_transfer_bar_view(sending(1000, 5000, 500, 3), 1200000, &seen, &until);
  check("an active transfer shows Sending", a.phase == ClipBarPhase::Sending && a.bytesDone == 1000);
  a = clip_transfer_bar_view(cancelling(ClipImageReason::User, 3), 1300000, &seen, &until);
  check("a cancel not settled yet shows Cancelling", a.phase == ClipBarPhase::Cancelling);
  a = clip_transfer_bar_view(ended(ClipOutcome::Cancelled, static_cast<uint8_t>(ClipImageReason::User), 4), 1400000, &seen,
                             &until);
  check("the settled outcome shows the result", a.phase == ClipBarPhase::Result && a.outcome == ClipOutcome::Cancelled &&
                                                    a.detail == static_cast<uint8_t>(ClipImageReason::User));
  a = clip_transfer_bar_view(ended(ClipOutcome::Cancelled, 9, 4), 1400000 + kClipBarResultUs - 1, &seen, &until);
  check("...for about 5 s", a.phase == ClipBarPhase::Result);
  a = clip_transfer_bar_view(ended(ClipOutcome::Cancelled, 9, 4), 1400000 + kClipBarResultUs, &seen, &until);
  check("...then the bar hides", a.phase == ClipBarPhase::Hidden);
  // A fast LAN copy can begin and end between two 250 ms polls: its outcome is still news. So is a
  // copy that never left this PC (too large): it has no Sending phase at all.
  a = clip_transfer_bar_view(ended(ClipOutcome::Published, 0, 5, 1u << 20, 180), 9000000, &seen, &until);
  check("a transfer never seen active still gets its result line", a.phase == ClipBarPhase::Result &&
                                                                       a.outcome == ClipOutcome::Published);
  a = clip_transfer_bar_view(ended(ClipOutcome::NotSent, static_cast<uint8_t>(ClipPackageResult::TooLarge), 6), 9050000,
                             &seen, &until);
  check("a copy that was never sent gets its result line", a.phase == ClipBarPhase::Result && a.outcome == ClipOutcome::NotSent);
  a = clip_transfer_bar_view(sending(0, 5000, 10, 6), 9100000, &seen, &until);
  check("a new transfer replaces the result line", a.phase == ClipBarPhase::Sending);
  a = clip_transfer_bar_view(ended(ClipOutcome::Failed, static_cast<uint8_t>(ClipImageReason::Timeout), 7), 9300000, &seen, &until);
  check("...and its own end is shown", a.phase == ClipBarPhase::Result &&
                                           a.detail == static_cast<uint8_t>(ClipImageReason::Timeout));

  // ------------------------------------------------------------------ file copy (t-zdmsd4gb D6)
  {
    namespace fn = remote60::native_poc::file_copy::net;
    namespace fc = remote60::native_poc::file_copy;
    FileBarState st;
    FileCopyClient::Progress p;
    uint64_t now = 1000000;
    ClipBarView v = file_transfer_bar_view(p, now, &st);
    check("file: nothing to say -> not the file's line", !v.isFile);
    p.offered = 1;
    p.offeredFiles = 3;
    v = file_transfer_bar_view(p, now, &st);
    check("file: a published offer says a paste is POSSIBLE (not that anything was sent)",
          v.isFile && clip_transfer_bar_text(v) == L"복사한 파일 3개를 원격 PC에서 붙여넣을 수 있습니다" && !clip_transfer_bar_has_cancel(v));
    v = file_transfer_bar_view(p, now + kClipBarResultUs + 1, &st);
    check("file: ...for 5 s, then gone", !v.isFile);
    p.sending = true;
    p.files = 3;
    p.bytesDone = 2621440;
    p.bytesTotal = 10485760;
    p.elapsedMs = 4000;
    v = file_transfer_bar_view(p, now, &st);
    check("file: sending -- files, percent, MB, seconds, Cancel offered",
          clip_transfer_bar_text(v) == L"원격 PC로 파일 3개 보내는 중 25% (2.5 / 10.0 MB) · 4초" && clip_transfer_bar_has_cancel(v));
    p.bytesDone = p.bytesTotal;
    check("file: sending never says 100 % (the consumer has not ended it)",
          has(clip_transfer_bar_text(file_transfer_bar_view(p, now, &st)), L" 99% "));
    p.sending = false;
    p.receiving = true;
    p.bytesDone = 0;
    p.elapsedMs = 12000;
    const std::wstring recv = clip_transfer_bar_text(file_transfer_bar_view(p, now, &st));
    check("file: receiving says so, and past 10 s that it is taking time",
          has(recv, L"원격 PC에서 파일 3개 받는 중 0%") && has(recv, L"전송에 시간이 걸리고 있습니다"));
    p.cancelling = true;
    v = file_transfer_bar_view(p, now, &st);
    check("file: a cancel not yet confirmed says 'cancelling', no Cancel button",
          has(clip_transfer_bar_text(v), L"취소하는 중") && !clip_transfer_bar_has_cancel(v));
    p.cancelling = false;
    p.receiving = false;
    p.finished = 1;
    p.lastToRemote = false;
    p.lastReason = static_cast<uint8_t>(fn::PasteEndReason::Completed);
    p.lastFiles = 3;
    p.lastBytes = 10485760;
    p.lastElapsedMs = 9000;
    v = file_transfer_bar_view(p, now, &st);
    check("file: completed = the consumer ended it successfully",
          clip_transfer_bar_text(v) == L"파일 3개 붙여넣기 완료 (10.0 MB, 9초)" && !clip_transfer_bar_has_cancel(v));
    p.finished = 2;
    p.lastReason = static_cast<uint8_t>(fn::PasteEndReason::Verification);
    p.lastBytes = 4096;
    const std::wstring ver = clip_transfer_bar_text(file_transfer_bar_view(p, now, &st));
    check("file: a failed check says 'transfer data check failed', not 'the source changed'",
          has(ver, L"전송 데이터 검증에 실패") && !has(ver, L"원본"));
    check("file: ...and after bytes moved, that a partial file may be left (removal is not promised)",
          has(ver, L"일부만 저장된 파일이 남았을 수 있습니다"));
    p.finished = 3;
    p.lastReason = static_cast<uint8_t>(fn::PasteEndReason::None);
    p.lastRefused = static_cast<uint16_t>(fc::Status::Replaced);
    p.lastBytes = 0;
    const std::wstring rep = clip_transfer_bar_text(file_transfer_bar_view(p, now, &st));
    check("file: refused before any byte because the source was replaced -- said, and no 'partial file'",
          has(rep, L"다른 파일로 바뀌어") && !has(rep, L"일부만"));
    p.finished = 4;
    p.lastRefused = static_cast<uint16_t>(fc::Status::Refused);
    check("file: refused as busy",
          has(clip_transfer_bar_text(file_transfer_bar_view(p, now, &st)), L"다른 전송이 진행 중"));
    FileBarState fresh;
    check("file: a result from before the bar existed is not news", !file_transfer_bar_view(p, now, &fresh).isFile);
    // r3: the helper could not run -- said, not silent. helper-shell-token r1: and not guessed. The
    // remote PC's reason is not on the wire, so "update" is never said for it (0.2.146 said "update
    // once more" for a token failure an update could not fix); only a helper found MISSING here is.
    now += 2 * kClipBarResultUs;
    p.noHelper = 1;
    p.noHelperHere = false;
    p.noHelperHereMissing = false;
    const std::wstring remoteNo = clip_transfer_bar_text(file_transfer_bar_view(p, now, &st));
    check("file: the REMOTE PC's helper not starting is said as 'could not start', not 'update'",
          remoteNo == L"원격 PC의 파일 복사 도우미를 시작하지 못했습니다. 설치 상태와 실행 권한을 확인해 주세요" &&
              !has(remoteNo, L"업데이트"));
    check("file: ...for 5 s, then gone", !file_transfer_bar_view(p, now + kClipBarResultUs + 1, &st).isFile);
    p.noHelper = 2;
    p.noHelperHere = true;
    p.noHelperHereMissing = false;
    const std::wstring hereNo = clip_transfer_bar_text(file_transfer_bar_view(p, now + 2 * kClipBarResultUs, &st));
    check("file: THIS PC's helper not starting (not missing) names this PC, 'could not start', not 'update'",
          hereNo == L"이 PC의 파일 복사 도우미를 시작하지 못했습니다. 설치 상태와 실행 권한을 확인해 주세요" &&
              !has(hereNo, L"업데이트"));
    p.noHelper = 3;
    p.noHelperHereMissing = true;
    check("file: THIS PC's helper MISSING is the one case that says update / reinstall",
          clip_transfer_bar_text(file_transfer_bar_view(p, now + 4 * kClipBarResultUs, &st)) ==
              L"이 PC의 GNLink 에 파일 복사 도우미가 없습니다. GNLink 를 업데이트하거나 다시 설치해 주세요");
    p.noHelper = 4;
    p.noHelperHere = false;
    check("file: ...a stale 'missing' flag never turns a REMOTE refusal into 'update'",
          !has(clip_transfer_bar_text(file_transfer_bar_view(p, now + 6 * kClipBarResultUs, &st)), L"업데이트"));
  }

  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", g_failed ? "FAILED" : "PASSED", g_checks, g_failed);
  return g_failed ? 1 : 0;
}
