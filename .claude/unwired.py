import io

p='apps/native_poc/src/update_effects.hpp'
d=io.open(p,encoding='utf-8').read()
old='''  /** False when anything required is missing. Reason goes to `detail` when given. */'''
assert d.count(old)==1
new='''  /**
   * The names of every injected seam that nothing has filled in.
   *
   * Exists because of how the defects in this layer were found -- one at a time, each after a
   * test had set a seam that production never set. A seam nobody wires is not a missing feature
   * that shows up as a failure; it is a step that silently does not happen, which is why the
   * manifest was never fetched and the liveness check never ran. Enumerating them turns "did
   * anyone remember" into a question a caller can actually ask.
   */
  std::vector<std::string> unwired() const;

  /** False when anything required is missing. Reason goes to `detail` when given. */'''
d=d.replace(old,new,1)
io.open(p,'w',encoding='utf-8',newline='').write(d)

p='apps/native_poc/src/update_effects.cpp'
d=io.open(p,encoding='utf-8').read()
i=d.find('bool UpdateEffectsConfig::validate(')
assert i!=-1,'validate def'
impl='''std::vector<std::string> UpdateEffectsConfig::unwired() const {
  // Every std::function on this struct, by name. The list is here rather than at a call site so
  // that adding a seam and forgetting to wire it is one edit away from being reported, instead of
  // depending on someone noticing at the point where it would have been used.
  std::vector<std::string> missing;
  if (!fetchManifest) missing.push_back("fetchManifest");
  if (!fetchArtifact) missing.push_back("fetchArtifact");
  if (!enumerateTargets) missing.push_back("enumerateTargets");
  if (!requestStop) missing.push_back("requestStop");
  if (!captureRegistration) missing.push_back("captureRegistration");
  if (!registerInstall) missing.push_back("registerInstall");
  if (!restoreRegistration) missing.push_back("restoreRegistration");
  if (!relaunch) missing.push_back("relaunch");
  if (!healthCheck) missing.push_back("healthCheck");
  if (!releaseBeforeRollback) missing.push_back("releaseBeforeRollback");
  return missing;
}

'''
d=d[:i]+impl+d[i:]
io.open(p,'w',encoding='utf-8',newline='').write(d)

p='apps/native_poc/src/updater_effects.cpp'
d=io.open(p,encoding='utf-8').read()
old='''  WindowsUpdateEffects effects(config);
  effects.set_installed_version(options_.installedVersion);'''
new='''  // Nothing runs until every seam is filled. This is the check that would have caught the manifest
  // fetch that was never wired and the version that never reached registration: both were
  // assembled, both were exercised through a config a TEST had filled in, and production shipped
  // with the field empty. An unwired seam is not a failure -- it is a step that quietly does not
  // happen, which is far harder to see.
  {
    const std::vector<std::string> missing = config.unwired();
    if (!missing.empty()) {
      std::string names;
      for (const std::string& name : missing) {
        if (!names.empty()) names += ", ";
        names += name;
      }
      deps_.log("refusing to run: these are not wired -- " + names);
      UpdateOutcome broken;
      broken.result = UpdateResult::AbandonedBeforeSwap;
      broken.detail = "the updater was not fully assembled: " + names;
      return broken;
    }
  }

  WindowsUpdateEffects effects(config);
  effects.set_installed_version(options_.installedVersion);'''
assert old in d,'effects construction'
d=d.replace(old,new,1)
io.open(p,'w',encoding='utf-8',newline='').write(d)
print('unwired() plus the refusal in run()')
