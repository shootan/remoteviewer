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

edit('noseam/java/com/remote60/androiddirect/SharpenTestSeam.kt', [
    ('''    fun inputLine(kind: Int, x: Int, y: Int, buttons: Int, queued: Boolean): String? = null''',
     '''    fun inputLine(kind: Int, x: Int, y: Int, buttons: Int, queued: Boolean): String? = null

    /** sh-strength r2 B: the display-size pass is an A/B experiment of test builds only -- never here. */
    fun displayPassFrom(@Suppress("UNUSED_PARAMETER") intent: Intent?): Int = 0'''),
])

edit('shseam/java/com/remote60/androiddirect/SharpenTestSeam.kt', [
    ('''    /** r2 H5.1: what a touch became on the wire (video pixels), for the harness. */''',
     '''    /**
     * sh-strength r2 B (Codex §4B A/B): `--ez shTestDisplayPass true` draws the sharpening pass at the VIEW's pixel
     * size with the viewport placement (enlarge + sharpen in one draw); `--ez shTestDisplayPassDouble true` is its
     * negative control -- the same, with the TextureView's own placement transform left on (applied twice).
     * 0 = A (off), 1 = B, 2 = B with the double transform.
     */
    fun displayPassFrom(intent: Intent?): Int = when {
        intent?.getBooleanExtra("shTestDisplayPassDouble", false) == true -> 2
        intent?.getBooleanExtra("shTestDisplayPass", false) == true -> 1
        else -> 0
    }

    /** r2 H5.1: what a touch became on the wire (video pixels), for the harness. */'''),
])

edit('main/java/com/remote60/androiddirect/MainActivity.kt', [
    ('''    private val sharpenFaults: SharpenRenderer.SharpenFaults by lazy { SharpenTestSeam.faultsFrom(intent) }''',
     '''    private val sharpenFaults: SharpenRenderer.SharpenFaults by lazy { SharpenTestSeam.faultsFrom(intent) }
    /** sh-strength r2 B: the display-size pass (test builds only; the shipped seam always answers 0 = A). */
    private val sharpenDisplayPass: Int by lazy { SharpenTestSeam.displayPassFrom(intent) }
    private var sharpenDisplayPlacementLogged = ""

    /** B is in effect while the sharpening renderer is the TextureView's producer (starting or live). */
    private fun sharpenDisplayPassActive(): Boolean =
        sharpenDisplayPass != 0 &&
            (sharpenPolicy.path == SharpenPathPolicy.Path.STARTING || sharpenPolicy.path == SharpenPathPolicy.Path.LIVE) &&
            videoTextureView.width > 0 && videoTextureView.height > 0'''),
    ('''    private fun resolveSurfaceBufferWidth(): Int {
        if (videoWidth > 0) return videoWidth''', '''    private fun resolveSurfaceBufferWidth(): Int {
        // sh-strength r2 B: the pass draws at the view's pixel size (the remote source size is unchanged elsewhere).
        if (sharpenDisplayPassActive()) return videoTextureView.width
        if (videoWidth > 0) return videoWidth'''),
    ('''    private fun resolveSurfaceBufferHeight(): Int {
        if (videoHeight > 0) return videoHeight''', '''    private fun resolveSurfaceBufferHeight(): Int {
        if (sharpenDisplayPassActive()) return videoTextureView.height
        if (videoHeight > 0) return videoHeight'''),
    ('''        val p = viewerViewport.place(viewWidth, viewHeight, resolvedContentWidth, resolvedContentHeight)
        if (p == null) {
            videoTextureView.setTransform(matrix)
            return
        }''', '''        val p = viewerViewport.place(viewWidth, viewHeight, resolvedContentWidth, resolvedContentHeight)
        if (sharpenDisplayPassActive()) {
            // sh-strength r2 B: the pass draws the picture where the viewport placed it, in a view-sized buffer;
            // the TextureView adds nothing (identity) -- else the placement would be applied twice. The double
            // transform is the negative control's (test seam 2) only.
            val place = if (p != null) DisplayPlacement(p.left, p.top, p.width, p.height)
            else DisplayPlacement(0f, 0f, viewWidth, viewHeight)
            if (sharpenDisplayPass == 2 && p != null) {
                matrix.setScale(p.width / viewWidth, p.height / viewHeight)
                matrix.postTranslate(p.left, p.top)
            }
            videoTextureView.setTransform(matrix)
            sharpenRenderer?.setDisplayPlacement(place)
            val logged = "%.1f,%.1f,%.1fx%.1f buffer=%dx%d".format(place.left, place.top, place.width, place.height,
                surfaceBufferWidth, surfaceBufferHeight)
            if (logged != sharpenDisplayPlacementLogged) {
                sharpenDisplayPlacementLogged = logged
                diagnosticsLog.log("sharpen_display_pass", "mode=$sharpenDisplayPass place=$logged " +
                    "view=${viewWidth.toInt()}x${viewHeight.toInt()} content=${resolvedContentWidth}x$resolvedContentHeight")
            }
            if (::viewerPanChip.isInitialized) renderViewportChips()
            return
        }
        if (sharpenDisplayPass != 0) sharpenRenderer?.setDisplayPlacement(null)
        if (p == null) {
            videoTextureView.setTransform(matrix)
            return
        }'''),
])
print('ok')
