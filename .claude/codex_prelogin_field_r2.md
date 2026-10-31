# prelogin-field r2 — 90289e8 후보 검토

`task prelogin-field / r2 / kind=review-result / reply_to=remote#izxg3q0o` · 2026-10-20

**NEEDS_CHANGES — D3의 가용성 결함 2건을 보수한 뒤 0.2.161 출하 후보로 연결한다. 현재 후보의 서명·게시는 보류한다.** D2 방어 수정의 소스/보완된 e2e 근거는 수용한다. 아래 보수는 기존 D3 범위이므로 사용자 추가 GO 없이 검증용이 작업용1의 같은 task에 연결한다.

대상 `90289e8332f92caf113ed4e26c88af7c857d03d2`, 제품 delta `7e06da1`, 선행 D1/D4/진단 `ac07b68`. 작업용·검증용 HEAD와 tracked clean, 보고 및 원본 요약을 대조했다. 기능 전체 재설계나 관계없는 전량 재검사는 요구하지 않는다.

## R2-1 · P1 — 로그 수집 정체가 worker/종료 경로를 무기한 막을 수 있음

`prelogin_worker_log.cpp:249`는 동기 anonymous pipe를 만들고 stdout/stderr를 그 write end로 연결한다. collector는 파일 WriteLine(:309), Flush(:301), 기존 stdout으로의 동기 forward(:314)를 **마친 뒤에야 다음 ReadFile로 돌아간다**. 파일 I/O나 forward가 정체되면 pipe를 더는 비울 수 없고, 64KiB를 넘는 출력에서 생산자 스레드가 막힌다. reader 오류 시에도 collector가 단순 반환(:287–290)하며 읽기 핸들을 남기므로 안전한 장애 전환이 없다.

특히 Drain은 fflush/cout.flush(:327–330)와 동기 marker WriteFile(:334)를 **완료한 뒤** deadline을 만든다(:335). 따라서 `Drain(2000)`은 함수 전체의2초 상한이 아니며, 이미 찬 pipe에서는 timeout 루프에 도달조차 못한다. 새 `PreloginStopServing` exit15/desktop 이탈 exit14 hook도 이 함수를 호출하므로, 로그 보존 때문에 기존의 “더 이상 송출하지 않고 즉시 종료” 경계가 무기한 지연될 수 있다.

