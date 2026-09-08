# 0.2.103 안정화 코드 재검토 — 수정 필요 사항 원장

기준 소스: `edb1ec9d12b8abd9cae9eed0400fbff455cbb438` (제품은 `acd8233`, 이후 테스트·문서 변경). 2026-09-08 작성.

사용자 요청에 따라 Codex가 코드와 로그를 직접 대조하고 작성했다. 최초 전수 감사는 `4206161..edb1ec9` 71개 변경 파일에 대한 검증용 Claude의 검토였으며, 그 보고를 그대로 제품 원인으로 채택하지 않았다. 아래 주요 항목은 Codex가 실제 분기·호출자를 재확인했다. 이번 실기 조사는 [별도 보고서](field_test_2026-09-08_0.2.103.md)에 기록한다. 사용자 정정 이후 타 에이전트 위임은 중단했고, 문서화는 Codex가 직접 수행했다. 일반 운영 정책을 영구 변경하는 작업은 아니다.

**상태: 조사·문서화 완료, 아래 제품 수정은 미실행.** 기존 테스트 PASS를 아래 조건의 검증으로 취급하지 않는다. 코드 결함의 존재와 실제 현장 증상의 최초 원인 입증을 구분한다. 우선순위 P1은 다음 안정화 작업에서 먼저 처리할 항목이며, 모든 조건의 현장 발생률을 뜻하지 않는다.

## 1. 우선 수정 항목

### A01 / P1 — 인증 거부 상태에서 업로더 busy loop

- 코드: `apps/native_poc/src/log_upload.cpp:198`, `:251-253`, `:264-271`. 401이면 `next_job_locked`는 작업을 반환하지 않는데, wait predicate는 인증 상태와 무관하게 `queuedBytes >= batchMaxBytes`이면 즉시 깨어난다. 다시 같은 분기로 진입하고 30회마다 파일을 열어 진단 로그를 쓴다.
- 조건: 401 일시정지 후 큐가 기본 배치 크기 192KiB 이상으로 누적. `f911845` 이후 경로. 이전 실기 전체의 원인으로 소급하지 않는다.
- 증거: 검증용이 제품 업로더를 링크한 격리 loopback 프로브를 실행했고 Codex가 소스와 `.claude/probe/probe_uploader_spin.out`을 직접 대조했다. 큐가 작을 때 3초 CPU 0초·로그 증가 0B, 약 200KB 누적 후 3초 CPU 2.91초·로그 2,372,858B 증가. 코어 하나와 디스크를 소모할 수 있다. 이번 09:57 실기의 발생 증거는 별도 확보하지 못했다.
- 수정 방향: 전송 가능 상태에서만 배치 크기로 wake, 자격증명 변경/stop은 별도 wake. 유휴 로그는 반복 횟수가 아니라 시간 기준 제한. 통신하지 않는 상태에서도 enqueue/status 호출이 오래 막히지 않아야 한다.
- 완료 기준: 기본 192KiB를 넘긴 401 장기 대기에서 CPU·로그 증가 상한 검증, 새 토큰으로 재개, owner 변경/늦은 응답 격리 기존 회귀 유지. 기존 `log_upload_test`의 600B cap 케이스만으로 완료 불가.

### A02 / P1 — 빈 인코더 출력에서 epoch FIFO pop 누락

- 코드: `mf_h264_codec.cpp:1827-1834`. `produced && !haveBytes`에서 timestamp와 synthetic만 pop하고 epoch는 남긴다. 정상 출력 `:1864-1870`과 불일치한다.
- 영향: 빈 출력 이후 세대 태그가 한 칸씩 뒤처진다. 전환 후 새 IDR이 old epoch로 폐기되고 뒤 P에서 추가 IDR 요청/드롭이 발생할 수 있다. `host_stage_encode_send_h264_au.cpp:165-180`에서 old 폐기는 재요청하지 않고, current P가 대기 중일 때만 재요청한다.
- 증거: Codex 직접 코드 대조 및 기존 deque 모델+제품 epoch gate 프로브 출력 확인. 모델에서 새 IDR 폐기→P 폐기→다음 IDR 수락. **빈 출력의 실 MFT 발생 빈도와 이번 두 영상 정지의 원인이라는 주장은 미입증**이다.
- 수정 방향: timestamp/synthetic/epoch를 한 입력 레코드 FIFO로 묶어 모든 push/pop/clear를 일원화. 단순 pop 추가 후에도 실패·빈 출력·overflow·재초기화 경로를 전부 점검한다.
- 완료 기준: 빈 출력 1회/여러 회 뒤 flush, 새 IDR 수락과 태그 정렬을 실제 제품 경로 또는 제어 가능한 MFT 경계로 검사. 복구 reset으로 우연히 치유된 PASS와 구분.

