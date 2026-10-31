# keepalive r2 — 최근 호스트 3대 상시 유지 설계 판정

**10-26 정책 갱신:** 사용자1·2순위 배정/빌드/게시 지시에 대한 `.claude/codex_rel_0163_r1.md` §5가 휴대폰 초기 기본 정책의 최신 판정이다. R2 기본은 전면 유지(onStop 해제·복귀 preconnect)로 채택하며, 이는 미답을 사용자 동의로 해석한 것이 아니다. 아래 §2의 답 대기는 background 포함 의도/새 서비스 수명 범위에 남고 초기 전면 기본 구현·릴리스 설명을 막는 조건으로 읽지 않는다. background 상시 유지 완료로 보고하지 않으며 다른 본 계약은 유지한다.

2026-10-22 / 요청 `proposal_keepalive_r2_2026-10-22.md` / 검증용 remote#wwml7h91.

**수정 조건부 설계 GO.** 최근 사용 3대·5분 만료 제거·앱 시작 시 미리 연결은 수용한다. **현 Host의 단일 세션을 그대로 둔 자동 미리 연결은 NEEDS_CHANGES**다. 호스트에서 여러 기기의 대기 연결을 분리하는 계약/실행 증명이 선행해야 한다. 휴대폰 백그라운드 범위는 사용자 답 대기이며 'onStop이면 앱이 꺼진 것'으로 확정하지 않는다. 아래가 기존 r1 계약 중 정책·구조 변경의 정본이다. 기존 구현/출하 OK 판정이 아니다.

## 1. 정책과 사용자 동작

- 최대 **3대는 active + parked를 합한 서로 다른 host** 수다. 디렉터리 origin + account stable ID + hostId로 기기별 최근 사용 이력을 저장한다. 자동 preconnect/heartbeat/목록 갱신은 최근 사용 순서를 바꾸지 않는다. 사용자의 성공한 접속/실제 재사용만 순서를 갱신하며, 실패한 탭·인증 시도만으로 기존 연결을 연쇄 축출하지 않는다. 기기 간 공용 이력 동기화는 이번에 새 서버 기능으로 만들지 않는다.
- 유지 대상은 이 최근 3대다. 그중 한 대가 offline이어도 임의로 과거 네 번째를 채우지 않는다. 자격이 유효한 앱 시작/복귀 시 해당 host를 **처음부터 제어 전용 대기 연결**로 준비한다. 자동 선택/화면 열기/원격 입력/클립보드/전원 켜기·잠금 해제·재부팅은 하지 않는다. 마지막 '화면' 자동 복원까지 추가하는 지시로 해석하지 않는다. 준비된 PC를 누르면 새 directory connect/observe/punch/Hello 없이 기존 대상 선택/보기로 이어진다.
- 5분의 **제품 park 만료만 제거**한다. park 전환 fence, connect/cancel/close, transport liveness, 인증/권한·서버 capability/relay lease의 실제 수명은 없애거나 무한 대기시키지 않는다. `0`을 넣어 기존 코드에서 즉시 만료되는 식으로 구현하지 말고 명시적 unlimited 정책을 사용한다. 서버 토큰/lease가 만료된다면 인증된 갱신/안전한 재접속 경로를 정의하며 만료를 무시하지 않는다.
- 네 번째 성공 연결은 parked LRU만 정리한다. 연결 실패로 기존 세션을 먼저 잃지 않는 기존 commit 규칙을 유지한다. ready 유지 연결은 3 이하, 전환 중 staging 후보는 최대1·기존 connect 시한 이내로 제한하고 실제 소켓/closing 수와 steady 수를 따로 기록한다. 무한 reservation/teardown 누적 금지. PC의 모든 3개 창이 실제 사용 중이면 자동으로 창을 닫지 않고 한도와 종료할 방법을 알린다.
- '그 PC 연결 해제'는 해당 PC의 자동 유지도 현재 앱/로그인 수명 동안 억제한다. 다음 목록 갱신/online 이벤트가 즉시 다시 연결하면 버튼의 의미가 사라진다. 명시적 다시 접속으로 억제를 해제한다. 계정 로그아웃/변경은 모든 entry와 pending 작업을 무효화한다. 일시적인 목록 누락만으로 정상 연결을 끊지 않는다.

## 2. 휴대폰 수명 — 답을 기다리는 한 부분

사용자에게 **홈 이동/화면 꺼짐에도 유지 vs 화면에 보일 때만 유지**를 질문했다. 답 전에는 onStop 전체 종료를 최종 요구로 승인하지 않는다. 독립적인 세션/Host/재통합 작업은 진행한다.

