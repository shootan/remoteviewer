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

edit('test/java/com/remote60/androiddirect/SharpenPathPolicyTest.kt', [
    ('''    /** Producer-connecting commands: what must never happen on a surface that is not free. */''', '''    // sh-strength r3: a method change replaces the renderer -- released first, the new one only after it let go
    @Test
    fun restartReleasesThenStartsANewRenderer() {
        val (p, gen) = start(SharpnessLevel.WEAK)
        val c = p.restartRenderer()
        assertEquals(listOf(Command.DetachDecoder, Command.ReleaseRenderer(gen)), c)
        assertEquals(SharpenPathPolicy.Path.RELEASING, p.path)
        val after = p.rendererReleased(gen, detached = true)
        assertEquals(listOf(Command.StartRenderer(gen + 1, SharpnessLevel.WEAK.strength)), after)
        assertEquals(emptyList<Command>(), SharpenPathPolicy(SharpnessLevel.OFF).restartRenderer())
    }

    /** Producer-connecting commands: what must never happen on a surface that is not free. */'''),
])

edit('main/res/values/strings.xml', [
    ('''    <string name="ui_sharpness_hint">휴대폰에서만 처리 · 받는 데이터량은 그대로</string>''',
     '''    <string name="ui_sharpness_hint">휴대폰에서만 처리 · 받는 데이터량은 그대로</string>
    <!-- sh-strength r3 (Codex codex_sh_strength_r2.md §2): where the pass draws; A stays the default. -->
    <string name="ui_sharpen_method_label">선명화 방식</string>
    <string name="ui_sharpen_method_default">기본</string>
    <string name="ui_sharpen_method_display">화면 크기 (실험)</string>
    <string name="ui_sharpen_method_hint">화면 크기: 글씨를 더 또렷하게 표시 · 기기에 따라 지연·발열 증가 · 선명도 끄기면 쓰지 않음</string>'''),
])

edit('main/res/layout/activity_main.xml', [
    ('''            <TextView
                android:layout_width="match_parent"
                android:layout_height="wrap_content"
                android:layout_marginTop="6dp"
                android:text="@string/ui_sharpness_hint"
                android:textColor="@color/ui_text_secondary"
                android:textSize="12sp" />
''', '''            <TextView
                android:layout_width="match_parent"
                android:layout_height="wrap_content"
                android:layout_marginTop="6dp"
                android:text="@string/ui_sharpness_hint"
                android:textColor="@color/ui_text_secondary"
                android:textSize="12sp" />

            <!-- sh-strength r3: where the sharpening draws (SharpenMethod) -->
            <TextView
                android:layout_width="match_parent"
                android:layout_height="wrap_content"
                android:layout_marginTop="12dp"
                android:text="@string/ui_sharpen_method_label"
                android:textColor="@color/ui_text"
                android:textStyle="bold" />

            <LinearLayout
                android:layout_width="match_parent"
                android:layout_height="wrap_content"
                android:layout_marginTop="8dp"
                android:orientation="horizontal"
                android:baselineAligned="false">

                <TextView
                    android:id="@+id/settingsSharpMethodDefaultButton"
                    style="@style/UiSegment"
                    android:layout_height="match_parent"
                    android:minHeight="48dp"
                    android:paddingTop="6dp"
                    android:paddingBottom="6dp"
                    android:text="@string/ui_sharpen_method_default" />
                <TextView
                    android:id="@+id/settingsSharpMethodDisplayButton"
                    style="@style/UiSegment"
                    android:layout_marginStart="8dp"
                    android:layout_height="match_parent"
                    android:minHeight="48dp"
                    android:paddingTop="6dp"
                    android:paddingBottom="6dp"
                    android:text="@string/ui_sharpen_method_display" />
            </LinearLayout>

            <TextView
                android:layout_width="match_parent"
                android:layout_height="wrap_content"
                android:layout_marginTop="6dp"
                android:text="@string/ui_sharpen_method_hint"
                android:textColor="@color/ui_text_secondary"
                android:textSize="12sp" />
'''),
])

