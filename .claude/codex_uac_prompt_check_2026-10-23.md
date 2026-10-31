# UAC 진입·복귀 화면 누락 — 0.2.162 원인 조사

## 최종 조사 결론 — 아래의 초기 가설보다 이 절이 우선

사용자 추가 재현: 카카오톡 실행→UAC 예/아니오 안 보임→첫 재접속으로 UAC 보임→예 선택→검은 화면에서 복귀 안 됨→두 번째 재접속으로 정상. **현재 WGC 경로에서 입력 데스크톱 전환에 맞춰 캡처 자원을 갱신하지 않는 수명 처리 누락을 문제 지점으로 확인했다.** 재접속에 따른 캡처 재생성이 두 전환을 수동으로 복구한다. 특정 Windows 내부 자원(아이템/세션/풀/공유 텍스처) 중 어느 것이 검은 내용을 내는지 픽셀 단위로 분해한 것은 아니며, 수정 A/B 실험은 아직 수행하지 않았다. 회귀 도입 commit은 미확정이다.

**초기 'WGC는 UAC를 절대 못 찍음', 'SYSTEM 캡처 helper 부재가 이번 직접 원인' 추정은 채택하지 않는다.** 과거 0.2.103 실기에 WGC 전환 후 UAC 표시/클릭 성공 기록이 있고, 이번에도 같은 사용자 계정 WGC를 재생성하면 UAC가 보였다는 직접 사용자 관찰이 있다. SYS worker 일반화나 UAC 보안 설정 변경을 이번 원인 결론으로 제안하지 않는다.

### 시각을 맞춘 직접 근거

- KakaoTalk.exe PID29044 생성: **10-23 18:07:23**, 사용자 PC의 CIM 조회. 사용자 승인 후 프로그램 실행과 일치한다.
- 로컬 Host: **18:07:05.472** epoch21 제어 종료→18:07:10.480 idle capture detach→**18:07:15.904 WGC 새 캡처/reattach**→18:07:17.422 대상 선택으로 WGC 재생성(streamGen20). 이 첫 재접속 후 UAC가 보였다는 사용자 관찰과 일치한다.
- secure_input.log **18:07:19.039/19.071** mouse down/up가 실제 좌표(862,712)에 전달됐다. 입력 broker는 UAC 구간 포함 누적 **454 attempts/454 delivered**다. 화면 표시 실패를 입력 차단으로 설명할 근거는 없다.
- 승인/카카오 실행 뒤 검은 화면 구간에도 **18:07:26 Host callbackFrames20·encodedFrames19·sentFrames19**가 있었고 캡처 재시작 수는44로 그대로다. 첫 재접속 Viewer9032는 18:07:23~28에도 같은 gen20의 새 seq를 present했다. 따라서 단순 socket/Hello 단절이나 '프레임이 전혀 안 와서'만 발생한 문제와 구분한다. **검은 픽셀 자체는 사용자 관찰 귀속**이며 원격 캡처 원본 픽셀을 이번 조사에서 저장하지 않았다.
- **18:07:33.418** Host epoch23(두 번째 접속)→**18:07:34.271 WGC 새 캡처/window-select(streamGen21)**. 이 뒤 일반 화면이 복구됐다는 사용자 관찰과 일치한다. NAS Viewer는 각각 PID9032/49236으로 connect/present가 확인된다. Host·Viewer 시계는 약1초대 차이가 있으므로 서로 다른 시각을 정밀 latency 합산에 쓰지 않았다.
- Host의 WGC 적용: 10-22 **19:16:54.323 requested=wgc active=wgc**. NAS APK에도 **19:16:53.501/56.035 desktop_backend_sync backend=wgc**가 있다(Codex 직접 대조). 이 호스트의 quarantine은0/storeempty다. 더 이른 설정 선택 원점과 PC 세션 상속은 아래10-26 보완 근거로 연결한다.

### 10-26 보완 — 설정 선택 원점과 PC 세션에 남은 이유

검증용 remote#yj8mrmrx의 `uac-prompt-check r1 / evidence` 제공 내용을 반영했다. 이 항목의15:24 원문/전체 기간 분포는 **검증용 판독 인용**이며, Codex가 이번 갱신에서 NAS를 다시 다운로드한 결과는 아니다.

