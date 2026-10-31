# -*- coding: utf-8 -*-
import io

def edit(path, pairs):
    d = io.open(path, encoding='utf-8').read()
    for i, (old, new) in enumerate(pairs):
        assert d.count(old) == 1, '%s pair %d: %s' % (path, i, old[:70])
        d = d.replace(old, new, 1)
    io.open(path, 'w', encoding='utf-8', newline='').write(d)
    print('ok', path)


# ---------------------------------------------------------------- (5) split the two lists
edit('apps/native_poc/src/update_process_targets.hpp', [
 ('''const std::vector<std::wstring>& product_image_names();''',
  '''const std::vector<std::wstring>& product_image_names();

/**
 * Everything a release replaces: the images above, the updater, and the two data files.
 *
 * Kept separate from product_image_names() because the two lists answer different questions --
 * "what must not be running while we replace it" and "what do we replace" -- and there is exactly
 * one file where the answers differ. GNLinkUpdater.exe has to be REPLACED (otherwise it becomes a
 * permanent maintenance binary frozen at its compile-time constants, which is the trap that ruled
 * out re-running the old installer) but must not be a STOP target: the running updater is a copy
 * outside installDir, so the installed one is not running, and anything else answering to that
 * name is not ours to close.
 *
 * The lists were the same one for a while, on the argument that sharing prevents drift. It did
 * not: GNLinkUpdater.exe was missing from the payload for every release, so the updater has never
 * once been updated by an update. The anti-drift property is kept by DERIVING this from that --
 * the stop list cannot gain an entry this one lacks -- which is the invariant that actually
 * matters. product_payload_list_contract() asserts it.
 */
std::vector<std::wstring> product_payload_names();

/** True when every stop target is also replaced. Nothing may be closed and then left stale. */
bool product_payload_list_contract();'''),
])

edit('apps/native_poc/src/update_process_targets.cpp', [
 ('''std::vector<ProcessTarget> enumerate_product_processes(const std::vector<std::wstring>& imageNames) {''',
  '''std::vector<std::wstring> product_payload_names() {
  std::vector<std::wstring> names = product_image_names();
  // Replaced but never stopped -- see the header. The destination is installDir\\\\GNLinkUpdater.exe
  // while the process runs from the working copy outside it, which is what makes this safe;
  // UpdateEffectsConfig::validate() checks that as full destination paths, not names.
  names.push_back(L"GNLinkUpdater.exe");
  names.push_back(L"ui\\\\shell.html");
  names.push_back(L"ui\\\\macro.html");
  return names;
}

bool product_payload_list_contract() {
  const std::vector<std::wstring> payload = product_payload_names();
  for (const std::wstring& stopped : product_image_names()) {
    bool found = false;
    for (const std::wstring& name : payload) {
      if (_wcsicmp(stopped.c_str(), name.c_str()) == 0) {
        found = true;
        break;
      }
    }
    if (!found) return false;
  }
  return true;
}

std::vector<ProcessTarget> enumerate_product_processes(const std::vector<std::wstring>& imageNames) {'''),
])

edit('apps/native_poc/src/updater_effects.cpp', [
 ('''  // The executables come from the same list the process enumerator uses, so the set that is
  // stopped and the set that is replaced cannot drift apart; the two data files are named
  // alongside them.''',
  '''  // Derived from the list the process enumerator uses, so the set that is stopped cannot gain an
  // entry the set that is replaced lacks. They are not the SAME list: GNLinkUpdater.exe is
  // replaced and not stopped, and while they were one list it was in neither -- so no release has
  // ever updated the updater. See product_payload_names().'''),
 ('''  deps.payloadNames = product_image_names();
  deps.payloadNames.push_back(L"ui\\\\shell.html");
  deps.payloadNames.push_back(L"ui\\\\macro.html");''',
  '''  deps.payloadNames = product_payload_names();'''),
])

# ---------------------------------------------------------------- (6) required set before the disk is touched
edit('apps/native_poc/src/update_effects.cpp', [
 ('''bool WindowsUpdateEffects::VerifyDownload(const ManifestFields& fields) {
  if (stagedReleaseId_.empty() || stagedReleaseId_ != fields.releaseId) {
    lastError_ = "nothing staged for this release";
    return false;
  }
  if (fields.artifacts.empty()) {
    lastError_ = "manifest lists no artifacts";
    return false;
  }
''',
  '''bool WindowsUpdateEffects::VerifyDownload(const ManifestFields& fields) {
  if (stagedReleaseId_.empty() || stagedReleaseId_ != fields.releaseId) {
    lastError_ = "nothing staged for this release";
    return false;
  }
  if (fields.artifacts.empty()) {
    lastError_ = "manifest lists no artifacts";
    return false;
  }

  // Everything this swap will need, checked here rather than at the swap itself.
  //
  // Swap already refuses when a payload file is missing from staging -- but by then the product
  // has been asked to stop and the user is looking at a closed application. 0.2.109 shipped a
  // manifest without GNLinkSetup.exe and that is exactly what happened: download, signature and
  // hashes all passed, and the release died at the swap with the host already going down. The
  // same answer is available here, before anything is asked to close.
  for (const std::wstring& name : config_.payloadNames) {
    bool named = false;
    for (const ManifestArtifact& artifact : fields.artifacts) {
      if (_wcsicmp(widen_name(artifact.name).c_str(), name.c_str()) == 0) {
        named = true;
        break;
      }
    }
    if (!named) {
      lastError_ = "the manifest does not name " + to_utf8(name) + ", which this update replaces";
      return false;
    }
  }
''')
])
