# Codex 재시작 체크포인트 — 2026-09-08 13:0x (검증용 Claude remote#865h2pvp 작성)

목적: Codex(remote#8cotzdve) 세션이 멈춰 재시작할 때, 검증용이 Codex 로부터 받았거나 Codex 에 보낸 확정 사항·진행 상태를 새 Codex 세션에 그대로 다시 전달하기 위한 요약. 세션 ID 는 재시작 시 바뀌므로 새 Codex 는 `a2a_agents` 로 검증용claude / 작업용claude 를 다시 찾아 pin 할 것.

## 0. 현재 버스 주소 (2026-09-08 13:41 재시작 후 갱신)
- **Codex: remote#fqvxnfkm** / **작업용 Claude: remote#2ic5x3em** / **검증용 Claude: remote#srb0qig4**(이 문서 작성자, a2a_whoami 확인).
- 재시작 전: Codex 8cotzdve, 작업용 nt5lxb96, 검증용 865h2pvp. 구 task(t-thjz1nw2·t-uhqkw7h9)는 당사자 세션이 바뀌어 상태 변경 불가 — 새 세션에서 이어받거나 재발급.
- 13:41 전달 완료: 새 Codex 에 §3 요약(seq 1328), 새 작업용에 남은 순서(seq 1329).

## 0-b. 재시작 전 주소
- Codex: remote#8cotzdve (멈춤). 버스에 remote#xe5umloy (kind codex) 도 보였음 — 어느 쪽이 살아있는지 새 세션에서 확인.
- 검증용 Claude: remote#865h2pvp (이 문서 작성자). 작업용 Claude: remote#nt5lxb96.
- 절차: AGENTS.md "Agent Role Separation (3역할 절차)" + CLAUDE.md 행동 기준. 계획 변경이 필요한 부분은 Codex 확정 전 위임 금지, 변경 없이 OK 한 독립 범위는 검증용이 작업용에 직접 위임.

## 1. 저장소 상태
- 브랜치 refactor/viewer-split. HEAD **858f766** fix(log-upload) (A01) — 그 아래 abd136e/4a92d5e/fd1f220(11:27 캡처 문서), be43e09/c09e4d0(Codex 직접 문서), edb1ec9(0.2.103 후보 마지막 코드 commit). push 없음. 설치본 dist/GNLinkSetup-0.2.103.exe — **사용자 설치·실기 보고 있음(09:57/11:27 세션)**, 실행 바이너리 해시는 미확인(Codex 정정 2026-09-08 13:5x). 저장소 문서상 "결함 포함 후보" 표기는 유지.
- 작업 트리: 작업용이 V14+A04(fix(viewer)) 를 작업 중(미커밋: native_video_client_shared_core.cpp/.hpp, udp_video_nack.hpp, viewer_video_receiver.cpp, viewer_udp_recovery_test.cpp). A02+A06 미착수.
- 제품 버전 0.2.103. 릴리스는 세 항목 뒤 0.2.104 설치본 1회 예정. 설치·라이브 세션 중지·서버 배포·push 금지 유지.

## 2. 진행 중인 task (버스)
- **2026-09-08 13:5x**: 새 Codex(remote#fqvxnfkm)가 t-thjz1nw2 를 연속으로 인수(중복 task·구현 재발급 없음). A01 은 검증용 직접 검사 결과로 인수(Codex 재실행 아님). V14+A04 는 §3.2 최종 조건·회귀 통과 뒤 commit, 초기 120ms 포기안 회귀 금지. A02/HN07/A06 §3.3 유지. 2차 캡처·후속 설계는 보류.
- **t-thjz1nw2** (Codex 8cotzdve → 검증용): "Windows 안정화 1차 수정 계획 검토 → 작업용 구현·재시험 후보". 상태 working. Codex 재시작 시 이 task 는 새 세션에서 다시 넘기거나(권장) 새 Codex 가 이어받는다고 선언.
- **t-uhqkw7h9** (검증용 → 작업용 nt5lxb96): A01 + V14/A04 + (추가) A02/HN07/A06 구현. 상태 working. 작업용은 검증용에게만 보고(Codex 직접 최종 요청 금지).
- 완료된 것: 캡처 건 t-2z9fjuqu(검증용 completed 최종 보고), t-2occguzn·t-gmwzjgy1(작업용 completed), 감사 건 t-cb7l51pj.

## 3. Codex 가 확정한 계획 (새 Codex 에 그대로 재확인 요청할 내용)
### 3.1 A01 log_upload busy loop — **완료·검증 통과** (commit 858f766, history #416)
- 대기 조건 `stopping || wakeSeq != seen || (credentials && !authRejected && queuedBytes >= batchMaxBytes)`, next_job false 시 timed wait 복귀, idle-alive diag 60s 스로틀, LogUploadStatus.workerCycles. 계약(ownerEpoch·configGeneration·늦은 401/500 격리·재시도 3회/2·4s/60s·hold 4·큐 4MB) 불변.
- 회귀 [10](제품 기본값 192KiB/2000ms/4MB): 수정 전 FAIL(2s 에 workerCycles +364,024·diag +1MB·CPU 1,984ms) → 수정 후 3회 PASS(+1·0B·0ms). 검증용 직접: log_upload_test exit 0(.claude/verify_a01_log_upload_test.log), probe_uploader_spin 재빌드 CPU 0%·diag 0B(.claude/probe/probe_uploader_spin_postfix.out).

### 3.2 V14+A04 Windows 수신 recovery — **확정 규칙 구현 완료, 미커밋**(13:41 검증용 확인: GiveUpIncomplete(gen,seq)·lastProgressUs·spent_for(hasTail)·RTT replyAllowance clamp·terminalMin 계산·hardCap 5s 정규화 모두 트리에 반영). 남은 것: 하네스 segfault 수정 확인 → 재빌드 → 회귀 실행 → fix(viewer) 커밋 #417.
- 공통 시간 maintenance: run_udp 루프 상단에서 모든 경로(video/control/malformed/ignored/continue/timeout)가 drain_deliveries → maybe_send_nack → fg.tick 을 통과, elapsed 기준 rate-limit ≥5ms, timeout 경로는 force. (작업 트리에 이미 구현됨, 유지)
- **N3 head 포기 규칙(Codex 최종 확정)** — 후속 완료 AU 가 없는 미완성 head 에만: ① 스케줄러가 그 AU 의 마지막 적용 단계까지 소진(tail 있으면 tail 3라운드, 없으면 hole 3라운드; `exhausted_final_phase`) ∧ ② now ≥ 마지막 실제 NACK 송신 + replyAllowance ∧ ③ now ≥ lastProgressUs(새 data chunk 또는 FEC 복구; 중복/control 은 진행 아님) + replyAllowance ∧ ④ now ≥ firstPacketUs + terminalMin. ⑤ now ≥ firstPacketUs + hardCap 이면 진행과 무관하게 포기(한 episode, 기존 backoff, 재요청 폭주 금지).
  - terminalMin = tailGraceUs + maxRounds×roundUs + replyAllowance (고정값 금지; 기본 120+3×25+50 = 245ms).
  - replyAllowance = 최근 유효 control RTT 가 있으면 max(50ms, 2×RTT), 튜너블 상한 1s clamp; stale(≈5s)/unknown → 50ms. RTT 는 제어 스레드가 갱신하는 atomic(갱신 시각 포함).
  - hardCap 기본 5s(튜너블), 설정값은 terminalMin 보다 작지 않게 정규화. 실패 예산이지 정상 전송 보장이 아님; 1s 넘게 진행 중인 정상 AU 는 보존·완성.
  - NACK 미협상/비활성 → ① 을 기다리지 말고 ③+④ 로 유계 폴백. waitForKey 중 비-key head → ③ 만으로 포기 가능, 새 IDR 요청/reset 을 head 마다 반복하지 말 것(alreadyWaiting 억제), 뒤 미완성 IDR 수리 기회 보장.
  - 포기 API `GiveUpIncomplete(gen, seq)`: 판정에 쓴 동일 identity 만 제거("oldest"/도착순 다른 대상 삭제 금지), 판정 뒤 완료됐으면 취소. 포기 후 discontinuity(waitForKeyFrame + reason 2, 기존 리미터·500ms→2s backoff).
  - A03(후속 완료 AU 있을 때 hold 120ms 만료 정책)은 불변 — 범위 밖.
  - 진단: 포기 로그·회귀 출력에 사용한 RTT·replyAllowance·terminalMin·hardCap·사유(①~⑤) 명시.
- 회귀(제품 run_udp 경로, 결정론): S16 keyframe wait + 5ms control noise 무영상 → tick 재요청; S17/S18 정적 화면 마지막 P/IDR tail 손실·NACK 무응답·후속 없음 → 마지막 라운드+allowance 뒤 폴백 1회·회복; 후속 없는 tail 손실에 응답 도착 → IDR 요청 0 으로 완성; S19 RTT 150~300ms → 늦은 tail 응답 수락; S20 hole 소진 후 tail 단계 잔존 → 조기 포기 금지; S21 NACK off 정적 손실 폴백; S22 1s 넘게 진행 중인 큰 AU 보존; S23 hardCap 경계 1 episode; shared_core: 중복 chunk/control 은 progress 아님, 포기 대상 identity 일치; 열린 current 체인 + known-old AU → 다음 P 정상(HN07). 기존 S1~S15 PASS 유지.
- 검증용 개입 기록: 작업용의 첫 구현은 철회된 초기 규칙(`roundsSpent || holdPast`, holdPast = first+120ms ∧ !busy)이어서 커밋 전 중지·재작업 지시(작업용 수정 전 로그 .claude/n3_prerule_recovery.log: S19 202ms 포기, S20 101ms hole 소진만으로 포기, S21 폴백 없음, S22 진행 중 IDR 235ms 포기 → 11 FAIL). 현재 확정 규칙 적용·빌드·단독 5회 진행 중(13:0x).
- 결론 문구 제한: "제어채널 자체 블록/경로 blackhole 을 이 수정으로 해결" 주장 금지.

### 3.3 A02+HN07+A06 encoder provenance — **미착수(작업용 순서상 V14 뒤)**, 확정 사양
- mf_h264_codec: pendingInputSampleTimesHns_/Synthetic_/Epoch_ 세 deque 를 `struct PendingInput{tsHns, synthetic, epoch}` 하나로 통합 — push(≈1927), 빈 출력 pop(≈1827-1834, 현재 epoch 누락 버그 = 감사 F1), 정상 pop(≈1864-1870), overflow trim(≈1931-1936), initialize/shutdown clear 모두 한 곳. 디코더 측 FIFO 는 범위 밖. wire ABI(H264AccessUnit·UdpVideoChunkHeader) 불변.
- A06 overflow(감사 C2): trim 대신 `provenanceInvalid_` 래치 — overflow 감지 즉시 set + FIFO clear, 이후 모든 AU inputEpoch=0. 래치 해제는 **initialize()(shutdown+initialize 재초기화) 성공 시에만**(기존 MFT 보유 출력·metadata 가 함께 비워진 뒤). 접근자 `provenance_invalid()` + EncodeStats 노출.
- stage(host_stage_encode_send_h264.cpp): 매 encode 호출 **반환 직후, 배치 루프 바깥**(outUnits 가 비어도)에서 확인 → EpochGate 닫기(awaitingKey + 상태 provenanceInvalid) + 기존 ResetEncoder 경로로 **즉시** 재동기 요청(공통 예산 3회/10s 우회 금지). 예산 소진이면 pending/backoff 로 두고 invalid 유지(새 입력 push·새 epoch·key 를 유효 provenance 로 꾸미거나 gate 열기 금지), 허용 시각이 되면 main loop tick 에서 재시도(새 영상 도착에 의존하지 않음). invalid 동안 배치 출력 전부 송출 금지. 재동기 성공 뒤 forceKeyNext → 새 current IDR 로 재개. 실제 MFT flush/rebuild 는 codec 호출 반환 뒤 owner 안전 지점에서.
- gate(host_epoch_gate.hpp): **HN07 축소판** — gate 가 검증된 current IDR 로 열린 뒤 폐기가 발생하면 **unknown(0)/future 등 현재 참조 체인을 무효화할 수 있는 출력만** awaitingKey 재진입 + forceKeyNext(기존 300ms in-flight 래치·reset cap 유지); provenance 가 확실한 이전 epoch AU 의 조용한 폐기(DropOldEpoch)는 체인을 다시 닫지 않음(불필요 IDR 방지). provenanceInvalid 중에는 태그 전부 불신 → 겉보기 current key 도 AcceptKey 하지 않음. 정상 gate 의 늦은 valid-current-IDR 우선 수락 유지. kAwaitMaxDropped 12/700ms 한도는 invalid 아닌 경우에만.
- 회귀(host_encode_epoch_test fake transform 경계 + gate 단위, 결정론): a. 빈 출력 1회 뒤 flush → 새 IDR AcceptKey(수정 전 FAIL drop-old-epoch); b. overflow → 빈 출력 호출에서도 재동기 요청 발생; c. overflow 뒤 pre-flush key 미수락; d. 예산 소진 + 정적 무입력 → pending·invalid 유지·송출 0, 허용 시각 뒤 재시도; e. 재동기 실패(initialize 실패) → invalid 유지; f. invalid 중 새 key 도착 → 미수락; g. 재동기 성공 뒤 새 IDR 수락·P 재개; h1. gate 열린 뒤 unknown 폐기 → 다음 P 폐기·IDR 재강제; h2. 열린 current 체인 + known-old AU 폐기 → 다음 current P 정상. 기존 [1][2][2b][3]·host_sender_epoch·recovery S14/S15 PASS 유지.
- 범위 확장 금지: HN01(latest-content 보존), HN04(partial-output 결과형), 수명 재설계, A03 hold/tail, rate/pacing/자동재접속, Android/서버/installer/util/2차 캡처.

### 3.4 릴리스·보고 규칙
- 단계별 scoped Git MCP commit + history(#416 A01, #417 V14/A04, #418 A02 예정)·구현계획 체크·작업목록 갱신·docs/stabilization_audit_2026-09-08.md 체크.
- 세 항목 뒤 전체 Release 빌드 1회 → 0.2.104 설치본 1개(payload sha256 = Release 빌드, "0.2.104" 임베드, 제품 소스 = release commit). 설치·라이브 세션 중지·서버 배포·push 금지. 테스트 전 qwinsta(RDP 접속 시 판정 무효). 환경 실패(e2e 43000 라이브 호스트 점유, GDI 캡처 간헐)는 분리.
- 작업용 → 검증용 직접 검사·수정 반복 → 검증용 구현 OK(commit 해시·범위·직접 검증/보고 구분·산출물 sha256·잔여 위험) → Codex 최종 확인. 이번 릴리스는 확인된 코드 결함 수정이며 11:27 물리 단절 해결 보장 아님.

## 4. Codex 재시작 후 새 세션에 보낼 요약 메시지(초안)
"검증용 remote#<현재>. 이전 Codex(8cotzdve)와 진행 중이던 t-thjz1nw2(Windows 안정화 1차) 상태: A01 완료·검증 통과(858f766, #416). V14+A04 는 Codex 확정 규칙(N3 ①~⑤·replyAllowance RTT 기반·terminalMin 계산식·hardCap 5s·GiveUpIncomplete identity·NACK off 폴백·회귀 S16~S23)으로 작업용이 재작업 중(초기 규칙은 검증용이 커밋 전 중지). A02+HN07+A06 는 확정 사양(레코드 FIFO 통합·provenanceInvalid 래치·즉시 재동기 요청·예산 3회/10s 준수·HN07 unknown/future 만 재차단)으로 위임됨, 미착수. 세 항목 뒤 0.2.104 설치본 1회. 확정 사항 전문은 .claude/codex_restart_checkpoint_20260908.md §3. 새 세션이 t-thjz1nw2 를 이어받는지(또는 새 task 로 재발급) 확인 요청. 다음 Codex 판단 필요 항목: (1) 구현 OK 뒤 최종 확인, (2) 2차 패킷 캡처 방법(보류 중), (3) 후속 설계(A03 hold/tail·HN01·HN04·pacing·자동재접속)."

## 5. 참고 산출물(모두 .claude/, 미추적)
- 감사: stabilization_full_audit.md(전수 재검토, F1/U3/N1/N2/N3/C2 등), probe/(gnlink_pcap.py, probe_uploader_spin, probe_nack_trace(A04-1~4 기준선), probe_fifo_model).
- 캡처: packet-capture-20260908/(etl/pcapng sha256 46e9460c…/895b4d76…, verify_notes.md §1~§8, 작업용 report_1127.md·capture_coverage.md 정정본, Start/Stop-GnlinkCapture.ps1 2차 helper 준비만), field-20260908-1127/(NAS 사본).
- 이번 라운드: win_stab_round1_review.md(계획 판정·체크리스트·A01 검사·A04 개입 기록), a01_*·v14_*·n3_prerule_* 로그(작업용).
- 추적 문서: docs/field_test_2026-09-08_0.2.103.md 9절, docs/history.md #414~#416, docs/stabilization_audit_2026-09-08.md(Codex 작성), docs/full_code_audit_2026-09-08.md(Codex 작성).
