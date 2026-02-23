const WebSocket = require('ws');
const fs = require('fs');
const path = require('path');
const http = require('http');
const https = require('https');

const PORT = Number(process.env.PORT || 3000);
const HEARTBEAT_MS = Number(process.env.HEARTBEAT_MS || 15000);
const REGISTER_TIMEOUT_MS = Number(process.env.REGISTER_TIMEOUT_MS || 10000);
const EXPECTED_AUTH_TOKEN = process.env.REMOTE60_SIGNALING_TOKEN || '';
const TLS_KEY_PATH = process.env.REMOTE60_SIGNALING_TLS_KEY || '';
const TLS_CERT_PATH = process.env.REMOTE60_SIGNALING_TLS_CERT || '';
const PUBLIC_DIR = path.join(__dirname, 'public');

const ERR = {
  INVALID_JSON: 'E_INVALID_JSON',
  INVALID_JSON_OBJECT: 'E_INVALID_JSON_OBJECT',
  MISSING_TYPE: 'E_MISSING_TYPE',
  INVALID_ROLE: 'E_INVALID_ROLE',
  REGISTER_TIMEOUT: 'E_REGISTER_TIMEOUT',
  REGISTER_FIRST: 'E_REGISTER_FIRST',
  AUTH_FAILED: 'E_AUTH_FAILED',
  PEER_NOT_CONNECTED: 'E_PEER_NOT_CONNECTED',
  UNKNOWN_TYPE: 'E_UNKNOWN_TYPE',
};

let baseServer = null;
let isTls = false;
function contentTypeFor(filePath) {
  const ext = path.extname(filePath).toLowerCase();
  if (ext === '.html') return 'text/html; charset=utf-8';
  if (ext === '.js') return 'application/javascript; charset=utf-8';
  if (ext === '.css') return 'text/css; charset=utf-8';
  if (ext === '.json') return 'application/json; charset=utf-8';
  return 'application/octet-stream';
}

function sendText(res, status, body) {
  res.writeHead(status, { 'content-type': 'text/plain; charset=utf-8' });
  res.end(body);
}

function handleHttp(req, res) {
  const method = String(req.method || 'GET').toUpperCase();
  if (method !== 'GET' && method !== 'HEAD') {
    sendText(res, 405, 'method_not_allowed');
    return;
  }

  let pathname = '/';
  try {
    pathname = new URL(req.url || '/', 'http://localhost').pathname;
  } catch {
    pathname = '/';
  }

  if (pathname === '/healthz') {
    res.writeHead(200, { 'content-type': 'application/json; charset=utf-8' });
    res.end(JSON.stringify({ ok: true }));
    return;
  }

  const rel = (pathname === '/' ? 'index.html' : pathname.replace(/^\/+/, ''));
  const normalized = path.normalize(rel);
  const absPublic = path.resolve(PUBLIC_DIR);
  const absTarget = path.resolve(path.join(absPublic, normalized));
  if (!absTarget.startsWith(absPublic + path.sep) && absTarget !== absPublic) {
    sendText(res, 403, 'forbidden');
    return;
  }

  fs.stat(absTarget, (statErr, st) => {
    if (statErr || !st || !st.isFile()) {
      sendText(res, 404, 'not_found');
      return;
    }
    fs.readFile(absTarget, (readErr, data) => {
      if (readErr) {
        sendText(res, 500, 'read_failed');
        return;
      }
      res.writeHead(200, { 'content-type': contentTypeFor(absTarget) });
      if (method === 'HEAD') {
        res.end();
      } else {
        res.end(data);
      }
    });
  });
}

if (TLS_KEY_PATH && TLS_CERT_PATH) {
  try {
    const key = fs.readFileSync(TLS_KEY_PATH);
    const cert = fs.readFileSync(TLS_CERT_PATH);
    baseServer = https.createServer({ key, cert }, handleHttp);
    isTls = true;
  } catch (e) {
    console.error('[signaling] fatal: tls certificate load failed', String(e?.message || e));
    process.exit(1);
  }
} else {
  baseServer = http.createServer(handleHttp);
}

const wss = new WebSocket.Server({ server: baseServer });

wss.on('error', (err) => {
  const msg = String(err?.message || err || 'unknown');
  if (err?.code === 'EADDRINUSE') {
    log('fatal: port already in use', { code: 'E_PORT_IN_USE', port: PORT });
  } else {
    log('fatal: signaling server error', { code: 'E_SERVER', msg });
  }
  process.exit(1);
});

/** @type {Map<string, WebSocket>} */
const peers = new Map(); // role -> socket

let nextConnId = 1;
const stats = {
  connected: 0,
  registered: 0,
  routed: 0,
  invalid: 0,
  dropped: 0,
};

function log(msg, extra = undefined) {
  const ts = new Date().toISOString();
  if (extra === undefined) {
    console.log(`[signaling][${ts}] ${msg}`);
  } else {
    console.log(`[signaling][${ts}] ${msg}`, extra);
  }
}

function ctx(ws, msg = undefined) {
  return {
    session: ws?._connId ?? 'n/a',
    role: ws?._role ?? 'unregistered',
    requestId: msg?.requestId || null,
  };
}

function send(ws, payload) {
  if (ws && ws.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify(payload));
    return true;
  }
  return false;
}

function sendError(ws, code, reason, requestId = undefined, extra = undefined) {
  const payload = { type: 'error', code, reason };
  if (requestId) payload.requestId = requestId;
  if (extra && typeof extra === 'object') Object.assign(payload, extra);
  send(ws, payload);
}

function otherRole(role) {
  return role === 'host' ? 'client' : role === 'client' ? 'host' : null;
}

