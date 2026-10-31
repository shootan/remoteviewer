import io

BS = chr(92)

# ================================================================== header
p = 'apps/native_poc/src/update_handoff.hpp'
d = io.open(p, encoding='utf-8').read()

old = '''std::wstring make_ready_event_name(uint32_t pid, uint64_t tick);'''
new = '''std::wstring make_ready_event_name(uint32_t pid, uint64_t tick);

/**
 * The acknowledgement event that belongs to a ready event.
 *
 * Derived from the ready name rather than passed separately, so the two cannot be given to
 * different attempts. One attempt, one stem, two events.
 *
 * The acknowledgement is what makes the handshake two-sided. Signalling ready tells the host the
 * download is verified; it does not tell the UPDATER that anyone heard. Without an answer the
 * updater goes on to stop the product on the assumption that someone is leaving, and if nobody
 * was listening -- the host gave up, the host was never there, the event had already been closed
 * -- it stops a product that nothing is going to bring back for a swap nobody agreed to.
 */
std::wstring make_ack_event_name(const std::wstring& readyEventName);

/** What the waiting caller should do NEXT, which is not always a final answer. */
enum class HandoffStep {
  /**
   * Nothing has been decided. Keep waiting.
   *
   * This is the case that did not exist, and its absence was the defect. The process the host
   * starts is a BOOTSTRAP: it copies the updater into a working directory, launches that copy and
   * exits immediately, because it cannot hold open the file the update has to replace. Its exit is
   * the normal, successful path -- and the host was treating it as the updater dying.
   *
   * So the host told the user "the installed version is unchanged and you can carry on", closed
   * the ready event, and stopped listening. The copy, meanwhile, was downloading perfectly well;
   * it then signalled into an event nobody held and stopped the very host that had just promised
   * nothing would happen. For a remote user that is being told the update was cancelled and then
   * losing the machine.
   */
  KeepWaiting,
  /** The updater has a verified download. Getting out of the way is now safe. */
  ExitNow,
  /** It is not going to happen this time. Carry on as though nothing had been attempted. */
  KeepRunning,
};

const char* handoff_step_name(HandoffStep step);

/**
 * What to do when the wait wakes up.
 *
 * `bootstrapExitCode` is only read when `bootstrapExited` is true. Zero means it handed over and
 * left on purpose; anything else means it failed before there was a worker to wait for, and there
 * is nothing more to wait for either.
 */
HandoffStep handoff_step(bool readySignalled, bool bootstrapExited, uint32_t bootstrapExitCode,
                         bool timedOut, std::string* detail);

/**
 * Whether the updater may go on to stop the product.
 *
 * Both halves are required. The signal has to have been DELIVERED -- an event that could not be
 * opened means nobody is waiting -- and it has to have been ANSWERED, because a host that has
 * already given up and told the user so must not then be shut down by a late arrival.
 *
 * Returning false here is not a failure of the update. Nothing has been touched at this point, so
 * the correct outcome is that no update happens and everything keeps running.
 */
bool may_stop_the_product(bool signalDelivered, bool acknowledged, std::string* detail);'''
assert old in d, 'ready name decl'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)

# ================================================================== implementation
p = 'apps/native_poc/src/update_handoff.cpp'
d = io.open(p, encoding='utf-8').read()

old = '''HandoffVerdict handoff_verdict(bool readySignalled, bool updaterExited, bool timedOut,'''
new = '''std::wstring make_ack_event_name(const std::wstring& readyEventName) {
  if (readyEventName.empty()) return {};
  return readyEventName + L".ack";
}

const char* handoff_step_name(HandoffStep step) {
  switch (step) {
    case HandoffStep::KeepWaiting: return "KeepWaiting";
    case HandoffStep::ExitNow: return "ExitNow";
    case HandoffStep::KeepRunning: return "KeepRunning";
  }
  return "?";
}

HandoffStep handoff_step(bool readySignalled, bool bootstrapExited, uint32_t bootstrapExitCode,
                         bool timedOut, std::string* detail) {
  const auto say = [detail](const char* text) {
    if (detail) *detail = text;
  };

  if (readySignalled) {
    // First, deliberately. A signal that arrived is decisive whatever else has happened since.
    say("the updater holds the lock and has a verified download");
    return HandoffStep::ExitNow;
  }
  if (bootstrapExited) {
    if (bootstrapExitCode == 0) {
      // The normal path, and the one that used to be read as death. The bootstrap's whole job is
      // to start the working copy and get out of the way -- it cannot stay, because the file it
      // is running from is one of the files the update replaces.
      say("the bootstrap handed over and exited, which is what it is meant to do -- still "
          "waiting for the working copy");
      return HandoffStep::KeepWaiting;
    }
    say("the bootstrap failed before it could hand over, so there is nothing to wait for");
    return HandoffStep::KeepRunning;
  }
  if (timedOut) {
    say("the updater did not reach a verified download in time");
    return HandoffStep::KeepRunning;
  }
  say("the updater did not signal");
  return HandoffStep::KeepRunning;
}

bool may_stop_the_product(bool signalDelivered, bool acknowledged, std::string* detail) {
  const auto say = [detail](const char* text) {
    if (detail) *detail = text;
  };
  if (!signalDelivered) {
    // The event could not be opened. Either nobody was ever waiting, or whoever was has closed it
    // and moved on -- and in both cases stopping the product would be stopping something that is
    // not expecting to be stopped and has nobody to bring it back.
    say("the ready signal could not be delivered -- nothing is waiting for this update");
    return false;
  }
  if (!acknowledged) {
    say("the ready signal was delivered but never answered -- whoever was waiting has given up");
    return false;
  }
  say("the waiting caller acknowledged and is standing down");
  return true;
}

HandoffVerdict handoff_verdict(bool readySignalled, bool updaterExited, bool timedOut,'''
assert old in d, 'verdict def'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('the handoff protocol has both sides now')
