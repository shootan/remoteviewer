# Host / PC 클라이언트 스레드·장애 복구 전수 점검

작성: 2026-09-12, Codex 직접 조사. 목적은 **멈춤·반복 장애·복구 실패를 소스에서 찾아 후속 작업 가능한 원장으로 보존**하는 것이다. 제품 수정·설치·프로세스 재시작·NAS 게시를 수행한 보고서가 아니다.

## 1. 결론

스레드가 전부 정상이라고 판정할 수 없다. **실제 DXGI 워커 정체가 NAS Host 로그에 7회** 있고, 소스에는 다음 서로 다른 복구 공백이 있다.

1. **반복 장애:** DXGI watchdog은 프로세스를 죽여 다시 띄우지만, 반복된 코드 44에 대해 캡처 백엔드를 바꾸거나 해당 장치 경로를 일정 시간 제외하지 않는다. 재시작 횟수에 따른 대기만 증가한다.
2. **살아 있는 실패:** 메인 루프가 계속 돌면서 인코더 출력이 없거나 encode가 실패하면, 루프 정체 감시는 작동하지 않는다. 출력 고갈 진단 자체는 로그만 남긴다. provenance 오류의 별도 재초기화는 존재하므로 “인코더 복구가 전혀 없다”는 뜻은 아니다.
3. **실패 상태를 잃음:** 일부 캡처 재시작은 먼저 기존 세션을 닫고 요청 플래그를 소비한다. 재시작 실패 후 재시도 상태를 복구하지 않아, 프로세스·제어 연결은 살아 있는데 캡처가 없는 상태가 가능하다.
4. **복구 대상이 바뀜:** 보조 모니터를 선택해도 공용 restart가 주 모니터를 다시 선택한다. 복구 성공 로그와 실제 송출 대상이 다를 수 있다.
5. **PC 표시·세션 복원 공백:** 표시 실패가 성공으로 집계되는 경로, D3D 장치 손실 재생성 누락, UI 스레드에 종속된 감시가 있다. 죽은 뷰어를 닫는 동작은 있으나 기존 대상의 자동 재연결은 없다.

**현장 사건의 최초 GPU/드라이버 원인을 확정한 것은 아니다.** 7회 로그는 DXGI 호출 단계 정체와 watchdog 종료 선언을 증명한다. 각 재기동 뒤 영상·입력까지 복구됐다는 증거와는 다르다.

## 2. 조사 기준과 범위

- 기준 HEAD: `8bc9d1c2e5b5ae6f617e6ede93cb384993367753` + 당시 워킹트리. `apps/native_poc`, `libs`, `apps/directory`의 C/C++/JS/HTML 및 빌드 정의 **364파일 / 97,904줄**을 목록화·해시 고정·위험 패턴 전수 검색했다. 테스트·도구도 포함한 수치다.
- 파일별 검색 결과와 심층 추적 여부는 [전수 목록](host_pc_thread_recovery_inventory_2026-09-12.md)에 있다. **364파일의 모든 문장을 수동 정독하거나 모든 실행 분기를 검증했다는 의미가 아니다.** 생성/종료/대기/오류/복구 검색 후 실제 소유자와 호출 분기를 중심으로 심층 추적했다.
- 소스 사본·전체 SHA-256·재현 프로브·NAS 원본: `.claude/audit-20260912-direct/`. 이 폴더는 로컬 근거이며 커밋 대상이 아니다. 핵심 근거·판정·재현 입력은 본문에 보존한다.
- 최초 미커밋 뷰어 6파일을 덮어쓰지 않았다. 조사 도중 다른 작업의 `55c93c0` 커밋, 버전 파일과 별도 사건 문서 변경이 들어왔다. 이 보고서는 고정 사본 기준이며 타 작업의 구현·빌드 결과를 자신의 검증으로 사용하지 않는다.
- 대상은 **Windows Host/Stream/Capture, PC Shell/Viewer, 공유 네트워크·인코더·업데이터·입력·디렉터리 계약**이다. Android UI/JNI 전체, 외부 라이브러리/드라이버 내부, 모든 자동화 스크립트는 이번 전수 대상 밖이다.
- 이 PC는 `console / shotan / session 1 Active`, RDP는 Listener 상태. 소형 프로브는 `pure-logic`, NAS 조회는 `network`다. GPU·UAC·실게임 장애 주입은 하지 않았다.
- 이 PC 설치 EXE의 FileVersion 리소스는 조회 시 공란이었다. 설치본 SHA-256: Host `9b7b42f809677555a84196cb087d7645e922eae6a7534d31a91b11dad1d9ef2a`, Client `d857ca5f59ca44f3d2fb9f685fbdb4854829430e0a3ffea6e7c728090e92c3e5`, Viewer `82b79c2046ed37f8580c1ef7a6ae52bf4692ea49685293e12622ee9b21b5b5fc`. 이 해시를 NAS 상대 기기의 버전으로 대신 쓰지 않는다.

판정 표기: **코드** = 해당 조건에서 문제 분기가 소스에 존재, **로그** = 이번 원본에 실제 발생, **프로브** = 제품 헤더/함수의 격리 실행으로 확인, **위험** = 별도 OS/오류 주입이 필요한 잠재 문제, **정책 공백** = 기존 의도는 확인되지만 사용자 복구 요구를 충족하지 못함. P1은 우선 대응, P2는 조건부·안정성 후속, P3는 낮은 우선순위다.

## 3. NAS 현장 근거

사용자가 지정한 `gnlink` 계정과 로컬 `remote60_deploy` 키로 `192.168.0.6`에 읽기 전용 접속했다. 키 내용은 읽거나 옮기지 않았다. 서버 시각 조회는 `2026-09-12T14:50:49+09:00`; 이후 다운로드한 고정 사본이다. 경로 접두사는 `/opt/gnlink/remote60-directory/logs/shotan/`.

