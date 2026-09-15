# 게임 원격 장애 수정안과 출시 준비 계획

이 파일은 수정 전 계획이다. 이 브랜치의 실제 수정·검증·미완료 범위는 [구현 결과](remote_game_stability_implementation_2026-09-12.md)를 따른다.

상태: **사용자 지시에 따라 Codex 직접 조사·수정 계획 작성 · 제품 수정/검증 완료 아님**.
관련 근거: [사건 기록](incident_2026-09-12_remote_game.md), [코드 조사](incident_2026-09-12_code_review.md).
이번 범위는 원인 조사·수정 설계·검증 계획 문서화다. 제품 코드 변경·배포·사용 중 세션 조작은 포함하지 않는다.

## 출시 판단

현재 보고된 반복 멈춤, 입력 해제 누락 후보, 입력 전송 대기, 수신/표시 불안정의 재현·수정·행동 검증이 남아 있다. **현재 증거로 출시 준비 완료라고 판정할 수 없다.** 평균 디코딩 FPS·프로세스 생존·배포 성공만으로 출시를 승인하지 않는다.

아래는 확인된 범위의 수정 후보와 검증 계획이다. 기존 원장의 모든 미완료 항목을 현재 결함으로 재등록하지 않는다. `docs/full_code_audit_2026-09-08.md`, `docs/stabilization_audit_2026-09-08.md`는 과거 버전 기준이므로 현 후보의 수정 이력과 대조해 중복/해결 여부를 재분류한다.

## 수정 작업별 범위와 완료 조건