### A03 / P1 — 뒤 완료 AU가 있을 때 tail NACK 전에 hold 만료

- 코드: `udp_video_nack.hpp:34` tail grace 120ms, `viewer_startup.cpp:113` 기본 hold 120ms, `native_video_client_shared_core.cpp:675-700`, `viewer_video_receiver.cpp:209-211`, `:330-333`의 drain→NACK 순서.
- 조건/영향: AU N의 마지막 조각이 손실되고 뒤 AU가 완성되면, tail NACK 가능 시각에 drain이 N부터 폐기한다. 조각 재전송으로 해결할 손실이 참조 단절/IDR 비용으로 바뀐다. **후속 완료 AU가 없으면 head가 남아 tail NACK이 가능하므로 모든 tail 손실에 적용되는 주장은 틀리다.**
- 증거: Codex 직접 코드 대조. 기존 제품 assembler+scheduler의 가상 시계 프로브에서 N=10 tail 손실·11/12 완료 시 NACK 0건과 gap 배달 확인. 실 NIC/네트워크 재현과 구분.
- 수정 방향: tail 판단, hold 기한, NACK 라운드/RTT 여유, 8개 동시 assembly 및 byte cap을 함께 설계. hold 시간만 늘리면 8개 cap에 먼저 퇴출될 수 있다. 지연을 무제한 늘리지 않는다.
- 완료 기준: 중간 조각/마지막 조각 손실 각각, 후속 연속 도착·없음·재정렬·재전송 무응답, 완성 IDR 즉시 해제, 메모리·시간 상한. 회복 가능한 손실은 불필요 IDR 없이 복구.

### A04 / P1 — Normal 상태의 NACK 소진이 능동 복구로 이어지지 않음

- 코드: `udp_video_nack.hpp:113-119`은 소진 후 false만 반환. `native_video_client_shared_core.cpp:675-677`은 뒤 완료 AU가 없으면 만료 검사 전 반환. `viewer_frame_gate.cpp:547` 이후 `tick`은 waitForKey/Congested일 때만 재요청. `viewer_session_watchdog.cpp:70-78` link-silent는 로그만 기록.
- 영향: 마지막 AU가 불완전하고 재전송이 실패한 **Normal/waitForKey=0** 상태에서 다음 AU가 없으면 gap 배달도 없고 복구 타이머 진입도 없다. UAC/secure desktop에서는 kick이 차단되어 후속 프레임 도착에 일반적인 시간 상한이 없다.
- 이번 실기: gen2 seq1393 손실 후 `waitForKey=0`, NACK 로그 뒤 무수신, link-silent, 결국 control peer-lost. 새 reason 7 요청이 없는 것과 코드가 일치한다. 단 UDP 경로 자체가 끊긴 상황에서는 IDR 요청만 추가해도 정상 회복된다고 보장할 수 없다.
- 별개 조건: 이미 waitForKey=1인 상태에서 앞의 미완성 P가 IDR NACK을 가리는 경우에도 **fg.tick의 reason 7 폴백은 살아 있다**. assembler-only 프로브에 tick이 없다는 이유로 제품 영구 정지를 주장하지 않는다.
- 완료 기준: 마지막 P/IDR 손실·NACK 무응답·후속 영상 없음에서도 유계 시간 안에 복구 상태로 진입. 요청 backoff/episode 상한 유지. control만 생존하는 경우, 양방향 단절, secure 무입력 각각 구분.

### A05 / P2 — NACK 대상은 도착순, 배달 차단자는 seq순

- 코드: `native_video_client_shared_core.cpp:707`은 deque의 첫 미완성을 반환하지만 `PopDelivery:667-670`은 seq 최소를 선택한다. `udp_video_nack.hpp`는 이 결과 하나만 추적한다.
- 영향: N+1이 먼저 도착하면 N이 실제 차단자인데 N+1에 라운드를 소모할 수 있다. key 대기 중에는 앞 P 때문에 뒤 불완전 IDR을 조회하지 못한다.
- 증거: Codex 직접 대조 및 기존 상태추적 출력(31에 3라운드, 차단자30에 0건) 확인. 현장 패킷 재정렬의 직접 증거와는 별개.
- 완료 기준: seq wrap·도착순 역전·key wait·새 세대·중복/늦은 조각에서 실제 복구 가능한 차단자/IDR을 선택. A03/A04와 함께 검증.

