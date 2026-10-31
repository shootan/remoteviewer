# prelogin r20 0f9406e — Codex 통합 검토

`t-r639inm8 / r20 / kind=review-result / reply_to=remote#pvbntb3z` · 2026-10-19

**SOURCE OK (통합 delta) / EVIDENCE NEEDS_CHANGES (게시 전 보완).**

대상 `0f9406ec8690c165d15fa836f16899c0ec92d96f`, parents `37907a0` + `adce5d6`. 작업용 integ-prelogin·검증용 verify-eop 모두 같은 HEAD, tracked clean을 직접 확인했다. 이미 검토한 r18 구현 전체를 재감사한 판정이 아니라 main/secure-pipe와의 통합 차이, Q1/Q2, 충돌 해소와 보고 근거에 대한 판정이다. 기존 수용 한계 8건은 유지한다. 이 판정은 기능 완료·게시 승인·실기 성공이 아니다.

## 소스 판정

### RequestWindowList의 role → park 순서: 수용

`native_video_client_session.cpp:144–189`: 최초 `mu_` 검사는 먼저 풀고 HostRoleGate의 RequestList를 호출한다. Deferred/NotAtLogonScreen은 상태만 처리하고 반환하며, Send만 `parkMu_`를 잡아 park 확인과 window/monitor 요청 등록을 함께 한다. role의 post가 `mu_`를 잡는 동안 `parkMu_`를 보유하지 않아 이번 통합으로 새 중첩을 만들지 않았다. 이후 제어 루프의 NextAction과 park 판정도 `parkMu_` 아래에 남아 있다(:815–834).

첫 pong의 deferred 송신 등록(:886–898)은 parkMu_ 밖이지만, 해당 세션의 park는 HostHonorsStreamPause가 참이어야 시작되고(:341–357), 그 값은 window-list 응답의 thumbnail 지원으로 확정된다(:415–416, :924). 첫 pong 전에는 role gate가 list 요청을 보류하며 Reset은 해당 지원값을 지운다(:1619). 따라서 정상 연결의 첫 pong 처리에서 이미 park 중이라고 보는 반례는 현재 호출선에서 성립하지 않는다. 알려진 prelogin 호스트에 대한 최종 송신 allowlist도 남아 있다(:843–854).

이것은 소스 근거다. 현재 `client_host_role_gate_test`와 `client_park_state_test`는 각 부분을 검사하며, android_session_reboot_e2e는 재부팅 요청 경로다. 이 세 시험 통과만으로 새 role/park 조합을 실행했다고 주장할 수 없다.

### Q1: 수용

broker startup/빠른 경로가 모두 OpenVerifiedLocked → OpenVerifiedServicePipe로 연결된다. 빠른 경로는 waitMs=1, 실패 cooldown, raw 폴백 없음, 열린 핸들별 검증·SQOS·불확실한 write 비재전송을 유지한다. 시험 endpoint는 제품 호출선에서 활성화되지 않는다는 검증용 확인 및 현재 참조를 확인했다. 측정된 busy 대기는 작업용 14.56ms, 검증용 9.24ms로, 함수 전체의 무대기/1ms 보장은 아니다. 남아 있는 상위 주석의 “never waits / wait at all” 표현은 다음 관련 편집에서 실제 범위로 맞추면 된다(별도 승인 관문 아님).

### Q2와 listener 생성 3회 실패 정책: 수용

`secure_input_pipe_server.hpp:181–235`: 첫 생성 FIRST, 다음 listener 선확보, connect 실패 동일 핸들 재사용, 총8=처리7+listener1, 포화 거부 후 기존 연결 유지, 완료 연결 회수가 구현돼 있다. 실패한 다음-listener 생성 1·2회는 현재 listener를 보유하고 연결을 거부한다. 성공하면 연속 실패 카운터를 0으로 돌린다. 3회 연속 실패는 의도적인 제공 중단 후 registry STOP과 join으로 끝난다. 이 임계치는 **실제 생성 시도 횟수**이며, 시간 기반 3회 자동 재시도라는 뜻은 아니다. 다음 클라이언트가 없으면 listener를 보유하고 기다리는 것이 현재 정책이다.

