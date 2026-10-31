import io

BS = chr(92)

# ================================================================== deps
p = 'apps/native_poc/src/updater_effects.hpp'
d = io.open(p, encoding='utf-8').read()
old = '''  std::function<bool(const std::wstring& readyEventName, uint32_t timeoutMs)> awaitAck;'''
new = '''  /**
   * Opens the acknowledgement channel and hands back a handle to HOLD. Null when it cannot.
   *
   * Opened BEFORE the ready signal goes out, and that order is the whole point. The caller
   * creates the channel and closes it the moment it has answered -- so if the updater waits until
   * it needs the answer before opening it, the last handle can be gone by then and the named
   * object with it. The open fails, the answer looks absent, and the update stands down having
   * already told the caller to leave.
   *
   * Holding it from before the signal until after the answer keeps the object alive across the
   * window where both sides are letting go.
   */
  std::function<void*(const std::wstring& readyEventName)> openAck;

  /** Waits on a handle from `openAck`. Returns whether the answer came. */
  std::function<bool(void* ackHandle, uint32_t timeoutMs)> awaitAck;

  /** Releases a handle from `openAck`. */
  std::function<void(void* ackHandle)> closeAck;'''
assert old in d, 'awaitAck decl'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)

# ================================================================== production
p = 'apps/native_poc/src/updater_effects.cpp'
d = io.open(p, encoding='utf-8').read()
old = '''  deps.awaitAck = [](const std::wstring& readyEventName, uint32_t timeoutMs) {
    const std::wstring ackName = make_ack_event_name(readyEventName);
    if (ackName.empty()) return false;
    HANDLE event = OpenEventW(SYNCHRONIZE, FALSE, ackName.c_str());
    if (!event) return false;
    const bool acked = WaitForSingleObject(event, timeoutMs) == WAIT_OBJECT_0;
    CloseHandle(event);
    return acked;
  };'''
new = '''  deps.openAck = [](const std::wstring& readyEventName) -> void* {
    const std::wstring ackName = make_ack_event_name(readyEventName);
    if (ackName.empty()) return nullptr;
    return OpenEventW(SYNCHRONIZE, FALSE, ackName.c_str());
  };
  deps.awaitAck = [](void* ackHandle, uint32_t timeoutMs) {
    if (!ackHandle) return false;
    return WaitForSingleObject(static_cast<HANDLE>(ackHandle), timeoutMs) == WAIT_OBJECT_0;
  };
  deps.closeAck = [](void* ackHandle) {
    if (ackHandle) CloseHandle(static_cast<HANDLE>(ackHandle));
  };'''
assert old in d, 'awaitAck impl'
d = d.replace(old, new, 1)

old = '''  if (static_cast<bool>(signalReady) != static_cast<bool>(awaitAck)) {
    return fail("signalReady and awaitAck must be supplied together");
  }'''
new = '''  const int handshakeParts = static_cast<int>(static_cast<bool>(signalReady)) +
                             static_cast<int>(static_cast<bool>(openAck)) +
                             static_cast<int>(static_cast<bool>(awaitAck)) +
                             static_cast<int>(static_cast<bool>(closeAck));
  if (handshakeParts != 0 && handshakeParts != 4) {
    // All or none. A partial handshake is the shape of the defect they exist for: signalling with
    // no way to hear the answer, or opening a channel with no way to hold it.
    return fail("the handshake seams must be supplied together: signalReady, openAck, awaitAck, "
                "closeAck");
  }'''
assert old in d, 'validate pair'
d = d.replace(old, new, 1)

# ---------------------------------------------------------------- the decorator
old = '''                 std::function<bool(const std::wstring&)> signalReady,
                 std::function<bool(const std::wstring&, uint32_t)> awaitAck,
                 WindowsUpdateEffects* concrete, std::function<void(const std::string&)> log)'''
new = '''                 std::function<bool(const std::wstring&)> signalReady,
                 std::function<void*(const std::wstring&)> openAck,
                 std::function<bool(void*, uint32_t)> awaitAck,
                 std::function<void(void*)> closeAck, WindowsUpdateEffects* concrete,
                 std::function<void(const std::string&)> log)'''
