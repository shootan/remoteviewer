R = 'C:/work/gnlink-wt/feat-apk-0233/apps/native_poc/src/'


def edit(name, reps):
    p = R + name
    s = open(p, encoding='utf-8', newline='').read()
    crlf = '\r\n' in s
    if crlf:
        s = s.replace('\r\n', '\n')
    for a, b in reps:
        assert s.count(a) == 1, (name, a[:90], s.count(a))
        s = s.replace(a, b)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'w', encoding='utf-8', newline='').write(s)
    print('ok', name)


edit('client_mouse_xbutton_fence.hpp', [
    ('''#include <cstdint>
''', '''#include <algorithm>
#include <cstdint>
#include <vector>
'''),
    ('''// ends only when that up was sent and answered (OnSentAction after the ACK). To a host without X it never goes.
class ClientMouseXPressLedger {''', '''// ends only when that up was sent and answered (OnSentAction after the ACK). To a host without X it never goes.
//
// r4 (codex_apk_xfence_src_r3.md N1): a down admitted and QUEUED (not sent yet) whose up is then refused at the entry
// (no X by then) would leave the queue alone -- and if X came back before the sender took it, that down would go
// with no up after it. The down's queue sequence is noted (NoteQueued); an up refused at the entry cancels the
// still-unsent downs of its button by those sequences, and the sender drops a cancelled down (its held bit hidden
// until the next press). A down already taken by the sender is not cancelled: it is owed once answered, as before.
// A normal quick pair (down and up both queued) is untouched: its admitted up completes the pair.
class ClientMouseXPressLedger {'''),
    ('''        entryHeld_[s] = false;  // the button is up: every press of it is over at the entry
        entryRefused_[s] = false;''', '''        if (!ok) {  // r4: the downs of the button still waiting in the queue go with this refused up
          cancelled_[s].insert(cancelled_[s].end(), pending_[s].begin(), pending_[s].end());
          pending_[s].clear();
        }
        entryHeld_[s] = false;  // the button is up: every press of it is over at the entry
        entryRefused_[s] = false;'''),
    ('''  // The send side, right before an InputEvent leaves.
  bool AdmitSend(bool hostTakesX, uint16_t kind, uint32_t keyCode, uint16_t* buttons) {
    bool ok = true;
    const int s = Slot(keyCode);
    if (IsMouseXButtonEdge(kind, keyCode)) {
      if (kind == 2) {
        if (hostTakesX) {''', '''  // r4: an event admitted at the entry has been queued with this sequence (the controller calls it after Enqueue).
  void NoteQueued(uint16_t kind, uint32_t keyCode, uint32_t seq) {
    if (!IsMouseXButtonEdge(kind, keyCode)) return;
    const int s = Slot(keyCode);
    if (kind == 2) {
      pending_[s].push_back(seq);
    } else {
      pending_[s].clear();  // its up is queued behind it: the pair is whole
    }
  }

  // The send side, right before an InputEvent leaves. `seq`: the event's queue sequence.
  bool AdmitSend(bool hostTakesX, uint16_t kind, uint32_t keyCode, uint16_t* buttons, uint32_t seq = 0) {
    bool ok = true;
    const int s = Slot(keyCode);
    if (IsMouseXButtonEdge(kind, keyCode)) {
      if (kind == 2 && Take(&cancelled_[s], seq)) {
        dropped_[s] = true;  // r4: its up was refused at the entry -- the down does not go either
        ok = false;
      } else if (kind == 2) {
        Take(&pending_[s], seq);  // taken by the sender: no longer cancellable (owed once answered)
        if (hostTakesX) {'''),
    ('''    return AdmitSend(hostTakesX, a->inputEvent.kind, a->inputEvent.keyCode, &a->inputEvent.buttons);''',
     '''    return AdmitSend(hostTakesX, a->inputEvent.kind, a->inputEvent.keyCode, &a->inputEvent.buttons, a->inputEvent.seq);'''),
    ('''      entryHeld_[s] = entryRefused_[s] = dropped_[s] = owed_[s] = false;
    }''', '''      entryHeld_[s] = entryRefused_[s] = dropped_[s] = owed_[s] = false;
      pending_[s].clear();
      cancelled_[s].clear();
    }'''),
    ('''  bool SendOwed(uint32_t keyCode) const { return Slot(keyCode) >= 0 && owed_[Slot(keyCode)]; }''',
     '''  bool SendOwed(uint32_t keyCode) const { return Slot(keyCode) >= 0 && owed_[Slot(keyCode)]; }
  size_t PendingDowns(uint32_t keyCode) const { return Slot(keyCode) >= 0 ? pending_[Slot(keyCode)].size() : 0; }
  size_t CancelledDowns(uint32_t keyCode) const { return Slot(keyCode) >= 0 ? cancelled_[Slot(keyCode)].size() : 0; }'''),
    ('''  static int Slot(uint32_t keyCode) { return keyCode == 5 ? 0 : keyCode == 6 ? 1 : -1; }''',
     '''  static int Slot(uint32_t keyCode) { return keyCode == 5 ? 0 : keyCode == 6 ? 1 : -1; }
  static bool Take(std::vector<uint32_t>* v, uint32_t seq) {
    const auto it = std::find(v->begin(), v->end(), seq);
    if (it == v->end()) return false;
    v->erase(it);
    return true;
  }'''),
    ('''  bool owed_[2] = {false, false};          // a down the host received (sent + answered), its up not yet
};''', '''  bool owed_[2] = {false, false};          // a down the host received (sent + answered), its up not yet
  std::vector<uint32_t> pending_[2];       // r4: queued downs (their sequences) not taken by the sender yet
  std::vector<uint32_t> cancelled_[2];     // r4: queued downs whose up was refused at the entry: not to be sent
};'''),
])

edit('native_video_client_session.cpp', [
    ('''  inputQueue_.Enqueue(make_control_input_event(inputQueue_, kind, buttons, x, y, wheelDelta,
                                               keyCode, now_us()));
  return true;''', '''  const QueuedControlInputMessage queued =
      make_control_input_event(inputQueue_, kind, buttons, x, y, wheelDelta, keyCode, now_us());
  inputQueue_.Enqueue(queued);
  mouseXLedger_.NoteQueued(kind, keyCode, queued.inputEvent.seq);  // r4: a queued X down, by its sequence
  return true;'''),
])
