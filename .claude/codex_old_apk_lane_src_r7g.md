# old APK lane r7e~r7g SOURCE / 보정 패치와 S/P 1회 runner

2026-10-30 · Codex remote#tt7fcvky → 검증용 remote#7fsfy22z

## 판정과 다음 행동

대상은 `6345512..b11fc0d40b49e086ed3df9bcbfcaa467fd7318a2`다. 제품 코드 변경0과 tracked clean을 확인했다. bundle SHA-256 `7a2329060c67edb2f46511714ce625f44f893208ed29c2d7fe282e4df7e8d963`, SUMS `6a23eebc445d44d916b5f7870a901ff808a8f318f2d8699441c1096eee051470`와 일치한다.

**r7g의 이전 로그 부재 문제는 해소됐다. 다만 b11fc0d 원본의 30초 영상 진행 판정에는 아래 반례가 있어 그대로 실행하지 않는다.** 이를 고친 **첨부 패치의 정확한 바이트에 조건부 SOURCE OK**를 준다. 패치 적용·독립 회귀·고정 commit/hash 확인을 거치면 **추가 SOURCE 왕복 없이 S→P 각1회**를 진행할 수 있다. 출하/EVIDENCE 승인은 아니다.

검증용의 적용 후63/64 보고도 확인했다. 남은1건은 이전 시험이 읽기 실패를 관측 호출 전에 주입해, 새 baseline 거부 경로가 먼저 동작한 경우다. FAIL 판정을 PASS로 낮추지 않는다. 아래 승인 시험 패치로 **기준선 실패와 루프 중 실패를 분리**한다. 실행 하네스 oa_avd.py의 승인 바이트는 그대로 유지한다.

**실행 고정 r7h:** 검증용이 독립 pick65/65 rc0/RESULT PASS를 보고하고, 두 파일만 Git MCP로 `ff1c72a19aa2e8656d207047c5e076c656c4f540`에 고정했다(부모 b11fc0d). Codex가 commit 구성·tracked clean과 두 blob의 승인 canonical hash를 직접 확인했다. 따라서 위 조건부 SOURCE gate는 충족됐다. runner5405f48b로 시작한 S/P의 실제 결과와 rel-0164 출하 판정은 아직 대기이며, Source 승인만으로 실기 성공을 주장하지 않는다.

검증용은 같은 작업용1 task로 다음을 잇는다.

1. b11fc0d 위에 아래 watch/stop 패치와 시험 분리 패치를 적용한다. 변경 대상은 oa_avd.py·oa_pick_test.py 두 파일뿐이다. 원본/이전63/64 결과는 보존한다.
2. 검증용이 독립적으로 수정된 **pick65/65**를 확인한다. 이미 통과한 pmclear12/redact7/orchestration26은 이 시험 분리로 바뀌지 않아 결과를 재사용할 수 있다. 패치 바이트와 아래 반례/양성도 대조한다. 실패하면 실기0이며, 기대값을 낮춰 통과시키지 않는다. 새로운 제품 변경은 하지 않는다.
3. 해당 두 파일 보수를 clean commit으로 고정하고, full commit과 두 canonical source hash를 기록한다. 이 정확한 패치의 적용·검증·commit은 이미 허용한 범위이며 별도 go가 필요 없다.
4. 아래 runner에 그 full commit과 검증용이 만든 traced server의 절대경로를 전달해 S→P 각1회를 실행한다. 5 exe/APK/SDK·slot/job/guard는 유지한다.

제품 수정이나 새 테스트 설계로 넓히지 않고도 고칠 수 있도록 검토용 실제 보정본·패치·직접 대조를 만들었다. worker/verify worktree의 파일은 Codex가 수정하지 않았다.

## 닫힌 경계

set_pick_mark/same_viewer는 성공한 snapshot과 앱 marker·Host prefix hash를 사용한다. 고정 함수 직접 대조7건에서 정상 무변화만 통과하고, 앱 pull 실패/Host 읽기 실패·축소·교체/marker 상실/scene 변경은 거부했다. 이전의 빈 문자열을 정상 무변화로 보는 N2는 닫는다. ④ 실패 뒤 return1, HOSTS에서 확인 후 BACK1회, viewer BACK0과 IME unknown/shown tap0도 유지된다.

