import io

def edit(path, pairs):
    d = io.open(path, encoding='utf-8').read()
    for i, (old, new) in enumerate(pairs):
        assert d.count(old) == 1, '%s pair %d' % (path, i)
        d = d.replace(old, new, 1)
    io.open(path, 'w', encoding='utf-8', newline='').write(d)
    print('ok', path)

# ================================================================ client: one snapshot, one lock
edit('apps/native_poc/src/client_shell_main.cpp', [
 ('''      std::lock_guard<std::mutex> lock(gStateMu);
      gSessionToken.clear();''',
  '''      std::lock_guard<std::mutex> lock(gStateMu);
      gSessionToken.clear();
      // Signing out is an owner change. Without this the account name and the server address are
      // unchanged and the epoch is unchanged, so an attempt started while signed in would still
      // compare as the same owner -- and would be acknowledged after the user had left.
      ++gOwnerEpoch;'''),
 ('''/**
 * The directory this shell is signed in to, or the one it last remembered.''',
  '''/**
 * Everything an update attempt is judged against, read once under one lock.
 *
 * Read separately, these four can disagree: the session can be cleared between reading the
 * account and reading the epoch, and the answer is then a mixture of two owners that never
 * existed at the same time. One lock, one snapshot.
 */
struct OwnerSnapshot {
  std::string session;
  std::string account;
  std::string serverUrl;
  uint64_t epoch = 0;
};

/**
 * The directory this shell is signed in to, or the one it last remembered.'''),
])

# ================================================================ host: a lock and a snapshot
edit('apps/native_poc/src/host_app_main.cpp', [
 ('''  uint64_t ownerEpoch = 0;''',
  '''  uint64_t ownerEpoch = 0;
  /**
   * Guards `cache` and `ownerEpoch` against the background threads that read them.
   *
   * The update handover reads the account, the machine id, the server address, the token and the
   * epoch from a thread that is not the UI thread, while signing in and signing out write those
   * same fields. std::string is not safe to read while another thread assigns it, so this was a
   * data race -- undefined behaviour, not merely a stale value.
   */
  std::mutex ownerMu;'''),
 ('''void sign_out(bool keepAccount) {
  g.streaming.Stop();
  g.cache.hostToken.clear();
  g.cache.hostId.clear();''',
  '''void sign_out(bool keepAccount) {
  g.streaming.Stop();
  {
    std::lock_guard<std::mutex> lock(g.ownerMu);
    g.cache.hostToken.clear();
    g.cache.hostId.clear();
    // Signing out is an owner change. Without this the account and the machine id are unchanged
    // and the epoch is unchanged, so `same_owner_as` stays true and an attempt authorised while
    // signed in would still be acknowledged -- after the user had signed out.
    ++g.ownerEpoch;
  }'''),
 ('''        g.cache.hostToken = result->hostToken;
        ++g.ownerEpoch;''',
  '''        {
          std::lock_guard<std::mutex> lock(g.ownerMu);
          g.cache.hostToken = result->hostToken;
          ++g.ownerEpoch;
        }'''),
])
