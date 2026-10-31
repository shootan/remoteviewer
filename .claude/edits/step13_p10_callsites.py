import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit

ROOT = r'D:\remote\remote'
S = ROOT + r'\apps\native_poc\src'

# ---------------- CMake: new test target ----------------
edit(ROOT + r'\apps\native_poc\CMakeLists.txt', [
(r"""# The thing a user installs: sign-in window plus tray, supervising the streaming host.
add_executable(remote60_host_app WIN32""",
 r"""# The log uploader's credential lifecycle (P10): token replaced / identity changed / 401 pause /
# bounded retry / permanent discard, against an in-process fake directory on 127.0.0.1.
add_executable(remote60_log_upload_test
  src/log_upload.cpp
  src/directory_client.cpp
  src/log_upload_test.cpp
)
target_include_directories(remote60_log_upload_test PRIVATE src)
target_link_libraries(remote60_log_upload_test PRIVATE common ws2_32 iphlpapi shell32)

# The thing a user installs: sign-in window plus tray, supervising the streaming host.
add_executable(remote60_host_app WIN32"""),
])

# ---------------- host shell ----------------
edit(S + r'\host_app_main.cpp', [
(r"""/**
 * Hands this machine's log to the directory once there is a token to authenticate with.
 *
 * Called from both places a token appears -- a fresh sign-in and a cache loaded at startup --
 * because a host that was already signed in never signs in again, and that is the common case.
 * Starting twice is harmless; the uploader returns early when it is already running.
 */
void start_log_upload() {
  remote60::native_poc::LogUploadConfig upload;
  upload.directoryUrl = g.cache.directoryUrl;
  upload.hostToken = g.cache.hostToken;
  upload.device = g.cache.machineId;
  std::string reason;
  const bool on = remote60::native_poc::log_upload_start(upload, &reason);
  std::printf("[gnlink-host] log upload %s %s\n", on ? "on" : "off", reason.c_str());
}""",
 r"""/**
 * Hands this machine's log to the directory with the token it holds right now.
 *
 * Called from both places a token appears -- a fresh sign-in and a cache loaded at startup.
 * The second call is not a no-op: a sign-in re-registers the host, and the server retires the
 * previous token that instant (server.js handleHostRegister), so an uploader still holding it
 * gets 401 for the rest of the process's life -- which is what emptied the NAS of host logs on
 * 2026-09-07 (P10). The uploader is re-pointed; what it queued under the same account stays.
 */
void start_log_upload() {
  remote60::native_poc::LogUploadConfig upload;
  upload.directoryUrl = g.cache.directoryUrl;
  upload.hostToken = g.cache.hostToken;
  upload.device = g.cache.machineId;
  upload.identity = g.cache.accountId + "/" + g.cache.machineId;
  std::string reason;
  const bool on = remote60::native_poc::log_upload_configure(upload, &reason);
  std::printf("[gnlink-host] log upload %s %s\n", on ? "on" : "off", reason.c_str());
}"""),
(r"""  g.cache.hostToken.clear();
  g.cache.hostId.clear();
  (void)directory::save_host_cache(g.cachePath, g.cache);
  apply_signed_in_ui(false);""",
 r"""  g.cache.hostToken.clear();
  g.cache.hostId.clear();
  (void)directory::save_host_cache(g.cachePath, g.cache);
  // The token is gone; so is the owner of whatever the uploader still holds.
  remote60::native_poc::log_upload_clear_credentials("signed out");
  apply_signed_in_ui(false);"""),
(r"""  const uint32_t restarts = g.streaming.Restarts();
  if (restarts > 0) detail += L"\nStreaming host restarts: " + std::to_wstring(restarts);
  set_status(detail);""",
 r"""  const uint32_t restarts = g.streaming.Restarts();
  if (restarts > 0) detail += L"\nStreaming host restarts: " + std::to_wstring(restarts);
  // The uploader learns about a retired token before the heartbeat does (it posts every few
  // seconds); a pause there is the same "sign in again" and must not stay invisible.
  if (remote60::native_poc::log_upload_status().authRejected) {
    detail += L"\nLog upload: the server rejected this PC's token. Use Change account to sign in again.";
  }
  set_status(detail);"""),
])