SH는 공개 Settings 버튼과 앱의 requested off·origin=settings, 영상의 direct/requested off 및 sharpen_start/fallback0을 확인한다. 실제 0.2.32 UI 배치가 미실증인 한계는 남는다. row가 없거나 화면 밖이면 INVALID이며 prefs 편집/숨은 handler로 우회하지 않는다.

실패 직후 진단은 읽기 전용이며 password flag/id의 text를 제외하고 fixture 비밀번호를 mask한다. private dump·화면은 로컬에 보존한다. 원문 자료가 없으면 실패 이유를 추정해 채우지 않는다.

## 보정1 — 누적 out의 잘못된 0 기준으로 정지 영상이 통과

원본 observe_connection은 pick marker **뒤**에 아직 video_debug 줄이 없으면 out0=0으로 둔다. 그 뒤 첫 로그가 기존 누적 out=55를 반복할 뿐이어도 out1>0이므로 “영상 진행” PASS가 된다. 첫 frame이 55에서 멈춘 경우를 거부하지 못한다. “watch-frozen” stub의 성공만으로 실제 누적 계수 경계를 덮었다고 할 수 없다.

실제 고정 함수에 가상 clock과 로그 snapshot만 대역화해 재현했다. 관측 시작 전에 이미 out55이며 30초 뒤에도 out55인 경우 **원본 True, 보정 False**다. 정상55→57은 보정에서도 True다.

첨부 보정은 시작 시 읽기 성공한 현재 pick의 snapshot에서 실제 누적 기준값을 잡고, 그 snapshot의 marker 뒤에서 나온 최종 out과 비교한다. baseline/최종 읽기 실패·marker 소실·후속 표본 부재는 거부하고, 계수 reset/감소 뒤 값이 다시 커졌다는 이유로 진행이라고 하지 않는다. 경과 시간은 monotonic으로 잰다. 실제 표시 fps나 물리 화면을 증명하는 계측으로 확대하지는 않는다.

## 보정2 — 마지막 단계 실패 뒤 ⑦ 평가를 계속함

원본 P branch는 p_disconnect / p_reconnect / 6r pick이 False여도 ⑦로 간다. S의6b pick 반환값도 무시한다. 이후 새 입력이 없는 경로도 있지만 “실패 후 다음 시나리오0”과 through1–6 완료 조건에는 맞지 않는다. 보정은 각 실패에서 즉시 arm을 끝내고 나머지를 NOT RUN으로 남긴다. 실제 고정 P branch 문장 대조3건에서 원본은⑦로 갔고, 보정은 return1/⑦0이었다.

## 승인 패치와 직접 대조

- 패치: `D:/remote/remote/.claude/codex_old_apk_review/r7g_b11/watch_baseline_approved.patch`
- 패치 SHA-256: `a752efa7d61b72c1079da7170d1e9c4b9be0e1e60a064774a1e76a7a245aba29`
- 원본 oa_avd.py canonical UTF-8/LF SHA-256: `e48e9d1b52ed112f93b6cf355f2925df922662bc9389af8e12ad29812e5ee19f`
- 보정 oa_avd.py canonical UTF-8/LF SHA-256: `c2b238d66aaaec05c77c22b730b528e5379ce0369b323e0a829c8bcc3577088b`
- 보정본 사본: 같은 폴더 `oa_avd_fixed.py`. BOM과 CRLF만 정규화해 비교하고 내용 변경을 정규화로 숨기지 않는다.

`git apply --check`는 author의 b11fc0d에서 성공했다(적용은 하지 않음). 직접 순수 대조는 **17건**이다: watch7건(정지/정상 증가/reset/기준 없음/최종 읽기 실패/marker 상실/연결 종료), P 실패 흐름3건, 기존 snapshot guard7건. 원본 반례와 보정 통과를 함께 기록했다. `r7g_watch_review.py`, `r7g_b11/checks.json`에 source/patch hash와 결과가 있다. 실제 adb·Host·Viewer·AVD·입력은0이다. 검증용의 b11 pick64/pmclear12/redact7/orchestration26은 별도 실행 귀속이며, Codex가 그 전체를 다시 실행했다고 하지 않는다.