| 기기/스트림 | NAS 상대 경로 | 로컬 근거 / SHA-256 |
|---|---|---|
| 이 PC Host | `8ec6ecb1-11fb-4db3-8127-aea0c5311225/host.log` | `nas-host-local.log`, 42,336줄, `02ba69cdf6428a02d13034c58df5268fda6b5a9496d3e7d5ebbb1c43682ffc57` |
| 다른 Host | `51bd6657-9b5b-41d0-8385-6557d42c3990/host.log` | `nas-host-other.log`, 73,975줄, `f73d8912504c12bd70bdc618bd24fb8d6edc45f85bba3e0286f0339722578270` |
| PC Viewer | `68f79d01-d6c4-4a4a-a3ae-69da5be54826/viewer.log` | `nas-viewer.log`, 83,676줄, `32f11e84eeb670d67c3c7ca9723d7397db9384940093a14beadca1894ea0edef` |
| PC Shell | 같은 기기의 `client.log` | `nas-client.log`, 311줄, `8df2cf2a6d21cec8b5e1b16fac7efc98abb2d60d685abd9532aad44d4becd7a8` |

### 3.1 DXGI 정체 7회

`nas-host-other.log`의 아래 watchdog 원문에는 자체 시각 접두사가 없다. 따라서 **주변 타임스탬프를 위치 표식으로 적었으며 정확한 감시기 실행 시각으로 단정하지 않는다.** 호출 내부 정체 시간은 `phaseAgeUs`다.

| 줄 | 직전 / 직후 타임스탬프 (09-12) | 단계 | 정체 시간 |
|---|---|---|---|
| 51903 | 11:30:02.644 / 11:30:08.226 | acquire | 5.155초 |
| 54209 | 13:10:35.989 / 13:10:36.420 | release | 5.070초 |
| 62213 | 13:16:25.859 / 13:16:26.679 | release | 5.437초 |
| 63794 | 13:17:53.210 / 13:17:54.596 | release | 5.016초 |
| 72209 | 13:23:56.290 / 13:23:57.039 | release | 5.394초 |
| 72528 | 13:34:05.215 / 13:34:07.466 | acquire | 5.222초 |
| 73830 | 14:40:46.033 / 14:40:47.485 | release | 5.053초 |

모두 `terminating (exit 44) for supervisor relaunch`. 마지막 사건 다음에는 14:40:47.486 startup, 47.487 UDP bind, 47.497 directory agent started, 47.834 directory online이 이어진다. **프로세스/등록 복귀는 확인되지만 원래 세션의 자동 복원 완료 판정은 아니다.** 이 PC Host 사본에서는 같은 표식이 0회다. 사본 밖 과거까지 “발생 없음”으로 확대하지 않는다.

### 3.2 PC 뷰어가 실제로 닫힌 사례

`nas-viewer.log:26324` 13:16:29.564 `link-silent`, `:26326` 13:16:33.286 `peer-lost`, `:26331` 13:16:39.004 `session-dead action=close`, `:26332` 13:16:39.031 `done`. Shell에는 13:16:41 새 session started가 있다. 이 사례에서 **감지·종료가 전혀 안 됐다는 주장은 틀리다.** 새 시작이 자동 재연결이라는 표식은 없으며 Shell 코드도 자동 재연결을 하지 않는다.

`nas-host-other.log`에는 encode 실패 카운터 줄이 6개 있으나 9월 9일과 12일 자료가 섞여 있다. 출력 고갈 표식은 이번 두 Host 사본에서 0개다. 따라서 아래 인코더 복구 공백을 이번 사건의 확정 원인으로 쓰지 않는다. 소스/현장 바이너리 동일성과 기기 간 시계 오차도 미확정이다.

## 4. 스레드 소유·복구 지도

기본 소스 경로는 `apps/native_poc/src/`. 생성→정지→join 순서만으로 정상이라고 판정하지 않고, 대기 중인 작업이 실제로 끝날 조건을 함께 적었다.

