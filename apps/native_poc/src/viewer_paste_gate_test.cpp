// Paste on demand (t-y4wj64jw): the viewer's decisions without a window (viewer_paste_gate.hpp).
// Pure: no window, no socket, no clock of its own.

#include "viewer_paste_gate.hpp"

#include <iostream>
#include <string>

namespace {

using namespace remote60::native_poc::viewer;

int gChecks = 0;
int gFailures = 0;

void check(const std::string& name, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::cout << (ok ? "PASS  " : "FAIL  ") << name;
  if (!detail.empty()) std::cout << "  " << detail;
  std::cout << "\n";
}

PasteMods mods(bool ctrl, bool shift = false, bool alt = false, bool win = false) {
  PasteMods m;
  m.ctrl = ctrl;
  m.shift = shift;
  m.alt = alt;
  m.win = win;
  return m;
}

PasteGate::Ticket ticket(uint64_t id, PasteFormat f = PasteFormat::Text, uint64_t budget = kPasteTextDeadlineUs,
                         uint64_t conn = 1, uint64_t target = 1) {
  PasteGate::Ticket t;
  t.id = id;
  t.connGen = conn;
  t.targetGen = target;
  t.key = PasteKey::CtrlV;
  t.format = f;
  t.budgetUs = budget;
  return t;
}

constexpr uint32_t kInsert = 0x2D;

}  // namespace

