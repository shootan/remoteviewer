// t-970r4zgo (2): a monitor selected through the window-select transaction. Pure logic, the same
// code the host, the shared client and the APK run:
//   monitor_select_target.hpp   -- the target id: classification before any HWND use, and the
//                                  host's resolution against the list this connection was sent
//   selection_ack_gate.hpp      -- the APK decoder's gate: only the answer to THIS selection opens
//                                  it, an IDR that beat its answer is asked for again
//   WindowPanelStateModel       -- a monitor answer is a monitor selection, and a later window list
//                                  (selectedWindowId 0) does not undo it
// No MediaCodec runs here (the APK's codec and display are not exercised); the process-level path
// is host_monitor_fallback_e2e_test.

#include <cstdio>
#include <string>
#include <vector>

#include "monitor_select_target.hpp"
#include "native_video_client_shared_core.hpp"
#include "selection_ack_gate.hpp"

using namespace remote60::native_poc;

namespace {

int gFailures = 0;
int gChecks = 0;

void check(const char* name, bool cond, const std::string& detail = {}) {
  ++gChecks;
  std::printf("%s  %s%s%s\n", cond ? "PASS" : "FAIL", name, detail.empty() ? "" : "  ", detail.c_str());
  if (!cond) ++gFailures;
}

const char* kind_name(SelectTargetKind k) {
  return k == SelectTargetKind::Window ? "window" : k == SelectTargetKind::Monitor ? "monitor" : "malformed";
}

}  // namespace