## 승인 시험 분리 패치 — 63/64의 경로 차이 처리

- 패치: `D:/remote/remote/.claude/codex_old_apk_review/r7g_b11/stub_watch_split_approved.patch`
- 패치 SHA-256: `09e8f26ad6d7fdf0b38551128823800a733b3f81f43173a49ab249f70a5acbc4`
- oa_pick_test.py canonical UTF-8/LF SHA-256: `de8fbf819af2664bdef0d96e7da9927b1e777230e0e648b9d4dc9a9fae4fec1f`
- 사본은 같은 폴더 `oa_pick_test_fixed.py`다. verify-0164의 현 시험 파일에서 apply --check 성공을 확인했으며 직접 적용하지 않았다.

새 baseline case는 pick 이후 첫 app snapshot부터 실패시켜, 기준선 거부 문구·False verdict·tap0을 검사한다. 기존 loop case 두 개(app pull 실패/Host 로그 축소)는 첫 두 snapshot이 성공한 것을 확인한 뒤, **첫 loop sleep 다음의 세 번째 snapshot**에서만 fault를 주입한다. 초기 gate 실패를 루프 실패로 잘못 부르지 않으며, reads의 앞 두 성공과 fault1회도 검사한다. 결과 기대는 여전히 FAIL·기존 same_viewer 이유·tap0이다. hook과 env는 finally로 복원한다. 기존2 case에 baseline1개를 더하므로 전체65개가 된다.

Codex는 새 시험 문장 그대로와 실제 보정된 observe_connection/applog_snapshot/set_pick_mark/same_viewer 함수를 대역 I/O/clock 아래 실행했다. **분리3건 모두 통과**, baseline case의 가상 경과0, 두 loop case는 경과 후 실패였다. 실제 adb·process·Host는0이다. `r7g_stub_split_review.py`, `r7g_b11/stub_split_checks.json`에 근거를 남겼다. 독립 전체65개 실행을 대신한 결과는 아니므로 검증용이 전체를 한 번 확인한다. 제품 동작/승인 oa_avd.py 바이트는 이 시험 수정으로 바뀌지 않는다.

## trace 사본 SOURCE

53fced4의 base server.js `2742143d72bb9482429f3046109b0a833bbf8a5049a3b26a8e1eac2f586c4bb7`에 고정 apply_diff 함수를 직접 적용했다. 사본은 `52918c04b8fef8f4e66af5c6abe4a7332429416397004432accb79d8c231492a`와 일치하고 Node syntax check도 rc0이다. 원래 줄을 대체하는 것은 relay send3곳의 trace callback 추가뿐이고 나머지는 trace 관련 추가다. Node 서버를 실행한 것은 아니다. `r7g_b11/trace_review.json`에 기록했다.

fixture의 pinned hash와 `[relay-trace] ENABLED` 확인 전 Host/AVD0도 수용한다. 검증용이 수행한 실제 양성 relay_trace_test와 plain+env 음성은 별도 EVIDENCE다. embedded `.diff`의 빈 context 줄5곳 때문에 전체 diff --check가 공백 경고를 내지만, 그 파일을 제외한 코드 diff --check는0이다. diff 문법의 context 공백을 지우는 별도 수정을 요구하지 않는다.

trace는 로컬 시험 사본에만 쓰며 NAS 배포0이다. packet kind/rx/send callback·5초 snapshot/종료 bucket은 수신 경로를 좁히는 근거다. socket send 성공은 원격 수신/앱의 ACK 처리 성공이 아니다. control head를 직접 식별하지 못하면 원인을 확정하지 않는다.

## 승인 runner와 실행 gate

runner: `D:/remote/remote/.claude/codex_old_apk_review/run_old_apk_lane_r7g_reviewed.ps1`

SHA-256: `5405f48bcd7e7e683ee83a96a9846b49e738d27827d5e776632b3bb901b77e02`. PS parser0 오류를 확인했으며 실행하지 않았다. 인자를 빼먹으면 대화형 입력을 기다리지 않고 실행 전 거부한다. 이 값은 CRLF 보정과 승인 시험 분리의 두 파일 gate를 포함하며 이전2647d649/e3cafa62 버전을 대체한다.

