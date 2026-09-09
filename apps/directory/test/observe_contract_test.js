// The third leg of the observe-endpoint contract: what the server actually emits.
//
// The C++ and Kotlin suites read apps/shared/observe_endpoint_vectors.txt and check that they
// parse it the same way. That establishes the clients agree with each other. It says nothing
// about whether the document this server emits is one of the shapes those rules accept -- and if
// it is not, both clients would correctly parse nothing out of it and both would fall back, which
// is precisely the failure the whole change exists to remove.
//
// So this reads the same vectors for the rules, starts its own server, and holds the real
// response against them: the advertisement is nested where the clients look, the port is a whole
// number in range, and a port the server cannot honour is left out entirely rather than emitted
// as something the vectors say is not a port.
//
// Own server, own ports, own data file, loopback only.

const http = require('http');
const path = require('path');
const os = require('os');
const fs = require('fs');
const { spawn, spawnSync } = require('child_process');

const HTTP = 18290;
const UDP = 18291;

let failures = 0;
function check(name, cond, detail) {
  console.log(`${cond ? 'PASS' : 'FAIL'}  ${name}${detail ? '  ' + detail : ''}`);
  if (!cond) failures++;
}

function get(pathname, body, token) {
  return new Promise((resolve, reject) => {
    const payload = body ? JSON.stringify(body) : null;
    const req = http.request(
      { host: '127.0.0.1', port: HTTP, path: pathname, method: payload ? 'POST' : 'GET',
        headers: Object.assign(
          payload ? { 'content-type': 'application/json',
                      'content-length': Buffer.byteLength(payload) } : {},
          token ? { authorization: 'Bearer ' + token } : {}) },
      (res) => {
        let data = '';
        res.on('data', (c) => (data += c));
        res.on('end', () => {
          try { resolve({ status: res.statusCode, body: JSON.parse(data || '{}'), raw: data }); }
          catch { resolve({ status: res.statusCode, body: {}, raw: data }); }
        });
      });
    req.on('error', reject);
    if (payload) req.write(payload);
    req.end();
  });
}

function waitForServer() {
  return new Promise((resolve, reject) => {
    const deadline = Date.now() + 8000;
    const attempt = () => {
      const req = http.request({ host: '127.0.0.1', port: HTTP, path: '/healthz' },
                               (res) => { res.resume(); resolve(); });
      req.on('error', () => {
        if (Date.now() > deadline) return reject(new Error('server did not start'));
        setTimeout(attempt, 100);
      });
      req.end();
    };
    attempt();
  });
}

/** The rules from the shared file, as the clients apply them. */
function loadRules() {
  let dir = __dirname;
  for (let i = 0; i < 6; i++) {
    const candidate = path.join(dir, 'apps/shared/observe_endpoint_vectors.txt');
    if (fs.existsSync(candidate)) return fs.readFileSync(candidate, 'utf8');
    dir = path.join(dir, '..');
  }
  throw new Error('shared vectors not found');
}

/** Rules 2, 3 and 7, applied to a response the way a client applies them. */
function parseObserve(response) {
  const observe = response && response.observe;
  if (!observe || typeof observe !== 'object') return { known: false, port: 0, host: '' };
  const port = observe.port;
  if (typeof port !== 'number' || !Number.isInteger(port) || port < 1 || port > 65535) {
    return { known: false, port: 0, host: '' };
  }
  const host = typeof observe.host === 'string' ? observe.host.trim() : '';
  return { known: true, port, host };
}

async function runServer(env, body) {
  const serverPath = path.join(__dirname, '..', 'server.js');
  const server = spawn(process.execPath, [serverPath], { env, stdio: 'ignore' });
  try {
    await waitForServer();
    await body();
  } finally {
    await new Promise((resolve) => { server.on('exit', resolve); server.kill(); });
  }
}

(async () => {
  const rules = loadRules();
  check('the shared vectors are readable from here', rules.includes('port|0|0|443|1|0'));

  const dataPath = path.join(os.tmpdir(), `remote60-observe-contract-${process.pid}.json`);
  fs.rmSync(dataPath, { force: true });
  const baseEnv = { ...process.env,
                    REMOTE60_DIR_DATA: dataPath,
                    REMOTE60_DIR_PORT: String(HTTP),
                    REMOTE60_DIR_UDP_PORT: String(UDP),
                    REMOTE60_DIR_SIGNUP_KEY: 'observe-signup-key' };
  spawnSync(process.execPath, [path.join(__dirname, '..', 'server.js'),
                               '--add-account', 'observer', 'observe-pass-1234'],
            { env: baseEnv, stdio: 'ignore' });

  try {
    // ---- a configured endpoint reaches every document a client reads it from
    await runServer({ ...baseEnv,
                      REMOTE60_DIR_OBSERVE_PORT: '29181',
                      REMOTE60_DIR_OBSERVE_HOST: 'obs.example' }, async () => {
      const health = await get('/healthz');
      const fromHealth = parseObserve(health.body);
      check('healthz advertises the endpoint', fromHealth.known && fromHealth.port === 29181,
            JSON.stringify(health.body.observe));
      check('...with the host it was given', fromHealth.host === 'obs.example', fromHealth.host);
      check('...nested where the clients look, not at the top level',
            health.body.port === undefined, health.raw);

      const login = await get('/api/login', { id: 'observer', pw: 'observe-pass-1234' });
      const fromLogin = parseObserve(login.body);
      check('login carries the same endpoint', fromLogin.known && fromLogin.port === 29181,
            JSON.stringify(login.body.observe));

      const registered = await get('/api/host/register',
                                   { id: 'observer', pw: 'observe-pass-1234',
                                     hostName: 'Observed PC', machineId: 'machine-observe' });
      const fromRegister = parseObserve(registered.body);
      check('host registration carries it too', fromRegister.known && fromRegister.port === 29181,
            JSON.stringify(registered.body.observe));

      check('the port is a whole number, not a string',
            typeof health.body.observe.port === 'number' &&
              Number.isInteger(health.body.observe.port), health.raw);
    });

    // ---- a port the server cannot honour is not emitted as one
    //
    // The vectors say 65536 is not a port. A server that published it anyway would be handing
    // both clients a value they must discard -- and if either one did not, it would dial 0.
    await runServer({ ...baseEnv, REMOTE60_DIR_OBSERVE_PORT: '65536' }, async () => {
      const health = await get('/healthz');
      check('an out-of-range port is left out entirely', health.body.observe === undefined,
            health.raw);
      check('...so a client reads it as absence, not as a port',
            parseObserve(health.body).known === false);
    });

    await runServer({ ...baseEnv, REMOTE60_DIR_OBSERVE_PORT: 'not-a-number' }, async () => {
      const health = await get('/healthz');
      check('a nonsense port is left out entirely', health.body.observe === undefined,
            health.raw);
    });

    // ---- with nothing configured, the advertisement is the udp port the server is really on
    await runServer(baseEnv, async () => {
      const health = await get('/healthz');
      const got = parseObserve(health.body);
      check('by default the server advertises the port it listens on',
            got.known && got.port === UDP, JSON.stringify(health.body.observe));
    });
  } catch (err) {
    check('the contract exercise ran to the end', false, String(err && err.message));
  } finally {
    fs.rmSync(dataPath, { force: true });
  }

  console.log(failures ? `\n${failures} FAILED` : '\nall observe contract checks passed');
  process.exit(failures ? 1 : 0);
})();