int main() {
  {
    std::puts("\n--- the target id: classified before it can be an HWND ---");
    struct Case {
      const char* what;
      uint64_t id;
      SelectTargetKind kind;
      uint32_t monitor;
    } cases[] = {
        {"desktop (0)", 0, SelectTargetKind::Window, 0},
        {"a zero-extended HWND", 0x00000000000A0B2CULL, SelectTargetKind::Window, 0},
        {"a sign-extended HWND", 0xFFFFFFFF8000A0B2ULL, SelectTargetKind::Window, 0},
        {"monitor 0 (BASE|0) -- valid, not desktop", 0x7000000000000000ULL, SelectTargetKind::Monitor, 0},
        {"monitor 1", 0x7000000000000001ULL, SelectTargetKind::Monitor, 1},
        {"monitor 0xFFFFFFFF", 0x70000000FFFFFFFFULL, SelectTargetKind::Monitor, 0xFFFFFFFFu},
        {"a reserved bit mixed in (0x7000000100000001) is NOT monitor 1", 0x7000000100000001ULL,
         SelectTargetKind::Malformed, 0},
        {"top nibble 7, other upper bits (0x7FFFFFFF00000002)", 0x7FFFFFFF00000002ULL, SelectTargetKind::Malformed, 0},
        {"just below the namespace (0x6FFFFFFFFFFFFFFF)", 0x6FFFFFFFFFFFFFFFULL, SelectTargetKind::Window, 0},
        {"just above it (0x8000000000000001) -- not taken for a monitor by '>= BASE'", 0x8000000000000001ULL,
         SelectTargetKind::Window, 0},
    };
    for (const auto& c : cases) {
      const SelectTarget t = classify_select_target(c.id);
      check(c.what, t.kind == c.kind && (c.kind != SelectTargetKind::Monitor || t.monitorId == c.monitor),
            std::string(kind_name(t.kind)) + " monitor=" + std::to_string(t.monitorId));
    }
    check("encode(1) is the APK's MONITOR_ID_BASE + 1 (8070450532247928833)",
          encode_monitor_select_target(1) == 8070450532247928833ULL);
  }
  {
    std::puts("\n--- the host's resolution against the list THIS connection was sent ---");
    const std::vector<std::wstring> listed = {L"\\\\.\\DISPLAY1", L"\\\\.\\DISPLAY2"};
    const auto m1 = resolve_select_request(encode_monitor_select_target(1), listed);
    check("monitor 1 -> the device listed as 1", !m1.refused && m1.monitor && m1.device == L"\\\\.\\DISPLAY2");
    const auto m0 = resolve_select_request(encode_monitor_select_target(0), listed);
    check("monitor 0 -> the device listed as 0", !m0.refused && m0.monitor && m0.device == L"\\\\.\\DISPLAY1");
    const auto m5 = resolve_select_request(encode_monitor_select_target(5), listed);
    check("monitor 5 (not listed): refused, no clamp to the last", m5.refused &&
                                                                       std::string(m5.refused) == "monitor_not_listed");
    const auto none = resolve_select_request(encode_monitor_select_target(0), {});
    check("no list sent on this connection: refused", none.refused && std::string(none.refused) == "monitor_not_listed");
    const auto bad = resolve_select_request(0x7000000100000001ULL, listed);
    check("malformed: refused as invalid_target, never looked up as a window",
          bad.refused && std::string(bad.refused) == "invalid_target" && !bad.monitor);
    const auto win = resolve_select_request(0x00000000000A0B2CULL, listed);
    check("an HWND: a window request, not refused here", !win.refused && !win.monitor);
  }
  {
    std::puts("\n--- the selection gate: the answer to THIS selection, then its generation only ---");
    SelectionAckGate g;
    g.Prepare(7);
    check("before the answer nothing is admitted (old picture)", !g.Admit(40));
    check("... not even the new generation's frame", !g.Admit(41));
    const auto stale = g.OnAck(true, 41, 6);
    check("an answer to selection 6 (an earlier pick) is IGNORED and the gate stays shut",
          stale.result == SelectionAckGate::AckResult::Ignored && g.awaiting() && !g.Admit(41));
    const auto ack = g.OnAck(true, 41, 7);
    check("the answer to selection 7 applies with streamGen 41", ack.result == SelectionAckGate::AckResult::Applied &&
                                                                    g.expected() == 41);
    check("the new generation's frame (41) dropped before the answer -> a fresh IDR is asked for", ack.requestKeyframe);
    check("after it: generation 41 passes", g.Admit(41));
    check("an old-generation frame (40) does not", !g.Admit(40));
    const auto dup = g.OnAck(true, 99, 7);
    check("a second answer for the same selection changes nothing",
          dup.result == SelectionAckGate::AckResult::Ignored && g.expected() == 41);
  }
  {
    SelectionAckGate g;
    g.Prepare(8);
    (void)g.Admit(40);  // only the old generation seen before the answer
    const auto ack = g.OnAck(true, 42, 8);
    check("no frame of the answered generation dropped: no extra IDR is asked for",
          ack.result == SelectionAckGate::AckResult::Applied && !ack.requestKeyframe);
    g.Prepare(9);
    const auto failed = g.OnAck(false, 0, 9);
    check("a failure answer leaves the gate shut (no frame admitted)",
          failed.result == SelectionAckGate::AckResult::Failed && !g.Admit(42) && !g.Admit(43));
    g.Reset();
    check("no selection armed: frames pass", g.Admit(5));
    const auto none = g.OnAck(true, 5, 0);
    check("an answer with nothing armed is ignored", none.result == SelectionAckGate::AckResult::Ignored);
  }
  {
    std::puts("\n--- the panel: a monitor answer is a monitor selection ---");
    WindowPanelStateModel panel;
    ControlWindowSelectedMessage ack{};
    ack.seq = 3;
    ack.flags = 0x1u;
    ack.windowId = encode_monitor_select_target(1);
    ack.streamGeneration = 12;
    std::snprintf(ack.title, sizeof(ack.title), "%s", "DISPLAY2");
    const auto r = panel.ApplyWindowSelected(ack);
    auto s = panel.Snapshot();
    check("ok, status window_selected (the APK's ack), selectedMonitorId 1, desktop mode (selectedId 0)",
          r.ok && s.status.rfind("window_selected", 0) == 0 && s.selectedMonitorId == 1 && s.selectedId == 0,
          s.status + " mon=" + std::to_string(s.selectedMonitorId) + " id=" + std::to_string(s.selectedId));
    check("the answer's target and generation are kept for the APK's log",
          s.lastSelectWindowId == encode_monitor_select_target(1) && s.lastSelectStreamGeneration == 12);
    ControlWindowListMessage list{};
    list.selectedWindowId = 0;
    list.itemCount = 0;
    panel.ApplyWindowList(list, 4);
    s = panel.Snapshot();
    check("a later window list (selectedWindowId 0) does not undo the monitor selection", s.selectedMonitorId == 1);
    ControlWindowSelectedMessage bad = ack;
    bad.windowId = 0x7000000100000001ULL;
    const auto rb = panel.ApplyWindowSelected(bad);
    check("an 'ok' answer naming a malformed target is not a success", !rb.ok &&
                                                                          panel.Snapshot().selectedMonitorId == 1);
  }

  {
    std::puts("\n--- r3 M4: a screen pick goes out only against the list it was made from ---");
    WindowPanelStateModel panel;
    auto list_of = [](uint32_t count, const char* second) {
      ControlMonitorListMessage m{};
      m.itemCount = count;
      for (uint32_t i = 0; i < count; ++i) {
        m.items[i].id = i;
        m.items[i].x = static_cast<int32_t>(i * 1920);
        m.items[i].width = 1920;
        m.items[i].height = 1080;
        std::snprintf(m.items[i].name, sizeof(m.items[i].name), "%s", i == 0 ? "DISPLAY1" : second);
      }
      return m;
    };
    panel.ApplyMonitorList(list_of(2, "DISPLAY2"));
    const uint64_t r1 = panel.Snapshot().monitorListRevision;
    panel.ApplyMonitorList(list_of(2, "DISPLAY2"));
    check("the same list fetched again keeps its revision", panel.Snapshot().monitorListRevision == r1);
    panel.ApplyMonitorList(list_of(2, "DISPLAY3"));
    const uint64_t r2 = panel.Snapshot().monitorListRevision;
    check("another screen at the same index is a new revision", r2 != r1);
    uint64_t id = 0;
    check("a pick made against the current list is sent",
          panel.RequestSelect(encode_monitor_select_target(1), "monitor_select_requested", 0, r2) &&
              panel.TakeSelectRequest(&id) && id == encode_monitor_select_target(1));
    check("a pick queued, then a different list arrives before it is sent: NOT sent, said so",
          panel.RequestSelect(encode_monitor_select_target(1), "monitor_select_requested", 0, r2) &&
              (panel.ApplyMonitorList(list_of(1, "")), !panel.TakeSelectRequest(&id)) &&
              panel.Snapshot().status == "window_select_failed: monitor_list_changed",
          panel.Snapshot().status);
    check("a window pick is not fenced by the monitor list",
          panel.RequestSelect(0x00000000000A0B2CULL, "window_select_requested", 0, r1) && panel.TakeSelectRequest(&id) &&
              id == 0x00000000000A0B2CULL);
  }

  {
    std::puts("\n--- r4 M1-A: the selection is owed its generation's IDR until one is admitted ---");
    SelectionAckGate g;
    g.Prepare(21);
    (void)g.Admit(60, true);  // the answered generation's IDR, before the answer: dropped
    const auto ack = g.OnAck(true, 60, 21);
    check("answered after its IDR was dropped: owed", ack.result == SelectionAckGate::AckResult::Applied && g.KeyOwed());
    check("a delta of that generation passes but does not pay it", g.Admit(60, false) && g.KeyOwed());
    check("an IDR of another generation is not admitted and does not pay it", !g.Admit(59, true) && g.KeyOwed());
    // r5 N1: passing the gate is not delivery -- the decoder may still drop it.
    check("an IDR of that generation PASSING the gate does not pay it", g.Admit(60, true) && g.KeyOwed());
    const uint64_t t1 = g.OwedToken();
    g.KeyLost(60);
    check("dropped by the decoder (no surface): still owed, same duty", g.KeyOwed() && g.OwedToken() == t1);
    g.Rearm();
    check("a surface arrives: still owed, the duty re-armed (new token)", g.KeyOwed() && g.OwedToken() != t1);
    g.KeyDelivered(59);
    check("another generation delivered does not pay it", g.KeyOwed());
    g.KeyDelivered(60);
    check("an IDR of that generation DELIVERED to the decoder pays it", !g.KeyOwed() && g.OwedToken() == 0);
    g.KeyLost(60);
    check("one of that generation lost afterwards owes one again (new duty)", g.KeyOwed() && g.OwedToken() != 0);
    g.KeyDelivered(60);
    g.Prepare(22);
    (void)g.Admit(61, true);
    (void)g.OnAck(true, 61, 22);
    g.Reset();
    check("abandoned (Reset): nothing owed", !g.KeyOwed());
    g.Prepare(23);
    (void)g.OnAck(true, 62, 23);
    check("its IDR not dropped before the answer: nothing owed", !g.KeyOwed());
  }
  {
    std::puts("\n--- r4 M2-A: an answer is shown only for the selection most recently requested ---");
    WindowPanelStateModel panel;
    ControlWindowSelectedMessage a{};
    a.seq = 1;
    a.flags = 0;  // A failed
    a.windowId = encode_monitor_select_target(0);
    std::snprintf(a.reason, sizeof(a.reason), "%s", "capture_restart_failed");
    panel.RequestSelect(encode_monitor_select_target(0), "monitor_select_requested", 31);
    uint64_t id = 0;
    (void)panel.TakeSelectRequest(&id);  // A went out
    panel.RequestSelect(encode_monitor_select_target(1), "monitor_select_requested", 32);  // then B was picked
    const auto r = panel.ApplyWindowSelectedFor(a, 31);
    check("A's late failure is not shown while B is the requested selection",
          !r.shown && panel.Snapshot().status == "monitor_select_requested", panel.Snapshot().status);
    ControlWindowSelectedMessage b = a;
    b.seq = 2;
    b.flags = 0x1u;
    b.windowId = encode_monitor_select_target(1);
    b.streamGeneration = 70;
    check("B's own answer is shown", panel.ApplyWindowSelectedFor(b, 32).shown &&
                                         panel.Snapshot().status.rfind("window_selected", 0) == 0);
    check("an untracked answer (tag 0) is shown as before", panel.ApplyWindowSelectedFor(a, 0).shown &&
                                                                panel.Snapshot().status.rfind("window_select_failed", 0) == 0);
  }

  if (gFailures == 0) {
    std::printf("\nRESULT: ALL PASS  (%d checks, 0 failed)\n", gChecks);
    return 0;
  }
  std::printf("\nRESULT: %d FAILED  (%d checks)\n", gFailures, gChecks);
  return 1;
}
