import io

p = 'apps/native_poc/src/update_effects_test.cpp'
d = io.open(p, encoding='utf-8').read()

anchor = '  remove_tree(install);\n\n  std::cout << "\\n" << (gFailures == 0 ? "RESULT: ALL PASS"'
assert anchor in d, 'tail anchor'

block = '''  // ---------------------------------------------------------------- the manifest has to arrive
  //
  // This was the gap that made everything downstream unreachable in production: FetchManifest
  // returned whatever set_manifest had been given, set_manifest had no production caller, and so
  // every real run ended at the first step with "no manifest available". Every later stage was
  // correct and never executed. What was missing was not a check but a WIRE, and nothing tested
  // that the wire existed.

  {
    // No document injected and no fetcher: the honest failure, unchanged.
    UpdateEffectsConfig c = base_config(install, staging);
    WindowsUpdateEffects e(c);
    std::string doc;
    std::string sig;
    check("with neither a document nor a fetcher, FetchManifest fails",
          !e.FetchManifest(&doc, &sig));
    check("and says why", e.last_error().find("no manifest") != std::string::npos, e.last_error());
  }

  {
    // A fetcher, and nothing injected. This is the production shape.
    UpdateEffectsConfig c = base_config(install, staging);
    int fetches = 0;
    c.fetchManifest = [&fetches](std::string* document, std::string* signatureHex) {
      ++fetches;
      *document = "schema=2@nreleaseId=r-1@nplatform=windows@narch=x64@nversion=9.9.9@n";
      *signatureHex = std::string(128, 'a');
      return true;
    };
    WindowsUpdateEffects e(c);
    std::string doc;
    std::string sig;
    check("a configured fetcher supplies the manifest", e.FetchManifest(&doc, &sig),
          e.last_error());
    check("and the document is the one it fetched",
          doc.find("version=9.9.9") != std::string::npos, doc);
    check("and the signature too", sig == std::string(128, 'a'));

    // Fetched once per attempt, not once per stage. Re-fetching would let the server change what
    // is being installed part way through -- the hazard releaseId exists for, one level up.
    std::string again;
    std::string againSig;
    check("a second call does not go back to the server", e.FetchManifest(&again, &againSig));
    check("and returns the same bytes", again == doc && againSig == sig);
    check("the fetcher ran exactly once", fetches == 1, std::to_string(fetches));
  }

  {
    // A fetcher that fails is a failed attempt, not a silent one.
    UpdateEffectsConfig c = base_config(install, staging);
    c.fetchManifest = [](std::string*, std::string*) { return false; };
    WindowsUpdateEffects e(c);
    std::string doc;
    std::string sig;
    check("a fetcher that fails fails the step", !e.FetchManifest(&doc, &sig));
    check("and the reason names the fetch",
          e.last_error().find("fetch") != std::string::npos, e.last_error());
  }

  {
    // An injected document wins, so a test that supplies one never reaches the network even when
    // a fetcher is also configured.
    UpdateEffectsConfig c = base_config(install, staging);
    bool fetcherRan = false;
    c.fetchManifest = [&fetcherRan](std::string*, std::string*) {
      fetcherRan = true;
      return true;
    };
    WindowsUpdateEffects e(c);
    e.set_manifest("schema=2@nversion=0.0.1@n", std::string(128, 'b'));
    std::string doc;
    std::string sig;
    check("an injected document is used", e.FetchManifest(&doc, &sig));
    check("and the fetcher is not called", !fetcherRan);
  }

'''
block = block.replace('@n', chr(92) + 'n')
d = d.replace(anchor, block + anchor, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('manifest wire regression added')
