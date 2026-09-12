# 2026-09-12 게임 원격 장애 코드 조사

후속 [수정안·출시 준비 계획](release_readiness_2026-09-12.md)에 R01~R12의 구현 방향·보존 조건·검증 기준, 프레임 간격/반복 멈춤/동시 접속 시험과 출시 게이트를 정리했다. 사용자의 직접 수행 지시에 따라 Codex가 추가 검토했으며 기존 작업용 GMux 세션에는 연락하지 않았다. WAN 판정기의 근거 누락 PASS는 격리 로그로 재현했다.

기준: [사건 기록](incident_2026-09-12_remote_game.md), 그 문서의 NAS 스냅샷. 사용자 요청에 따른 읽기 전용 코드 조사이며 제품 수정·빌드·실기 재현은 하지 않았다. HEAD `8bc9d1c` 및 조사 당시 작업 트리를 읽었다. `viewer_present.cpp`, `viewer_window_proc.cpp`, `viewer_startup.cpp` 등에는 다른 작업의 미커밋 변경이 있으므로 함수명을 기준으로 추적한다. 사건 바이너리와의 동일성은 미검증이다.

## 결론과 우선순위

| 우선순위 | 발견 | 판정 |
|---|---|---|
| 1 | 영상 밖에서 버튼을 놓으면 UP·눌림 해제·capture 해제를 건너뜀 | 구체적인 코드 결함 경로. 사건 재현은 미확인 |
| 1 | 제어 요청마다 응답을 기다리는 직렬 처리 | 입력 지연 증폭 구조 확인. 실제 입력 큐 최대 252ms 관측 |
| 1 | 영상 수신·디코딩·입력 ACK 수신이 한 스레드에 연결 | 영상 처리 정체가 제어 응답까지 지연시킬 수 있음. 이번 최초 정체 위치는 미확정 |
| 2 | 표시 지연을 decode queue로 추정해 디코더 reset·IDR 대기로 전환 | 혼잡 복구가 지연을 확대할 수 있는 결합. 디코더 큐 실측으로 해석 금지 |
| 2 | 단일 최신 프레임 + WM_PAINT 병합 | 디코딩 60회와 표시 40회 차이를 설명. 최초 UI 지연 원인은 미확정 |
| 2 | ABR severe 하향과 별도 static recovery | 화질 저하/호전의 코드 경로 확인. 자동 화질 자체를 최초 손실 원인으로 확정하지 않음 |
| 1 | DXGI 워커를 별도 감시 후 5초에 프로세스 종료 | 14:40 멈춤/재기동과 일치. ReleaseFrame 내부 정체 원인은 미확정 |

## 1. 반복 드래그: 해제 이벤트 처리 순서

`viewer_window_proc.cpp::WndProc`의 `WM_LBUTTONUP`은 다음 순서다.

1. UI/패널 처리 및 조기 반환.
2. `map_client_point_to_video_coords()` 실패 시 반환.
3. `mouseButtons.fetch_and(~1)` → `enqueue_input_event(kind=3)` → `release_mouse_capture_if_idle()`.

`viewer_layout.cpp::map_client_point_to_video_coords`는 영상 contentRect 밖 좌표를 거부한다. 따라서 영상 내부 DOWN에서 `SetCapture`한 상태로 밖에서 UP을 받으면 3번이 실행되지 않는다. 원격의 마지막 버튼 상태와 로컬 `mouseButtons`가 눌린 채 남아 다음 이동을 드래그로 보낼 수 있다. `WM_CAPTURECHANGED/WM_CANCELMODE` 정리나 유효한 후속 UP으로 풀릴 수 있으므로 영구 고착이라고 말하지 않는다.

`native_video_client_shared_core.cpp::ClientInputQueue::Enqueue`는 DOWN/UP을 이동처럼 병합하거나 큐 상한에서 버리지 않는다. 이번 후보는 **이미 큐에 들어간 UP 손실이 아니라 큐에 UP을 넣기 전 조기 반환**이다. 수정 검토 방향은 UP 처리에서 눌림 정리를 좌표 변환 성공 여부와 분리하고 마지막 유효 좌표/경계 좌표 정책을 정하는 것. 실제 수정은 하지 않았다.

## 2. 입력이 늦어지는 경로

