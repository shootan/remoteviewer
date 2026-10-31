import io

BS = chr(92)

# ================================================================== deps
p = 'apps/native_poc/src/updater_effects.hpp'
d = io.open(p, encoding='utf-8').read()
old = '''  std::function<void(const std::wstring& eventName)> signalReady;'''
new = '''  /**
   * Tells whoever is waiting that the download is verified. Returns whether it was DELIVERED.
   *
   * The return value used to be void, and that was half of a defect: an updater whose signal went
   * nowhere carried on and stopped the product anyway. Nobody was waiting, so nobody was going to
   * bring it back.
   */
  std::function<bool(const std::wstring& eventName)> signalReady;

  /**
   * Waits for the acknowledgement that belongs to `readyEventName`. Returns whether it came.
   *
   * The other half. A signal that was delivered still does not mean anyone acted on it -- the
   * waiting caller may have already given up and told the user that no update would happen. It
   * must not then be shut down by an updater that arrived late, so the updater waits to be told
   * "yes, I am standing down" before it stops anything.
   */
  std::function<bool(const std::wstring& readyEventName, uint32_t timeoutMs)> awaitAck;'''
assert old in d, 'signalReady decl'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)

# ================================================================== production impls
p = 'apps/native_poc/src/updater_effects.cpp'
d = io.open(p, encoding='utf-8').read()
old = '''  deps.signalReady = [](const std::wstring& eventName) {
    if (eventName.empty()) return;
    HANDLE event = OpenEventW(EVENT_MODIFY_STATE, FALSE, eventName.c_str());
    if (!event) return;
    SetEvent(event);
    CloseHandle(event);
  };'''
new = '''  deps.signalReady = [](const std::wstring& eventName) {
    if (eventName.empty()) return false;
    HANDLE event = OpenEventW(EVENT_MODIFY_STATE, FALSE, eventName.c_str());
    // Could not be opened, so nobody holds it. That is not "signalled anyway" -- it is the
    // absence of anyone to signal, and the caller has to be able to tell the difference.
    if (!event) return false;
    const bool set = SetEvent(event) != FALSE;
    CloseHandle(event);
    return set;
  };
  deps.awaitAck = [](const std::wstring& readyEventName, uint32_t timeoutMs) {
    const std::wstring ackName = make_ack_event_name(readyEventName);
    if (ackName.empty()) return false;
    HANDLE event = OpenEventW(SYNCHRONIZE, FALSE, ackName.c_str());
    if (!event) return false;
    const bool acked = WaitForSingleObject(event, timeoutMs) == WAIT_OBJECT_0;
    CloseHandle(event);
    return acked;
  };'''
assert old in d, 'signalReady impl'
d = d.replace(old, new, 1)

old = '''  // signalReady may be absent: an update nobody is waiting on is an ordinary case.
  return true;'''
new = '''  // signalReady and awaitAck may be absent: an update nobody is waiting on is an ordinary case,
  // and then there is no handshake to complete. What must not happen is one without the other --
  // signalling with no way to hear the answer is exactly the shape of the defect they exist for.
  if (static_cast<bool>(signalReady) != static_cast<bool>(awaitAck)) {
    return fail("signalReady and awaitAck must be supplied together");
  }
  return true;'''
assert old in d, 'validate tail'
d = d.replace(old, new, 1)

# ================================================================== the decorator
old = '''                 std::function<void()> captureTargets,
                 std::function<void(const std::wstring&)> signalReady,
                 WindowsUpdateEffects* concrete, std::function<void(const std::string&)> log)'''
new = '''                 std::function<void()> captureTargets,
                 std::function<bool(const std::wstring&)> signalReady,
                 std::function<bool(const std::wstring&, uint32_t)> awaitAck,
                 WindowsUpdateEffects* concrete, std::function<void(const std::string&)> log)'''
assert old in d, 'ctor sig'
d = d.replace(old, new, 1)

old = '''        signalReady_(std::move(signalReady)),'''
new = '''        signalReady_(std::move(signalReady)),
        awaitAck_(std::move(awaitAck)),'''
assert old in d, 'ctor init'
d = d.replace(old, new, 1)

old = '''      if (captureTargets_) captureTargets_();  // before anyone is told they may leave
      if (signalReady_) signalReady_(eventName_);'''
new = '''      if (captureTargets_) captureTargets_();  // before anyone is told they may leave
      // Recorded, not discarded. Whether anyone received this decides whether the product may be
      // stopped, and that decision is made at PrepareForSwap -- the last point before anything
      // is touched.
      signalDelivered_ = signalReady_ ? signalReady_(eventName_) : false;
      if (!signalDelivered_ && log_ && !eventName_.empty()) {
        log_("the ready signal could not be delivered -- nothing is waiting for this update");
      }'''
assert old in d, 'verify body'
d = d.replace(old, new, 1)

old = '''  bool PrepareForSwap() override { return inner_.PrepareForSwap(); }'''
new = '''  bool PrepareForSwap() override {
    // The gate, and it sits here because this is the last step before anything is stopped or
    // moved. Failing here abandons the attempt with the disk untouched, which is the correct
    // outcome of a handover nobody completed.
    if (!eventName_.empty()) {
      const bool acked = awaitAck_ ? awaitAck_(eventName_, kAckTimeoutMs) : false;
      std::string why;
      if (!may_stop_the_product(signalDelivered_, acked, &why)) {
        if (log_) log_("not going ahead: " + why);
        return false;
      }
      if (log_) log_(why);
    }
    return inner_.PrepareForSwap();
  }'''
assert old in d, 'PrepareForSwap'
d = d.replace(old, new, 1)

old = '''  bool note(const char* step, bool ok) {'''
new = '''  /**
   * How long to wait for the acknowledgement.
   *
   * Generous, because the answer comes from a UI thread that may be busy, and short enough that
   * an update does not sit indefinitely against a caller that has gone. Timing out here costs
   * nothing: nothing has been touched.
   */
  static constexpr uint32_t kAckTimeoutMs = 30000;

  bool note(const char* step, bool ok) {'''
assert old in d, 'note'
d = d.replace(old, new, 1)

old = '''  std::function<void(const std::wstring&)> signalReady_;'''
new = '''  std::function<bool(const std::wstring&)> signalReady_;
  std::function<bool(const std::wstring&, uint32_t)> awaitAck_;
  bool signalDelivered_ = false;'''
assert old in d, 'member'
d = d.replace(old, new, 1)

old = '''                           options_.installedVersion, captureNow, deps_.signalReady, &effects,
                           deps_.log);'''
new = '''                           options_.installedVersion, captureNow, deps_.signalReady,
                           deps_.awaitAck, &effects, deps_.log);'''
assert old in d, 'construction'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('the updater waits to be answered before it stops anything')