- NAS SM-S948N-b7378c/apk.log: **10-22 15:24:17 tab=settings → 15:24:21.768 desktop_backend_request=dxgi → 15:24:23.217 desktop_backend_request=wgc**. SH 실기 중 폰 설정 탭에서 WGC가 선택됐다는 요청 순서가 확인됐다고 보고했다. 그 전10-02~10-22 오전 sync는 DXGI, 이후10-22/23/25 sync는 WGC였다고 보고했다.
- Host 적용은10-22 19:16:54, 이후10-23 09:45와10-25 23:01·23:02 재요청으로 이어진다. PC Viewer는 backend 요청을 보내지 않아 폰 선택이 Host의 requested 상태에 남고 PC 세션도 그 WGC를 이어 쓰는 경로다. 따라서 **설정 선택이 WGC 유지 경로를 노출했고, 그 경로의 UAC edge 갱신 누락이 증상을 일으키는 연결**로 정리한다. 설정 선택 자체를 사용자 잘못이나 SH 셰이더의 결함이라고 표현하지 않는다.
- 시간상 인접한15:13~15:16 잠금 화면 시험을 원인으로 묶지 않는다. 이 근거로 '최근 로그인 변경이 원인'이라고 단정할 이유는 더 줄었지만, 결함 도입 commit이나 패치 효과까지 입증한 것은 아니다.
- 검증용은 사용자에게 이미 **폰 설정에서 DXGI로 되돌린 뒤 재접속**하는 임시 우회를 안내했다고 보고했다. Codex는 사용자 기기 설정을 바꾸지 않았고, 우회 성공을 직접 검증하지 않았다. 이 임시 우회는 WGC 상태의 UAC 진입·복귀 처리 수정/완료를 대신하지 않는다.

추가 증거 수신만으로 paused 작업·제품 수정·배포를 재개하지 않는다. 답장 불필요 요청에 따라 중복 A2A ACK는 보내지 않았다.

### 정확한 코드상의 누락

게시 b4dfaed와 현재 main의 관련 제품 소스는 동일하다.

1. `host_stage_backend.cpp:251` 이후 입력 데스크톱 probe/복원은 **active != EffectiveRequested()인 백엔드 강등 상태에만** 들어간다. 현재 requested=WGC/active=WGC이므로 이 전환 추적 경로 자체를 지나지 않는다. 정상 Default→UAC→Default 양쪽 edge를 독립적으로 감지해 현재 WGC를 다시 만드는 owner가 없다.
2. `host_capture_session.cpp:599~605`의 WGC 재시작은 CreateForMonitor(restart-refresh)로 새 capture item을 만들고 뒤에서 새 frame pool/session을 만든다. 실제 두 재접속은 idle reattach/대상 선택으로 이 경로를 실행했다. UAC 전이 자체는 이 함수를 호출하는 조건이 아니다.
3. callback/드레인 watchdog은 프레임 진행 지표를 본다. 검은 화면이어도 callbacks·encoding·present가 진행한 이번 유형은 그 지표만으로 캡처 실패를 판정할 수 없다. 무조건 검은 픽셀을 버그로 간주하는 감시를 추가하는 것도 올바른 수정이 아니다(정상 검은 화면이 존재).
4. **잠금 화면 판정과 보안 데스크톱 전환은 별개다.** `lock_input_policy.hpp:34`는 lockKnown이면 WTS locked만 사용하고, known-unlocked UAC를 단지 non-Default라는 이유로 잠금 취급하지 않는다. locked/unknown 때의 fail-closed도 문자 입력 규칙이지 WGC 화면을 숨기는 규칙이 아니다. LockCred 광고/암호 입력 기능을 완화해 이 화면 문제가 해결된다고 볼 근거는 없다.

수정 방향은 **동일 제어 연결을 유지하며 UAC 진입·복귀 desktop edge를 감지→기존 WGC 캡처 자원 재구성→이전 캡처/미디어 세대 정리·새 IDR/출력 확인**이다. 기존 WGC/DXGI 요청·격리 정책, 잠금 암호 입력 차단, 입력 agent를 무력화하지 않는다. WTS lock/logon 이벤트만 추적하면 UAC에는 이벤트가 없을 수 있으므로 입력 데스크톱 전이 owner가 필요하다. transient/조회 실패를 무조건 '실제 잠금'으로 바꾸거나 전환 중 restart를 무한 반복하지 않는다. 전체 Host를 SYSTEM으로 바꾸거나 기존 paused 과제를 섞을 필요는 없다.

회귀 확인 acceptance: 같은 socket/control cookie로 정상 화면→UAC 표시→사용자 예 또는 아니오→정상 화면 복귀를 두 번 이상, 새 directory/Hello·수동 재접속0. WGC로 시작한 경우와 DXGI→WGC 전환 경우, 이미 WGC인 경우를 나눠 본다. 실제 잠금·로그인 전 경로와 기존 credential/input 경계는 별도 회귀로 유지한다. 현재는 원인 조사만 수행했고 이 수정/테스트/설치/게시를 완료했다고 주장하지 않는다.

### 보존한 조사 근거

