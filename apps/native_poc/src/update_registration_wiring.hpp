#pragma once

// Connecting the updater's Register and Rollback steps to the shared registration unit.
//
// Three callbacks that have to share one piece of state -- the snapshot of what was registered
// before the update -- so they are built together rather than wired up separately. Getting that
// wrong is not a subtle failure: rolling back with the NEW version would leave old files on disk
// while the registry claims the new version, which is exactly the reference-point poisoning that
// ruled out re-running the old installer in the first place.
//
// Nothing here is a fallback. UpdateEffectsConfig's callbacks stay required and injected; this
// just builds the production set, so a test that supplies its own is not fighting a default.
//
// Design: docs/업데이트_기능_설계.md 3.1 (Register, Rollback), and the package-contract condition
// that rollback registration must use the PREVIOUS install's values.

#include <functional>
#include <memory>
#include <string>

#include "install_registration.hpp"

namespace remote60::native_poc::update {

/** The three callbacks UpdateEffectsConfig needs for registration, sharing one snapshot. */
struct RegistrationEffects {
  /** Called before the swap. Records what is registered now, so rollback can put it back. */
  std::function<bool()> capture;
  /** Called at the Register stage, with the version the manifest claims. */
  std::function<bool()> apply;
  /** Called during rollback. Restores the captured values -- NOT the new ones. */
  std::function<bool()> restore;

  /** The last registration result, for the log. Empty until apply() has run. */
  std::function<install::RegistrationResult()> lastResult;
};

/**
 * Builds the production set from a target and the operations it needs.
 *
 * `target.version` is the version being installed -- from a verified manifest, never a constant
 * compiled into whichever binary happens to be registering.
 *
 * The returned callbacks share ownership of the snapshot, so they stay valid as long as any of
 * them does. Calling `restore` before `capture` restores nothing and reports success: there was
 * no previous registration to put back, which is a true statement rather than a failure.
 */
RegistrationEffects make_registration_effects(install::RegistrationTarget target,
                                              install::RegistrationOps ops);

/**
 * The operations a real installation needs: a process runner and a Start menu shortcut writer.
 *
 * `shortcutFolder` empty means the machine's Start menu, which is what production passes. A test
 * passes a directory of its own so that it can drive these exact functions -- real COM, real
 * IPersistFile::Save -- without writing into every user's Start menu.
 */
install::RegistrationOps production_registration_ops(const std::wstring& shortcutFolder = {});

}  // namespace remote60::native_poc::update
