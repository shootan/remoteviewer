# Windows 안정화 1차 수정 — 검증용 검토 기록 (remote#865h2pvp, 2026-09-08)

Codex task t-thjz1nw2 (기준 HEAD abd136e, 0.2.103). 작업용 task t-uhqkw7h9 (A01 + V14/A04 위임, seq 1285). A02+HN07 은 A06 reset 트리거 (a)/(b) Codex 확정 후 추가 위임.

## 계획 판정 (seq 1284)
| 항목 | 판정 | 비고 |
|---|---|---|
| A01 log_upload busy loop | OK(변경 없음) → 위임 | predicate 조건·flushInterval 대기·idle diag 시간 스로틀·workerCycles 카운터·회귀(192KiB pause + token 재개) |
| V14+A04 수신 recovery | OK(변경 없음) → 위임 | 공통 maintenance 모든 경로·elapsed rate-limit ≥5ms·N3 유계 recovery·complete IDR 즉시 수락·backoff 유지·회귀 6종·blackhole 주장 금지 |
| A02+HN07 provenance | OK → Codex A06 (a)안 확정(seq 수신) 뒤 위임(seq 1289) | 레코드 FIFO 통합·gate 열린 뒤 어떤 폐기든 awaitingKey 재진입+forceKey·A06 invalid 래치+재초기화에서만 해제; 트리거 (a) overflow 즉시 reset 요청(권고) vs (b) 12/700ms 한도 |

## 검사 체크리스트 (commit 별)
- 공통: qwinsta RDP 없음, Git MCP scoped commit, history/구현계획/작업목록 갱신, 수정 전 FAIL 로그 보존→수정 후 PASS, 실행 횟수, 환경 실패(e2e 43000·GDI) 분리, 범위 밖 파일 미변경(hold/tail·HN01·HN04·pacing·재접속·Android·서버·installer).
- A01: predicate diff 원문; next_job false 사이클 대기; wakeSeq/stop 즉시성; idle diag 스로틀; 계약 불변(ownerEpoch·configGeneration·401/500 격리·재시도); 회귀 workerCycles ≤5/2s·diag ≤1KiB·token 재개; 검증용 직접: probe_uploader_spin 재실행(수정 후 CPU≈0·diag 증가 0) + log_upload_test 실행.
- V14+A04: 모든 경로가 maintenance 통과(control/malformed/ignored/continue/timeout), rate-limit elapsed 기준, N3 분기(Normal·후속 없음·소진/기한) → waitForKey+reason 2, Android(hold 0) 불변, S1~S15 PASS; 검증용 직접: probe_nack_trace 재실행([S-keywait+P, no successor]·N3 케이스가 recovery 진입) + recovery/shared_core/frame_gate 테스트 실행.
- A02(추가 시): PendingInput 구조체 FIFO, 빈 출력 pop 포함 전 지점 일치, gate 폐기 후 awaitingKey, A06 래치·initialize 에서만 해제, reset cap 3/10s·valid current IDR 우선 유지, wire ABI 불변; 검증용 직접: probe_fifo_model 을 새 구조에 맞춰 재실행 + host_encode_epoch_test·host_sender_epoch_test.
- 릴리스: 0.2.104 한 번, payload sha256 = Release 빌드, "0.2.104" 임베드, 제품 소스 = release commit.

## 진행 로그
- seq 1289: A02+HN07+A06 작업용에 추가 위임. 확정 설계: codec `provenanceInvalid_` 래치(overflow 즉시 set, FIFO clear, 이후 epoch 0, initialize 성공 시에만 해제) / stage 는 encode 반환 직후 배치 루프 밖에서 확인 → gate 닫기(awaitingKey+provenanceInvalid) + 기존 ResetEncoder 경로 재동기(예산 3회/10s 우회 금지, 소진 시 pending/backoff·invalid 유지·main loop tick 재시도) / invalid 중 송출 0·겉보기 current key 미수락 / gate 열린 뒤 어떤 폐기든 awaitingKey 재진입+forceKey / 회귀 a~h.
- 검사 추가 항목(A02): overflow 신호가 outUnits 빈 호출에서도 stage 에 닿는지(코드 경로), 예산 소진 시 pending 재시도가 tick 에서 도는지, initialize 실패 시 invalid 유지, EncodeStats 노출, wire ABI(H264AccessUnit/UdpVideoChunkHeader) 불변 diff 확인.

## Codex 수정 2건 반영 (seq 수신 → 작업용 seq 1293, Codex 제안 seq 1294)
- HN07 축소: 열린 gate 에서 unknown/future 폐기만 chain 닫기; provenance 확실한 old-epoch 조용한 폐기는 닫지 않음(불필요 IDR 방지); invalid 중엔 전부 차단. 회귀 h 를 두 케이스로 분리.
- A04 N3 포기 조건 보류: 위임문의 "hold 기한 초과 OR 라운드 소진" 은 tail NACK 가능 시점(120ms)에 head 를 지워 A03 퇴행 → 철회. 제안(Codex 확정 대기): 후속 없는 head 만 ① 마지막 적용 단계 라운드 소진 ∧ ② lastNack+replyAllowance ∧ ③ lastProgress+replyAllowance ∧ ④ first+terminalMin, ⑤ first+hardCap 절대 상한. 기본 replyAllowance 50ms(가능하면 max(50, 2×RTT)), terminalMin 245ms, hardCap 1000ms. waitForKey 중 비-key head 는 ③만. 구현: assembler lastProgressUs·GiveUpOldestIncomplete, scheduler exhausted_final_phase·last_sent_us, 판정은 수신기 공통 maintenance.
- 검사 추가: N3 구현 시 tail 손실(후속 없음) 120ms NACK→응답→완성(추가 IDR 0), tail 무응답 170+50ms 폴백, hole 소진+tail 단계 잔존 조기 포기 금지, 큰 AU 진행 중 유지(상한 1s), 열린 current+known-old → 다음 P 정상. probe_nack_trace 에 위 시나리오 추가 재실행.

