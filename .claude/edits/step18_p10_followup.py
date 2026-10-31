import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit

ROOT = r'D:\remote\remote'
S = ROOT + r'\apps\native_poc\src'

# ---------------- server: sessions persist is out of scope -- restore the original contract ----------------
J = ROOT + r'\apps\directory\server.js'
edit(J, [
(r"""/** @type {{accounts: Object, hosts: Object, sessions: Object}} */
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
}""",
 r"""/** @type {{accounts: Object, hosts: Object}} */
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
}"""),
(r"""function indexHostTokens() {
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
}""",
 r"""function indexHostTokens() {
  hostTokens.clear();
  for (const host of Object.values(store.hosts)) {
    if (host.tokenHash) hostTokens.set(host.tokenHash, host.hostId);
  }
}"""),
(r"""const sessions = new Map();      // sha256(sessionToken) -> {accountId, expiresAt}, mirrored in store.sessions""",
 r"""const sessions = new Map();      // sessionToken -> {accountId, expiresAt}"""),
(r"""  let expired = 0;
  for (const [hash, s] of sessions) {
    if (s.expiresAt <= now) {
      sessions.delete(hash);
      delete store.sessions[hash];
      expired++;
    }
  }
  if (expired) saveStoreSoon();""",
 r"""  for (const [token, s] of sessions) if (s.expiresAt <= now) sessions.delete(token);"""),
(r"""  const s = sessions.get(hashToken(token));
  if (!s || s.expiresAt <= Date.now()) return null;
  return s;""",
 r"""  const s = sessions.get(token);
  if (!s || s.expiresAt <= Date.now()) return null;
  return s;"""),
(r"""  const token = randomToken();
  const expiresAt = Date.now() + SESSION_TTL_MS;
  const session = { accountId: id, expiresAt };
  sessions.set(hashToken(token), session);
  store.sessions[hashToken(token)] = session;
  // Now, not in 200 ms: a deploy right after a login must not sign that client out again.
  saveStoreNow();
  sendJson(res, 200, { sessionToken: token, expiresAt });""",
 r"""  const token = randomToken();
  const expiresAt = Date.now() + SESSION_TTL_MS;
  sessions.set(token, { accountId: id, expiresAt });
  sendJson(res, 200, { sessionToken: token, expiresAt });"""),
])

edit(ROOT + r'\apps\directory\test\restart_test.js', [
(r"""// A host -- and a signed-in client -- must stay signed in across a server restart.
//
// Host tokens once lived only in memory, so every deploy or reboot quietly invalidated them.
// Each PC would then be told its token was unknown, and since the host app deliberately does
// not keep the password, someone had to walk to the machine and sign in again. Sessions had the
// same flaw until 2026-09-07 (P10): a restart signed every client out, and the client shell,
// which keeps no password either, kept uploading its log with a dead token. This runs in two
// phases around a restart performed by the runner; the register phase hands the runner
// "hostToken:sessionToken".""",
 r"""// A host must stay signed in across a server restart.
//
// Host tokens once lived only in memory, so every deploy or reboot quietly invalidated them.
// Each PC would then be told its token was unknown, and since the host app deliberately does
// not keep the password, someone had to walk to the machine and sign in again. This runs in two
// phases around a restart performed by the runner; the register phase hands the runner
// "hostToken:sessionToken". Client sessions are in memory by contract (the server's auth
// lifetime is a separate scope): the verify phase pins that a restart DOES forget them and that
// an unknown session is refused, which is what the client shell's uploader must cope with."""),
(r"""    r = await api('GET', '/api/hosts', null, sessionToken);
    const sessionSurvived = r.status === 200;
    console.log(`${sessionSurvived ? 'PASS' : 'FAIL'}  client session survives a server restart  status=${r.status}`);
    if (!sessionSurvived) failures++;

    r = await api('GET', '/api/hosts', null, 'not-a-real-session');""",
 r"""    r = await api('GET', '/api/hosts', null, sessionToken);
    const sessionForgotten = r.status === 401;
    console.log(`${sessionForgotten ? 'PASS' : 'FAIL'}  a client session does not survive a server restart (in-memory by contract)  status=${r.status}`);
    if (!sessionForgotten) failures++;

    r = await api('GET', '/api/hosts', null, 'not-a-real-session');"""),
])