`.claude/test-tmp/uac-investigate-2026-10-23/`의 `host_1800_1810.txt`, `viewer_1800_1810.txt`, `viewer_9032_present_summary.txt`, `secure_input_1805_1808.txt`, `apk_backend_10122_1916.txt`, `source_trace.txt`. 초기에 수행한 passive35초 관찰은 당시 Default만 관찰해 UAC 재현 증거가 아니며 별도로 보존했다. 초기 조사 중 개인정보/암호나 전체 영상 프레임을 수집하지 않았고 제품 파일/서비스/레지스트리·보안 설정 변경과 UAC 승인 자동화는 없었다. 미추적 조사서만 작성했으므로 제품 commit/push는 없다.

---

아래는 단계별 조사 기록이며, 중간 가설/답 대기는 위 결론으로 갱신됐다.

## 사용자 정정 반영 — 조사 대상 확정

사용자가 명확히 정정했다: **관리자 권한이 필요한 파일을 실행할 때의 UAC '예 / 아니오' 화면이 원격 뷰어에 다시 안 보임**. GNLink 실행 때 자체 승격 prompt가 필요한가의 문제가 아니다. 앞선 조사 초점은 잘못 좁혀졌으며 아래의 '어떤 트리거인지 답 대기'는 해소됐다. Host elevated=1은 이 UAC 화면 전달 성공의 근거가 될 수 없다. 기존에 해결했던 보안 데스크톱 캡처/입력 회귀로 추적한다.

### 정정 후 직접 확인

- 현재 Host 로그에는 `10-22 19:16:54.323 desktop-backend-applied seq=1 requested=wgc active=wgc desktopActive=1`이 있고 10-23 16:40까지 WGC reattach가 이어진다. startup은 `dxgiQuarantined=0`, Host store도 `reason=none store=empty`였다. 따라서 현재 WGC는 DXGI 격리가 강제한 것이라고 단정할 수 없고 클라이언트의 WGC 요청이 확인된다.
- ordinary Host의 capture는 user/관리자 token이다. SYSTEM prelogin worker는 별도로 서비스가 Winlogon에 생성하며 로그인 전 경로다. 현재 코드에서 일반 WGC→UAC를 별도 SYSTEM 캡처로 연결하는 근거를 아직 찾지 못했다. DXGI input-desktop attach 옵션도 prelogin 전용이다. **현재 WGC 경로와 기존 UAC 전달 경로의 결합이 우선 원인 후보**이며 실제 UAC 전이 프레임을 아직 재현하지 않아 확정하지 않는다.
- main host_stage_backend는 일반 DXGI 실패 후 WGC로 demote하고 Default가 안정되면 promotion하는 경로다. 이 기본 demote 코드는 5063cd9에도 있었다. 0.2.162의 변경은 quarantine EffectiveRequested 적용이며, 현재 이 PC는 quarantine이 없으므로 그 변경 하나를 원인으로 지목할 근거는 없다. user-token launch도 이전부터 존재한다. 도입/회귀 commit 미확정이다.
- Microsoft DuplicateOutput 문서는 secure desktop 접근에 LOCAL_SYSTEM 조건을 명시한다: https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgioutput1-duplicateoutput . 이 사실은 '관리자이므로 UAC 캡처도 된다'를 반박하는 권한 경계 근거이며, WGC 현장 실패의 직접 재현 증거는 아니다.
- 과거 docs/history #234(0.2.12/13)는 UAC가 보이되 클릭되지 않던 JOURNALPLAYBACK/DPI 문제였다. 이번 사용자는 **표시 자체 누락**을 보고하므로 이 과거 입력 수정만 확인하고 해결됐다고 할 수 없다. #400 등에는 실제 UAC 전이 캡처/복귀 실기가 존재하므로 원래 사용자 행동은 회귀 검증 대상으로 유지한다.
- 10-23 16:45:24 조회에서는 input desktop=Default, consent.exe 미관찰이었다. 현재 보안 화면이 떠 있는 상태를 직접 관측한 것은 아니다. 다음 실 재현에서는 UAC 전·중·후의 실제 foreground desktop/consent, 캡처 backend·프레임/IDR와 원격 표시를 같은 시각으로 대조해야 한다. 임의 설치 파일 실행·승인 클릭·보안 설정 완화는 하지 않았다.
- 버스 재발견: 현재 Codex remote#18s8ffjf. remote_claude remote#yj8mrmrx와 작업용1/2는 모두 onCall=false. 기존 일시 중지 상태를 해제하거나 옛 sessionId로 지시하지 않았고, Codex CLI 폴백도 실행하지 않았다. 제품 구현/검증을 위임할 경우에는 현재 상대 확인/사용자 범위를 따로 연결해야 한다.