function cleanupSocket(ws, reason = 'cleanup') {
  if (!ws) return;
  if (ws._registerTimer) {
    clearTimeout(ws._registerTimer);
    ws._registerTimer = null;
  }

  if (ws._role && peers.get(ws._role) === ws) {
    peers.delete(ws._role);
    const peer = peers.get(otherRole(ws._role));
    if (peer) send(peer, { type: 'peer_state', peerOnline: false });
    log('role offline', { ...ctx(ws), reason });
  }
}

function validateMessage(msg) {
  if (!msg || typeof msg !== 'object') return { code: ERR.INVALID_JSON_OBJECT, reason: 'invalid_json_object' };
  if (typeof msg.type !== 'string') return { code: ERR.MISSING_TYPE, reason: 'missing_type' };
  return null;
}

wss.on('connection', (ws, req) => {
  ws._connId = nextConnId++;
  ws._alive = true;
  ws._role = null;

  stats.connected++;
  log('connected', { ...ctx(ws), ip: req?.socket?.remoteAddress || 'unknown' });

  ws.on('pong', () => {
    ws._alive = true;
  });

  ws._registerTimer = setTimeout(() => {
    if (!ws._role) {
      sendError(ws, ERR.REGISTER_TIMEOUT, 'register_timeout');
      stats.dropped++;
      log('register timeout', ctx(ws));
      ws.terminate();
    }
  }, REGISTER_TIMEOUT_MS);

  send(ws, { type: 'hello', message: 'connected', need: 'register', session: ws._connId });

  ws.on('message', (raw) => {
    let msg;
    try {
      msg = JSON.parse(raw.toString());
    } catch {
      stats.invalid++;
      sendError(ws, ERR.INVALID_JSON, 'invalid_json');
      return;
    }

    const err = validateMessage(msg);
    if (err) {
      stats.invalid++;
      sendError(ws, err.code, err.reason, msg?.requestId);
      return;
    }

    if (msg.type === 'register') {
      const requested = msg.role;
      if (requested !== 'host' && requested !== 'client') {
        stats.invalid++;
        sendError(ws, ERR.INVALID_ROLE, 'invalid_role', msg.requestId);
        return;
      }
      if (EXPECTED_AUTH_TOKEN && msg.authToken !== EXPECTED_AUTH_TOKEN) {
        stats.invalid++;
        sendError(ws, ERR.AUTH_FAILED, 'auth_failed', msg.requestId);
        log('register denied: auth_failed', { ...ctx(ws, msg) });
        return;
      }

      ws._role = requested;
      const prev = peers.get(requested);
      if (prev && prev !== ws) {
        send(prev, { type: 'info', reason: 'replaced_by_new_connection' });
        prev.close();
      }

      peers.set(requested, ws);
      stats.registered++;
      if (ws._registerTimer) {
        clearTimeout(ws._registerTimer);
        ws._registerTimer = null;
      }

      send(ws, { type: 'registered', role: requested, session: ws._connId, requestId: msg.requestId });
      send(ws, { type: 'peer_state', peerOnline: !!peers.get(otherRole(requested)), requestId: msg.requestId });

      const peer = peers.get(otherRole(requested));
      if (peer) send(peer, { type: 'peer_state', peerOnline: true });

      log('registered', { ...ctx(ws, msg) });
      return;
    }

    if (!ws._role) {
      stats.invalid++;
      sendError(ws, ERR.REGISTER_FIRST, 'register_first', msg.requestId);
      return;
    }

    if (msg.type === 'offer' || msg.type === 'answer' || msg.type === 'ice') {
      const peer = peers.get(otherRole(ws._role));
      if (!peer) {
        sendError(ws, ERR.PEER_NOT_CONNECTED, 'peer_not_connected', msg.requestId, { for: msg.type });
        return;
      }

      const ok = send(peer, {
        type: msg.type,
        from: ws._role,
        sdp: typeof msg.sdp === 'string' ? msg.sdp : undefined,
        candidate: msg.candidate,
        candidateMid: typeof msg.candidateMid === 'string' ? msg.candidateMid : undefined,
        candidateMLineIndex: Number.isInteger(msg.candidateMLineIndex) ? msg.candidateMLineIndex : undefined,
        requestId: msg.requestId,
        session: ws._connId,
      });

      if (ok) {
        stats.routed++;
        log('routed', { ...ctx(ws, msg), type: msg.type, to: otherRole(ws._role) });
      } else {
        stats.dropped++;
      }
      return;
    }

    stats.invalid++;
    sendError(ws, ERR.UNKNOWN_TYPE, 'unknown_type', msg.requestId, { got: msg.type });
  });

  ws.on('close', () => cleanupSocket(ws, 'close'));
  ws.on('error', (e) => {
    log('socket error', { ...ctx(ws), msg: String(e?.message || e) });
    cleanupSocket(ws, 'error');
  });
});

const heartbeatTimer = setInterval(() => {
  for (const ws of wss.clients) {
    if (ws._alive === false) {
      log('heartbeat timeout', ctx(ws));
      ws.terminate();
      continue;
    }
    ws._alive = false;
    try {
      ws.ping();
    } catch {
      ws.terminate();
    }
  }
}, HEARTBEAT_MS);

wss.on('close', () => clearInterval(heartbeatTimer));

process.on('SIGINT', () => {
  log('SIGINT received, shutting down', stats);
  clearInterval(heartbeatTimer);
  wss.close(() => process.exit(0));
});

baseServer.listen(PORT, () => {
  const scheme = isTls ? 'wss' : 'ws';
  log(`listening on ${scheme}://127.0.0.1:${PORT}`);
});
