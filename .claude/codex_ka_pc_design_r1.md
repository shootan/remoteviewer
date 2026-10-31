# KA PC ③ 설계 r1 — Codex 판정

2026-10-27 / remote#tt7fcvky → 검증용 remote#7fsfy22z / task ka-pc-design.

**수정 조건부 설계 GO: O1(호스트별 managed Viewer가 자기 StandbyLease 소유) 채택. 결정①~④·⑥은 아래 경계로 수용, ⑤ network 종료 뒤 자동 재activation 1회는 불허한다. 구현·SOURCE·독립 EVIDENCE·배포 승인은 별도다.** 기존 승인된 PC managed 프로세스 구조와 kept 모델 A를 잇고, O2로 재작성하지 않는다.

입력 `proposal_ka_pc_r1.md` SHA-256 **ec828cc33f8a36f251e0341252565a32ffbd6aa07079c4cca5dea816d7596075**, 검증용6건 의견, `codex_keepalive_r2.md`·kept 모델·KA② SOURCE 판정을 읽었다. main의 shell registry/child launch/exit-reconnect/logout 및 Viewer 자체 UDP handshake·window init·watchdog, 고정0a5e71d의 Host entry/gen/grant/SessionEnded 경계를 대조했다. 새 시험·제품 실행은 없다. 현재 ka-r3 작업 파일은 보수가 진행 중이므로 그것을0a5e71d의 확정 API로 간주하지 않는다.

## 여섯 결정 — 검증용 안과 비교

| 건 | 판정/최종 조건 |
|---|---|
| ① host key별 device id·세대 | **동의, PC 소유 범위의 명시적 예외로 채택.** key=정규화 directory origin+안정 account ID+hostId. key별 영속 CSPRNG id와 단조 gen, 단일 live writer. 자식 재시작마다 새 id를 만들지 않는다. 아래 저장/잠금 조건 적용. |
| ② park 때 창·디코더 해제 | **동의, 초기 기본 확정.** Active의 session 종료/fence 뒤 창·decoder·MF/D3D 및 관련 device/cache/thread를 해제해 standby의 실제 작업0을 증명한다. 숨긴 자원 캐시 유지로 미리 바꾸지 않는다. 재활성 지연/메모리는 분리 측정한다. |
| ③ 로그아웃 때 일반 Viewer도 닫음 | **동의, 그 셸이 해당 계정으로 생성·소유한 directory Viewer까지.** managed/일반 여부로 계정 경계를 달리하지 않는다. 외부 standalone/raw-IP·다른 셸/사용자의 Viewer는 대상이 아니다. PID/창명 검색으로 확대하지 않는다. |
| ④ 명시 tap은 준비된 standby 경유 | **동의.** StandbyReady면 그 lease activation. supported 캐시만 있고 아직 준비 전이면 유한 준비/취소 또는 명시적인 실패 표시를 거치며 ready를 꾸미지 않는다. 구 Host/prelogin/지원unknown의 명시 legacy 연결 경로는 별도로 유지한다. 자동 legacy fallback0. |
| ⑤ network 종료 뒤1회 재activation | **불허.** 자동 fresh admission/Ping/standby 복구는 가능하나 session 종료 뒤 새 activation gen은 새 사용자 행동이 필요하다. 기존 살아 있는 grant/transport의 이미 승인된 재전송·resume와 구분한다. |
| ⑥ 창 검증 | **동의.** 기존의 화면 밖 배치 seam을 우선, CreateDesktop은 이 환경에서 실제 렌더/입력 증거를 얻을 수 있을 때만 대안. 아래 관찰 경계 적용. 사용자 화면을 가리는 시험은 승인하지 않는다. |

## ⑤ 불허 근거와 재연결 동작

Host `StandbyTable::Activate`는 **더 큰 gen을 새 사용자 activation/takeover로 처리**한다. 같은 gen은 그 grant가 여전히 live일 때만 응답하고 실제 SessionEnded 뒤에는 Superseded다. 제어 종료 원인이 network라는 클라이언트의 추정은 다른 owner가 생기지 않았다는 증거가 아니다.

