# -*- coding: utf-8 -*-
import io

def edit(path, pairs):
    d = io.open(path, encoding='utf-8').read()
    for i, (old, new) in enumerate(pairs):
        assert d.count(old) == 1, '%s pair %d: %s' % (path, i, old[:60])
        d = d.replace(old, new, 1)
    io.open(path, 'w', encoding='utf-8', newline='').write(d)
    print('ok', path)


# ---------------------------------------------------------------- 1 and 2: the observe fetch
edit('apps/native_poc/src/directory_client.hpp', [
 ('''   * does not advertise yet may start later, so this retries with backoff and then stops asking.
   */
  bool FetchObserveEndpointFromHealth();''',
  '''   * does not advertise yet may start later, so this retries with backoff and then stops asking.
   *
   * The cooldown counts CYCLES of Run(), not seconds: 2, 4, 6 ... at the default heartbeat
   * interval of 25 s. Writing it as "2, 4, 6" without the unit reads like seconds and is out by
   * more than an order of magnitude.
   *
   * ⚠ LIMIT, stated rather than implied: after kMaxFetchAttempts the asking stops for the rest of
   * this process's life. A directory that is given an observe port LATER will not be picked up,
   * and the host has to be restarted to see it. That is a deliberate bound -- an omission on the
   * server should not turn into a machine that talks to it forever -- but it is not permanent
   * self-recovery, and it should not be described as such.
   */
  bool FetchObserveEndpointFromHealth();'''),
])

edit('apps/native_poc/src/directory_client.cpp', [
 ('''    observeFetchCooldown_ = observeFetchAttempts_ * 2;  // 2, 4, 6 ... cycles
''',
  '''    // Cycles of the loop below, not seconds: 2, 4, 6 ... at the default 25 s heartbeat interval.
    observeFetchCooldown_ = observeFetchAttempts_ * 2;
'''),
])

# ---------------------------------------------------------------- 3: the shutdown bound
edit('apps/native_poc/src/log_upload.hpp', [
 (''' * Worst case wait = kStopDrainBudgetMs + one in-flight http_post. On this tree the log path uses
 * directory_client's 6 s timeout on connect and on receive, so the bound is 3 s + up to ~12 s
 * against a host that accepts and never answers; against an unreachable one, 3 s + ~6 s. Called
 * with no uploader or UI lock held -- see LogUploadShutdown.''',
  ''' * Worst case wait = kStopDrainBudgetMs + one in-flight http_post. On this tree the log path uses
 * directory_client's 6 s timeout on connect and on receive, so the bound is 3 s + up to ~12 s
 * against a host that accepts and never answers; against an unreachable one, 3 s + ~6 s. Called
 * with no uploader or UI lock held -- see LogUploadShutdown.
 *
 * ⚠ The 5,997 ms in log_upload_shutdown_test is what THAT fixture measured -- a loopback server
 * that accepts and never answers. It is one point on the bound above, not a guarantee about every
 * https destination: a slow TLS handshake, a different timeout, or a proxy in the path all move
 * it. Quote it as a measurement of that case, never as the ceiling.''')
])