| ID / 우선순위 | 문제와 수정 방향 | 지켜야 할 조건 | 완료 증거 |
|---|---|---|---|
| R01 / 필수 | **드래그 UP 누락**: WndProc의 버튼 해제를 좌표 변환·UI hit-test 조기 반환보다 먼저 처리. 원격 DOWN을 보낸 버튼은 마지막 유효 좌표 또는 영상 경계 좌표로 UP을 보냄. 공통 release 경로 사용 | 시작부터 UI가 소유한 클릭은 원격에 보내지 않음. 모든 누른 버튼, focus/capture 상실, 취소, 세션 종료 처리. 중복 UP 정책 명시 | 영상 안/밖·패널·창 밖에서 해제, 빠른 반복 100회, 좌/우/중간 버튼 조합 후 눌림 잔류 0. 실제 게임 반복 드래그 확인 |
| R02 / 필수 | **입력 직렬 대기**: enqueue/send/ACK 시각과 action 종류를 먼저 계측. 느린 목록/통계 요청이 입력을 막는 구간 제거. 독립 진행 가능한 요청의 비동기 처리/별도 제어 경로 검토 | 버튼/키 순서·exactly-once·ACK seq 대응·세션 epoch 보장. 단순 스레드 추가로 같은 스트림 동시 Read 금지. 이동만 최신값 병합 | 느린 ping/목록/재전송 중에도 버튼 해제 전달. 전송 전 대기·ACK·행동 반응 p50/p95/p99 비교. 252ms 대기 재현과 원인 action 제거 |
| R03 / 필수 | **영상 처리와 ACK 수신 결합**: 수신 스레드는 datagram 수신·제어 분배·경량 조립을 계속 수행하고 디코더 작업을 분리하는 안 검증. bounded AU queue와 명시적 참조 복구 정책 설계 | 메모리 상한, seq/gen 소유권, 종료/재접속 취소, 디코더 단일 소유. P-frame 임의 폐기 후 다음 P를 정상 decode하지 않음 | 디코더 지연/재초기화 주입 중에도 ACK 수신 진척, 소켓 드롭 계측. 전후 동일 손실 조건의 프레임/입력 지연 비교 |
| R04 / 필수 | **표시 지연이 decode 혼잡을 유발**: assembly/decoder backlog와 publish→present 지연을 별도 지표로 구분. 표시만 늦을 때는 최신 표시 정책으로 회복하고 decoder reset은 참조 손상/실제 decode 복구 조건에 한정하는 안 검증 | 느린 표시를 무시해 실제 오래된 영상이 계속 나오게 하지 않음. 손실 시 IDR 대기 및 회복 제한 유지 | decode는 정상이고 UI만 지연되는 대조 실험에서 불필요한 reset/IDR 폭주 0. 참조 프레임 손실 실험에서는 정상 복구 |
| R05 / 필수 | **불규칙한 표시 간격**: paint 요청/실제 진입/Present/표시 시점을 계측. Sleep polling을 메시지+프레임 이벤트 대기로 바꾸는 안, DXGI frame latency 대기 기반 표시 검토 | 최신 프레임 정책을 무제한 FIFO로 바꾸지 않음. picker/resize/최소화/DPI/닫기 경로 유지. 실제 디스플레이 동기화와 앱 호출 성공 구분 | 연속 움직임에서 표시 간격 분포·최대 gap·반복 stutter 측정. 60 decode/40 present 구간 개선, 입력 중 UI starvation 없음 |
| R06 / 필수 | **반복 캡처 멈춤**: 3초 경고 시 비차단 진단/덤프 확보, 5초 watchdog 회복 검증. 장기안은 capture/encode worker와 세션 broker 분리 | 불투명 DXGI 호출을 스레드 강제 종료로 풀지 않음. `docs/OSLink_구조분석.md`의 broker epoch/generation/wire seq 소유권과 옛 worker 출력 차단 계약 유지 | Release 정체 주입→복구→현재 화면/입력 확인, 연속 재발 및 재시작 폭주 방지. 정지 화면에서도 seed/IDR 확보. 네트워크 세션 보존은 별도 설계 검증 |
| R07 / 필수 | **NACK 대기·손실 지표**: 네트워크 청크 누락, host 의도적 seq 생략, assembly 만료를 분리. hold/재전송/포기 예산을 입력 RTT 오염과 구분해 계측 후 조정 | 복구 예산을 무조건 줄여 IDR 폭주를 만들지 않음. 정상 완성 프레임에 고정 대기 추가 금지. 재전송 증폭/대역 상한 유지 | 손실·지터·재정렬·burst·대역 제한별 복구시간, 폐기 이유, 재전송량 확인. NACK on/off 대조는 격리 조건에서만 수행 |
| R08 / 필수 | **화질 하향/복귀와 설정**: 요청 FPS/비트레이트, 적용값, 실제 해상도, ABR 원인을 UI/진단에서 구분. stale feedback에서 static recovery 승급 정책 재검토 | 60fps를 보장 FPS로 오표시하지 않음. 저화질 고착과 반복 승강 모두 검증. 낮은 원본을 upscale해 복구로 위장하지 않음 | 60fps 선택→연결→runtime 적용→혼잡 하향→회복 상향을 같은 기기/버전에서 확인. 이전 30fps 시작 원인 추적 |
| R09 / 필수 | **원본 화면 공급 감소**: 게임 FPS·focus·백그라운드 제한·DXGI content 획득을 동시에 기록. 캡처 watchdog과 실제 화면 변화 감시 분리 | 정지 화면/커서-only/합성 refresh를 실제 60fps로 세지 않음. 게임 설정을 승인 없이 바꾸지 않음 | 정지→이동→정지 마지막 화면 보존, 게임 foreground/background/최소화/복원에서 기대 동작과 실제 캡처 일치 |
| R10 / 선행 필수 | **동시 세션 로그 식별**: viewer 프로세스 시작마다 진단 session ID 생성, Host ID·viewer PID·실행 버전/hash·epoch를 로그에 연결. pipe reader에도 context 전달. 생성 시각과 부모 수집 시각을 별도로 기록 | 인증 토큰을 ID로 사용하지 않음. 회전/업로드 후에도 세션 식별 유지. 기존 파서 호환/버전 관리 | 두 viewer가 같은 seq를 동시에 출력해도 NAS에서 분리 가능. 한 세션 종료/재시작·로그 회전 후 attribution 유지 |
| R11 / 필수 | **연결은 살아 있으나 영상은 멈춘 상태**: 통신/수신/decode/publish/실제 표시/원본 freshness를 별도 health로 표시. 오래된 화면 반복과 정상 정지 화면 구분 후 제한된 회복·명시적 오류 | 움직임이 없는 것만으로 재시작하지 않음. 합성 프레임으로 실제 캡처 정상 판정을 갱신하지 않음. 무한 IDR/재접속 폭주 금지 | 제어 alive+영상 없음, synthetic만 수신, UI만 정체, 정상 정지 화면 대조에서 올바른 상태 표시/복구 |
| R12 / 지원 범위에 따라 필수 | **동시 viewer의 shell 상태 독립성**: 프로세스별 session registry와 상태 메시지에 ID 추가. 개별 종료가 전역 idle/실패를 덮어쓰지 않도록 집계 | 기존 연결 소유권/업데이트 종료 정책 유지. 한 Host 다중 제어 지원 여부를 명시 | A/B 동시 사용 중 A 종료·실패·재접속해도 B의 활성 상태와 입력 대상 유지 |

