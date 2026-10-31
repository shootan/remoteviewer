import io

BS = chr(92)

p = 'apps/native_poc/src/update_effects.cpp'
d = io.open(p, encoding='utf-8').read()
d = d.replace('  for (auto it = createdDirs_.rbegin(); it != createdDirs_.rend(); ++it) {\n'
              '    if (false) RemoveDirectoryW(it->c_str());\n  }',
              '  for (auto it = createdDirs_.rbegin(); it != createdDirs_.rend(); ++it) {\n'
              '    RemoveDirectoryW(it->c_str());\n  }', 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)

p = 'apps/native_poc/src/update_release_test.cpp'
d = io.open(p, encoding='utf-8').read()

anchor = '  // ================================================================ 4. a swap that fails part way\n'
block = '''  {
    // The mechanism behind "no mixing", asserted directly rather than through its consequence:
    // two releases staged one after the other occupy separate directories, so the files of one
    // are never where an attempt at the other would look for them.
    remove_tree(staging);
    CreateDirectoryW(staging.c_str(), nullptr);

    Server serverA;
    serverA.current = &a;
    UpdateEffectsConfig ca = base_config(&serverA);
    WindowsUpdateEffects ea(ca);
    check("R3: release A stages", ea.Download(fields_for(a)), ea.last_error());

    Server serverB;
    serverB.current = &b;
    UpdateEffectsConfig cb = base_config(&serverB);
    WindowsUpdateEffects eb(cb);
    check("R3: release B stages", eb.Download(fields_for(b)), eb.last_error());

    const auto staged = snapshot(staging);
    int dirsSeen = 0;
    for (const auto& entry : staged) {
      if (entry.first.find(L"@@") != std::wstring::npos) ++dirsSeen;
    }
    check("R3: both releases are staged, six files across two directories", staged.size() == 6,
          std::to_string(staged.size()));
    check("R3: every staged file sits under a release-named directory", dirsSeen == 6,
          std::to_string(dirsSeen));
    // The claim that matters: B's download did not land on top of A's files.
    bool aIntact = false;
    for (const auto& entry : staged) {
      if (entry.second == hostBytes) aIntact = true;
    }
    check("R3: release A's staged bytes were not overwritten by release B's download", aIntact);

    ea.DiscardDownload();
    eb.DiscardDownload();
  }

'''
block = block.replace('@@', BS * 2)
assert anchor in d, 'R4 section anchor'
d = d.replace(anchor, block + anchor, 1)

# a small helper the block above needs
helper_anchor = '  const auto accept = [](const std::string&, const std::vector<uint8_t>&) { return true; };\n'
helper = '''  /** The parsed shape of a release's manifest, for the cases that drive the effects directly. */
  const auto fields_for = [&](const Release& r) {
    ManifestFields f;
    f.schema = 2;
    f.releaseId = r.id;
    f.platform = "windows";
    f.arch = "x64";
    f.version = r.version;
    for (const Body& body : r.bodies) {
      ManifestArtifact art;
      art.name = body.name;
      art.size = body.bytes.size();
      art.sha256 = sha256_of(body.bytes);
      art.url = "https://updates.example/" + r.version + "/" + body.name;
      f.artifacts.push_back(art);
    }
    return f;
  };

'''
assert helper_anchor in d, 'accept anchor'
d = d.replace(helper_anchor, helper_anchor + '\n' + helper, 1)

io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('staging keying asserted')
