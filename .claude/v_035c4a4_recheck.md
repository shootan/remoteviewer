# 검증용 재확인 — 035c4a4 (FINAL_PACKET seq2191 이후 변경분)

## 왜 다시 봤나
FINAL_PACKET 은 55c93c0 / 20ea3e4 에 고정돼 있었다. 작업용 보고(seq2192)에 8bc9d1c·4cad92e 가
언급됐고, 확인 결과 **보고되지 않은 커밋 035c4a4 가 하나 더** 있었다. 그것이 내가 33 PASS 를 받은
테스트 파일이므로 해시 고정이 깨진다 → 영향 범위만 재확인.

## 영향 범위 판정
- 8bc9d1c : 55c93c0 **이전** 커밋. 이미 검증 대상에 포함돼 있었다.
- 4cad92e : docs/history.md 단독.
- 035c4a4 : viewer_window_proc_isolated_test.cpp (+65) + docs/ui_state_table.md.
- `git diff --name-only 20ea3e4..035c4a4 -- apps/ | grep -v '_test\.cpp$'` → **빈 출력**.
  제품 코드 무변경 ⇒ **게시된 0.2.123 바이트 검증(manifest 28895aa8fafb6832 / 공개 URL 10/10 /
  dist installer byte-identical)은 그대로 유효**하다. 재게시 불필요.

## 재실행 (build-verify, 작업용 build-local 과 공유하지 않음)
```
cmake --build build-verify --config Release --target remote60_viewer_window_proc_isolated_test
./build-verify/apps/native_poc/Release/remote60_viewer_window_proc_isolated_test.exe
rc=0   PASS=38  FAIL=0
viewer_window_proc_isolated_test: PASS (38 passed, 0 failed)
device=hardware  swapEffect=FLIP_DISCARD
```
33 → 38 은 추가된 `ok()` 5개와 일치한다(케이스 9 에 4, 케이스 10 에 1). 구성이 바뀐 두 실행이므로
총계 비교가 아니라 **항목 대응**으로 확인했다.

## 새로 들어온 근거 — 결함과 수정이 같은 바이너리 안에 있다
```
PASS  BEFORE: drawn with GDI and not presented, the screen still shows the video        00E600
PASS  ...while the window's own DC holds a perfectly good picker -- the two disagree    0D0F14
PASS  AFTER: presented through the swapchain, the screen shows it            00E600 -> 0D0F14
PASS  opening the picker mid-session does not ask the host to stop the stream   requests=0
```
이전에는 결함을 보려면 **옛 커밋을 체크아웃**해야 했다. 이제 한 프로세스·한 실행 안에서 GDI 경로가
화면에 닿지 못함을 직접 보여준 뒤 수정 경로가 닿는 것을 보여준다. 내가 FINAL_PACKET 에 적은
"합성 화면과 창 DC 를 구분해 판정" 의 근거가 **추론에서 실측 대조로** 올라갔다.
⚠️ 케이스 9 가 창 DC 에 직접 그리는 것은 **테스트가 제품을 넘어 손을 뻗는 것**이고 제품은 더 이상
그렇게 하지 않는다 — 파일 주석도 그렇게 적고 있다.

## 이번에 찾은 것 (별건·기존 결함·오늘은 도달 불가)
`viewer_window_proc.cpp:659` `WndProc` — `WM_TIMER` 가 아는 ID 둘(kPacedPresentTimerId 0x712,
kCursorOverlayTimerId 0x711) 이 아닐 때 `break` 로 switch 를 벗어나 **return 없이 함수가 끝난다**
(MSVC C4715). `LRESULT` 가 미정값 ⇒ UB.
- **55c93c0 이 만든 것이 아니다**: `git show 943672c:...` 에 같은 `break` 가 있다. 기존 결함이며
  0.2.123 뿐 아니라 그 이전 버전도 같다.
- **심각도 낮음 · 오늘 도달 불가**: 이 창에 걸리는 SetTimer 는 위 둘뿐이고 둘 다 `return 0` 한다.
- **위험은 다음 사람에게 있다**: 세 번째 타이머를 추가하면 조용히 쓰레기 LRESULT 를 반환한다.
- 범위 판단: picker 수정 완성에 **필수가 아니다**. 별건 기록으로만 남기고 이번 건에 끼워 넣지 않는다.

## 유지되는 미검증
사용자 원래 증상과의 동일성(모양·경계만 일치) · WARP 미실행 · 영상 경로 A/B 미측정 ·
이 PC 1대/콘솔 세션 · 인앱 완주 · 다른 GPU/드라이버.

---

## 추가 확인 — 케이스 ⑽ 이 제품이 도달하는 상태인가 (검증용 단독, 2026-09-12)

**물어야 했던 이유.** 케이스 ⑽ 은 `ctx.sel.activeStreamGeneration` 을 **7로 직접 써넣고** 측정한다.
제품이 세션 중간에 그 값을 0 이외로 만들지 않으면, 그 단정은 **제품이 도달하지 않는 상태**를 검사한
것이고 `requests=0` 은 장식이 된다. ("부정 대조가 실제로 실패하는지 확인한다" 와 같은 문제다.)

**추적.**
- viewer 측 기록은 `viewer_selection_gate.cpp:96` 한 곳 — `readyGeneration` 값을 그대로 넣는다.
- `readyGeneration` ← `RecordReveal`(`:74-75`) ← `post_pc_selection_reveal`(`viewer_picker.cpp:177`)
  ← `viewer_video_receiver_frame.cpp:366` 의 **프레임 헤더 `h.streamGeneration`** = 호스트가 보낸 값.
- 호스트: `host_capture_session.hpp:156` **`std::atomic<uint64_t> streamGenerationState{1}`** — **1에서 시작**.
  `host_loop_helpers.cpp:436` **`next = prev + 1`**, `:485` store. 롤백 `:412` 는 `prev`(≥1) 복원.
  `host_capture_session.cpp:787` 도 `streamGeneration == 0` 을 무효로 취급한다.

**판정.** 호스트 generation 은 **단조증가 ≥ 1 이고 0이 될 수 없다.** 따라서 viewer 의 `== 0`
(`viewer_picker.cpp:130`)은 모호하지 않다 — 0 은 **viewer 자신의 리셋**뿐이다
(`viewer_startup.cpp:169` 시작, `viewer_control_client.cpp:439` 연결 끊김). 즉:
- 최초 picker(선택 드러난 적 없음) = 0 → 스트림 보류가 **정상**.
- 선택이 한 번 드러난 뒤 = ≥1 → 세션 중간 picker 는 스트림을 멈추지 않아야 한다.
- 재접속 후에는 다시 0 이 되어 첫 picker 가 또 보류한다 — 일관됨.
⇒ **케이스 ⑽ 의 generation 7 은 제품이 실제로 도달하는 상태의 대표값이다. 단정은 공허하지 않다.**

⚠️ 이것은 **소스 추적**이다. 실기에서 호스트가 보낸 generation 을 관측한 것이 아니다.