## A04 기준선(수정 전, probe_nack_trace A04-1~4, .claude/probe/probe_nack_trace_a04_baseline.out)
- A04-1 후속 없는 tail 손실 + 호스트 응답: tail NACK round0 1125ms(첫 datagram 1000) → round1 1150 → 응답 도착 1160 → seq 10 정상 배달, gap 0, waitForKey 0. (현 동작 정상 — 수정 후에도 유지되어야 함)
- A04-2 무응답: round 0/1/2 (1125/1150/1175) 뒤 pending 1 영구, waitForKey 0 → N3 재현(폴백 없음). 수정 후 기대: 1175+50ms(≥ first+245ms) 뒤 head 폐기 + waitForKey/reason 2.
- A04-3 hole 소진(1025/1050/1075) 뒤 tail 단계(1125/1150/1175) 정상 진행 → 수정 후에도 hole 소진 시점에 조기 포기 금지 확인용.
- A04-4 300청크 IDR 이 2ms/청크로 600ms 동안 도착 중: hole NACK 3라운드 뒤 120ms 에 tail 단계가 아직 송신 중인 idx 62.. 를 요청(관찰: tail grace 가 느린 대형 AU 진행을 in-flight 로 보지 못함 — A03 범주, 이번 범위 밖; terminal 규칙의 ③진행 조건으로 포기만 막으면 됨).

## A04 terminal 규칙 Codex 확정 → 작업용 위임 (seq 1297)
- ①~④ + ⑤hardCap. terminalMin = tailGrace+maxRounds×roundUs+replyAllowance(기본 245ms, 고정값 금지). replyAllowance = max(50ms, 2×최근 유효 control RTT) clamp ≤1s, stale/unknown → 50ms. hardCap 기본 5s(튜너블, ≥terminalMin 정규화), 실패 예산·1 episode·재요청 폭주 금지. NACK 미협상 → ③+④ 폴백. waitForKey 비-key head → ③, alreadyWaiting 억제. GiveUpIncomplete(gen,seq) identity, 완료 시 취소. A03 불변.
- 검사 추가: RTT atomic 의 cross-thread 갱신/stale 처리, terminalMin 계산식, hardCap 정규화, 포기 로그의 값·사유, 회귀 10종 매핑(위 위임문), 프로브 A04-1~4 재실행(1: 변화 없음, 2: 폴백 발생 시각 = 1175+50 이상, 3: tail 단계 전 포기 없음, 4: 진행 중 보존).

## A01 검사 (858f766) — 통과
- 제품 diff: predicate `stopping || wakeSeq != seen || (credentials && !authRejected && queued >= batch)`, next_job false 시 timed wait 복귀, idle-alive 60s 스로틀, workerCycles(status/stop 리셋). 계약(ownerEpoch·configGeneration·401/500 격리·재시도) 변경 없음(diff 범위 밖).
- 회귀 [10]: 제품 기본값(192KiB/2000ms/4MB), 수정 전 FAIL(364,024 cycles·+1,001,105B diag·CPU 1,984ms, .claude/a01_prefix_log_upload_test.log) → 수정 후 3회 PASS(+1·0B·0ms, a01_postfix_*.log). 토큰 재개: STALE→FRESH, 보관 배치 먼저, 큐 배치 배출.
- 검증용 직접: log_upload_test 실행 exit 0(.claude/verify_a01_log_upload_test.log, [1]~[10] FAIL 0), probe_uploader_spin 재빌드(새 log_upload.cpp) B) 창 CPU 0.00s·diag 0B(probe_uploader_spin_postfix.out). qwinsta RDP 없음.
- 문서: history #416, 구현계획·stabilization_audit A01 체크, 작업목록 1.4. 과장 없음(설치본은 A02 뒤).

## V14+A04 커밋 전 개입 (seq 1304/1306)
- 작업 트리 diff 가 철회된 초기 포기 규칙(`roundsSpent || holdPast`, holdPast = first+120ms ∧ !busy)이라 커밋 보류·재구현 지시. 근거: udp_video_nack.hpp `exhausted() = rounds_ >= maxRounds`(tail 단계 미고려), receiver maintenance 람다 reason "nack-exhausted"/"hold-expired". DropIncomplete "anything older" 도 identity 한정으로 정정 요구. 공통 maintenance(5ms rate-limit·모든 경로·timeout force)는 유지.
- 재검사 시: exhausted_final_phase, replyAllowance(RTT), terminalMin 식, hardCap 5s, GiveUpIncomplete identity·완료 취소, NACK off 폴백, waitForKey 비-key 억제, 회귀 10종, 프로브 A04-1~4(A04-3 에서 hole 소진 시점 포기 없음이 핵심).

## 재시작 후 (13:4x, 주소 Codex fqvxnfkm / 작업용 2ic5x3em / 검증용 srb0qig4)
- 새 Codex 가 t-thjz1nw2 연속 인수(중복 발급 없음). A01 은 검증용 직접 검사 결과로 인수. §3.2/§3.3 유지, 초기 120ms 포기안 회귀 금지.
- V14+A04 커밋 전 검사(작업 트리): 확정 규칙 구현 일치(GiveUpIncomplete(gen,seq)·lastProgressUs·spent_for(hasTail)·RTT allowance clamp·terminalMin·hardCap 정규화, 진단 로그에 값·사유 출력).
- **S23 FAIL 진단(검증용)**: GiveUpIncomplete 가 lastDeliveredSeq_ 를 전진시키지 않아 포기한 seq 의 늦은 청크가 stale 가드(shared_core :821)를 통과해 조립 재생성 → head 재차단 → 같은 seq 2회 포기(로그 :294/:299)·fresh IDR 미배달로 "resumed" 실패(:303). 수정 방향 (a) 포기 시 deliveredAny_/lastDeliveredSeq_ 전진 또는 (b) abandonedSeq_ 가드 + 회귀 추가. seq 1333 로 전달.
- 절차 지적: 트리에 mf_h264_codec 수정이 섞여 A02 가 조기 착수됨 → V14 커밋 뒤 별도 커밋으로 분리 요구.
- 검사 시 확인할 것(재실행): S23 통과, 같은 seq 재포기 0, S16~S22 유지, sweep 39/40(e2e 환경만), shared_core/frame_gate PASS, 프로브 A04-1~4 대조.

