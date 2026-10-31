import io

BS = chr(92)
p = 'apps/native_poc/src/update_effects_test.cpp'
d = io.open(p, encoding='utf-8').read()

old = '''  {
    wchar_t self[MAX_PATH]{};
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    const std::wstring selfPath = self;
    const size_t slash = selfPath.find_last_of(L"@@/");
    const std::wstring selfName = selfPath.substr(slash + 1);

    UpdateEffectsConfig c = base_config(install, staging);
    c.payloadNames = {L"AlphaPayload.bin", selfName};
    std::string why;
    check("naming the running updater as payload is refused", !c.validate(&why), why);
    check("and the reason says so", why.find("updater") != std::string::npos, why);
  }'''.replace('@@', BS * 2)

new = '''  {
    wchar_t self[MAX_PATH]{};
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    const std::wstring selfPath = self;
    const size_t slash = selfPath.find_last_of(L"@@/");
    const std::wstring selfDir = selfPath.substr(0, slash);
    const std::wstring selfName = selfPath.substr(slash + 1);

    {
      // A payload whose DESTINATION is the running image. This is the thing that must not happen:
      // the swap would move the running executable aside and have nothing to put back.
      UpdateEffectsConfig c = base_config(selfDir, staging);
      c.payloadNames = {L"AlphaPayload.bin", selfName};
      std::string why;
      check("a payload destination that is the running updater is refused", !c.validate(&why),
            why);
      check("and the reason says so", why.find("updater") != std::string::npos, why);
    }
    {
      // The same NAME, a different directory -- and this must be ALLOWED. It is how the updater
      // stays updatable: it runs from a copy of itself outside installDir while the installed
      // copy is replaced like any other file. A name comparison refuses this, and refusing it
      // would leave a second permanent maintenance binary frozen at its compile-time constants
      // (the trap that ruled out option (C) for registration, history #440).
      UpdateEffectsConfig c = base_config(install, staging);
      c.payloadNames = {L"AlphaPayload.bin", selfName};
      c.updaterImagePath = selfPath;  // outside `install`, which is a temp directory
      std::string why;
      check("replacing an installed copy while running from elsewhere is allowed", c.validate(&why),
            why);
    }
    {
      // Defence in depth: even with a different name, a destination equal to the running image
      // is refused. Contrived, but it is the property being asserted rather than the spelling.
      UpdateEffectsConfig c = base_config(selfDir, staging);
      c.payloadNames = {selfName};
      c.updaterImagePath = selfDir + L"@@" + selfName;
      std::string why;
      check("the comparison is on the whole path, not the name alone", !c.validate(&why), why);
    }
  }'''.replace('@@', BS * 2)

assert old in d, 'updater self-replace block'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('validate cases rewritten')