### A06 / P2, 발생 빈도 미확정 — FIFO overflow의 잘못된 provenance

- 코드: `mf_h264_codec.cpp:1930-1934`는 64개 초과 시 레코드 앞쪽을 버리지만 MFT 안의 출력은 버리지 않는다. 이후 오래된 출력에 더 새 입력의 timestamp/epoch를 붙일 수 있다. FIFO가 빈 경우만 epoch=0으로 두는 `:1859-1874`로는 보호되지 않는다.
- 증거: Codex 코드 대조 및 deque 모델+제품 gate에서 old key를 new epoch로 수락하는 형상 확인. 모델은 실제 MFT/호스트 reset 전체를 재현한 것이 아니므로 실제 발생률·범위는 미확정.
- 수정 방향: overflow 시 provenance 무효 래치와 MFT 입력·출력 재동기. **deque clear만 해서는 불충분**하다. clear 뒤 새 입력을 push하면 옛 출력이 그 레코드를 소비할 수 있다. reset/flush 완료 전 epoch=0 유지 등 수명 계약이 필요하다.
- 완료 기준: overflow 후 새 입력/옛 출력 교차에서도 잘못된 epoch로 gate를 열지 않음, 복구 reset 상한 유지.

## 2. 이번 실기로 추가 확인한 수정·설계 필요 항목

| ID | 우선순위·상태 | 직접 확인 코드/증거 | 조치와 완료 기준 |
|---|---|---|---|
| A07 | P1, 복구 설계 공백 | `host_stage_stats_h264.cpp:319` 피드백 3초 신선도 + `host_abr.hpp:222-290` stale/pressure 누적. 실기 단절 후 약 5초에야 12→9Mbps | 무수신/피드백 단절을 일반 품질 지표와 분리. control peer-lost 전 유계 대응 검증. 아직 물리 드롭 원인이 이 지연이라는 뜻은 아님 |
| A08 | P1, 재접속 재발 경로 확인 | `host_stage_runtime_tune.cpp:130-142` 명시 bitrate 요청 시 high profile, `:174-175` 적용/쿨다운. 09:59:38과48에 다시12Mbps·60Mbps pacing | 사용자의 품질 상한과 재접속 초기 전송률 분리. 같은 peer 재접속에서 최근 실패를 무시하고 즉시 같은 부하를 반복하지 않도록 설계; 다른 peer/명시 설정 변경 격리 |
| A09 | P2, 순간 송신 예산 | `host_encoder_manager.cpp:92-103`, `host_net_io.cpp:94-107` 및 `:248-280` 재전송 루프. 기본 delta pacing=mean×5/floor40Mbps, IDR=100Mbps, NACK는 별도 byte bucket+무pacing 전송 | 실측 경로 용량과 FEC/헤더/재전송 포함한 전송 예산 검토. 평균 설정12Mbps는 UDP wire cap이 아님. 회사망/공유기 차단은 패킷 캡처 전 미확정 |
| A10 | P2, 정적 입력 지연 후보 | `host_kick.hpp:45` 150ms, `host_stage_encode_send_h264.cpp:363-369` 마지막 실 입력 후 arm, `host_capture_session.cpp:800-826` secure kick 거부. 호스트 표본 age102~185ms | 마지막 실 프레임 출력 지연 목표를 정의하고 비동기 출력 drain/보조 입력 정책 검토. 안전한 화면 provenance와 sparse CPU·대역 비용 유지. 단순 secure kick 허용 금지 |
| A11 | P1(진단 신뢰성), 코드 오류 | `viewer_present.cpp:245,255,299,340`, `viewer_video_receiver_frame.cpp:386`: 호스트 QPC와 뷰어 QPC를 직접 차감/음수0처리 | raw cross-clock `netUs/totalUs`를 unknown으로 분리하거나 검증된 동기화·오차범위를 사용. 이번 표본의0은 지연0이 아님. `aligned_lag_us` 상대 lag를 절대 입력 지연으로 표시하지 않음 |
| A12 | P2, 관측 공백 | `viewer_video_receiver.cpp:206-212` timeout continue가 아래 UDP 1초 stats를 건너뜀. host NACK 카운터는 존재하나 30초 로그 미출력 | 무수신에도 elapsed·recvError·lastDatagram·oldestIncomplete·NACK 소진·상태를 시간 기반 기록. 호스트 NACK received/cache hit/suppressed/send error와 목적 endpoint 기록. 정상 로그 폭증 방지 |

