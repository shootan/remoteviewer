# 3역할 계획 검토 기록 — 검증용 Claude (remote#1528xbin), 2026-09-07

대상: Codex(remote#ktuqt7y9) task t-40epdtja 의 계획 A(정책 문서) / 계획 B(S13 회귀 수정).
기준: HEAD 1287883, 추적 파일 clean(직접 확인), `.claude/` 만 미추적. 콘솔 세션 Active, rdp-tcp 미접속(qwinsta 직접 확인).

## 판정 요약

| 계획 | 판정 | 비고 |
|---|---|---|
| A 정책 문서 | **OK (조건 3건 추가) → Codex 승인** | 역할 식별은 버스 displayName, ephemeral ID 금지, 검증용의 "OK" 형식 고정 |
| B S13 수정 | **변경 요청(변형 R) → Codex 가 R 로 계획 수정·확정** | 결함 진단·회귀 조건은 그대로 동의. 한 줄 "이동"(K)보다 "삭제"(R)가 상태 수명에 맞음 (아래 근거). Codex 확정 근거: held/unknown IDR 은 freshness 근거가 아니고 fresh 키는 admit 에서 이미 pending 을 끝냄 |

## 계획 B — 결함 독립 확인 (코드 직접 읽음)

1. `viewer_frame_gate.cpp:474-482` `note_reference_sync` 첫 줄이 `resumeAnchorPending=false` 를 **무조건** 실행 (c3c3d76 diff 로 도입 확인).
2. `viewer_video_receiver_frame.cpp:244-264` 실제 호출 순서: `admit` → `decode_access_unit` 성공 → `note_decode_ok` → (overflow 아니면) `note_reference_sync(in)` **모든 decoded 프레임** → `outFrames.empty` 면 `note_decode_empty` → 아니면 `clear_empty_streak`. 즉 보유(held) 재개 프레임이 비키로 decode 되는 순간 pending 이 풀린다.
3. 그 다음 fresh 프레임(recvGap 2ms): `admit` 의 `else if (gate.resumeAnchorPending …)` 분기가 안 타므로 `anchorHere=false`, `presentAnchorFloorUs` 는 옛 값 유지 → `entryFloorUs = 옛 floor` → lag ≈ fresh.capture − max(presented, 옛 floor) ≈ 0.78s → streak 3 에서 Congested.
4. 실패 로그(`.claude/test_sweep_fail_remote60_viewer_udp_recovery_test.log:184-189`): S13 shape old-real→fresh 에서 `state=congested reason=decode_queue decodeQueueLagUs=782291 seq=29`, `in.recvGapUs=1319`, transitions 3, requests 1, heldResume 1. 위 3 과 일치.
5. 단독 PASS 이유: 프레임 간 2ms 송출이지만 decode 가 4~6ms 라 present 스레드가 burst 사이에 `lastPresentedCaptureUs` 를 fresh 로 전진시키면 lag 가 작아진다. 부하(sweep 연속 실행) 시 present 가 늦어 재현. → 단독 PASS 는 무효화 근거 아님(동의).
6. 단위테스트가 못 잡은 이유: `viewer_frame_gate_test.cpp` `Rig::feed` 는 admit 만 하고 `decoded()` 는 T-P11a 에서 키프레임 뒤 한 번만 호출. 비키 decode 를 흉내내지 않았다.

## 계획 B — 수정안에 대한 이견 (변경 요청)

Codex 안: `note_reference_sync` 의 클리어를 `if (in.keyFrame)` 안으로 이동(변형 K).
검증용 제안: 그 줄을 **삭제**(변형 R). 이유:

- `admit` 시점에 pending 이 남는 키프레임은 (a) held 로 판정된 키(hold>300ms) 또는 (b) 비-gap 분기의 stamp-미상 키뿐이다. fresh 키는 `admit` 의 `freshKnown` 분기(또는 gap 분기의 pre-P11 규칙)가 이미 anchor 하고 pending 을 끝낸다. 따라서 `note_reference_sync` 의 클리어는 fresh 키에는 **중복**이다.
- (a) held IDR: 오래된 픽셀의 IDR 이 pending 을 끝내면 뒤따르는 fresh burst 가 옛 floor 로 판정돼 지금 S13 과 같은 오탐이 IDR 형상에서 재발한다. P11 의 의도("보유 프레임은 anchor 아님, 첫 fresh 가 anchor")와 어긋난다.
- (b) stamp-미상 키: c3c3d76 자체가 "미상 stamp 는 pending 을 시작도 종료도 못 한다"고 명문화했다. 변형 K 는 이 규칙과 모순된다.
- 변형 R 에서도 "키프레임이 pending 을 끝낸다"는 관측 규칙은 유지된다(fresh 키 → admit 에서 종료). 바뀌는 건 held/미상 키 두 경우뿐이고, 둘 다 pending 유지가 옳다. pending 은 8프레임/500ms 예산·상태 전환·waitForKey 로 이미 유계.
- T-P11d 의 "decoded keyframe ends pending" 단정은 그 키가 fresh(capture=heldAt, send=heldAt+6ms)라 admit 에서 이미 종료되므로 변형 R 에서도 통과. 다만 held IDR 케이스를 새 테스트로 명시해야 한다.

변형 K 도 현재 S13 결함은 고치므로 Codex 가 K 를 고집하면 수용 가능(차이는 held/미상 IDR 형상 한 가지). 작업용에는 **R 을 기본**으로 위임하고, Codex 가 반대하면 한 줄·단정 하나만 바꾸면 되도록 테스트를 분리해 두게 했다.

## 계획 B — 회귀 완료조건 (동의 + 구체화)

- frame_gate 단위테스트: 수신기 순서를 그대로 따르는 헬퍼(Decode 판정마다 `note_decode_ok`→`note_reference_sync`→`clear_empty_streak`; 빈 출력 변형은 `note_reference_sync`→`note_decode_empty`; 실패 변형은 `note_decode_failure`). 신규: held 비키 decode 뒤 pending 유지·첫 fresh anchor·Congested 0·요청 0 (1287883 에서 FAIL 해야 함); held 빈 출력 뒤 동일; decode 실패 → waitForKey → 다음 admit 에서 pending 종료; fresh IDR 은 종료, held IDR 은 유지(R). 기존 T-P11a/b 도 receiver-faithful 헬퍼로 전환.
- recovery S13: 단독 ×3 + 전체 sweep 순서 ×2(실패 로그 보존). 수정 전 재현 로그는 sweep 0.2.102c 것을 보존. 결정적 재현(burst 중 present anchor 고정)이 rig 에서 가능하면 shape(a′) 추가, 불가능하면 그 사실을 보고.
- 실제 backlog 감지 유지: T-P11b(전부 held, 예산 뒤 trip)·T-P11c(작은 hold+renderer 정체 trip) 그대로 PASS.
- 1287883 의 sender 펜스·versioned snapshot·단일 dispatch 회귀(`host_sender_epoch_test`, `host_input_target_rect_test`, S14/S15) PASS 유지.
- e2e 는 포트 43000 점유(설치 호스트) 환경 실패로 분리 보고 — `netstat -ano | findstr 43000` 으로 점유 근거를 남기되 라이브 호스트를 멈추지 않는다.

## 계획 A — 조건 3건

1. 역할 식별은 버스 탭 displayName(검증용claude/작업용claude)과 세션 시작 시 `a2a_whoami` 로 하고, 세션 ID·resume UUID 는 정책 문서에 적지 않는다(Codex 6 항과 일치, 명문화 요청).
2. 검증용의 "구현 OK" 는 고정 형식: 대상 commit 해시, 검토 범위, 검증용이 직접 실행한 테스트/빌드와 작업용 보고를 구분, 산출물 식별(파일명·크기·sha256·빌드 commit), 미검증 한계. OK 이후 대상 파일이 바뀌면 그 commit 은 재검사.
3. 검증용은 저장소 추적 파일(제품·문서)을 수정·커밋하지 않는다. 검토 기록은 `.claude/`(미추적)에 둔다. AGENTS 의 "Claude 담당: … 커밋" 문구는 작업용으로 좁힌다. Mandatory Workflow 의 "파일 변경 없으면 사유 명시·커밋 생략"이 검증용에 그대로 적용된다.

기타: 체크포인트 ②의 보류 항목(`step32_release3_docs.py`·`git_add_416.json` "0.2.102 re-cut")은 S13 결함 포함 후보라 **폐기**. 릴리스 후보는 0.2.103(`product_version.hpp` 0.2.102, `dist/` 에 0.2.103 없음 — 직접 확인).

## 위임·확정 순서 (시간순)

1. 검증용 → Codex: A OK(조건 3건), B 변경 요청(변형 R) 발송 (seq 1099).
2. 검증용 → 작업용: task t-5ahw0t5x 발송 — A 먼저, B 는 R 기본으로 (seq 1100). **절차상 오류**: 이견이 있는 B 는 Codex 확정을 받은 뒤 위임했어야 함(아래 정정).
3. Codex → 검증용: A 조건 3건 승인. B 는 변형 R 로 계획 수정·확정. waitForKeyFrame 해제·lastDecodedKeyCaptureUs 갱신은 유지, 회귀 범위(비키/empty/held 키/unknown 키/fresh 키/실패·상태 전환/실제 backlog) 요구대로. 0.2.102 결함 후보는 보존·미승인 표시만(삭제 금지). 0.2.103 은 최종 검증 가능한 한 버전으로.
4. 검증용 → 작업용: B = "Codex 계획 R 확정" 으로 task 갱신 전달. 진행 중이던 A·준비 작업은 되돌리지 않음(Codex 지시).

### 절차 정정 (Codex, 이후 적용)
- OK 한 부분만 즉시 자율 위임한다. 계획 변경이 필요한 부분은 Codex 수정·확정을 받은 뒤 위임한다. "기본안으로 먼저 위임하고 반대 시 변경" 은 사용자 절차와 다르므로 쓰지 않는다.
- 구현 검사·보완 요청은 검증용↔작업용 사이에서 직접 반복한다. Codex 최종 확인 요청은 대상 commit·직접 확인 근거·산출물·미검증 한계를 갖춘 구현 OK 일 때만.

## 검사 로그 (검증용 직접 실행)

(작업용 보고 후 기입)

### 중간 검사 (2026-09-07 18:50, 검증용 직접 확인 — 작업 트리 읽기 전용, 테스트 미실행)
- A commit 40b836e: AGENTS.md 3역할 절·CLAUDE.md 행동/보고 기준 절이 계획 + 조건 3건과 일치. **누락**: Codex 추가 문장("계획 변경 부분은 Codex 확정 전 위임 금지 / 변경 없이 OK 한 독립 범위는 즉시 위임") — commit 18:38:29 가 전달(seq 1109)보다 앞섬. grep 으로 부재 확인 → 후속 docs(policy) commit 요청(seq 1114).
- B 제품 diff(미커밋): `note_reference_sync` 의 `resumeAnchorPending=false` 삭제 + admit/state 주석 정정 = 확정안 R 과 일치. waitForKeyFrame 해제·lastDecodedKeyCaptureUs 갱신 유지.
- B 테스트 diff(미커밋): Rig 헬퍼 decoded/decodedEmpty/decodeFailed/feedDecoded 가 수신기 순서(note_decode_ok→note_reference_sync→clear_empty_streak / →note_decode_empty / note_decode_failure)와 일치. T-P11a/b receiver-faithful 전환, 신규 g/h/i/j. T-P11j 의 held IDR 계산(hold≈867ms>300ms, 비-gap 분기 → pending 유지, 이후 fresh P anchor) 검산 OK. S13 shape 2 (a′) 는 기존 rig 노브 `pinPresentAnchor`(1287883 에 이미 존재, S9/S10 사용) 재사용.
- 로그: `.claude/s13_prefix_frame_gate_1287883.log` a/g/h/j FAIL(i 는 당시 기대값 차이) / `.claude/s13_prefix_recovery_1287883.log` (a′) transitions +3·요청 +1 결정적 재현, 형상 0/1 은 타이밍상 PASS / `.claude/s13_postfix_frame_gate.log` PASS / `.claude/s13_postfix_recovery_run1~3.log` 세 형상 모두 0/0 PASS / `.claude/s13_postfix_sweep1.txt`·`sweep2.txt` 각 39/40, 비통과 = e2e 만.
- e2e 환경 근거(검증용 직접): `netstat -ano` UDP 0.0.0.0:43000 = PID 13856 (tasklist 로 프로세스명 확인, 아래 로그). 라이브 호스트 미중지.
- 줄바꿈: 수정 4개 파일 모두 전 줄 CRLF(1287883 blob 은 LF 정규화 저장, autocrlf=true) → 혼합 없음. git 경고는 autocrlf 안내일 뿐.
- 문구 정정 요청: T-P11d printf/주석 "decoded keyframe ends pending" → "fresh keyframe (anchored at admit)".
- 절차: 작업용이 t-5ahw0t5x 를 completed 로 잘못 갱신 → working 복귀 요청.
- PID 13856 = GNLinkStream (Get-Process, 검증용 직접 확인 18:52). e2e "udp hello ack failed" 는 라이브 스트림 프로세스의 43000 점유 때문 → 환경 실패로 분리, 미검증 항목에 유지.

### commit 대조 (21:0x, 검증용 직접)
- d980ff0 docs(policy): 요청 문장이 AGENTS.md 검증용 항목·CLAUDE.md 검증용 항목에 원문 그대로 삽입, history #408 보완 줄. OK.
- 1005ebe fix(viewer): 제품 diff(viewer_frame_gate.cpp·_state.hpp) = 중간 검사 때 작업 트리와 동일(R). 테스트 diff 동일 + (j) 조건부 문구 제거. history #409(1287883 기록 포함·실행 횟수·환경 분리), 구현계획 P11 문구 "종료는 admit 에서만 … decode 자체는 종료 안 함·held/미상 IDR 은 유지" 로 정정. **미반영**: T-P11d printf/주석("decoded keyframe ends it") — seq 1115 로 test(viewer) 소형 commit 요청, 설치본은 release commit 트리에서 재생성 요구.
- 검증용 직접 실행(1005ebe + version bump 작업 트리 바이너리, qwinsta rdp 없음, 21:09): `remote60_viewer_frame_gate_test` PASS/FAIL 0 (`.claude/verify_frame_gate_1005ebe.log`), `remote60_viewer_udp_recovery_test` PASS, S13 세 형상 transitions 0·요청 0 (`.claude/verify_recovery_1005ebe_run1.log`).
- 설치본(작업용이 21:06 생성, release commit 전): dist/GNLinkSetup-0.2.103.exe 3,466,240 B sha256 c263758b…4268daf ≠ 0.2.102 85a8769d…b63fee(같은 크기, 다른 내용). 최종 검사는 release commit 트리 재생성본으로 다시 한다.
- 설치본 구성(검증용 직접, Python): dist/GNLinkSetup-0.2.103.exe 안에 installer_payload/ 의 GNLinkHost·GNLinkStream·GNLinkInputService·GNLinkCapture·GNLinkClient·GNLinkViewer 6개가 바이트 그대로(RCDATA) 포함, 각 payload = Release 빌드 sha256 동일. GNLinkViewer.exe(프레임 게이트 포함 타깃 remote60_native_video_client_poc) 는 수정 후 빌드(18:40:53, 1005ebe 소스), sha256 05b13c9acceb…. GNLinkHost/GNLinkClient 는 UTF-16 "0.2.103" 1회씩, 설치본 3회, "0.2.102" 0회. GNLinkStream(17:57, 1287883 트리)·InputService·Capture 는 이번 변경과 무관해 재빌드 없음(정상). 이 설치본은 release commit 전 트리 산출이므로 최종 검사에서 재생성본과 다시 대조.

### 최종 보고 검사 (21:2x, 작업용 .claude/s13_release_report_to_verifier.md)
- acd8233 chore(release): 제품 변경 = product_version.hpp 0.2.102→0.2.103 한 줄, history #410, 작업목록 1.0/1.1/1.3 을 0.2.103 기준으로. 0.2.102 후보는 "결함 포함·미승인" 표기, 삭제 없음. OK.
- 설치본 c263758b…(21:06) 은 acd8233 의 제품 소스와 동일 트리(1005ebe + version bump) 산출 → 제품 소스 식별 = acd8233. 이후 테스트/문서 전용 commit 은 제품 바이너리를 바꾸지 않으므로 재생성 불요(중간 검사 "release commit 트리에서 재생성" 요구는 이 근거로 철회).
- 릴리스 sweep `.claude/release_0.2.103_sweep.txt` 38/40: e2e(환경, PID 13856 GNLinkStream) + recovery **S12** 1회 FAIL(`:1294 corrupted_keys()==1`, 나머지 S12 단정·S13 세 형상·S14·S15 PASS).
- S12 분석(검증용 직접): `SendAu(bytes, au.keyFrame, …)` 가 AU 단위 key 로 zero 처리 → 강제 IDR 이 비동기 인코더 때문에 다음 SendFrame(pump 내부 684행)에서 나와도 그때 corrupt 됨. 단정이 첫 SendFrame 직후라 경쟁. 1005ebe 와 무관(S12 에 held 프레임 없음 → pending 미개입). 판정: 하네스 결함, 제품 회귀 아님, 계획 범위 밖 → Codex 범위 판단 요청, 확정 전 위임 안 함.
- 미반영 1건: T-P11d 라벨/주석(seq 1115 3번) → test(viewer) 전용 commit + ledger S12 항목 + history #411 요청(NEEDS_CHANGES 경미).
- 검증용 직접 실행 근거: `.claude/verify_frame_gate_1005ebe.log` PASS, `.claude/verify_recovery_1005ebe_run1.log` PASS.
- S12 빈도 특성(검증용 직접, 릴리스 recovery exe 18:40 빌드 = 1005ebe 소스, 단독 실행): run2 PASS(S12 0 FAIL), run3 FAIL 1건 — S12 `:1302 goodIdr != 0`(강제 IDR 을 넣은 SendFrame 이 그 호출에서 AU 를 하나도 안 냄; 1294 와 같은 원인의 두 번째 경쟁 지점, 이후 "shown" 단정은 goodIdr=0 이라 무의미하게 통과), S13 세 형상 0/0 유지 (`.claude/verify_recovery_release_run2.log`, `_run3.log`). 단독에서도 재현되므로 sweep 부하 전용이 아님. 누적: 릴리스 sweep 1/1 + 단독 1/3 (S12 실패), 상쇄 근거 아님·하네스 결함 판정 유지.

### 재검사 1 (21:4x, 검증용 직접)
- e08de14 test(viewer): T-P11d 상단 주석·printf·인라인 주석 문구만 변경(diff 3곳, 동작 없음), history #409 보완 1줄. 제가 재실행: `.claude/verify_frame_gate_e08de14.log` PASS(FAIL 0, 새 라벨 25행 확인).
- 94f5d74 docs(viewer): 뷰어_리팩터_발견사항.md F-22(S12 :1294 단정 위치, 범위 밖·미수정), history #411. fefccfd docs(viewer): F-22 에 :1302 goodIdr 지점 추가. 문서만.
- 제품 바이너리 불변(검증용 직접 sha256): GNLinkViewer 05b13c9acceb / GNLinkHost ebf361bd460d / GNLinkClient cec4cb43e09d — 21:09 기록과 동일, 작업용 tp11d_product_bins_before/after 와 일치. 설치본 c263758b… 미재생성. 식별 방식 (i) 확정: 제품 소스 = acd8233, 이후 commit 은 테스트/문서만.
- Codex 범위 확정(수신): S12 하네스 수정 이 task 포함, 조건(단정 삭제·완화 금지, 실제 손상 IDR 송신 관측 시점으로 이동, 유계, 손상 1회, 무폭주·비정지·정상 IDR 복귀 유지, 비동기 순서 관측, 실패 sweep 순서 재검증). seq 1126 으로 작업용에 위임(:1302 지점 포함, 단독 ×5 + sweep ×1).

### 재검사 2 — S12 하네스 수정 edb1ec9 (21:4x, 검증용 직접)
- diff 검토: `send_key_and_wait_on_wire` = SendFrame(true) 정확히 1회 → SendFrame(false) 만 16.7ms 간격, 상한 30회/1500ms, keyframes_sent 증가로 wire 관측, 순서는 로그만("S12 order"). `corrupted_keys()==1` 단정은 삭제·완화 없이 관측 뒤로 이동, 정상 IDR 뒤와 시나리오 끝에 반복(정확히 1회 손상). goodIdr = 관측한 last_key_seq, `!= 0` 필수. shown = `published_key(goodIdr)`(present 폴링에서 f.key 인 프레임의 seq 집합) + publishedCount 증가 → 뒤 P·0 sentinel 로 통과 불가. 무폭주(≤ before+2)·비정지·Normal 유지. Codex 조건 전부 충족. 제품 소스 무변경(파일 3개: 테스트 1 + docs 2).
- 잔여 위험(기록): published_key 는 2ms 폴링이라 부하로 16ms 이상 굶으면 IDR 버전을 놓쳐 FAIL 할 수 있음 — FAIL 메시지에 missed publishes 수가 나와 진단 가능(통과 경로 아님). 지금까지 작업용 5회+sweep 1회, 검증용 3회 모두 missed 로 인한 실패 없음.
- 검증용 직접 실행: `.claude/verify_recovery_edb1ec9_run1..3.log` 3/3 PASS(FAIL 0, S13 세 형상 0/0). S12 order: 세 번 모두 동기 형상(call 1). 작업용 단독 5회(`.claude/s12_postfix2_recovery_run1..5.log`)는 run1/run5 에서 비동기 형상(첫 호출 P, IDR 은 call 2~3, 20~38ms) 관측 후 통과 — 옛 단정을 깨던 형상이 새 관측으로 통과함을 확인.
- 작업용 sweep `.claude/s12_postfix2_sweep1.txt` 38/40: e2e(환경) + gdi_capture_process(3.67fps). 검증용 직접: `remote60_gdi_capture_process_test` 21:41 재실행 FAILED 3.67fps(`.claude/verify_gdi_capture_now.log`), qwinsta 콘솔 Active·RDP 없음·LogonUI 없음(잠금 아님). 18:xx sweep 4회는 53fps PASS, 제품 바이너리는 acd8233 빌드 그대로(sha256 불변)·테스트 exe 미재빌드 → 이번 commit 들과 무관한 환경 변화(원인 미조사). 미검증으로 분리(ledger D1 과 같은 성격).
- 제품 바이너리 sha256(검증용 직접, 재확인): Viewer 05b13c9acceb / Host ebf361bd460d / Client cec4cb43e09d / Stream a1cc8a726c72, 설치본 c263758bba03 — 21:09 값과 동일.
- 판정: **구현 OK** (아래 Codex 발송 형식).

### 종결 (21:5x)
- Codex 최종 확인 수신: HEAD edb1ec9·clean, 1005ebe = R, acd8233 이후 테스트/문서만, 정책 문구, 설치본 크기·sha256 일치를 Codex 가 직접 읽기 확인. 테스트는 검증용 독립 실행 근거 수용(Codex 중복 실행 없음). 계획 A·B 구현/자동검증 범위 최종 OK.
- 작업용에 t-5ahw0t5x completed 정리 지시(미검증 항목 보존, "전체 실기 성공" 금지, 설치/push/배포 보류). 감독 task t-40epdtja 는 검증용이 completed 로 갱신.
- 남은 것(사용자 판단): 0.2.103 설치 후 실기(UAC 2회·재로그인 NAS 로그·정적 타이핑·영상), GDI 캡처 테스트 환경 원인, e2e 는 라이브 호스트 중지 시에만 가능.