assert old in d, 'ctor sig'
d = d.replace(old, new, 1)

old = '''        awaitAck_(std::move(awaitAck)),'''
new = '''        openAck_(std::move(openAck)),
        awaitAck_(std::move(awaitAck)),
        closeAck_(std::move(closeAck)),'''
assert old in d, 'ctor init'
d = d.replace(old, new, 1)

old = '''      signalDelivered_ = signalReady_ ? signalReady_(eventName_) : false;
      if (!signalDelivered_ && log_ && !eventName_.empty()) {
        log_("the ready signal could not be delivered -- nothing is waiting for this update");
      }'''
new = '''      // The channel is opened FIRST and held. Signalling before holding it leaves a window in
      // which the caller answers, closes its handle, and the named object goes with it -- so the
      // answer that was delivered cannot be found, and this stands down after having already told
      // the caller to leave.
      if (!eventName_.empty()) {
        ackHandle_ = openAck_ ? openAck_(eventName_) : nullptr;
        if (!ackHandle_) {
          // No channel means no way to be answered. Not signalling is the safe half of that: the
          // caller keeps running because it was never told to go.
          if (log_) {
            log_("the acknowledgement channel could not be opened, so nothing was signalled -- "
                 "no update this time");
          }
          signalDelivered_ = false;
          return ok;
        }
      }
      signalDelivered_ = signalReady_ ? signalReady_(eventName_) : false;
      if (!signalDelivered_ && log_ && !eventName_.empty()) {
        log_("the ready signal could not be delivered -- nothing is waiting for this update");
      }'''
assert old in d, 'verify body'
d = d.replace(old, new, 1)

old = '''      const bool acked = awaitAck_ ? awaitAck_(eventName_, kAckTimeoutMs) : false;
      std::string why;
      if (!may_stop_the_product(signalDelivered_, acked, &why)) {
        if (log_) log_("not going ahead: " + why);
        return false;
      }
      if (log_) log_(why);'''
new = '''      // Waited on the handle held since before the signal -- not opened here, which is where the
      // window was.
      const bool acked = (awaitAck_ && ackHandle_) ? awaitAck_(ackHandle_, kAckTimeoutMs) : false;
      std::string why;
      const bool proceed = may_stop_the_product(signalDelivered_, acked, &why);
      release_ack();
      if (!proceed) {
        if (log_) log_("not going ahead: " + why);
        return false;
      }
      if (log_) log_(why);'''
assert old in d, 'prepare body'
d = d.replace(old, new, 1)

old = '''  std::function<bool(const std::wstring&)> signalReady_;
  std::function<bool(const std::wstring&, uint32_t)> awaitAck_;
  bool signalDelivered_ = false;'''
new = '''  std::function<bool(const std::wstring&)> signalReady_;
  std::function<void*(const std::wstring&)> openAck_;
  std::function<bool(void*, uint32_t)> awaitAck_;
  std::function<void(void*)> closeAck_;
  void* ackHandle_ = nullptr;
  bool signalDelivered_ = false;

  /** Releases the channel once, whenever this ends -- answered, refused or abandoned. */
  void release_ack() {
    if (ackHandle_ && closeAck_) closeAck_(ackHandle_);
    ackHandle_ = nullptr;
  }'''
assert old in d, 'members'
d = d.replace(old, new, 1)

old = '''  bool AcquireLock() override { return inner_.AcquireLock(); }'''
new = '''  ~ReadySignaller() { release_ack(); }

  bool AcquireLock() override { return inner_.AcquireLock(); }'''
assert old in d, 'AcquireLock'
d = d.replace(old, new, 1)

old = '''                           options_.installedVersion, captureNow, deps_.signalReady,
                           deps_.awaitAck, &effects, deps_.log);'''
new = '''                           options_.installedVersion, captureNow, deps_.signalReady,
                           deps_.openAck, deps_.awaitAck, deps_.closeAck, &effects, deps_.log);'''
assert old in d, 'construction'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('the worker holds the channel from before the signal until after the answer')
