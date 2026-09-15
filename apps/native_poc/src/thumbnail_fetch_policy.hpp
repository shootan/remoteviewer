#pragma once

// When a client should ask the host for a window preview again.
//
// Role:    one predicate, thumbnail_fetch_due, shared by the two clients that queue preview
//          fetches -- the Windows viewer's picker and the session controller the Android client
//          drives.
// Thread:  pure function over its arguments.
// Input:   what we already have for a window, what the last attempt did, and now.
// Output:  whether to queue another fetch.
// Callers: viewer_picker.cpp (queue_thumbnail_fetches_from_panel),
//          native_video_client_session.cpp (QueueThumbnailFetchesFromPanel).
//
// It exists because both callers had the same bug and would have needed the same fix twice.
//
// Each of them throttled on the CACHED PREVIEW: a preview newer than five seconds was left alone.
// A preview that never arrived is not in that cache, so it fell past the throttle and was queued
// again on every single panel refresh. A window whose preview cannot be produced -- the one the
// host is deliberately skipping after it stopped answering WM_PRINT -- was therefore asked about
// forever, at whatever rate the picker happened to refresh.
//
// The cache answers "how fresh is what I have". It cannot answer "when did I last ask", and those
// come apart exactly when asking does not work. So the attempt is tracked separately, and a failed
// attempt is throttled on the host's cooldown rather than on the refresh interval.

#include <cstdint>

namespace remote60::native_poc {

struct ThumbnailFetchState {
  // What is cached. previewFetchedUs is only meaningful when havePreview is true.
  bool havePreview = false;
  uint64_t previewFetchedUs = 0;
  // What the last completed exchange did, whether or not it produced pixels. A request that never
  // completed (the socket dropped) is not an attempt: the session is going away with it.
  bool attempted = false;
  uint64_t lastAttemptUs = 0;
  bool lastAttemptFailed = false;
};

struct ThumbnailFetchPolicy {
  // How stale a preview that works may get before it is refreshed. Unchanged from what the two
  // callers already did.
  uint64_t refreshUs = 5ull * 1000 * 1000;
  // How long to leave a window alone after a preview did not come back. Matches the host's
  // cooldown on purpose: asking during it can only produce the same skip response, and the point
  // is to stop spending a control-thread roundtrip per panel refresh to be told so.
  uint64_t retryAfterFailureUs = 60ull * 1000 * 1000;
};

inline bool thumbnail_fetch_due(const ThumbnailFetchState& state,
                                const ThumbnailFetchPolicy& policy, uint64_t nowUs) {
  // Checked before the cache, deliberately. A window can have an old preview AND a recent failure
  // -- it worked once and then stopped answering -- and in that case the failure is the newer
  // information. Testing the cache first would refresh it right back into the host's cooldown.
  if (state.attempted && state.lastAttemptFailed) {
    if (nowUs - state.lastAttemptUs < policy.retryAfterFailureUs) return false;
    return true;
  }
  if (state.havePreview && nowUs - state.previewFetchedUs < policy.refreshUs) return false;
  return true;
}

}  // namespace remote60::native_poc
