# 시험 Host 입력 주입 — 안전 Finding 및 판정 정정

2026-10-28 / Codex remote#tt7fcvky → 검증용 remote#7fsfy22z.

**공통 규칙을 CLAUDE.md 시험 절에 둔다. 실제 Host의 주입 OFF를 확인하기 전에는 연결하지 않는 fail-closed가 필수다. KA의 해당 실 Host e2e 승인은 재확인 대기로 전환한다. 순수 시험과 NP의 현 구성 단위 판정은 유지한다. KA PC③는 최종 EVIDENCE OK를 발급한 적이 없다.**

> **2차 추가 판정(2026-10-28): viewer_prelogin_login_e2e_test는 현재 형태의 모든 모드에서 실행 금지다.** 이를 사용한 PC 0.2.163 PS/LK 및 APK 0.2.32 android-select/prelogin 행동 EVIDENCE는 안전한 재확인 전 관문 사용 보류로 정정한다. 기존 관측 원문과 제품 SOURCE·서명/해시·게시 사실은 보존한다. 아래 **Finding-2**의 설계/영향 판정이 함께 적용된다.

> **3차 추가 판정(2026-10-28): 입력 router OFF와 별개인 Host wake/power 효과도 차단해야 한다.** 입력 OFF만 확인한 실 GNLinkStream 스트리밍 run을 무조작 격리 완료로 인용하지 않는다. ⑴ 로컬 전원 정책을 중앙 효과 경계에 연결하는 방향을 조건부 채택하며, 전원 유지 API까지 포함한다. 아래 **Finding-3** 참조. 9개 시험 보류·특정 prelogin 시험의 명시 실행 금지는 유지한다.

앞선 검토에서 실 Host의 effective 입력 주입 설정 확인이 누락됐다. 이 환경 격리 전제를 정정한다. 아래는 기능 관측을 지우거나 모든 결과를 일괄 폐기하는 판정이 아니라, **무엇을 계속 근거로 쓸 수 있는지와 안전한 재확인이 필요한 범위**를 명시한 것이다.

## 확인 사실과 미확인

- 소스에서 `HostArgs::enableInputInjection=true`, 기본 mode `background_message`를 확인했다. startup은 해당 설정·mode·compile policy로 inputRouter.injectionEnabled를 정한다. loopback·별도 cache·화면 밖 창·비관리자 실행은 이 스위치를 끄지 않는다.
- host_standby_admission_e2e의 기존 isolation_args는 directory-cache만 지정한다. **주입 OFF라는 실행 전제가 없다.** KA 검증용 4회가 실제 주입 이벤트를 발생시켰는지는 보존 자료만으로 미확인이라는 보고를 유지한다.
- KA PC③ r1~r4의 `injectionEnabled=1`, eventKind=6 합계 **2490**, 그 ControlInput timing 로그 안의 key-down/마우스/text 0은 **작업용2의 집계 보고**다. 별도 wake-display의 직접 API 호출은 이 집계에 포함되지 않는다. Codex가 합계를 전수 재집계한 것은 아니다. 이 수를 KA의 로그가 없는 4회에도 적용하거나 그 4회가 0이었다고 추정하지 않는다.
- Host의 input-timing에는 `osInjectionConfirmed=0`이 고정 출력된다. 이것은 **OS 효과를 확인하지 않았다는 것**이며 “OS에 아무 영향도 없었다”는 증거가 아니다. 입력 주입 경로가 열린 상태를 key-up뿐이었다는 이유로 무해하다고 판정하지 않는다.
- 정정 전 raw 로그/해시/실행 시각·PID·설정은 보존한다. staging 로그가 이미 삭제됐다는 공백도 그대로 기록하며 사후 생성하거나 무주입으로 추정하지 않는다. 추가 키 입력으로 사용자 상태를 복원/검사하지 않는다.

## CLAUDE.md에 넣을 공통 규칙 — 채택

정본은 CLAUDE.md의 **시험 격리/검증 독립성 절**에 둔다. AGENTS.md에 상세 규칙을 중복하지 않는다. 기존의 “OS 효과 경계 주입 허용”·“테스트 계정/root/process handle 격리”를 실제 launch 조건으로 구체화한다.

1. **실 입력 경로가 있는 Host를 띄우는 모든 자동 시험은 시험 전용 설정에서 입력 주입을 명시적으로 OFF**한다. GPU 여부·e2e/단위라는 이름과 무관하며, 구 게시본·compatibility·부정 대조·직접/중계·helper child도 각각 적용한다. 제품의 기본값을 바꾸는 작업은 아니다.
2. **각 Host의 이번 launch에서 나온 effective OFF 증거를 확인한 뒤에만** Viewer/클라이언트/session 연결 또는 입력 가능한 요청을 보낸다. source 설정 문자열이나 “보내려는 입력 없음”만으로 통과시키지 않는다. 새 PID·새 stdout/로그 범위·binary/config hash에 묶어 이전 실행의 disabled 줄을 재사용하지 않는다.
3. 명시된 OFF 설정과 effective OFF가 모두 성립해야 한다. `enabled`, 설정 오류/무시, 미지원 옵션, 확인 로그 없음·timeout·상충 상태는 **setup 실패/invalid로 종료**하고 연결을 진행하지 않는다. `check(false)`를 출력한 뒤 계속하는 것도 fail-closed가 아니다. 자기 launch handle의 자식만 회수하며 사용자 설치본/다른 세션 프로세스는 건드리지 않는다.
4. run 도중 `injectionEnabled=1` 등 모순이 보이면 해당 run을 중지/invalid로 기록한다. **사후 grep은 사전 연결 gate의 대체물이 아니다.** OFF 설정 누락/true·읽기 실패·OFF 확인 미도달의 부정 대조에서 실제 연결 0을 확인한다.
5. 설정을 거부하는 구/prelogin Host는 제품 보안을 풀어 시험하지 않는다. effective OFF를 보장하는 **명시된 product-equivalent test build/OS 효과 경계**를 쓰거나 그 case를 invalid/미검증으로 남긴다. compile-time policy로 막는 경우도 그 build 차분과 실제 block 상태를 기록한다.
6. **실 OS 입력 주입 자체를 시험해야 하는 경우**는 이미 승인된 격리 목적지/세션·효과 경계가 있는 범위에서만 수행한다. 현재 사용 중인 사용자 PC에 기본 ON으로 흘리는 방식은 허용하지 않는다. 안전한 독립 작업은 계속하며, 입력 주입을 켜야만 다른 기능을 검증할 수 있다고 가정하지 않는다.

startup의 `disabled (unsupported mode)` 문구만으로 승인/거부하지 않는다. **해당 source/binary에서 명시 지원하는 `none` → `Disabled`를 확인하고 정확한 argv와 이번 launch의 effective OFF를 결합한 경우는 명시 OFF로 인정한다**(아래 상충 판정). 임의 unknown 값의 fallback 비활성을 정상 OFF로 인정하지 않는다. 실제 구현이 출력하는 알려진 상태 또는 검증된 compile block을 사용한다. 현재 작업 중인 하네스 초안을 이것만으로 SOURCE 승인한 것은 아니며, **모든 launch의 hard barrier**가 고정 후보에 들어가야 한다.

## OFF로 계속 확인할 수 있는 행동

host_control_session은 injectionEnabled 조건과 별도로 input packet을 수신하고 ACK를 보낸다. 코드의 ACK 자체도 **수신 확인이지 OS injection 성공 확인이 아니다**. 따라서 실제 전송·Host 수신·release-before-session-end 순서, 인증/epoch/peer fence·영상/lease 동작은 OS 주입을 끈 채 제품 수신 경로로 확인할 수 있다.

이것을 실제 Windows 키 상태 변경/UAC·OS 입력 성공으로 보고하지 않는다. 반대로 임의의 항상 성공 stub으로 수신/정책 코드를 복제해 전체 제품 경로를 검증했다고 하지 않는다. 다른 OS 효과의 격리 조건도 기존 정본대로 유지한다.

## 기존 EVIDENCE의 처리

| 근거 | 현재 처리 |
|---|---|
| **KA 82679b2 shared** 실 GNLinkStream e2e 74/0×2 | **안전한 실행 전제 재확인 대기.** 기능 관측 원문은 보존하되 해당 e2e를 무주입 격리 완료·통합/출하 관문 통과의 근거로 사용하지 않는다. |
| **KA 534242b punch** 실 GNLinkStream e2e 78/0×2 | **동일하게 재확인 대기.** 627/629·1619/594 ms는 당시 관측값으로 보존한다. 안전한 하네스로 해당 Host 행동을 다시 연결한다. |
| table·entry·FakeHost/Echo lease·SendFn 기록기 punch·IPC·Book/계산 probe | 실제 GNLinkStream/OS 입력 경로를 쓰지 않는 **확인된 순수 범위는 유지**한다. 파일/삭제 등 각각의 기존 한계도 그대로다. |
| **NP e16ad0d 현 구성 단위** | **EVIDENCE OK 유지.** 고정 소스의 `np_early_bootstrap_e2e.js`는 Node dgram 가짜 Host이고, native probe는 directory_session_open 후 prepared socket을 닫는다. GNLinkStream/Viewer·authenticated control·입력 주입을 실행하지 않는다. Node suite/fence/punch_any도 이번 실 Host default-ON 결함 대상이 아니다. 이후 실제 Viewer 통합은 별도다. |
| **KA PC③ r1~r3/r4 시험 자료** | **기능 관측 자료로 보존, 격리 성공 주장은 정정.** KA PC③의 Codex 판정은 현재 SOURCE NEEDS_CHANGES이며 최종 독립 EVIDENCE OK가 없으므로 취소할 해당 최종 승인은 없다. 안전한 하네스의 새 후보/독립 실행 전에는 기존 84/123 등을 출하 관문 근거로 쓰지 않는다. |

**행동이 실제로 관측됐다는 사실**과 **승인된 격리 환경에서 완료 조건을 충족했다는 판정**은 분리한다. key-up이 기능 assertion과 직접 관계없어 보인다는 이유로 환경 위반을 면제하지 않는다. 한편 그 이유만으로 제품 SOURCE·해시·서명/게시 정합·무관 순수 시험까지 전부 거짓으로 만들지도 않는다. 과거 run을 OFF 실행으로 소급 바꾸지 않는다.

같은 결함이 전수 확인에서 APK/다른 실제 Host 시험에도 발견되면 그 run/행동의 격리 주장과 재확인 범위를 같은 목록에 추가한다. 현재 확인되지 않은 다른 run의 주입 유무·OS 영향은 단정하지 않는다.

## 필요한 재확인과 담당

- 수정 전 실 Host 자동 시험의 보류는 **GPU e2e만이 아니라 주입 OFF를 입증하지 못한 모든 해당 launch**에 적용한다. 순수 로직·읽기 전용 조사·안전한 다른 시험은 계속한다.
- 이미 검증용이 작업용1에 맡긴 전수 조사와 작업용2의 PC 하네스 보수를 **그 창구로 계속**한다. Codex가 별도 중복 task/worker 지시를 발송하지 않는다.
- launch 목록·과거 실제 설정/수집 가능한 이벤트 종류/수·확인 공백을 한 보고로 취합한다. **2490 집계는 해당 PC 자료 범위**로 유지한다.
- 검증용은 CLAUDE.md를 문서 전용 절차로 직접 반영하고, 고정된 하네스 diff·OFF/실패 gate의 부정 대조를 함께 검토 패킷으로 연결한다. 사용자에게 설정 변경 허가를 다시 받을 단계가 아니다.
- 안전한 후보에서 영향을 받은 **실 Host 행동만** 독립 확인한다. KA 공용 admission/activation/영상/즉시 재입장·old-host, PC의 실제 UI/owner/held-release 수신 순서 등이다. 무관 table/entry/NP suite를 이 Finding 때문에 전량 반복하지 않는다. 새 hash·설정·PID·OFF 확인→최초 연결 시각·결과를 보존한 뒤 보류된 부분을 재판정한다.

## 판정서 정정

혼동을 막기 위해 Codex 소유의 기존 `codex_keepalive_r2_shared_evidence.md`·`codex_ka_punch_thread_evidence.md` 상단에 **실 Host e2e 사용 보류**를 명시하고, NP 판정 유지 및 최신 PC SOURCE의 환경 주석도 연결한다. 원문/과거 판정은 지우지 않는다. 검증용의 실행 보고·공개 history/계획·Git MCP 기록은 검증용이 같은 Finding에 취합한다.

Codex의 직접 실행은 이전부터 FakeHost/Echo·송신 기록기·pipe·계산/페이지 handler probe로 제한돼 있었다. 이번 Finding에서도 읽기 전용 조사와 자기 판정서 정정만 했으며, 실제 Host/Viewer·키/clipboard·사용자 설치·운영/NAS·삭제·commit/push는 실행하지 않았다. 사용자에게 필요한 조치는 현재 없다. 실제 화면 영향은 미확인임을 사용자 창구가 최종 기록에 분명히 남긴다.

---

## Finding-2 — prelogin 시험의 Default 대역·Tab/포인터 (추가 판정)

검증용의 고위험 Finding-2와 자기 실행 보고를 수용한다. **기존 이 시험의 격리 확인 누락도 정정한다.** “key-up만 있었다”는 앞선 PC KA③ 집계를 이 다른 시험에 적용하지 않는다.

### ① 현재 시험 실행 금지 — 동의

**작업용1·작업용2·검증용·Codex 모두 현재 viewer_prelogin_login_e2e_test를 실행하지 않는다.** PS/LK·기본 prelogin·android-select·render·foreign-peer 등 모든 option 조합과 같은 경로를 타는 wrapper/복사본도 포함한다. REMOTE60_ALLOW_UI_TEST=1은 실행 스위치이며 사용 중인 Default 데스크톱에 입력 효과를 내도 된다는 권한 근거가 아니다.

정적 조사·코드 수정/빌드·OS 효과 없는 비해당 독립 시험은 계속한다. 현재 금지를 환경변수/분기 우회로 풀지 않는다. 사용자 지시 **“지금부터 이 시험 실행 금지”**를 우선하며, **이 답변은 실행 재개 지시가 아니다.** 재설계된 대체안의 고정 source/build 차분과 차단 경계는 검토할 수 있지만, 동일 시험의 재개는 사용자의 명시 재개 지시 전까지 보류한다. 이미 실행 중인 자기 시험이 있으면 검증용이 자기 launch handle/Job으로만 중지하며 사용자 제품/다른 세션은 종료하지 않는다. Codex가 별도 worker task를 중복 발송하지 않는다.

### 읽기 전용으로 확인한 경로

- start_host의 Service 경로는 기본적으로 **GNLINK_STREAM_TEST_LOGON_DESKTOP=Default**를 넣고 PreloginWorkerArgs(`--prelogin`)로 worker를 띄운다. 일부 PS 모드의 Winlogon alias도 실제 별도 desktop을 생성한 증거가 아니다. 격리된 파일/제한 토큰·shutdown privilege 제거와 입력 효과 차단은 다른 경계다.
- phone 경로는 **Tab down/up, pointer (10,10)**를 QueueInputEvent에 넣는다. 현재 `tabQueued && moveQueued` assertion은 **클라이언트 큐 적재 성공**이며 Host 정책 통과·sink 도달·OS 적용을 각각 증명하지 않는다. 일반 ACK 역시 앞 절대로 수신 확인이다.
- credential fixture의 target title 지정이 모든 navigation/pointer 주입 경로의 목적지를 자기 창으로 제한한다는 보장은 확인되지 않았다. prelogin worker는 사용자 설정 파일을 단순히 읽게 바꿔 해결할 대상도 아니다.
- foreign-peer의 “injection” 용어는 비정상 peer의 control datagram 투입을 뜻하는 부분도 있다. 해당 label만으로 OS 주입 발생을 단정하지 않는다. 하지만 정상 peer의 navigation/포인터와 positive control을 포함해 **모든 OS 효과 경로**를 차단/기록해야 한다.

검증용이 보고한 10-26 rel-0163 PS/LK/기본 prelogin 반복, 10-27 APK android-select vv_sel·v6_sel, 최종 PS render 등의 **실행 이력과 모드별 정확한 입력 발생 여부는 로그/명령에 연결해 보존**한다. 이는 현재 제공된 조사 목록이며 모든 과거 run을 망라했다고 주장하지 않는다. Tab/커서가 사용자에게 실제 반영됐는지는 미확인이다. 이를 알아보려고 현 시험을 다시 실행하거나 보상 입력을 주지 않는다.

### ② 재설계 — 입력 정책/dispatch 검증은 기록 sink를 기본으로

**권고안은 시험을 분리하고 OS 효과 경계에 record-only sink를 두는 것**이다. 실제 수신·인증/peer/epoch·prelogin 정책·dispatch 코드는 공유한다. 그 뒤의 실제 OS 호출/서비스 위임을 대체하고, 다음처럼 Host 쪽 관측을 단정한다.

- 허용된 A의 Tab down/up·pointer가 실제 decoder/정책을 지나 **해당 seq·session·role·좌표와 함께 sink에 정확히 도달**한다.
- 금지된 문자/text·비세션 B의 control은 실제 gate에서 거부되어 sink 호출 0이다. A의 허용 positive control은 대응 sink 호출이 정확히 발생한다.
- 클라이언트 Queue 성공이나 무조건 ACK만으로 “navigation/pointer가 통과”라고 쓰지 않는다. 단계별 증거를 남기고 결론은 **정책 허용·시험 sink까지 dispatch**로 한정한다. 실제 Windows 입력 성공이라고 하지 않는다.

이것은 수신 함수/정책을 복제하거나 항상 성공하는 가짜 구현으로 제품 전체를 검증했다는 방식이 아니다. 실제 OS 입력 성공은 이 시험이 증명하지 않는다고 명시한다. 필요하면 sink의 실패 응답도 주입해 오류 처리 경계를 확인한다.

**SendInput만 막는 seam은 충분하지 않다.** background_message·메시지 기반 주입·cursor/keyboard 대체 API·SYSTEM broker/service 위임 등 실제 dispatch가 나가는 모든 관련 경로가 record-only 경계 안에 있어야 한다. 사용자 Default로 빠지는 fallback은 허용하지 않는다. 기존 SAS/restart/credential의 OS seam도 실제 효과 없는 상태인지 함께 고정한다.

안전 확인은 예컨대 **OS effects=record-only, 실제 OS 효과 호출 0, 실제 broker 입력 위임 0**을 이번 process/build와 결합해 연결 전에 확인한다. logical dispatch를 시험하려고 내부 input 허용 상태를 유지하는 test build라면 이를 **OS 주입 ON과 혼동하지 않는 별도 상태**로 표현한다. 앞의 기본 ON 금지를 완화하는 예외가 아니라, 소스 검토로 물리 효과가 없음을 증명한 OS 경계 test build다. 출하물에는 해당 seam/키/flags가 없어야 한다.

차단 경계의 부정 대조도 안전하게 한다. 잘못된 sink 선택·차단 누락이 시험 실패로 잡히되, **부정 대조조차 실제 OS 입력을 호출하지 않는 최종 test guard** 안에서 수행한다. 실패를 증명하려고 사용자 desktop으로 실제 입력을 흘리지 않는다.

### capture/render와 실제 OS 입력은 별도 시험