### S23 보강 근거 (검증용 직접, 13:5x)
- 단독 "PASS" 실행에도 같은 seq 21 을 반복 포기: run1 total=2, run5 total=2·3; sweep2 는 hard-cap 2건 + resumed FAIL. → "hardCap 한 episode" 조건이 모든 실행에서 위반되고 있으며 sweep FAIL 은 그 결과가 표면화된 것.
- 테스트 공백: S23 이 포기 횟수를 단정하지 않아 결함이 있어도 PASS. 단정 추가 요구(같은 (gen,seq) 포기 ≤1, 포기 뒤 늦은 청크가 head 재차단 않음). seq 1336 전달.

## V14+A04 커밋 7d0d69b 검사 — NEEDS_CHANGES 1건 (14:0x)
- 통과: 공통 maintenance(모든 경로·5ms elapsed rate-limit·timeout force), 포기 규칙 ①~⑤ 구현(spent_for(hasTail)·last NACK+allowance·lastProgress+allowance·terminalMin 계산·hardCap 정규화·NACK off ③+④·waitForKey 비-key ③), 진단 로그 값·사유, S16/S19(RTT 300ms 늦은 응답 수락)/S20(hole 소진 후 tail 유지)/S21/S22(1.2s 진행 보존), shared_core progress·identity, sweep3 39/40(e2e 43000 환경만), 수정 전 FAIL 로그 보존, A03/Android 경로 불변.
- **미해소**: `GiveUpIncomplete`(커밋본)이 조립만 erase → 포기한 seq 의 늦은 청크가 stale 가드(shared_core :821) 통과 → 조립 재생성·head 재차단. 증거 n3_rule2_recovery_run2.log S23 블록: 같은 (gen1,seq21) 3회 포기(hard-cap age 1,000,893 total=1 → hard-cap age 1,000,880 total=2 → nack-spent age 425,432 total=3), run1/run3 도 2~3회. Codex "hardCap 한 episode" 위반. 테스트가 못 잡는 이유: S23 단정이 keyframe_requests(리미터 120ms/토큰 3에 눌림)만 봄.
- 요구(seq 1336): identity 를 지나간 것으로 표시 / 단정을 포기 횟수 기준으로 + 늦은 청크 재차단 없음 케이스 / 단독 5회+sweep 재실행 후속 커밋. Codex 에 통지(seq 1337).

### Codex 동의 + 구현 주의 (seq 수신 → 작업용 seq 1339)
- delivered/abandoned 혼동 금지: 판정한 (gen,seq) 의 늦은 data/FEC 재생성만 차단, 다른 gen·seq wrap·정상 앞선 AU 오폐기 금지, lastDeliveredSeq_ 를 배달한 것처럼 올려 gap/IDR 복구 계약 훼손 금지 → 내 제안 (a) 철회, tombstone 방식 권장.
- tombstone 이면 크기·수명 상한과 Reset/generation 경계 명시. 한도 초과 시 latestDelivered 로 꾸미지 말 것.
- 회귀 직접 단정: 동일 identity 포기 count==1, 늦은 청크 뒤 head 재차단 없음, 새 valid IDR/P 진행. published_key_at_or_after 완화는 해소 근거 아님.
- history 정정: "하네스 문제" → "실패한 하네스가 제품 결함을 드러냄". 재현 로그 보존.
- 검사 시 확인: tombstone 상한·Reset 처리, 다른 gen/wrap 오폐기 없음(shared_core 단위), 포기 count 단정 존재, 단독 5회+sweep.

## A02 커밋 c77b01d 검사 — 통과 (14:2x)
- codec: PendingInputFifo(kMaxEntries 64) — overflow 시 provenanceInvalid_ 래치 + entries_ clear, Clear()(shutdown) 래치 유지, Reset()(initialize 성공)만 해제, PopForEmptyOutput() 이 레코드 전체 pop(F1), 래치 중 auEpoch=0, EncodeStats.provenanceInvalid 노출. 디코더 FIFO 미변경(9곳 잔존). wire ABI 불변.
- gate: provenanceInvalid → 열린 체인 즉시 닫음·겉보기 current key 거부·전 출력 DropUnknownEpoch(bound 시 ResetEncoder 유지). HN07 재닫힘은 auEpoch==0/>epochNow 만, DropOldEpoch 는 체인 유지(reclosedByUnknown 카운터). epoch_gate_take_reset_budget 공용.
- stage: encode 반환 직후 배치 밖 검사 → provenanceResyncPending → TryProvenanceResync(예산·실패 시 pending 유지) → host_stage_time_limit tick 재시도.
- 검증용 직접 실행: host_encode_epoch_test exit 0([1][2][2b][3][4]) .claude/verify_a02_encode_epoch.log. 작업용 sweep a02_sweep1 39+1(e2e 환경).
- 문서 #418·구현계획·작업목록·stabilization_audit 체크 적절, 한계 서술(실 MFT overflow 강제 불가 → 계약 회귀) 정확.
- 남은 것: V14 후속 커밋(tombstone·단정·history 정정) → 확인 후 세 항목 구현 OK.

