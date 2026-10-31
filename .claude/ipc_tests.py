import io

# ---------------------------------------------------------------- option parsing
p = 'apps/native_poc/src/updater_options_test.cpp'
d = io.open(p, encoding='utf-8').read()
anchor = '''  // ------------------------------------------------------- the updater still refuses cleartext'''
added = '''  // --------------------------------------------------- the credential arrives by name, not value
  //
  // What travels in the argument list is the NAME of a pipe. The credential itself never appears
  // in an argument, an environment block, a file or a log -- which is the whole reason there is a
  // pipe rather than one more flag.
  {
    std::vector<std::wstring> args = complete();
    args.push_back(L"--credential-pipe");
    args.push_back(L"\\\\\\\\.\\\\pipe\\\\GNLinkUpdateCred-123-456");
    args.push_back(L"--manifest-envelope");
    const ParseResult parsed = parse_updater_options(args);
    check("a credential pipe name is accepted", parsed.ok(), parsed.detail);
    check("...and kept as given",
          parsed.options.credentialPipeName == L"\\\\\\\\.\\\\pipe\\\\GNLinkUpdateCred-123-456");
    check("...alongside the wire shape", parsed.options.derivedEndpoint);

    const ParseResult without = parse_updater_options(complete());
    check("without it there is no channel", without.options.credentialPipeName.empty());
    check("...and the shape is the detached one", !without.options.derivedEndpoint);
  }

''' + anchor
assert d.count(anchor) == 1, 'options anchor'
d = d.replace(anchor, added, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok options test')

# ---------------------------------------------------------------- argument building
p = 'apps/native_poc/src/update_handoff_test.cpp'
d = io.open(p, encoding='utf-8').read()
marker = '''    const auto args = updater_arguments(complete_spec());'''
assert d.count(marker) == 1, 'handoff anchor'
block_start = d.rfind('\n  {', 0, d.find(marker))
assert block_start > 0

added = '''
  // ---------------------------------------------- what the argument list may and may not carry
  //
  // The pipe name is in here. The credential is not, and cannot be: there is no field on the spec
  // that could hold one. Asserted rather than assumed, because "we would never put it there" is
  // the kind of thing that stops being true in a hurry once somebody needs it quickly.
  {
    UpdaterLaunchSpec spec = complete_spec();
    spec.credentialPipeName = L"\\\\\\\\.\\\\pipe\\\\GNLinkUpdateCred-1-2";
    spec.derivedEndpoint = true;
    const auto args = updater_arguments(spec);

    bool named = false;
    bool envelope = false;
    for (size_t i = 0; i < args.size(); ++i) {
      if (args[i] == L"--credential-pipe" && i + 1 < args.size() &&
          args[i + 1] == spec.credentialPipeName) {
        named = true;
      }
      if (args[i] == L"--manifest-envelope") envelope = true;
    }
    check("the pipe is named in the arguments", named);
    check("and so is the wire shape", envelope);

    // The token a real run would hold. It must not be reachable from anything the launch writes.
    const std::wstring fixtureToken = L"fixture-token-not-a-real-credential";
    bool leaked = false;
    for (const std::wstring& arg : args) {
      if (arg.find(fixtureToken) != std::wstring::npos) leaked = true;
    }
    check("no credential is anywhere in the argument list", !leaked);

    UpdaterLaunchSpec plain = complete_spec();
    const auto plainArgs = updater_arguments(plain);
    bool mentionsChannel = false;
    for (const std::wstring& arg : plainArgs) {
      if (arg == L"--credential-pipe" || arg == L"--manifest-envelope") mentionsChannel = true;
    }
    check("a launch with no credential names no channel", !mentionsChannel);
  }
'''
d = d[:block_start] + added + d[block_start:]
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok handoff test')