| 실행 영역 | 생성/소유 | 중지·대기 | 남은 주요 경계 |
|---|---|---|---|
| Host Shell supervisor / stdout reader | `host_app_main.cpp::StreamingHostProcess` | child 종료 후 reader join, supervisor join | Stop의 무제한 join, pipe 정체, 반복 code44 같은 경로 재시작 |
| Host 로그인 / 업데이트 | `host_app_main.cpp`, detached | 결과를 HWND로 post | 창 수명·예외·post 실패; 로그인 결과의 세대는 추가 검증 필요 |
| Host directory agent | `directory_client.cpp::HostAgent` | running=false, thread join | HTTP/DNS 동안 취소 공백, observe 주소 공유, 6회 재시도 소진 |
| Host TCP control / UDP reader / dispatcher | `host_startup_control.cpp`, `SessionState` | stop/channel close/epoch notify/join | 초기화 early return, 오류 고착, 입력 pipe 대기 |
| Host 메인 capture/encode | `native_video_host_main.cpp` | stage flow와 `shutdown_host` | startup 감시 공백, 정상 return 우회, 출력 없음과 loop 생존 불일치 |
| DXGI capture worker | `libs/capture/src/capture_backend_dxgi.cpp` | stop + join; 별도 watchdog kill | acquire/release 실제 정체, 같은 backend 반복 |
| WGC callback / readback worker | `host_capture_session.cpp`, `d3d_capture_readback.cpp` | cookie 무효화/pool close/readback join | 복구 실패 상태 소비, GPU/COM 종료 무제한 대기 |
| GDI worker process / pipe reader | `gdi_capture_process.cpp` | child bounded wait/terminate, reader join | 최종 reader join은 별도 deadline 없음 |
| Host encoded sender | `host_encoded_sender.cpp` | stop + cv notify + join | 정상 epoch/barrier 존재; 블로킹 OS 송신 종료 상한 별도 |
| 메인/DXGI watchdog | `host_startup_capture.cpp` | RAII joiners | kill 직전 동기 로그, 메인 stop과 종료 감시 결합 |
| PC Shell 로그인/hosts/Viewer 감시 | `client_shell_main.cpp`, detached | 현재는 owner task join/cancel 없음 | 늦은 로그인·목록·자식 종료 응답 혼입 |
| PC control / recv+decode | `viewer_startup.cpp`, `ViewerContext` | channel close/socket close, 각각 3초 join | recv가 영상·ACK 공동 처리; 종료는 bounded이나 재연결 없음 |
| PC UI/present/liveness | `viewer_window_proc.cpp`, UI thread | WM_CLOSE/message pump 종료 | Present가 UI를 막으면 같은 UI의 watchdog도 멈춤 |
| secure input / unlock relay | `secure_input_service_main.cpp`, `host_unlock_relay.cpp` | stop + CancelSynchronousIo 반복 + join | 일반 입력 broker 동기 write 별도, cancel 성공 대기 상한 |
| log uploader | `log_upload.cpp`, static worker | Shutdown guard + cv + join | 인증 pause/재시도는 존재; 진행 중 네트워크 호출 종료 상한 |
| WebView UI 프로세스 | Shell/toolbar/macro | COM/controller 소유 | 브라우저 프로세스 실패 후 재생성 정책 없음 |

## 5. Host / 캡처 / 인코더 발견 원장

각 항목의 완료 조건은 **후속 수정 때 필요한 검증**이며 이번에 성공했다는 뜻이 아니다.

| ID / 우선순위 / 판정 | 근거 위치와 문제 | 복구 방향·완료 조건 |
|---|---|---|
| H01 / P1 / 로그+정책 공백 | `host_app_main.cpp:579-635`, `host_startup_capture.cpp:114-149`: code43/44는 crashStreak에서 제외하고 반복 횟수는 relaunch 지연만 바꿈. 7회 실제 DXGI wedge | code44 반복 시 장치/백엔드별 제한·안전한 대체 경로·사용자 실패 상태를 정의. 동일 정체 주입 반복에서 재시작만 무한 반복하지 않고 새 영상·입력 복귀까지 측정 |
| H02 / P1 / 코드 | `host_stage_encode_send_h264.cpp:361-368,398-470`: encode 실패는 count+Continue, accepted/no-output 진단은 로그만. 메인 heartbeat는 계속 갱신 | input/accepted/output/error 시계를 분리하고 출력 없는 active episode에 bounded 재초기화/프로세스 승격. provenance FIFO overflow 복구는 별도 존재하며 이 항목의 대체 증거가 아님. input 미수용 또는 overflow 이전 정체도 유계 회복 검사 |
| H03 / P1 / 코드 | `native_video_host_main.cpp:142,248-258,280`, `host_session.hpp:84-87`: control thread 시작 후 startup의 다수 return, stage Flow::Return은 shutdown을 우회. std::thread가 joinable인 채 파괴되면 정상 exitCode 대신 terminate; 앞선 resource 소멸이 먼저 막힐 수도 있음 | 런타임 RAII/하나의 unwind 경로. 그래픽/encoder/readback/capture 초기화 단계별 실패와 frozen-ring exit3에서 모든 소유 스레드 종료·소켓 수명·의도한 종료 코드 검증 |
| H04 / P1 / 코드 | `native_video_host_main.cpp:249-260`: 메인 watchdog은 그래픽·encoder·readback·capture 초기화가 모두 끝난 뒤 시작. supervisor는 child process INFINITE wait | bring-up watchdog을 초기화 전에 별도 arm하거나 부모가 startup deadline을 소유. graphics/MF/capture 호출 중 무한 대기 주입에도 시작 실패를 유계 보고 |
| H05 / P1 / 코드 조건부 | `host_shutdown.cpp:115-148`, `host_startup_capture.cpp:377-379`: stop=true가 메인 watchdog도 종료시키는데 control/readback/sender join은 무제한. DXGI 별도 감시는 남지만 모든 종료 자원을 커버하지 않음 | 종료 전용 deadline을 유지하고 shared state 해제 전 강제 종료 경계 정의. readback Map, broker write, COM close 정체 각각에서 process exit 유계 |
| H06 / P1 / 코드 조건부 | `host_app_main.cpp:270-272,415-417,572-577`: 일반 Stop은 TerminateProcess 반환을 확인하지 않고 supervisor.join. kChildExitBudgetMs는 일반 join 경로를 제한하지 않음. reader join도 EOF에 의존 | terminate 결과/child wait/pipe drain 각각 deadline과 실패 결과. 종료 거부/pipe writer 잔존에서 Host UI와 업데이트 handoff가 무한 정지하지 않는지 검사 |
| H07 / P1 / 코드 | `host_stage_geometry.cpp:120-169`, `host_stage_watchdogs.cpp:110-141,154-230`, `host_capture_session.cpp:366-423`: pending 소비→Detach(sessionReady=false)→restart 실패 로그. size-change/첫 frozen-ring 실패 뒤 pending 재무장 없음. watchdog은 sessionReady를 요구하므로 재진입 안 됨 | RestartPending/RetryBackoff 상태 보존. resize 또는 ring 회복 중 한 번 실패하고 이후 정상 API를 주어 추가 클릭 없이 캡처 재개. idle reattach와 backend fallback은 이미 별도 재시도가 있음 |
| H08 / P1 / 코드 | `host_encoder_manager.cpp:50-77,121-141`, `host_loop_helpers.cpp:231-242`: 기존 codec shutdown 후 initialize 실패. 확정 geometry는 source를 먼저 저장하고 실패를 void로 삼킴. 같은 source의 다음 확인은 조기 return | encoder 준비 성공과 active/source commit 결합, 오류 전달 및 dirty retry 상태. 새 geometry init 1회 실패 후 같은 입력만으로 복구; 메인 생존을 성공으로 판정하지 않기 |
| H09 / P1 / 코드 | `host_stage_selection.cpp:101-141`는 선택 모니터 item/ID를 저장하지만 `host_capture_session.cpp:426,448-449,520`는 restart마다 primary_monitor_info 및 primary WGC item으로 덮어씀 | 선택 모니터 identity를 capture/backend/GDI/input geometry에 일관되게 전달. 주/보조 화면 색상을 다르게 하고 선택·idle 재attach·backend 전환 뒤 실제 픽셀과 입력 좌표 검증 |
| H10 / P1 / 코드 조건부 | `host_capture_device.cpp:125-144`: 명시 preferredWindow 생성 실패가 monitor fallback으로 이어짐. `host_capture_session.cpp:440-445`, `host_loop_helpers.cpp` 선택 호출자는 요청 HWND 상태를 유지할 수 있음 | 명시 창 실패는 fail-closed, 대체 허용 시 실제 source identity 반환. 사라지는 창/권한 오류 주입에서 엉뚱한 desktop 송출0 확인 |
| H11 / P1 / 코드 | `directory_client.cpp:894-916`: observe metadata 조회는 최대6회, 소진 뒤 시간 경과에 의한 재무장 없음. HTTPS + 광고 미확보 상태에서는 서버가 나중에 정상화돼도 스스로 다시 health를 묻지 않음 | 일시 오류와 영구 미설정을 분리하고 제한된 저빈도 재시도/수동 재시도 경로. 6회 실패 후 endpoint 복구 시 Host 재로그인·재시작 없이 online |
| H12 / P2 / 코드 경합 | `directory_client.cpp:513-514` UDP reader의 `observeAddr_` 읽기와 `:952` HostAgent worker의 resolve_ipv4 출력 쓰기가 같은 mutex/atomic snapshot을 사용하지 않음 | 주소를 로컬에서 완성한 뒤 동기화된 snapshot으로 교체. 초기 HTTPS health 발견/재시도와 UDP 수신을 겹쳐 race 검증 |
| H13 / P2 / 코드 조건부 | `host_startup_control.cpp:258-274`: 치명적 recv 오류도 로그+50ms 대기 후 같은 socket으로 영구 재시도. 정상 timeout/reset을 복원 불가 descriptor 오류와 구분하지 않음 | 영구 오류에서 socket/프로세스 재구성 또는 명시 실패로 승격. WSAENOTSOCK 등 fault에서 무한 'continuing' 없이 회복. 현재 로그에서 이 오류 발생을 확인한 것은 아님 |