`viewer_control_client.cpp`는 `NextAction` 한 개 → `execute_control_action` → 응답 처리 → 다음 반복이다. `native_video_client_tcp_control.cpp::execute_control_action`은 송신 후 즉시 `recv_control_response`로 응답을 기다린다. UDP도 `udp_control_channel.cpp::UdpControlLink::EnsureInbound`가 Receive를 반복하며 응답을 기다린다. Receive의 25ms 대기는 응답 도착으로 깨어날 수 있으므로 고정 25ms 지연이라고 단정하지 않는다.

그 결과 앞선 ping/설정/입력의 응답이 지연되면 뒤의 입력이 큐에서 기다린다. 이동끼리는 최신 것으로 병합하지만 버튼 경계는 보존하므로 빠른 반복 드래그가 밀릴 때 버튼 순서 대기도 누적될 수 있다. 큐 크기만 늘리는 것은 반응 지연 해결이 아니다.

**사건 근거**: `viewer-current.log:76229`, 14:45:23.208 `moveQueueAgeAvgUs=133938 moveQueueAgeMaxUs=252525`, 이동 ACK 최대 `20690us`. 이 샘플은 이미 전송 전 대기 약 252ms가 있었음을 보여 준다. 현재 항목의 ACK가 빠르더라도 앞선 제어 요청의 지연은 숨겨질 수 있다. 어떤 앞선 action이 막았는지는 1초 초과만 남기는 slow-action 로그로 충분히 식별하지 못한다.

## 3. 영상 수신과 입력 ACK가 같은 처리 흐름에 묶임

`viewer_video_receiver.cpp::VideoReceiver::run_udp`는 `recv` → UDP control `OnPacket` → 영상 조립/maintenance → `deliver_completed` → `process_h264_frame`을 같은 스레드에서 실행한다. `viewer_video_receiver_frame.cpp`의 `decode_access_unit`도 이 흐름에서 동기 호출된다. 조립된 프레임을 디코딩하는 동안 다음 datagram과 입력 ACK를 읽지 못한다. NACK 복구·디코더 초기화·publish·동기 로그 작업도 함께 살펴야 한다.

이 구조는 소켓 버퍼 적체와 ACK 지연의 **가능한 증폭 경로**다. 실제 어느 호출이 먼저 오래 걸렸는지는 아직 증명하지 못했다. 14:44~47에 기록된 `stage=decode` 샘플 최대는 **22544us**(14:45:02.134)다. 수백 ms의 제어 지연을 한 번의 디코더 호출 탓으로 확정할 근거가 없으며, 함수 반환 없는 정체는 이 로그 자체에 남지 않을 수 있다.

UDP 조립기의 `note_sequence_gap`은 호스트가 키프레임 큐 교체 등으로 송신하지 않은 seq의 간격도 dropped에 포함할 수 있다. 따라서 앞 문서의 dropPm은 여전히 **조립기 드롭/간격 지표**이며 회선 패킷 손실률이 아니다. FEC/NACK 복구는 누락된 청크 관측을 보조하지만 손실 위치를 특정하지 못한다.

## 4. 혼잡 추정이 표시 진척에 의존

`viewer_video_receiver_frame.cpp`가 `lastPresentedCaptureUs`를 `FrameGateInputs.presentedCapUs`에 넣는다. `viewer_frame_gate.cpp::admit`은 대체로 `입력 captureQpcUs - 마지막 표시 captureUs`를 `decodeQueueLagEstimateUs`로 계산한다(정지 화면 재개 보정 floor 등 예외 있음). 이는 디코더 입력 큐의 길이나 대기시간 실측이 아니다.

연속 도착에서 임계치를 넘으면 Congested 전이 → decoder reset → 키프레임 요청 → 비키프레임 드롭으로 이어진다. 따라서 UI가 표시를 못 따라가도 이 복구가 시작될 수 있다. 사건 로그의 `reason=decode_queue`, `burstDrops`, 반복 키프레임 대기는 이 경로와 부합하나 최초 원인이 표시 스레드였다는 확정은 아니다. 처음 밀린 경계와 복구가 만든 추가 대기를 분리해서 측정해야 한다.

## 5. 디코딩은 60인데 표시는 40인 이유

`viewer_video_receiver_frame.cpp`는 `ctx.frameBuf.frame` 한 슬롯에 최신 화면을 덮어쓰며, 이전 version이 표시되지 않았으면 `overwriteBeforePresentCount`를 올린다. `viewer_present.cpp::request_video_paint`는 `paintQueued`가 이미 true면 요청을 병합한다. `paint_video_frame`은 최신 화면을 그린 뒤 새 version이 있으면 다시 요청한다.

