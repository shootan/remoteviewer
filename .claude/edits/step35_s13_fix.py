import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit

ROOT = r'D:\remote\remote'
S = ROOT + r'\apps\native_poc\src'

edit(S + r'\viewer_frame_gate.cpp', [
(r"""void FrameGate::note_reference_sync(const FrameGateInputs& in) {
  gate.resumeAnchorPending = false;  // a decoded keyframe is the anchor; any pending ends here (P11)
  gate.waitForKeyFrame = false;""",
 r"""void FrameGate::note_reference_sync(const FrameGateInputs& in) {
  // Deliberately no resumeAnchorPending change here (P11, 2026-09-07 S13 regression): the
  // receiver calls this for EVERY decoded frame, so clearing pending here ended it the moment the
  // held resume frame itself decoded, and the fresh burst behind it read as an 0.8 s backlog. A
  // fresh keyframe already ends pending in admit (it is the anchor); a held or stamp-unknown
  // keyframe is a held picture like any other and keeps it. Pending's lifetime is decided in
  // admit: fresh anchor, budget, state change, keyframe wait.
  gate.waitForKeyFrame = false;"""),
(r"""  // Lifetime: pending is a Normal-state, same-episode notion. A state change, a keyframe wait or
  // a decoder resync (note_reference_sync) ends it.""",
 r"""  // Lifetime: pending is a Normal-state, same-episode notion. It ends on a fresh anchor (below),
  // the budget, a state change or a keyframe wait -- all decided here. A decoded frame, key or
  // not, does NOT end it (note_reference_sync): a held or stamp-unknown keyframe keeps it."""),
])
edit(S + r'\viewer_frame_gate_state.hpp', [
(r"""  // leaves the anchor PENDING; the first fresh frame after it (hold <= resumeAnchorMaxHoldUs)
  // anchors. Bounded: after resumeAnchorPendingMaxFrames or resumeAnchorPendingMaxUs without a
  // fresh frame -- a genuine backlog holds every frame -- the old rule resumes and the lag trips.""",
 r"""  // leaves the anchor PENDING; the first fresh frame after it (hold <= resumeAnchorMaxHoldUs)
  // anchors. Bounded: after resumeAnchorPendingMaxFrames or resumeAnchorPendingMaxUs without a
  // fresh frame -- a genuine backlog holds every frame -- the old rule resumes and the lag trips.
  // Lifetime is decided in admit only: fresh anchor, budget, state change, keyframe wait. A
  // decoded frame does not end it -- a held or stamp-unknown IDR keeps pending like any held
  // frame (the S13 regression of 2026-09-07 came from ending it on every decode)."""),
])
print('step35 fix ok')