## 6. PC Shell / Viewer / 입력 발견 원장

| ID / 우선순위 / 판정 | 근거 위치와 문제 | 복구 방향·완료 조건 |
|---|---|---|
| V01 / P1 / 프로브+정책 공백 | `viewer_recv_liveness.hpp:127-140`, `viewer_session_watchdog.cpp:50-99`: control이 살아 있는 동안 video publish가 99초 없어도 sessionDead=false. decode 정체는 로그만. 제어 datagram이 계속 오면 linkSilent도 false | active target의 first-frame/output/render deadline을 별도로 정의. 정상 정지 화면·picker·잠금은 제외하고 출력 장애에서 사용자가 이해할 실패/재연결 제공. UDP read는 자체 Tick과12초 timeout이 있으므로 'decode 정체면 영원히 control=true'라고 단정하지 않음 |
| V02 / P1 / 코드 조건부 | `viewer_window_proc.cpp:636-640`, `viewer_present.cpp`, `viewer_nv12_renderer.hpp:346`: 감시가 UI WM_TIMER이고 GPU Present도 UI에서 동기 실행 | UI progress를 외부 스레드/부모에서 관측. Present/Draw가 복귀하지 않는 조건에서 watchdog 자체가 멎지 않도록 유계 process 회복. 현재 PC UI hang 실기 주입은 안 함 |
| V03 / P2 / 프로브+정책 공백 | `viewer_session_watchdog.cpp:28-33`: controlEverConnected를 UI poll에서만 설정. control이 첫 poll 전에 실패하거나 처음부터 unavailable이면 controlGoneSinceUs=0 유지 | 'ever connected'와 '연결 시작/실패' 상태 구분. 최초 제어 실패 + 영상 없음에서도 유계 실패 안내. 기존 pure test L9의 의도된 정책임을 확인했으며 새로운 회귀로 단정하지 않음 |
| V04 / P1 / 코드 | `viewer_nv12_renderer.hpp:346-355`, `viewer_present.cpp:251-275`: Present 실패 후 ready/device 유지, init은 ready=false에서만. device-removed/reset도 정상 재구성 상태로 전환하지 않음 | HRESULT 보존·장치 손실 분류·renderer와 decoder surface ownership 조율. 실패 뒤 새 장치에서 실제 픽셀 표시; 반복 fail count만 증가하지 않기 |
| V05 / P1 / 코드 | `viewer_present.cpp:290-323`: StretchDIBits 반환을 무시하고 presented=true, lastPresentedVersion/capture anchor 갱신. NV12 GDI fallback은 같은 HWND의 flip swapchain도 유지 | GDI/D3D 전환과 실제 blit 결과 확인. 실패 주입 시 presented anchor 전진0; DWM 화면 검증. picker 합성 문제는 다른 `55c93c0` 작업과 분리되며 이 실패 분기는 사본에 남음 |
| V06 / P1 / 코드 | `client_shell_main.cpp:754-811,826-838,1109-1129`, `ui/shell.html:400-415`: 로그인/목록 worker 완료가 현재 operation/owner epoch 검증 없이 상태·UI를 적용. refresh 후 logout하면 늦은 hosts가 로그인 화면을 다시 숨김 | login/refresh/logout을 세대별 취소·채택. 지연 응답의 A→logout→B 로그인에서 A token/host list/UI 복귀0. 로그아웃 뒤 token 복원은 login worker 경로이고 refresh만으로 token 복원된다고 혼동하지 않음 |
| V07 / P2 / 코드 | `client_shell_main.cpp:548-640,1060-1077`: update handoff는 detached waiter인데 start_client_update 반환 즉시 gUpdateStarting=false/updateBusy=false. 첫 다운로드·credential handoff가 아직 진행 중이어도 다음 클릭 허용 | waiter 종료까지 operation guard 유지. 느린 다운로드 중 재클릭에서 추가 UAC/중복 bootstrap0. updater의 별도 install lock을 건너뛸 수 있다는 주장은 아님 |
| V08 / P2 / 코드·정책 공백 | `client_shell_main.cpp:694-702,720-749`, `client_update_gate.cpp`: 네트워크 결과 전에 해당 owner/epoch를 checked로 확정. Unreachable/Rejected에 재시도 일정이 없고 같은 로그인에서는 재조회 억제 | transient 실패의 bounded retry 또는 확인 버튼. 로그인 후 첫 요청 실패→서버 정상화만으로 새 버전 확인 가능. 검증 거부는 자동 신뢰 완화 금지 |
| V09 / P2 / 정책 공백 | `viewer_session_watchdog.cpp:91-100`, `client_shell_main.cpp:1015-1028`: 죽은 viewer를 닫고 Shell은 idle/error만 표시. 직전 host/target을 재연결하는 상태 머신 없음 | '자동 재연결' 제품 요구를 명시하고 횟수·취소·로그아웃 경계·입력 초기화 포함. Host 재시작 후 같은 세션 목적을 복원하는 종단 시험. 단순 프로세스 재시작을 reconnect로 보고하지 않기 |
| V10 / P1 / 코드 | `viewer_window_proc.cpp:128-136,397-435`, `viewer_layout.cpp:105`: 영상 내부 DOWN 후 영상 밖 UP이면 좌표 변환 실패 return이 mouseButtons 해제/UP enqueue/ReleaseCapture보다 먼저임. 좌·우·중간 버튼 경로 해당 | release 상태 정리를 좌표 성공과 분리하고 마지막 유효 좌표 정책. letterbox·창 밖 release에서 원격 버튼 해제 확인. WM_CAPTURECHANGED 등 다른 정리로 풀릴 수 있어 영구 고착 단정 금지. 별도 사건 문서의 후보를 직접 재대조함 |
| V11 / P2 / 코드 | `native_video_client_shared_core.cpp:63-93`: queue>=256에서도 move가 없고 새 입력이 key/text이면 계속 push. 크기·나이·bytes 상한이 아님 | key-up 안전성을 유지하면서 admission/backpressure·입력 epoch reset 설계. 링크 정체 중 긴 붙여넣기/키 입력에서 메모리 상한 및 회복 후 오래된 입력 폭주 방지 |
| V12 / P2 / 코드, TCP 옵션 | `viewer_video_receiver.cpp:530-544`: select는 첫 byte만 보장하며 뒤 recv_all은 메시지 전체 deadline 없음. RawFrame payloadSize대로 무상한 할당, width/height/stride/buffer length의 일관성 검증 없이 publish | TCP framing deadline/공용 크기 validator. partial header/body 후 peer 생존, 초대형 크기, 짧은 BGRA에서 유계 실패. 기본 UDP 사건 원인으로 귀속하지 않음 |
| V13 / P2 / 코드 구조 | `viewer_video_receiver.cpp::run_udp`, `viewer_video_receiver_frame.cpp::process_h264_frame`: socket recv/영상 조립/decode/control ACK dispatch가 같은 recv thread. decode가 길면 ACK도 대기; control은 요청 하나마다 응답을 기다림 | 수신과 decode의 bounded queue 분리 또는 단계 예산·backpressure. input ACK의 socket 도착→dispatch→소비 지연을 따로 계측. 스레드 수 증가만으로 개선 판정 금지 |

