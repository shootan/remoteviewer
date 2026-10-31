import io

p = 'apps/directory/server.js'
d = io.open(p, encoding='utf-8').read()

# --- config near the other env reads
old = "const SIGNUP_KEY = process.env.REMOTE60_DIR_SIGNUP_KEY || '';"
new = (old + "\n"
       "// Where published update manifests live: <UPDATE_DIR>/<platform>.manifest and .sig.\n"
       "// Unset means the update endpoint is off, which is the state every server is in until\n"
       "// someone deliberately publishes something -- there is no default directory to fall into.\n"
       "const UPDATE_DIR = process.env.REMOTE60_UPDATE_DIR || '';")
assert old in d, 'env anchor'
d = d.replace(old, new, 1)

# --- handler, placed after handleLogs
old = """const RELAY_AUTH_TTL_MS = 60 * 1000;      // how long a connect stays relay-eligible"""
new = '''/**
 * Hands out the published manifest for one platform.
 *
 * The artifact itself is NOT served from here. This process also runs the relay and the
 * heartbeat on the same event loop, and streaming a multi-megabyte installer through it would
 * make every session on the box wait behind the download. The manifest says where the artifact
 * is; something built to serve files serves it.
 *
 * Authentication reuses what already exists -- a client session or a host token -- because an
 * update check is something a signed-in machine does, and inventing a third credential for it
 * would be a third thing to get wrong. The manifest is signed either way, so this is about not
 * publishing the fleet's update state to anyone who asks, not about trusting the transport.
 */
async function handleUpdateManifest(req, res) {
  if (!UPDATE_DIR) return sendJson(res, 503, { error: 'updates are not published on this server' });

  const session = sessionFor(req);
  if (!session) {
    const hostToken = String(req.headers['x-host-token'] || '');
    const hostId = hostToken ? hostTokens.get(hashToken(hostToken)) : undefined;
    if (!hostId || !store.hosts[hostId]) {
      return sendJson(res, 401, { error: 'unknown session or host token' });
    }
  }

  const query = new URL(req.url, 'http://localhost').searchParams;
  const platform = String(query.get('platform') || '');
  // Whitelisted rather than sanitised. This value becomes part of a file name, and the set of
  // platforms is two -- there is no reason to accept anything else and then try to make it safe.
  if (platform !== 'windows' && platform !== 'android') {
    return sendJson(res, 400, { error: 'platform must be windows or android' });
  }

  let manifest;
  let signature;
  try {
    manifest = fs.readFileSync(path.join(UPDATE_DIR, `${platform}.manifest`), 'utf8');
    signature = fs.readFileSync(path.join(UPDATE_DIR, `${platform}.sig`), 'utf8').trim();
  } catch {
    // Nothing published for this platform is an ordinary answer, not a server fault.
    return sendJson(res, 404, { error: `no manifest published for ${platform}` });
  }

  // Read back what is about to be handed out. A manifest whose signature does not check here
  // would fail on every client instead, and the first person to find out should not be a user.
  const trusted = String(process.env.REMOTE60_UPDATE_PUBLIC_KEY || '');
  if (trusted) {
    const checked = updateManifest.loadManifest(manifest, signature, trusted, platform);
    if (checked.status !== updateManifest.Status.OK) {
      console.error(`[update] refusing to serve ${platform}: ${checked.status} ${checked.detail}`);
      return sendJson(res, 500, { error: 'published manifest did not verify' });
    }
  }

  return sendJson(res, 200, { manifest, signature });
}

''' + old
assert old in d, 'handler anchor'
d = d.replace(old, new, 1)

# --- route table
old = """  'POST /api/logs': handleLogs,
};"""
new = """  'POST /api/logs': handleLogs,
  'GET /api/update/manifest': handleUpdateManifest,
};"""
assert old in d, 'route anchor'
d = d.replace(old, new, 1)

# --- require the module next to the other requires
old = "const https = require('https');"
new = old + "\nconst updateManifest = require('./update_manifest');"
assert old in d, 'require anchor'
d = d.replace(old, new, 1)

# --- boot log line, so the state is visible rather than guessed
old = "  observeSock = startUdp();"
new = ("    console.log(UPDATE_DIR\n"
       "      ? `[update] serving manifests from ${UPDATE_DIR}`\n"
       "      : '[update] off (REMOTE60_UPDATE_DIR unset)');\n"
       "  });\n"
       "  observeSock = startUdp();")
assert old in d, 'boot anchor'
# the console.log lines belong inside the listen callback, which ends just before observeSock
old_block = "  });\n  observeSock = startUdp();"
assert old_block in d
d = d.replace(old_block, new, 1)

io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('server route added')
