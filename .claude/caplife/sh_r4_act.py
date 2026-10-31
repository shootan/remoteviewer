base = 'C:/work/gnlink-wt/apk-prelogin-select/apps/android_direct_client/app/src/'

def edit(rel, pairs):
    p = base + rel
    s = open(p, 'rb').read().decode('utf-8')
    crlf = '\r\n' in s
    s = s.replace('\r\n', '\n')
    for old, new in pairs:
        assert s.count(old) == 1, (rel, s.count(old), old[:90])
        s = s.replace(old, new)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'wb').write(s.encode('utf-8'))

M = 'main/java/com/remote60/androiddirect/MainActivity.kt'
edit(M, [
    # the latch: a cleared swap before any frame is not a frame; the shown layout is what the renderer had swapped
    ('''        val surfaceId = sharpenPolicy.surfaceId ?: 0L
        val timestamp = surface.timestamp
''', '''        val surfaceId = sharpenPolicy.surfaceId ?: 0L
        val timestamp = surface.timestamp
        // sh-strength r4 (N1 c): the layout the view now shows -- the one the renderer's last video swap was drawn with
        // (a cleared swap names none). One buffer of slack: a swap that lands between this latch and the read is
        // credited a frame early (stated limit).
        sharpenRenderer?.let { sharpenShownPlacementGen = it.swappedPlacementGen.get() }
        if (timestamp == SharpenRenderer.CLEAR_SWAP_TIMESTAMP) return  // r4: a cleared swap before any frame
'''),
    # placement sent with a generation
    ('''    private var sharpenPlacementSent: DisplayPlacement? = null
''', '''    private var sharpenPlacementSent: DisplayPlacement? = null
    /** r4 (N1 c): the generation of the placement last sent to the renderer, and the one the view has shown. A remote
     *  touch starting while they differ (B) is held: the screen still shows the old layout, the mapping the new. */
    private var sharpenPlacementGenSent = 0L
    private var sharpenShownPlacementGen = 0L
    private var sharpenInputHeldGen = -1L

    /** r4 (N1 c): B is drawing a layout the view has not shown yet. */
    private fun sharpenLayoutNotShown(): Boolean =
        sharpenPlacementSent != null && sharpenDisplayPassActive() && sharpenShownPlacementGen != sharpenPlacementGenSent
'''),
    ('''            if (place != sharpenPlacementSent) {  // r3 (N2): the same placement is not sent again
                sharpenPlacementSent = place
                sharpenRenderer?.setDisplayPlacement(place)
            }''', '''            if (place != sharpenPlacementSent) {  // r3 (N2): the same placement is not sent again
                sharpenPlacementSent = place
                sharpenPlacementGenSent += 1  // r4 (N1 c): named, so its display can be told
                sharpenRenderer?.setDisplayPlacement(place, sharpenPlacementGenSent)
            }'''),
    # a new renderer: nothing of its layout shown yet
    ('''        sharpenPlacementSent = null  // r3: a new renderer has no placement yet''',
     '''        sharpenPlacementSent = null  // r3: a new renderer has no placement yet
        sharpenShownPlacementGen = 0L  // r4: and nothing of it shown'''),
    # the fence at a remote touch's start
    ('''        if (event.actionMasked == MotionEvent.ACTION_DOWN) {
            showViewerControls()
        }
''', '''        if (event.actionMasked == MotionEvent.ACTION_DOWN) {
            showViewerControls()
            // sh-strength r4 (N1 c): the picture on screen is still the old layout while the mapping is the new one --
            // this touch is not sent (no coordinate of either layout); the next one, once shown, is.
            if (sharpenLayoutNotShown()) {
                if (sharpenInputHeldGen != sharpenPlacementGenSent) {
                    sharpenInputHeldGen = sharpenPlacementGenSent
                    diagnosticsLog.log("input_held_layout", "sent=$sharpenPlacementGenSent shown=$sharpenShownPlacementGen")
                }
                return true
            }
        }
'''),
])

S = 'shseam/java/com/remote60/androiddirect/SharpenTestSeam.kt'
edit(S, [
    (''' *   --ef shTestStrength <s>''', ''' *   --es shTestFault layout:<n>[@<k>]                    sh-strength r4: the output size check lies n times (-1:
 *                                                         always), from the k-th placement change (default 1)
 *   --es shTestFault holdplace:<ms>                       r4: the renderer takes a placement this late
 *   --ef shTestStrength <s>'''),
    ('''            holdReleaseMs = if (fault.startsWith("hold:")) fault.removePrefix("hold:").toLongOrNull() ?: 0L else 0L,
        )''', '''            holdReleaseMs = if (fault.startsWith("hold:")) fault.removePrefix("hold:").toLongOrNull() ?: 0L else 0L,
            layoutMismatchQueries = if (fault.startsWith("layout:")) fault.removePrefix("layout:").substringBefore('@').toIntOrNull() ?: 0 else 0,
            layoutMismatchFromPlacement = if (fault.startsWith("layout:") && '@' in fault) fault.substringAfter('@').toIntOrNull() ?: 1 else 1,
            holdPlacementMs = if (fault.startsWith("holdplace:")) fault.removePrefix("holdplace:").toLongOrNull() ?: 0L else 0L,
        )'''),
])
print('ok')
