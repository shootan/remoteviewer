# 게임 원격 안정화 수정 후보 — 2026-09-12

**아래 수치/해시 표는 revision 1 기록이다.** 독립 검토 후 보수와 현재 기준은 [revision 2 검토 보수](remote_game_stability_review_2026-09-13.md), 최신 코드·문서·실행 파일 해시는 `build-incident/candidate-manifest.json`을 따른다.

## 위치와 완료 경계

- 사용자 지시로 Codex 직접 구현. 기존 GMux 작업 세션에 연락/위임하지 않았다.
- 작업 트리: `D:/remote/remote/.claude/worktrees/remote-game-stability`.
- 브랜치: `fix/remote-game-stability`, 기준 커밋 `035c4a44998bfae82049f5d1afe62d3b6b8fb299`.
- 빌드: `build-incident`, Release, VS2022/MSVC, 콘솔 세션 `shotan`/ID 1.
- **확인된 코드 경로 수정 및 격리 자동 검증을 수행한 후보**다. 실제 장애 기기의 모든 문제가 해결됐다거나 출시 완료라고 판정하지 않는다.
- 제품 버전 문자열은 기존 `0.2.123`을 유지한다. 설치/업데이트용 새 릴리스가 아니다. 버전 번호만으로 설치본과 이 후보를 비교하지 않는다.
- 원래 작업 트리의 제품 파일과 다른 세션의 빌드 폴더는 수정하지 않았다. 개인키·인증 정보는 복사하지 않았다.
- Git MCP 도구가 제공되지 않아 **새 커밋 없음**. 위 해시는 수정 커밋이 아닌 기준 커밋이다. 미커밋 변경은 이 작업 트리에 있으며 merge/push/서명/배포/설치도 미수행.

## 실제 수정

| 영역 | 수정 내용 | 확인 범위 / 남은 경계 |
|---|---|---|
| 드래그 해제 | `viewer_window_proc.cpp`: 원격에 전달한 버튼의 UP은 영상 밖·패널·터치 억제 상태에서도 마지막 유효 좌표로 먼저 전달. 눌림 상태 정리. 미처리 WM_TIMER 반환 경로도 보완 | 실제 WndProc 메시지로 100회 반복 해제 및 보조 버튼 경로 검증. 실제 게임의 물리 입력 실기는 별도 |
| 정적 화면 타이핑: 마지막 픽셀 | `host_capture_session.cpp`, `d3d_capture_readback.*`: 새 content를 캡처 제한 전에 버리지 않고 소유권 있는 GPU 슬롯에 보존. worker가 시간에 맞춰 최신 프레임을 소비. 꽉 찬 링은 가장 오래된 pending 슬롯을 교체하고 Reading 슬롯은 보호 | WARP 64×64 및 1920×1080, 2개 슬롯에 30개 변경 후 생산 중단해 마지막 픽셀 전달 확인. 실제 GPU 부하/드라이버 차이는 미검증 |
| 정적 화면 타이핑: 인코더 지연 | `host_kick.hpp`와 host stage 호출자: 마지막 입력을 밀어내는 kick을 고정 150ms에서 목표 FPS의 두 프레임 주기로 변경(60fps 약 33.3ms, 30fps 약 66.7ms). 범위 15~150ms, 기존 1회 kick·IDR barrier 규칙 유지 | kick 시각/중복 억제 회귀. 사용자 키 입력부터 실제 게임 화면 반응까지의 종단 실측은 아님 |
| 입력 스케줄링 | `native_video_client_shared_core.*`, `viewer_control_client.cpp`: 입력 enqueue가 condition variable로 즉시 깨움. 입력을 배경 통계/설정보다 우선하되 8개 burst 후 배경 작업 기회 보장. 선택/세션 barrier와 ping 우선순위 유지 | 큐 순서·UP 보존·피드백 starvation 방지 회귀. 이미 진행 중인 제어 요청의 응답 대기와 WAN RTT 자체는 남음 |
| 수신/디코딩 결합 | `viewer_udp_ingress.hpp`, `viewer_video_receiver.cpp`: 소켓 전용 worker가 제어 패킷과 Tick을 처리. 기존 receiver는 bounded 영상 datagram 큐를 소비하며 조립/디코더는 계속 단일 소유 | 두 독립 loopback socket에서 영상 소비가 정지해도 control 분배. 큐 1024개/약 1.6MB 상한. overflow는 별도 계측과 참조 복구로 연결 |
| 표시 지연의 잘못된 혼잡 복구 | `viewer_frame_gate.*`, `viewer_decoder_state.hpp`, `viewer_video_receiver_frame.cpp`: 실제 decoder output 진척을 혼잡 기준에 반영. UI 표시가 늦은 것만으로 decoder reset하지 않음 | 100개 이상 실제 수신/디코드 frame 동안 표시 anchor 고정해 참조 유지 확인. 손실/IDR 복구 시험도 유지 |
| 표시 대기 | `viewer_startup.cpp`: Sleep polling 대신 메시지 도착으로 깨우는 대기. 최신 한 프레임 정책 유지 | 실제 GNLinkViewer 창의 1080p60 loopback 표시 및 스크린샷. 장시간 게임·WAN 성능 보증은 아님 |
| 반복 DXGI 정체 | `host_app_main.cpp`: exit 44 감지 후 같은 supervisor 실행 동안 DXGI를 격리하고 다음 child에 WGC 요청. supervisor 종료 시 원래 환경 복구 | 빌드 및 기존 backend 계약 대조. 실제 ReleaseFrame 드라이버 정체 재현/현장 WGC 전환은 미검증. 최초 5초 watchdog 대기와 세션 재접속은 여전히 필요할 수 있음 |
| 화질 복귀 | Host frame/tick에 원본 content 크기를 별도 전달. GPU downscale된 payload 크기를 원본 encodeSource로 되먹이지 않음. 같은 종횡비의 크기 변경도 refit. ABR은 stale feedback을 건강한 링크로 간주해 static 승급하지 않음 | ABR 회귀/빌드. 현장 720p→1080p 전체 전환은 별도 확인 필요 |
| 영상 처리 정체 | `viewer_recv_liveness.hpp`, `viewer_session_watchdog.cpp`: ingress가 control을 살려 두어도 Decode/Publish가 5초 이상 정체하면 종료/알림 정책으로 진행. 정상 Recv idle과 구분 | pure liveness 및 timeout 회귀. UI 스레드 자체/드라이버의 모든 정체를 회복한다는 보장은 아님 |
| 세션 구분 | `client_shell_main.cpp`: viewerSession·PID·Host ID·제품 버전·수집 tick을 로그에 부착. 구조화 viewer log에는 eventQpcUs 추가. 한 viewer 종료 후 다른 연결 수가 있으면 빈 idle 문구 대신 남은 연결 수 표시 | 소스 대조/제품 빌드. 실제 복수 Host UI/로그 업로드·계정 전환 완주는 미검증 |
| 출시 판정 | `summarize_wan_capture.ps1`: 기본 목표 57, 최소 통계 샘플 10, 표시 성공 수와 실제 frame-gap 근거 필수. 기본 최대 gap 250ms. 미확인 exit 2, 실패 exit 1 | 누락/표시 0/멈춤/400ms stutter를 거부하고 정상 fixture만 통과. 연속 움직임 시험용이며 정지 화면 로그에 그대로 적용하지 않음 |