이는 대기열을 쌓지 않고 최신 화면을 보여 주는 정책이다. 14:50:35의 decoded 60 / present 40 / overwrite 21, 다음 초 59 / 42 / overwrite 19와 부합한다. 통계창 경계 때문에 정확한 보존식으로 계산하지 않는다. `Present` 호출 성공도 물리 디스플레이의 실제 스캔아웃 횟수와 동일한 증거는 아니다.

`viewer_startup.cpp::run_message_pump`는 메시지가 없을 때 `Sleep(5)`로 쉰다. 이벤트 기반 대기 대신 polling이어서 새 paint 요청 반영에 대기가 추가될 수 있다. 실제 sleep 시간/스케줄링 지연은 측정하지 않았으므로 이것을 40fps의 확정 원인이나 고정 프레임 제한으로 적지 않는다. UI 메시지 지연·Present 호출·디스플레이 표시를 각각 측정해야 한다.

## 6. NACK 대기와 ABR 화질 변동

사건 세션은 `nackNegotiated=1 nackHoldUs=120000 recvTimeoutMs=25`. 수신기는 기본 8개 assembly의 순서 유지와 NACK 복구 기회를 보장한다. 앞 프레임이 불완전하면 뒤 완성 프레임을 최대 hold 정책 동안 기다리게 할 수 있다. 이 대기는 무조건 모든 프레임에 120ms를 더한다는 뜻이 아니다.

`run_udp`의 불완전 head 포기 정책은 최근 제어 RTT의 2배를 응답 허용 시간에 반영하고 상한을 둔다. 제어 RTT 자체가 수신 처리에 영향을 받으므로 처리 지연이 복구 대기 예산을 늘릴 가능성이 있다. 사건의 각 frame에 실제 적용된 예산은 미측정이다.

`host_abr.hpp`는 손실 지표·decoded fps·지연에 severe 하향을 적용한다. sparse 구간의 일반 승급 판단은 막히지만 **별도 static recovery가 존재**한다. 기본값은 조건을 만족하는 sparse 구간 8초 후 한 단계 복구하며, 이 경로는 지표가 stale이어도 healthy로 취급한다. 실제 14:48:02의 `static_recovery`와 맞는다. 다만 이후 새 세션 runtime config 재적용도 있어 14:50 호전을 이것 하나로 설명할 수 없다.

## 7. DXGI 정체의 코드 경계

`capture_backend_dxgi.cpp`는 `onFrame` 콜백 후 Release 단계 진입 → `ReleaseFrame()` → 진행 시계 갱신 순서다. Release 단계 정체라는 로그는 onFrame 작업을 실행 중이라는 뜻이 아니다. `host_startup_capture.cpp::startup_start_dxgi_watchdog`는 독립 스레드로 500ms마다 진행을 보고 3초 경고, 5초 이상이면 `TerminateProcess`로 종료한다. 해당 로그와 일치한다.

코드에는 ReleaseFrame 호출을 중간 취소하는 경로가 없고, 감시기는 캡처 스레드만 교체하는 대신 전체 호스트 프로세스를 재시작하게 한다. 이것이 눈에 보인 수초 정지·세션 단절의 회복 방식이다. `d3dMt=off`만으로 스레드 경쟁을 확정하거나, 과거 AcquireNextFrame starvation 주석을 이번 Release 정체의 원인으로 전용하지 않는다. GPU/드라이버 내부 원인은 정체 시점 스택/덤프가 필요하다.

## 다음 검증

- 드래그: 영상 밖 UP을 실제 창 메시지로 재현하고 버튼 상태/원격 UP 유무를 확인.
- 입력: 모든 action의 enqueue/send/ACK 시각을 제한된 진단 구간에 기록해 252ms를 만든 선행 action 식별.
- 영상: datagram 도착/읽기, assembly 완료/배달, decode 시작/끝, publish, paint 시작/끝을 동일 seq/gen으로 대조. 계측 없는 스레드 분리나 버퍼 확대를 확정 수정안으로 취급하지 않음.
- 혼잡/표시: 정상 디코딩 상태에서 표시만 늦추는 대조 실험으로 불필요한 decoder reset 여부 확인.
- 캡처: 정체 때 스택/덤프, GPU 및 드라이버 정보를 확보. 정상 정지 화면과 호출 정체를 구분.

검증 범위: 코드 읽기·호출 연결 추적·기존 로그 필드 교차 확인. 결함 수정이나 실제 동작 재현 성공을 주장하지 않는다.