정상 포화는 생성 실패 카운터를 증가시키지 않으며 전체 서비스를 중단하지 않는다. 실제 자원 오류가 반복될 때 명시적으로 서비스를 내리는 선택은 앞선 결정의 허용 범위다. 이번 범위에서 무한 재시도나 정책 재설계는 요구하지 않는다.

`secure_input_service_main.cpp:1375–1397`: 서버가 gRunning=true 상태에서 반환하면 원래 생성 오류(없으면 ERROR_PIPE_BUSY)를 선택하고 gRunning=false, agent 정지, prelogin/admin/unlock worker 정리·join 뒤 SERVICE_STOPPED와 오류를 보고한다. 외부 STOP으로 gRunning=false가 된 경우에는 정상 종료 오류 0을 유지한다. 오류 보존과 최종 보고 호출선은 소스로 확인했으나 **이 수명주기 전체의 실행 시험은 없다.**

그 밖에 remerge diff에서 CMake 양쪽 타깃 및 trusted_service_pipe 링크, Android 양쪽 문자열, host Serve 인증 인자와 두 시험 seam, 시작 인자 거부, 입력 park/filter 보존, fec desktopAttach 인자·WGC window probe 분기를 확인했다. 이번 통합 delta에서 새 P1 소스 결함은 찾지 못했다. `37907a0` 단독의 링크 가능 문제는 최종 후보에서 필요한 소스가 추가돼 있어 이번 후보 반려 사유로 삼지 않는다; ①만 독립 출하 가능한 후보라고 부르지는 않는다.

## 게시 전 보완할 실행 증거 (같은 기능 범위)

**E1 — listener 오류와 실제 서비스 종료 배선 시험이 필요하다. 정적 확인만으로 게시 gate를 닫지 않는다.**

- 제품 RunMultiClientPipeServer에 격리된 이름/실제 pipe를 사용하고 생성 오류만 주입한다. 1·2회 실패에서 기존 연결 입력 지속·listener 소유권 유지·신규 거부, 이후 성공과 카운터 reset, 다시 연속 3회 실패에서 종료·모든 연결 join/핸들 정리를 검사한다. 1실패→성공→2실패에서 조기 종료하지 않는 대조도 포함한다.
- 첫 생성 실패 및 연속 3회 실패 각각이 **실제 제품 서비스 종료 코드**를 거쳐 running 해제·정리·최종 STOPPED 1회·비0 원래 오류로 연결되는지 시험한다. 정상 외부 STOP은 오류 0인 대조를 둔다. 종료 배선을 끊거나 오류를 0으로 바꾼 부정 대조가 실패해야 한다.
- 기존 설치 GNLinkSecureInput을 조작할 필요는 없다. 같은 제품 수명주기를 호출하는 격리 test build에서 SCM 보고/프로세스 효과 경계만 관찰 가능하게 주입하는 방법을 허용한다. 보고용 helper만 따로 호출하거나 서비스 흐름을 시험에서 복제하는 것은 배선 증거가 아니다. 이 방식은 실 SCM 등록·권한·QueryServiceStatusEx 실측을 증명하지 않으므로 해당 실기는 별도 미확인으로 남긴다. 실제 SCM 환경에서 시험한다면 고유 서비스/pipe/저장 경로를 쓰며 기존 제품 변경은 하지 않는다.

**E2 — RequestWindowList의 통합 동작을 제품 ClientSessionController로 확인한다.**

UI·SendInput·desktop 없이 fake control host로 첫 pong 전 요청 보류→일반 host pong 뒤 list 도달, prelogin pong의 list/monitor wire 0, 일반 host의 지원 확인→park 중 list/monitor wire 0→resume 뒤 새 요청 도달을 검사한다. park 진입과 요청이 겹쳐도 요청이 park 중 wire로 나가지 않고 실행이 끝나야 한다. 별개 helper 두 개의 PASS 대신 실제 controller/제어 루프를 호출한다. 제품 UI 시험을 지금 재개하라는 지시는 아니다.