| 목적 | 허용할 방법 / 증명 범위 |
|---|---|
| 입력 허용/거부·peer/role·routing | 실제 제품 처리 + 기록 sink. 실제 OS 효과 없음. |
| PS/LK·APK picker/renderer | navigation 입력 전주곡을 분리하고 OS 입력은 OFF/기록 전용. 소유 fixture 창은 화면 밖/비활성 방식으로 사용하고 실제 제품 UI→bridge→프레임을 확인한다. 동일 프로세스의 불필요한 navigation 시험을 같이 실행하지 않는다. |
| 별도 desktop/window station | 후보 수단일 뿐 자동 동등성 아님. 실제 객체·session·station/desktop·모든 child/broker 목적지를 확인하고 user Default fallback을 차단한다. 렌더/GPU가 안 되는 환경이면 그 항목은 invalid로 적는다. 사용자 desktop을 자동 SwitchDesktop해 시험하지 않는다. |
| 실제 OS 주입·Winlogon/UAC·진짜 GPU 동작 | 필요한 경우 승인된 별도 VM/전용 격리 환경 등에서 수행한다. 없으면 미검증으로 남기고 안전한 source/순수/기록 sink 작업은 계속한다. 보안 정책/운영 desktop 경계를 시험 편의로 약화하지 않는다. |