R02~R07은 원인 경계별 후보 설계다. 전부 한 번에 적용하지 않고 재현/계측 → 최소 수정 → 해당 경계 검증 → 통합 순으로 진행한다. DXGI 드라이버 내부 근본 원인은 아직 미확정이며, worker 분리는 장애 격리안이지 드라이버 결함을 해결했다는 뜻이 아니다.

## 불규칙 프레임과 반복 멈춤의 측정 기준

동일 세션의 capture/encode/wire/assembly/decode/publish/present를 seq·generation·epoch로 연결한다. 통계에는 실제 측정 구간 길이를 포함하고 재접속 전후 누적 카운터를 단순 빼지 않는다.

- 연속 움직임과 정지 화면 결과를 분리. p50/p95/p99/max 표시 간격, 50/100/250/1000ms 이상 gap 횟수·연속 길이, synthetic 제외 실제 새 화면 수를 보고.
- 디코딩 FPS, 앱 Present 성공 횟수, 실제 표시 증거를 별도로 보고. VSync/모니터 주사율, 최소화/가림, CPU/GPU 부하를 고정.
- 입력 생성→전송→호스트 주입→게임 변화→표시를 측정. ping RTT만으로 반응속도 통과 금지.
- 단절/재연결뿐 아니라 “통신 생존+오래된 화면 반복”을 실패로 검출. 자동 재시작 후 버튼/키 잔류, 옛 frame/epoch 노출도 검사.

초기 **검증 목표안**(합의된 기존 SLA가 아님): 통제된 60Hz LAN/연속 움직임 1080p60에서 10분 평균 실제 새 화면 표시 ≥57fps, 표시 gap p95≤33.4ms·p99≤50ms, 의도하지 않은 250ms 이상 freeze 0. 입력 전송 전 대기 p95≤20ms·p99≤50ms. 실제 input-to-visible 목표는 측정 수단/기준 장비와 함께 별도 고정. WAN은 RTT·손실·지터·대역을 명시하고 LAN 수치를 무조건 적용하지 않음. 환경 한계나 목표 미달은 숨기지 않고 출시 범위 축소 또는 차단으로 판정.

## 다중 세션 조사와 제품 동시 접속 시험

사용자가 기존 GMux 세션들은 다른 작업용이라고 정정하고 Codex 직접 수행을 지시했다. 다른 에이전트에는 연락/위임하지 않았다. **제품의 여러 원격 연결 시험**은 별도 검증 계획이며 아직 실행하지 않았다. 실행 시 격리한 테스트 기기/프로필을 사용하고 현재 사용자 게임 세션을 건드리지 않는다.

코드상 한 Host 런타임의 `SessionState`는 현재 client/UDP control/epoch 한 묶음이며, `SenderState::PumpUdpHello`는 peer 변경 시 송신 큐를 비우고 media epoch를 바꾼다. `host_app_main.cpp`의 supervisor는 `PROCESS_INFORMATION child_` 한 개를 관리한다. 반면 `client_shell_main.cpp`는 접속 요청마다 viewer 프로세스와 로그 reader를 만든다. 이는 한 Client의 여러 viewer와 한 Host의 여러 동시 제어가 다른 계약임을 보여 준다. 실제 허용/거부 정책은 directory 인증·UI까지 확인해야 하며 이번 조사만으로 동시 제어 지원을 선언하지 않는다.