상태: **UAC 원격 표시 회귀 제보 수용·읽기 전용 원인 조사**, 해결/재현/수정 완료 아님. 기존 async 트리거 질문에 사용자가 답한 것으로 처리하고 반복 질문하지 않는다.

2026-10-23 / 사용자 '관리자 확인누르는창이 갑자기 안뜨네…로그인때 뭐주려다가…확인'. 기존 전 작업 일시 중지는 유지하며 이번 새 문제 조사만 수행했다. 서비스/설치본/레지스트리/실 세션/앱은 변경·재시작하지 않았다.

## 직접 확인

- Windows 정책: EnableLUA=1, ConsentPromptBehaviorAdmin=5, PromptOnSecureDesktop=1. UAC를 끄거나 무조건 승인하도록 설정된 상태가 아니다.
- 실행 프로세스: GNLinkSecureInput 서비스 Running/Auto, 서비스 PID4804→GNLinkHost PID9864→GNLinkStream PID2456. Host/Stream은 10-22 16:34:10부터 실행 중, owner=shotan. 현재 consent.exe는 관찰되지 않았다(이 순간에 없다는 사실이며 과거 미생성 증명은 아님).
- 메모리에 올린 읽기 전용 Win32 token query: Host9864/Stream2456 **TokenIsElevated=1, TokenElevationType=2(Full)**. Explorer2204는 Elevated=0/Type=3(Limited). 따라서 일반 사용자 셸 전체가 승격됐거나 GNLink가 관리자 권한을 잃은 상태는 아니다.
- 설치 Host/Stream/InputService SHA256은 `.claude/test-tmp/codex-publish-0162/external_hashes.json`의 과거 직접 HTTPS 검증된 0.2.162와 세 개 모두 일치. 처음 후보 manifest 경로는 정리되어 존재하지 않아 참고값 없음 상태였고, 이를 mismatch로 판정하지 않고 보존된 외부 검증 JSON으로 다시 대조했다.
  - Host `67a4d7658039380f14be2956be7a950f9ae9de37b880e3a6248ce518fb970120`, embedded requireAdministrator.
  - Stream `c6c3c313b62813d1cd5fa1faf52b99903f22da43182e77e3485617137a457d55`, asInvoker(승격 Host 토큰 상속).
  - Service `ae849c5b3e34e4572fdf49854bf64808fa3b6c401b364606299df3dfcc739191`.
- host_app.log: 10-22 16:34:10 `prelogin access on: the service starts GNLink at logon; Run entry removed`. 현재 부모 프로세스 체인과 일치한다.
- main의 secure_input_service_main `launch_user_host`는 서비스가 검증된 사용자/linked-Full 토큰으로 Host를 생성하는 기존 경로다. prelogin_user_host_launch/console_probe/secure_input_broker는 5063cd9→b4dfaed 제품 차이 없음. 관련 launch 변경 이력은 10-07/08로 0.2.162 잠금 암호 입력 변경보다 앞선다. LK/DW 배포 때문에 이 권한 경로가 새로 생겼다고 할 근거는 없다.
- 보호된 prelogin_worker.log 읽기는 현재 비승격 조사 프로세스에서 AccessDenied였다. 권한/ACL을 바꾸거나 관리자 창을 새로 띄워 읽지 않았다. 다른 로그·프로세스 검사는 계속 가능했다.

## 판정과 남은 확인

**이 PC의 현재 GNLink Host/Stream 관리자 권한 누락 회귀는 관찰되지 않았다.** GNLink가 로그인 후 서비스에서 이미 승격돼 자동 실행되고, 그 자식 작업이 토큰을 상속하는 경우에는 매번 새 승격 확인 창이 필요하지 않을 수 있다. [Microsoft UAC 설명](https://learn.microsoft.com/en-us/windows/security/application-security/application-control/user-account-control/how-it-works).

단 사용자가 말한 창의 실제 트리거가 아직 확인되지 않았다. 'GNLink 실행/로그인', '업데이트/설치', '원격 PC에서 다른 관리자 작업' 중 어느 상황인지 async 질문을 제시한 상태다. 이 답 전에는 현상을 정상이라고 최종 종결하거나 원격 UAC 캡처 회귀를 배제하지 않는다. 다른 프로그램에서 승격 요청이 발생했는데 원격 화면에만 안 보이는 경우는 secure-desktop capture/입력 경계의 별도 문제다. 실제 재현 동작·PC·시각을 확보하면 그 구간 로그와 consent/desktop 전이를 대조한다.

제품 수정 없이 읽기 전용 조사 및 미추적 운영 기록만 수행했으므로 제품 commit/push는 없다. 기존 KA/LK-UX/SH 일시 중지 작업을 재개하거나 타 세션에 새 작업을 발송하지 않았다.