## 7. 공유 I/O / 서버 / 수명 경계

| ID / 우선순위 / 판정 | 근거 위치와 문제 | 복구 방향·완료 조건 |
|---|---|---|
| B01 / P1 / 코드 조건부 | `secure_input_broker.cpp:139-163`: pipe를 동기 GENERIC_WRITE로 열고 mutex 안에서 WriteFile. 3초 Connect 예산은 write를 제한하지 않음 | receiver가 pipe를 읽지 않을 때 취소 가능한 write deadline. 제어 스레드·Host 종료가 같이 묶이지 않아야 함 |
| B02 / P2 / 위험 | `host_startup_capture.cpp:120-122,398-403`, `host_app_main.cpp:498-507`: watchdog이 child stdout/stderr pipe에 동기 WriteFile 후 TerminateProcess. 부모 log reader/디스크가 막혀 pipe가 차면 kill 전에 감시 스레드도 막힐 수 있음 | kill 결정을 로그 완료에 의존시키지 않기. stdout 미소비 fault에서도 지정 시간에 종료. 이번 7회에서 pipe hang이 있었다는 증거는 없음 |
| B03 / P2 / 코드 | `udp_control_channel.cpp:171-196,273-284`: 개별8MiB 제한은 있으나 rxPending의 총 byte/항목/TTL 제한 없음. fragIndex 개수만으로 완료하고 offset 전체 coverage를 검증하지 않음 | aggregate cap, TTL, seq window, 범위 중복/빈틈 검사. 잘못된 peer 패킷 연속에서도 메모리/CPU 제한. 정상 네트워크가 임의 offset을 만든다는 주장은 아님 |
| B04 / P2 / 위험 | `update_credential_channel.cpp:63-72`: CancelIoEx 후 1초 event wait 결과를 무시하고 return. 호출자 OVERLAPPED/버퍼 수명보다 취소 완료가 늦을 가능성을 제거하지 못함 | cancel 요청과 I/O completion을 분리해 소유권 유지. 취소 지연/실패 fault에서 pending storage 해제0. 실제 늦은 커널 완료/메모리 손상 재현은 미수행 |
| B05 / P2 / 코드 | `winhttp_transport.cpp:142-164`: QueryDataAvailable/ReadData 실패도 EOF와 같이 break한 뒤 sent=true/return true. 중간까지 받은 body가 성공 응답으로 반환 가능 | 정상 EOF와 I/O 오류 구분, partial 실패 전달. 일부 JSON 이후 read 오류 주입에서 전체 성공 금지. HTTP timeout은 호출별이지 전체 wall-clock deadline이 아님 |
| B06 / P1 / 프로브+코드 | `apps/directory/server.js:494-502,535-547,767-790`: store read의 ENOENT 외 실패도 빈 store로 기동. save 실패를 삼키고 host register는 새 token을 메모리에 반영한 뒤200. 재기동 후 자격·호스트 상태 복구 불가 가능 | 손상/권한오류는 빈 초기화와 분리, durable save 성공 후 credential commit. EIO/디스크 full/rename 실패에서 기존 데이터·토큰 보존 및 명시 실패 |
| B07 / P2 / 코드 | `apps/directory/server.js:551-556,717-740,747-760`: scryptSync가 같은 Node event loop에서 실행되고 host/register에는 login의 backoff 공유가 없음 | async bounded KDF·공용 인증 admission. 합법적 재로그인 부하에서도 relay/heartbeat 지연 예산 측정. NAS에 부하를 가하지 않았음 |
| B08 / P3 / 코드 | `d3d_capture_readback.cpp:27-32`: QPC ticks×1,000,000 선곱 overflow. 공용 qpc_now_us와 다른 구현 | 몫/나머지 안전 변환 통일. 10MHz 가정 약21.35일 경계 전후 단조 증가. 실제 QPC 주파수·uptime 미측정, 현장 원인으로 단정 금지 |
| B09 / P2 / 코드+위험 | `client_shell_main.cpp:340-345,754,826,1015`, `update_check.cpp:82`: detached 작업은 창/전역 상태 종료와 join 관계 없음. post_to_page는 PostMessage 실패 시 heap payload를 해제하지 않음 | operation owner/cancellation, HWND generation 및 post 실패 해제. 닫기 직전 지연 login/check/child-exit에서 stale UI/로그 재구성/누수 검사. 실제 use-after-free는 미재현 위험으로만 기록 |
| B10 / P2 / 정책 공백 | `client_shell_main.cpp` 및 `client_macro_window.cpp`: WebMessageReceived는 있으나 WebView ProcessFailed 복구 handler 없음 | browser/render process 실패 후 재생성 또는 명시 오류·재시도, 인증 상태 안전 복원. WebView 실기 crash 주입은 미수행 |