# ---------------- uploader: owner / routing epoch fencing ----------------
L = S + r'\log_upload.cpp'
edit(L, [
(r"""  bool credentials = false;
  bool authRejected = false;
  uint64_t configGeneration = 0;  // bumped by every credential change; a send remembers its own""",
 r"""  bool credentials = false;
  bool authRejected = false;
  // Who the queued lines belong to and where they go: account identity + normalised server
  // host:port + device. A change (or a sign-out) bumps ownerEpoch; a send carries the epoch it
  // was made under, and an answer for a previous epoch may not touch anything -- not the pause
  // state, not the hold, and its body is never re-sent under the new owner's credentials.
  std::string ownerKey;
  uint64_t ownerEpoch = 0;
  uint64_t foreignAnswersDiscarded = 0;
  uint64_t configGeneration = 0;  // bumped by every credential change; a send remembers its own"""),
(r"""  std::string host;
  uint16_t port = 0;
  uint64_t configGeneration = 0;
  uint32_t attempts = 0;        // before this send
  uint64_t firstAttemptUs = 0;  // 0 = first send
};""",
 r"""  std::string host;
  uint16_t port = 0;
  uint64_t ownerEpoch = 0;
  uint64_t configGeneration = 0;
  uint32_t attempts = 0;        // before this send
  uint64_t firstAttemptUs = 0;  // 0 = first send
};

std::string owner_key(const std::string& identity, const std::string& host, uint16_t port, const std::string& device) {
  std::string h = host;
  for (auto& c : h) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return identity + "|" + h + ":" + std::to_string(port) + "|" + device;
}"""),
(r"""  job->host = s.host;
  job->port = s.port;
  job->configGeneration = s.configGeneration;
  return true;
}""",
 r"""  job->host = s.host;
  job->port = s.port;
  job->ownerEpoch = s.ownerEpoch;
  job->configGeneration = s.configGeneration;
  return true;
}"""),
(r"""    {
      std::lock_guard<std::mutex> lk(s.mu);
      s.lastStatus = status;
      if (job.attempts > 0) ++s.retriedBatches;""",
 r"""    {
      std::lock_guard<std::mutex> lk(s.mu);
      if (job.ownerEpoch != s.ownerEpoch) {
        // A late answer for a batch whose owner is gone (another account, another server or
        // device, a sign-out): whatever it says -- 401, 5xx, even 200 -- it describes a session
        // that no longer exists. Discard the batch; the current owner's state is untouched.
        ++s.foreignAnswersDiscarded;
        ++s.discardedBatches;
        diag("late answer for a previous owner discarded status=" + std::to_string(status) + " reached=" +
             (sent ? "1" : "0") + " stream=" + job.stream + " bytes=" + std::to_string(job.body.size()));
        continue;
      }
      s.lastStatus = status;
      if (job.attempts > 0) ++s.retriedBatches;"""),
(r"""  const std::string device = config.device.empty() ? directory::machine_id() : config.device;
  const std::string headers = build_headers(config, device);
  const char* auth = config.sessionToken.empty() ? "host-token" : "bearer";

  std::string reason;
  {
    std::lock_guard<std::mutex> lk(s.mu);
    if (s.running && !s.stopping) {
      const bool sameCredentials = s.credentials && s.headers == headers && s.host == host && s.port == port;
      const bool sameIdentity = s.config.identity == config.identity;
      if (sameCredentials && sameIdentity) {""",
 r"""  const std::string device = config.device.empty() ? directory::machine_id() : config.device;
  const std::string headers = build_headers(config, device);
  const char* auth = config.sessionToken.empty() ? "host-token" : "bearer";
  const std::string ownerKey = owner_key(config.identity, host, port, device);

  std::string reason;
  {
    std::lock_guard<std::mutex> lk(s.mu);
    if (s.running && !s.stopping) {
      // Same owner = same account, same server (host:port) and same device: only then does a new
      // token inherit the queue and the held batches. Anything else is a different destination for
      // what is queued, and what is queued is discarded.
      const bool sameIdentity = s.credentials && s.ownerKey == ownerKey;
      const bool sameCredentials = sameIdentity && s.headers == headers;
      if (sameCredentials && sameIdentity) {"""),
(r"""      uint64_t lines = 0, batches = 0;
      if (!sameIdentity) discard_all_locked(s, &lines, &batches);
      const bool wasRejected = s.authRejected;
      s.config = config;
      s.host = host;
      s.port = port;
      s.device = device;
      s.headers = headers;
      s.credentials = true;
      s.authRejected = false;
      ++s.configGeneration;""",
 r"""      uint64_t lines = 0, batches = 0;
      if (!sameIdentity) {
        discard_all_locked(s, &lines, &batches);
        ++s.ownerEpoch;  // answers still in flight for the previous owner are discarded on arrival
      }
      const bool wasRejected = s.authRejected;
      s.config = config;
      s.host = host;
      s.port = port;
      s.device = device;
      s.headers = headers;
      s.ownerKey = ownerKey;
      s.credentials = true;
      s.authRejected = false;
      ++s.configGeneration;"""),
(r"""      reason = std::string(sameIdentity ? "token replaced" : "identity changed") + " auth=" + auth +""",
 r"""      reason = std::string(sameIdentity ? "token replaced" : "owner changed") + " auth=" + auth +"""),
(r"""      s.config = config;
      s.host = host;
      s.port = port;
      s.device = device;
      s.headers = headers;
      s.credentials = true;
      s.authRejected = false;
      s.stopping = false;
      s.running = true;
      ++s.configGeneration;
      s.worker = std::thread(worker_loop);""",
 r"""      s.config = config;
      s.host = host;
      s.port = port;
      s.device = device;
      s.headers = headers;
      s.ownerKey = ownerKey;
      ++s.ownerEpoch;
      s.credentials = true;
      s.authRejected = false;
      s.stopping = false;
      s.running = true;
      ++s.configGeneration;
      s.worker = std::thread(worker_loop);"""),
(r"""    discard_all_locked(s, &lines, &batches);
    s.credentials = false;
    s.authRejected = false;
    s.headers.clear();
    s.config.sessionToken.clear();
    s.config.hostToken.clear();
    ++s.configGeneration;""",
 r"""    discard_all_locked(s, &lines, &batches);
    s.credentials = false;
    s.authRejected = false;
    s.headers.clear();
    s.ownerKey.clear();
    ++s.ownerEpoch;  // a late answer for anything sent before the sign-out is discarded
    s.config.sessionToken.clear();
    s.config.hostToken.clear();
    ++s.configGeneration;"""),
(r"""  st.heldBatches = s.held.size();
  st.lastStatus = s.lastStatus;""",
 r"""  st.heldBatches = s.held.size();
  st.foreignAnswersDiscarded = s.foreignAnswersDiscarded;
  st.lastStatus = s.lastStatus;"""),
(r"""  s.droppedLines = s.discardedLines = 0;
  s.lastStatus = 0;""",
 r"""  s.droppedLines = s.discardedLines = 0;
  s.foreignAnswersDiscarded = 0;
  s.ownerKey.clear();
  s.lastStatus = 0;"""),
(r"""#include <algorithm>
#include <chrono>""",
 r"""#include <algorithm>
#include <cctype>
#include <chrono>"""),
])