E1/E2는 지금 가능한 비-UI 보완으로 검증용이 기존 task에서 작업용1에 취합해 연결한다. 소스 변경이 생기면 새 commit과 해당 산출물을 고정하고 영향받는 시험을 독립 실행한다. 관계없는 89종 전량 재검사를 매번 요구하지 않는다.

사용자가 “나중에”로 미룬 제품 UI·입력·desktop 캡처, 실제 SCM·빠른 사용자 전환·UAC·재부팅은 계속 DEFERRED다. 자동화 가능한 핵심 사용자 행동을 생략한 일반 릴리스 완료는 불가하다. E1/E2를 충족해도 UI/실기 전부 통과 또는 UAC만 남았다고 바꾸지 않는다. 범위를 명확히 한 테스트 후보라는 판단과 일반 게시 완료 판단은 구분한다.

## 근거와 한계

- 직접 읽음: 작업용 보고 SHA256 `7be7838f6943b7f5cce18d9b5c5994bc68fc8a1e43a855e66e4d2ae9e22d26da`; 검증용 보고 `d45bad4d1d3d5798438018793b724875bf26d7f7c8577a05d05a28734523f785`.
- 검증용 원본 summary_vr20.txt `8332f820b8d1cafec45e3cf8c9a5d6bb465e0ff3be70c8b67e10dce1328841e9`: 89개 항목의 종료코드·최종 요약·slot 반환, 87 exit0/2 exit1을 확인했다. secure_input_pipe_server 18/0 및 client gate 로그를 직접 읽었다. wire_cap_defer·gpu_scaler는 기존 실패 비교 근거를 보고에서 인용하며, 작업용 credential_hops 996 간헐은 독립 PASS로 삭제하지 않는다.
- 현재 검증용 GNLinkStream `faaf3ff6c1432c2b5e8ec267b9c6823f088df8637c3aae0136b58b75ae7adda8`는 summary의 해시와 일치한다. GNLinkInputService `cd5c8ebf44a1cbfa7e3379b7eb4fee439391743b8e60b248c1f75ff15a43a1b9`, pipe-server 시험 exe `75c708be591154aedbc4fdc7b7f32ec25cc6c68693314fc6fd6f42e18608d2e3`도 직접 해시를 구했다. 작업용 산출물과 동일하다고 주장하지 않으며, 게시 candidate 동일성도 아직 없다.
- 작업용 변이 A/B 관련 실패 로그, D의 상한 실패 3개, E(+5ms)의 churn 실패 3회를 읽었다. E는 확대된 창의 반례 검출 증거이며 무확대 경쟁의 검출 확률 또는 경쟁 부재 증명은 아니다. 작업용의 pass1 mutant 잔류 결과는 무효이고 최종 재빌드/pass2와 검증용 독립 산출물만 유효 근거로 취급한다.
- Release 독립 빌드 rc0·Android 219/0는 보고 근거이며 Codex 직접 빌드/실행 결과가 아니다. Codex 직접 수행: 소스·remerge diff·보고/로그·HEAD/clean/해시 확인, `git diff --check 0537eb9 0f9406e` exit0. UI·서비스·빌드·재부팅 실행 없음.

SCM 보고의 기준은 [Microsoft SetServiceStatus 문서](https://learn.microsoft.com/en-us/windows/win32/api/winsvc/nf-winsvc-setservicestatus)의 cleanup 후 최종 STOPPED 1회·적절한 오류 보고다. 위 코드는 이 호출 순서를 갖지만 문서와 소스만으로 실제 종료 성공을 대신하지 않는다.

검토서 외 제품/공개 문서 수정 없음으로 commit/push 없음. 기능의 history/계획 상태·커밋 기록은 기존 작업 단위에 취합한다. 다음 담당은 검증용 remote#pvbntb3z: E1/E2 작업 연결 및 독립 결과 확보. 사용자 UI 보류는 유지한다.
