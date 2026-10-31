import io

BS = chr(92)
p = 'apps/native_poc/src/updater_assembly_test.cpp'
d = io.open(p, encoding='utf-8').read()

old = ('    manifest = "schema=2' + BS + 'nreleaseId=r-0.2.105' + BS + 'nplatform=windows' + BS +
       'narch=x64' + BS + 'nversion=0.2.105' + BS + 'n";')
new = ('    // 99.0.0 rather than a real release number, deliberately: the assertion below is that\n'
       '    // the version came from the manifest and NOT from kProductVersion, and it can only say\n'
       '    // that while the two differ. Pinned to 0.2.105 it passed until the build was bumped to\n'
       '    // 0.2.105 -- at which point it went red for the right reason, having until then been\n'
       '    // checking a moving target against a literal.\n'
       '    manifest = "schema=2' + BS + 'nreleaseId=r-99.0.0' + BS + 'nplatform=windows' + BS +
       'narch=x64' + BS + 'nversion=99.0.0' + BS + 'n";')
assert d.count(old) == 1, 'manifest literal'
d = d.replace(old, new, 1)

pairs = [
 ('''    check("the version the manifest carried is the one that was verified",
          effects.verified_version() == "0.2.105", effects.verified_version());
    check("and it is what the health check was told to expect",
          rec.healthExpectedVersion == "0.2.105", rec.healthExpectedVersion);''',
  '''    check("the version the manifest carried is the one that was verified",
          effects.verified_version() == "99.0.0", effects.verified_version());
    check("and it is what the health check was told to expect",
          rec.healthExpectedVersion == "99.0.0", rec.healthExpectedVersion);'''),
 ('''    // health check used whatever version the updater binary was compiled as. The manifest says
    // 0.2.105 and the installed version is 0.2.104; neither is this binary's kProductVersion.''',
  '''    // health check used whatever version the updater binary was compiled as. The manifest says
    // 99.0.0 and the installed version is 0.2.104; neither can ever be this binary's
    // kProductVersion, which is what makes the next two checks mean anything.'''),
]
for o, n in pairs:
    assert d.count(o) == 1, o[:60]
    d = d.replace(o, n, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok')
