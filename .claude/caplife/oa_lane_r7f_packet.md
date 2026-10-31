# rel-0164 old-APK lane r7f — SOURCE 패킷 (2026-10-30, 정본 codex_old_apk_lane_src_r7d.md ③ / 검증용 r7f)

## 1. 고정
- worktree `C:\work\gnlink-wt\old-apk-lane`, commit **9cec590**(3f18465 위, 3f18465는 6345512 위). 시험 lane 파일만 바뀌었고 제품 파일은 0입니다. tracked 변경 0. push·서명·게시·실기는 하지 않았습니다.
- 묶음 diff `caplife/oa_lane_r7f_bundle.diff`(6345512..9cec590, `0e5dbbe6…bc4`). r7f만 본 diff는 `caplife/oa_lane_r7f.diff`(3f18465..9cec590, `721b3bb7…f9`).
- SUMS `caplife/oa_lane_r7f_SHA256SUMS.txt`(`d9d93bf5…f18`): 바뀐 11파일의 9cec590 blob sha256입니다.
- `git diff --check`의 trailing whitespace 2건은 커밋된 `.diff` 파일 안의 빈 context 줄(diff 형식)이며 결함이 아닙니다.

## 2. 항목별 구현과 거부 대조 (stub, 기기 없음)
| 항목 | 구현 | 대조 (oa_pick_test 53/53 · orchestration 26/26 · relay check) |
|---|---|---|
| 1 SH off | 두 arm 모두 install + pm clear 뒤, targets 화면에서 pick 전에 `listSettingsButton` → `settingsSharpOffButton`을 누릅니다(b554e76 MainActivity.kt:531, :5308). control은 화면(android:id/content) 안에 있고 enabled여야 합니다. 아니면 arm은 **NOT RUN(INVALID)**이고, prefs 편집이나 숨은 handler로 대체하지 않습니다. **2s 판정**: 앱 자신의 `sharpness_select level=off … origin=settings` + 버튼 selected. 설정 화면 UI dump를 남깁니다. **3s 판정**(picture): `sharpen_path path=direct requested=off`, sharpen 경로 0, `sharpen_start` 0, fallback 0 | control PASS / row 없음·화면 밖 → INVALID(Off tap 0) / 로그 없음·selected 아님 → FAIL / **weak 요청의 fallback(effective=off) → FAIL** / sharpen_start → FAIL / arm S 전체 흐름에서 control 불가 → exit 2, pick 0 |
| 2 relay trace | `oa_relay_trace_server.py`가 53fced4 apps/directory의 **시험 사본**을 만듭니다. server.js `2742143d…`에 `relay_trace/relay_trace_53fced4.diff`(`f72329cc…`)를 적용합니다. 이 diff는 검토된 7a79b09 trace를 53fced4에 맞춰 다시 자른 것으로, 추가와 relay send 3곳의 trace callback만 있습니다(off이면 undefined). 결과 `52918c04…`이고 hash를 모두 고정했으며 fuzz 없이 정확히 적용됩니다. fixture는 KE_RELAY_TRACE=1이면 `-ServerSha256` 일치와 서버 자신의 `[relay-trace] ENABLED`(8 s 안)를 요구하고, 아니면 host와 AVD 모두 0입니다. 다른 branch server.js 교체 0, 배포 0 | **양성**: 사본에서 7a79b09 relay_trace_test ALL PASS(실제 `[relay-trace] connect=… snap … c2hRx/h2cRx` 줄) / **음성**: 53fced4 그대로 + env → "the trace says it is on" FAIL(relay 자체는 정상) / orchestration: 고정+선언 → 실행, 미선언·pin 없음·pin 불일치 → host·AVD 0 (`.claude/relay_trace_check_r7f.txt`) |
| 3 30 s 관측 | 3과 3s 뒤 **입력 없이 30 s** 동안 같은 viewer·연결(same_viewer: scene/status/새 pick/Host session ended·새 client)과 영상 진행(video_debug out 증가)을 봅니다. 끊기면 즉시 끝나고 arm도 끝납니다(4 이후 0). 30 s 미만은 stub 밖에서 INVALID | control PASS(입력 0) / 관측 중 끊김 → 즉시 FAIL + 기록에 오류 문구 / 영상 정지 → FAIL / 30 s 미만 → INVALID / arm S 전체 흐름: 끊김 → picture tap 0, 4–7 NOT RUN |
| 4 오류 직후 기록 | FAIL 직후마다 **읽기 전용** 기록을 남깁니다: UI dump, 스크린샷, app/host 로그. password field의 text는 기록하지 않고 fixture 비밀번호는 masking합니다. 예외 경로도 같습니다 | 끊김 FAIL 뒤 `fail1_ui.txt`에 오류 문구 / sign-in 화면 기록에 비밀번호 없음 |
| 5 실패 뒤 정지 | 4와 3w에 더해 **5, 6a FAIL도 arm 종료**입니다. P는 두 manual connect 전에 키보드 부재를 확인합니다(2k/6k). 숨기지는 않습니다 | P 키보드 shown → FAIL, key·tap 0 / source 검사 |

## 3. 검증용 실행 (S → P 각 1회)
- relay 사본을 한 번 만듭니다: `python -I automation\old_apk_lane\oa_relay_trace_server.py build <wt>\.claude\oa_relay_server\<새 이름>`.
- 슬롯 인자: `-ServerJs <그 사본>\apps\directory\server.js -ServerSha256 52918c04b8fef8f4e66af5c6abe4a7332429416397004432accb79d8c231492a`, env `KE_RELAY_TRACE=1`. relay는 arm S에만 쓰입니다(P는 direct).
- 나머지(SDK pin, 5 exe, 슬롯·guard·job, input none/power off)는 그대로입니다.

## 4. 판정 문구와 한계
- SH-off 통과는 **requested=off 설정에서의** 구 APK 호환 근거일 뿐입니다. 기본 weak의 종료는 **미해결 한계**로 남습니다. 구 Host A/B는 필수가 아닙니다.
- relay trace의 send 성공은 socket의 응답일 뿐이며, 상대 수신이나 앱의 ACK 처리 증거가 아닙니다. control head 식별이 계측에 없으면 head 원인을 확정하지 않습니다.
- 3w의 진행 판정은 out 증가(>0)만 봅니다. 성능 임계값은 두지 않았습니다.
- **미실증(실기에서 처음 확인)**: 0.2.32 Settings 패널의 실제 배치. 실제 dump에서 확인한 것은 `listSettingsButton`(verify-0164 oa-s-…025800 `3_targets_ui.txt`)뿐이고, SH row는 b554e76 layout 소스로만 확인했습니다. 화면 밖이면 그 arm은 NOT RUN입니다.
- 실제 sharpen_path 줄의 시점과 형식은 소스(MainActivity.kt:5333)로만 확인했습니다.