전면 한정이면 기존 onStop 종료(구성 변경 예외 유지)·onStart/복귀 preconnect를 채택한다. 백그라운드 포함이면 Activity 밖의 소유자/적합한 foreground service·지속 알림·중지 동작·프로세스 종료·화면 꺼짐·네트워크 변경 수명을 먼저 정본에 반영한다. 전면에서 뒤로 갈 때 영상·입력·clipboard는 먼저 park하고, 디코더/Surface는 Activity에 남겨 background owner가 잡지 않게 한다. 임의 배터리 최적화 해제·부팅 자동 실행·영구 wakelock·무한 재시작은 이번 지시로 추가하지 않는다.

Android의 onStop은 Activity가 보이지 않는 수명 이벤트이지 전원이 꺼짐/앱 프로세스 종료와 동의어가 아니다. FGS도 종류·시작 조건이 있고, 특히 target35+의 dataSync에는 시간 제한이 있어 무제한 연결의 우회 수단으로 가정하면 안 된다. 현재 APK target34/FGS 선언 없음은 직접 확인했으며 서비스 종류는 사용자 답과 실제 용도에 맞는 설계에서 정한다.

공식 근거: [Activity lifecycle](https://developer.android.com/guide/components/activities/activity-lifecycle), [FGS 종류](https://developer.android.com/develop/background-work/services/fgs/service-types), [백그라운드 시작 제한](https://developer.android.com/develop/background-work/services/fgs/restrictions-bg-start), [target35+ timeout](https://developer.android.com/develop/background-work/services/fgs/timeout). OS가 종료/네트워크 사용을 제한했는데도 '항상 살아 있음'으로 표시하지 않는다.

## 3. APK 재통합 — 재사용 GO, 오래된 파일로 덮어쓰기 금지

기준은 게시된 main **82b3d14**다. `feat/keepalive-multi a2e9686`를 재사용하되 실제 merge-base는 **eedd207**이다. `5063cd9..a2e9686` diff를 기능 패치로 쓰면 이후 prelogin/LK/DW/hello/SH를 대량 삭제하는 모양이 되므로 금지한다. base→branch의 실제 기능 변경은 apps26개(그중 native_poc13개)로 직접 확인했다. 새 worktree에서 정상 3-way merge 또는 이 변경만 전진 이식하고 충돌·수동 해소를 한 번에 취합한다. 원본 branch/evidence/worktree는 보존한다.

기존 entry handle/incarnation·entry별 gate·callback binding·active decoder1·클립보드 r3 계약은 유지한다. SH metadata-before-buffer·surface별 BLOCKED/late release·source-size/crop·설정은 반드시 함께 보존한다. shared punch/hello의 cadence/full-batch/error 분류 보수도 유지한다. 새로운 기능의 전처리 define/출하 gate는 기존 LK/DW/hello/SH의 합집합으로 검사한다.

**새 preconnect entry는 보여준 적 없어도 정상 parked로 유지**돼야 한다. 기존 SessionTable의 `kUnshownGraceMs=10000`/`never_shown` 정리와 기존 BeginPark가 Active만 받는 경로를 그대로 재사용하면 10초 만료나 일시 활성화를 일으킨다. 사용자 activation intent와 자동 대기 admission을 구분하고, 자동 연결이 decoder를 잠깐 bind하거나 다른 active를 바꾸지 않도록 수정·반례를 둔다. 단순 TTL 상수 변경으로 끝나는 작업이 아니다.

최근 host3대의 계정별 영속 저장은 새로 구현한다. 현재 SessionPersistence는 수동 endpoint/뷰어 설정 저장이며 최근 authenticated host3대를 이미 제공한다고 가정할 근거는 없었다. token/IP/표시명을 논리 키로 저장하지 않는다.

## 4. PC 구조 — 호스트별 관리되는 Viewer 프로세스 채택

초기 구현은 **셸이 관리하는 호스트별 Viewer, 유지 합계3대**를 선택한다. 셸 안에 decoder/네트워크를 통째로 옮기는 단일 프로세스 다중 세션 구조로 재작성하지 않는다. 현 셸에는 이미 gViewerByHost/소유 process handle·취소 이벤트가 있고 서로 다른 PC의 창 여러 개가 가능하다. 기존의 여러 가시 창 동작을 임의로 전역 active1로 축소하지 않는다. 같은 host의 중복 Viewer는 계속 금지한다.

- managed parked 시작 모드: 처음부터 숨김·제어 전용, 사용자 activation 전 target select/resume/입력·clipboard·preview·디코드/렌더 작업0. 기존 create_window의 ShowWindow/WM_CLOSE→DestroyWindow 및 decoder 초기화를 그대로 호출하고 나중에 숨기는 방식은 불충분하다. PC를 실제 보는 때 필요한 UI/미디어를 열고 park 때 작업을 중단한다. 남겨 두는 캐시/장치 자원은 비용 보고에 포함한다.
- 셸↔자식의 제한된 사설 명령/결과 IPC로 Park/Activate/Disconnect/상태를 구현한다. host key + child incarnation + owner epoch + operation을 검증한다. stdout 출력만 읽고 임의 HWND 찾기/전역 PostMessage로 세션을 제어하지 않는다. 새 경로의 token을 argv/env/로그에 추가하지 말고 상속 핸들 allowlist 등 사설 전달을 사용한다.
- managed 창 X=park 후 숨김, 별도 '이 PC 연결 해제'=해당 entry 종료/자동 재시도 억제. standalone Viewer의 기존 X 종료는 보존한다. 셸 최소화는 종료가 아니다. 셸 실제 종료/로그아웃/owner 변경/업데이트 handoff는 자식·pending connect까지 정리한다. 셸 crash 시에도 자신이 만든 자식만 회수하도록 Job/소유 핸들 수명을 설계한다. PID 재사용/다른 사용자 프로세스/설치 서비스는 건드리지 않는다.
- 재접속/restore/최근 intent를 entry별로 격리한다. 현재 전역 gViewerOperation/gReconnectRequest만으로 3대의 지연 종료·재시도를 처리하면 다른 PC의 복구를 지울 수 있다. 기존 DW는 실제 presented 대상만 복원/명시 monitor·window는 picker라는 계약을 유지한다. parked 프로세스는 자동 재연결 뒤 스스로 창을 열거나 과거 화면을 활성화하지 않는다.

## 5. Host와 여러 기기 — 필수 선행 설계·실행

직접 소스: main host_session.hpp는 단일 control channel/peer/streamControlActive/epoch, host_startup_control.cpp:497~522는 새 인증 Hello를 AdmitHello하고 sender peer·epoch를 바꾼다. :531 이후 control/bulk는 현재 adopted peer만 허용한다. **pause는 다중 클라이언트 admission이 아니다.** PC와 APK가 서로 다른 host3대를 각각 유지하는 것과, 같은 host에 두 기기가 parked로 연결되는 것은 다르다.

목표 계약은 **호스트에 복수의 인증된 제어 전용 대기 entry, 실제 영상·입력 소유자는 기존처럼 한 기기**다. 자동 preconnect/heartbeat/reconnect/갱신은 현재 active를 교체하거나 입력·선택·clipboard·서비스 작업을 수행할 권한이 없다. 사용자 명시 activation만 기존 takeover 규칙으로 active 소유자를 바꿀 수 있다. 교체된 쪽은 parked로 남거나 분명한 대기 상태로 내려가고 자동으로 제어권을 재탈취하지 않는다.

따라서 구현 정본에 아래를 먼저 고정한다(새 wire 필요 시 capability로 협상):

1. 대기 admission과 activation의 구분, 기존 auth를 우회하지 않는 session/device/connection-generation 식별·재전송 멱등성, peer 이동과 relay 공유 주소에서도 entry를 구별하는 방식.
2. entry별 control ACK/retransmit/liveness/permission·lease 및 active input/미디어 generation. 대기 entry의 Ping/Pause/Close가 다른 active의 streamControlActive·held input·LockCred/파일/clipboard를 변경하지 못하는 host gate.
3. 제어권 전환 시 기존 held input release·옛 epoch 무효화·실제 새 IDR/선택 경계. 이전 active의 지연 패킷이 새 owner를 건드리지 않는 반례.
4. 호스트 전체/기기별 유한 entry 상한과 Busy/자원 회수. 사용자 '3대'는 클라이언트가 유지하는 host 수이며 host에 무제한 기기 수를 허용한다는 뜻이 아니다. 최소 APK+PC2기기의 parked 공존과 active1을 증명한다.
5. 구 Host에는 자동 preconnect를 보내 기존 세션을 탈취하지 않는다. 지원 협상을 active를 바꾸는 Hello로만 알아낼 수 있다면 그 자체가 해결 전제다. 구 Host는 '상시 유지 미지원/업데이트 필요'로 표시하고 명시적 수동 접속은 기존 경로로 제공한다. 새 Host와 구 APK/PC의 명시 접속 호환을 검증한다.

**첫 실행 단위는 이 host/admission 격리의 제한된 spike/설계 확정**으로 잡는다. 새 wire/relay 라우팅/세션 권한이 필요한 실제 차이와 active 무간섭 실행 근거를 한 고정 후보로 취합해 검증용→Codex에 올린다. 기존 프로토콜로 가능한 근거가 나오면 불필요한 wire 변경은 하지 않는다. 그 전에 기존 Hello 기반 자동3대 재접속을 출하하지 않는다. 대기 연결을 TCP accept 개수만 늘리거나 클라이언트 auto-retry를 꺼서 동시 유지가 되는 것처럼 처리하지 않는다.

## 6. 재시도·비용·판정 기준

유지 시간 무제한과 재시도 부하 무제한은 다르다. host별 backoff(예:1/2/4/8/16/30초 cap+jitter), 자동 connect 동시1·수동 탭 우선/취소, 동일 host 중복 op0을 둔다. 목록online 이벤트는 기존 시도를 합류하며 초마다 새 capability/connect를 만들지 않는다. 권한 철회/로그아웃/명시 해제/다른 기기의 제어권 인수는 offline과 다른 종료 사유로 다룬다. 새 연결은 non-preempt 대기부터 시작하며 과거 active였다는 이유로 자동 활성화하지 않는다. 일시적인 네트워크/목록 장애를 인증 철회로 단정하지 않는다.

비용은 direct와 실제 relay 각각 0/1/3 parked 및 active1+parked2로 측정한다. 안정 park에서 host video 송신/decoder 입력/preview/clipboard/원격 입력0, 모든 parked일 때 capture·encoder 작업은 기존 idle 정책에 따라 중단돼야 한다. 전환 tail은 별도 구간으로 분리한다. control bytes·패킷/s 양방향, relay 양쪽 leg/connection 수, directory connect/observe/capability 횟수, process/private bytes/threads/CPU·wakeups/GPU 작업을 실제로 제시한다. 메모리는 안정 구간에서 증가 추세가 없어야 하며 유지 중 directory connect/Hello 반복0(명시적 lease 갱신·장애 복구 제외), busy retry 폭주0은 필수다. 근거 전 고정 byte/s·배터리 소모 수치를 성공처럼 만들지 않는다.

prepared reuse의 목표는 loopback/고정 입력에서 탭→대상 UI 사용 가능 p95≤1초, 대상 선택→첫 정상 presented 프레임은 별도 분포로 측정한다. cold 대비 새 connect/Hello0이 핵심이며 '≤1초'를 모든 실 LAN/relay에 보장하거나 대상 UI 시간과 첫 영상 시간을 혼동하지 않는다. 5분 초과 실제 유지 시험+가상시계 장시간·만료/cancel 시험, direct/relay 3대 부하 구간과 빠른 반복 전환을 수행한다. 실폰 배터리/Doze는 실제 기기 근거 없으면 미검증으로 명시한다.

## 7. 단계·버전·독립 검증

검증용이 이 판정과 사용자 백그라운드 답을 **한 유효 구현 정본**으로 취합한다. 다음 담당은 검증용→작업용이며 최초 실행 task ID를 연결한다. shared native/Host 수명·권한 변경은 한 담당이 쓰고, APK UI/PC 셸의 분리 파일 작업만 병행 가능하다. 별건 directory-retry 문구/시험 보수를 이번 기능에 합치지 않는다.

순서: ①host/admission 격리 계약·spike(독립적으로 APK 세션 재통합 준비 가능) → ②shared control/Host기반 + APK → ③PC managed Viewer → ④양 기기/호스트 교차 독립 UI·출하 격리 → ⑤다음 릴리스. **APK0.2.32/vc31·PC0.2.163은 후보 번호**로 수용하되 검토 전 버전/게시 파일을 건드리지 않는다. 이 판정은 구현 방향 승인이고 미검토 코드 출하 OK는 아니다. 단계마다 같은 추상 go를 재요청할 필요 없다.

필수 행동은 최근 A/B/C 저장→앱/셸 재시작→3대 자동 대기(화면·입력0)→A/B/C 실제 탭 재사용→D 성공 때 parked LRU 한 대만 종료, 실패D 때 기존 유지, 개별 해제 뒤 자동 재생성0, 한 host offline/재온라인이 다른2대를 끊지 않음, owner 변경 뒤 지연 callback0이다. APK는 단일 decoder·SH off/.20/.35·회전/크게보기·clipboard r3를 재검증한다. PC는 실제 렌더된 창/셸에서 park/숨김/재개·다중 창·종료/crash·한 자식 실패가 다른2대를 보존하는 것을 검증한다. **같은 host에 실제 APK+PC parked 공존→한쪽 activation→다른쪽 heartbeat/재접속 무간섭→명시 takeover**를 direct와 relay에서 증명해야 전체 기능 OK다. mock PASS·TCP socket 수만으로 대체하지 않는다.

현 0.2.162/0.2.31 게시본은 그대로다. Codex 산출물은 미추적 설계 판정서/운영 메모이며 제품 수정·빌드·설치·서명·게시·삭제·commit/push는 하지 않았다. 구현 history/계획 checkbox/기능 commit은 해당 작업용 정본에 취합한다.