| 조합 | 반드시 확인할 행동 |
|---|---|
| 한 Client에서 Host A/B 동시 연결 | 키/마우스가 focus 대상 한 곳에만 전달, 각 세션 FPS/ABR/닫기 독립 |
| 두 Client에서 같은 Host 접속 | 지원 정책 확인: 공유/관찰/제어권 전환/명시적 거부 중 제품 계약과 일치. 조용한 peer 교체·입력 섞임 금지 |
| 한 연결 손실·재접속 중 다른 연결 정상 사용 | 프로세스/소켓/전역 상태 영향 없음, epoch별 stale 메시지 차단 |
| 1→2→4 연결 부하 단계 | CPU/GPU/VRAM/메모리/핸들/소켓 및 세션별 지연·프레임. 지원 상한 초과 시 명시적 오류 |
| focus/Alt-Tab/닫기/모니터 전환 반복 | 모든 버튼/키 release, 잘못된 Host 주입 0, 세션별 target identity 유지 |

## 출시 게이트에 필요한 추가 작업

1. **검증 도구 보강**: `automation/summarize_wan_capture.ps1:143-151`은 present-gap 누락 시 decoded 평균만으로 pass 가능. 필수 근거 누락은 Unknown/실패로 처리하고 표시 분포/입력/실제 회복을 필수화. 기본 decoded 목표 20은 60fps 출시 기준이 아님.
2. **soak 보강**: `automation/soak_native_video_reconnect.ps1` 기본 20회×3초·종료 코드 판정은 장시간/동시 세션 검증이 아님. 첫 현재 화면, 입력 성공, 마지막 화면 freshness, teardown 자원 회수, 재연결 후 target/설정 일치를 검사. 초안 목표는 100회 전환/재연결, 혼합 동작 1시간, 고정 후보 8시간 soak이며 미실행.
3. **실제 경로 범위**: 지원 GPU·디스플레이·Windows 세션, LAN/direct/WAN/relay, 고DPI, 다중 모니터, 게임 전체화면/창모드, UAC/잠금·복원·절전/깨우기 경계를 구분. 각 경로의 적용/제외와 이유 기록.
4. **UI·설치/업데이트**: picker를 실제로 열고 대상 선택/취소/실패/복구, 설정 유지, 인앱 업데이트·취소·실패·rollback·재실행·언인스톨을 검증. 기존 과거 체크리스트를 현 버전 미구현으로 단정하지 말고 증거 갱신.
5. **세션·계정 경계**: 다른 계정/기기 대상 접근 거부, 만료/재접속, 입력 소유권, 로그 계정/기기 구분, 잘못된 화면 fallback 금지. 지원 Android 경로는 Windows 결과로 대체하지 않음.
6. **고정 후보 검증 및 게시**: 버전·build commit·파일 hash 고정 → 검증용 독립 행동 검증 OK → 서명 → gnlink 자동 NAS 배포 → 외부 HTTPS/manifest 서명/버전/파일 hash/필요 API 확인 → Codex 최종 검토. 사용자 설치/재시작 및 UAC 실기는 별도. push 보류 유지.

출시 판정표는 각 항목을 **확인된 결함 / 조건부 위험 / 수정됨·미검증 / 검증 완료 / 적용 제외**로 분류한다. 담당·선행 작업·근거 링크·남은 사용자 행동을 반드시 연결한다. “모든 문제를 찾았다”는 보장은 하지 않으며 미조사 영역을 드러낸다.

## Codex 직접 조사로 추가 확인한 출시 위험

### A. 기존 WAN 판정기의 잘못된 통과를 실행 재현

격리 경로 `.claude/release-readiness-probes/`에 합성 로그를 만들고 **기존** `automation/summarize_wan_capture.ps1`을 그대로 실행했다. 제품 게임/네트워크 재현이 아니라 판정기 단위 검증이다.

| 입력 조건 (`-DecodedFpsGate 57`) | 출력 |
|---|---|
| decodedFrames=60, d3dPresentSuccess=0, presentGapOver1s 필드 없음 | `GATE_A_PRESENT_GAP_OK=Unknown`, **`GATE_A_PASS=True`** |
| 같은 값에 presentGapOver1s=3 추가 | `GATE_A_PRESENT_GAP_OK=False`, `GATE_A_PASS=False` |

두 실행 명령은 정상 완료했다. 출력은 `missing-present-result.txt`, `frozen-present-result.txt`에 보관. 첫 케이스는 화면 표시 실패를 성공 처리할 수 있는 근거 누락 경로를 확정한다. 한 개 샘플로도 통과하므로 최소 지속시간/샘플 수 검증도 필요하다. 수정안: 필수 필드/표시 성공/실제 구간 길이 누락은 Unknown으로 유지하고 release pass 금지, 진짜 실패는 프로세스 종료 코드에도 반영, 음성 대조 fixture를 회귀로 보존한다.

