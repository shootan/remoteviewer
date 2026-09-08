#pragma once

// How the updater assembles everything else — the layer where five defects lived undetected.
//
// It used to be a class inside updater_main.cpp, which is linked into the product binary and
// nothing else. That meant nobody could drive it, and nobody did: set_manifest had no caller, the
// verified version was never assigned, and process identities were captured after the point at
// which they had already gone. All three were assembly mistakes -- every part they connect was
// correct and separately tested. The suites were green while the feature could not run.
//
// So the assembly is here, in a translation unit a test links, and everything effectful it needs
// arrives as UpdaterDeps. The point is not that the dependencies are swappable; it is that
// "does the updater wire this up correctly" becomes a question something can ask.
//
// The trusted key is one of those dependencies. Production passes default_verifier(); a test
// passes a verifier for a throwaway key, so a signed fixture can be exercised end to end WITHOUT
// the product's trust anchor being altered or a build flag changing what production trusts.
//
// Design: docs/업데이트_배선_계획.md W1, docs/업데이트_기능_설계.md 3.1-3.6.

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "install_registration.hpp"
#include "update_effects.hpp"
#include "update_relaunch.hpp"
#include "update_state_machine.hpp"
#include "updater_options.hpp"

namespace remote60::native_poc::update {

/** Everything the assembly needs from the outside world. Nothing here has a default. */
struct UpdaterDeps {
  /** Fetches a small text document. Production is WinHTTP; a test serves from memory. */
  std::function<bool(const std::string& url, size_t maxBytes, std::string* body,
                     std::string* error)>
      fetchText;
  /** Streams one artifact to a path, refusing anything larger than `maxBytes`. */
  std::function<bool(const std::string& url, const std::wstring& destPath, uint64_t maxBytes,
                     std::string* error)>
      fetchFile;
  /** The product processes that are running. Production enumerates by image name. */
  std::function<std::vector<ProcessTarget>()> enumerateTargets;
  /** Asks one to close. Never forces. */
  std::function<bool(const ProcessTarget&)> requestStop;
  /** Service, firewall and shortcut operations. A test records rather than performs. */
  install::RegistrationOps registrationOps;
  /** Builds the relaunch effects. Injected so a test can substitute dummies for real images. */
  std::function<RelaunchEffects(const RelaunchConfig&, const std::vector<ProcessTarget>&)>
      makeRelaunch;
  /** Checks the manifest signature. Production uses the compiled-in trusted key. */
  SignatureVerifier verifier;
  /** Releases a caller waiting to exit. Called at exactly one point; see UpdaterEffects::run. */
  std::function<void(const std::wstring& eventName)> signalReady;
  /** Where log lines go. */
  std::function<void(const std::string&)> log;
  /** This process's own image, so the swap can refuse to replace it. */
  std::wstring selfImagePath;

  /** False when anything required is missing. */
  bool validate(std::string* detail = nullptr) const;
};

/** The production set: WinHTTP, the real enumerator, the real SCM, the compiled trusted key. */
UpdaterDeps production_updater_deps(std::function<void(const std::string&)> log);

/**
 * Assembles the effects from options and dependencies, and runs one update.
 *
 * Kept as a class only because the assembly happens in two stages: what can be known before the
 * manifest, and what cannot be known until it has been verified.
 */
class UpdaterEffects {
 public:
  UpdaterEffects(UpdaterOptions options, UpdaterDeps deps);

  /** Builds what does not depend on the manifest. False when the dependencies are incomplete. */
  bool build(std::string* detail);

  /** Runs one attempt. */
  UpdateOutcome run(const std::string& platform);

  /** What to tell the user when something did not come back. Empty when everything did. */
  const std::string& user_notice() const { return userNotice_; }

  /**
   * The version this attempt verified, or empty when it never got that far.
   *
   * Exposed for the same reason it exists: a caller -- or a test -- must be able to see that the
   * version which reached registration came from the manifest and not from this binary.
   */
  const std::string& verified_version() const { return verifiedVersion_; }

 private:
  UpdaterOptions options_;
  UpdaterDeps deps_;
  UpdateEffectsConfig config_;
  install::RegistrationTarget registrationTarget_;
  std::string userNotice_;
  std::string verifiedVersion_;
};

/**
 * The order in which a working copy is made and started.
 *
 * Extracted with its steps injected because the ORDER is the property that matters and nothing
 * could observe it: the directory has to be checked before it is created, created before anything
 * is copied into it, and the copy has to exist before it is launched. A check that ran after the
 * copy would be reporting on a directory that had already received an executable.
 */
struct WorkingCopySteps {
  /** Returns false to refuse. Called first, always. */
  std::function<bool()> checkDirectory;
  std::function<bool()> createDirectory;
  std::function<bool()> copySelf;
  /** Returns false when the child could not be started safely. */
  std::function<bool()> launch;
};

enum class WorkingCopyResult { Started, RefusedDirectory, CreateFailed, CopyFailed, LaunchFailed };

const char* working_copy_result_name(WorkingCopyResult result);

/** Runs the steps in the one order that is safe, stopping at the first refusal. */
WorkingCopyResult prepare_working_copy(const WorkingCopySteps& steps);

}  // namespace remote60::native_poc::update