edit(S + r'\log_upload.hpp', [
(r"""  uint64_t heldBatches = 0;       // waiting for a retry slot or for a new token
  uint32_t lastStatus = 0;        // http status of the last send (0 = unreachable)""",
 r"""  uint64_t heldBatches = 0;       // waiting for a retry slot or for a new token
  uint64_t foreignAnswersDiscarded = 0;  // late answers for a previous owner (account/server/device/sign-out)
  uint32_t lastStatus = 0;        // http status of the last send (0 = unreachable)"""),
(r"""  // Whose lines these are, as the caller knows it (account plus host/machine). When it changes,
  // whatever is still queued was produced under the previous owner and is discarded rather than
  // sent under the new credentials into the new account's directory.
  std::string identity;""",
 r"""  // Whose lines these are, as the caller knows it (the account). Together with the server
  // (host:port) and the device it forms the OWNER of what is queued: when any of the three
  // changes, whatever is still queued -- and any answer still in flight -- belonged to the
  // previous owner and is discarded rather than sent under the new credentials to the new place.
  std::string identity;"""),
(r""" * on writing to disk. A running uploader keeps its queue when only the token changed (same
 * identity) and discards it when the identity changed. A 401 pause ends here: held batches go
 * out with the new token first. Calling it again with identical values is a no-op.""",
 r""" * on writing to disk. A running uploader keeps its queue when only the token changed (same
 * owner: identity, server and device) and discards it when the owner changed. A 401 pause ends
 * here: held batches go out with the new token first. Calling it again with identical values is
 * a no-op."""),
])