### B. 반복 멈춤의 대기시간이 커지는 경로

`host_app_main.cpp` supervisor는 watchdog 재기동을 5분 창으로 센다. 처음 3회는 100ms×3, 4회째부터 `min(300,30*recoveries)` ticks이므로 **4회째 12초, 이후 증가해 최대 30초**를 재시작 전에 기다린다. 캡처 watchdog 5초·종료 정리·새 프로세스 초기화는 별도 시간이다. 반복 멈춤이 더 길어질 수 있는 코드 경로이며 사건에서 4회 이상 발생했다는 증거는 아니다.

수정안: backoff를 무조건 제거하지 말고 UI에 감지 원인·재시도 횟수·대기·복구 실패를 표시하고 재시작 전 진단을 보존. 반복 실패 시 제한된 회복과 명시적 중단 상태를 설계. 검증은 첫 3회/4회/상한/5분 창 갱신을 포함하며 실제 현재 화면과 입력 회복으로 판정한다.

### C. 살아 있는 연결과 살아 있는 화면의 차이

`viewer_recv_liveness.hpp::evaluate_session_liveness`의 sessionDead는 controlGone과 videoStopped가 함께 필요하다. `viewer_session_watchdog.cpp`의 recvStalled/linkSilent는 진단 로그를 남기며, sessionDead 경로에서만 close/notify한다. synthetic publish나 control alive 상태는 자동 종료 조건을 막을 수 있다. 정상 정지 화면 보호에는 필요하지만 이 판정만으로 영상 정상임을 선언할 수 없다. R11로 별도 freshness/표시 health와 복구 기준을 추가한다.

### D. 동시 로그와 shell 상태 혼합

`client_shell_main.cpp::viewer_log_write_line`은 모든 viewer 출력을 같은 viewer.log와 NAS `viewer` stream에 넣는다. `pump_viewer_output_to_log` 인자는 pipe handle뿐이고 줄에 session ID/PID/Host ID를 추가하지 않는다. 시간은 부모 reader가 줄을 기록할 때 붙인다. 따라서 pipe 적체 시간은 영상 발생 시각과 다르며 동시 세션의 seq/gen이 겹치면 줄 출처가 모호해진다. 기존 사건의 숫자도 단일 세션/Host wire 대조라는 전제를 유지해야 한다. R10을 성능 수정에 앞선 계측 작업으로 둔다.

같은 파일의 viewer 종료 watcher는 개별 session ID 없이 `post_status("idle", "")` 또는 전역 error를 보낸다. UI에서 다른 활성 세션의 상태를 어떻게 표시하는지 실기 확인이 필요하다. 원격 프로세스 분리는 확인했지만 shell 상태까지 독립임을 보증하지 않으므로 R12로 검증/개선한다.

## 실행 순서와 문서 완료 경계

1. **기준 고정/계측**: 실행 바이너리·지원 환경·R10 로그 ID, 판정기 필수 근거 검증. 같은 원본으로 재현 가능하게 한다.
2. **확정 코드 결함부터**: R01 UP 경로, 판정기 잘못된 통과를 최소 수정하고 음성 대조 회귀 검증.
3. **지연 경계 분리**: R02/R03의 입력 대기·수신 적체를 측정하고 최소 변경. 이후 R04/R05의 복구 정책·표시 대기 개선.
4. **캡처/복구·품질 안정화**: R06/R07/R08/R09/R11을 장애 주입과 정상 정지 화면 대조로 검증. 대규모 worker 분리는 작은 조치로 해결되지 않는 근거와 계약 검토를 갖춘 별도 단계.
5. **제품 동시 접속과 출시 전 완주**: R12와 위 조합표, 장시간/손실/전환 시험, UI·설치·업데이트·계정 경계, 고정 후보 독립 검증·배포 흐름.

이번 **조사·수정 계획 문서 작성**은 Codex가 직접 수행했다. 제품 수정·다중 연결 실기·장시간 시험·출시 승인은 완료하지 않았다. 다른 작업용 GMux 세션의 참여나 독립 검증을 주장하지 않는다.