`captureGateDropContent` 통계는 이제 실제 content 폐기가 아니라 소비 지연이므로 `captureGateDeferredContent`로 명칭을 바꿨다. NACK wire 형식·세션 인증 계약은 변경하지 않았다. 수신 조립기 dropPm을 네트워크 패킷 손실률로 해석하지 않는다.

## 검증 근거

재실행: `powershell -NoProfile -ExecutionPolicy Bypass -File automation/verify_remote_game_stability.ps1`.
스크립트는 CMakeCache의 source root를 확인해 다른 작업 트리의 빌드를 잘못 실행하지 않는다. 먼저 해당 테스트 target을 build-incident에 빌드해야 한다.

- [통합 검증 출력](../build-incident/verification.log): 열 개의 대상 실행 모두 exit 0. 큐/스케줄러, FrameGate, kick, ABR, liveness, readback, 마지막 픽셀, 분리 수신, 실제 WndProc, UDP 복구 포함.
- WndProc: **41 passed / 0 failed**. 입력은 Win32 메시지이며 transport 경계는 기록 double이다. 물리 마우스→원격 게임 전체 증거로 확대하지 않는다.
- UDP 복구: 실제 Media Foundation codec·loopback·제품 receiver/assembler 경로. 손실/재정렬/NACK/키프레임/정지 화면 tail/epoch 전환 검증. 창 표시를 대신하는 publish 기반 harness이므로 별도 실제 viewer UI 시험을 추가했다.
- [Full HD 마지막 픽셀 시험](../build-incident/capture-fhd-result.log): 실제 WARP readback 1920×1080, 마지막 변경 뒤 추가 callback 없이 픽셀 도달, exit 0.
- 출시 gate 음성 대조: 누락 exit 2, 표시 0·freeze·400ms stutter exit 1, 정상 exit 0.