현 Viewer watchdog은 control-lost/processing-stall에 recoveryExitCode43을 쓰고, shell은43/44/46을 재시작 대상으로 취급한다. 이것만으로 takeover와 단절을 권위 있게 구별하지 못한다. 상대가 인수한 직후 control-lost를 network로 분류해 새 gen을 한 번 보내도 **자동 재탈취**다. 횟수1이라는 제한은 이 권한 문제를 해결하지 않는다.

managed 분기는 기존43/44/46→ordinary Hello/새 activation 자동 재시작을 이어받지 않는다. 창에 연결 종료/다시 연결 동작을 보여주고 standby만 회복한다. 명시 reconnect 버튼/카드 tap에서만 새 op/gen으로 활성화한다. 상대 takeover가 확인되지 않았으면 “다른 기기가 연결했다”라고 단정하지 말고 확인된 사유 또는 “연결이 끊겼습니다”를 쓴다.

현재 activation handshake 중 유실에 대한 **같은 gen 재송신**, 아직 끝나지 않은 세션의 기존 control retransmit/resume는 원 계약대로 허용한다. SessionEnded 뒤 살아 있는 창·lease만을 근거로 이를 새 activation으로 확장하지 않는다. 별도 자동 재개권/owner fencing을 새 wire로 설계하는 일은 이번③ 범위가 아니다.

## ① 저장 범위와 프로세스 소유

Host의 gen 비교는 deviceId별·그 Host의 entry 안에서 이뤄진다. 따라서 서로 다른 host key가 다른 id를 쓰는 것은 이 계약에서 가능하다. **설치당 allocator1이라는 기존 클라이언트 설명을 PC의 key별 단일 소유 allocator로 명시적으로 갱신**하되 APK의 기존 단일 프로세스 소유를 바꾸지 않는다. 서버/Host가 이 id를 검증된 물리 기기 신원으로 쓰거나 전체 단말 수 제한을 보장한다는 주장도 하지 않는다.

- 같은 key는 stable id를 재사용하고 모든 admission/activation gen을 송신 전 내구 예약한다. 손상·저장 실패·소진/재설치의 정책은③ N3 보수 계약을 따른다. Host의 작은gen 허용 우회를 만들지 않는다.
- 두 셸/자식이 같은key store를 동시에 쓰면 한쪽이 **송신 전에** 배타 잠금 실패로 거부한다. `.gen`을 atomic replace하면서 잠금 대상 inode/파일 자체를 교체하지 않도록 **별도의 stable lock 파일** 등으로 key 소유를 고정한다. LockFileEx가 replacement 파일에 자동 승계된다고 가정하지 않는다.
- 파일명은 전체SHA-256 key를 쓰거나 header의 full key 검증으로 짧은 hash 충돌을 감지한다. 경로에 raw host/account 문자열을 결합하지 않는다. tab 구분 저장은 필드 escaping/길이·파싱 규칙을 고정하고 locale/clock tie로 순서가 뒤집히지 않게 stable 순서를 보존한다.
- recent 이력의 편의 저장과 gen의 내구 예약은 요구 수준이 다르다. wide 경로·원자 교체뿐 아니라 gen의 flush/실패/단일 writer 경계까지 공용 담당이 보수한다. PC 담당이 같은 shared 파일에 중복 writer를 만들지 않는다.

## 수명·IPC — 구현 전에 정본에 반영할 조건

**로그아웃/계정 변경/종료는 먼저 fence한다.** 제안§6의 epoch 증가를3초 대기 뒤까지 미루지 않는다. 전환 시작 시 owner epoch 증가·모든 pending op/auto-retry/token 요청 무효화·새 admission/activation 차단을 하고, old-owner 자식 snapshot에만 종료를 지시한다. Release/close 응답은 이전 owner의 teardown 결과로만 수집하고 UI/신규 계정 상태를 갱신하지 못하게 한다. 새 로그인 뒤 같은key 자식이 생겨도 옛 shutdown 대상으로 편입되지 않는다.

