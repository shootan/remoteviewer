import io

p = 'apps/native_poc/src/update_effects_test.cpp'
d = io.open(p, encoding='utf-8').read()

# ---- base_config: scratch registry/service + new callback signatures
old = """  c.enumerateTargets = []() { return std::vector<uint32_t>{}; };
  c.requestStop = [](uint32_t) { return true; };
  c.registerInstall = []() { return true; };"""
new = """  c.enumerateTargets = []() { return std::vector<ProcessTarget>{}; };
  c.requestStop = [](const ProcessTarget&) { return true; };
  // Scratch values. Nothing reads them yet, but requiring them means a future RegisterInstall
  // cannot quietly reach for HKLM\\\\...\\\\Uninstall\\\\GNLink or the real GNLinkSecureInput service.
  c.registryRoot = L"HKCU\\\\Software\\\\GNLinkUpdateTest";
  c.serviceName = L"GNLinkUpdateTestService";
  c.registerInstall = []() { return true; };"""
assert old in d, 'base_config anchor'
d = d.replace(old, new, 1)

# ---- missing-field table additions
old = """        {"healthCheck", [](UpdateEffectsConfig& x) { x.healthCheck = nullptr; }},
    };"""
new = """        {"healthCheck", [](UpdateEffectsConfig& x) { x.healthCheck = nullptr; }},
        {"registryRoot", [](UpdateEffectsConfig& x) { x.registryRoot.clear(); }},
        {"serviceName", [](UpdateEffectsConfig& x) { x.serviceName.clear(); }},
    };"""
assert old in d, 'missing table anchor'
d = d.replace(old, new, 1)

# ---- process stop section: full identities
old = """    const std::vector<uint32_t> pids = {a.pid(), b.pid()};

    UpdateEffectsConfig c = base_config(install, staging);
    c.enumerateTargets = [pids]() { return pids; };
    c.requestStop = [&](uint32_t pid) {
      // Stands in for the real WM_CLOSE / CTRL_BREAK request. These are our own children.
      if (pid == a.pid()) { a.kill(); return true; }
      if (pid == b.pid()) { b.kill(); return true; }
      return false;
    };"""
new = """    std::vector<ProcessTarget> targets;
    ProcessTarget ta, tb;
    check("identity captured for a", capture_process_identity(a.pid(), &ta));
    check("identity captured for b", capture_process_identity(b.pid(), &tb));
    check("identity carries an image path", !ta.imagePath.empty(), std::string());
    check("identity carries a creation time", ta.creationTime != 0);
    targets = {ta, tb};

    UpdateEffectsConfig c = base_config(install, staging);
    c.enumerateTargets = [targets]() { return targets; };
    c.requestStop = [&](const ProcessTarget& t) {
      // Stands in for the real WM_CLOSE / CTRL_BREAK request. These are our own children.
      if (t.pid == a.pid()) { a.kill(); return true; }
      if (t.pid == b.pid()) { b.kill(); return true; }
      return false;
    };"""
assert old in d, 'process stop anchor'
d = d.replace(old, new, 1)

# ---- stubborn process section
old = """    UpdateEffectsConfig c = base_config(install, staging);
    c.quiesceTimeoutMs = 300;  // short, because the point is that it gives up
    c.enumerateTargets = [pid]() { return std::vector<uint32_t>{pid}; };
    c.requestStop = [](uint32_t) { return true; };  // asked, but it does not comply"""
new = """    ProcessTarget target;
    check("stubborn identity captured", capture_process_identity(pid, &target));

    UpdateEffectsConfig c = base_config(install, staging);
    c.quiesceTimeoutMs = 300;  // short, because the point is that it gives up
    c.enumerateTargets = [target]() { return std::vector<ProcessTarget>{target}; };
    c.requestStop = [](const ProcessTarget&) { return true; };  // asked, does not comply"""
assert old in d, 'stubborn anchor'
d = d.replace(old, new, 1)

# ---- cannot-be-asked section
old = """    UpdateEffectsConfig c = base_config(install, staging);
    c.enumerateTargets = []() { return std::vector<uint32_t>{4}; };  // System, never ours
    c.requestStop = [](uint32_t) { return false; };"""
new = """    UpdateEffectsConfig c = base_config(install, staging);
    c.enumerateTargets = []() {
      ProcessTarget t;
      t.pid = 4;  // System. Never ours, and never actually touched -- requestStop refuses first.
      t.creationTime = 1;
      return std::vector<ProcessTarget>{t};
    };
    c.requestStop = [](const ProcessTarget&) { return false; };"""
assert old in d, 'cannot-ask anchor'
d = d.replace(old, new, 1)

io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('signatures patched')
