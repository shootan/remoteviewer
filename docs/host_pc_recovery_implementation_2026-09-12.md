# Host/PC 복구 수정 내역과 검증 상태 — 2026-09-12

사용자 지시로 Codex가 직접 수정한 후보다. 위치 `D:/remote/remote-worktrees/host-pc-recovery`, 브랜치 `audit/host-pc-recovery`, 기준 `035c4a4`. 서버 커밋은 `27d9e74`, Native 수정은 이 문서와 함께 별도 커밋한다. 원래 폴더와 설치 제품은 변경하지 않았다.

**현재 판정: 구현·자체 검증 후보. 전체 실기 완료나 배포 완료가 아니다.** 원장 36항목에는 정책 공백·조건부 위험도 포함된다. 아래의 구현 반영을 현장 36건 해결 확인으로 해석하지 않는다. 당시 근거와 파일 해시는 [조사 원장](host_pc_thread_recovery_audit_2026-09-12.md)·[파일 목록](host_pc_thread_recovery_inventory_2026-09-12.md)에 보존한다.

## 수정 목록

| 번호 / 원장 | 원인 | 반영 내용·주요 파일 |
|---|---|---|
| 1 / H01 | 반복 DXGI 정체 뒤 같은 방식으로 재시작 | `host_recovery_policy.hpp`, `host_app_main.cpp`: 부모가 code44 반복을 기억하고 2회 뒤 5분간 WGC 전환. 정상 5분 실행 시 실패 이력 초기화. 자식 전용 환경 블록으로 부모·업데이터에 임시 backend가 남지 않게 함 |
| 2 / H03·H04·H05 | 초기화 실패의 cleanup 우회, 초기화·종료 정체 감시 누락 | `native_video_host_main.cpp`, `host_startup.hpp`, `host_shutdown.cpp`, `host_watchdog.hpp`: RAII 단일 정리, startup 전 감시 시작, 자원 소멸까지 유지. startup 30초·shutdown 10초 제한. 정상 접속 대기는 예외 |
| 3 / H06 | 자식·reader·supervisor 종료 대기가 무제한 | `host_app_main.cpp`: Job Object로 자식 수명 소유, suspended 생성→job 배정→실행, supervisor stop 10초 제한 및 실패 시 프로세스 종료 경계 |
| 4 / H07 | 캡처 재시작 실패 뒤 sessionReady=false로 고착 | `host_capture_session.*`, `host_stage_time_limit.cpp`: restart pending과 0.5~8초 backoff 보존, 프레임 없이도 재시도, 성공 후 keyframe/kick 재무장 |
| 5 / H03·H05·H07 | 늦은 WGC callback의 해제된 상태 참조 위험 | `capture_callback_gate.hpp`, `host_capture_session.*`: attachment별 gate를 delegate가 소유, close 후 진입 차단, active callback drain 뒤 해제. drain 정체는 유계 종료 |
| 6 / H02·H08 | encode 오류·무출력은 로그만 남기고 실패 설정은 소실 | `host_encoder_manager.*`, encode/time-limit stage: 원하는 설정 pending 보존, 재초기화 backoff, 오류 2초/무출력 5초 뒤 복구 요청. 초기화 5회 실패 또는 60초 내 복구 예산 초과 시 exit46. 순수 입력 backpressure는 timeline 반복 초기화 금지 |
| 7 / H08 | 재시도 tune이 최신값을 덮거나 epoch 소실 | `host_main_loop_mailbox.hpp`, tune/selection/stats stage: 같은 epoch의 누락 필드만 병합, 최신 명시값 우선, 다른 epoch 격리. refit·품질 전환 실패도 재시도 |
| 8 / H09·H10 | 재시작/fallback 때 선택 대상과 캡처·입력 좌표 불일치 | capture session/device·input inject·control session: monitor device name 보존, handle 재해석, 사라진 명시 대상/다른 PID의 HWND 거부. 비주 모니터는 같은 대상 WGC로 처리, 입력도 캡처 모니터 좌표 사용 |
| 9 / H11·H12·H13 | observe 조회 6회 뒤 포기, 주소 경쟁, 폐기 소켓 반복 오류 | `directory_client.cpp`, `host_startup_control.cpp`: 상한 있는 주기로 조회 계속, 주소 mutex snapshot, 영구 소켓 오류 시 종료 |
| 10 / V06·B09 | 로그아웃 뒤 늦은 결과가 옛 인증·화면 복원 | `client_shell_main.cpp`, `host_app_main.cpp`: owner epoch·UI-thread 결과 채택, 전달 때도 epoch 검사, post 실패 payload 해제, 로그아웃/종료 시 epoch 선행 무효화 |
| 11 / V07·V08 | 업데이트 진행 중 busy 조기 해제, 첫 확인 실패 뒤 재시도 없음 | Shell·`ui/shell.html`: handoff 완료까지 busy, transient 확인 실패 최대 4회 재시도, 설정 화면 오류도 전역 busy 해제 |
| 12 / V09 | Viewer 장애 종료 뒤 재연결 없음 | Shell·HTML: 복구 종료 코드에 같은 Host로 최대 3회(1/2/4초) 재연결, 취소 버튼·로그아웃·수동 연결로 취소. **직전 개별 창 복원은 아직 없고 picker를 다시 표시** |
| 13 / V01·V02·V03 | control만 살아 있거나 UI/첫 영상 정체를 감지 못함 | viewer liveness/watchdog/startup: recv/decode·UI·첫 영상·필수 control 분리 감시. Host 정적 프레임 heartbeat capability를 Pong에 광고하여 정적인 구형 Host 오인 방지 |
| 14 / V04·V05 | 렌더 실패를 성공 처리, 장치 손상 뒤 복구 없음 | `viewer_present.cpp`: GDI 실제 반환 검사, flip HWND에 부적절한 GDI fallback 금지, swapchain 1회 재생성. 장치 제거/연속 2초 실패는 복구 종료 후 새 프로세스의 decoder/device로 복원 |
| 15 / V10·V11 | 영상 밖 mouse-up 누락, 입력 큐 무한 증가 | WndProc·shared core: 좌표 변환 전 버튼 해제, 마지막 유효 좌표로 UP 전달. 큐 admission 제한·release 우선 보존/병합·오래된 비해제 입력 폐기 |
| 16 / V13 | decode 정체가 UDP ACK도 막음 | `udp_receive_pump.hpp`, viewer receiver: 수신/ACK와 decode 분리, 영상 큐 1,024개/4MiB 제한·drop 계측, control은 backlog 우회 |
| 17 / V12·B03 | TCP 부분 메시지 무한 대기·무제한 payload, UDP 조각 총량/coverage 무검사 | `native_socket.hpp`, viewer receiver·UDP control: TCP 메시지 5초 deadline·raw/encoded 크기 검사. reliable control 64개/32MiB 총량·30초 TTL·조각 overlap/gap 거부 |
| 18 / B01·B02·B04 | pipe/log write가 종료 차단, 취소 전 OVERLAPPED 해제 위험 | bounded pipe/exit·secure broker·credential channel: overlapped write와 취소 완료 확인, 불확실한 write 재전송 금지. 진단 쓰기 전 독립 종료 예약·진단 길이 버퍼 제한 |
| 19 / B09·B10 | detached 작업 수명·계정 변경 뒤 옛 로그 업로드·WebView crash | async worker group·update check·Shell/Host·log upload·macro: worker 소유/회수·15초 shutdown, uploader mutex 안에서 작업 당시 계정 확인. WebView 최대 3회/60초 재생성·현재 인증 복원 |
| 20 / B05·B06·B07·B08 | HTTP 중간 실패 성공 처리·저장 실패 성공 응답·동기 인증 부하·QPC overflow | WinHTTP 부분 응답 실패/잔여 예산. 서버 durable 실패 rollback/503·손상 store 시작 거부·최대 4개 async KDF/공용 backoff. QPC 몫/나머지 변환. CMake HTML staging 및 exact-byte 서명 fixture 보존 |