## 3. 기존 결함·조건부 위험과 검사 부채

- **A13 / 기존 종료 경로:** `log_upload_stop` 제품 호출자가 없고 정적 `UploaderState`에 joinable worker가 남는다. 두 셸 정상 종료 시 수명 정리가 필요하다. 특정 과거 fastfail을 이 원인으로 확정하지 않는다. stop 도입 시 configure/stop 경쟁과 무한 최종 drain도 함께 제한해야 한다. 현재 stop/configure 경쟁은 제품 호출이 없는 잠재 API 결함이다.
- **A14 / 잔여 입력 경쟁:** `host_secure_target_rect.hpp:80-88` 두 번째 snapshot과 broker send는 한 트랜잭션이 아니다. snapshot은 `host_loop_helpers.cpp:241` restart 성공 뒤 갱신된다. live rect/창 모드 gate는 완화 조건이지 모든 전환에서 무해하다는 증명이 아니다. 전환 admission 시점·in-flight 예외를 명시하고 대상 전환 중 첫 이벤트로 검증. 이번 UAC 버튼 정확 체감은 일반 경쟁 부재 증명이 아니다.
- **A15 / watchdog·UX:** `viewer_session_watchdog.cpp:29-34` 첫 poll 전 control 종료 시 everConnected가 설정되지 않을 수 있다. link-silent는 복구가 아니라 진단이고, dead-session 종료는 자동 재접속이 아니다. 정상 exit0과 장애 종료를 셸에서 구분하는 기준도 필요하다. 실제 recv error/종료·무수신·초기 연결 실패 각각 검사한다.
- **A16 / 기존 모니터 선택:** `host_stage_selection.cpp:110-131`, `host_capture_session.cpp:426,449,520` 선택 처리/재시작이 주모니터로 돌아간다. secure rect가 실제 주모니터와 일치해도 선택 기능이 정상이라는 뜻은 아니다. 다중 모니터 실기는 별도 미검증.
- **A17 / UAC 후 readback:** shared D3D 보호가 off→on, worker D3D 호출 평균이 수십µs→약7ms. `d3d_capture_readback.cpp:692-715`와 `capture_backend_dxgi.cpp:396-417` 공유 장치 호출 경합 후보. 이번에는 수백ms readback 정체/링 웨지로 영상 정지가 발생했다는 증거가 없다. 보호 OFF 복원은 기존 설계상 채택하지 않은 방안이며 별도 검증 없이 적용하지 않는다.
- **검사 부채:** 빈 출력 epoch·overflow 경로(A02/A06), 마지막 조각+후속완료(A03), Normal 무후속(A04), 192KiB 이상 auth pause(A01)가 기존 PASS로 덮이지 않았다. S14의 old 폐기 횟수 조건부/테스트 verdict 사본, sender in-flight 실제 중첩의 증명, target rect 순수 결정과 실제 dispatch 배선의 차이도 각각 명시한다. 테스트의 미실행 경로를 PASS로 세지 않는다.

## 4. 처리 게이트

- [x] Codex 직접 코드/프로브 출력 대조 및 추적 문서 작성
- [x] 0.2.103 새 실기 로그의 단절·UAC·정적 지연 경계 조사
- [x] A01 업로더 busy loop 수정·기본 크기 회귀 (history #416)
- [ ] A02/A06 입력 provenance 일원화·실패 경계 검증
- [ ] A03/A04/A05 NACK/hold/복구 episode 통합 설계·유계 회귀
- [ ] A07/A08/A09 단절 대응·재접속 초기 전송 정책 검증
- [ ] A10/A11/A12 정적 출력 지연·시간/손실 계측 개선
- [ ] 양단 패킷 캡처로 최초 UDP 유입 중단 위치 확인
- [ ] 수정 뒤 동일 실기(정적→UAC→고화질→재접속) 통과

제품 코드·버전·설치본·서버 설정을 변경한 문서가 아니다. 수정 순서의 확정은 위 증거와 후속 설계에 따라 진행한다.