## 구현 OK (14:3x, Codex seq 1346)
- 대상: 858f766(A01 #416) / 7d0d69b(V14+A04 #417) / c77b01d(A02+A06+HN07 #418) / fe42957(A04 후속 tombstone #419 + #417 정정). HEAD fe42957, 트리 clean, push 없음.
- fe42957 검사: tombstone `abandoned_`/IsAbandoned(gen,seq,nowUs) 가 stale 가드 앞(shared_core :834), 상한 kAbandonedMax 16·TTL 5s·Reset 소거(:620), lastDeliveredSeq_ 미전진 → 배달 gap·droppedPreviousIncomplete·IDR 계약 불변, 다른 generation/seq 무영향. 단정을 RecvStats.udpStuckHeadGiveUps → rig give_ups() 로 노출해 요청 리미터 은폐 제거, S17/S18/S21/S23/S24 각각 포기 1회 단정, S24 신규(늦은 청크 재차단 없음), S23 은 30ms/청크로 hardCap 실발동.
- 검증용 직접 실행: recovery exit 0(S1~S24), 포기 로그 6건 전부 total=1 / shared_core PASS / host_encode_epoch exit 0 / log_upload exit 0 / uploader spin 프로브 CPU 0%·diag 0B. RDP 없음.
- 문서 절단 사고: 작업용 편집 헬퍼가 history.md·full_code_audit 를 0바이트로 만들었다가 복구 → 현재 790,099 B·24,237 B, HEAD 와 차이 없음(검증용 확인). 헬퍼는 "문자열 생성 후 쓰기" 로 수정됨.
- 미검증: 사용자 실기, e2e(43000 점유)·gdi_capture(간헐 3.67fps, 이번 변경 무관·원인 미확정), 실 MFT overflow 강제 불가(계약 회귀), 11:27 물리 단절 미해결.
- 다음: Codex 최종 확인 → fe42957 트리에서 0.2.104 릴리스 1회 → 산출물 sha256·임베드 버전·빌드 commit 대조.

## Codex 최종 검토 NEEDS_CHANGES (14:4x) — 구현 OK 철회, 검증용 코드 확인 완료
1. A06 배선 누락(확인): host_stage_encode_send_h264.cpp:337(surface 실패)·:348(BGRA 실패) early-return 이 :456 provenance 검사보다 앞 → 입력 수용 후 false 반환 호출에서 래치가 서면 pending 미설정, time_limit 은 pending 만 보므로 재동기 요청 유실. 확정 사양("반환 직후, 성공/units 무관") 미충족. 수정: 반환값 보관 + early-return 앞 owner 반영, 회귀는 fake transform(overflow+false+units0 / 부분 출력 뒤 false → 무입력 tick).
2. tombstone 경계(확인): IsAbandoned TTL 5s·용량 16 초과 시 같은 identity 재조립·재포기 가능 → "abandon==1" 경계에서 깨짐. 시간 TTL 단독 폐기, 의미 있는 경계(새 배달로 lastDeliveredSeq_ 통과 / generation·Reset / 용량)까지 bounded 유지. delivered 구분·wrap·다른 gen 보호 유지. 설계 변경 필요 시 근거 선회신.
3. 로그 스로틀(확인): TryProvenanceResync 가 예산 소진 경로(host_encoder_manager.hpp:141)에서 provenanceResyncFailed 를 올리지 않아 `failed==0` 조건이 매 프레임 로그를 찍음 → 시간 기준 스로틀.
4. 표기: gdi_capture 실패는 "원인 미확정" 만(무관 단정 철회, 내 이전 표현 포함).
→ 작업용 seq 1350, Codex 회신 seq 1351. 릴리스 보류 유지.

## retirement 설계 — 내 O(1) 안 철회 (Codex NEEDS_REVISION, 14:5x)
- 내 전제 "포기는 in-order head" 는 **거짓**(검증용 재확인): OldestIncomplete(shared_core:741-743)는 도착순 첫 미완성을 반환하고 수신기(:235→:268)가 그것을 포기 대상으로 사용; seq 최소는 PopDelivery(:702-706) 뿐. 재정렬 시 highWater 가 정상 앞선 AU 를 prefix 로 차단 → 안 됨.
- 수정 제안(seq 1356): 정확 identity 목록 유지·TTL 폐기 없음, 회수는 (배달 워터마크 | 승인된 active-generation 전환 | Reset | 용량 도달 시 **명시적 복구 episode** = waitForKey+reason2 1회 뒤에만 비움) 넷뿐. 포기 대상 선택 변경(N2)은 이번 범위 밖으로 남김. wrap 은 반주기 전제 명시("영향 없음" 철회). 회귀: 재정렬 보호(31 포기 후 30 정상 배달), 오랜 시간 뒤 재도착 차단, 최초 포기 identity 재도착 차단, 용량 경계 복구 1회·조용한 망각 0, 워터마크 회수 뒤 stale 가드 차단, 다른 gen·앞선 AU 미차단.
- 작업용은 (1)(3)(4) 만 진행 중, tombstone 변경 보류, 릴리스 보류.

## 포화 정책 재제안 + 배선 근거 (seq 1358, Codex 확정 대기)
- 배선 근거(검증용 직접): `assembler.Reset()` 은 수신 경로에서 호출 0건(세션 종료/소멸이 사실상 유일 경계) → Reset 회수 경계는 신규 배선 필요. generation 수락은 viewer_video_receiver_frame.cpp:182 `ctx.sel.AdmitGeneration`(viewer_selection_gate.cpp:44-66)로 **디코드 단계**에서만 판정하고 어셈블러는 generation 을 키로 쓰지 않음(같은 seq·다른 gen 은 Malformed) → active-generation 회수 경계도 `NoteActiveGeneration(gen)` 같은 신규 배선 필요. 둘 다 범위 포함 여부 Codex 확정 요청(권고: 포함).
- 포화 정책: retired_ 보존 + saturated_ 래치(=1 episode). 수락 정책 = 기존 조립의 청크·FEC 계속 수락, **새 조립은 키프레임만**, 비-키 새 AU·retired 재도착은 Ignored → 포화 중 새 포기 identity 자체가 생기지 않음(저장 실패로 인한 조용한 일반 복귀 구조적 차단). admitted IDR 이 포기되면 예약 슬롯(소량)에 정확 저장, 슬롯 소진 시 새 IDR 조립도 만들지 않음(기록 폐기 없음). 해제는 검증된 복구 IDR 배달 뒤 그 워터마크가 덮는 기록만 회수, 더 새로운 것은 유지, 용량 아래로 내려가면 래치 해제.
- 회귀: cap 도달+IDR 장기 미도착+첫 retired 반복 도착 → 재조립·재포기 0·episode 1·메모리 유계·요청은 backoff 만; 첫 IDR 손실 뒤 다음 IDR 로 복구; 회수 후 더 새로운 retired 재수용 0; 재정렬 30 미차단; 다른 gen 미차단.

## 포화 설계 v3 (seq 1364, Codex 반려 2건 반영 — 확정 대기)
- 반려: (1) 예약 슬롯 소진 시 새 IDR 거부 = 복구 불가 흡수 상태, (2) 기존 조립 전부 수락하면 포화 전 P 가 도착순 OldestIncomplete 로 repair 선택을 가로채고 키 플래그 허용만으로는 동시 IDR 상한이 안 생김.
- v3: 진입 시 비-키 조립 전부 폐기(포화 중 비-키 admit 금지라 재생성 불가 → retirement 기록 불요), 키 후보 슬롯 K=2 재사용(가장 오래된 것 교체, 교체·타임아웃은 "같은 episode 복구 시도" 로만 계수·retirement 아님, 헤더에 불변식 예외 명시), 재생성 방지는 saturationKeyFloor(후보 중 seq 최소) 로 더 오래된 키 거부, 중복은 진행·교체 판정에서 무시, 후속 정상 IDR 은 항상 진입 가능(흡수 없음). NACK 선택은 키 후보만 남아 옛 P 간섭 없음. 해제·회수는 NoteDecodedKey(gen,seq) 뒤 그 워터마크가 덮는 기록만.
- 배선 답변: assembler 는 run_udp 지역 객체(viewer_video_receiver.cpp:57)·receiver 는 세션마다 emplace(viewer_startup.cpp:544) → Reset 배선 불요. NoteActiveGeneration 제안 철회(회수는 워터마크+디코드 키 신호만). "검증된 복구 IDR" 신호 = 디코더 성공 뒤 fg.note_reference_sync(viewer_video_receiver_frame.cpp:260 → viewer_frame_gate.cpp:486), 어셈블러 Completed 아님.
- 회귀 추가: 연속 IDR 실패 3회 이상 뒤 정상 IDR 로 복구(흡수 없음), 교체된 후보의 늦은 청크가 신선 후보를 밀어내지 않음.

## 포화 설계 v4 (seq 1367, Codex 경계 4건 답변 — 확정 대기)
1. 해제 신호: note_reference_sync(입력 메타) 철회 → 디코드 **출력**의 provenance 사용. DecodedFrameNv12.sampleTimeHns + sampleTimeFromOutput(mf_h264_codec.hpp:40-41), tsFromMft 일 때 decodedCaptureUs = sampleTimeHns/10 = 그 출력 사진의 capture 스탬프. `NoteDecodedKeyCapture(gen, captureQpcUs)` 가 후보 IDR 헤더 스탬프와 정확히 일치할 때만 해제; 출력 타임스탬프 미제공 백엔드면 래치 유지(흡수 아님, 후보 계속 수락). broad decoder 변경 없음.
2. floor: episode 동안 **단조 비감소**(교체·타임아웃 시 max), 후보 0개여도 유지, 수락은 strictly newer(동등 seq 재도착 거부), 옛 identity 청크가 후보 타이머·lastProgress 를 리셋 못 함.
3. generation: floor·후보를 gen 별로 분리(교차 seq 비교 없음). 새 gen 키는 옛 gen floor 무관하게 수락(승인 전 조립 허용해야 디코드 도달), 늦은 old-gen 키는 그 gen floor 로 거부·새 gen 후보 축출 불가. 어셈블러의 seq 전역 단조 가정은 **기존 제품 동작**(gen 을 배달 키로 쓰지 않음, 같은 seq·다른 gen 은 Malformed) — 별도 항목으로 제기.
4. 해제 시 더 새로운 retired 가 cap 이면 래치 유지; 워터마크 회수는 같은 gen 기록에만. 회귀: 해제 직후 잔여 후보·제거된 P 늦은 청크가 head 재차단 없음, key 입력→이전 P 출력/출력0 미해제, 지연 old-gen key 출력 미해제, 후보 전부 타임아웃 뒤 옛 키 거부·새 키 복구, 세대 전환 교차.

## 포화 설계 v5 (seq 1369) — 해제 계약을 "전송 조립 복구 경계" 로 축소(Codex 제안 수용)
- 해제 신호: 완성 키 AU 가 `AdmitGeneration==Accept`(frame.cpp:182) + `fg.admit==Decode` 를 통과한 **직후, decode 호출 전** `NoteKeyAccepted(gen,seq)`. 그 AU 자신의 헤더 값이라 provenance 문제·타임스탬프 백엔드 의존 없음, stale/거부 IDR 로 해제 불가, decode 성공 주장 없음.
- 이후 실패는 기존 배선: note_decode_failure(reason 4, frame_gate:426)+디코더 rebuild, note_decode_empty(reason 5), timestamp overflow(reason 4, :461), fg.tick(reason 7 backoff).
- 세대 상태는 **항상 1개**(최신 세대로 교체) → 무제한 map 없음. 구세대 패킷은 포화 중 Ignored, 근거는 선택 게이트가 구세대를 DropStraggler/DropWrongGeneration(viewer_selection_gate.cpp:51-66) 으로 버린다는 기존 계약.
- floor vs live 후보: **live 후보 조회 우선**(floor 이하라도 기존 수리 계속), 제거된 후보의 늦은 청크·중복은 live 아님 → Ignored, 타이머·lastProgress 갱신 불가.
- 해제 시 더 새로운 retired 가 cap 이면 래치 유지, 회수는 같은 세대 워터마크 범위만.
- 회귀: 후보 전부 타임아웃 뒤 옛 키 거부·새 키 복구 / 세대 전환 중 늦은 구세대 키가 후보 축출 못 함 / floor 이하 live 후보 수리 지속 / 해제 직후 잔여 청크 head 재차단 없음 / cap 유지 / 재정렬 보호.

## 포화 설계 v6 (seq 1371) — generation 자동 교체 철회, 선택 계약 적용
- 게이트 계약 확인(viewer_selection_gate.cpp:45-67): pending&&awaitingAck → 전부 거부 / pending&&expectedGen!=0 → 정확 일치 / 비pending&&activeGen!=0 → 정확 일치 / activeGen==0 → 전부 허용. 최신 우선 아님 → 내 자동 교체 철회.
- v6: 수신기가 admissibleGen 을 계산해 전달, 포화 중 후보 생성·교체·floor 갱신은 gen==admissibleGen 에서만(그 외 Ignored·상태 불변), pending&&awaitingAck 구간엔 교체 없음. legacy activeGen==0 은 episodeGen 고정 + 무진행 hardCap 초과 시 1회 재고정(흡수 방지, O(1)).
- retired cap liveness: 승인된 경계에서 admissibleGen 이 바뀌면 입력 가드로 다른 gen 을 거부하고 **그 가드 하에서** 다른 gen 기록을 회수(망각이 아니라 가드 대체) → 구 gen 기록으로 cap 이 유지돼 P 가 영구 거부되는 문제 해소. 같은 gen 회수는 배달 워터마크 범위만.
- 회귀: activeGen=5 에 gen6 반복(정상 5 보호), pending expected=7 에 gen8 거부, legacy 교차 gen·재고정, 구 gen retired cap → 새 gen IDR 수락 → P 재개, 거부된 IDR 로 해제 안 됨·NoteKeyAccepted identity 검증, K=2·live 우선·floor 보존·재정렬 보호 유지.

## 포화 설계 v7 (seq 1373) — generation 분리 삭제, seq 단조 근거로 축소
- 호스트 계약(코드 확인): hdr.seq = ++encoder.encodedSeq(host_stage_encode_send_h264_au.cpp:305); encodedSeq 리셋은 선언 초기화(host_encoder_manager.hpp:199)와 reconnect_tcp_data_session(host_loop_helpers.cpp:341, 호출처는 raw_send_fail/h264_send_fail 의 **TCP** 경로)뿐 → UDP 세션에서 generation 이 바뀌어도 seq 는 단조. generation 은 capture.streamGenerationState(host_stage_pop_frame.cpp:193/248)에서 오며 seq 를 리셋하지 않음. 어셈블러 수명 = run_udp 지역(viewer_video_receiver.cpp:57)·세션마다 emplace(viewer_startup.cpp:544) = seq 단조 구간.
- v7: retired·floor·후보를 **seq 만으로** 관리(gen 분리·admissibleGen 복제·구세대 가드·재고정 전부 철회). 회수는 배달 워터마크 seq 통과만으로 안전(구세대 cap 문제 소멸). 후보 K=2·floor 단조·live 우선 유지. 해제는 확정 계약(Accept+Decode 직후 NoteKeyAccepted, 전달 후보 identity 일치) 그대로 — 게이트가 거부하는 gen 은 해제 신호를 못 만듦.
- 잔여 위험: TCP 재접속의 seq 리셋(UDP 미적용) → 방어로 "새 seq 가 마지막 배달보다 반주기 이상 뒤면 포화 상태 초기화" 1줄(기존 sequence_is_newer 반주기 전제와 동일).

## 포화 설계 확정 → 작업용 위임 (seq 1375, Codex 조건 A~D)
- A: "반주기 이상이면 초기화" 삭제(unsigned 차로 재정렬/재시작 구별 불가, last=100·old=99 반례). 초기화는 새 세션 객체만, wrap 회귀 포함.
- B: 일반 retired 는 exact(gen,seq), prefix(floor)는 포화 후보 정책 한정. 배달 워터마크 회수는 UDP 전역 seq 계약+기존 stale 가드 범위에서 gen 교차 허용, 같은 seq·다른 gen Malformed 유지, 적용 범위 주석.
- C(내 오류 정정): "최종 게이트가 막으니 후보 축출도 안전" 은 거짓 — 거부될 gen 의 높은 seq 가 최종 전에 후보 축출·floor/워터마크 상승 유발. → 후보 생성·교체 전에 기존 ctx.sel.AdmitGeneration 공용 판정 호출(구조 검증 뒤), 비Accept 면 상태 불변. 어셈블러는 수신기 주입 predicate 로 연결(Android 미설정), 경쟁 한계는 최종 수락 계약과 구분 문서화.
- D: 잘못된 gen 해제 0 + 정상 후보/floor/워터마크 불변, old99/last100 초기화 0, wrap, 후보 0에서 floor 보존, accepted key 뒤 P 정상.
- 유지: K=2 실제 상한·재사용, floor 단조·보존, live 후보 우선, 늦은 중복 무시, 해제는 Accept+Decode 직후 NoteKeyAccepted(전달 후보 identity 일치), decode 실패는 기존 reason 4/5/7.

## 5549d7c 검사 (15:0x) — 부분 통과, (2)(d) 재반려
- 통과: (1) NoteProvenance 를 surface/bgra early-return 앞 + 배치 밖에 배치, (3) 지연 로그 1s 시간 스로틀, (4) gdi 문구 "원인 미확정", TTL 삭제·배달/다른 gen 은퇴.
- **미해소**: `native_video_client_shared_core.cpp:656` `while (abandoned_.size() >= kAbandonedMax) abandoned_.pop_front();` + 헤더 :401-402 "bounded hole" — Codex 3회 반려한 조용한 망각. seq 1378 로 확정 설계(seq 1375) 교체 재지시.
- seam 은 이미 승인(Codex, 내 seq 1366) — 작업용이 재질의해 재확인 통지.
- 부하 흔들림 2건(검증용 확인): S15 `WaitRealSenderHeld(2000)`(viewer_udp_recovery_test.cpp:1446)은 **1287883 에서 도입된 기존 시나리오**, epoch [3] holdKick 단정은 **1607404 도입** — 둘 다 이번 변경 이전 자산이고 실 MFT/실 sender 타이밍 의존. "원인 미확정·미검증" 으로 history 유지, PASS 로 상쇄 금지. 필요 시 대기 시간 상향은 별건.

## f77adad 통과 + 순서/연결 방식 결정 (15:1x)
- f77adad: 용량 pop_front·미확정 generation 은퇴 제거 확인(kAbandonedMax·pop_front 0건). 은퇴 = 같은 gen 배달 워터마크 + Reset 뿐. 병적 경우 무한 증가는 UNFINISHED/RELEASE-BLOCKING 으로 헤더·#421·구현계획·작업목록 1.7 [~] 표시(완료 표시 없음). 포화 설계가 유계화 예정.
- 순서: (B) 포화 설계 → (A) seam. 근거: (B) 가 릴리스 차단, (A) 는 회귀 커버리지; 파일도 분리(shared_core/receiver vs codec/host stage).
- (A) 연결 방식: (ii) 제품 함수 **순수 추출** 승인(Codex "연결 방식 설명 후 진행" 범위). 조건 5가지(동작 변경 0·인자 최소·fake transform 으로 실제 overflow→drain 실패·추출 전후 기존 테스트 동일·재현 불가 시 사유 선회신). Codex 에 통지(seq 1383).
- (B) predicate: AdmitGeneration(viewer_selection_gate.hpp:75)을 수신기가 std::function 으로 주입, 호출은 구조 검증 뒤·후보 생성 전, Android 미설정, 최종 수락 재검증 유지·경쟁 한계 주석 분리.

## ac48758 (포화 설계) 검사 — 통과 (15:2x)
- 구현 대조: cap = episode 트리거(축출 없음, GiveUpIncomplete 에서 기록 폐기 0), 진입 시 비-키 조립만 폐기, 후보 생성·교체는 saturationAdmit_ 통과 시에만·floor 는 물러난 후보 seq 로 단조 상승, IsAbandoned exact(gen,seq), NoteKeyAccepted 는 이 episode 가 배달한 후보 identity 일치 시에만 해제·같은 gen 의 그 seq 이하만 회수·남은 기록이 cap 이면 saturated_ 유지, 초기화는 Reset 뿐. 해제 호출 지점 viewer_video_receiver_frame.cpp:237-245 = FrameGateVerdict::Decode 직후·decode 전(계약대로).
- 검증용 직접 실행: shared_core PASS, recovery exit 0·포기 6건 전부 total=1. RDP 없음. 작업용 보고: 빌드 exit 0, sweep 39+1(e2e 환경).
- 남은 릴리스 차단: A06 early-return 실행 회귀(seam) 하나. 조건은 seq 1387.

## (A) seam 구현 사전 확인 (커밋 전 트리, 15:3x)
- RunEncodeCall(host_encoder_manager.hpp:166-168)이 encode 호출 + NoteProvenance 를 함께 소유, {ok, provenanceLatched} 반환. stage surface(:299)·BGRA(:351) 양쪽이 래퍼 경유, early-return(:345·:366)에 provenance 호출 없음 → 우회 불가 구조.
- seam: mf_h264_codec.cpp:1516-1528, testTransform_ 설정 시에만 열거 skip(backend "test_transform"), 미설정이면 기존 경로·순서 불변, ComPtr 소유.
- 커밋 후 검사 항목: (i) seam 미설정에서 기존 [1][2][2b][3][4] 동일, (ii) overflow 가 실제 65입력 수용 로그로 발생, (iii) units0/부분출력 두 변형에서 false 여도 gate 차단·pending 유지, (iv) 무입력 tick 3상태(예산 소진→실패→성공 해제), (v) history 의 한계 서술이 실행 회귀 vs 코드 대조로 분리.

## f13887b (A06 seam 회귀) 검사 — 통과 → 구현 OK 재발행 (15:4x, Codex seq 1395)
- 검증용 직접: host_encode_epoch_test exit 0 — [5] 두 변형 모두 실제 overflow(로그 "overflow after 65 accepted inputs") 뒤 gate closed=1 pending=1; [1][2][2b][3][4] 는 backend=mft_enum_hw([5]만 test_transform) → seam 미설정 경로 불변. host_sender_epoch_test PASS(추출 동일성). recovery exit 0·포기 6건 total=1, shared_core PASS, log_upload exit 0, uploader spin CPU 0%. RDP 없음.
- 코드: stage 의 provenance 호출은 :476 한 곳뿐이고 두 early-return 은 래퍼 내부 처리 → 우회 불가. RunEncodeCall 이 encode+provenance 소유, seam 은 testTransform_ 설정 시에만 열거 skip·ComPtr 소유.
- 문서: #423 ④ 에 실행 회귀 vs 코드 대조 구분 명시, 구현계획·작업목록 갱신.
- 대상 commit: 858f766 / 7d0d69b / fe42957 / 5549d7c / f77adad / ac48758 / c77b01d / f13887b. 산출물 없음(0.2.104 미수행).
- 미검증: stage 호출 배선은 코드 대조, 환경 실패 2종, 부하 흔들림 2건(기존 자산, 원인 미확정), 사용자 실기, 11:27 물리 단절 미해결, A03·A05·HN01·HN04·N2·seq 단조 가정은 범위 밖.

## 구현 OK 철회 — Codex 지적 6건 전부 확인 (15:5x)
내 ac48758 검사는 설계 형태만 보고 경계 동작을 놓침. 확인(shared_core.cpp):
1. GiveUpIncomplete:681-689 saturated_ 무관 push_back → 포화 중 후보 실패가 retired 무한 증가(같은 episode 시도 분리 없음).
2. :941-953 후보 2개일 때 incoming 신선도 검사 없이 min-seq 제거 → 재정렬 키가 진행 중 후보 축출. assemblyIt(:914) 가 deque(hpp:459) erase(:952) 로 무효화된 뒤 :955 비교 → UB.
3. :926/:948 floor 가 saturationFloorGen_ 동일 gen 한정 → 전역 seq floor 아님.
4. NoteKeyAccepted:659-664·배달 회수 gen 한정, 주석 "다른 gen 미확정" → seq 전역 회수 미반영, 구 gen cap 반례 잔존.
5. :726 deliveredKeyCandidates_ push 는 배달마다, clear 는 해제·Reset 뿐 → 거부되는 완성 키 반복 시 무한 증가.
6. :690-696 진입 시 키 조립 K=2 트림 없음(hold 8), :665-669 은 cap 유지 시에도 floor 무조건 초기화(단조 위반).
→ 작업용 seq 1399(위치·수정 방향·결정론 회귀 6종, 수정 전 FAIL→후 PASS), Codex seq 1400 철회 통지. 릴리스 금지 유지.

## adeffe7 (경계 6건) 검사 — 통과, 구현 OK 재발행 (16:0x, Codex seq 1405)
- 코드: (1) :695-701 포화 중 미기록·키면 RaiseSaturationFloor, (2) admission 이 find_if(:983) 앞으로 이동해 iterator 재계산 + 교체는 `!sequence_is_newer(packet.seq, oldest->seq)` 면 Ignored, (3) saturationFloorGen_ 0건, (4) :668-670 회수에서 generation 제거·주석 seq 단조, (5) kDeliveredKeyCandidateRing=8·:754 pop_front·보수적 동작 주석, (6) 진입 K=2 트림 + :674-679 래치 유지 시 floor 유지.
- 검증용 직접: 빌드 no-op 로 바이너리=adeffe7 확인, shared_core PASS(bounds 전부), recovery exit 0. **주의(내 grep 오류 정정)**: 로그 전체에서 `total=` 을 세면 호스트의 keyframe-request 줄까지 잡혀 42건으로 보였으나, 시나리오 블록 단위로 집계하면 S17/S18/S20/S21/S23/S24 각 1회·동일 identity 반복 0.
- 작업용 보고(미재실행): bnd_prefix.log 에 bounds(1) 16→36·bounds(2) FAIL + Segmentation fault(139), 수정 후 PASS ×3, 빌드 exit 0, sweep 38+2.
- 미검증: A06 stage 배선은 코드 대조, 환경 2종, 부하 흔들림 2건(기존 자산), 사용자 실기, 11:27 미해결. 범위 밖: A03·A05·HN01·HN04·N2.

## Codex 최종 확인 → 0.2.104 후보 생성 승인 (16:1x)
- Codex 가 f13887b..adeffe7 diff 직접 확인, 지적 6건 수정 승인. 실행 검증은 검증용 직접 결과와 작업용 보고를 구분해 수용. 릴리스 후보 생성 승인(설치·라이브 중지·배포·push 는 승인 아님).
- 정정 지시 1건: `NoteKeyAccepted` 주석의 "decoder's new reference" → decode 전 transport admission 이므로 "조립/게이트 수락 경계, decode 성공 아님" 으로(주석·문서만).
- 표기 제한: e2e/GDI·holdKick·S15 는 원인 미확정·미해소 그대로, "전수 PASS" 금지, 11:27 물리 단절 해결 보장 아님, 산출물 검사 전 task 완료 금지.
- **작업용 주소 재변경**: 2ic5x3em → **d2453q5i**(세션 재시작). 인계 + 릴리스 지시 seq 1412 발송(seq 1411 은 미전달).
- 다음: 설치본 생성 뒤 payload sha256·임베드 버전·빌드 commit·크기·트리 clean 대조 → Codex 산출물 최종 보고.

## 0.2.104 산출물 검사 — 통과 (16:2x, Codex 보고 seq 1414)
- commit 8d9ec74(부모 adeffe7), 트리 clean. 제품 변경 = product_version.hpp 1줄 + shared_core.cpp:665-670/.hpp:355-359 주석(Codex 지정 문구). 로직·wire·ABI 0.
- 설치본 dist/GNLinkSetup-0.2.104.exe 3,475,968 B sha256 039b3a47…dff2f = build-local Release GNLinkSetup.exe(검증용 재계산 일치). 임베드 "0.2.104" 3건, "0.2.103/0.2.102" 0건.
- payload 6종 모두 Release 빌드와 sha256 일치·설치본에 원본 포함(Host 98510ac8/Client ff4efccc/Viewer aa8f540d/Stream 4949313c/InputService 1fd2aa56/Capture 253734df). Host·Client 에 0.2.104 1건씩.
- #426: 포함 커밋 9건·릴리스 트리 명시·"재실행하지 않음"·"전수 PASS 아님"·미해소 4종·A06 배선 코드 대조·11:27 보장 아님·작업목록 1.8 신설. 과장 표현 없음.
- 재실행 안 한 것: sweep·단위 회귀(제품 로직 무변경). 필요 시 8d9ec74 기준 sweep 1회 가능.
- 다음: Codex 판단(설치 여부·2차 캡처·후속 설계 A03/A05/HN01/HN04/N2). 설치·push·배포 없음.

## 종결 — Codex 최종 확인 완료 (16:3x)
- Codex 가 8d9ec74 HEAD·clean, adeffe7 대비 제품 diff(버전+주석), 설치본 3,475,968 B·sha256 039b3a47… 를 직접 대조해 일치 확인. payload·임베드·회귀는 검증용 직접 근거로 구분 수용. **Windows 안정화 1차 구현 + 0.2.104 후보 생성 완료 승인.**
- 최종 commit 10건: 858f766(A01 #416) / 7d0d69b(#417) · fe42957(#419) · 5549d7c(#420) · f77adad(#421) · ac48758(#422) · adeffe7(#425) [V14+A04] / c77b01d(#418) · 5549d7c · f13887b(#423) [A02·A06·HN07] / 8d9ec74(#426 릴리스 후보).
- 산출물: dist/GNLinkSetup-0.2.104.exe 3,475,968 B sha256 039b3a47…dff2f, 제품 소스 = 8d9ec74. 미설치·미배포·미push.
- 미완 유지: 사용자 실기, 미해소 4종(e2e 43000 점유·GDI 3.6fps·epoch [3] holdKick·S15 WaitRealSenderHeld), 11:27 물리 단절 원인, A06 stage 배선은 코드 대조까지. 범위 밖: A03·A05·HN01·HN04·N2·어셈블러 seq 전역 단조 가정.
- 추가 테스트 반복 불필요. 설치·라이브 중지·push·배포·2차 캡처·후속 구현은 대기.
