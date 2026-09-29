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
}  // namespace

int main() {
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
  }

  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", g_failed ? "FAILED" : "PASSED", g_checks, g_failed);
  return g_failed ? 1 : 0;
}
