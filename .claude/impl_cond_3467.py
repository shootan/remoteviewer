import io

p = 'apps/native_poc/src/update_effects.cpp'
d = io.open(p, encoding='utf-8').read()

# ---- validate(): the updater must not be able to replace itself (condition 6)
old = """  // Staging inside the directory being replaced would make the updater's own working files part
  // of the swap. Design 3.2 puts it alongside instead."""
new = """  // The updater replacing itself mid-swap would move the running executable aside and then fail
  // to put anything back. It lives outside installDir by design (3.2), but design is not a
  // guarantee -- a config that named its own image would sail straight into it, so this checks.
  if (!updaterImagePath.empty()) {
    const size_t slash = updaterImagePath.find_last_of(L"\\\\/");
    const std::wstring updaterName =
        (slash == std::wstring::npos) ? updaterImagePath : updaterImagePath.substr(slash + 1);
    for (const std::wstring& name : payloadNames) {
      if (_wcsicmp(name.c_str(), updaterName.c_str()) == 0) {
        return fail("payloadNames includes the running updater");
      }
    }
    // And the updater's own file must not sit inside the directory being replaced.
    if (updaterImagePath.size() > installDir.size() &&
        _wcsnicmp(updaterImagePath.c_str(), installDir.c_str(), installDir.size()) == 0 &&
        (updaterImagePath[installDir.size()] == L'\\\\' ||
         updaterImagePath[installDir.size()] == L'/')) {
      return fail("the updater is inside the directory it would replace");
    }
  }

  // Staging inside the directory being replaced would make the updater's own working files part
  // of the swap. Design 3.2 puts it alongside instead."""
assert old in d, 'validate anchor'
d = d.replace(old, new, 1)

# ---- VerifyDownload: version consistency (condition 4)
old = """  if (actual != fields.sha256) {
    lastError_ = "sha256 mismatch";
    return false;
  }
  return true;
}"""
new = """  if (actual != fields.sha256) {
    lastError_ = "sha256 mismatch";
    return false;
  }

  // The hash pins WHICH bytes arrived; this asks whether those bytes agree with what the manifest
  // says they are. The realistic failure it catches is a manifest paired with the wrong artifact
  // -- a publishing mistake rather than an attack, since an attacker who could choose the bytes
  // would have had to defeat the signature first.
  if (!config_.expectedVersion.empty()) {
    if (!file_contains_utf16_version(staged_artifact_path(), config_.expectedVersion)) {
      lastError_ = "staged artifact does not carry version " + config_.expectedVersion;
      return false;
    }
    // The manifest's own version must be the one we were told to expect, or two sources disagree
    // about what is being installed and neither is obviously right.
    if (!fields.version.empty() && fields.version != config_.expectedVersion) {
      lastError_ = "manifest version " + fields.version + " does not match the expected " +
                   config_.expectedVersion;
      return false;
    }
  }
  return true;
}"""
assert old in d, 'verify anchor'
d = d.replace(old, new, 1)

# ---- the UTF-16 scan itself
old = """bool file_size_bytes(const std::wstring& path, uint64_t* out) {"""
new = """bool file_contains_utf16_version(const std::wstring& path, const std::string& version) {
  if (version.empty()) return false;
  // The needle is the version as UTF-16LE, which is how a wide string literal sits in a PE image.
  std::string needle;
  needle.reserve(version.size() * 2);
  for (char c : version) {
    needle.push_back(c);
    needle.push_back('\\0');
  }

  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;

  // Read in chunks with an overlap, so a needle straddling a boundary is still found.
  const size_t chunk = 1024 * 1024;
  const size_t overlap = needle.size();
  std::string buffer;
  buffer.resize(chunk + overlap);
  size_t carried = 0;
  bool found = false;
  for (;;) {
    DWORD read = 0;
    if (!ReadFile(file, buffer.data() + carried, static_cast<DWORD>(chunk), &read, nullptr)) break;
    if (read == 0) break;
    const size_t have = carried + read;
    if (buffer.find(needle, 0) != std::string::npos && buffer.find(needle) < have) {
      found = true;
      break;
    }
    carried = (have >= overlap) ? overlap : have;
    if (carried > 0) {
      std::memmove(buffer.data(), buffer.data() + have - carried, carried);
    }
  }
  CloseHandle(file);
  return found;
}

bool file_size_bytes(const std::wstring& path, uint64_t* out) {"""
assert old in d, 'file_size anchor'
d = d.replace(old, new, 1)

io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('effects cpp updated')
