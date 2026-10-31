# -*- coding: utf-8 -*-
import io

p = 'apps/native_poc/src/client_update_ui_test.cpp'
d = io.open(p, encoding='utf-8').read()
B = chr(92)

old = '''    const remote60::native_poc::update::ManifestResult loaded =
        remote60::native_poc::update::load_manifest(
            read_file(manifestPath), read_file(manifest_sig_path(manifestPath)),
            remote60::native_poc::update::trusted_public_key_hex(), "windows");
    if (loaded.outcome != remote60::native_poc::update::ManifestOutcome::Ok) {
      ok(false, "the manifest pinned against did not verify", loaded.detail);
    } else {
      std::string named;
      for (const auto& artifact : loaded.manifest.artifacts()) {
        if (artifact.name == "ui''' + B + B + B + B + '''shell.html") named = artifact.sha256;
      }
      ok(!named.empty() && named == pageSha,
         "the page under test is the one the signed manifest names",
         named.empty() ? std::string("the manifest does not name ui") + "''' + B + B + B + B + '''shell.html"
                       : named.substr(0, 16) + " vs " + pageSha.substr(0, 16));
    }'''

new = '''    // The production verifier, with the key compiled into this build. A manifest that does not
    // verify is not allowed to say what the page should be -- otherwise the pin could be moved by
    // editing a text file.
    namespace upd = remote60::native_poc::update;
    const upd::ManifestResult loaded =
        upd::load_manifest(read_file(manifestPath), read_file(manifest_sig_path(manifestPath)),
                           "windows", upd::default_verifier());
    if (loaded.status != upd::ManifestStatus::Ok || !loaded.manifest.has_value()) {
      ok(false, "the manifest pinned against did not verify", loaded.detail);
    } else {
      std::string named;
      for (const upd::ManifestArtifact& artifact : loaded.manifest->fields().artifacts) {
        if (artifact.name == "ui''' + B + B + '''shell.html") named = artifact.sha256;
      }
      std::printf("pin   %s  %s\\n", narrow(manifestPath).c_str(),
                  loaded.manifest->fields().version.c_str());
      ok(!named.empty() && named == pageSha,
         "the page under test is the one the signed manifest names",
         named.empty() ? std::string("the manifest names no ui shell.html")
                       : named.substr(0, 16) + " vs " + pageSha.substr(0, 16));
    }'''
assert d.count(old) == 1, 'pin fix anchor'
d = d.replace(old, new, 1)

# ManifestStatus enum name check happens at compile time; nothing else to change.
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok')