5533a518 검토본을 바탕으로 두 필수 인자를 받는다.

- `-CandidateHead`: **승인 패치 두 개 적용 후 새 commit의 full40자리 값**. b11fc0d 원본이 아니다. runner는 HEAD 일치·b11의 후손·b11 대비 oa_avd.py/oa_pick_test.py **두 파일만** 변경·두 canonical source hash·tracked clean을 확인한다. 다른 제품/스크립트 변경을 끼우거나 미커밋으로 실행하면 거부한다.
- `-TraceServerJs`: 검증용 worktree의 `.claude/oa_relay_server/` 아래 생성한 traced server.js의 실제 절대경로. 위치와52918c04 전체 raw hash, plain 서버의 **LF 정규화 source** 2742143d hash를 slot 전 확인한다.

검증용 보충의 CRLF 차이를 직접 확인했다. plain checkout raw는 `5a22f37280fe412f32f94b34b155e9091500b3694eadd020e9c15c67486e67db`, CRLF→LF이면2742143d다. 이전 runner는 이 plain 파일을 raw2742143d와 비교해 정상 checkout을 거부했으므로 수정했다. plain은 BOM/CRLF만 정규화한 source hash로 판정하고 raw/정규화 hash를 모두 기록한다. 실제 verify-0164 파일을 새 PowerShell 계산식으로 확인해 통과했고 parser0 오류다. 생성된 trace 사본은 계속 raw52918c04로 고정하며 APK/5 exe binary hash는 정규화하지 않는다.

검증용 초안66616fec의 S trace/P env 제거 방향은 맞다. 다만 그 초안의 b11fc0d 원본 gate는 위 관측 보정 전 source를 가리키므로 실행 대상으로 승인하지 않는다. 최신 검토 runner는 정확한 보정 후 commit/source와 읽기 오류시 Stop을 함께 확인한다. 실행 문서는 이 최신 runner 하나를 기준으로 잇는다.

S는 traced server + ServerSha256 pin + KE_RELAY_TRACE=1, P는 기존 plain server + KE_RELAY_TRACE=0이다. **S에서 켠 env를 그대로 P에 넘기면 plain server가 trace 선언 gate에 걸리므로 arm별로 바꾼다.** OA_OBSERVE_SEC는30, fake adb env는 제거한다. 기존5 exe/APK/SDK gate·첫 nonzero 중단·finally 자기 slot 해제는 유지한다. 새 파일 hash 읽기 오류가 조건문을 건너뛰지 않도록 ErrorActionPreference는 Stop으로 뒀다. 실제 실행/정리 동작까지 검증했다고 주장하지 않는다.

검증용이 이 runner를 그대로 쓰거나 이 검토 범위와 동등한 보정을 적용하고 hash를 기록할 수 있다. 정확한 승인 source와 테스트 결과가 고정되면 새 commit 번호를 기록해 통지하고 이어 실행한다. 내용이 다른 추가 변경이면 그 변경만 SOURCE로 확인한다.

## 최종 해석 경계

S/P 각1회, 첫 frame 뒤 입력 없는30초, SH-off, 종료/FAIL 직후 읽기 전용 진단, 기존 guard/slot/job 조건은 앞 결정 그대로다. 첫 nonzero 뒤 batch가 멈추며, 정리가 확인되고 독립 전제가 안전할 때만 기존 허용의 P 독립1회를 판단한다. S 반복·추가 weak arm은 없다.

SH-off의 실제 전체 lane 성공은 **그 설정의** 구 APK 호환 근거다. 기본 weak 종료는 미확정 한계이며 기존 실패를 소급 PASS로 하지 않는다. 구 Host A/B를 지금 필수로 추가하지 않고, rel-0164 최종 EVIDENCE·서버/외부 관문·한계를 함께 보고 출하를 판단한다. APK0.2.33 보류와 사용자 지시의 push 보류는 유지한다.

이번 변경은 미추적 검토/보정 산출물뿐이며 제품/추적 파일·사용자 설치·운영·게시·push를 건드리지 않았다. 제품 commit은 Codex가 만들지 않았다. 사용자 현재 조치 없음.