int main() {
  // ---------------------------------------------------------------- which keys are a paste
  check("Ctrl+V is a paste", classify_paste_key('V', mods(true)) == PasteKey::CtrlV);
  check("Ctrl+Shift+V is a paste (as text)", classify_paste_key('V', mods(true, true)) == PasteKey::CtrlShiftV);
  check("Shift+Insert is a paste", classify_paste_key(kInsert, mods(false, true)) == PasteKey::ShiftInsert);
  check("V alone is not", classify_paste_key('V', mods(false)) == PasteKey::None);
  check("Ctrl+Alt+V (AltGr+V) is not", classify_paste_key('V', mods(true, false, true)) == PasteKey::None);
  check("Win+V (clipboard history) is not", classify_paste_key('V', mods(true, false, false, true)) == PasteKey::None);
  check("Ctrl+Insert is not a paste (it is a copy)", classify_paste_key(kInsert, mods(true)) == PasteKey::None);
  check("Ctrl+Shift+Insert is not a paste", classify_paste_key(kInsert, mods(true, true)) == PasteKey::None);
  check("Insert alone is not", classify_paste_key(kInsert, mods(false)) == PasteKey::None);
  check("Ctrl+C / Ctrl+X / Ctrl+Insert are copies",
        is_copy_key('C', mods(true)) && is_copy_key('X', mods(true)) && is_copy_key(kInsert, mods(true)));
  check("C alone, Ctrl+Alt+C, Ctrl+V are not copies",
        !is_copy_key('C', mods(false)) && !is_copy_key('C', mods(true, false, true)) && !is_copy_key('V', mods(true)));

  // ---------------------------------------------------------------- what a gesture does
  check("feature off: the key goes as before", route_paste(false, true, true) == PasteRoute::Forward);
  check("remote copy newest: the key goes at once", route_paste(true, true, false) == PasteRoute::PassThrough);
  check("...even to an older host", route_paste(true, false, false) == PasteRoute::PassThrough);
  check("local copy newest + confirming host: send", route_paste(true, true, true) == PasteRoute::Send);
  check("local copy newest + older host: update needed, no key", route_paste(true, false, true) == PasteRoute::UpdateNeeded);

  {
    LatestCopy l;
    l.ResetForSession(true);
    check("a session with something copied here starts local", l.LocalIsLatest());
    l.NoteRemoteCopy();
    check("a copy on the remote PC makes it the newest", !l.LocalIsLatest());
    l.NoteLocalCopy();
    check("then a copy here takes it back", l.LocalIsLatest());
    l.ResetForSession(false);
    check("a session with nothing pasteable here starts remote", !l.LocalIsLatest());
  }

  // ---------------------------------------------------------------- the rebuilt key sequence
  {
    const auto c = paste_chord(PasteKey::CtrlV, 0, false);
    check("Ctrl+V chord is four balanced edges", c.size() == 4 && c[0].down && c[0].vk == 0x11 && c[1].down &&
                                                     c[1].vk == 'V' && !c[2].down && c[2].vk == 'V' && !c[3].down &&
                                                     c[3].vk == 0x11);
    check("...with the US scan codes by default", c[0].scan == 0x1D && c[1].scan == 0x2F && !c[1].ext);
    const auto r = paste_chord(PasteKey::CtrlV, 0x34, false);
    check("...and the user's own scan code when known", r[1].scan == 0x34 && r[2].scan == 0x34);
    const auto s = paste_chord(PasteKey::CtrlShiftV, 0, false);
    check("Ctrl+Shift+V: both modifiers down first, up last in reverse",
          s.size() == 6 && s[0].vk == 0x11 && s[1].vk == 0x10 && s[2].vk == 'V' && s[3].vk == 'V' && s[4].vk == 0x10 &&
              s[5].vk == 0x11 && s[0].down && s[1].down && !s[4].down && !s[5].down);
    const auto i = paste_chord(PasteKey::ShiftInsert, 0, false);
    check("Shift+Insert chord: Insert is the extended key", i.size() == 4 && i[0].vk == 0x10 && i[1].vk == kInsert &&
                                                                i[1].scan == 0x52 && i[1].ext && !i[3].down);
    check("no chord for no key", paste_chord(PasteKey::None, 0, false).empty());
    int balance = 0;
    for (const auto& st : s) balance += st.down ? 1 : -1;
    check("every chord is balanced (nothing left held)", balance == 0);
  }

  // ---------------------------------------------------------------- one pending, one waiting
  {
    PasteGate g;
    check("admit: the first paste starts", g.Admit(ticket(1), 1000) == PasteGate::Admitted::Started);
    check("...with its deadline", g.pending().deadlineUs == 1000 + kPasteTextDeadlineUs);
    check("a second while pending waits", g.Admit(ticket(2), 2000) == PasteGate::Admitted::Waiting);
    check("a third is dropped", g.Admit(ticket(3), 3000) == PasteGate::Admitted::Dropped);
    PasteGate::Ticket done;
    check("an answer for another id is ignored", g.OnAnswer(99, true, 1, 1, &done) == PasteGate::Outcome::Ignored);
    check("...and the paste is still pending", g.Pending() && g.pending().id == 1);
    check("the waiting one's answer (early) is ignored", g.OnAnswer(2, true, 1, 1, &done) == PasteGate::Outcome::Ignored);
    check("applied: inject", g.OnAnswer(1, true, 1, 1, &done) == PasteGate::Outcome::Inject && done.id == 1);
    check("...exactly once: the same answer again is ignored", g.OnAnswer(1, true, 1, 1, &done) == PasteGate::Outcome::Ignored);
    PasteGate::Ticket next;
    check("promote: the waiting paste starts", g.Promote(50000, &next) && next.id == 2 && g.pending().id == 2);
    check("...its deadline counted from its own start", g.pending().deadlineUs == 50000 + kPasteTextDeadlineUs);
    check("nothing else waits", !g.HasWaiting() && !g.Promote(60000, &next));
    check("refused: fail, no key", g.OnAnswer(2, false, 1, 1, &done) == PasteGate::Outcome::Fail && !g.Pending());
  }
  {
    PasteGate g;
    g.Admit(ticket(7, PasteFormat::Text, kPasteTextDeadlineUs, 5, 9), 0);
    PasteGate::Ticket done;
    check("applied on an older connection: fail (the key would go elsewhere)",
          g.OnAnswer(7, true, 6, 9, &done) == PasteGate::Outcome::Fail);
    g.Admit(ticket(8, PasteFormat::Text, kPasteTextDeadlineUs, 5, 9), 0);
    check("applied after the target changed: fail", g.OnAnswer(8, true, 5, 10, &done) == PasteGate::Outcome::Fail);
  }
  {
    PasteGate g;
    g.Admit(ticket(1), 0);
    PasteGate::Ticket exp;
    check("no timeout before the deadline", !g.OnTick(kPasteTextDeadlineUs - 1, &exp) && g.Pending());
    check("timeout at the deadline", g.OnTick(kPasteTextDeadlineUs, &exp) && exp.id == 1 && !g.Pending());
    PasteGate::Ticket done;
    check("a late answer after the timeout is ignored", g.OnAnswer(1, true, 1, 1, &done) == PasteGate::Outcome::Ignored);
    g.Admit(ticket(2, PasteFormat::Image, 0), 0);
    check("an image has no gate deadline (its transfer has one, and Cancel)", !g.OnTick(3600000000ULL, &exp) && g.Pending());
    g.Admit(ticket(3, PasteFormat::Files, kPasteFilesDeadlineUs), 0);
    const auto ended = g.CancelAll();
    check("cancel ends the pending and the waiting paste", ended.size() == 2 && !g.Pending() && !g.HasWaiting());
  }

  // ---------------------------------------------------------------- keys typed while waiting
  {
    PasteGate g;
    g.Admit(ticket(1), 0);
    check("a key down is held", g.Hold(HeldKey{0x0100, 'A', 0}));
    check("...and its up is recognised as held", g.HoldsDown('A'));
    check("a key never held down is not", !g.HoldsDown('B'));
    g.Hold(HeldKey{0x0101, 'A', 0});
    check("after its up the key is no longer held down", !g.HoldsDown('A'));
    bool overflow = false;
    for (size_t i = g.HeldCount(); i < kPasteHeldKeyLimit + 1; ++i) {
      if (!g.Hold(HeldKey{0x0100, 'Z', 0})) overflow = true;
    }
    check("the held keys are bounded", overflow && g.HeldCount() == kPasteHeldKeyLimit);
    PasteGate::Ticket done;
    g.OnAnswer(1, true, 1, 1, &done);
    const auto held = g.TakeHeld();
    check("on inject the held keys are handed back in order", held.size() == kPasteHeldKeyLimit && held[0].wp == 'A' &&
                                                                  held[0].msg == 0x0100 && held[1].msg == 0x0101);
    check("...once", g.HeldCount() == 0);
    g.Admit(ticket(2), 0);
    g.Hold(HeldKey{0x0100, 'Q', 0});
    g.OnAnswer(2, false, 1, 1, &done);
    check("on failure the held keys are dropped", g.HeldCount() == 0);
    g.Admit(ticket(3), 0);
    g.Hold(HeldKey{0x0100, 'Q', 0});
    PasteGate::Ticket exp;
    g.OnTick(kPasteTextDeadlineUs, &exp);
    check("on timeout too", g.HeldCount() == 0);
  }

  // ---------------------------------------------------------------- the bar's lines
  check("a failure says the paste did not run",
        paste_failure_text(PasteFailure::Timeout).find(L"붙여넣기를 실행하지 않았습니다") != std::wstring::npos);
  check("an older host: update", paste_failure_text(PasteFailure::UpdateNeeded).find(L"업데이트") != std::wstring::npos);
  check("cancel", paste_failure_text(PasteFailure::Cancelled) == L"붙여넣기를 취소했습니다");
  check("silent says nothing", paste_failure_text(PasteFailure::Silent).empty() && paste_failure_text(PasteFailure::None).empty());

  std::cout << "\n" << (gFailures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILED") << "  (" << gChecks << " checks, "
            << gFailures << " failed)\n";
  return gFailures == 0 ? 0 : 1;
}