# ---------------- tests ----------------
T = S + r'\log_upload_test.cpp'
edit(T, [
(r"""  CHECK(reason.rfind("identity changed", 0) == 0);""",
 r"""  CHECK(reason.rfind("owner changed", 0) == 0);"""),
(r"""// 8. The diag never contains a token, whatever happened above.
void test_diag_has_no_token() {
  std::printf("[8] diag holds no token\n");
  const std::string d = read_diag();
  CHECK(!d.empty());
  for (const char* secret : {"HOST-TOKEN-ONE", "HOST-TOKEN-TWO", "SESSION-ALICE", "SESSION-BOB", "STALE",
                             "FRESH"}) {
    CHECK(d.find(secret) == std::string::npos);
  }
  CHECK(d.find("auth rejected status=401") != std::string::npos);
  CHECK(d.find("identity changed") != std::string::npos);
  CHECK(d.find("gave up") != std::string::npos);
}""",
 r"""// 9. Owner fencing: a request of owner A is in flight (the server sits on it) when the owner
//    changes -- another account, another server, another device, or a sign-out followed by the
//    same account signing in again. The late answer, 401 or 500, must not pause the new session,
//    must not resurrect the batch, and A's body must never reach the new owner's destination.
void late_answer_for_previous_owner(FakeLogServer& server, FakeLogServer& server2, int status, int change) {
  const char* what = change == 0 ? "account" : change == 1 ? "server url" : change == 2 ? "device" : "sign-out + same account";
  std::printf("[9] late %d after an owner change (%s) is discarded\n", status, what);
  server.Clear();
  server2.Clear();
  server.ScriptSteps({FakeLogServer::Step{status, 400}});  // the answer arrives 400 ms later
  std::atomic<int> callbacks{0};
  log_upload_set_auth_rejected_callback([&callbacks] { ++callbacks; });
  LogUploadConfig a = base_config(server, "alice/machine-A");
  a.sessionToken = "SESSION-A";
  std::string reason;
  CHECK(log_upload_configure(a, &reason));
  log_upload_enqueue("client", "alice private line");
  CHECK(server.WaitFor(1, 2000));  // in flight; the server is sitting on it
  FakeLogServer* receiver = &server;
  LogUploadConfig b = a;
  switch (change) {
    case 0: b.identity = "bob/machine-A"; b.sessionToken = "SESSION-B"; break;
    case 1: b.directoryUrl = server2.url(); receiver = &server2; break;
    case 2: b.device = "other-device"; break;
    default: log_upload_clear_credentials("logged out"); b.sessionToken = "SESSION-A2"; break;
  }
  CHECK(log_upload_configure(b, &reason));
  CHECK(reason.rfind("owner changed", 0) == 0);
  const size_t before = receiver->Count();
  log_upload_enqueue("client", "new owner line");
  CHECK(receiver->WaitFor(before + 1, 2000));
  // Let the late answer land and anything it might wrongly trigger play out.
  CHECK(wait_until([] { return log_upload_status().foreignAnswersDiscarded == 1; }, 2000));
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  for (FakeLogServer* srv : {&server, &server2}) {
    const auto reqs = srv->Requests();
    for (size_t i = 0; i < reqs.size(); ++i) {
      if (srv == &server && i == 0) continue;  // the in-flight request itself
      CHECK(reqs[i].body.find("alice private line") == std::string::npos);
      CHECK(reqs[i].auth() == "Bearer " + b.sessionToken);
      CHECK(reqs[i].device() == b.device);
    }
  }
  const LogUploadStatus st = log_upload_status();
  CHECK(!st.authRejected);
  CHECK(st.heldBatches == 0);
  CHECK(st.foreignAnswersDiscarded == 1);
  CHECK(st.sentBatches == 1);
  CHECK(callbacks.load() == 0);
  log_upload_set_auth_rejected_callback({});
  log_upload_stop();
}

// 8. The diag never contains a token, whatever happened above.
void test_diag_has_no_token() {
  std::printf("[8] diag holds no token\n");
  const std::string d = read_diag();
  CHECK(!d.empty());
  for (const char* secret : {"HOST-TOKEN-ONE", "HOST-TOKEN-TWO", "SESSION-ALICE", "SESSION-BOB", "STALE",
                             "FRESH", "SESSION-A", "SESSION-B", "SESSION-A2"}) {
    CHECK(d.find(secret) == std::string::npos);
  }
  CHECK(d.find("auth rejected status=401") != std::string::npos);
  CHECK(d.find("owner changed") != std::string::npos);
  CHECK(d.find("gave up") != std::string::npos);
  CHECK(d.find("late answer for a previous owner discarded") != std::string::npos);
}"""),
(r"""  FakeLogServer server;
  if (!server.Start()) {
    std::printf("log_upload_test: could not start the fake server\n");
    return 2;
  }
  test_token_replaced_same_identity(server);
  test_identity_change_discards_queue(server);
  test_401_pauses_until_new_token(server);
  test_transient_retry_is_bounded(server);
  test_permanent_discards_at_once(server);
  test_clear_credentials(server);
  test_stale_401_after_replacement(server);
  test_diag_has_no_token();
  server.Stop();""",
 r"""  FakeLogServer server;
  FakeLogServer server2;
  if (!server.Start() || !server2.Start()) {
    std::printf("log_upload_test: could not start the fake servers\n");
    return 2;
  }
  test_token_replaced_same_identity(server);
  test_identity_change_discards_queue(server);
  test_401_pauses_until_new_token(server);
  test_transient_retry_is_bounded(server);
  test_permanent_discards_at_once(server);
  test_clear_credentials(server);
  test_stale_401_after_replacement(server);
  for (const int status : {401, 500}) {
    for (int change = 0; change < 4; ++change) late_answer_for_previous_owner(server, server2, status, change);
  }
  test_diag_has_no_token();
  server.Stop();
  server2.Stop();"""),
])
print('P10 follow-up edits ok')