상속 IPC는 부모→자식/자식→부모용 **두 pipe**와 필요한 child 끝만 handle allowlist에 넣는다. 부모 끝·다른 자식의 끝·Job handle은 상속하지 않으며, 불필요한 복사 handle을 닫아 EOF가 실제 owner 종료를 나타내게 한다. 프레임은 길이/버전/type/최대 크기를 검증하고 bounded queue로 받는다. pipe가 막혀 UI/lease Ping/로그아웃을 무한정 붙잡지 않게 전용 I/O와 취소·deadline을 둔다. [Microsoft pipe 상속 문서](https://learn.microsoft.com/en-us/windows/win32/ipc/pipe-handle-inheritance).

모든 명령/응답은 key+child incarnation+owner epoch에 결합하고, activation/presented/picker/session-over는 그 opId까지 검사한다. **TokenRenewed는 owner 범위 tokenVersion**을 추가해 늦은 갱신이 최신 token을 덮지 못하게 한다. token 갱신 실패/401과 권한 철회는 transient network retry와 분리하며, logout 뒤 NeedToken이 come_back/로그인을 되살리지 않는다. token은 사설 pipe/메모리 수명으로만 전달하고 로그/argv/env에 새로 추가하지 않는다.

Job은 셸이 가진 **마지막 handle**이 닫혀야 KILL_ON_JOB_CLOSE가 동작한다. 자식에게 Job handle을 상속하거나 누수시키지 않는다. 생성 직후 도주 구간을 없애기 위해 child를 suspended로 생성→자기 Job에 편입 성공→resume하는 순서를 쓰거나 동등한 생성 시 결합을 쓴다. 편입 실패는 unowned child 실행으로 강등하지 않는다. 한 PC 제거/실패는 나머지2대를 끊지 않게 child별 소유 handle/job을 사용한다. 계정 전체 종료와 개별 종료를 구분한다. [Microsoft Job 문서](https://learn.microsoft.com/en-us/windows/win32/procthread/job-objects), [Microsoft의 suspended→Job→resume 예시](https://devblogs.microsoft.com/oldnewthing/20131209-00/?p=2433).

graceful fence/Release→유한 종료 대기는 worker에서 수행하며 셸 UI를3초씩 동기 block하지 않는다. 강제 회수는 launch에서 보유한 자기 process/job handle만 사용한다. Host active grant는20초 standby TTL로 곧바로 소멸한다고 보장할 수 없다. active liveness는 session 자체이고, **peer-lost/실제 session end 뒤** standby 만료가 적용된다. crash/종료 시험에서는 client child0, Host session end, entry 만료를 각각 관측한다.

## activation/park·UI와 자원

새 managed 시작은 IPC/WSA/lease만 준비한다. 창을 생성·ShowWindow·MF/D3D/decoder 초기화한 뒤 숨기는 구현은 대기0 조건을 충족하지 않는다. 사용자 tap 이후에는 handshake와 UI/decoder 초기화를 병렬화할 수 있지만 **신규 op에 모두 결합**한다. 실패/취소가 먼저면 늦은 init/HelloAck가 창을 보이거나 입력을 켜지 못하게 한다. PickerReady는 보이는/입력 가능한 실제 picker와 해당 op의 연결 준비가 충족된 시각, Presented는 그 선택의 첫 정상 frame으로 따로 기록한다. 초기화 단축만으로 p95≤1초를 선언하지 않는다.

park/닫기에서는 held input release·clipboard/선택 차단→reader/handshake 정지 확인→**정확한 activation op의 SessionOver**→lease Ping 재개를 직렬화한다. Viewer는 ClientSessionController와 별도 startup/receiver를 쓰므로 `udpHandshake`를 호출만 하는 것으로 안전해지지 않는다. ② 보수된 lease의 ownership/Abandon/SessionOver 계약에 맞춰 duplicated socket 소유·미사용 args·실패·취소·callback 생존을 실제 Viewer 경로에 연결한다. old0a5e71d raw-this API를 그대로 복사하지 않는다. 창/renderer의 늦은 종료가 새 op의 decoder나 socket을 닫지 않아야 한다.

ReturningToStandby 동안 local SessionOver/Ping Ok/server active enum을 **Host grant 반환 확인**으로 표시하지 않는다. 유한 확인과 Unknown/Lost의 non-preempt 재입장을 갖춘다. 각visible Viewer가 active일 수 있는 PC 동작은 유지하고 앱 전체active1로 축소하지 않는다. active+standby 논리key3·staging1, 실패D eviction0·성공D standby LRU만 회수, visible3개면 명시 안내 조건도 유지한다.

지원 캐시는 directory 지원과 개별 Host 결과를 분리한다. 무응답은 **확인되지 않음**이지 구 Host 판별의 확정 근거가 아니다. 캐시는 host/version·owner/lifetime·유한 재probe 정책에 묶어 일시 손실을 영구 미지원으로 고정하지 않는다. 준비됨은 실제 entry/liveness로만 표시하고 사용 중인 프로그램/설치본을 임의로 조작하지 않는다.

## 독립 검증·실행 순서

화면 밖 시험은 실제 제품 control 렌더·실제 입력/bridge→native 결과를 보존하는 위치/환경 seam으로 둔다. `CapturePreview`는 shell WebView의 증거이며 Viewer D3D 최종 presented 증거를 대신하지 않는다. 화면 밖/다른 desktop의 렌더가 throttle/미표시되면 그 환경의 해당 검증만 invalid로 적고, 가능한 기존 renderer 경계 캡처/격리 desktop으로 해결한다. 프레임 callback만으로 실제 표시를 주장하지 않는다. 설치본/사용자 창·보안 desktop을 조작하지 않는다.

기존 §8 행동 목록에 다음을 한 묶음 추가한다: 자동 network 재activation 0(직접·relay, takeover를 network처럼 보이게 하는 반례), logout **직전/대기 중** 늦은 TokenRenewed/Provide/Activate와 새 계정의 같은 key, 자식 생성/Job 편입 중 셸 crash·Job handle 상속 부정 대조, 같은 key의 두 프로세스 잠금·비ASCII 저장·재시작 gen, init/handshake 역순 취소, 개별 child 종료가 다른 2대 보존. 정상 활성→목록→동일 lease standby→재tap과 Host의 실제 session end를 관측한다.

실 NAT admission은 아직 미검증이다. punch가 없는 standby helper를 NP의 legacy relayEarly로 해결됐다고 간주하지 않는다. relay/직접 도달 여부·실 NAT/포트·교차기기·비용은 단계별 근거가 필요하다. 다만 그 후속을 이유로 승인된 PC 전용 구현/격리 실행 전체를 멈추지 않는다. O1의 비용은 절대값·steady 증가·0/1/3·active 1+standby 2·cold/reuse 지연을 함께 보고하며 “프로세스 3개 메모리>셸 1개”만으로 실패/구조 변경을 자동 결정하지 않는다.

검증용은 6건 판정·자기 안 비교와 위 조건을 **같은 PC task의 정본 하나**에 취합해 기존 작업용2에 연결한다. 공용 ② N1~N3는 작업용1이 보수하며 고정 검수 commit/API를 PC 기준에 합친다. APK e 배선 전체 완료를 PC 전용 IPC/UI 구현의 불필요한 장벽으로 만들지 않는다. NP의 viewer_startup 변경은 SOURCE를 통과한 고정 commit을 순차 통합하고 실제 충돌/회귀를 확인한다. 미검수 NP/KA branch 전체를 통째로 승인한 것은 아니다. 자동 preconnect 출하 OFF·운영 NAS/공유기·설치본 무변경을 유지한다.

Codex 제품 수정·빌드·시험/프로세스 실행·삭제·commit/push 없음. 이 미추적 판정서 외 history/계획·Git MCP commit은 해당 구현 작업에서 취합한다. 사용자 현재 조치 없음.