# ---------------- client shell ----------------
edit(S + r'\client_shell_main.cpp', [
(r"""    {
      // Now that a token exists the shell can hand its logs to the directory, which is the only
      // way the phone's and this machine's logs ever sit side by side.
      remote60::native_poc::LogUploadConfig upload;
      upload.directoryUrl = server;
      upload.sessionToken = token;
      std::string reason;
      log_line(remote60::native_poc::log_upload_start(upload, &reason)
                   ? "log upload on " + reason
                   : "log upload off: " + reason);
    }""",
 r"""    {
      // Now that a token exists the shell can hand its logs to the directory, which is the only
      // way the phone's and this machine's logs ever sit side by side. A second login re-points
      // the uploader (a restarted server forgets every session; the old token then gets 401),
      // and a login as a different account discards what was queued under the previous one.
      remote60::native_poc::log_upload_set_auth_rejected_callback([] {
        log_line("log upload: the server rejected the session token; sign in again to resume");
        post_status("error", "로그 업로드 인증이 만료됐습니다. 다시 로그인하면 재개됩니다.");
      });
      remote60::native_poc::LogUploadConfig upload;
      upload.directoryUrl = server;
      upload.sessionToken = token;
      upload.identity = accountId + "@" + server;
      std::string reason;
      log_line(remote60::native_poc::log_upload_configure(upload, &reason)
                   ? "log upload on " + reason
                   : "log upload off: " + reason);
    }"""),
(r"""  if (type == "logout") {
    {
      std::lock_guard<std::mutex> lock(gStateMu);
      gSessionToken.clear();
    }
    post_to_page("{\"type\":\"signedOut\"}");""",
 r"""  if (type == "logout") {
    {
      std::lock_guard<std::mutex> lock(gStateMu);
      gSessionToken.clear();
    }
    remote60::native_poc::log_upload_clear_credentials("logged out");
    post_to_page("{\"type\":\"signedOut\"}");"""),
])

