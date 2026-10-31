import json
R = 'C:/work/gnlink-wt/apk-prelogin-select'
D = 'D:/remote/remote/.claude/caplife/'
A = 'apps/android_direct_client/app/src/'
commits = [
    ('sh_a', [A + 'main/java/com/remote60/androiddirect/SharpnessLevel.kt', A + 'main/res/values/strings.xml',
              A + 'test/java/com/remote60/androiddirect/SharpenPathPolicyTest.kt'],
     """feat(apk, sh-strength r2 A): 강하게 is 0.60 (was 0.35)

Codex codex_field_0162_0231.md §4; user 10-22 "약하게랑 강하게랑 차이가 없어". The stored code stays "strong", so a
phone that chose 강하게 keeps it at the new strength (policy recorded in SharpnessLevel); off / weak 0.20 / unset are
unchanged. The menu shows "강하게 · 0.60". Same 1-pass 5-tap kernel, source 1 px, crop/rotation, fallback.

Product / test / docs: product (SharpnessLevel, strings) / test (SharpenPathPolicyTest strength) / docs (none).

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"""),
    ('sh_b', [A + 'main/java/com/remote60/androiddirect/SharpenRenderer.kt', A + 'main/java/com/remote60/androiddirect/MainActivity.kt',
              A + 'noseam/java/com/remote60/androiddirect/SharpenTestSeam.kt', A + 'shseam/java/com/remote60/androiddirect/SharpenTestSeam.kt'],
     """feat(apk, sh-strength r2 B): the display-size one-pass sharpening, as a TEST-BUILD A/B experiment

Codex §4B. With a placement (SharpenRenderer.setDisplayPlacement) the pass draws at the VIEW's pixel size the video
quad where ViewerViewport placed it -- enlargement and sharpening in one draw, one final clamp -- over black, and
the TextureView adds no transform (identity; else the placement is applied twice). Taps stay one visible SOURCE
pixel apart, the frame's SurfaceTexture transform is applied once, and a frame whose source size is unknown is NOT
drawn in B (no output-size fallback; counted). A placement change redraws a still picture, latest only. Off /
fallback / direct restore the source-size buffer and the TextureView transform (the buffer size follows the path).

Switched on only by the test seam (src/shseam: --ez shTestDisplayPass true; shTestDisplayPassDouble = the double-
transform negative control). The shipped seam (src/noseam) answers 0: the product path is A. Touch and the remote
coordinate keep the viewport's source-based mapping.

Product / test / docs: product (renderer B mode, MainActivity buffer/transform wiring, shipped seam returns A) /
test (shseam switch) / docs (none).

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"""),
    ('sh_t', [A + 'shseam/java/com/remote60/androiddirect/SharpenShaderFixture.kt', 'automation/sh_filter_avd.py',
              'automation/sh_pattern_window.ps1', 'automation/gnlink_check_fixed_server.py'],
     """test(sh-strength r2): same-frame strength matrix and A/B -- shader fixture, real APK compare / cost / touch on an off-screen pattern

- SharpenShaderFixture "strengthab": one "text" frame (2-px bars, 1-px line, low-contrast checker) through the
  production renderer: A 0/.20/.35/.60, B x1.33 and x2, B without a source size (must not draw), B with output-pixel
  taps (negative control). sh_filter_avd.py strength_ab checks each against shader_model (the shader on the CPU).
- compare_ab / latency_ab / touch_ab: the real APK on the AVD, a 1440x810 pattern shown x1.33. The pattern is a
  LAYERED window outside every monitor (sh_pattern_window.ps1 -Layered, a 4x4 block pulsed in the G strip so a
  window capture keeps delivering frames) -- this PC's screen is never covered. Window cards are matched by exact
  title (a selected window's header carries it too). Border detection R > 60 (s=0.60 darkens the 1-px red row).
- APK gate: shTestDisplayPass is a test marker (the seam APK fails on it, the seam-free one passes).

Product / test / docs: product (none) / test (fixture, AVD harness, pattern window, APK gate) / docs (none).

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"""),
]
for key, files, msg in commits:
    json.dump({'repo_path': R, 'files': files}, open(D + key + '_add.json', 'w', encoding='utf-8'))
    json.dump({'repo_path': R, 'message': msg}, open(D + key + '_commit.json', 'w', encoding='utf-8'), ensure_ascii=False)
print('ok')
