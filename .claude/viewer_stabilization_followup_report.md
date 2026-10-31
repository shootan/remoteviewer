# 화면 안정화 후속 수정 최종 보고 — P10 · P9 · P11 (A2A task t-xox45oo5)

기준 HEAD a18708c → 릴리스 0.2.101. 브랜치 `refactor/viewer-split`, push 보류. 서버 배포·라이브 프로세스 교체·설치는 하지 않음. 콘솔 세션 Active(RDP 없음)에서 테스트.

## 커밋
| 해시 | 내용 |
|---|---|
| f911845 | P10: 업로더 `log_upload_configure`(재포인팅), 401 일시정지+보관 재전송, 상한 재시도, 상태/콜백, 호스트 상태카드·클라 페이지 "sign in again" |
| e997b97 | P10 보완: 서버 sessions persist 되돌림(범위 외), owner(계정+host:port+device)/로그아웃 epoch 펜싱, 늦은 401/500 회귀 8종 |
| 03b0d13 | P9: target rect 규칙(`host_input_target_rect.hpp`) + 재시작마다 재푸시, landing 진단 예산 에피소드 재충전 |
| dbb7fc0 | P9 보강: 실제 캡처 HMONITOR 의 live rect, 1초 periodic 재확인(원점만 변경 포함) |
| 1607404 | P11: 인코더 flush epoch FIFO + `host_epoch_gate.hpp`(새 epoch 첫 송출=IDR), 뷰어 보유 재개 프레임 anchor 보정, 실 MFT 재현 테스트 |
| 23b0f3c | test: FakeHost 의 seq gap 을 다음 키프레임 앞에 적용(S7 간헐 실패 제거) |
| a9114ff | 0.2.101 버전·history #406·작업목록 실기 항목 |
| 927b9f4 | P11 보완: gate fail-closed(미상/미래 epoch)·rebuild 3회/10s 상한·sender dequeue epoch 펜스·실 sender 경계 테스트·3경로 재현 |
| 6a9aa32 | P9 보완: secure 전달 직전 live rect 재확인, 조회 실패 시 전송 거부(구 rect·0 rect 우회 차단) |
| c3c3d76 | 뷰어 보완: pending 예산 미연장·유효 stamp·수명 종료·회귀 4건 |
| ded9060 | 0.2.102 버전·history #407·정정(#401/#403/#405/#406) |
| 1287883 | sender 허용 판정을 첫 datagram 직전으로(active fence 는 현재 epoch 만 통과), P9 versioned snapshot + 단일 dispatch 경로(재probe·fallback·텍스트 포함), 실 sender 경유 뷰어 연결 S15 |
| (release) | 0.2.102 재생성·history #408 |

## 테스트 전수 결과
- 1607404 시점 sweep(`.claude/test_sweep_0.2.101.txt`): 39종 = 36 PASS + 3 FAIL. 23b0f3c 뒤 recovery 3/3 PASS → 37 PASS + 2 미해소.
- 0.2.102 sweep(`.claude/test_sweep_0.2.102.txt`): 40종 = **39 PASS + 1 FAIL**. FAIL = `udp_control_e2e_test`(udp hello ack 실패, 포트 43000) — 설치된 GNLinkHost/GNLinkStream 실행 중, 통제 재실행 없음 → 환경 영향 추정. `gdi_capture_process_test` 는 같은 라이브 호스트 상태에서 PASS(53fps) → 이전 실패는 라이브 호스트 때문이라 볼 수 없고 간헐·원인 미확인.
- `node test/run.js` ALL PASS(서버는 원래 계약).

## 항목별 확인된 원인 / 변경 / 한계
### P10 로그 업로드 401
- 원인: `log_upload_start` 가 실행 중이면 새 토큰을 버림(재등록/재로그인 no-op); 서버는 재등록 즉시 이전 토큰 폐기; 실패 배치 무재시도 유실. 클라는 비밀번호 미보관 → 자동 재로그인 불가.
- 변경: configure 재포인팅(같은 owner 는 큐 유지, owner 변경 시 폐기), 401 → pause+보관(4개)+새 토큰 재전송, 미도달/5xx/408/429 → 초기 포함 총 3회 송신(2s·4s 재시도)·60s 나이, 기타 4xx 즉시 폐기, owner epoch 로 늦은 응답 격리, 상태 표시. 서버 계약 변경 없음(persist 는 범위 외로 되돌림).
- 한계: 호스트 401 은 재인증(Change account) 필요 상태로만 표시(x-host-token 자동 갱신 경로 없음 — 호스트 재등록 시 재포인팅으로 해결). 실제 NAS 업로드 복구는 설치 후 실기.

### P9 UAC 클릭 좌표
- 원인: 시작 시 1회 읽은 주모니터 rect(RDP 중 2236x1232)를 갱신 안 함; 에이전트는 0 rect 에서만 폴백.
- 변경: 캡처 (재)시작 + 1초마다 실제 캡처 HMONITOR 의 live 물리 rect 재도출·변경 시 재푸시+로그; 창모드 0x0(secure 경로 미사용); DPI 스케일 없음; 진단 예산 12/에피소드·60/분.
- 한계: 다중/선택 모니터 실기 장비 없음; 모니터 선택이 재시작 후 주모니터로 되돌아가는 별도 결함은 ledger(입력-화면 일관성은 유지).

