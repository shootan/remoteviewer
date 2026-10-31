// The RAII half of a stop target: one handle, four facts, and a hand-written move.
//
// Its own translation unit for one reason -- so the move can be tested. It lived inside
// update_effects.cpp, which drags in the whole update pipeline, and a test that wanted to move one
// TargetWatch had to link all of it. The move has already been wrong once (it dropped
// requestFailed, and the settle then waited for nobody), which is exactly the kind of thing that
// should be checkable in isolation. See update_identity_match_test.cpp.

#include <windows.h>

#include <utility>

#include "update_effects.hpp"

namespace remote60::native_poc::update {

TargetWatch& TargetWatch::operator=(TargetWatch&& other) noexcept {
  // Self-move has to be a no-op, not a close: `w = std::move(w)` would otherwise shut the handle
  // it is about to keep.
  if (this != &other) {
    if (handle) CloseHandle(static_cast<HANDLE>(handle));
    target = std::move(other.target);
    handle = other.handle;
    gone = other.gone;
    requestDelivered = other.requestDelivered;
    requestFailed = other.requestFailed;  // dropped here once; the settle then waited for nobody
    // Emptied, so exactly one destructor closes the handle.
    other.handle = nullptr;
  }
  return *this;
}

TargetWatch::~TargetWatch() {
  if (handle) CloseHandle(static_cast<HANDLE>(handle));
}

}  // namespace remote60::native_poc::update