## 8. 이전 원장·병행 작업과의 중복 정리

이전 [2026-09-08 원장](full_code_audit_2026-09-08.md)의 항목을 그대로 현재 결함으로 복사하지 않았다.

| 기존 이슈 / 경로 | 이번 재확인 |
|---|---|
| 메인 watchdog detached UAF | `MainLoopWatchdogThread`와 DXGI joiner가 현재 존재. 이 결함 자체는 재등록하지 않음 |
| Host Stop vs CreateProcess publish 경합 | `host_app_main.cpp:560-568` running 재확인 존재. H06은 별개인 무제한 join/종료 실패 경계 |
| UDP maintenance가 control 패킷에 굶음(V14) | `viewer_video_receiver.cpp:323` loop 앞 maintenance 존재. 기존 문제를 현재 미수정으로 적지 않음 |
| provenance 오류 후 정지 화면에서 재시도 없음 | `host_stage_time_limit.cpp:105` tick 재시도+kick 존재. H02/H08은 provenance에만 의존할 수 없는 오류 분기 |
| idle detach 후 재attach 실패 | `host_stage_stream_active.cpp:119-156` bounded backoff와 성공 후 applied commit 존재. H07은 size-change/frozen-ring 등의 다른 경로 |
| DXGI/GDI runtime fallback 실패 | `host_stage_backend.cpp:157-188` 요청 플래그 재무장 존재. H07과 혼동 금지 |
| log uploader 종료시 joinable static terminate / 단발 transient drop | Shutdown guard, owner/config generation, 401 pause, transient retry 존재. 모든 경로가 완전히 안전하다는 인증은 아님 |
| picker DWM 합성 | 기존 history #530 및 병행 `55c93c0`의 별도 수정. 이번 조사에서 직접 UI 재현·승인하지 않음. V04/V05 실패 경로는 별도 |