# ---------------- directory server: sessions survive a restart ----------------
J = ROOT + r'\apps\directory\server.js'
edit(J, [
(r"""/** @type {{accounts: Object, hosts: Object}} */
let store = { accounts: {}, hosts: {} };

function loadStore() {
  try {
    store = JSON.parse(fs.readFileSync(DATA_PATH, 'utf8'));
    if (!store.accounts) store.accounts = {};
    if (!store.hosts) store.hosts = {};
  } catch (err) {
    if (err.code !== 'ENOENT') console.error('[directory] store read failed:', err.message);
    store = { accounts: {}, hosts: {} };
  }
  indexHostTokens();
}""",
 r"""/** @type {{accounts: Object, hosts: Object, sessions: Object}} */
let store = { accounts: {}, hosts: {}, sessions: {} };

function loadStore() {
  try {
    store = JSON.parse(fs.readFileSync(DATA_PATH, 'utf8'));
    if (!store.accounts) store.accounts = {};
    if (!store.hosts) store.hosts = {};
    if (!store.sessions) store.sessions = {};
  } catch (err) {
    if (err.code !== 'ENOENT') console.error('[directory] store read failed:', err.message);
    store = { accounts: {}, hosts: {}, sessions: {} };
  }
  indexHostTokens();
  indexSessions();
}"""),
(r"""function indexHostTokens() {
  hostTokens.clear();
  for (const host of Object.values(store.hosts)) {
    if (host.tokenHash) hostTokens.set(host.tokenHash, host.hostId);
  }
}""",
 r"""function indexHostTokens() {
  hostTokens.clear();
  for (const host of Object.values(store.hosts)) {
    if (host.tokenHash) hostTokens.set(host.tokenHash, host.hostId);
  }
}

/**
 * Sessions survive a restart too (P10, 2026-09-07). They were purely in memory, so a deploy
 * signed every client out at once: the client shell keeps no password, its log uploader kept
 * posting with the dead token, and the office viewer's log never reached the NAS again. As with
 * host tokens only the hash is stored; the 12 h TTL is unchanged and enforced on load.
 */
function indexSessions() {
  sessions.clear();
  const now = Date.now();
  let dropped = 0;
  for (const [hash, s] of Object.entries(store.sessions)) {
    if (s && s.accountId && s.expiresAt > now) sessions.set(hash, { accountId: s.accountId, expiresAt: s.expiresAt });
    else { delete store.sessions[hash]; dropped++; }
  }
  if (dropped) saveStoreSoon();
}"""),
(r"""const sessions = new Map();      // sessionToken -> {accountId, expiresAt}""",
 r"""const sessions = new Map();      // sha256(sessionToken) -> {accountId, expiresAt}, mirrored in store.sessions"""),
(r"""  for (const [token, s] of sessions) if (s.expiresAt <= now) sessions.delete(token);""",
 r"""  let expired = 0;
  for (const [hash, s] of sessions) {
    if (s.expiresAt <= now) {
      sessions.delete(hash);
      delete store.sessions[hash];
      expired++;
    }
  }
  if (expired) saveStoreSoon();"""),
(r"""  const s = sessions.get(token);
  if (!s || s.expiresAt <= Date.now()) return null;
  return s;""",
 r"""  const s = sessions.get(hashToken(token));
  if (!s || s.expiresAt <= Date.now()) return null;
  return s;"""),
(r"""  const token = randomToken();
  const expiresAt = Date.now() + SESSION_TTL_MS;
  sessions.set(token, { accountId: id, expiresAt });
  sendJson(res, 200, { sessionToken: token, expiresAt });""",
 r"""  const token = randomToken();
  const expiresAt = Date.now() + SESSION_TTL_MS;
  const session = { accountId: id, expiresAt };
  sessions.set(hashToken(token), session);
  store.sessions[hashToken(token)] = session;
  saveStoreSoon();
  sendJson(res, 200, { sessionToken: token, expiresAt });"""),
])

# ---------------- server tests ----------------
edit(ROOT + r'\apps\directory\test\logs_test.js', [
(r"""  check('another device is unaffected by that budget', other.status === 200, `status=${other.status}`);

  console.log(failures === 0 ? '\nlogs_test: PASS' : `\nlogs_test: ${failures} FAILED`);""",
 r"""  check('another device is unaffected by that budget', other.status === 200, `status=${other.status}`);

  // 7. a host token authenticates too: the host app has no session, only its registration
  const hostReg = { id: 'tester', pw: 'test-pass-1234', hostName: 'Log PC', machineId: 'machine-logs' };
  const reg = await request('POST', '/api/host/register', { body: hostReg });
  const hostToken = reg.body && reg.body.hostToken;
  check('a host registers for an upload token', reg.status === 200 && !!hostToken, `status=${reg.status}`);
  const viaHost = await request('POST', '/api/logs', {
    body: 'host line\n',
    headers: { 'x-host-token': hostToken, 'x-log-device': 'pc1', 'x-log-stream': 'host' },
  });
  check('a host token stores the batch', viaHost.status === 200, `status=${viaHost.status}`);
  const hostFile = path.join(LOG_DIR, 'tester', 'pc1', 'host.log');
  check('host lines land under the host\'s account',
        fs.existsSync(hostFile) && fs.readFileSync(hostFile, 'utf8').includes('host line'), hostFile);

  // 8. re-registering retires the previous token for uploads as well -- the uploader has to be
  //    re-pointed at the new one (P10, 2026-09-07: four minutes of the old token = 401 for 2 hours)
  const reg2 = await request('POST', '/api/host/register', { body: hostReg });
  const rotated = reg2.body && reg2.body.hostToken;
  check('re-registration issues a different token', reg2.status === 200 && rotated && rotated !== hostToken);
  const oldAfter = await request('POST', '/api/logs', {
    body: 'stale\n',
    headers: { 'x-host-token': hostToken, 'x-log-device': 'pc1', 'x-log-stream': 'host' },
  });
  check('the retired host token is refused for uploads', oldAfter.status === 401, `status=${oldAfter.status}`);
  const newAfter = await request('POST', '/api/logs', {
    body: 'fresh\n',
    headers: { 'x-host-token': rotated, 'x-log-device': 'pc1', 'x-log-stream': 'host' },
  });
  check('the rotated host token is accepted', newAfter.status === 200, `status=${newAfter.status}`);

  // 9. a session the server does not know (restarted before persistence, expired, made up)
  const stale = await request('POST', '/api/logs', {
    body: 'x\n',
    headers: { authorization: 'Bearer not-a-session', 'x-log-device': 'dev1', 'x-log-stream': 'viewer' },
  });
  check('an unknown session token is refused', stale.status === 401, `status=${stale.status}`);

  console.log(failures === 0 ? '\nlogs_test: PASS' : `\nlogs_test: ${failures} FAILED`);"""),
])

