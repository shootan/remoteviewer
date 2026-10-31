p = 'C:\\work\\gnlink-wt\\apk-prelogin-select\\apps\\native_poc\\src\\selection_ack_gate.hpp'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')

def rep(old, new):
    global s
    assert s.count(old) == 1, (s.count(old), old[:80])
    s = s.replace(old, new)

rep('''// It never opens on a frame before the answer.
class SelectionAckGate {''', '''// It never opens on a frame before the answer.
//
// apk-0232 r1 (Codex codex_rel_0163_e1_decision.md, the shared-session half): a LOCAL selection. A desktop pick at
// a host known to be at the logon screen (kCaptureFlagPreloginV1) gets no answer -- that host takes no WindowSelect
// and serves its one desktop. The same armed selection (same tag) then needs no ack, but keeps everything else:
// pending, the first-picture wait, the straggler filter after it. Its generation is BOUND by its first IDR that
// actually reaches the decoder (KeyDelivered of the key Admit let through as the candidate); until then it owes an
// IDR (the session asks, bounded), deltas are dropped, and an IDR the decoder could not take binds nothing and is
// owed again. A host answer never completes a local selection; a local selection never completes another's.
class SelectionAckGate {''')

rep('''  void Prepare(uint64_t selection) {
    pending_ = selection;
    awaiting_ = true;
    expected_ = 0;
    droppedCount_ = 0;
    keyOwed_ = false;
  }

  // An IDR of the answered generation reached the decoder (queued): the duty is paid.
  void KeyDelivered(uint64_t frameStreamGeneration) {
    if (expected_ != 0 && frameStreamGeneration == expected_) keyOwed_ = false;
  }

  // An IDR of the answered generation passed the gate but the decoder could not take it (no
  // surface, codec not configured, ...): the selection is owed one (again).
  void KeyLost(uint64_t frameStreamGeneration) {
    if (pending_ == 0 || expected_ == 0 || frameStreamGeneration != expected_ || keyOwed_) return;''',
    '''  void Prepare(uint64_t selection) {
    pending_ = selection;
    awaiting_ = true;
    expected_ = 0;
    droppedCount_ = 0;
    keyOwed_ = false;
    local_ = false;
    candidate_ = 0;
  }

  // apk-0232 r1: the armed selection `selection`, still waiting for its answer, continues as a LOCAL one (see the
  // class comment). Only that selection, only while it waits; false otherwise (a later pick replaced it, it was
  // answered, refused or abandoned) -- then nothing changes.
  bool BeginLocal(uint64_t selection) {
    if (selection == 0 || pending_ != selection || !awaiting_ || local_) return false;
    awaiting_ = false;
    local_ = true;
    expected_ = 0;
    candidate_ = 0;
    keyOwed_ = true;  // the first picture needs an IDR: asked for until one reaches the decoder
    ++owedToken_;
    return true;
  }

  // apk-0232 r1: the armed selection `selection` cannot be made (a window pick at the logon screen): it is over
  // as a failure -- nothing passes for it (like a failed answer). Only that selection, only while it waits.
  bool Refuse(uint64_t selection) {
    if (selection == 0 || pending_ != selection || !awaiting_) return false;
    awaiting_ = false;
    local_ = false;
    expected_ = 0;
    keyOwed_ = false;
    return true;
  }

  // The generation local selection `selection` was bound to, 0 while unbound (or not that local selection).
  uint64_t LocalBoundFor(uint64_t selection) const {
    return (selection != 0 && local_ && pending_ == selection) ? expected_ : 0;
  }

  // An IDR of the answered generation reached the decoder (queued): the duty is paid.
  // apk-0232 r1: for a local selection still unbound, the IDR Admit let through as its candidate binds it here.
  void KeyDelivered(uint64_t frameStreamGeneration) {
    if (local_ && pending_ != 0 && expected_ == 0) {
      if (candidate_ != 0 && frameStreamGeneration == candidate_) {
        expected_ = candidate_;
        keyOwed_ = false;
      }
      return;
    }
    if (expected_ != 0 && frameStreamGeneration == expected_) keyOwed_ = false;
  }

  // An IDR of the answered generation passed the gate but the decoder could not take it (no
  // surface, codec not configured, ...): the selection is owed one (again).
  void KeyLost(uint64_t frameStreamGeneration) {
    if (local_ && pending_ != 0 && expected_ == 0) {
      // apk-0232 r1: an unbound local selection's candidate the decoder did not take binds nothing.
      if (candidate_ == frameStreamGeneration) candidate_ = 0;
      if (!keyOwed_) {
        keyOwed_ = true;
        ++owedToken_;
      }
      return;
    }
    if (pending_ == 0 || expected_ == 0 || frameStreamGeneration != expected_ || keyOwed_) return;''')

rep('''  void Reset() {
    pending_ = 0;
    awaiting_ = false;
    expected_ = 0;
    droppedCount_ = 0;
    keyOwed_ = false;
  }

  Ack OnAck(bool ok, uint64_t streamGeneration, uint64_t requestTag) {
    if (pending_ == 0 || !awaiting_ || requestTag != pending_) return {AckResult::Ignored, false};''',
    '''  void Reset() {
    pending_ = 0;
    awaiting_ = false;
    expected_ = 0;
    droppedCount_ = 0;
    keyOwed_ = false;
    local_ = false;
    candidate_ = 0;
  }

  Ack OnAck(bool ok, uint64_t streamGeneration, uint64_t requestTag) {
    // A local selection waits for no answer (awaiting_ is false): a host's answer never completes it.
    if (pending_ == 0 || !awaiting_ || requestTag != pending_) return {AckResult::Ignored, false};''')

rep('''  bool Admit(uint64_t frameStreamGeneration, bool isKey = false) {
    (void)isKey;
    if (pending_ != 0 && awaiting_) {''',
    '''  bool Admit(uint64_t frameStreamGeneration, bool isKey = false) {
    if (pending_ != 0 && local_ && expected_ == 0) {
      // apk-0232 r1: an unbound local selection starts at an IDR. The one let through is its candidate; it binds
      // once it really reaches the decoder (KeyDelivered). A delta binds nothing and does not pass.
      if (!isKey || frameStreamGeneration == 0) return false;
      candidate_ = frameStreamGeneration;
      return true;
    }
    if (pending_ != 0 && awaiting_) {''')

rep('''  bool KeyOwed() const { return keyOwed_; }
  uint64_t pending() const { return pending_; }
  bool awaiting() const { return awaiting_; }
  uint64_t expected() const { return expected_; }''',
    '''  bool KeyOwed() const { return keyOwed_; }
  uint64_t pending() const { return pending_; }
  bool awaiting() const { return awaiting_; }
  uint64_t expected() const { return expected_; }
  bool local() const { return local_; }''')

rep('''  bool keyOwed_ = false;
  uint64_t owedToken_ = 0;
};''', '''  bool keyOwed_ = false;
  uint64_t owedToken_ = 0;
  bool local_ = false;      // apk-0232 r1: the armed selection needs no answer (see BeginLocal)
  uint64_t candidate_ = 0;  // ...and the IDR generation let through for it, not yet bound
};''')
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
