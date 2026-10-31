import io

p='apps/native_poc/src/updater_effects.hpp'
d=io.open(p,encoding='utf-8').read()
old='''  const std::string& last_effects_error() const { return lastEffectsError_; }

 private:'''
new='''  const std::string& last_effects_error() const { return lastEffectsError_; }

  /**
   * Seams the assembly left empty on the last run. Empty is the only acceptable value.
   *
   * Exposed rather than merely logged because "production wires everything a test wires" is the
   * question this layer kept answering wrongly, and it should be answerable by an assertion
   * instead of by reading the assembly and hoping.
   */
  const std::vector<std::string>& unwired_seams() const { return unwiredSeams_; }

  /**
   * Backups the commit could not delete, from the last run.
   *
   * They used to leave no trace: the delete result was ignored, so a file that stayed beside the
   * installation was indistinguishable from one that had been removed. A surviving .gnlink-old
   * with nothing to explain it is exactly what this records.
   */
  const std::vector<std::wstring>& orphaned_backups() const { return orphanedBackups_; }

 private:'''
assert old in d,'accessors anchor'
d=d.replace(old,new,1)
old='''  std::string lastEffectsError_;
};'''
new='''  std::string lastEffectsError_;
  std::vector<std::string> unwiredSeams_;
  std::vector<std::wstring> orphanedBackups_;
};'''
assert old in d,'members anchor'
d=d.replace(old,new,1)
io.open(p,'w',encoding='utf-8',newline='').write(d)

p='apps/native_poc/src/updater_effects.cpp'
d=io.open(p,encoding='utf-8').read()
old='''    const std::vector<std::string> missing = config.unwired();
    if (!missing.empty()) {'''
new='''    const std::vector<std::string> missing = config.unwired();
    unwiredSeams_ = missing;
    if (!missing.empty()) {'''
assert old in d,'record unwired'
d=d.replace(old,new,1)
old='''  verifiedVersion_ = *versionToInstall;
  lastEffectsError_ = effects.last_error();
  return outcome;'''
new='''  verifiedVersion_ = *versionToInstall;
  lastEffectsError_ = effects.last_error();
  orphanedBackups_ = effects.orphaned_backups();
  for (const std::wstring& name : orphanedBackups_) {
    deps_.log("a backup could not be removed and is still beside the installation: " +
              to_utf8(name) + ".gnlink-old");
  }
  return outcome;'''
assert old in d,'run tail'
d=d.replace(old,new,1)
io.open(p,'w',encoding='utf-8',newline='').write(d)
print('run() now reports what it left unwired and what it left behind')