WinHTTP request handle의 timeout 적용은 [Microsoft 문서](https://learn.microsoft.com/en-us/windows/win32/api/winhttp/nf-winhttp-winhttpsettimeouts)에 대조했다. DNS/connect 등 모든 OS 내부 호출의 절대 wall-clock 상한을 증명한 것은 아니다.

## 직접 검증

Windows 콘솔 session1/shotan Active, RDP 연결 없음(`qwinsta`), 단일 모니터. MSVC 19.44/SDK 10.0.26100.0 x64 Release. 임시 계정·프로필·자식은 worktree `.claude/test-tmp`에 격리했다.

| 검증 / 의존성 | 결과·증명 범위 |
|---|---|
| Host·Stream·Viewer·Client Release / build | 빌드 성공. `.claude/frozen-build.log`와 최종 추가 변경 빌드 로그. test-only entry는 출하 타깃에 연결하지 않음 |
| 서버 전체 회귀 / network·filesystem | `node apps/directory/test/run.js`: ALL PASS, exit0. 실제 HTTP+ENOSPC→503/기존 바이트·토큰 보존, 저장 복구 후 signup, login/register 공용 제한, 손상 store 시작 거부 |
| Native 11종 / pure-logic·network·gpu | `.claude/native-final-results.json`: 11개 모두 exit0. UDP loss/reorder/malformed/budget, viewer liveness, 입력 큐, mailbox epoch, 실제 MFT encode, update flow/credential, log upload/stop, Host startup failure |
| `remote60_recovery_process_test` / network·process·pipe | 최신 17 assertion PASS. 실제 blocked pipe write 제한·건강한 write 1회 전달, media 소비 정지 중 control 수신/backlog 제한, full stdout pipe 자식 exit44, callback gate/drain, DXGI fallback 정책, 부모 환경 보존 |
| `remote60_host_runtime_failure_test` / process | 실제 Host main/control workers, connection/graphics provider만 격리 주입. graphics 실패73 정상 unwind, startup hang exit43 약30,052ms, shutdown sender hang exit43 약10,068ms |
| 실제 `GNLinkStream.exe` 정상 대기 / process·network | loopback TCP 무접속 33,000ms 생존. 시험 자식만 종료. 정상 accept 대기가 startup 장애로 오인되지 않음 |
| `client_recovery_ui_runner.js` / WebView·network | 제품 Shell/native/HTTP, 18 PASS. DOM 로그인 A→지연 refresh→로그아웃→늦은 응답 차단→로그인 B. 전용 WebView browser PID crash→재생성→B 인증 보존. 취소 버튼 native 도달·설정 오류 busy 해제 |
| `remote60_viewer_window_proc_isolated_test` / UI·gpu | 41 PASS. 실제 WndProc/picker/render, 입력 network sink 대체. 영상 밖 3버튼 UP·picker 왕복 control/swapchain 보존. 시험 PID/owner로 창 탐색 제한 |
| `remote60_client_update_ui_test` / WebView | **전체 FAIL: 26 PASS / 1 FAIL.** 가시성·가림 부정 대조·활성·DOM 클릭→WebMessage·중복 차단·실패 후 버튼 복원 PASS. 후보의 서명 manifest가 없어 release pin FAIL. 이를 PASS로 재분류하거나 검증 우회하지 않음 |
| `winhttp_recovery_runner.js` / network | 실제 WinHTTP+격리 HTTP, 정상/잘린200/slow drip PASS. 350ms 요청 예산의 drip 실패 실측408ms, 시험 상한1,500ms 이내. TLS 장애 전체 증거는 아님 |

`.claude/client-recovery-ui.png`, `build-local/apps/native_poc/Release/client_update_ui.png`를 직접 열어 계정 B 복구 화면과 업데이트 버튼을 확인했다. DOM.click은 물리 클릭/UAC와 구분한다. 캡처·원시 로그는 로컬 증거이며 제품에 포함하지 않는다.

## 전체 해결 판정 전에 남은 범위

- [ ] H01/H02/H05/H07: 실제 DXGI/드라이버 반복 정체에서 새 영상·원격 입력 복귀와 soak. NAS wedge7회는 증상 근거이며 GPU 내부 원인 확정이 아님.
- [ ] H06: 실제 자식 종료 거부/외부 pipe writer 잔존의 Host UI·update handoff 종단 시험.
- [ ] H09/H10: 다중 모니터 제거·재연결과 실제 원격 입력 좌표 시험. 현재 한 모니터만 있음.
- [ ] H11: 이미 받은 잘못된 observe endpoint의 재발견 정책 추가 검토. 이번에는 6회 실패 후 영구 중단을 수정함.
- [ ] H08: mailbox epoch는 검사했으나 encoder pending target과 peer 전환 전체 조합은 추가 검증 필요.
- [ ] V09: 실제 Host 재시작→자동 재연결→영상/입력 종단 시험 및 직전 선택 창 자동 복원. 현재 같은 Host 연결 후 picker 재표시.
- [ ] V04/V05/B10: 실제 GPU device removal과 macro WebView 복구 동작. Shell browser crash는 검사했으나 모든 실패 유형은 아님.
- [ ] V07/B04: OS 업데이트 설치·UAC·credential 취소 지연의 커널 완료 실기. 설치는 실행하지 않음.
- [ ] 고정 후보 독립 검토·공유 브랜치와 통합 검토. 버전 인상·서명·NAS 게시·외부 hash/manifest 검증은 미수행.

Git MCP 없이 CLI 가능함을 설명한 뒤 사용자가 직접 전체 수정을 지시한 범위에서 로컬 CLI 커밋을 사용한다. 정책 파일 변경·**push 없음**. NAS는 읽기 전용 조사만 했으며 후보로 NAS나 설치 앱을 교체·재시작하지 않았다.


## 2026-09-14 커밋 이력 정정

위 내용은 구현 당시의 기록이다. Native 원본 `daacbb2`는 백업 브랜치에 보존하고 12개 수정 커밋으로 재구성했다. [대응표와 중간 빌드·검증 결과](host_pc_recovery_commit_split_2026-09-14.md)를 따른다. 제품 코드의 최종 내용은 동일하며 기존 미검증 상태도 유지한다.
