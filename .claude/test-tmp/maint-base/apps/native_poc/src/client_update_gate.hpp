#pragma once

// When the client may ask about updates, and when an answer is still worth showing.
//
// Both questions used to be answered inline in client_shell_main.cpp, which no test links -- so
// neither was ever executed by anything but the product. That is the same shape as the defect
// that let the updater ship for months unable to stop a windowless process: the decision existed,
// and nothing could reach it. These two functions are pure so that the decisions can be driven
// directly.
//
// The client checks once at start-up, before anyone has signed in. At that moment there is no
// session token and often no server address, so the check reports "not configured" -- correctly,
// and then never runs again, because the only call site was that one. Signing in produces exactly
// the two things the check needs and nothing asks again. should_check() exists so the check can
// be invited at every point where those values change, without turning that into a check per
// keystroke or a second popup per sign-in.
//
// may_publish() is the other half. A check is asynchronous; a user can sign out, or sign in as
// somebody else, while one is in flight. The answer that arrives then describes a session that no
// longer exists, and showing it would put another account's update notice on this one's screen.

#include <cstdint>
#include <string>

namespace remote60::native_poc::client {

/** What the gate remembers between calls. One per process. */
struct UpdateGateState {
  bool checked = false;
  std::string owner;
  uint64_t epoch = 0;
};

/**
 * Whether a check should run for this owner and epoch.
 *
 * False when the same (owner, epoch) has already been checked. The epoch moves on every sign-in
 * and sign-out, so signing in again is a new question and gets a new answer, while a second
 * invitation for the same session is ignored -- which is what stops a popup accumulating each
 * time something re-invites the check.
 */
bool should_check(const UpdateGateState& state, const std::string& owner, uint64_t epoch);

/** Records that a check was started for this owner and epoch. */
void note_checked(UpdateGateState* state, const std::string& owner, uint64_t epoch);

/**
 * Whether an answer for `requestOwner`/`requestEpoch` may still be shown.
 *
 * Compared against the values at the moment the answer arrived, not against what the gate last
 * checked: a check may be in flight while another has already been started. Anything that does
 * not match is an answer about a session that has ended.
 */
bool may_publish(const std::string& requestOwner, uint64_t requestEpoch,
                 const std::string& currentOwner, uint64_t currentEpoch);

}  // namespace remote60::native_poc::client
