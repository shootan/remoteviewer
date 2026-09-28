// The transfer bar's words and when it shows them (viewer_clip_transfer_bar.hpp) -- the functions the
// bar itself calls, no window. The window, the click and the host are viewer_clip_bar_e2e_test.

#include <cstdio>
#include <string>

#include "poc_protocol.hpp"
#include "viewer_clip_transfer_bar.hpp"

using namespace remote60::native_poc;

namespace {
int g_checks = 0, g_failed = 0;
void check(const char* name, bool ok) {
  ++g_checks;
  if (!ok) ++g_failed;
  std::printf("%s  %s\n", ok ? "PASS" : "FAIL", name);
}
bool starts(const std::wstring& s, const std::wstring& p) { return s.compare(0, p.size(), p) == 0; }
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
ClipImageClient::Progress ended(ClipImageState s, ClipImageReason r, uint64_t finished, uint64_t total = 5u << 20,
                                uint64_t ms = 31000) {
  ClipImageClient::Progress p;
  p.finished = finished;
  p.lastState = static_cast<uint8_t>(s);
  p.lastReason = static_cast<uint8_t>(r);
  p.bytesTotal = total;
  p.bytesConfirmed = total;
  p.elapsedMs = ms;
  return p;
}
}  // namespace

int main() {
  // ---- the words
  ClipBarView v;
  v.phase = ClipBarPhase::Sending;
  v.bytesDone = 2254857;  // 43 % of 5 MiB
  v.bytesTotal = 5u << 20;
  v.elapsedMs = 4200;
  const std::wstring s = clip_transfer_bar_text(v);
  check("sending: percent, sizes in MB, seconds", s == L"원격 PC로 이미지 보내는 중 43% (2.2 / 5.0 MB) · 4초");
  check("sending offers Cancel", clip_transfer_bar_has_cancel(v));
  v.elapsedMs = 12000;
  check("past 10 s the line says the network is why (never 'instant')",
        has(clip_transfer_bar_text(v), L"네트워크가 느려 시간이 걸리고 있습니다"));
  v.bytesDone = v.bytesTotal;
  check("sending never says 100 % (the host has not published yet)", has(clip_transfer_bar_text(v), L" 99% "));
  v.bytesDone = 0;
  v.bytesTotal = 0;
  check("sending with nothing known yet says 0 %", has(clip_transfer_bar_text(v), L" 0% "));

  ClipBarView r;
  r.phase = ClipBarPhase::Result;
  r.bytesTotal = 5u << 20;
  r.elapsedMs = 31000;
  r.state = static_cast<uint8_t>(ClipImageState::Published);
  check("published: where it went, size and time",
        clip_transfer_bar_text(r) == L"이미지를 원격 PC 클립보드에 넣었습니다 (5.0 MB, 31초)");
  check("a result offers no Cancel", !clip_transfer_bar_has_cancel(r));
  r.state = static_cast<uint8_t>(ClipImageState::Cancelled);
  r.reason = static_cast<uint8_t>(ClipImageReason::User);
  check("cancelled by the user", clip_transfer_bar_text(r) == L"이미지 보내기를 취소했습니다");
  r.reason = static_cast<uint8_t>(ClipImageReason::Superseded);
  check("cancelled by a newer copy says so", has(clip_transfer_bar_text(r), L"새로 복사한 내용"));
  r.reason = static_cast<uint8_t>(ClipImageReason::Session);
  check("cancelled by the session ending says so", has(clip_transfer_bar_text(r), L"연결이 끊겨"));
  r.state = static_cast<uint8_t>(ClipImageState::Superseded);
  r.reason = 0;
  check("the host's clipboard changed first", has(clip_transfer_bar_text(r), L"원격 PC 클립보드가 먼저 바뀌어"));
  r.state = static_cast<uint8_t>(ClipImageState::Failed);
  r.reason = static_cast<uint8_t>(ClipImageReason::Stalled);
  check("failed: says it failed and why",
        clip_transfer_bar_text(r) == L"이미지를 보내지 못했습니다 (원격 PC가 응답하지 않음)");
  r.reason = static_cast<uint8_t>(ClipImageReason::VerifyFailed);
  check("failed: verify", has(clip_transfer_bar_text(r), L"검증 실패"));
  ClipBarView hidden;
  check("hidden says nothing", clip_transfer_bar_text(hidden).empty() && !clip_transfer_bar_has_cancel(hidden));

  // ---- when
  uint64_t seen = 0, until = 1;  // as the bar starts
  ClipBarView a = clip_transfer_bar_view(ended(ClipImageState::Published, ClipImageReason::None, 3), 1000000, &seen, &until);
  check("the first poll does not replay what ended before the bar existed", a.phase == ClipBarPhase::Hidden);
  a = clip_transfer_bar_view(sending(1000, 5000, 500, 3), 1200000, &seen, &until);
  check("an active transfer shows Sending", a.phase == ClipBarPhase::Sending && a.bytesDone == 1000);
  a = clip_transfer_bar_view(ended(ClipImageState::Cancelled, ClipImageReason::User, 4), 1400000, &seen, &until);
  check("its end shows the result", a.phase == ClipBarPhase::Result &&
                                        a.state == static_cast<uint8_t>(ClipImageState::Cancelled) &&
                                        a.reason == static_cast<uint8_t>(ClipImageReason::User));
  a = clip_transfer_bar_view(ended(ClipImageState::Cancelled, ClipImageReason::User, 4), 1400000 + kClipBarResultUs - 1,
                             &seen, &until);
  check("...for about 5 s", a.phase == ClipBarPhase::Result);
  a = clip_transfer_bar_view(ended(ClipImageState::Cancelled, ClipImageReason::User, 4), 1400000 + kClipBarResultUs,
                             &seen, &until);
  check("...then the bar hides", a.phase == ClipBarPhase::Hidden);
  // A fast LAN copy can begin and end between two 250 ms polls: its outcome is still news.
  a = clip_transfer_bar_view(ended(ClipImageState::Published, ClipImageReason::None, 5, 1u << 20, 180), 9000000, &seen,
                             &until);
  check("a transfer never seen active still gets its result line", a.phase == ClipBarPhase::Result &&
                                                                       a.state == static_cast<uint8_t>(ClipImageState::Published));
  // A new transfer replaces a result still on screen.
  a = clip_transfer_bar_view(sending(0, 5000, 10, 5), 9100000, &seen, &until);
  check("a new transfer replaces the result line", a.phase == ClipBarPhase::Sending);
  a = clip_transfer_bar_view(sending(4000, 5000, 900, 5), 9200000, &seen, &until);
  a = clip_transfer_bar_view(ended(ClipImageState::Failed, ClipImageReason::Timeout, 6), 9300000, &seen, &until);
  check("...and its own end is shown", a.phase == ClipBarPhase::Result &&
                                           a.reason == static_cast<uint8_t>(ClipImageReason::Timeout));

  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", g_failed ? "FAILED" : "PASSED", g_checks, g_failed);
  return g_failed ? 1 : 0;
}