이것은 source 흐름과 [Microsoft anonymous pipe 동작](https://learn.microsoft.com/en-us/windows/win32/ipc/anonymous-pipe-operations)의 buffer-full 시 WriteFile 대기 계약으로 확인한 결함이다. 해당 제품 후보의 정체 상황을 Codex가 직접 재현했다고 쓰지는 않는다. 정상6MB 출력·정상 FATAL drain 시험은 이 반례를 실행하지 않았다.

**수정 조건:** 로그 I/O/forward 장애가 worker의 정상 제어·입력·종료를 영구히 막지 않아야 한다. pipe reader와 느린 파일/forward를 bounded queue 등으로 분리하고 포화 시 drop/진단하는 방식, 또는 동등한 제한된 전송 방식 중 구현은 작업용이 선택한다. reader가 끝났을 때도 생산자가 무한 대기하지 않게 한다. deadline은 Drain 진입부터 적용하고, marker를 넣는 단계·stdio flush·disk flush를 기다리다 상한을 우회하지 않게 한다. 디스크가 멈춘 상황에서 로그 보존 성공과 종료 보장을 동시에 주장하지 말고, 상한 초과/실패를 보고하고 종료를 계속한다.

**필수 반례 시험:** 실제 sink에서 file/forward writer를 시험 경계로 멈추고 pipe 용량 이상을 출력한다. worker의 진행과 Drain/exit14·15 경로가 정한 상한 안에 끝나는지 확인한다. reader 중단/포화도 포함한다. old 구현 또는 상한/장애 전환을 제거한 대조는 해당 assertion에서 실패해야 한다. parent harness가 임의로 kill한 것을 정상 종료로 세지 않는다. 정상 상황의 FATAL 디스크 도달·회전·비밀 미기록 검사는 유지한다.

## R2-2 · P1 — Install 실패 시 부분 redirection·리소스가 남으며 예외는 main의 catch 밖

`Install(:253–271)`은 stdout을 `_dup2`로 바꾼 후 stderr의 DuplicateHandle/_open_osfhandle/_dup2 실패에서 즉시 return한다. 이때 stdout은 pipe에 연결돼 있지만 collector는 :276에서만 시작된다. 호출자는 설치 실패를 기록하고 worker를 계속 실행하므로 이후 stdout 누적이 pipe를 채우면 다시 정지한다. `CreateEvent(:250)`·freopen·SetStdHandle 결과도 실패 처리하지 않는다.

또한 collector의 `std::thread` 생성 실패는 rollback/catch가 없다. `install_prelogin_worker_log()`는 main 맨 앞에서 호출되고 제품의 startup 예외 catch보다 밖이므로, 로그 초기화 자원 부족이 원래 기능의 예외 진단/rc49 경로까지 우회할 수 있다. “Install 오류 시 아무것도 redirect되지 않음”이라는 헤더 계약과 다르다.

**수정 조건:** 설치를 transaction으로 만든다. 원래 CRT fd/표준 핸들을 보존하고 필요한 event/pipe/collector 준비를 확인한 뒤 commit하거나, 어떤 단계가 실패해도 양쪽 fd/표준 핸들·파일/chain/pipe/event/forward를 복원·정리한다. worker가 원래 표준 출력이 없는 경우도 정의한다. thread 생성 예외를 포함해 로그 초기화 실패가 worker를 terminate시키지 않게 한다. 실패한 sink로 stderr 진단을 다시 써서 막히지 않도록 한다. 실패 복구 때문에 unsafe 파일/TEMP로 로깅하거나 세션 간 핸들 상속을 켜는 것은 허용하지 않는다.

**필수 반례 시험:** stdout만 redirect된 시점의 stderr 설정 실패, event 생성 실패, collector 시작 실패를 제품 Install 경계에 주입한다. Install 실패 후 stdout/stderr에 pipe 용량보다 많이 써도 child가 계속 진행·정상 종료하고, 실패한 sink의 핸들/파일 잠금이 남지 않는지 확인한다. 정상 무표준핸들 시작 및 재설치/거부 동작도 해당 계약대로 확인한다. 원래 부분 설치 코드는 timeout/예외/복원 assertion으로 실패해야 한다.

## 수용한 부분 및 기존 근거

- **D2:** prelogin startup은 WGC IsSupported 및 WinRT D3D wrapper를 호출하지 않고 native D3D11/MF/COM은 유지한다. 일반 경로의 IsSupported HRESULT 예외는 exit6, 일반 wrapper 및 WGC 폴백/장치 transaction은 유지된다. prelogin의 desktop 이탈/캡처 불능 종료·다른 backend 거부도 유지된다. SYSTEM/Winlogon의 실제 rc49 원인을 확정한 것은 아니다.
- **e2e 보수:** 90289e8은 worker 환경에 trap/log root 등을 전달하는 시험 변경이다. pe3는 parent7e06da1+tracked dirty1 시점의 실행이며 그 변경을 고정한 commit과 연결해 인정한다. 4종 exit0, mutant exit1/3FAIL을 원본 summary에서 확인했다. mutant stdout에 trap이 없다는 요약과 worker 로그의 trap2줄을 혼동하지 않는다. pe2는 mutant도 통과한 무효 시험으로 계속 제외한다.
- **D3 신뢰·일반 동작:** worker 자체 설치, 서비스 bInheritHandles=FALSE/job 계약 유지, known ProgramData, HeldChain/no-delete-sharing, SY/BA private, 파일 유형/owner/DACL/reparse/hardlink1 검사, 검증한 handle의 회전, 정상 runtime2MiB+2MiB/4KiB 행 제한은 소스로 확인했다. 실제 SYSTEM 경로 권한은 실행 근거가 없다. 일반 오류가 **반환된** 경우의 drop/회전 중단 처리는 I/O가 **반환하지 않는** 경우의 안전성을 대신하지 않는다.
- **D1/D4:** TaskbarCreated 등록과 해당 메시지의 UIPI 허용·제한 재시도, online 상태의 punch 진행 줄 무시·offline/error 반영을 읽었다. 작업용 tray9/0와 mutant, 상태8/0는 관련 근거다. 실제 Explorer 로그온 타이밍/승격 Host의 방송 수신은 미확인이다.
- 검증용 독립20종의 exit코드/최종 요약·종료, fixed_server9·encoder_poll ship gate를 원본에서 읽었다. worker-log15/0, monitor_fallback173/0, fec136/0 등 정상 영향 검증은 유효하다. 사용 중이라 독립 SendInput/트레이를 재실행하지 않은 범위는 그대로 기록한다. 이번 반려를 이유로 지금 사용자 UI·입력·로그아웃을 조작하지 않는다.

## 보완 후 연결

검증용이 두 결함을 한 번에 작업용1 기존 task로 전달하고, 새 고정 후보에서 위 실패 주입 시험과 logger 정상/회전/즉시 종료·WinRT 진단 등 영향 범위를 독립 확인한다. 불필요한 사용자 확인이나 모든23종 반복 관문을 만들 필요는 없다. 기존 SendInput 근거는 출처/후보 매핑을 유지하고, 필요 시 사용자가 허용한 미사용 창에 영향 범위만 실행한다.

두 결함이 종결되면 0.2.161 버전 후보→dry-run 고정 패키지 검수/출하 훅 배제→허용된 서명·게시·외부 검증→사용자 업데이트/로그아웃 실기 한 묶음의 기존 흐름에 동의한다. 지금 버전/서명/게시 OK는 아니다. APK0.2.30 유지 및 실제 서비스/Winlogon·트레이 타이밍·원인 미확정 한계도 유지한다.

Codex 직접 수행: 제품/시험 diff·호출선·보고/원본 summary·API 문서·HEAD/clean/해시, `git diff --check 7b84c28 90289e8` exit0. 빌드·정체/자원 실패 주입·SendInput·서비스·설치·로그아웃을 직접 실행하지 않았다.

증거 SHA256:

- 작업용 보고 `9344515435482a973b9cc07b093818d5d92701790b25c68cd90531987528c406`
- 검증용 보고 `43da1b9fbc5100462a099e7099bf5f22d3e0cf55c63dfc3c179f7f116b4f37b3`
- pe3 summary `54f0678e1d4fbc4f7f13fd4fb18737dc99456aaa4f7ae09ffc5b668606cce8aa`
- 검증용 GNLinkStream `ab8e6371b7ef83efa279f4394cd9caeb2077733cbc78b2cf7585a06fa1e95cb1`(독립 summary와 일치)

다음 담당은 검증용 remote#izxg3q0o: 같은 task의 두 보수/실행 근거를 연결한다. 사용자 즉시 행동은 없다. 검토서 외 제품/공개 문서 변경이 없어 새 commit/push는 없다; 구현 기록은 기존 기능 단위에 취합한다.