병행 [게임 원격 사건 코드 조사](incident_2026-09-12_code_review.md)의 드래그 UP, 직렬 입력, recv/decode 결합은 V10/V13과 중복된다. 파일은 덮어쓰지 않았으며 해당 문서의 실기 수치를 본 조사에서 직접 측정했다고 표현하지 않는다. 기존 Android 전용·보안·배포 항목은 원장에 남기고 여기서 전부 재검증했다고 하지 않는다.

## 9. 직접 실행한 소형 검증

### 제품 liveness 헤더 프로브

고정 사본의 `viewer_recv_liveness.hpp`를 그대로 include한 C++17 프로그램을 MSVC x64로 컴파일·실행했다. UI/GPU/네트워크를 흉내 내어 정상이라고 통과시키는 테스트가 아니라, 판단 함수에 구체적인 실패 상태를 넣은 검사다. 명령과 원문은 [프로브 원문](host_pc_thread_recovery_probes_2026-09-12.md) 및 `.claude/audit-20260912-direct/{compile-probe.cmd,liveness_probe.cpp}`에 있다.

```
control_alive_video_absent_99s stalled=0 silent=0 dead=0
decode_stalled_99s_control_alive stalled=1 dead=0
control_never_observed_connected dead=0
control_gone_10s_video_absent dead=1
PROBE CONFIRMED (policy gaps, not product health PASS)
```

컴파일/실행 종료 코드0. 마지막 줄은 정상 대조: 모든 상태에서 무조건 dead=false인 잘못된 프로브가 아님. 재현 입력: now=100초, lastPublish=1초, 첫 케이스는 stage=Recv/최근 datagram/connected=true, 둘째 stage=Decode/stageEnter=1초, 셋째 connected=false/controlGoneSince=0, 넷째 controlGoneSince=90초. 기본 임계값은 stall2초/silent3초/dead5초다. **이 결과는 실제 GPU 정체 후 UI 복구 성공 증거가 아니다.**

### 서버 저장 실패 프로브

고정 `server.js`의 `loadStore`, `indexHostTokens`, `saveStoreNow` 함수 원문을 Node VM에 넣고 filesystem만 격리했다. `readFileSync` EIO → 기존 account가 빈 store로 대체됨, `writeFileSync` 실패 → saveStoreNow는 예외/실패값 없이 정상 undefined 반환. 2개 assertion 통과, 종료 코드0. host/register의200 응답은 프로브가 실행한 HTTP 경로가 아니라 소스 호출자 대조 근거다. NAS 파일/서버 상태를 바꾸지 않았다.

## 10. 후속 수정 순서와 완료 기준

1. **H01/H02/H03/H04/H05/H07/H08:** 상태 머신을 `Starting → Active → Recovering → Backoff → Failed/Stopped`로 명확히 하고 startup/output/shutdown의 서로 다른 시계를 둔다. 모든 오류를 exit로 바꾸기보다 제어·출력·자원 생존을 따로 판단한다.
2. **H09/H10/V10:** 잘못된 대상 송출 및 입력 release 소실을 먼저 차단. 모니터/창/입력 target identity를 성공 후 확정한다.
3. **V01~V05/V09:** first frame·decode·실제 present·UI heartbeat를 구분하고 부모/감시자의 bounded recovery에 연결. 기존 대상 자동 복원은 로그인/취소/잠금 정책을 정한 뒤 구현한다.
4. **H11/H12/V06~V08/B01~B06:** 일시 오류 후 스스로 다시 시도할 수 있는 상태와 비동기 소유권·I/O cancellation 보강.
5. **V11~V13/B03/B07~B10:** 큐·리소스·메시지 예산, 입력/영상 분리 계측, 관측 정확도와 장기 안정성.

필수 회귀는 실패를 한 번 주입하고 제거했을 때 **재시작 버튼 없이 새 화면·선택 대상·입력이 돌아오는지**, 영구 실패에서는 **무한 대기/무한 재시작 대신 명시 실패·취소가 되는지**, shutdown 중에는 **스레드/소켓/pipe 수명이 bounded인지**다. 정상 정지 화면·picker·잠금 상태를 오탐하지 않는 음성 대조도 포함한다.

### 10.1 바로 수정 작업으로 옮길 수 있는 방안

아래는 **수정안**이며 반영 완료가 아니다. 원장 표에는 나머지 항목까지 개별 수정·검증 조건을 적었다. 새 timeout의 정확한 값과 백엔드 전환 정책은 실제 장치에서 측정해 정한다.