edit(ROOT + r'\apps\directory\test\restart_test.js', [
(r"""// A host must stay signed in across a server restart.
//
// Host tokens once lived only in memory, so every deploy or reboot quietly invalidated them.
// Each PC would then be told its token was unknown, and since the host app deliberately does
// not keep the password, someone had to walk to the machine and sign in again. This runs in two
// phases around a restart performed by the runner.""",
 r"""// A host -- and a signed-in client -- must stay signed in across a server restart.
//
// Host tokens once lived only in memory, so every deploy or reboot quietly invalidated them.
// Each PC would then be told its token was unknown, and since the host app deliberately does
// not keep the password, someone had to walk to the machine and sign in again. Sessions had the
// same flaw until 2026-09-07 (P10): a restart signed every client out, and the client shell,
// which keeps no password either, kept uploading its log with a dead token. This runs in two
// phases around a restart performed by the runner; the register phase hands the runner
// "hostToken:sessionToken"."""),
(r"""    // Handed to the runner, which restarts the server and passes it back.
    process.stdout.write(r.body.hostToken);
    process.exit(0);""",
 r"""    const login = await api('POST', '/api/login', { id: 'tester', pw: 'test-pass-1234' });
    if (login.status !== 200 || !login.body.sessionToken) {
      console.log(`FAIL  client logs in before restart  status=${login.status}`);
      process.exit(1);
    }
    // Handed to the runner, which restarts the server and passes it back.
    process.stdout.write(r.body.hostToken + ':' + login.body.sessionToken);
    process.exit(0);"""),
(r"""    const token = process.argv[3] || '';
    let failures = 0;

    let r = await api('POST', '/api/host/heartbeat', { hostToken: token });""",
 r"""    const handed = (process.argv[3] || '').split(':');
    const token = handed[0] || '';
    const sessionToken = handed[1] || '';
    let failures = 0;

    let r = await api('POST', '/api/host/heartbeat', { hostToken: token });"""),
(r"""    const plaintextAbsent = !raw.includes(token);
    console.log(`${plaintextAbsent ? 'PASS' : 'FAIL'}  store keeps only the token hash`);
    if (!plaintextAbsent) failures++;""",
 r"""    const plaintextAbsent = !raw.includes(token) && !raw.includes(sessionToken);
    console.log(`${plaintextAbsent ? 'PASS' : 'FAIL'}  store keeps only the token hashes`);
    if (!plaintextAbsent) failures++;

    r = await api('GET', '/api/hosts', null, sessionToken);
    const sessionSurvived = r.status === 200;
    console.log(`${sessionSurvived ? 'PASS' : 'FAIL'}  client session survives a server restart  status=${r.status}`);
    if (!sessionSurvived) failures++;

    r = await api('GET', '/api/hosts', null, 'not-a-real-session');
    const sessionRejected = r.status === 401;
    console.log(`${sessionRejected ? 'PASS' : 'FAIL'}  unknown session still rejected  status=${r.status}`);
    if (!sessionRejected) failures++;"""),
])
print('all P10 call-site edits ok')
