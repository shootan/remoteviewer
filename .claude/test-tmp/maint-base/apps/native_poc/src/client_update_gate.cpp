#include "client_update_gate.hpp"

namespace remote60::native_poc::client {

bool should_check(const UpdateGateState& state, const std::string& owner, uint64_t epoch) {
  if (!state.checked) return true;
  // The epoch alone would be enough today -- it is bumped on every sign-in and sign-out -- but the
  // owner is compared too, because a counter that is reset or that wraps would otherwise make two
  // different accounts look like the same question.
  return !(state.owner == owner && state.epoch == epoch);
}

void note_checked(UpdateGateState* state, const std::string& owner, uint64_t epoch) {
  if (!state) return;
  state->checked = true;
  state->owner = owner;
  state->epoch = epoch;
}

bool may_publish(const std::string& requestOwner, uint64_t requestEpoch,
                 const std::string& currentOwner, uint64_t currentEpoch) {
  return requestOwner == currentOwner && requestEpoch == currentEpoch;
}

}  // namespace remote60::native_poc::client
