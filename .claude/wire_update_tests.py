import io

p = 'apps/directory/test/run.js'
d = io.open(p, encoding='utf-8').read()

# 1) manifest module test alongside the version-comparison one (no server needed)
old = """  const versions = await runTest(['version_compare_test.js']);
  if (versions.code !== 0) {
    cleanup();
    console.log('\\nRESULT: FAILED');
    process.exit(versions.code);
  }
"""
new = """  const versions = await runTest(['version_compare_test.js']);
  if (versions.code !== 0) {
    cleanup();
    console.log('\\nRESULT: FAILED');
    process.exit(versions.code);
  }

  // Also serverless: the manifest module verifies the same fixed signature the C++ and Kotlin
  // suites verify, so a disagreement between the three shows up here.
  console.log('\\n--- update manifest (shared vectors) ---');
  const manifestModule = await runTest(['update_manifest_test.js']);
  if (manifestModule.code !== 0) {
    cleanup();
    console.log('\\nRESULT: FAILED');
    process.exit(manifestModule.code);
  }
"""
assert old in d, 'version test anchor'
d = d.replace(old, new, 1)

# 2) the endpoint, on its own server with a scratch publish directory
old = """  // The host on the server's own LAN: the advertised address is made deliberately unreachable"""
new = """  // The update endpoint gets its own server and its own scratch publish directory, because
  // "unset means off" is the state every other pass in this file depends on.
  console.log('\\n--- update manifest endpoint ---');
  const updateDir = path.join(os.tmpdir(), `remote60-update-${process.pid}`);
  fs.rmSync(updateDir, { recursive: true, force: true });
  fs.mkdirSync(updateDir, { recursive: true });
  {
    // Published straight from the shared vectors, so the bytes served are the bytes the other
    // two runtimes verify.
    const vectors = path.join(__dirname, '..', '..', 'shared', 'update_manifest');
    fs.copyFileSync(path.join(vectors, 'test_manifest.txt'), path.join(updateDir, 'windows.manifest'));
    fs.copyFileSync(path.join(vectors, 'test_manifest.sig'), path.join(updateDir, 'windows.sig'));
    // android is deliberately left unpublished, so "nothing for this platform" has a case.
  }
  const updateEnv = { ...env, REMOTE60_UPDATE_DIR: updateDir };
  server = spawn(process.execPath, [serverPath], { env: updateEnv, stdio: 'ignore' });
  await sleep(1500);
  const updateRoute = await new Promise((resolve) => {
    const child = spawn(process.execPath, [path.join(__dirname, 'update_route_test.js')],
                        { env: updateEnv, stdio: 'inherit' });
    child.on('exit', (code) => resolve(code ?? 1));
  });
  await stopServer(server);
  fs.rmSync(updateDir, { recursive: true, force: true });
  if (updateRoute !== 0) {
    cleanup();
    console.log('\\nRESULT: FAILED');
    process.exit(updateRoute);
  }

  // The host on the server's own LAN: the advertised address is made deliberately unreachable"""
assert old in d, 'same-lan anchor'
d = d.replace(old, new, 1)

io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('run.js wired')
