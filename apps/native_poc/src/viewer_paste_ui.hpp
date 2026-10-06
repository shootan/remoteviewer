#pragma once

// Paste on demand (t-y4wj64jw): what the transfer bar asks of the window procedure's paste state.
//
// Role:    the paste state lives with the key handling in viewer_window_proc.cpp (it decides which
//          keys go to the host and when). The bar is a window of its own in the clip-image library
//          and reaches that state only through these, wired in viewer_clip_image_wiring.cpp.
// Thread:  the viewer's UI thread (the bar polls from its own timer on that thread).

#include "viewer_clip_transfer_bar.hpp"

namespace remote60::native_poc::viewer {

struct ViewerState;

/** The bar's line for paste on demand: waiting for an answer, or why the key was not sent. */
remote60::native_poc::ClipPasteBarView paste_bar_view(ViewerState& ctx);
/** The bar's Cancel while a paste waits. */
void paste_cancel_from_bar(ViewerState& ctx);
/** The bar's Retry after a failed paste: the same gesture again, with the clipboard as it is now. */
void paste_retry_from_bar(ViewerState& ctx);

}  // namespace remote60::native_poc::viewer