| 관련 항목 | 확인된 원인 → 구체적인 수정안 |
|---|---|
| H01, V09 | **같은 DXGI 경로를 재시작만 함** → `StreamingHostProcess::Supervise`에 종료 사유·장치·백엔드별 반복 실패 기록을 유지하고, 일정 횟수 이상이면 검증된 대체 백엔드로 제한 시간 동안 시작하거나 명시 Failed 상태로 전환한다. 정상 영상·입력 확인 후에만 실패 기록을 해제한다. Shell 재연결은 기존 host/target을 보존하되 logout/취소 시 폐기하고, 눌린 키를 재전송하지 않는다. **DXGI 내부 정체 원인 자체는 미확정**이므로 드라이버 수정이라고 표현하지 않는다. |
| H02, H08 | **loop 생존을 출력 생존으로 간주 / codec 실패 상태 유실** → `EncoderState`에 `needsReinitialize`, 마지막 실제 input 수용/출력 시각을 둔다. `ApplyTarget` 실패를 caller까지 전달하고 `stage_time_limit`에서 새 프레임과 무관하게 backoff 재시도한다. 지속적인 input/출력 단절만 재초기화하고 정지 화면·stream inactive는 제외한다. 재초기화가 연속 실패하면 부모가 식별할 복구 종료 코드로 승격한다. |
| H03~H06 | **early return과 stop 시 감시 종료, 무제한 join** → 시작한 자원을 소유하는 runtime guard를 도입해 모든 return이 stop/wake/join을 거치게 한다. startup과 shutdown에는 메인 loop stop과 독립된 deadline을 둔다. 종료 실패 시 shared state를 파괴하거나 thread를 detach하지 않고, 식별된 자식 프로세스 단위로 최종 종료/실패 보고한다. 부모의 TerminateProcess·wait 결과도 확인한다. |
| H07 | **restart 요청을 성공 전에 지움** → geometry/frozen-ring의 플래그를 즉시 소비하는 대신 `pending reason + target + retryAt + attempts`로 옮긴다. restart 실패 시 pending 유지, 성공 후에만 clear/sessionReady 확정. `sessionReady=false`인 상태도 복구 scheduler가 처리하도록 한다. |
| H09, H10 | **재시작이 요청 대상을 버리거나 다른 대상으로 폴백** → 선택한 모니터의 안정적 장치 식별자를 저장하고 매 restart에서 현재 HMONITOR를 다시 해석한다. DXGI/WGC/GDI와 입력 좌표가 같은 실제 대상을 사용하게 한다. `CreateItemForPrimaryMonitor`를 명시 창/명시 모니터/기본 대상 함수로 나누고, 명시 대상 실패는 다른 화면으로 성공 처리하지 않는다. candidate 생성 성공 후 선택 ID·title·geometry를 함께 확정한다. |
| H11~H13 | **재시도 횟수 소진·비동기 주소 공유·영구 socket 오류 반복** → observe metadata는 재시도 상한 소진 후에도 저빈도 재시도/수동 refresh로 재무장한다. sockaddr를 로컬 임시값에 만든 후 mutex 보호 snapshot으로 교체한다. UDP 오류를 일시/영구로 분류하고 영구 오류는 socket 또는 child 재구성으로 넘긴다. |
| V01~V05 | **복구 판단이 control/publish에 편중되고 renderer 실패를 숨김** → 연결 시작·첫 영상·decode 반환·실제 present·UI heartbeat 시계를 분리한다. renderer의 HRESULT를 보존하고 device removed/reset에서 resources를 안전한 순서로 다시 만든다. StretchDIBits 실패는 presented=false로 유지하고 frame anchor를 갱신하지 않는다. UI가 막히면 같은 UI timer가 아니라 별도 감시자가 판단하도록 한다. |
| V06~V08, B09 | **비동기 결과의 수명·완료 경계가 없음** → login/hosts/update 작업에 operation ID와 owner epoch를 캡처하고 UI thread에서 현재 작업인지 검증한 뒤 결과를 적용한다. logout/창 종료 시 무효화한다. updateBusy는 detached waiter의 최종 종료 경로에서만 해제한다. check의 '시도함'과 '성공 확인함'을 분리해 transient 실패에 backoff를 둔다. PostMessage 실패 시 payload를 즉시 해제한다. |
| V10~V13 | **UP 처리가 좌표에 종속 / 무상한 입력 / ACK가 decode를 기다림** → 버튼을 실제로 누른 상태라면 UP에서 좌표 변환 전에 상태를 정리하고 마지막 유효 좌표로 release를 보낸다. 입력 queue에는 byte·age 예산과 overflow 시 안전한 release/reset 규칙을 둔다. TCP는 전체 header/body deadline과 checked payload validator를 적용한다. recv/ACK dispatch와 decode를 분리할 때는 bounded queue와 프레임 참조 소유권을 함께 구현한다. |
| B01~B05 | **취소 가능한 I/O와 완료 확인 부족** → broker write를 overlapped+deadline으로 전환하고 completion까지 버퍼를 소유한다. credential 취소 후1초 경과를 완료로 간주하지 않는다. watchdog kill 전에 blocking 로그를 기다리지 않는다. UDP fragment에는 aggregate budget·TTL·범위 coverage를 추가한다. WinHTTP read 오류는 body를 성공 반환하지 않고 status/error를 일관되게 실패 처리한다. |
| B06~B08, B10 | **저장·인증·시간·UI 장애 경계 부족** → 서버는 ENOENT만 신규 store로 처리하고 parse/EIO는 원본 보존 후 시작 실패 또는 제한 상태로 둔다. write/rename 성공 후 token 교체를 commit한다. 인증 KDF는 bounded async 작업으로 옮겨 relay loop를 보호한다. readback QPC는 공용 안전 변환으로 통일한다. WebView ProcessFailed 시 UI 재생성 또는 명시 재시도 상태를 제공한다. |

## 11. 완료·미검증·Git 상태

- 완료: 소스 범위 목록화/해시 고정/위험 패턴 검색, 핵심 스레드·복구 분기 직접 추적, NAS 4개 스트림 대조, 2종 소형 프로브, 본 원장과 파일별 목록 작성.
- 미검증: 실제 드라이버 정체 원인, 모든 OS 오류 주입, 실 UI 버튼/모니터·GPU·UAC·게임 종단 행동, 상대 기기 실행 바이너리와 사본의 정확한 동일성. 제품 정상·수정 완료·배포 완료로 보고하지 않는다.
- 제품 소스 변경/제품 빌드/설치/재시작/게시 없음. 원본 로그와 키는 Git에 넣지 않는다. `git push` 미실행.
- 조사 종료 당시에는 Git MCP 부재로 커밋하지 않았다. 이후 사용자가 Git MCP 없이 작업 가능한지 확인하고 **새 브랜치에서 Codex의 직접 전체 수정**을 요청한 단계에서는 이 분리 브랜치에 로컬 Git CLI로 기록한다. 공유 정책 파일은 바꾸지 않으며 push는 보류한다. 구현·검증 진행은 별도 기록으로 구분한다.