### P11 UAC 복귀 혼잡
- 원인(메커니즘 재현, 현장 원인 미확정): 비동기 HW MFT 가 이전 입력의 AU 를 다음 호출에 내놓고 flush 가 인코더를 비우지 않으면 복귀 첫 호출에서 전환 전 AU 가 오래된 stamp·real 로 송출된다 → 뷰어가 anchor 로 삼고 신선 burst 를 backlog 로 판정. `remote60_host_encode_epoch_test` [2] 가 이 PC 의 `mft_enum_hw` **surface 입력 경로**에서 800ms 보유를 재현했으나, 현장 호스트는 BGRA 버퍼 경로(stats `nv12SurfaceFrames=0`)였고 그 경로에서는 재현되지 않아 seq 24484 의 현장 원인은 미확정.
- 변경: 호스트 epoch gate(old-epoch 폐기·P 선행 시 재요청·상한 시 encoder reset, 보류/재송출 없음) + 뷰어 보정(hold>300ms 재개 프레임은 anchor 아님, 8프레임/500ms 상한으로 진짜 backlog 유지). secure kick 차단은 변경 없음.
- 한계: secure desktop 중 kick 차단으로 실제 AU 가 0.5s 보유되는 형상(seq 24469)의 호스트 측은 미해결(별도 설계) — 뷰어 보정은 오탐 IDR 비용을 줄일 뿐 오래된 픽셀의 실제 지연은 남음(S13 회귀). S14 의 FakeHost 는 CPU 입력 경로라 대개 동기식이어서 gate 의 old 폐기는 round 별 0~1건(폐기 경로 실 MFT 검증은 epoch test [2] surface 경로); 송신 경계는 실 sender 테스트(#407).

## 테스트 (실행한 것)
- 신규/변경: `remote60_log_upload_test` 15케이스, `node test/run.js`(directory/restart/relay/logs), `remote60_host_input_target_rect_test`, `remote60_secure_input_mapping_test`(+2), `remote60_host_encode_epoch_test`(실 MFT, surface 경로), `remote60_viewer_frame_gate_test`(+2), `remote60_viewer_udp_recovery_test` 14/14.
- 전수 실행 결과: `.claude/test_sweep_0.2.101.txt` (history #406 에 요약).

## 미검증 (사용자 실기)
1. 호스트 재시작 후 NAS host.log 재개, 재로그인 뒤 `log_upload.diag` `token replaced`·401 없음.
2. 회사 클라 재로그인 뒤 NAS viewer.log 연속.
3. UAC 2회: `secure_input.log` 두 번째도 `target=(0,0)/1920x1080 mapped==in`; 호스트 `epoch-gate dropped-old`·`first-frame auEpoch==curEpoch`; 뷰어 `[congestion]` 0.
4. 정적 타이핑·고화질 영상 회귀 없음.

## 설치본
`dist/GNLinkSetup-0.2.102.exe` (0.2.101 은 설치 전 대체됨). 설치/배포는 사용자 지시 후.

## 보완 회차 2(Codex seq 1062/1064) 요약
- sender: 허용 판정 = pacing 뒤 첫 datagram 직전; active fence 는 epoch 이 현재와 같을 때만 송신(old·미태그·미래 폐기), legacy 통과는 fence 비활성 시만. 예외 = "flush 전에 첫 datagram 이 허용된 AU 1개"(허용 검사~첫 sendto 의 µs 창은 닫지 않음, metadata 유지, 단일 스레드라 동시에 1개). 실 sender 테스트 [B'](dequeue·검사 뒤 첫 datagram 직전 정지→flush→재개 → 0 datagram) 추가.
- P9: `InputTargetSnapshot`(version·mode·HMONITOR·gen, lock) + `secure_dispatch`(주입 포트) + 브로커 `…WithRect`(검증 rect 를 메시지에 stamp, 한 lock 에서 write). secure 분기·재probe·default fallback·텍스트 4곳 모두 이 경로. 조회 중 target 교체·조회 실패·빈 rect·미상·창모드 → 미전송.
- 연결: recovery S15 — 제품 SenderState 스레드 경유(fence·pacing·chunking) 출력 스트림을 뷰어가 수락: round 0 held+queued old 3건 폐기·IDR 첫, round 1 in-flight 완료·IDR 다음; Congested 0·요청 0.

## 보완 회차 1(Codex seq 1059/1060) 요약
- P11 보장 범위: emit gate + sender dequeue 펜스 → "flush 이후 새로 wire 에 시작되는 AU 는 새 epoch IDR 이 첫 번째"; 송신 중이던 AU 1개는 완료(in-flight 예외; 뷰어 held-resume 규칙이 그 stamp 를 anchor 로 쓰지 않음). 미상(0)/미래 epoch fail-closed. rebuild 3회/10s 상한. 실 sender 테스트 [A]/[B]/[C].
- 재현 경로: BGRA 버퍼(현장 경로) 미재현, NV12 버퍼·surface 재현 → 현장 원인 미확정.
- P9: dispatch 직전 live rect 확인·갱신, 조회 실패 시 거부. 실제 소켓 경로는 배선만(단위테스트 없음).
- 뷰어 pending: 예산 미연장·유효 stamp·수명 종료; host hold 작음+renderer 정체 backlog 회귀 유지.