Microsoft 문서상 SendInput은 입력 stream에 이벤트를 넣는 API이며 특정 시험 HWND/desktop을 지정하는 인자가 없다. window station은 clipboard/desktop 등을 포함하고, 비대화형 station을 만드는 것만으로 대화형 입력 성공을 검증할 수는 없다. WinSta0에서도 현재 input desktop은 하나이고, Default는 로그인 사용자의 상호작용 desktop이다. 따라서 **이름 alias나 SetThreadDesktop만으로 사용자 입력 stream 격리·실제 GPU 동등성을 얻었다고 추론하지 않는다**. [SendInput](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput), [Window Stations](https://learn.microsoft.com/en-us/windows/win32/winstation/window-stations), [Desktops](https://learn.microsoft.com/en-us/windows/win32/winstation/desktops), [SetThreadDesktop](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setthreaddesktop).

### ③ 이전 판정 — 기능 관측 유지와 전체 EVIDENCE 유효를 구분

**“판정 자체는 전부 유효하고 무조작 위반만 기록”이라는 무조건적 처리는 수용하지 않는다.** 원문에서 관측된 기능을 지우지는 않지만, 안전한 자동 검증 완료라는 전제가 충족되지 않았고 입력/포커스/포인터 변화가 캡처·정지 화면·선택 동작에 무관했다고 입증되지 않았다.

- **PC 0.2.163 PS/LK 및 이 하네스를 공유한 행동 근거**: 기존 screenshot·frame/counter·로그는 과거 관측으로 보존한다. 해당 행동 EVIDENCE의 관문 사용은 **안전한 재확인 대기**로 전환한다. 특정 결과가 틀렸다고 단정하는 것도, 모두 영향 없었다고 면제하는 것도 하지 않는다. 별도 UA/다른 시험은 실행 식별자를 대조해 공유 run이면 같은 범위에 포함하고 무관 근거는 유지한다.
- **APK 0.2.32 android-select/prelogin fixture**: 같은 처리다. vv_sel/v6_sel과 같은 run을 쓰는 selector/UA-2/화면 근거를 식별하고 필요한 행동만 안전한 하네스에서 재확인한다. 독립적인 순수 계산/소스·서명·artifact hash 근거까지 일괄 폐기하지 않는다.
- **배포 사실**: PC 0.2.163/APK 0.2.32의 기존 SOURCE 판정, 게시 binary·manifest 서명/해시·외부 HTTP·main/dist 정합은 이번 시험 환경 결함과 별개로 유지한다. “배포된 사실”을 없애지는 않지만, **안전한 행동 검증까지 전체 완료됐다는 뜻으로 인용하지 않는다**. 이 발견만으로 제품 결함/자동 rollback 필요를 판정하거나 사용자 설치본을 재시작하지 않는다.
- **KA/NP/순수 근거**: 앞 절의 범위 판정을 유지한다. NP 가짜 UDP 대역을 실제 prelogin Host 시험과 섞어 보류하지 않는다.

Codex 소유의 rel-0163 EVIDENCE r2/FINAL_PACKET 및 APK0232 EVIDENCE/FINAL_PACKET 상단에 이 범위를 추가 정정한다. 날짜·원문·배포 기록은 보존한다. 검증용은 사용자에게 이미 알린 사실에 **실제 화면 영향 미확인·현재 금지·영향 판정/남은 재확인**을 같은 사고 기록으로 연결한다. 사용자에게 Tab/커서 재현이나 수동 복구를 요구하지 않는다.

현재 실행 금지와 안전한 대체 설계 검토를 기존 작업용1/2·검증용 창구에 한 번 취합해 전달한다. 재설계 후보는 소스·모드별 OS 효과 경계·동등성 한계를 고정해 Codex 전문 검토로 연결하며, 그 검토 자체로 사용자 실행 금지를 해제하지 않는다. 이번 Codex 작업은 소스/기존 판정·공식 API 문서의 읽기 전용 조사와 자기 판정서 정정뿐이며, 해당 시험이나 사용자 입력 API를 실행하지 않았다. 현재 사용자에게 추가 행동이나 재개 승인을 요청하지 않는다.

---

## 정책 문서 r1 감수 — DOC OK 대상은 보정 완료 diff

2026-10-28 / 검증용의 `.claude/injection_policy_r1.diff`(main 6fed2a3, CLAUDE.md·docs/history.md)에 대한 전문 검토.

**감수 완료본 `.claude/injection_policy_r1_reviewed.diff`는 DOC OK다.** 원 제출 r1을 그대로 승인한 것은 아니다. 다음을 보정한 완성 diff를 제공하며, 동일 내용으로 반영하면 두 파일만 Git MCP commit을 진행할 수 있다. 사용자 재확인·동일 내용의 별도 재승인 왕복은 불필요하고 **단독 push는 하지 않는다**. 이 문서 승인으로 시험 실행 금지는 풀리지 않는다.

### ① 문구 정합 — 보정 내용

- “이 PC 실제 입력으로 key-up을 보냈다”는 서술은 **입력 주입 경로 ON 확인, OS 실제 영향 미확인**으로 구분했다.
- 실행 중 ON/모순 발견 시에도 중지·invalid로 처리하는 규칙을 보완했다.
- 정본 CLAUDE.md 자체에 **현 viewer_prelogin_login_e2e_test 전 모드/wrapper 금지·사용자 명시 재개 전 실행 금지**를 넣었다. history의 “재설계 대기”만으로 재설계 후 자동 재개할 수 있다고 읽히지 않게 했다.
- “PC 최종 EVIDENCE 없음”은 **KA PC③**로 좁혔다. 별도로 이미 판정한 **PC 0.2.163 PS/LK 및 APK 0.2.32 android-select/prelogin의 관문 사용 보류**도 history에 연결했다. Source·서명/hash·게시 사실 유지와 구분했다.
- 검증용 4회 미확인 범위는 host_standby_admission_e2e로 명확히 했다. 다른 Host 시험은 전수 조사 중으로 남겼고, 근거 없이 실행 횟수/OS 효과를 합산하지 않았다.

### ② 규칙 5·6과 재설계 기준의 관계

**5·6은 필수 기준이지만 그것만으로 충분한 재설계/재개 승인은 아니다.** Finding-2의 제품 수신·정책·dispatch 공유, record-only sink의 물리 효과/broker/Default fallback 차단, Host 측 sink 도달·거부 확인, desktop/window station 동등성 한계를 함께 적용한다. Queue/ACK만으로 정책 통과·OS 입력 성공을 주장하지 않는다. 이 요지도 감수본에 반영했다.

현재 금지된 시험은 수정·빌드·정적 검토를 계속할 수 있지만 **명시 재개 지시 전 실행하지 않는다**. 금지되지 않은 안전한 독립 작업은 계속한다.

### 반영할 파일과 검증

- **현재 제출 r1 작업 트리에 덧붙일 보정 patch**: `.claude/injection_policy_r1_review_fixes.diff`.
- **main 6fed2a3 기준 최종 감수 전체 diff**: `.claude/injection_policy_r1_reviewed.diff`.
- 감수 전체 diff SHA-256: **d183038f4d8175696cf9f5801a6e7757769a4ec4fa546c817dba8e959228732d**.
- 반영 후 텍스트를 UTF-8/LF로 개행 정규화한 SHA-256:
  - CLAUDE.md: **c09a93734ed4cc29f170bc3be3e7d46e9043fd07fabbe6932dc66a04f83b13c0**
  - docs/history.md: **afdea5ab42f159e905dc910baa833e84cfa61cde90a7d6d331563562854bbd83**

Codex가 실제 작업 diff와 제출 r1의 일치, 보정 patch의 **git apply --check PASS**, 최종 감수 전체 문구를 확인했다. 검증용 작업 트리의 두 파일은 수정하지 않았다. 검증용은 보정 반영 후 최종 diff/대상 두 파일을 대조해 `docs(test): require verified input isolation for harnesses` 같은 Conventional Commit으로 기록한다. 의미가 다른 추가 변경이 생기면 그 차이만 다시 검토한다.

검증은 문서/diff 정적 확인뿐이며 금지된 시험·제품 build·OS 입력은 실행하지 않았다. Codex commit/push 없음. 사용자 현재 조치 없음.

---

## OFF 표현 상충 판단 — mode=none 채택, config의 환경 초기화 구분

2026-10-28 / 검증용의 mode=none 대 config 결정 요청.

**①을 채택한다. 해당 고정 source/binary에서 `--input-injection-mode none`은 의도된 명시 OFF로 인정한다.** 임의의 unsupported 값까지 허용하는 결정은 아니다. 기존의 연결 전 effective OFF gate·기타 OS 효과 격리와 **금지된 viewer_prelogin_login_e2e_test의 실행 금지**는 그대로다. 이 결정은 하네스 전체 SOURCE 승인이나 금지 해제가 아니다.

### none은 parser가 명시 지원하는 Disabled 값

`host_input_inject.cpp::parse_input_injection_mode`는 none/disabled/off 등을 **명시적으로 InputInjectionMode::Disabled**로 반환한다. background_message만 BackgroundMessage로 매핑하며 unknown도 안전 쪽 Disabled로 fallback한다. 이 둘을 시험 설정 승인에서 구분한다. **표준화할 인자는 정확히 `none`**이고 unknown fallback을 이용하지 않는다.

현 ka-r3에서 읽은 source는 **0fb4676b618718c4cac02bfe0840fb3b122352b1**이며, 해당 parser 파일 blob **40f21afff0dfb04db546c6d2800e1535f6c0026b**는 공개 0.2.163의 build source fae0e1fc…와도 같다.

startup의 실제 값은 다음 조건으로 정해진다.

```
injectionEnabled = args.enableInputInjection
                && (parsed mode == BackgroundMessage)
                && !compile-time block
```

따라서 none/Disabled는 Args의 enableInputInjection 기본값이 true여도 **effective injectionEnabled=false**다. startup이 이 경우를 `disabled (unsupported mode) mode=none`이라고 출력하는 것은 로그 분류가 포괄적이어서 생기는 혼동이다. **그 문구만 보고 none을 미지원 설정으로 취급했던 해석은 정정한다.** parser에 명시된 none은 정상 지원되는 OFF 경로다. 로그 문구 개선은 할 수 있지만 안전한 하네스를 만드는 선행 필수 제품 변경은 아니다.

승인할 launch는 정확한 argv=none·대상 source/binary hash·이번 PID/로그에서 알려진 disabled 상태와 **완전한 mode 값 none**을 함께 확인해야 한다. disabled 접두만 보거나 `mode=none...` 부분 일치로 임의 값을 받아들이지 않는다. enabled/상충/미확인·버전별 의미 불일치는 계속 setup 실패로 종료한다.

### 실제 차단 범위 — 일반 입력 router와 모든 OS 효과는 다름

host_startup_config의 위 gate와 host_control_session의 **ControlInputEvent / physical input / ControlInputText** 경로를 대조했다. 이들의 실제 주입은 inputRouter.injectionEnabled 조건 뒤에 있고, 그 안의 local/broker/fallback도 함께 차단된다. 이 범위에서 none은 `enableInputInjection=false`와 같은 효과다. ACK는 별도 수신 확인으로 남는다.

그러나 **SAS 위임·credential·clipboard·재부팅 등 별도 OS 기능 전체를 끄는 스위치라고 주장하지 않는다**. 예컨대 prelogin SAS는 별도 DelegateSasToService 경로다. 그러한 명령을 사용하는 시험은 기존의 별도 효과 sink/격리가 필요하다. none 하나를 근거로 금지된 prelogin 시험을 재개하거나 사용 중인 사용자 Default에 효과를 내지 않는다. 생산 worker의 고정 PreloginWorkerArgs/보안 계약도 시험 편의로 변경하지 않는다.

### ②의 h264 문제 — codec 문자열보다 runtime env 초기화가 원인 경계

`host_args.cpp`의 config 적용은 `json_profile::apply_runtime_env_overrides_from_json`을 호출한다. 이 함수는 **먼저 clear_runtime_env_overrides**를 수행하며 상속된 `REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE`도 지운다. 이후 JSON의 `forceEncodedExperiment=true`일 때 다시 설정한다. host_startup_config는 h264이면서 compile/env experiment 허용이 없으면 unsupported codec/exit11을 낸다.

Codex가 **실제 host_args.cpp만 링크한 순수 parser probe**를 실행했다. 각 child에서 FORCE=1을 먼저 설정하고 인자를 파싱했으며 Host startup·socket·UI·입력 API는 실행하지 않았다.

| 입력 | codec/transport | Args injection | FORCE 파싱 후 |
|---|---|---|---|
| config 없음, mode=none | h264/udp 유지 | true, mode none | **1 유지** |
| config에 enableInputInjection=false만 | h264/udp 유지 | false | **unset** |
| config에 codec/transport도 추가 | h264/udp 유지 | false | **unset** |
| 위 config + forceEncodedExperiment=true | h264/udp 유지 | false | **1** |

즉 **codec/transport만 config에 더하는 것으로 이 허용 flag 소실이 해결되지는 않는다**. 같은 오류를 조사할 때 full stderr의 experiment 허용 안내와 build 조건을 함께 기록한다. profile 경로를 쓰려면 해당 시험에 필요한 runtime 설정도 빠짐없이 보존해야 한다. 최소 예시는 다음과 같으며 다른 의도된 runtime env의 초기화 영향도 확인한다.

```json
{"enableInputInjection":false,"transport":"udp","codec":"h264","forceEncodedExperiment":true}
```

②는 이처럼 완전한 profile을 의도할 때 사용할 수 있다. 이번 안전 보수에 그것을 필수로 강제하지는 않는다. 명시 지원되는 none은 기존 runtime 환경을 건드리지 않아 입력 효과만 차단하는 하네스에 적합하다. prelogin이 config를 거부하는 기존 보안 계약도 유지한다.

probe: **`.claude/codex_test_injection_review/args_probe.cpp`**, exe SHA-256 **50d31d867ccab5fafe17410b44bb0d1ff3fa7ab1c4346bafef4ffd740f0f669a**. JSON 세 개와 산출물은 같은 내부 검토 폴더에 보존했다. 이는 순수 parser 직접 실행이며 GNLinkStream이나 금지된 하네스의 실행이 아니다.

### CLAUDE.md 문구 명확화 — 감수 patch DOC OK

r1 정책은 main **c37e49b3283447b3fb6be2b99dc8918ce376f71f**의 두 문서 commit으로 반영된 것을 읽기 전용 확인했다. 그 위에 지원된 none을 명시하는 **`.claude/injection_mode_policy_reviewed.diff`는 DOC OK**다. 기존 정책/금지/영향 판정은 유지하고 문구만 명확히 한다. 대상은 CLAUDE.md·docs/history.md 두 파일뿐이다.

- patch SHA-256 **820c99cf7b2bb94c9c384833efadf8adf963efa7c2fdd077c914d976620a1c90**.
- 반영 후 UTF-8/LF 정규화 SHA-256:
  - CLAUDE.md **a9419b34fe612888f836c2265405b18ed108158b405e78a7be3f52fc31afcefd**
  - docs/history.md **248bc1fd2d7a14ba61aaedc9858c7f9d19881406c4348d7b27ef693e5a82cf04**

검토 patch의 git apply --check PASS 및 작업 트리 무수정을 확인했다. 동일 문구면 검증용이 두 파일만 Git MCP commit할 수 있고, 추가 승인 왕복·단독 push는 없다. 하네스는 그 고정 diff와 실제 연결 전 gate/부정 대조를 별도로 확인한다. 과거 보류 EVIDENCE를 설정 선택 결정만으로 복구하지 않는다. 사용자 현재 조치 없음.

### 추가 실측 수용 — 공통 기본은 none 유지, 완전한 profile의 config OFF도 허용

검증용이 전달한 작업용2의 **config에 enableInputInjection=false + forceEncodedExperiment=true를 넣은 단독 Host 기동, 정형 disabled 확인·codec 오류 없음·Viewer 연결 0**을 수용한다. 이는 위 순수 parser 결과 및 startup gate의 설명과 일치하며 **②도 실제 사용 가능한 명시 OFF 경로**임을 보강한다. Codex가 Host를 새로 기동한 결과는 아니다.

**전체 하네스의 공통 기본을 두 필드짜리 config로 일괄 전환하지는 않는다. 입력 효과만 끄려는 하네스의 기본은 source가 지원하는 `--input-injection-mode none` + 해당 launch의 정확한 effective OFF 확인으로 유지한다.** 이미 profile을 의도적으로 사용하는 하네스는 **enableInputInjection=false 및 필요한 runtime 설정을 갖춘 config**를 사용할 수 있다. 그 하네스에 필요한 설정이 FORCE 하나뿐임을 확인했다면 보고된 두 필드 config도 충분하다.

이유는 config가 FORCE만이 아니라 clear_runtime_env_overrides 목록 전체를 초기화하기 때문이다. 코드/transport 오류가 사라졌다는 사실만으로 다른 guard·pacing·capture 등 의도된 runtime 값까지 보존됐다고 할 수 없다. 두 필드를 안전 정책의 전역 표준으로 삼아 무관한 기능 시험의 입력 계약을 바꾸지 않는다. 반대로 전용 profile의 사용을 막거나 정상 config OFF를 none으로 불필요하게 다시 바꾸라고 요구하지도 않는다.

따라서 **정책 표준은 “명시 지원된 OFF 의도 + 고정 source/binary/config/argv + 이번 launch의 effective OFF 확인 + 실패 시 연결 0”**이다. 허용되는 두 설정 경로는 이미 감수한 injection_mode_policy_reviewed.diff와 일치한다. 이번 추가 실측 때문에 별도 CLAUDE/history 재수정·승인 왕복은 필요 없다. 필요하면 실행 근거만 같은 사고/하네스 보고에 연결한다.

정형 `enableInputInjection=false` 로그는 config 경로의 확인값, 정확한 `mode=none`과 결합한 기존 disabled 로그는 none 경로의 확인값으로 각각 처리한다. 로그 문구의 모양만으로 효과 동등성이 달라지는 것은 아니다. 단독 기동은 설정/codec 기동 근거이며 해당 e2e의 안전한 전체 실행이나 과거 EVIDENCE 복구 근거는 아니다. 금지된 prelogin 하네스의 실행 금지는 계속 유지한다.

### 진행 보고 추가 판정 — 표준 재대기 불필요, real-input 4개는 두 층으로 분리

검증용의 add-to-pending 보고에 답한다. **공통 기본은 이미 ①, 지원된 `--input-injection-mode none` + 정확한 이번 launch의 effective OFF로 결정됐다.** seq14893·14897·14899 및 위 결론을 따른다. 의도된 완전한 profile의 config OFF도 허용하지만, 정형 로그를 얻으려고 모든 하네스를 config로 바꾸도록 강제하지 않는다. 표준 결정을 다시 기다릴 필요는 없으며, 최종 실행 여부는 **고정된 안전 하네스의 검토/선행조건**에 따른다. 슬롯이 풀렸다는 이유만으로 미검토본을 자동 실행하지 않는다.

#### real-input 4개 — 기본 거부 유지, 자동 회귀는 record-sink 부분을 분리

control_resume·viewer_mouse_xbutton·viewer_paste·viewer_release_all의 **실 OS 주입 경로는 기본 REFUSED 유지**가 맞다. **REMOTE60_ALLOW_REAL_INPUT_TEST=1은 기술적인 opt-in일 뿐 사용자의 승인·목적지 격리 증거가 아니다.** 에이전트가 이 값을 스스로 켜서 현재 사용자 Default에서 실행하면 안 된다. 거부는 부작용/창·clipboard·hook·Host 연결 전에 일어나야 하며 exit3/REFUSED를 PASS로 세지 않는다.

자동 검증은 다음과 같이 분리한다. **네 시험을 단순 receipt-only로 바꾸고 원래의 OS 성공 단정을 그대로 남기지는 않는다.**

| 시험 | 안전한 자동 검증의 대상 | 별도로 남는 실제 OS 항목 |
|---|---|---|
| control_resume | 실제 제품 resume·큐/seq·action 폐기/유지·Host 정책 뒤 sink의 release 수신/순서 | OS에서 실제 눌린 키 상태의 복구 |
| xbutton | capability 협상·Viewer 이벤트 처리·wire·Host 정책·sink의 X1/X2 down/up | 실제 OS/대상 앱의 마우스 버튼 처리 |
| paste | 실제 paste/baseline/permission/취소 정책과 transport·sink 내용/순서. clipboard 효과도 fixture backend로 격리 | 실제 OS clipboard/앱 붙여넣기 |
| release_all | 실제 held 집합·up 생성·release 한정 drain/확인·종료 전 Host sink 도달 | 실제 OS 키/버튼 해제 상태 |

policy·수신·dispatch 코드는 공유하고 OS 효과만 차단한다. input OFF 때문에 positive capability/dispatch 자체가 꺼져 해당 허용 경로를 검사할 수 없다면, **논리 허용과 실제 OS 효과가 분리된 record-only test build**로 한다. 항상 성공하는 수신 stub이나 단순 Queue/ACK를 대신 넣지 않는다. Host 쪽 실제 허용/거부·sink의 seq/session/payload 관측과 부정 대조를 남긴다. 사용자 clipboard/포커스/입력 stream을 사용하지 않는다.

기존 real-OS 검사를 지울 필요는 없다. 그 부분은 **명시 승인된 VM/전용 격리 환경·목적지**에서만 별도로 검증하고, 현재 그 환경/승인이 없으면 REFUSED/미검증으로 남긴다. 안전한 자동화 부분은 새 고정 Source의 효과 차단 경계를 검토한 뒤 비해당 시험으로 이어갈 수 있다. 금지된 viewer_prelogin_login_e2e_test를 이름/flag만 바꿔 우회 재개하는 것은 이 허용에 포함되지 않는다.

현재 helper 초안의 “person starting it says so” 설명도 **opt-in이 권한을 부여한다는 의미가 아님**으로 맞춘다. 네 개의 opt-in guard 추가만으로 이 시험의 안전성 SOURCE/EVIDENCE를 승인한 것은 아니다.

#### fatal_exception — case별 판단, 전체 gate 예외 승인 아님

OFF 로그보다 먼저 의도적으로 종료되는 **진짜 무클라이언트 startup fault case**는, 주입/네트워크 입력 경로에 도달하기 전의 실패 지점·기대 exit/진단·연결/입력 요청0을 고정해 별도 평가할 수 있다. 없는 OFF 로그를 PASS했다고 만들지 않고 “설정값 OFF 미확인, startup-fault 분리 근거”로 적는다.

그러나 읽은 host_fatal_exception_test.cpp에는 **run_with_client가 실제 Hello를 sendto**하고 probe-trap/wrapper-trap/wgc-throw/wgc-false/normal 등의 case를 실행하는 경로가 있다. 이들은 “client가 전혀 없는 startup 죽음”이 아니다. **Hello 전 연결 gate**를 적용해야 한다. 현재 기동 순서 때문에 OFF 확인 전에 Hello가 필요하다면, 안전한 test build의 초기 effective-OFF 증명/효과 강제 지점을 마련하거나 case를 분리한다. bare Hello라는 이유로 전체 표준을 생략하지 않는다.

또한 현재 사본은 exe 디렉터리에서 **`fatal_prelogin_logroot`를 만들어 시작/종료에 remove_all**한다. exe가 C: build에 있으면 저장소 밖 재귀 삭제 경로다. 실제 과거 삭제 실행 여부는 이 읽기 조사로 확정하지 않는다. 기존 T2와 같이 검증된 저장소 안 고유/소유 root와 삭제 대상의 절대 경계를 확보하기 전에는 그 사본을 실행하지 않는다. 입력 OFF 설정이 파일/프로세스 격리 규칙을 대신하지 않는다. 바깥 경로 정리/권한 질문 없이 원본과 후보를 보존한다.

#### 다음 담당과 고정점

8개 수정·4개 기본 거부·9개 보류는 진행 보고로 수용한다. 읽은 working helper는 config 방식 등 미커밋 변경을 포함하므로, 과거 메시지의 none 방식 설명만으로 현재 전체 하네스를 승인하지 않는다. **기존 작업용1의 전수 보수 정본에서 최종 shared guard·각 launch/case·real-input 분리·fatal 경로를 고정해 검증용이 한 패킷으로 연결**한다. Codex가 새 중복 task를 만들지 않는다.

정책 mode 명확화가 main **7f96e43d584686df2f86b069218bbaad52a29e6e**에 CLAUDE.md·history 두 파일로 반영된 것도 읽기 전용 확인했다. 이번 판단 때문에 표준의 새 승인 왕복/무관 전량 재시험을 만들지 않는다. 보류 EVIDENCE는 안전한 해당 행동을 독립 확인한 뒤 재판정하며, 금지된 prelogin 시험은 명시 재개 전 계속 금지다. 이번 조사는 소스/기존 기록 읽기와 자기 판정서 추가뿐이며 Host/Viewer·real-input 4개·fatal·금지 시험을 실행하지 않았다. 사용자 현재 조치 없음.

---

## Finding-3 — stream wake와 전원 유지의 독립 OS 효과

**⑴을 설계 방향으로 채택하되, 단순 one-shot wake bool보다 범위를 정확히 한다.** 기본 ON을 유지하는 **로컬 시작 전용·불변 전원 정책**을 실제 효과 직전 중앙 경계에 연결한다. 자동 시험에서는 해당 효과를 OFF 또는 검토된 기록 경계로 막는다. **⑵도 정당한 product-equivalent 대안**이며 구 바이너리/고정 prelogin 계약에서는 필요할 수 있다. 바이너리가 달라진다는 이유만으로 배제하지 않는다.

9개 보류는 유지한다. 이 답은 구현 SOURCE 승인이나 시험 재개 지시가 아니며, 금지된 prelogin 하네스는 사용자 명시 재개 전 계속 금지다.

### 확인한 원인과 추가 경계

작업 중 파일에는 이미 OS adapter 변경이 있으므로, 원인은 **고정 0fb4676b618718c4cac02bfe0840fb3b122352b1**의 git show로 확인했다.

- host_log.hpp의 wake 함수는 **SendInput 상대 mouse +1/-1 두 이벤트**와 **HWND_BROADCAST/SC_MONITORPOWER(-1)**를 호출한다. inputRouter와 연결돼 있지 않다. +1/-1이라는 이유로 무조작/정확한 원상복귀라고 하지 않는다.
- HostPowerKeepalive는 false→true edge의 wake 요청에 이를 실행하며, 해당 backend/stream-active/startup capture 호출들이 true를 요청한다. 반복 true만으로는 재호출하지 않는 현재 edge 조건도 확인했다.
- **Apply(true)는 ES_DISPLAY_REQUIRED를 포함**한다. 따라서 SendInput/broadcast 함수만 막아도 화면 전원 효과가 없어지는 것은 아니다. [SetThreadExecutionState 공식 설명](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-setthreadexecutionstate)도 display/system idle 유지 효과를 구분한다.
- **생성자는 Apply(false)로 ES_SYSTEM_REQUIRED를 요청하고 소멸자도 execution state를 해제**한다. main에서는 이 객체를 **parse_args보다 먼저 생성**한다. 따라서 “아직 영상/클라이언트가 없었다”만으로 전원 효과 0을 주장할 수 없다. startup-fault 예외도 이 경계를 포함해 다시 판단한다.

과거 run에서 조건에 맞는 경로는 API 호출을 시도했을 수 있으나 반환값을 버리는 코드이므로 **실제 커서/모니터 반영·전체 호출 횟수는 독립 확인되지 않았다**. 특히 기존 ControlInput key-up 2490/“mouse 0”은 이 wake 경로의 집계가 아니다. 해당 수를 전체 OS 입력 효과의 상한으로 쓰지 않는다. 실제 반영을 확인하려고 테스트를 다시 실행하거나 반대 방향 mouse 입력/전원 메시지를 보내지 않는다.

### ⑴ 구현 조건 — 중앙 전원 정책, 초기화 전 적용

1. 예를 들어 **hostPowerManagementEnabled**처럼 범위가 분명한 로컬 startup 정책을 둔다(최종 명칭은 구현 정본에 고정). 기본 ON의 기존 product 동작을 보존한다. CLI/config에서만 설정하며 directory/control/viewer·runtime tune·원격 stream 재시작으로 바꿀 수 없게 한다.
2. 시험의 OFF는 **HostPowerKeepalive의 synthetic wake·monitor power broadcast·display/system execution-state 요청**을 모두 차단한다. wakeDisplayOnStream만 따로 도입한다면 그것은 one-shot 부분의 설정일 뿐이며, 남은 display/system keepalive를 별도로 끄거나 기록 sink로 격리해야 전원 효과 0이라고 할 수 있다.
3. **parse/정책 검증·객체 연결은 최초 효과 이전**이어야 한다. 현재 생성자의 Apply(false)가 먼저 실행된 뒤 setter로 OFF하는 방식은 불충분하다. 정책을 받아 생성하거나, 초기 객체는 무효과로 두고 명시 초기화한다. 처음부터 OFF면 소멸자에서도 불필요한 실제 power API를 호출하지 않는다. ON으로 등록한 요구의 정상 해제는 자기 수명 안에서 보존한다.
4. 중앙 객체의 immutable 정책을 호출 직전에 검사한다. 각 call site의 true는 “요청”일 뿐 로컬 OFF를 뒤집지 못한다. raw wake 함수의 직접 호출로 우회하지 못하게 하며, 정상 stream/encoder/session 상태 전이를 OFF 때문에 막지 않는다.
5. startup 로그는 Args 문자열뿐 아니라 **실제로 bound된 정책**을 반영한다. launch 전에 source/binary가 이 차단을 지원하고 최종 argv/profile의 OFF 의도가 맞는지 확인한다. 그 launch의 actual input OFF와 power OFF를 연결/스트리밍 전에 함께 확인한다. unknown/ON/무확인은 invalid다.
6. 누락/ON/오타·우회 부정 대조는 **기록 전용 OS 경계/승인된 격리 환경**에서만 한다. 실제 사용자 PC에 default ON Host를 일부러 띄워 그때의 API 호출을 사후 로그로 잡는 방식은 안전한 fail-closed 검증이 아니다.

핵심은 “wake off 로그를 추가”하는 것만이 아니라 **그 OFF가 실제 effect 함수를 건너뛰는 것**이다. 기본 ON의 이전 호출 trace와 OFF의 0회는 같은 production 경계 + 기록 API로 검사할 수 있다. constructor/start/stop/restart·backend fallback·반복 상태·소멸까지 포함하고, ON trace를 검사한다고 실제 OS API를 호출하지 않는다. 이후 검수된 고정 후보의 일반 시험만 기존 권한 범위에서 연결한다.

### Codex의 무효과 직접 확인

`.claude/codex_test_injection_review/power_probe.cpp`는 **고정 원문 host_log.hpp snapshot**을 사용하고, SendInput/PostMessageW/SetThreadExecutionState 세 대상을 모두 기록 함수로 치환했다. 실제 Host/Viewer를 띄우지 않았고 대상 OS 입력/전원 API를 호출하지 않았다.

```
ctor_execution_calls=1 total_execution_calls=4
send_input_calls=1 mouse_events=2 monitor_broadcasts=1
```

scope는 생성→stream true→true 반복→false→소멸이다. 실제 대상 API 호출은 0이며 위 값은 원문이 **요청한 호출의 기록**이다. exe SHA-256 **98077b4395c4ca2ed54fc847642b6936ec4c74d2b5a361b1d3e98073464d20a6**. 원문 snapshot·probe는 내부 검토 폴더에 보존했다. 이 단위 결과를 과거 모든 run의 실제 OS 적용 집계로 바꾸지 않는다.

### ⑵와 구 버전·prelogin의 경계

동일 source/UI/실제 처리 경로를 공유하고 OS 효과만 기록 sink로 바꾸는 build는 CLAUDE.md가 허용한 방식이다. source commit·명시 build 차분·binary/UI 자산 hash·증명하지 못한 권한/OS 항목을 연결하고 출하물에 seam이 없음을 검사한다. **동일 binary 여부는 근거의 종류이며 안전성을 면제하는 이유가 아니다.**

⑴은 새로 구현되는 product candidate와 그 release binary를 같은 파일로 시험할 수 있게 한다. 하지만 **기존 게시 0.2.163에 새 옵션을 넘겨도 차단이 생기지는 않는다**. 미지원 옵션을 무시하는 구본을 “wake off 확인용”으로 사용자 PC에서 시험 삼아 띄우지 않는다. 필요한 구본 case는 승인된 격리 OS, 명시된 source-equivalent effect-sink build 또는 미검증 처리로 연결한다. 대체 build를 쓰면서 “실제 게시 binary로 성공”했다고 보고하지 않는다.

생산 prelogin worker의 고정 인자/보안 계약을 시험 때문에 풀지 않는다. 이름 alias/별도 desktop은 machine power API의 효과 차단을 대신하지 않는다. 실제 OS 전원/입력을 시험해야 하면 VM/전용 격리 환경 등 별도 경계가 필요하며, 현재 사용자 Default·모니터로 흘리지 않는다.

### EVIDENCE와 정책 반영

- 이전부터 보류한 실제 Host EVIDENCE는 계속 보류다. **입력 OFF만 확인한 AVD ke/sharpen·KA/PC·rel/verify의 실제 Host 스트리밍 근거**에도 이 power/wake 전제를 추가한다. 기능 관측 원문·source·서명/hash·배포 사실은 보존한다.
- wake 입력/전원은 화면·pointer damage·프레임 발생 조건에 영향을 줄 수 있다. 따라서 **PS 정지 첫 화면 등에서 기능 판정과 무관했다고 가정하지 않는다**. 새 안전 경계 아래에서 영향을 받은 행동을 확인한다. 무관 pure/NP UDP 모의 대역을 이 이유로 전량 다시 실행하지 않는다.
- 단독 Host 기동/Viewer0 보고도 일반 입력 설정/codec 기동 근거일 뿐, 현재 발견한 생성자 power 효과까지 없었다는 근거가 아니다. fatal의 조기 실패도 객체 생성 순서를 포함한 경계를 확인한다. 기존 파일/삭제 T2·기타 OS 효과 규칙은 유지한다.
- **CLAUDE.md에 별도 OS 효과 경계 규칙을 추가한다.** `.claude/injection_power_policy_reviewed.diff`는 **DOC OK**다. base는 main 7f96e43, 대상은 CLAUDE.md·docs/history.md 두 파일만이며 apply --check PASS·작업 트리 무수정을 확인했다.
  - patch SHA-256 **20ca205a2ec59b7dcb039be1b39c3d2f1080a6f2939fdea4f1fcd69c3c1fe435**
  - 반영 후 UTF-8/LF SHA-256: CLAUDE.md **53d16e4de1dd975af0a8a946545bf0c31778796832074d43df98da06700e7b92**, history **b74d6ab5c780e2faaee7ed080dcb2b914a295aa974257327e79b7c6b0acaf18d**.
  - 동일 문구면 검증용이 두 파일만 Git MCP commit 가능, 단독 push/추가 승인 왕복 없음. 문서 승인은 구현 SOURCE·시험 재개 승인이 아니다.

기존 작업용1이 공유 Host power 경계를 맡고 작업용2는 **검수된 고정 공용 후보를 받아 PC launch gate에 적용**하도록 검증용의 기존 단일 창구에서 취합한다. 별도 중복 writer를 만들지 않는다. 9개 보류를 유지하고 안전한 source/기록 sink/문서 등 비해당 독립 작업은 계속한다. 특정 prelogin 시험과 real-input 4개의 금지/격리 기준도 유지한다. 이번 Codex 직접 실행은 세 API를 기록한 위 review probe뿐이며 실제 입력/전원·Host·금지 시험·운영/설치·삭제·commit/push는 하지 않았다. 사용자 현재 조치 없음.

---

## 2026-10-28 — d9a3a91 SOURCE: NEEDS_CHANGES

고정 **ka-r3 d9a3a912b64b9b92d0a24d6405cb22b2611abc1f**, 직전 **0fb4676b618718c4cac02bfe0840fb3b122352b1** 대비 injection 변경을 검토했다. 요청 1·2·4의 SOURCE와 5의 설계 결정이다. **현재 record build는 사용자 OS에 효과를 내지 않는 경계를 완성하지 못했다. 8개 guard e2e를 이 build로 바꾸는 것만으로 재개할 수 없다.**

읽는 시점 worktree HEAD/report는 후속 **92f33ca**까지 진행돼 있었다. 아래 판단은 `git show d9a3a91:...`로 고정했다. 후속 **198a63a**의 none/완전한 로그 행 guard, **92f33ca**의 fatal 보수는 이번 고정 후보의 성과로 섞지 않는다. 요청 3의 알려진 fatal 결함은 재지적 목록에서 제외하고, 후속 고정 후보에서 함께 확인한다. 이번 판단은 그 후속 후보의 SOURCE 승인도 아니다.

### 수용하는 범위

- 일반 빌드에서 `send_input`·`post_message`·`set_cursor_pos`는 기존 위치의 Win32 호출에 인자·반환값을 직접 위임한다. `broker_refused`는 일반 빌드에서 false이며 추가 broker 작업을 하지 않는다. 읽은 변경과 아래 위임 probe에서는 제품 경로의 의미 변경을 찾지 못했다. **제품 전체 실행·bitwise 동일성까지 확인했다는 뜻은 아니다.**
- 실제 receipt/policy/routing/dispatch를 공유하고 OS 효과만 기록하는 별도 target 설계는 허용한다. CMake의 **REMOTE60_STREAM_TEST_SEAM=1과 REMOTE60_OS_INPUT_RECORD_ONLY=1 두 차분**을 모두 명시해야 한다. 단순히 OS macro 하나만 다르다고 기록하지 않는다. 출하 gate의 `os-input-record` marker 추가는 적절하다. 정상 산출물의 seam/marker 부재와 시험물을 이름 변경해도 출하가 거부되는지는 이후 고정 산출물에서 확인한다.
- d9의 **완전한 시험 profile config에서 enableInputInjection=false**를 사용하는 것은 앞서 허용한 경로다. none 방식이 아니라는 이유로 거부하지 않는다. `--config`가 runtime env를 지우므로 forceEncodedExperiment 등 해당 시험의 유효 설정을 함께 고정해야 한다. 후속 guard가 none 방식으로 정리됐다면 그 최종 방식 하나를 검토하면 된다. 표준을 다시 결정하거나 d9용 workaround를 따로 유지할 필요는 없다.

### N1 — record-only가 막지 못하는 OS 효과

**입력 세 API와 broker 차단만으로는 불충분하다.** 다음은 고정 원문에서 남아 있는 호출이며, 실제로 이 PC에 반영됐다고 관측한 결과는 아니다.

1. **전원:** host_log.hpp의 HostPowerKeepalive 생성자·Apply·소멸자는 여전히 직접 **SetThreadExecutionState**를 호출한다. record build도 생성 시 system 유지, stream 시 display 유지, 소멸 시 해제를 요청한다. 앞 Finding-3의 최초 생성 이전 정책/전체 전원 경계 조건이 그대로 적용된다. wake의 SendInput/broadcast만 기록하는 것으로 해결되지 않는다.
2. **IME/입력 상태:** host_input_inject.cpp의 desktop keydown은 wrapper 전에 **ensure_foreground_ime_alphanumeric**를 실행한다(:302, :769). 이 경로와 다른 IME 조정 경로에는 **AttachThreadInput, ImmSetConversionStatus, ImmSetOpenStatus**가 남아 있다(:325–337, :397–429). record target에서 Tab down을 보내도 foreground의 IME를 변경할 수 있는 경로다. AttachThreadInput도 순수 조회가 아니다. Microsoft는 입력 상태 공유와 호출 뒤 key state reset을 설명한다. [AttachThreadInput](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-attachthreadinput), [ImmSetConversionStatus](https://learn.microsoft.com/en-us/windows/win32/api/imm/nf-imm-immsetconversionstatus).
3. **자격증명 필드:** logon_ui_target.cpp::ReplaceWithUnicode는 **value->SetValue(empty)**로 필드를 지운 뒤(:244), identity를 재확인하고 wrapped SendInput을 수행한다. 따라서 typing만 기록해도 앞선 clear는 실제 UIA 쓰기다. [SetValue 공식 정의](https://learn.microsoft.com/en-us/windows/win32/api/uiautomationclient/nf-uiautomationclient-iuiautomationvaluepattern-setvalue). 대상이 fixture라는 별도 근거가 있더라도 현재 target 전체를 무효과라고 부를 수는 없다.

기존 작업용1 보수 정본에서 **전원·IME/입력 연결·UIA clear까지 실제 효과 경계로 분리**한다. 관측/검증과 쓰기를 구분하고, record target에서는 해당 쓰기를 수행하지 않고 요청·결과를 기록한다. 함수 전체를 항상 성공으로 조기 반환하거나 foreground/credential gate를 true로 고정하지 않는다. 정상 제품 분기는 이전 인자·오류·순서를 보존한다. 서비스/자식 프로세스가 효과를 대신 수행하는 우회도 launch 표의 실제 참여 대상 기준으로 확인한다.

record target의 broker refusal은 현 소스에서 EnsureInstalledAndConnected/WriteLocked 앞에 있으므로 실제 broker 연결을 막는 방향은 맞다. 다만 **broker unavailable/local fallback 경로의 근거**이며 실제 broker 성공·권한·설치·인증 경로의 성공 근거가 아니다. 그 공백을 채우려고 사용자 PC의 실제 서비스를 연결하지 않는다.

**8개 product GNLinkStream e2e는 input OFF여도 wake/power가 남고, d9 record target도 위 경계가 남는다.** 완성된 기록 target을 8개의 guard 시험에 사용하는 방향은 수용한다. 바이너리·source/build 차분·scope를 기록하고, 고정 SOURCE 검토 전 현재 target을 시험 삼아 실행하지 않는다. input/power/다른 쓰기의 실제 차단 범위를 나타내는 effective 상태를 gate로 삼아야 하며, 현 `os input effects=record-only` 문자열만으로 전체 차단을 주장하지 않는다.

### N2 — fail-closed 종료 시 자식 회수 보장

e2e_injection_guard.hpp::e2e_injection_setup_failed는 **std::_Exit(2)**로 종료한다. 하지만 새 os_input_record_e2e_test는 **CreateJobObject/SetInformationJobObject/AssignProcessToJobObject 결과를 확인하지 않고** suspended Host를 ResumeThread한다. 예를 들어 assignment가 실패해도 Host가 시작되며, 이후 guard 실패의 _Exit는 소멸자를 실행하지 않는다. 기존 host_control_deadline_e2e_test 등 연결한 launch에도 같은 미확인 패턴이 있다. job close가 자식을 반드시 회수한다는 전제가 성립하지 않는다.

공유 launch/guard에 연결한 대상에서 **job 생성·KILL_ON_JOB_CLOSE 설정·suspended child 편입 성공을 모두 확인한 뒤에만 Resume**한다. 실패 시 자기 소유 suspended process handle로 종료·대기·회수하고, 실행한 child도 오류 종료에서 확실히 회수한다. 전역 PID/이름 기반 kill은 사용하지 않는다. 정리 후에도 setup invalid로 실패를 유지한다. 이 회수 보장이 없으면 _Exit에 의존하지 않는다.

d9의 OFF 판독은 문자열 prefix 비교(:98)여서 완전한 행을 검사하지 않는다. 후속 198a63a의 수정이 있는 것은 확인했으므로 별도 d9 왕복을 만들지 않고 **최종 고정 guard의 알려진 값·행 경계·이번 launch·실패 child 회수**를 함께 검토한다. config/marker 관측 실패를 테스트 PASS나 임의 timeout skip으로 바꾸지 않는다.

### T1 — 기록 EVIDENCE의 실제 의미와 검증 결함

- 새 os_input_record_e2e_test는 `Ab`에 **unicode units=2 fnv=hash(Ab)** 한 행을 기대한다. 그러나 실제 host_input_inject.cpp::apply_input_text_message는 UTF-16 단위마다 send_desktop_unicode_char를 호출하고, 그 함수는 한 단위의 down/up INPUT 두 개를 보낸다(:281–289, :799, :817). 현재 sink는 **units=1인 A 행과 b 행**을 만든다. 이 기대값은 맞지 않는다. 실제 fragment 순서를 검증하거나 command별로 기록을 합산한다. 시험을 통과시키려고 제품 batching을 바꾸지 않는다.
- 현재 `>= dispatched+5`와 Tab/click/hash 문자열 존재만으로는 **요청 seq/session/epoch와 sink effect가 일치**한다고 증명할 수 없다. sink에 해당 문맥이 없다. 실제 검증을 통과한 dispatch 문맥을 test-only 기록으로 전달해 command/fragment/순서·허용/거부를 연결하거나 동등한 식별 근거를 둔다. wake/초기 release 같은 다른 원인의 효과와 구별한다. peer·epoch·전경 부정 대조에서도 실제 제품 gate와 sink 0/해당 효과만을 검증하고 규칙을 시험 안에 복제하지 않는다.
- 전후 GetCursorPos/foreground HWND가 같다는 것은 **“THIS PC saw none of it”의 증거가 아니다.** 중간 왕복·IME·전원·UIA 쓰기를 놓치며 사용자의 정상 동작도 실패로 오인한다. 무효과 증명은 검토된 효과 경계와 요청 기록에서 얻고 이 문구를 수정한다. 전후 상태를 남긴다면 제한된 보조 관측으로만 보고한다.
- record sink가 성공을 모델링하는 것은 OS 경계 분리에 사용할 수 있다. 실제 OS 수락/거부·UIPI·입력 성공을 증명하지는 않는다. 오류 처리 자체를 검증하는 case는 성공만 반환하는 모델로 검증했다고 주장하지 않는다.

### 결정 5 — foreground 관측 대역과 실제 OS 미검증을 함께 사용

**시험 전용 관측 provider/seam을 채택한다. 실제 foreground/LogonUI/OS 입력의 성공은 별도 미검증으로 남긴다.** 관측 대역 사용과 미검증 표기는 양자택일이 아니다.

실제 credential/routing 검증 코드에 필요한 **session/desktop·대상 HWND/pid/image/소유·focused password element identity·foreground 관계·조회 성공/실패** 관측값을 공급한다. 일반 제품은 기존 Win32/UIA 관측을 사용하며 test provider는 별도 target/marker로만 연결한다. 단순 foreground=true나 receiver 성공 stub으로 바꾸지 않는다. 쓰기 경계는 N1에 따라 별도로 기록한다.

유효한 대상/전경은 예상 clear/type sink로, 다른 pid/image·비password·전경 아님·조회 실패·오래된 identity는 거부로 검증한다. clear 이전 및 clear와 typing 사이에 target/focus가 바뀌는 경우도 동일 제품의 재검증 규칙을 적용한다. 사용자의 Default에서 창을 앞으로 가져오거나 Tab/SetFocus/커서 조작을 해서 전제를 충족시키지 않는다. 실제 Windows foreground·SYSTEM/LogonUI·UIPI·인증 성공은 승인된 격리 OS에서만 별도 확인한다.

**viewer_prelogin_login_e2e_test의 기존 실행 금지는 해제하지 않는다.** 이 SOURCE/재설계 결정은 그 시험의 실행 허가가 아니다. real-input 4개의 기본 REFUSED 및 격리/명시 권한 기준도 유지한다.

### Codex 직접 확인과 다음 고정 패킷

전체 Host/Viewer/금지 하네스 대신, 고정 header snapshot을 사용한 두 개의 **실제 OS 입력/전원 API를 호출하지 않는 review probe**를 실행했다. 원문·probe·exe는 `.claude/codex_test_injection_review/`에 보존했다.

| 직접 probe | 관측과 범위 | exe SHA-256 |
|---|---|---|
| default_wrapper_d9_probe | Win32 이름을 기록 함수로 바꿔 원문 일반 wrapper의 인자·부분/실패 반환·GetLastError 보존 및 broker false 확인. PASS, 위임 5회, 실제 입력 API 0 | 56b16173b74dc5ed5b862a7ce215d522645562925675ae969283e7214ca5c379 |
| record_power_d9_probe | record macro의 원문 HostPowerKeepalive를 생성→stream true→소멸. raw SetThreadExecutionState도 추가 spy로 치환: 생성 1회/합계 3회 요청. 실제 입력·전원 API 0 | d61ad24c801a4a966dcbc63e8adecc28f893c57830157c45b7ae6a579cfe9ccb |

기존 검증용 단일 창구에서 **N1·N2·T1, 후속 guard/fatal 보수, 관측 seam의 최종 source/build 차분·launch/case 표**를 한 고정 패킷으로 취합한다. 알려진 보수의 완료 여부를 조각 중계하거나 새 중복 작업자를 만들 필요는 없다. 안전한 SOURCE/문서/무효과 기록 단위 작업은 계속할 수 있고, **9개 실행 보류와 기존 실제 Host EVIDENCE의 안전 재검증 보류는 유지**한다. 무관 pure/NP 가상 UDP 근거·서명/hash/게시 사실에는 이 판정을 확대하지 않는다.

이번 변경은 이 검토 기록뿐이다. 제품 수정·전체 제품 빌드·Host/Viewer·IME/UIA 조작·금지 하네스·운영/설치·삭제·commit/push는 하지 않았다. 검토 기록은 릴리스 통합 문서/커밋 대상이 아니므로 별도 history·계획·commit을 만들지 않았다. 사용자 현재 조치 없음.

### 후보 갱신 198a63a — guard 변경 한정 수용, 전체 NEEDS_CHANGES 유지

검증용의 늦게 도착한 후보 갱신을 반영했다. `git diff d9a3a91 198a63a`와 고정 guard/순수 시험 원문을 읽었다. 인자는 **--input-injection-mode none**으로 통일되고, 허용 문구 뒤 **CR/LF 행 끝을 요구**한다. none2·다른 mode/원인·config 오류·enabled를 거부하고, 정확한 문구라도 행 끝이 아직 없으면 통과하지 않는다. **d9에서 지적한 OFF prefix 통과 문제는 이 변경 범위에서 닫는다.** 일반 config OFF를 허용했던 정책을 취소한다는 뜻은 아니며, 이 하네스가 선택한 표준 하나를 수용한 것이다.

**guard 변경 한정 SOURCE 수용이며 198a63a 전체 판정은 NEEDS_CHANGES다.** d9의 wrapper/record target이 그대로여서 위 **N1 전원·IME·UIA 효과, N2 실패 자식 회수, T1 기록/Unicode 검증**은 남는다. 작업용의 순수 16/16·15 target 빌드는 보고 인용이며 이번 Codex의 직접 실행이 아니다. 이 갱신에서는 추가 실행 없이 읽기만 했다. 이번 launch의 로그/child 소유는 실제 launch 구성까지 연결해야 하고, 문자열 판독만으로 그 전제나 전체 OS 효과 차단이 증명되지는 않는다.

**wake 설계 결정은 대기 중이 아니다.** 앞 Finding-3 및 d9 SOURCE가 현재 답이며, 완성된 record target을 guard e2e에 사용하는 방향도 이미 수용했다. 중앙 전원 경계를 포함한 효과 보수와 기존 고정 보수 패킷을 이어 가면 된다. 별도 선택 승인이나 같은 질문 재전송은 필요 없다. fatal은 이번 후보 실행 세트에서 계속 제외하고 후속 고정본에서 확인한다. 9개 실행 보류와 prelogin 명시 금지를 유지한다.

### 후보 갱신 92f33ca — fatal 추가 검토, 전체 NEEDS_CHANGES 유지

고정 **92f33ca50b8cf7ae5166ef36d1cc63b2fc1dfb54**의 source로 추가 검토했다. 검증용이 지정한 report hash **25479856e0c5…4ea7**와 실제 읽은 파일은 달랐다. 실제 SHA-256은 **764756e53e92950d276ee8f678c80a8704f6f1c2506f69a252e428826d07ff51**이며 이미 후속 **6b9cf79**의 r4 설명과 이전 92의 표가 함께 있었다. 이를 고정 92의 증거로 사용하지 않았다. **6b9의 전원 구현은 이번 SOURCE 판정에 포함되지 않는다.** 이전 report 재송신 때문에 작업을 멈출 필요 없이, 다음 보수 패킷에서 최종 commit과 일치하는 문서 snapshot/hash 하나를 고정한다.

**닫는 범위:** run_with_client의 sendto는 매 반복 **e2e_injection_state==1 확인 뒤**에 있다. 미확인 상태에서는 계속 송신하지 않으며, 다른 주입 상태면 해당 child를 종료/대기하고 invalid 코드를 반환한다. 첫 송신 전 실패라면 Hello 0이다. 이미 송신한 뒤 모순이 생긴 경우까지 Hello 0이라고 부르지는 않는다. 자기 helper가 요청한 송신 시도 수와 실제 성공 송신/Host 수신도 구분한다. 198의 OFF guard 수정과 이 **Hello 전 gate 위치 변경**은 SOURCE 수용한다.

**삭제 위치 이동도 방향은 수용한다.** anchor/root의 directory·reparse 확인, CreateDirectory로 새 gRun 확보, canonical 절대 경로에 trailing separator를 포함한 gRun prefix 검사를 통해 exe 옆 재귀 삭제를 없앴다. 알려진 고정 호출은 gRun/fatal_prelogin_logroot이며, 경계 검사 실패 시 remove_all에 진입하지 않는다. 다만 다음 경계를 남긴다.

- 질문한 **일반 directory junction은 설치 MSVC 14.44.35207에서 따라 내려가지 않는다.** `<filesystem>`의 `_Refresh`(:1993–2001)는 symlink와 mount-point junction을 별도 file_type으로 분류한다. `_Remove_all_dir`(:3883–3891)는 symlink_status가 directory인 경우만 재귀 진입하고 junction은 remove한다. Microsoft STL의 remove 구현은 **OPEN_REPARSE_POINT**로 대상 handle을 열어 제거한다. 따라서 정적인 일반 junction 때문에 그 target의 파일까지 재귀 삭제한다고 판정하지 않는다. 근거: [Microsoft STL 구현](https://raw.githubusercontent.com/microsoft/STL/vs-2022-17.14/stl/src/filesystem.cpp), 설치된 동일 계열 header 원문. 이는 source 판단이며 실제 삭제 시험을 실행한 결과가 아니다.
- **“경로 전체/모든 하위 링크 거부”는 현재 helper가 보장하지 않는다.** is_reparse는 canonical 결과의 마지막 항목에만 적용되며, canonical 전에 존재한 링크가 해소될 수 있다. STL도 알 수 없는 다른 reparse tag를 일반 directory/file로 취급한다(:2003). 따라서 위 일반 junction 비추종 결론을 모든 reparse point·동시 경로 교체 안전성으로 확대하지 않는다. 기존 신뢰 run에서 소유한 알려진 파일/빈 폴더만 비재귀 회수하는 방식이면 이 하네스에는 충분하다. 재귀 삭제를 유지한다면 요구한 link 거부 범위를 실제로 검사하고 오류 시 보존/실패하도록 맞춘다. 사용자/외부 경로 정리는 하지 않는다.
- **remove_all(canon, ec)의 ec를 버리고 있다.** 삭제 실패를 보고·실패 계수에 반영하고 남은 경로를 보존한다. 뒤의 파일/폴더 회수도 성공/실패를 관측한다. 링크 거부나 삭제가 성공했다고 단정한 문구를 현재 코드보다 넓게 쓰지 않는다. 경로별 조회/정리 실패에 대해 바깥 cleanup이나 광역 삭제로 fallback하지 않는다.

**fatal의 실행 격리는 아직 충분하지 않다.** run() 중 std/winrt는 주입된 조기 fault지만 **none은 fault를 주입하지 않는 정상 control**이다. helper가 socket을 만들지 않는 것과 Host가 네트워크에 노출되지 않는 것은 다르다. 92의 Args 기본 bindAddress는 빈 문자열(모든 interface)이고 run/run_with_client에는 loopback bind가 없다. CreateProcess는 사용자 환경을 그대로 상속하며 일반 case의 config/cache 등은 gRun으로 연결하지 않는다. 로그 root 이동만으로 전체 파일/네트워크 격리를 주장하지 않는다. 기존 공유 isolation 규칙으로 **loopback·시험 소유 cache/appdata·해당 launch의 effective 상태**를 연결하고, 조기 실패 case의 예외는 실제 도달 단계 기준으로 구분한다. prelogin의 고정 제품 보안 계약을 완화하지 않는다.

92에서 **최초 HostPowerKeepalive 생성의 execution-state 효과도 남아 있으므로** client 0/startup fault라는 이유로 실행을 재개하지 않는다. 현재 고정 후보 전체는 앞 **N1·N2·T1과 이 fatal 보수 범위를 포함해 NEEDS_CHANGES**다. wake는 Finding-3에서 이미 결정됐고, prelogin foreground/clipboard 대역 방향도 d9 결정 및 real4 설계 범위에 있다. 선택 답변 대기가 아니라 기존 작업용1의 통합 보수/고정 SOURCE가 다음 단계다. 알려진 보수를 진행하면서 늦은 92 패킷용으로 별도 재작업 branch/중복 task를 만들지 않는다.

이번 추가 검토는 고정 source·설치 STL·공식 구현 읽기와 판정서 기록뿐이다. Host/Viewer/fatal/prelogin·실제 입력/전원·삭제 시험·제품 빌드·commit/push는 하지 않았다. 실행 보류는 유지하며, 사용자 현재 조치 없음.

### 정책 반영 확인 — 14cdde1 DOC OK

**14cdde16097d253a8de840d1a06679b3acfeddfa**를 git show로 직접 확인했다. parent는 **7f96e43d584686df2f86b069218bbaad52a29e6e**이고 변경 파일은 **CLAUDE.md·docs/history.md 두 개뿐**이다. 승인 patch를 parent 내용에 재구성한 결과와 commit 내용이 일치하며 정책 반영은 **DOC OK**다. CLAUDE.md SHA-256은 **53d16e4de1dd975af0a8a946545bf0c31778796832074d43df98da06700e7b92**이다. history는 기존 UTF-8 BOM을 보존한 git blob SHA-256이 승인값 **b74d6ab5c780e2faaee7ed080dcb2b914a295aa974257327e79b7c6b0acaf18d**와 일치한다(BOM을 제외하면 **7931c33d9b224f990579b6920043de5a740c47091565a650c0c5d640c57f4fdf**). 이는 내용 차이가 아니다.

단독 push 미실행은 검증용 보고이며 이번에 네트워크 push를 수행하지 않았다. 제품/하네스 SOURCE와 실행 보류는 이 문서 commit으로 해제되지 않는다. 기존 작업용1의 **r4 통합 고정 후보**가 다음 SOURCE 대상이고 작업용2는 검수된 공용 guard를 소비한다. 92용 별도 중복 검토를 만들지 않는다. 이번 확인은 읽기와 이 판정 기록뿐이다.

### 후보 갱신 6b9cf79 — 전원 차단 수용, 전체 SOURCE NEEDS_CHANGES

요청한 **d9a3a91..6b9cf79 네 commit의 제품 경계**와 92f33ca 대비 전원 변경을 고정 git show로 확인했다. report SHA-256 **764756e53e92950d276ee8f678c80a8704f6f1c2506f69a252e428826d07ff51**은 이번 지정값과 일치한다. 조사 시점 worktree에는 이미 후속 미커밋 수정이 여러 파일에 있었으므로 그 변경은 읽은 고정 원문에 섞지 않았다. 원본/후속 작업은 건드리지 않았다.

**Finding-3의 전원 효과 차단 구현 자체는 SOURCE 수용한다.** main에서 const 정책을 bind/유효성 확인한 뒤 HostPowerKeepalive를 생성한다. ON이 기본이고, 객체의 enabled_는 const이며 setter가 없다. OFF는 생성·stream 상태 전환·소멸에서 execution-state·wake·monitor broadcast를 수행하지 않고 stream 상태만 갱신한다. wake가 private이어서 기존 call site의 true가 OFF를 우회하지 않는다. fixed src grep에서도 실제 SetThreadExecutionState 호출은 os_input 래퍼 일반 분기 한 곳이며, 다른 wake 호출자를 찾지 못했다. 일반 wrapper의 직접 위임 판단도 유지한다.

다만 **전원 옵션 해석과 guard의 상충 처리에 아래 두 지적**이 있고, record target/하네스의 기존 미해결도 남아 전체 SOURCE OK는 아니다.

#### P2 — 조기 binder가 다른 옵션의 값을 전원 옵션으로 오인

host_power_policy.hpp::bind_host_power_policy는 argv 전체를 한 칸씩 검색하며 다른 옵션의 피연산자를 구분하지 않는다. host_args.cpp는 `--capture-window-title` 다음 토큰을 그대로 제목으로 소비한다. 따라서 다음은 새 전원 옵션이 아니라 **제목 값**인데 전원 binder는 다르게 해석한다.

```
GNLinkStream --capture-window-title "--host-power-management"
  실제 제목 값 / 전원 설정 없음 → 기존 기본 ON이어야 함
  현재 binder: missing value → ok=0, main exit 65

GNLinkStream --capture-window-title "--host-power-management" off
  host_args에서 제목 값 + 무관 토큰 off
  현재 binder: enabled=0, fromCommandLine=1
```

고정 header의 직접 순수 probe로 두 상태를 확인했다. **기존 옵션의 값 경계를 공유하는 파싱으로 초기 정책을 결정**한다. 최초 효과 이전 bind라는 순서는 유지하고, 초기화 뒤 setter로 뒤집는 방식으로 해결하지 않는다. 두 독립 parser의 옵션 목록/arity가 어긋나지 않게 하며 실제 host_args와 동일 인자 배열을 대조하는 부정 대조를 둔다. 기본 ON·로컬 전용 계약을 바꾸라는 요구는 아니다.

#### P2 — power OFF 뒤 ON 상충을 놓침

e2e_power_state/e2e_whole_line은 첫 `host power management=`만 검사한다. **OFF 행→input OFF 행→power ON 행**이면 e2e_injection_state가 1을 반환하고, e2e_injection_contradicted도 injectionEnabled=1만 검사하여 false다. 고정 guard를 사용한 순수 probe 결과:

```
power_off_then_on gate=1 later_contradiction=0
```

현재 정상 객체가 runtime에서 정책을 바꾼다고 주장하는 것은 아니다. **상충 로그는 invalid**라는 guard 계약의 누락이다. 이번 launch 범위의 모든 관련 상태 행과 실행 중 상충 검사에서 ON/다른 값·오류를 거부한다. 첫 ON 뒤 OFF뿐 아니라 반대 순서도 확인한다. 이 부정 대조는 지금처럼 문자열/기록 대역에서만 하며 실제 ON Host를 띄우지 않는다.

#### 닫은 것과 남긴 것

- **이전 N1의 전원 부분은 닫는다.** record-only build의 power API도 이제 기록되며, normal OFF 객체는 기록 요청 자체도 0이다. 이 결론을 record target 전체 무효과로 확대하지 않는다.
- **N1 IME/AttachThreadInput·UIA SetValue, N2 job/실패 자식 회수, T1 command/fragment 식별·Unicode 기대값**은 6b9에서 해결되지 않았다. 92 fatal의 loopback/사용자 환경·삭제 오류/범위 지적도 그대로 남는다. 알려진 후속 working 보수가 있더라도 이 고정 후보의 해결로 세지 않는다.
- 6b9의 prelogin launch에도 두 OFF 인자를 넣는 변경은 생성자 전원 경계에 필요한 방향이다. 제품 prelogin 권한/고정 계약을 시험 때문에 풀지 않는다. 금지된 viewer_prelogin_login_e2e를 이 인자로 재개할 수는 없다.
- 공용 guard를 소비한 PC 브랜치 merge 및 시험은 별도 최종 고정점에 연결한다. 전원 원 지적이 닫힌 것을 이유로 남은 child 회수/다른 효과 경계를 건너뛰지 않는다.

#### 결정④ — 구 Host는 이번 라운드 NOT RUN, fixture는 제한된 보충

**게시본 0.2.163을 이번 라운드에서 NOT RUN·미검증으로 남기는 권고를 수용한다.** 그 바이너리는 새 전원 차단을 지원하지 않으므로 사용자 PC에서 확인용으로 기동하지 않는다. VM은 승인된 격리 후속이며 지금 선택/기동을 요구하지 않는다.

**순수 protocol/가상 transport fixture로 구 Host 대응 분기를 보충하는 것도 수용한다.** 실제 client/lease parser·상태 전이·timeout/fallback을 사용하고, 고정 구 Host source/보존 wire 근거에 맞는 응답·무응답을 공급한다. 구 Host가 실제로 보내지 않는 Unsupported ACK를 만들어 성공시키거나, 결과 enum을 fixture가 직접 반환해 판정 코드를 우회하지 않는다. 해당 계약의 capability 미지원·무응답/유실·비일치/오래된 응답·취소와 **자동 일반 연결/activation/재탈취 0**을 검증한다. 합성 대역 결과와 구 Host wire를 직접 확보한 결과도 구분한다.

이는 **새 client의 명시된 legacy 입력/침묵 처리 근거**다. 실제 0.2.163 실행·소켓 타이밍·NAT·제품과의 무간섭 성공으로 소급하지 않으며 이전 held real-Host EVIDENCE를 복구하지 않는다. 결과는 실행한 검사와 NOT RUN case를 따로 집계하고, 릴리스의 구 Host 호환성 미검증도 남긴다. 실제 구 제품 대응의 독립 근거가 필수인 출하 판단은 해당 범위에서 별도로 평가한다. 미검증 항목을 감추거나 다른 구성 단위까지 전량 보류하는 이유로 쓰지 않는다.

#### Codex 직접 확인

`.claude/codex_test_injection_review/power_6b9/`에 **6b9 고정 header와 필요한 header 의존성**을 보존하고, 별도 exe로 다음을 빌드/실행했다. 전체 제품/Host/Viewer가 아닌 순수·record-only 단위다. ON 부정 대조를 포함해 대상 OS 입력/전원 API는 기록 분기만 사용했다.

| 직접 실행 | 결과 | exe SHA-256 |
|---|---|---|
| host_power_policy_record_test, REMOTE60_OS_INPUT_RECORD_ONLY=1 | **18/18 PASS**: 기본/명시/오타·OFF 효과 0·ON 원래 trace·소멸·guard | 669ead9dff75ccf2ad830e81f616606a9ae535bc8579c8205cff6d49035016e3 |
| e2e_injection_guard_test | **20/20 PASS**: 기존 순수 case. 위 OFF→ON 상충은 여기에 없는 별도 반례 | ff6cc086fbb81ac62e55cc8a392ab0e35f126737dc1c234ff48a73b7e2edb236 |
| arg_boundary_probe | 위 옵션 피연산자 오인 2건과 OFF→ON guard 누락 재현, 반환 0은 반례 확인 | a81d6a7055aec1bc450d06d0a21438532dbfb8a4ba361e80ba5985f211a525f6 |

실제 Host/Viewer·사용자 입력/전원·clipboard·금지 하네스·삭제·제품 전체 빌드·운영/설치·commit/push는 하지 않았다. 출하 gate 52/52는 이번 Codex 직접 실행이 아니라 작업용 보고다. 다음 담당은 기존 검증용→작업용1이며 **남은 OS 효과·실패 회수·기록/fatal 보수와 위 두 경계를 한 고정 패킷으로 취합**한다. 별도 중복 task·wake 선택 재승인·구 Host 임의 실행은 필요 없다. 해당 실행 보류를 유지하고 비해당 안전 단위 작업은 계속한다. 사용자 현재 조치 없음.

### 후보 갱신 ba2eaf2 — 제품/기록 경계 SOURCE OK, 일반 하네스는 NEEDS_CHANGES

고정 **ba2eaf28ff515bea8f83617dade88e33f17b378a**, 신규 **6b9cf79..ba2eaf2**의 3 commit(7c37cc2·7205206·ba2eaf2)을 git show로 검토했다. tracked clean·diff --check 정상, report SHA-256 **aa35a5f9b3cc10e5917ff17ef5726fde22800ab5dd8779477e511c3daaa98ae7** 일치. 후속 working 내용을 섞지 않았다.

**판정은 세 범위로 구분한다.** 제품 SOURCE 수용을 9개 하네스 전체 실행 허가·출하 승인으로 해석하지 않는다.

| 범위 | SOURCE 판정 | 다음 단계 |
|---|---|---|
| 일반 빌드의 효과 wrapper·power/parse 변경 | **OK** | 제품 source 검토 범위의 수용. 최종 통합/출하 검증은 별개 |
| 새 ordinary **os_input_record_e2e**의 지정 기록 경계 | **OK, 아래 scope 조건** | 고정 source→시험 artifact hash 매핑·clipboard OFF·기록 target·job gate와 슬롯 해제를 확인한 뒤 검증용 독립 실행 가능 |
| 일반 9개 guard 하네스(fatal 포함) | **NEEDS_CHANGES** | 아래 실행 중/사후 상충 검사 연결을 보수. fatal은 포트/자식 종료 보수까지 제외 |

이것은 **전체 commit의 무조건 main merge/출하 승인도 아니다.** 제품과 하네스의 해결/미해결을 분리한 SOURCE 판정이다. 안전한 기록 경계의 독립 작업까지 함께 멈추지 않는다. 원래 viewer_prelogin_login_e2e는 전 모드·wrapper **계속 금지**이며 이 허용에 포함되지 않는다.

#### 닫는 지적과 제품 의미

- **N1 IME/입력 연결·UIA 쓰기:** 6개 AttachThreadInput, ImmSetOpenStatus/ConversionStatus, UIA SetValue clear를 경계로 옮긴 것을 수용한다. 일반 분기는 동일 인자·반환의 기존 Win32/UIA 직접 호출이다. record 분기는 해당 쓰기 대신 기록하고 broker 위임도 기존 guard로 거부한다. 일반 wrapper와 빈 DispatchContext가 GetLastError/HRESULT·반환 의미를 보존하는 것은 아래 가짜 API 위임 probe로 추가 확인했다. 전체 제품 실행 동등성을 직접 검증했다고 주장하지 않는다.
- **N1 전원**의 이전 SOURCE 수용은 유지한다. record marker에 다루는 API와 **clipboard 쓰기 미포함**을 명시한 것도 수용한다. foreground/IMM/UIA 조회가 실제 관측으로 남는 만큼, 실제 focus·IME 변경·LogonUI 입력 성공을 증명한 것은 아니다.
- **N2 핵심 편입 순서:** 10 launch의 CREATE_SUSPENDED와 e2e_adopt_or_invalid 호출을 대조했다. job 존재·KILL_ON_JOB_CLOSE read-back·Assign 성공 뒤에만 Resume한다. 실패 시 Resume으로 진행하지 않고 자기 child handle로 종료/대기를 요청하며 INVALID로 끝난다. 기존 무확인 Assign 뒤 Resume 문제를 닫는다. 실제 OS fault에서 Terminate/Wait 성공까지 독립 실행으로 확인한 것은 아니다. 아래 fatal의 직접 launch 수명 문제는 별개다.
- **T1 기록 출처:** DispatchContext가 실제 event/text/release dispatch를 둘러싸고, what/seq/servedEpoch/frag를 thread-local 수명에 붙인다. matcher가 해당 dispatch의 기록·epoch·fragment 및 필수 효과 순서를 검사하고, wake의 ctx none을 입력 대신 사용하지 않는다. Unicode의 **unit=1 두 기록** 수정, 전후 cursor/foreground로 무효과를 주장하던 문구 제거도 수용한다. **pointer의 정확한 좌표 변환·실제 peer/epoch 공격 거부·OS 수락까지 이 9개 문자열 시험이 증명하는 것은 아니다.**
- **P2-1 parser:** 별도 argv 검색을 제거하고 host_args의 token 소비 규칙으로 hostPower를 읽는다. 제목 값이 power switch인 반례가 이제 기본 ON으로 유지된다. main은 parse_args/정책 유효성 확인 **후** HostPowerKeepalive를 생성한다. profile에는 hostPower를 열지 않아 CLI 전용 경계도 유지한다.
- **parse_args의 부작용 판단:** 정확히는 순수 함수가 아니다. profile을 파일로 읽고 json_profile::apply_runtime_env_overrides_from_json이 **_putenv_s로 해당 프로세스 환경을 초기화/설정**한다. 그러나 고정 Args 초기화·parser·호출 helper에서 입력/전원 API, 프로세스 기동·서비스/registry/설치·파일 쓰기를 발견하지 못했다. 이번 최초 power 효과 이전 호출 순서는 수용한다. install_prelogin_worker_log는 parser 이전의 별도 파일 효과이며, 실제 prelogin 권한 계약은 유지하고 시험에서는 소유 log root를 사용해야 한다.
- **fatal의 해결 부분:** 재귀 삭제를 제거해 알려진 자기 파일/빈 폴더만 정리하고, 실패를 기록한다. loopback 인자·private LOCALAPPDATA·세 경로의 power OFF 및 none의 정상 control 재분류를 수용한다. 하위 junction의 재귀 추종 질문은 새 비재귀 정리에는 적용되지 않는다. 이것만으로 아래 launch 실패/수명을 닫지는 않는다.

#### G1 — 초기 상충 거부는 고쳤지만 실행 중/사후 검사와 9개 호출부가 빠짐

ba2의 e2e_whole_line/e2e_injection_state는 **전달된 전체 log**에서 OFF→ON 및 다른 input 상태를 양방향으로 거부한다. 이 **초기 판독 부분**은 수용한다. 그러나 **e2e_injection_contradicted는 여전히 injectionEnabled=1/input injection enabled만** 검사한다. 고정 helper 직접 실행:

```
initial_OFF=1 full_state_after_ON=-1 post_run_contradicted=0
```

더 큰 연결 누락은 **일반 9개 시험에서 e2e_injection_contradicted 호출이 없다**는 점이다. 고정 tree에서 사용자는 helper 자체/순수 시험뿐이다. 해당 하네스들은 초기 e2e_wait_injection_off를 통과한 뒤, 실행 중/종료 시 power/input 상태를 공통으로 다시 판정하지 않는다. fatal의 Hello 반복 판독/normal-control의 최종 판독은 일부 경로일 뿐 전체 공통 보장이 아니다.

**실행 중 관측되는 ON/다른 상태/오류와 종료 시 모순을 실제 하네스의 INVALID/자기 child 회수에 연결**한다. 공유 log 읽기/monitor 지점 등 기존 정본 안의 공통 방식으로 취합하고, 최종 PASS 출력보다 먼저 누적 범위 전체를 확인한다. 미지원/부분 로그는 정상 OFF 증거로 승격하지 않는다. helper가 존재한다는 것만으로 rule 4 완료를 세지 않는다.

고정 문자열/무효과 child 대역으로 **초기 두 OFF→연결 허용→후속 power ON/input 다른 값→중지·INVALID**를 실제 호출 경로에서 확인한다. 실제 ON Host를 일부러 띄우지 않는다. initial gate 순수 PASS와 이 실행 경로 검증을 구분한다. **이 보수가 고정되기 전 일반 9개 전체 실행 승인은 하지 않는다.**

#### F — fatal은 포트 확보 실패와 child 종료 경계를 마저 보수

1. **free_udp_port는 socket/bind/getsockname 반환을 확인하지 않는다.** 실패해 0을 얻어도 launch를 계속한다. 실제 고정 parse_args에 `--bind-port 0`을 주면 후보 목록이 비어 **기본 43000**으로 돌아간다. prelogin seam도 유효 port 후보가 없으면 제품 기본 목록으로 돌아갈 수 있다. 확보 실패를 **기동 전 setup INVALID**로 처리하고, 실제 사용할 transport/port의 확보·생성 결과를 확인한다. 이는 로컬 free-port 검사 뒤 실제 bind 사이의 일반 race와 구분한다. 그 뒤의 정상 bind 실패도 제대로 실패로 남기고 제품 기본 포트로 fallback하지 않는다.
2. **prelogin 직접 launch의 timeout 분기는 TerminateProcess 후 Wait 없이 process handle을 닫는다.** 그 뒤 로그를 읽고 지우므로 종료·flush/회수가 확인되지 않는다. 자기 handle로 terminate 결과와 bounded Wait를 확인하고, timeout/실패는 **종료 미확인·정리 보류**로 분리해 기록한다. run/run_with_client 및 공용 adoption의 실패 회수도 Terminate/Wait 결과를 버리고 “ended”라고 단정하지 않도록 함께 정리한다. 범위는 자기 child와 자기 run뿐이며 PID/이름 kill·바깥 cleanup은 없다.

부정 대조는 launch/OS API 기록 대역으로 먼저 검증할 수 있다. 현재 fatal을 시험 삼아 실행하지 않는다. 이 두 항목 보수와 G1이 고정되면, 금지된 viewer_prelogin_login_e2e와 구분되는 **fatal 자체의 고정 case**를 후속 검토해 포함할 수 있다.

#### 실행 가능한 독립 범위 — ordinary record 시험 하나

**os_input_record_e2e는 일반 9개와 다른 lane으로 진행 가능하다.** 이 시험은 새 ordinary record Host로 가상의 Tab/move/click/Ab를 보내는 것이며 prelogin/login 하네스 재개가 아니다. 조건은 기존 승인 범위 안에서 다음을 실제 산출물/launch에 연결하는 것이다.

- **ba2eaf2 source의 GNLinkStreamInputRecordTest**와 하네스의 build 차분·각 artifact hash를 고정한다. 일반 제품 Host로 대체하거나 marker 이름만으로 파일을 바꾸지 않는다.
- 이 시험의 실제 설정인 **REMOTE60_CLIPBOARD_SYNC=0·loopback·private LOCALAPPDATA/신규 staging·검증된 job 편입**을 유지하고, 그 launch의 지정 record scope marker를 client 이전에 확인한다. 논리 input 허용은 physical OS 효과 허용과 다르므로 일반 OFF guard의 injectionEnabled=1 거부를 무차별 적용하지 않는다.
- 기록 경계에서 SendInput/PostMessage/cursor/AttachThreadInput/ImmSet*/UIA.SetValue/power 및 broker가 수행되지 않고 기록/거부되는 source를 사용한다. 실제 credential/clipboard·real-input 4개를 이 시험에 추가하지 않는다. 조건/기동 실패는 INVALID로 끝내고 자기 child를 회수한다.
- 검증용이 **슬롯 해제 확인 뒤** 독립 실행한다. 이번 SOURCE OK가 EVIDENCE OK는 아니다. 기능/로그/매칭 실패가 나오면 그 고정 결과를 같은 task에서 조사한다. marker 확인이나 순수 9/9만으로 실제 record e2e 성공을 선보고하지 않는다.

이 독립 lane은 G1의 **일반 OS 효과 수행 Host**를 자동으로 시작할 권한이 아니다. 9개 보류·구 Host NOT RUN·real-input 4개 기본 REFUSED·기존 prelogin 명시 금지는 유지한다. 결정5는 아직 설계이며, 실제 foreground/credential 자동화 구현이나 실제 OS 인증 성공으로 처리하지 않는다. 결정④의 순수 fixture는 선택 보충이고 이번에 만들지 않았다는 상태를 수용한다.

#### Codex 직접 확인

`.claude/codex_test_injection_review/ba2/`에 고정 원문과 필요한 header 의존성을 보존했다. 아래는 전체 제품 빌드가 아닌 **기록/순수·API 대역 review exe**다. 실제 Host/Viewer/network/IME/UIA 쓰기·입력/전원·삭제는 0이다.

| 직접 실행 | 결과 | exe SHA-256 |
|---|---|---|
| host_power_policy_record_test + 고정 host_args.cpp | **27/27 PASS** | 32478abe915eb01c55eb77e1b83cf0dc61b89c92cc87c2c1d1709ea1106c9bcc |
| e2e_injection_guard_test | **20/20 PASS** | 0664a27db30012f18472f2e5b126a84c6ac909f5ff3601c13b74105014306366 |
| os_input_record_match_test | **9/9 PASS** | 2beffa4584c2110ee1a9d349174a13db7c143cae2400789434fa75ace20fcb23 |
| normal_wrapper_probe, record macro 없음 | 새 API 5개 위임·인자/오류/반환과 빈 DispatchContext **PASS**, 실제 쓰기 0 | a38bf4b40af2b1ba4bf3a28d9223c99de559dbfe576acbc7e7511f0ec7a043cc |
| late_power_probe | 초기/full-state는 정상 거부하나 사후 helper는 false인 반례 | 0e634f0fae747ccd2eae41562f056b32c96ed2dbd6ff606483ec1df4feb64ce3 |
| fatal_port_probe + 고정 host_args.cpp | 0 port 입력→실제 bindPort 43000/candidates 0 반례 | 0de8d9d0db0a1c9a88c293a72fc88fecaa84892e30737b29f9d6be37c3d06266 |

출하 gate 52/52와 작업용 제품 빌드는 보고 인용이다. Codex는 전체 제품/Host 시험·금지 시험·운영/설치·commit/push를 수행하지 않았다. 검증용은 **G1+fatal F를 기존 작업용1 정본의 한 보수로** 연결하고, 위 record lane은 그와 독립적으로 다음 담당/슬롯을 이어 갈 수 있다. 불필요한 wake/표준 선택 재승인이나 닫은 wrapper/Unicode 지적의 반복을 요구하지 않는다. 사용자 현재 조치 없음.

### 후보 갱신 f4689df — legacy fixture SOURCE OK, 제한 구성 단위 확인

고정 **f4689df8606523dc9a0d4ded1702592ee8d15275**, 신규 **ba2eaf2..f4689df**의 CMake·standby_legacy_host_test.cpp 두 파일만 검토했다. tracked clean, report SHA-256 **ab7faf33241537c0725865d09bfacaf5b21de689c0033af64810434c2c0fc1f4** 일치. **추가 fixture는 SOURCE OK**다. 실제 제품 Host·영속 generation store·앱을 대신 띄우지 않고 메모리 store와 loopback UDP 무응답 상대에 제품 StandbyLease/StandbyRunner를 연결한다. 상대가 Unsupported를 응답하거나 결과 enum을 직접 돌려주는 stub은 없다.

**Codex도 고정 원문과 header 의존성 16개를 별도 보존/빌드해 13/13 PASS를 직접 확인했다.** 원문·산출물은 `.claude/codex_test_injection_review/legacy_f468/`, exe SHA-256 **365cba71f41905e77362633e61384fa2334fc92e5654a412e26d9ffc3345bea4**다. 이 추가 fixture에 대한 독립 실행 근거로 사용할 수 있으며, 같은 13개를 단순 중계 목적으로 다시 돌릴 필요는 없다.

직접 관측한 범위:

- lease admission에서 **StandbyHello 6회 뒤 Unsupported**, 이어 **2.5초 관측 구간의 추가 datagram 0**. ActivationArgs 거부와 그 시점의 StandbyActivate/ordinary Hello 0.
- admission 중 Hello 2회 관측 뒤 같은 test thread에서 Release하고 **3초 동안 추가 Hello 0**, Activate 0, activation args 없음. 이 case는 **Release와 Pump의 동시 경합 시험이 아니다.** “즉시 정지”를 무한 시간/모든 thread 순서의 증명으로 확대하지 않는다.
- runner가 Unsupported를 게시하고 TakeActivation을 거부하며 Release 뒤 size 0. 전체 run의 StandbyActivate 0을 확인한다. **ordinary Hello 0 assertion은 첫 lease 단계에 있다.** 전체 runner run까지 동일한 assertion을 검사했다고 보고하려면 마지막에도 해당 집계를 확인해야 한다. 현재 판정은 실제 assertion 범위로 한정한다.

이것은 **프로세스 없는 protocol fixture이며 loopback socket과 thread는 실제 사용**한다. 무네트워크 순수 계산이라고 부르지 않는다. 실제 GNLinkStream/Viewer·사용자 입력/전원·clipboard·구 게시본·금지 prelogin 시험은 실행하지 않았다. fixture는 파일/registry를 쓰지 않고 메모리 store를 사용한다. review source/산출물만 저장소 안에 보존했으며 삭제·제품 전체 빌드·운영/설치·commit/push는 없다.

**제한:** 무응답 모델은 client가 침묵에 반응하는 근거다. 실제 0.2.163의 binary/wire를 새로 확인한 결과도, 무응답만으로 상대가 구 버전이라고 진단한 결과도 아니다. 유실/도달 불가/새 Host 무응답과의 구별을 이 fixture가 증명하지 않는다. 실제 구 Host 호환성·NAT·소켓/앱 무간섭은 계속 NOT RUN/미검증이며 이전 held EVIDENCE는 복구하지 않는다. Kotlin **StandbyKeep.onTap(UNSUPPORTED)=Cold(releaseFirst)**는 별도 기존 단위/소스 근거이며 이 C++ 실행에서 호출하지 않았다. 실제 APK 사용자 tap/연결 완료를 주장하지 않는다.

제품 코드는 ba2와 같으므로 **앞 제품/record 경계 SOURCE OK와 지정 record e2e 독립 lane 허용은 유지**한다. 이 시험 추가 때문에 그 제품을 전량 재검사/재빌드할 필요는 없다. 실제 사용 artifact의 기존 source/hash 연결은 보존한다. **일반 9개 G1·fatal F의 NEEDS_CHANGES, 구 Host NOT RUN·real4 기본 REFUSED·기존 prelogin 금지는 그대로**다. 다음 담당은 기존 검증용→작업용1의 G1+F 보수와 독립 record 실행이며, 별도 legacy task/VM 승인 왕복을 만들지 않는다. 사용자 현재 조치 없음.

### 후보 갱신 85f9383 — runtime 경로 수용, 종료 구간/일관된 INVALID는 NEEDS_CHANGES

고정 **85f9383**, 비교 **f4689df..85f9383**의 13파일을 확인했다. 변경은 시험/CMake뿐이며 제품 소스 0, tracked clean이다. report SHA-256 **dedbb041e8a31266ae367ef2c81d9da58d07bdae29856db4c7e03aa9f50f8747** 일치. **일반 8개 + fatal 전체 SOURCE는 아직 NEEDS_CHANGES**다. 아래 닫은 포트/회수/실행 중 판독은 재작업하지 않고 두 종료 경계를 보수한다.

#### 수용한 변경과 직접 확인

- e2e_injection_contradicted가 full-state 판독을 재사용하여 **뒤에 나온 power ON/다른 input mode와 injectionEnabled=1**을 거부한다. 이전 사후 helper의 power 누락은 닫는다.
- 8개 일반 하네스의 초기 gate 뒤 E2eInjectionMonitor 생성과 200ms poll, Stop/최종 helper 호출이 **실제 코드에 연결된 것**을 수용한다. 모순 시 자기 job의 종료를 요청하고 ActiveProcesses=0 확인 여부를 출력한 뒤 INVALID exit 2로 중단하는 경로도 확인했다. 다만 아래 N1 때문에 이를 “launch의 최종 출력까지 확인”으로 확대하지 않는다.
- e2e_free_loopback_udp_port가 socket/bind/getsockname 실패를 확인하고, fatal의 포트가 e2e_port_or_invalid를 거쳐 **0에서 기동 전 INVALID**가 되는 변경을 수용한다. 제품 기본 43000 fallback으로 진행하던 원 반례는 닫는다. 확보 후 실제 bind 사이의 일반 port race가 없어진 것은 아니다.
- 공용 adoption 실패의 Terminate/Wait 결과를 구분한 출력, e2e_end_child_confirmed의 bounded 실제 종료 관측, prelogin worker 종료 미확인 시 log root/stdout 보존을 수용한다. 원 prelogin timeout의 Wait 누락을 닫는다. 종료 요청 자체를 완료로 세지 않는 방향은 유지한다.
- **고정 runtime test를 Codex가 별도 빌드/직접 실행하여 10/10 PASS**를 확인했다. 입력/전원 API·실제 GNLinkStream/Viewer는 사용하지 않았다. 자기 sleeper child·job·event와 loopback 포트 API를 사용하는 시험이며, 무프로세스/무네트워크 순수 계산이라고 부르지 않는다. 작업용 ×3과 구분해 **Codex는 한 번** 실행했다.

#### N1 — monitor가 생산자/reader보다 먼저 끝나 종료 출력이 빠짐

구조체형 Host 4개의 Stop은 **injMonitor->Stop()를 첫 줄**에 둔다. 그 뒤 reading=false, job close/Host 종료 대기, reader join을 한다. 단일 Host의 bwe/control_deadline/two_dispatcher도 monitor Stop 뒤 job을 닫아 Host를 끝낸다. 이 시점에는 **Host가 아직 살아 있거나 stdout pipe의 마지막 출력이 reader에 남아 있을 수 있다.**

E2eInjectionMonitor::Stop은 그 순간 Check 한 번 뒤 **Registry에서 자기 자신을 제거**한다. 이후 e2e_injection_final_check_all은 살아 있는 monitor만 검사하므로 이미 Stop된 launch는 다시 보지 않는다. reader가 늦게 받은 상충 줄은 빠지고, 최종 helper는 0개를 보면서도 “no contradiction during the run” PASS를 출력한다.

고정 monitor/helper를 그대로 사용하고 실제 앱 대신 전체 출력 문자열에 마지막 reader flush를 모델링한 직접 probe:

```
PASS input / power state: no contradiction during the run (0 host(s) still monitored, judged again)
final_live=0 complete_output_contradiction=1 shutdown_tail_missed=1
```

**Host/출력 생산자의 종료 확인과 출력 수집 완료/EOF를 거친 최종 snapshot을 판정한 뒤에만 launch를 finalized로 만든다.** polling을 먼저 멈춰야 한다면 그 작업과 최종 판정을 분리하고, 지연된 pipe/file 출력을 수집한 뒤 같은 launch를 반드시 재판정한다. 이미 끝난 object의 callback/raw pointer를 무기한 남기는 식으로 해결하지 않는다. reader를 먼저 중지해 나머지 출력을 버리지 않게 한다.

job handle/log의 소유 수명도 최종 판정까지 연결한다. 실패 시 닫힌 handle을 재사용하거나 이미 해제된 Host에 접근하지 않는다. 최종 helper의 **0 active**를 그 자체로 모든 launch 완료/정상 근거로 출력하지 말고, 각 launch의 완료된 최종 판정과 구분한다.

부정 대조는 **초기 OFF→주행→poll Stop 시도→종료 직전/reader 마지막 chunk의 ON→최종 INVALID**, 자연 종료 직후 늦은 reader 수집, 파일 읽기/출력 수집 미확인 시 보존·invalid를 실제 wrapper 수명으로 확인한다. 사용자 PC의 실제 ON Host는 쓰지 않는다.

제공된 runtime test의 **final case는 monitor 생성 즉시 polling thread도 시작**하므로 final helper 단독 경로의 증명은 아니다. 이미 ON인 출력에 background Check 또는 Stop의 Check가 먼저 반응할 수 있다. phase/barrier로 polling과 종료 검사를 구분해 위 shutdown-tail과 final-only 반례를 결정적으로 확인한다. 기존 10개가 통과했다는 사실은 보존한다.

#### N2 — fatal의 모순은 전체 run을 INVALID로 중단하지 않음

fatal은 case 끝의 full-state 재판정을 추가했지만 **std/winrt/none은 check(false)로 gFailures만 증가**시키고 다음 Host case를 시작한다. run_with_client도 kInjectionInvalid를 돌려 caller의 check가 실패한 뒤 다음 case로 계속 간다. prelogin power 상충 역시 check 실패에 머문다.

이는 일반 monitor의 **상충→자기 child 종료 확인/미확인 기록→RESULT INVALID exit 2**와 다르고, 정책의 “상충을 발견한 run은 중지”를 만족하지 않는다. 포트 0의 기동 전 INVALID는 수용했지만 이 경로까지 닫힌 것은 아니다.

**fatal에서도 안전 상태 상충이 확인되면 뒤의 case/child를 더 시작하지 않고 run 전체를 INVALID로 끝낸다.** 해당 case의 자기 handle로 종료를 확인하거나 미확인을 명시하고, 종료/로그 수집이 미확인인 자료는 정리하지 않고 보존한다. 검사 대상이 이미 자연 종료한 child라면 그 확인 사실을 사용하며 존재하지 않는 job으로 종료 성공을 꾸미지 않는다. 정상 기능 assertion 실패와 안전 상태 상충은 구분한다.

정상 run/run_with_client에서 end_child_confirmed 반환을 (void)로 버린 뒤 log들을 일괄 삭제하는 경로도 **미확인 case의 자료는 보존**하도록 종료 상태를 caller까지 연결한다. prelogin에만 gKeepPreloginLog를 둔 것으로 모든 launch의 수명이 보장되지는 않는다.

새 대조는 실제 Host 대신 case launcher/log 대역으로 **첫 case의 상충→다음 launcher 호출 0·INVALID**, 초기 gate 뒤 상충/자연 종료의 상충 tail, 종료 미확인→그 case 자료 보존을 확인한다. 금지된 viewer_prelogin_login_e2e를 재개할 필요는 없다.

#### 실행 범위와 독립 근거

**이 고정 후보로 일반 8개 또는 fatal을 실행하도록 허용하지 않는다.** N1/N2를 기존 작업용1의 한 보수 패킷으로 연결한다. 제품 SOURCE·ba2 ordinary record e2e의 지정 lane과 f468 legacy fixture의 기존 수용은 유지한다. **record e2e는 승인된 ba2 source/기록 artifact hash·clipboard OFF·loopback/private staging/job 및 슬롯 해제를 확인한 뒤 독립 실행 가능**하며, 일반 OFF monitor를 logical injection enabled인 record target에 그대로 붙이지 않는다.

직접 review 산출물은 `.claude/codex_test_injection_review/guard_85/`에 보존했다.

| 직접 실행 | 결과 | exe SHA-256 |
|---|---|---|
| e2e_guard_runtime_test | **10/10 PASS**, sleeper child/job·문자열 상태·loopback port·종료 미확인 경로 | 2becb10621e2fea4bf35e1c6cb07c2c9212eeef8f81ba6716059f56649fdea53 |
| shutdown_tail_probe | 고정 monitor Stop→지연된 ON tail→최종 registry 0/PASS 누락 재현 | c2f7534f29dfceab4ba8d0a5ee0da9a211dca7222d8a262ca7a62415b01e0791 |

실제 제품 Host/Viewer·입력/전원/clipboard·금지 prelogin·real4·구 게시본은 실행하지 않았다. review 파일 외 사용자 자료 변경·삭제·전체 제품 빌드·운영/설치·commit/push는 없다. 다음 담당은 검증용→기존 작업용1의 N1/N2 보수, 그리고 독립 record lane이다. source 검토를 이유로 슬롯을 임의 해제하거나 승인된 독립 작업을 다시 선택 대기로 돌리지 않는다. 사용자 현재 조치 없음.

### 후보 갱신 a8b3407 — 종료 tail/등록 수명은 해결, 읽기 완료와 worker case는 NEEDS_CHANGES

고정 **a8b34073cf1793b895dab475b10accb1b77f00dc**, 비교 **85f9383..a8b3407**의 시험 12파일을 읽었다. tracked clean·diff --check 정상. report SHA-256 **aa1b21caa6033acc9fa89ea004badd522aca48074df53cd427325321ffea216f** 일치. **일반 8개 + fatal 전체 SOURCE는 NEEDS_CHANGES 유지**다. 제품/ordinary record lane·legacy fixture의 이전 수용은 바뀌지 않는다.

#### 닫는 범위

- **StopPolling과 Finalize 분리**, final state의 shared 소유·모든 launch 등록 유지, abandoned/incomplete의 INVALID를 수용한다. 85의 “Stop 뒤 registry 0이라 tail을 검사하지 못함” 원 반례는 닫는다. callback도 Finalize/destruction에서 끊어 끝난 Host object를 계속 참조하지 않는 방향이다.
- 구조체형 Host가 정상 종료를 확인한 뒤 reader join을 기다리고 Finalize로 넘어가는 **순서 변경**을 수용한다. 파일형 Host도 producer 종료를 먼저 확인하는 방향으로 바뀌었다. 단 아래 N1처럼 **정말 EOF/read success였는지의 증거**는 아직 없다.
- fatal의 **std/winrt/none·run_with_client INVALID**가 e2e_case_invalid로 전체 exit 2/후속 launch 0/자료 보존에 연결된 범위는 닫는다. 종료 미확인 tag의 log를 일괄 삭제에서 제외하는 변경도 수용한다. 아래 prelogin-worker 분기는 남아 있다.
- **Codex가 고정 runtime test를 별도 빌드해 16/16 PASS를 직접 확인**했다. final-only가 poll=false로 분리되고, tail/abandoned/incomplete·case INVALID가 실제 helper에서 검증되는 개선을 확인했다. 작업용 ×3과 구분해 Codex는 한 번 실행했다. 실제 제품 Host/Viewer/입력/전원은 사용하지 않았다.

#### N1 잔여 — producer 종료와 join을 읽기 완료로 간주

현재 구조체형 Stop의 **outputComplete는 e2e_end_job_confirmed(job)의 결과뿐**이다. reader는 `while(reading && ReadFile(...) && got>0)`에서 빠져나오지만 **정상 EOF와 ReadFile 오류를 구분/전달하지 않는다.** join은 thread가 끝났다는 뜻이며 올바르게 끝까지 수집했다는 증거는 아니다.

파일형 4개도 hostEnded만 Finalize에 넘긴다. **e2e_read_text_file은 open 실패에 빈 문자열, ReadFile 실패에 그때까지의 일부 문자열을 반환하며 read 성공/정상 끝 정보를 버린다.** Finalize의 Judge는 e2e_injection_state<0만 모순으로 보므로, 초기 gate 뒤 최종 읽기가 실패해 상태가 0이 돼도 finalized=true로 기록한다.

고정 helper와 실제 없는 review 파일 읽기를 사용한 직접 반례:

```
initial_OFF=1 final_state=0 final_read_bytes=0
finalized_launches=1 missing_capture_passed=1
PASS ... 1 judged on their complete output
```

probe가 Finalize에 true를 넘긴 이유는 **현재 caller도 job 종료만으로 true를 넘기기 때문**이다. Finalize(false)가 잘 거부되는 16개 시험과는 다른 실제 callback/caller 경계다.

**outputComplete를 producer 종료 AND 정상 EOF/파일 최종 읽기 성공으로 계산**한다. pipe reader는 정상 종료 조건/오류를 기록하고, file getter는 open/read 실패와 완전한 snapshot을 구분해 caller/Finalize에 전달한다. 최종 ordinary snapshot에는 초기 gate가 확인했던 두 OFF 상태도 일관되게 남아 있어야 한다. 단순 함수 호출 두 번으로 성공/자료를 따로 읽어 놓치는 방식보다, 검증한 **같은 최종 snapshot과 상태**를 판정에 사용한다. 수집 미확인/오류이면 INVALID·자료 보존이다.

또한 구조체 4개는 **job handle을 닫은 뒤 Finalize**하며 State에는 그 borrowed 값이 남는다. tail 상충/불완전 판정의 invalid 경로가 닫힌 handle을 다시 종료 대상으로 쓰지 않게, job 소유/확인된 종료 상태를 최종 판정까지 연결한다. 이미 종료가 확인됐다면 그 사실을 보고하고, 미확인이라면 유효한 자기 handle로만 회수한다.

부정 대조는 실제 Host 없이 **초기 OFF→producer 종료→파일 open 실패/부분 read 실패/pipe 비정상 종료→INVALID**, 정상 EOF와 성공 snapshot→finalized PASS를 실제 read wrapper에 연결한다. 기존 tail/final-only/abandoned는 유지하고 무관 suite를 전량 다시 돌릴 필요는 없다.

#### N2 잔여 — fatal prelogin-worker의 상충은 여전히 check 실패 후 계속

fatal의 독립 **--prelogin worker** 절은 아직 다음과 같다.

```
check("...worker said its power management OFF", said_power_off(read_all(log)));
// 조건에 따라 log root 정리
// 그 뒤 probe_trap 등 다음 Host case 시작
```

이 분기에 fatal_invalid/full-state 상충 판정이 없다. **power OFF 뒤 ON**이면 said_power_off가 false여도 gFailures만 증가시키고 자료 정리·다음 launch로 진행한다. 이는 85 판정에서 명시했던 prelogin power 상충 경로이며, std/winrt/none/run_with_client를 고친 것으로 닫히지 않는다.

**worker case도 삭제 이전의 최종 read 상태·power/input 안전 상태를 판정해 상충이면 즉시 전체 INVALID**로 연결한다. workerGone/ended 미확인도 해당 tag의 상태로 e2e_case_invalid에 정확히 전달한다. 이 worker는 configure fault라 input OFF 줄 전에 끝날 수 있다는 기존 예외와, 실제 출력에 상충 상태가 나타난 경우를 구분한다. 테스트를 통과시키려고 worker 권한/고정 계약을 바꾸지 않는다.

대조는 기존 case/log 대역에 **prelogin-worker tag**를 포함해 **상충→다음 launcher 0·삭제 0·exit 2**, 종료 미확인 표시/자료 보존을 확인한다. 금지된 viewer_prelogin_login_e2e를 실행할 필요가 없다.

#### 실행 범위·직접 근거

**일반 8개·fatal의 실행 범위는 아직 열지 않는다.** 위 N1/N2만 기존 작업용1의 같은 정본에서 한 묶음으로 보수한다. 제품 SOURCE·ba2의 지정 ordinary record e2e 독립 lane·f468 legacy fixture의 수용과 사용자 명시 prelogin 금지·real4 REFUSED·구 Host NOT RUN은 유지한다. record는 기존 고정 artifact/설정·격리/job·슬롯 해제 조건에서 진행할 수 있다.

`.claude/codex_test_injection_review/guard_a8/`에 고정 원문·의존성과 review 산출물을 보존했다.

| 직접 실행 | 결과 | exe SHA-256 |
|---|---|---|
| e2e_guard_runtime_test | **16/16 PASS**, poll/final/tail/abandoned/incomplete/case invalid·포트/종료 helper | e9e902fd73dd0289b0a47435dfc8263fc340777af54590527330567d0036e57d |
| missing_capture_probe | 실제 최종 file open 실패→빈 출력인데 finalized PASS 재현 | 69e18d62b83bb3b1ac157a475a32976fa4b39610d0fe3d4449e2a6d43a91609f |

runtime test는 자기 sleeper child/job/event·loopback port API를 사용했다. probe는 review 위치에서 실패하는 파일 읽기뿐이다. 실제 GNLinkStream/Viewer·사용자 입력/전원/clipboard·금지 시험·제품 전체 빌드·APK 설치·운영/삭제·commit/push는 하지 않았다. 사용자 현재 조치 없음.

### 후보 갱신 b8dcc93 — 일반 8개 + fatal의 안전 SOURCE OK, runtime 임시파일 경계는 별도 보수

고정 **b8dcc930ea1a2e99951ec7fbcd5fa339d5a04c3b**, 비교 **a8b3407..b8dcc93**의 시험 11파일을 검토했다. tracked clean·diff --check 정상, 제품 소스 변경 0이다. 새 packet **report_inj_r8.md** SHA-256 **2a78a3b32eaa828144d2006d2ae5440ba7e450842813bf7046f71eae6084bba2** 일치.

**일반 8개와 fatal의 입력/전원·출력 최종 판정/자식 수명 안전 SOURCE 관문은 OK**다. 기존 기능 자체의 최종 EVIDENCE/출하 승인은 아니다. 새 runtime 시험의 파일 fixture에 아래 별도 결함이 있어 **전체 commit의 무조건 main merge/기본 runtime 실행까지 승인하는 것은 아니다.** 영향 범위를 분리한다.

#### 닫는 N1 — 실제 read 상태·동일 최종 snapshot·job 수명

- e2e_read_to_end가 **정상 EOF(성공 0 byte 또는 ERROR_BROKEN_PIPE)**, 다른 read Error(코드와 부분 byte), 소유자 Cut을 구분한다. ERROR_MORE_DATA의 일부 데이터를 버리거나 EOF로 승격하지 않는다.
- file open/read 성공과 confirmed EOF를 e2e_final_file에서 한 snapshot으로 만들고, pipe reader도 readResult/readErr와 join 뒤 수집한 텍스트를 e2e_final_pipe로 넘긴다. **job 종료만을 output complete로 치던 반례를 닫는다.**
- Finalize는 **producerEnded AND final.complete AND 그 텍스트의 두 OFF 상태 AND 무상충**을 검사한다. 빈 EOF 파일도 통과하지 않는다. 같은 final snapshot을 재사용하며 callback을 다시 읽어 성공/자료가 엇갈리게 하지 않는다.
- 구조체형 4개의 job CloseHandle이 Finalize 뒤로 이동했다. monitor는 finalize/destruction에서 borrowed job을 놓고, registry는 완료 상태/byte 수를 보존한다. 닫힌 handle 재사용과 0 live를 전체 완료로 부르던 문제도 닫는다.

#### 닫는 N2 — startup-fault/worker의 전체 INVALID

- std/winrt는 종료·EOF·power OFF 필수인 startup_fault_verdict를 사용한다. input 줄 부재는 해당 configure fault의 예외로 명시하며, 실제 input 상태가 상충하면 거부한다.
- none은 정상 대조이므로 input/power **두 OFF 줄 모두 필수**다. 예외를 일반 launch로 확대하지 않는다.
- **prelogin-worker stdout을 삭제 전에 같은 startup-fault 판정으로 검사**하고, own log도 read 완료/무상충을 검사한다. 종료 미확인 tag가 gKeepLogs에 연결돼 fatal_invalid가 그 상태를 정확히 말한다. 거부 시 다음 probe_trap 등으로 진행하거나 자료를 삭제하지 않고 전체 INVALID exit 2다. 원 worker 분기 누락을 닫는다.
- run/run_with_client의 final_output_or_invalid가 종료/최종 read 미확인을 중단시키는 변경도 수용한다. 이미 승인한 포트 0 기동 전 INVALID·자기 handle 회수·비재귀/신규 run 격리는 유지한다.

#### 별도 T — 새 runtime test의 기본 %TEMP% 파일 생성·삭제

e2e_guard_runtime_test.cpp의 **temp_file(:57–67)**는 GetTempPathW를 검사 없이 사용해 **CREATE_ALWAYS**로 파일을 만들고, **:170/:188/:407의 DeleteFileW**로 지운다. 경로가 repository 안인지/신규 소유 파일인지 보장하지 않는다. 기본 %TEMP%가 저장소 밖이면 **AGENTS.md 최상위 정책**인 “repository root path를 벗어나는 범위는 절대 삭제커맨드를 실행하지않는다”를 어긴다.

따라서 **이 runtime exe의 기본 환경 재실행은 보류**다. 기존 작업용1이 정확한 repository test root·non-reparse·신규 소유 run·생성 결과 확인과 파일별 소유/삭제 guard를 연결한다. 환경을 D:로 우연히 설정해 둔 것만을 source guard로 대신하지 않는다. CREATE_ALWAYS로 기존 외부 파일을 덮지 않게 한다. 과거 작업용 실행의 실제 TempRoot/삭제 여부는 이 source 읽기만으로 확정하지 않으며, 확인 가능한 기록만 남기고 바깥 cleanup은 하지 않는다. 이 발견은 즉시 기존 검증용에 통지했다.

**이 결함은 새 runtime 시험 cpp의 fixture에 있다.** 일반 8개/fatal의 Source 변경과 분리하며 그 launcher들을 다시 수정하거나 안전한 기능 검증까지 대기시킬 이유로 확대하지 않는다. main 통합에는 이 별도 보수를 포함하고, 전체 commit 승인/기본 runtime 실행은 그 고정 보수 뒤에 연결한다.

#### Codex 직접 실행 — 원본 runtime 36개, repository 안에 제한한 환경

기본 환경 실행 대신 고정 원문/의존성을 별도 빌드하고, **새로 만든 repository 내부 전용 root**를 해당 review process와 그 children의 **TMP/TEMP**로 지정했다. 별도 Win32 probe로 **GetTempPathW의 실제 반환값이 그 root와 정확히 일치**함을 먼저 확인했다. root의 resolve가 repository 안이고 non-reparse인지 확인했으며 다른 자료는 사용하지 않았다. 기존 원본 runtime cpp/guard 코드는 수정하지 않았다.

그 환경에서 **36/36 PASS, exit 0**을 직접 확인했다. 없는/빈 최종 file·ERROR_MORE_DATA 부분 read·Cut·producer 미확인·startup-fault 수용/거부·양성 EOF·기존 runtime phase를 포함한다. 작업용 ×3 및 mutant a~d는 보고 인용이며 Codex는 **한 번** 실행했다. 이 결과는 **기본 임시파일 경로의 안전성 증명은 아니다.**

- review folder: `.claude/codex_test_injection_review/guard_b8/`
- 원본 runtime exe SHA-256: **688bc36bb7ca2268fc35bc83f50d0e1e79eacae43f6cdbc52866f886fff763e6**
- 실제 log: `guard_b8/run_private_tmp.log`; 환경 root 기록: `guard_b8/private_tmp_root.txt`.
- 실행 후 전용 root의 남은 파일 **0**, root 자체는 review 근거로 보존했다. runtime이 삭제한 fixture 파일은 이 검증된 repository 내부 root의 자기 파일뿐이다. 재귀 삭제는 없었다.
- 실제 GNLinkStream/Viewer/AVD·입력/전원/clipboard·금지 prelogin·real4·구 게시본은 실행하지 않았다. sleeper child/job/event·loopback port·file/pipe read API는 사용했다. 제품 전체 빌드·운영/설치·commit/push는 없다.

#### 검증용이 진행할 수 있는 범위

**슬롯 해제와 고정 build/artifact 매핑을 확인한 뒤, 아래 기존 case의 독립 실행은 추가 go 없이 진행 가능**하다. Source 관문 통과이며 실제 실행 결과는 별도 EVIDENCE로 남긴다.

| 허용된 기존 하네스 | 범위 |
|---|---|
| host_standby_admission_e2e | 새 검수 candidate의 product-client/real-directory/relay/standby case. **old-host는 계속 NOT RUN** |
| host_punch_reply_e2e | 기존 fake directory·직접/이동/ack/token/expiry/late case. old binary override는 사용하지 않음 |
| host_migration_e2e | 검수된 MigrationTest와 소유 fake directory의 기존 등록/실패 case |
| host_reboot_delegate_e2e | 기존 권한 제한·자기 service 대역·요청/응답 계약 case 그대로 |
| android_session_reboot_e2e | 기존 unauth/refused 응답·중복 송신 없음·세션 유지 case 그대로 |
| bwe_observe_e2e | 기존 자기 pattern/capture·bandwidth 관측 case, 기존 슬롯/창 격리 유지 |
| host_control_deadline_e2e | 기존 control 교환/응답 유한성 case |
| host_two_dispatcher_e2e | 기존 TCP/UDP dispatcher 상호작용 case |
| host_fatal_exception_test | std/winrt startup fault·none 정상 대조·자기 log root의 **worker 첫 configure fault**·bare Hello 뒤 probe/wrapper/wgc/normal 기존 case |

공통 조건은 **이 Source를 지원하는 고정 Host/MigrationTest·정확한 input none/power off·client 전 해당 launch의 두 OFF gate·실행 중 monitor·종료/EOF/동일 final snapshot·job/자기 child 수명·repository 내부 소유 fixture**다. fatal의 no-client configure fault는 문서화한 input 줄 부재 예외만 적용한다. 누락/상충/미완료/종료 미확인은 INVALID·보존이며 PASS로 합산하지 않는다. 비용/기능 실패도 해당 범위로 분리한다.

이 목록의 fatal worker fault는 **viewer_prelogin_login_e2e 재개가 아니다.** 그 원 하네스는 전 모드·wrapper 사용자 명시 재개 전 계속 금지다. real4 기본 REFUSED, 구 게시본의 실 실행 미검증도 유지한다. 실제 reboot·설치/재시작·사용자 credential/clipboard 조작으로 기존 fake/제한 fixture를 대체하는 범위는 열지 않는다. PC/AVD의 다른 launcher 전체가 이 목록만으로 자동 승인된 것도 아니며, 최종 통합 task의 source/guard 연결 근거로 기존 창구에서 진행한다.

기존 ba2 **ordinary os_input_record_e2e**의 지정 record artifact/clipboard OFF/격리·job·슬롯 조건과 f468 legacy fixture 수용도 유지한다. 검증용은 **8개+fatal 독립 실행·record lane**을 기존 task에 연결하고, runtime 기본 fixture의 T 보수는 같은 작업용1 정본에서 병행할 수 있다. Source 성공을 기존 held 실제 Host EVIDENCE 복구/기능 출하 승인으로 대신하지 않는다. 사용자 현재 조치 없음.

### 후보 갱신 d665957 — runtime fixture SOURCE OK, 슬롯과 독립된 1회 실행 가능

고정 **d6659575f9b1b8b4198651bc77ae0113a1c4931f**, 비교 **b8dcc93..d665957**의 e2e_test_root.hpp·e2e_guard_runtime_test.cpp 두 파일(+216/-13)을 검토했다. tracked clean·diff --check 정상, 제품 소스 0이다. report SHA-256 **1bdca86a81e38386d13a13cbe4560e80e7584cb209ac4b5d3213e8d57085ab56** 일치.

**현재 runtime test의 고정 호출/파일 경계는 SOURCE OK**다. b8에서 보류했던 기본 %TEMP% 파일 fixture 항목을 닫는다. 기존 b8의 지정 일반 8개+fatal Source 수용과 record/legacy lane도 유지한다. 기능 전체/실 Host/실기·출하 EVIDENCE 승인은 별개다.

#### 수용한 경계

- 기본 실행은 **REMOTE60_TEST_TMP의 정확한 D:/remote/remote/.claude/test-tmp 바로 아래 plain root**를 요구한다. 고정 anchor/기존 구성요소의 reparse 검사를 먼저 거치고 fallback을 사용하지 않는다. 설정 없는/다른 부모의 기본 실행은 생성·삭제로 진행하지 않는다.
- parent가 **새 guard-runtime-PID-tick-attempt** 폴더를 만들고, file-using child는 넘겨받은 run을 root/prefix/plain directory로 재검증한다. 기존 run을 새 run 생성 성공처럼 채택하지 않는다.
- **CREATE_NEW**를 사용한다. 현재 세 고정 tag의 파일 경로만 만들고, delete_run_file의 현재 호출부에는 **자기 run_file이 성공해 반환한 경로**만 들어간다. 생성 실패의 빈 경로는 삭제하지 않으며, direct-child 이외 경로도 거부한다. 이는 현재 내부 호출의 수용이며 함수 자체를 임의 파일 정리 API로 넓혀 쓰라는 의미는 아니다.
- run directory는 남긴다. GetTempPathW/%TEMP%·CREATE_ALWAYS에 의존하던 파일 경로를 제거한 것을 source에서 확인했다. 코드/경로 추정만으로 실제 삭제 성공을 단정하지는 않는다.

#### Codex의 제한 독립 확인

고정 source/의존성을 `.claude/codex_test_injection_review/root_d665/`에 보존하고 원본 runtime과 별도 root probe를 빌드했다. probe는 **원본 cpp의 main만 이름을 바꿔 포함**하여 실제 root/run_file/delete_run_file 함수를 호출했다. 원본 runtime 36개 전체는 이번에 다시 실행하지 않았다(b8 helper 동작의 Codex 36/36은 앞의 제한 환경 실행 근거로 유지).

실행 전 실제 anchor부터 drive까지 기존 경로의 조회 성공·directory/non-reparse를 확인했고, 짧은 ASCII 절대 root를 새 이름으로 지정했다. root 이외 자료는 사용하지 않았다.

```
read_only_invalid_roots_refused=4_of_4
new_run_valid=1
collision_refused_and_original_preserved=1
own_file_removed=1
```

- invalid 4개는 unset·저장소 밖 이름·anchor 자체·anchor 밖으로 정규화되는 경로이며 **create=false의 읽기 전용 판정**만 호출했다. 바깥 파일/폴더를 만들거나 지우지 않았다.
- positive는 새 repository root/run을 만들고 child 검증 함수로 재검사했다. 자기 파일 CREATE_NEW, 동일 tag/PID 충돌 거부·원 내용 보존, 자기 파일 삭제를 확인했다. root/run은 보존했다.
- probe exe SHA-256 **8eaf8da3485af7a536c177eae7a462c2e386ee137e83f9a7b4b001f014eaac44**. log는 `root_d665/root_owned_probe.log`다.
- 원본 runtime의 review build SHA-256 **7771c56a3fd59de5db54262c8d7e93903ee83e085a98d64957921e0040472999**는 **빌드만** 했다. 작업용 artifact **09678227…6766B**와 동일 파일이라고 주장하지 않는다.
- 이번 직접 probe는 Host/Viewer/GPU/network/input/power/clipboard 0이다. 파일 I/O는 검증된 repository 내부 자기 root뿐이다. 실제 %TEMP%/사용자 profile 열람·삭제, 제품 전체 빌드·운영/설치·commit/push는 없다.

#### 요청한 실행 허용

**작업용1/검증용은 이 runtime test를 슬롯과 독립적으로 1회 실행해 보고할 수 있다. 추가 go는 필요 없다.** 이 한정은 actual GNLinkStream/Viewer/AVD·GPU·입력/전원 API를 쓰지 않고 **자기 sleeper/job/event·문자열/pipe·loopback port**를 사용하는 고정 시험의 특성에 따른다. GPU/실 Host 시험까지 슬롯 예외를 넓히지 않는다. 슬롯 파일을 지우거나 변경할 필요도 없다.

실행은 **REMOTE60_TEST_TMP=D:/remote/remote/.claude/test-tmp/<새 ASCII 이름>**의 **짧은 절대 경로**로 한다. 기동 전 예상 root/기존 경로의 실 directory/non-reparse와 actual binary/source hash를 확인하고, 새 run 경로·rc·36개 결과·종료 미확인 여부·남은 fixture를 기록한다. 경로/생성/읽기/삭제 상태를 확인할 수 없으면 성공으로 꾸미지 않는다. 삭제 실패나 partial file이 남으면 그 root 안에 보존·기록하며 밖으로 cleanup을 넓히지 않는다.

현재 source/정상 경로에서 수용한 판단을, 조회가 실패한 경로까지 non-reparse가 확인된 것처럼 확대하지 않는다. 이 helper를 다른 profile/정리 경계에 재사용할 때는 그 caller의 입력/실제 경로/오류 처리도 함께 검토한다.

#### 과거 기록과 WebView 목록의 관계

- 작업용 보고의 **r8 전체 실행 9회·파일 3개씩**은 source와 보존 run 출력에 근거한 이력으로 기록한다. **27회의 생성/삭제 예정 경로·호출 수**와 실제 생성·삭제 성공은 구분한다. 실제 당시 TempRoot/PID/파일명/삭제 성공은 미확인이다. 현재 패턴 0개는 과거 무영향이나 삭제 성공의 증거가 아니다.
- r6/r7 runtime에 그 temp-file 코드가 없었다는 source 사실을 기존 r8 이력과 분리한다. Codex가 r8을 실행한 한 번은 별도 **repository private TMP/TEMP 사전 검증** 환경이었으며, 작업용 보고 9회에 섞지 않는다.
- 사용자 temp/profile을 더 읽거나 정리하지 않는다. 확인 가능한 source·당시 tool/run 로그만 정본/history에 취합한다.
- **guard runtime의 단순 fixture와 WebView UDF는 별도 경계**다. r9로 `.claude/codex_ka_pc_test_src_r1.md`의 ce0a672 profile/child 미결이 닫히지 않는다. product shell은 **GNLinkClient**, macro window는 **GNLinkMacro**를 GetTempPath에 붙여 custom UDF를 요청한다. 같은 API를 쓴다고 실제 같은 UDF라고 합치지 않으며, 각각 environment/policy override·유효 UserDataFolder 검증이 필요하다.
- 목록의 다른 GetTempPath 사용처는 **사용 자체만으로 위반이라고 단정하지 않는다.** 실제 생성·삭제·사용자 data 경로·현재 필요한 test를 읽기 전용으로 분류하고, repository 밖 삭제 경로가 확인된 시험은 보수 전 실행하지 않는다. 제품의 정상 지속 profile 경로를 일괄 변경하거나 새 병렬 writer로 전수 수정하지 않는다. 현재 R2 필수 consumer와 이미 배정된 PC WebView test를 기존 창구에서 우선 취합한다.

검증용은 승인된 runtime **1회와 그 결과**를 기존 test-injection task에 연결한다. 별도 슬롯 대기/재승인으로 되돌릴 필요는 없다. 기존 PC 시험 profile 보수·APK X owed 보수·실 Host 검증은 각각 기존 task/범위대로 이어 간다. 사용자 현재 조치 없음.

### r9 runtime 1회 — 제한 EVIDENCE OK

**d665957의 승인된 runtime 구성 단위 EVIDENCE는 OK**다. 작업용1의 1회 실행, 검증용의 로그/잔류 확인과 Codex의 아래 읽기 대조를 연결한다. Codex가 이 full run을 재실행한 결과라고 부르지 않는다.

- 새 report SHA-256 **7368d04ac33c45897617033b2ffa095debb804b4961e599fc6854c43bd6f4d6b** 일치.
- 실제 제공 exe **C:/work/gnlink-wt/ka-r3/build/apps/native_poc/Release/remote60_e2e_guard_runtime_test.exe** SHA-256 **09678227b768e27f9bdd2983e4d49a9b596f023810e104cf134e7f6a0506766b**를 직접 대조했다.
- runtime/root/guard/child helper 네 파일의 `git diff d665957`은 0줄이었다. build provenance/실행 rc=0·실행 직전 root 준비/경로별 상태·잔류 process 0은 작업용/검증용 보고와 구분해 수용한다.
- 실제 log **C:/work/gnlink-wt/ka-r3/build/r9.runtime.run1.log** SHA-256 **adb529e5115dd93045ffead18644855232f206567aa9a1ff8b31bff4f36bde26** 일치. **^PASS 36**, 마지막 **36 checks, 0 failures / RESULT PASS**를 직접 확인했다.
- log가 기록한 run은 **D:/remote/remote/.claude/test-tmp/inj-r9-run1/guard-runtime-34008-504495218-0**다. Codex 현재 읽기 검사에서도 run이 존재하는 non-reparse directory이고 내부 항목 **0개**였다. folder는 보존한다.
- log의 NOT confirmed 두 항목은 **abandoned monitor/event handle의 의도된 음성 대조**이며, 실제 미회수 child 두 개라고 세지 않는다. 종료 미확인 child 0·Host/Viewer/AVD/GPU 0·슬롯 무접촉은 해당 실행 보고 범위다.

과거 9회의 **생성 시도 27/삭제 시도 27, 실제 성공·당시 TempRoot·PID 미확인** 정정을 채택한다. 정본의 앞부분에 남은 “27개 생성” 같은 옛 확정형 문장은 이 정정으로 치환해, 두 상충 문구가 계속 남지 않게 한다. 현재 temp 패턴 0개로 과거 성공/무영향을 증명하지 않는다.

이 component를 닫기 위해 같은 36개를 다시 반복할 필요는 없다. 새 failure/변경이 생긴 해당 범위에서만 추가 검증한다. **실 Host 8개+fatal/PC UI·WebView profile/APK X·NDK/AVD의 미검증·Source 미결 또는 기존 실제 Host EVIDENCE 보류를 이 결과로 복구하지 않는다.** 기존 승인 범위/다음 담당에서 각각 진행한다.

이번 Codex 작업은 report/exe/log hash·결과·현재 자기 run directory 읽기와 판정서 기록뿐이다. 제품/시험 실행·사용자 temp/profile 열람·생성/삭제·설치·운영·commit/push는 하지 않았다. 사용자 현재 조치 없음.

## 추가 — 출하 marker gate self-test의 기본 TEMP cleanup (2026-10-28)

관련 판정: `.claude/codex_avd_guard_src_r2.md` T1. `automation/gnlink_check_fixed_server.py --self-test`의 기본 `tempfile.TemporaryDirectory()` 생성·cleanup은 **repository 밖 삭제 경계 위반**으로 기록하고 보수한다. 기본 TEMP self-test 재실행은 중단한다. 정상 artifact의 read-only scan·서명/외부 hash·게시 사실까지 무효로 만들지는 않는다.

최신 report_gate_temp_T.md(acc42fcc…af25)의 정정을 채택한다. 해당 작업용 세션의 기본 TEMP self-test 호출은 **8건(10-26 4건, 10-28 4건)**이고, 앞의 57fa793 검증분 1건은 그 안에 포함된다. 프로젝트 전체 **43 호출/8 세션(9-29~10-28)**은 작업용의 보존 기록 조사 보고로 인용하며 Codex가 사용자 영역 JSONL을 재집계한 것은 아니다. 호출 수와 실제 실행 수·중복·check 수·전체 경로·파일 목록·개별 삭제 결과는 구분한다. 52/57은 check 수이며 폴더/삭제 횟수가 아니다.

같은 보고서에는 전후 특정 TEMP prefix 개수 조회 0→0도 적혀 있으므로, 내용 읽기 자체가 전혀 없었다고 하지 않는다. 제한 조회 사실과 추가 정리/복구 없음, 실제 과거 경로·결과 미기록을 구분한다. 당시 TMP 환경 값이나 현재 파일 부재로 과거 무영향을 확정하지 않는다. 사용자 TEMP의 추가 탐색·정리·복구나 과거 재현 실행은 하지 않는다.

self-test는 실제 허용 repository의 명시 test root, 생성 전 모든 상위 경로·링크·타입 확인, 자기 새 run 소유와 cleanup 경계를 갖추고 밖/unset/조회 불명에서는 생성·삭제 전에 거부하도록 보수한다. 기능 assertion 기록과 경계 준수 증거를 분리하고, 보수본의 새 근거를 별도로 남긴다. 상세 사고 이력은 검증용이 기존 ledger/history에 취합한다.

8a84198의 root 사전 검사·새 run·TEMP fallback 제거는 수용하지만 cleanup은 아직 NEEDS_CHANGES다. `.claude/codex_avd_guard_src_r3.md` T1: 실제 자기 junction → 자기 sibling sentinel과 **삭제 함수 기록 대역**으로 `os.walk(topdown=False)`가 junction 안으로 진입해 run 밖 파일을 삭제 대상으로 선택함을 확인했다. 실제 삭제 0, sentinel 보존이며 사용자/외부 경로를 대상으로 시험하지 않았다. 진입 전 reparse 검사·traversal 제외와 실제 directory-junction 대조가 필요하다. 현재 root_test의 일반 파일 link 대역은 이 경우를 검사하지 않는다.

### T1 후속 closure — af49c26 / feat 9f9a91c (2026-10-28)

**SOURCE OK, 해당 self-test 보류 해제.** af49c26은 topdown의 진입 전 검사/prune으로 실제 junction을 따라가지 않는다. Codex 직접 대조에서도 sibling sentinel 선택 0이며, 실제 guarded cleanup은 자기 새 파일만 지우고 sentinel/junction을 남겨 left로 보고했다. `.claude/codex_avd_guard_src_r4.md`에 근거를 연결했다.

Codex가 고정 feat 9f9a91c gate를 허용된 새 repository test root에서 실행한 결과는 **58 checks ALL PASS/rc 0**, 자기 run 제거 후 지정 root 내부 0개다. 작업용의 ka-r3 53 checks/root_test 20개와 실행 귀속을 구분한다. 기본 TEMP나 옛 cleanup의 실행 허가는 아니다. 과거 8/43 호출·실제 결과 미확인과 경계 위반 기록은 유지한다. J1도 같은 판정서에서 닫혔으며, 지정 AVD의 SOURCE 보류 해제와 실제 앱 EVIDENCE는 별도다.

## 일반 8종 독립 EVIDENCE r1 — 2026-10-29

**구성 단위 EVIDENCE OK: b8dcc93의 지정 일반 8종.** 검증용 r1의 admission 두 모드를 포함한 9개 명령은 모두 rc 0·기능 RESULT 통과이며, 19개 Host launch는 complete output의 종료/EOF/input·power OFF 두 줄/무상충 최종 판정을 확인했다. 명령 rc와 개별 child 종료 코드는 구분한다. Codex는 보고 hash·14개 실행 파일 hash·두 verify worktree의 고정 HEAD/clean·원문 로그/rc를 직접 대조했다. 상세 범위와 audit hash는 `.claude/codex_inj_general_evidence_r1.md`에 있다. Codex의 시험 재실행은 없다.

**fatal과 ordinary record e2e는 미통과로 분리한다.** fatal none 정상 대조는 exit 11/OFF 부재로 INVALID였다. no-FATAL assertion만의 PASS는 정상 대조가 아니며 전체 gate는 이 경우를 거부했다. record는 epoch 0 획득 실패와 실제 effect epoch 1 매칭 실패이고, 실패 뒤 Host 원문이 삭제돼 판독 결함을 확정하지 못한다. effect epoch를 expected 값으로 역추정하지 않는다. D1(권위 있는 session epoch·미획득 INVALID·실패 자료 보존), D2(정상 대조 설정·긍정 판정)를 같은 하네스 작업에 취합하고 두 시험만 한정 재검증한다. 일반 8을 이 이유로 전량 반복할 필요는 없다.

이 판정은 실제 OS 입력 주입·prelogin UI·PC/APK 실기·실 NAT나 최종 통합 출하의 승인이 아니다. 과거 unsafe 실행은 이 새 근거로 소급 승인하지 않는다. old-host/real4·금지된 viewer_prelogin_login_e2e 및 나머지 task의 미결은 기존 범위를 유지한다.

### r10 SOURCE — record/fatal 한정 재실행 (ccaf745)

**SOURCE OK: ccaf7453f366e2baa4045a426f33978bb27c61a7**, 제품 변경 0인 두 시험 보수다. `.claude/codex_inj_record_fatal_src_r10.md`에 첫 token-less session epoch 1의 source 불변식·별도 접속 로그 조건, 판독 불가 INVALID/실패 staging 보존, fatal env 설정 순서·정상 startup 긍정 조건을 검토했다. Codex의 Host 없는 판독 block 대조는 14/14 PASS다. 실제 두 하네스를 실행한 결과가 아니다.

기존 ka-pc 우선 뒤 slot 해제를 확인하고 ccaf745 독립 build로 **두 시험만** 새 r2 자료로 재실행할 수 있다. 기존 record-only/loopback/clipboard OFF 및 fatal 지정 case·출력/종료 안전 조건을 유지하며 별도 go는 필요 없다. r1의 FAIL/INVALID는 새 EVIDENCE가 오기 전 미해결로 유지하고, 일반 8의 구성 단위 OK를 전량 재검사하지 않는다.

### r2 EVIDENCE closure — record/fatal, 2026-10-29

**구성 단위 EVIDENCE OK: record 12/0, fatal 26/0.** ccaf745 독립 worktree HEAD/clean, build·실행 artifact 14개 hash, 두 원문/summary를 Codex가 직접 대조했다. record-only Host의 marker 2개와 선언 전 client gate, 첫 session epoch 1의 source 출처, 해당 session의 6개 dispatch/effect 순서 매칭 및 ctx none wake 기록이 통과했다. fatal은 정상 waiting-client/deadline 종료·OFF final 판정, 지정 startup fault와 client case가 통과했다. fatal의 자기 비어 있지 않은 lad_* 폴더 NOTE 보존은 검수한 정리 동작이며 추가 삭제할 필요 없다.

근거는 `.claude/codex_inj_record_fatal_evidence_r2.md`와 `.claude/codex_test_injection_review/verify_inj_r2_audit.json`에 연결한다. Codex의 재실행은 없다. 일반 8 r1과 합쳐 **합의된 안전 실행 하네스 보수·독립 검증 범위는 닫을 수 있다.** 검증용은 현재 정본/history/계획 체크·기록 절차를 정리해 이 한정 작업을 종료한다.

r1 FAIL/INVALID와 과거 안전 경계 위반은 이력으로 보존한다. real4 기본 REFUSED·record-sink 전환 및 prelogin 재설계 미구현/원 시험 금지, old-host NOT RUN, 기존 실기 안전 재확인·ka-pc/AVD 별도 미결과 R2 통합/출하는 별도 상태다. 이 일반 record 성공을 실제 Windows 입력 수락·prelogin UI 성공이나 모든 사건의 무영향 증거로 확대하지 않는다.

## 후속 사건 연결 (2026-10-30, 검증용 remote#7fsfy22z 기록)
- **2026-10-30 09:32:58 +09:00, Codex 검토 probe 의 모니터 깨움 호출 1회.** 구·신 참조(old-ref-163 ff98f19) SOURCE 검토 중 Codex 의 `codex_old_apk_review/oldref_r1_checks.py` power spy 가 PostMessage alias 만 가로채고 직접 PostMessageW 를 빠뜨려, ON 부정 대조에서 HWND_BROADCAST / SC_MONITORPOWER(-1) 를 실제 OS 에 1회 호출했다. SendInput / SetThreadExecutionState 는 spy 로만 기록(실제 호출 0), OFF 경로 0, Host/Viewer/AVD/서비스/설치 실행 0. 화면에 보인 영향은 미확인.
- 원인은 Codex 검토 코드이며 작업용 안전 port 의 실패가 아니다. 이 ON 실행은 안전 PASS 근거에서 제외. 보수: 직접 A/W API hook + 실행 전 import 검사(SendInput / PostMessageA/W / SetThreadExecutionState 없음 강제), 보수 전 재실행 금지.
- 정본: `.claude/codex_oldref_review_safety_finding.md` (59d2b881…). 사용자 보고: 검증용이 같은 날 보고.