첫 UDP 회귀에서 기존 S9의 세 assertion이 실패했다. S9는 표시 anchor를 고정하면 정상 decoder도 Congested가 돼야 한다는 전제였다. 새 요구에 맞춰 **UI 지연 중 참조 체인이 계속 진행하고 불필요한 IDR가 없다는 검증**으로 바꿨다. 실제 손실에 대한 timer retry는 S3/S8 및 FrameGate timer 검증을 유지했다. 이후 통합 회귀는 통과했다. 이전 실패 로그도 build-incident에 보존했다.

### 실제 제품 Viewer UI

재실행: `powershell -NoProfile -ExecutionPolicy Bypass -File automation/probe_stability_viewer_ui.ps1 -FullHd`.
시험 전용 host는 현재 데스크톱을 캡처하지 않고 1080p 움직이는 막대 영상을 인코딩한다. viewer는 **실제 GNLinkViewer.exe**이며 loopback에 접속한다. 시험 창만 PID와 class로 찾아 표시하며 키보드 포커스나 원격 입력을 사용하지 않는다.

- Viewer/fixture **모두 exit 0**, 실제 [표시 스크린샷](../build-incident/visible-viewer.png) 확인.
- 약 10초의 짧은 시험. 초기 구간 제외 각 통계의 recv/decode/present는 대부분 **60/60/60**, 말미 58~61회. 통계창 경계로 순간 61회가 나올 수 있다.
- [간격 분석](../build-incident/ui-pacing-result.json): 527개 interval, p50 **13.597ms**, p95 **33.610ms**, p99 **37.873ms**, 최대 **61.423ms**, 100ms 초과 **0**.
- 최초 숨김 창 실행은 decoded 60 / present 0이었다. **표시 검증 무효**로 분리하고 창을 보인 뒤 다시 측정했다. 이를 제품 표시 장애의 재현으로 주장하지 않는다.
- 이 결과는 실제 장애 당시 Host A/B의 게임이나 WAN에서 얻은 결과가 아니다. 미리 제안한 10분/8시간 출시 목표를 통과한 것도 아니다. p95도 목표안 33.4ms보다 약간 높아 완전히 균일한 16.7ms 간격을 달성했다고 주장하지 않는다.

## 산출물 고정

[candidate-manifest.json](../build-incident/candidate-manifest.json)에 기준 커밋, 브랜치, 수정/신규 코드 파일 해시, 제품 실행 파일 해시를 기록했다. [테스트별 해시/종료 코드](../build-incident/stability-test-results.json)도 보존한다. Full HD readback 추가 시험은 별도 로그다.

| 파일 | SHA-256 |
|---|---|
| GNLinkClient.exe | `74b07e8b9e5a2132b87fb5687ae7d1ee7f1e2ffa42a5403ec942606b37e57f34` |
| GNLinkHost.exe | `48d3d776faa355b732bae460788137eb0162f122e234bf7960f224c343f71f17` |
| GNLinkStream.exe | `64d833a59d0f5b5d5e3c0dceb1af8eccb718c02aee7cb1a0e21d3bd61c9da1c3` |
| GNLinkViewer.exe | `07adf39b46c50d0c393b8c6ac37156599f63a4c13bb55094846fe59b120ccd29` |

원래 NAS 로그 스냅샷은 `D:/remote/remote/.claude/investigate-0912-1440/`에 있다. 이 작업 트리에 원시 로그를 중복 복사하지 않았고, 기존 근거 SHA-256 목록만 docs에 복사했다.

## 아직 닫지 않은 것

1. **실제 장애 기기의 게임 실기**: 정적 화면 한글/영문 입력, 반복 드래그, GPU 경합, 화면/모니터/잠금 전환, 저화질 복귀. 현장 설치본을 자동 교체하거나 재시작하지 않았다.
2. **드라이버 내부 원인과 완전한 무중단 회복**: WGC 전환은 반복 DXGI 사용을 피하는 완화책. 최초 wedge와 재연결까지 없애는 broker/worker 분리는 이번 구현에 포함하지 않았다.
3. **제품 동시 접속·장시간/WAN**: 두 소켓의 독립성 시험은 두 실제 Host에서 수시간 게임을 조작한 증거가 아니다. 한 Host 다중 제어 지원도 새로 추가하지 않았다.
4. **출시**: 고정 후보의 별도 검증용 OK, 새 버전 확정, 서명·NAS 자동 배포·외부 해시 확인, 필요한 사용자 UAC 실기가 남았다. 기존 다른 작업 세션을 승인 없이 동원하지 않았다.

따라서 상태는 **코드 수정 후보 + 격리 자동 검증 통과**다. “모든 현장 장애 해결” 또는 “배포 완료”는 아니다.
