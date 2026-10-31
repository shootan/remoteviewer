# -*- coding: utf-8 -*-
import io

def edit(path, pairs):
    d = io.open(path, encoding='utf-8').read()
    for i, (old, new) in enumerate(pairs):
        assert d.count(old) == 1, '%s pair %d: %s' % (path, i, old[:60])
        d = d.replace(old, new, 1)
    io.open(path, 'w', encoding='utf-8', newline='').write(d)
    print('ok', path)

edit('apps/native_poc/src/directory_client.hpp', [
 ('''  bool ApplyObserveEndpoint();''',
  '''  bool ApplyObserveEndpoint();
  /**
   * Gets the observe endpoint from the directory when registration did not supply one.
   *
   * A host that starts with a cached token never registers, so it never saw the login response
   * that carries the advertisement -- and on https there is no default to fall back to, so it
   * could not observe, could not heartbeat, and never appeared in anyone's list. The cache
   * holding a perfectly good token was what made this permanent: the more successful the last
   * run, the more certainly the next one was stuck.
   *
   * Asked over the same origin's health route, which needs no session. Bounded: a server that
   * does not advertise yet may start later, so this retries with backoff and then stops asking.
   */
  bool FetchObserveEndpointFromHealth();'''),
 ('''  ObserveEndpoint observeAdvertised_{};
  bool observeAddrReady_ = false;''',
  '''  ObserveEndpoint observeAdvertised_{};
  bool observeAddrReady_ = false;
  /** How many times the health route has been asked, so the asking is bounded. */
  int observeFetchAttempts_ = 0;
  /** Cycles left to wait before asking again. Grows, so a silent server is not polled forever. */
  int observeFetchCooldown_ = 0;'''),
])

edit('apps/native_poc/src/directory_client.cpp', [
 # ---------------------------------------------------------------- the fetch itself
 ('''bool HostAgent::ApplyObserveEndpoint() {''',
  '''bool HostAgent::FetchObserveEndpointFromHealth() {
  // Only when there is nothing else to go on. A configured port is the operator's decision and an
  // advertisement already in hand does not need refreshing.
  if (cfg_.observeUdpPort != 0 || observeAdvertised_.known) return true;

  // Bounded, and deliberately not a poll. A directory that has not been given an observe port may
  // be given one later, so asking again is right; asking forever would turn a server-side
  // omission into a machine that talks to it every twenty-five seconds until someone notices.
  constexpr int kMaxFetchAttempts = 6;
  if (observeFetchCooldown_ > 0) {
    --observeFetchCooldown_;
    return false;
  }
  if (observeFetchAttempts_ >= kMaxFetchAttempts) return false;
  ++observeFetchAttempts_;

  ObserveEndpoint advertised;
  std::string error;
  if (!directory_observe_from_health(cfg_.url, &advertised, &error)) {
    // Absent is not an error -- an older directory says nothing, and observe_port_for has a rule
    // for that. Unreachable is an error, and the two read differently to whoever is looking.
    SetStatus(error.empty()
                  ? "this directory does not say where to send address observations"
                  : "could not ask the directory where observations go: " + error);
    observeFetchCooldown_ = observeFetchAttempts_ * 2;  // 2, 4, 6 ... cycles
    return false;
  }
  observeAdvertised_ = advertised;
  observeFetchAttempts_ = 0;
  observeFetchCooldown_ = 0;
  return ApplyObserveEndpoint();
}

bool HostAgent::ApplyObserveEndpoint() {'''),
 # ---------------------------------------------------------------- the loop asks before observing
 ('''    if (EnsureRegistered()) {
      // The observation must precede the heartbeat: the heartbeat is what publishes the
      // address, and it publishes whatever the observation last recorded.
      if (!RefreshObservedAddress()) {
        SetStatus("address observation timed out");
      } else {''',
  '''    if (EnsureRegistered()) {
      // A host that started from a cached token never registered, so nothing has told it where
      // observations go. Asked here rather than inside EnsureRegistered, because that function
      // returns immediately when a token is cached -- which is exactly the case that needs this.
      if (!observeAddrReady_) FetchObserveEndpointFromHealth();

      // The observation must precede the heartbeat: the heartbeat is what publishes the
      // address, and it publishes whatever the observation last recorded.
      if (!RefreshObservedAddress()) {
        // Only when there is nothing better to say. The specific reason -- no advertisement, an
        // unusable host, a port that cannot be derived -- was already set by whoever found it,
        // and overwriting it with "timed out" turned every one of those into a network symptom.
        if (observeAddrReady_) SetStatus("address observation timed out");
      } else {'''),
])