edit('main/java/com/remote60/androiddirect/MainActivity.kt', [
    ('''    private lateinit var settingsSharpStrongButton: TextView''', '''    private lateinit var settingsSharpStrongButton: TextView
    private lateinit var settingsSharpMethodDefaultButton: TextView
    private lateinit var settingsSharpMethodDisplayButton: TextView'''),
    ('''        settingsSharpWeakButton = findViewById(R.id.settingsSharpWeakButton)''', '''        settingsSharpWeakButton = findViewById(R.id.settingsSharpWeakButton)
        settingsSharpMethodDefaultButton = findViewById(R.id.settingsSharpMethodDefaultButton)
        settingsSharpMethodDisplayButton = findViewById(R.id.settingsSharpMethodDisplayButton)'''),
    ('''        for ((spinner, origin) in listOf(viewerFpsSpinner to "menu", settingsFpsSpinner to "settings")) {''',
     '''        settingsSharpMethodDefaultButton.setOnClickListener { selectSharpenMethod(SharpenMethod.DEFAULT, "settings") }
        settingsSharpMethodDisplayButton.setOnClickListener { selectSharpenMethod(SharpenMethod.DISPLAY, "settings") }
        for ((spinner, origin) in listOf(viewerFpsSpinner to "menu", settingsFpsSpinner to "settings")) {'''),
    ('''                // The REQUESTED level stays selected after a fallback: the user's choice is not overwritten.
                for ((button, choice) in row.zip(SharpnessLevel.values())) button.isSelected = sharpness == choice
            }
        }''', '''                // The REQUESTED level stays selected after a fallback: the user's choice is not overwritten.
                for ((button, choice) in row.zip(SharpnessLevel.values())) button.isSelected = sharpness == choice
            }
        }
        if (::settingsSharpMethodDefaultButton.isInitialized) {
            settingsSharpMethodDefaultButton.isSelected = sharpenMethod == SharpenMethod.DEFAULT
            settingsSharpMethodDisplayButton.isSelected = sharpenMethod == SharpenMethod.DISPLAY
        }'''),
    ('''        sharpness = SessionPersistence.loadSharpness(this)''', '''        sharpness = SessionPersistence.loadSharpness(this)
        sharpenMethod = SessionPersistence.loadSharpenMethod(this)  // sh-strength r3: unset = DEFAULT (A)'''),
    ('''    /** sh-strength r2 B: the display-size pass (test builds only; the shipped seam always answers 0 = A). */
    private val sharpenDisplayPass: Int by lazy { SharpenTestSeam.displayPassFrom(intent) }
    private var sharpenDisplayPlacementLogged = ""

    /** B is in effect while the sharpening renderer is the TextureView's producer (starting or live). */
    private fun sharpenDisplayPassActive(): Boolean =
        sharpenDisplayPass != 0 &&
            (sharpenPolicy.path == SharpenPathPolicy.Path.STARTING || sharpenPolicy.path == SharpenPathPolicy.Path.LIVE) &&
            videoTextureView.width > 0 && videoTextureView.height > 0''', '''    /** sh-strength r2 B: the test seam's switch (test builds only; the shipped seam always answers 0). */
    private val sharpenDisplayPass: Int by lazy { SharpenTestSeam.displayPassFrom(intent) }
    private var sharpenDisplayPlacementLogged = ""
    /** sh-strength r3: the user's choice of where the pass draws (settings; stored). */
    private var sharpenMethod = SharpenMethod.DEFAULT
    /** r3 (N2): the placement last handed to the current renderer (null: none) -- the same one is never sent again. */
    private var sharpenPlacementSent: DisplayPlacement? = null

    /** 0 = A, 1 = B, 2 = B with the double transform (the test seam's negative control only). */
    private fun displayPassMode(): Int =
        if (sharpenDisplayPass != 0) sharpenDisplayPass else if (sharpenMethod == SharpenMethod.DISPLAY) 1 else 0

    /** B is in effect while the sharpening renderer is the TextureView's producer (starting or live). */
    private fun sharpenDisplayPassActive(): Boolean =
        displayPassMode() != 0 &&
            (sharpenPolicy.path == SharpenPathPolicy.Path.STARTING || sharpenPolicy.path == SharpenPathPolicy.Path.LIVE) &&
            videoTextureView.width > 0 && videoTextureView.height > 0

    /** r3 (N1): B is what the renderer WILL draw (a filtered level, no fallback): the output is sized for the view from
     *  the start, so the renderer begins with the layout it draws. */
    private fun sharpenDisplayPassIntended(): Boolean =
        displayPassMode() != 0 && sharpenPolicy.requested.filtered && sharpenPolicy.fallbackReason == null &&
            videoTextureView.width > 0 && videoTextureView.height > 0

    /** sh-strength r3: the user picks where the pass draws. A live renderer is replaced through the usual release. */
    private fun selectSharpenMethod(method: SharpenMethod, origin: String) {
        if (method == sharpenMethod) return
        sharpenMethod = method
        SessionPersistence.saveSharpenMethod(this, method)
        diagnosticsLog.log("sharpen_method", "method=${method.code} origin=$origin path=${sharpenPolicy.path}")
        executeSharpen(sharpenPolicy.restartRenderer())
        syncVideoSurface(forceRebind = false)  // the output size follows (view for B, source for A)
        renderQualityControls()
    }'''),
    ('''        // sh-strength r2 B: the pass draws at the view's pixel size (the remote source size is unchanged elsewhere).
        if (sharpenDisplayPassActive()) return videoTextureView.width''', '''        // sh-strength r2 B: the pass draws at the view's pixel size (the remote source size is unchanged elsewhere).
        // r3 (N1): from the moment B is intended, so a renderer starts with the size it draws at.
        if (sharpenDisplayPassIntended()) return videoTextureView.width'''),
    ('''        if (sharpenDisplayPassActive()) return videoTextureView.height''', '''        if (sharpenDisplayPassIntended()) return videoTextureView.height'''),
    ('''            val place = if (p != null) DisplayPlacement(p.left, p.top, p.width, p.height)
            else DisplayPlacement(0f, 0f, viewWidth, viewHeight)
            if (sharpenDisplayPass == 2 && p != null) {''', '''            val place = if (p != null) DisplayPlacement(p.left, p.top, p.width, p.height, viewWidth, viewHeight)
            else DisplayPlacement(0f, 0f, viewWidth, viewHeight, viewWidth, viewHeight)
            if (displayPassMode() == 2 && p != null) {'''),
    ('''            videoTextureView.setTransform(matrix)
            sharpenRenderer?.setDisplayPlacement(place)''', '''            videoTextureView.setTransform(matrix)
            if (place != sharpenPlacementSent) {  // r3 (N2): the same placement is not sent again
                sharpenPlacementSent = place
                sharpenRenderer?.setDisplayPlacement(place)
            }'''),
    ('''        if (sharpenDisplayPass != 0) sharpenRenderer?.setDisplayPlacement(null)''',
     '''        if (sharpenPlacementSent != null) {
            sharpenPlacementSent = null
            sharpenRenderer?.setDisplayPlacement(null)
        }'''),
    ('''        sharpenRenderers[generation] = SharpenRenderer(generation, st, strength, listener, sharpenFaults)''',
     '''        sharpenRenderers[generation] = SharpenRenderer(generation, st, strength, listener, sharpenFaults)
        sharpenPlacementSent = null  // r3: a new renderer has no placement yet'''),
])
print('ok')
