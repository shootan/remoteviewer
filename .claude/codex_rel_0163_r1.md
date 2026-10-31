# rel-0163 r1 — 1·2순위 배정 및 5건 설계 확정

2026-10-26 / 받은 task **t-nzdilv86** / 검증용 remote#yj8mrmrx / 입력 `plan_rel_0163_2026-10-26.md` 전문.

**배정·R1/R2 분리·빌드/게시 흐름 GO. 아래 조건을 구현 정본에 반영해 진행한다.** 이 문서는 설계 확정이며 미검토 WIP의 제품 OK/출하 검증 통과 판정은 아니다. 사용자10-26 '1·2순위를 분배해 빌드하고 게시까지' 지시는 해당 작업의 일시 중지를 해제하고 **관련 버전 게시·일반push까지** 승인한 것으로 수용한다. 같은 범위의 반복 go를 기다리지 않는다.

| 요청 | 판정 | 핵심 |
|---|---|---|
| UA-2 | GO | backend 요청은 현재 인증된 논리 제어 세션 소유, 종료 후 호스트 기본; APK 영속 전역설정의 자동 재적용도 수정 |
| PS-A | 수정 조건부 GO | 소유한 마지막 원본 재인코딩/없으면 유한 재획득. 캐시 재사용의 데스크톱·대상·수명 fence 필수; blanket flush 유지/guard 제거는 불가 |
| PS-B | GO | prelogin 재부착은 일반 secure→WGC 분기보다 먼저 DXGI 전용 처리; 진짜 실패만 기존 r10 T2 종료 |
| UA-1 | GO | 동일 연결의 입력 데스크톱 edge를 독립 추적→WGC 재구성/세대·IDR; 실제 잠금 판정과 분리 |
| KA 폰 기본 | 범위를 명시해 GO | R2 초기 제품 기본=화면에 보이는 동안 유지. 미답을 사용자 동의로 간주하지 않으며 background 유지 요구는 미확정/미구현으로 구분 |

## 1. UA-2 — 요청의 소유자와 APK 의미

캡처 방식 선택은 **이 host의 현재 인증된 논리 control 세션**에 한정한다. 새 세션은 호스트의 원래 기본으로 시작한다. 기본은 현 제품 DXGI이며, 별도로 구성된 startup 기본이 있으면 그것을 기준으로 하고 supervisor의 DXGI quarantine을 항상 적용한다. 클라이언트 요청 초기화가 quarantine을 해제하거나 서비스의 prelogin DXGI 전용 규칙을 바꿔서는 안 된다.

- override는 owner cookie/epoch·generation에 결합한다. EndSession/실제 인증 세션 종료/새 세션 채택 때 이전 override와 pending backend 명령을 무효화한다. **늦게 끝난 이전 Serve()가 새 세션의 요청을 지우지 못하도록** 현재 소유자를 비교하고 main capture owner에서 직렬 적용한다.
- 같은 논리 세션의 retransmitted Hello·control-resume·창/모니터 선택·stream pause는 '세션 종료'가 아니다. 이를 이유로 backend가 튀어서는 안 된다. 세션을 바꾼 reconnect는 초기화한다. 멈춘 스트림의 세션 종료 시 metadata만 기본으로 되돌리고 불필요한 캡처/GPU 작업을 켜지 않는다. 다음 active attach에 올바른 기본을 적용한다.
- APK의 현재 `SessionPersistence.desktop_backend` 전역 저장값을 다음 연결/다른 host에 자동 재전송하면서 '이번 연결만'이라고 표시하면 불일치다. **새 APK는 기존 전역 저장값의 자동 적용을 중단**하고 현재 entry/connection에 선택을 보관한다. 연결별 초기 UI도 host 기본/실제 응답을 따른다. 다른 영상 설정·SH off/약/강 저장값을 초기화하지 않는다. 연결 전에는 적용 대상을 분명히 하거나 제어를 비활성화한다.
- 문구는 '이 PC의 이번 연결에만 적용'을 채택한다. '요청한 방식'과 **호스트가 실제 적용한 방식**은 구분한다(예: DXGI 요청/WGC 격리 적용, window target은 WGC). 아직 응답이 없으면 적용 중/확인 불가를 표시하며 성공한 것처럼 radio만 바꾸지 않는다.
- 현재 고정 ControlPong에는 backend requested/effective/active의 독립 필드가 없다는 점을 확인했다. 기존 struct를 몰래 키우지 않는다. 기존 정보만으로 정확한 표시가 안 되면 최소 capability 협상/상태 응답을 추가하고 구 peer에는 모르는 요청을 보내지 않는다. 상태값/표식/번호는 KA spike·LK 변경과 합쳐 충돌 검사 후 검증용이 배정한다. UI 현재 방식은 캡처 생성/적용 상태이고 최종 영상이 정상이라는 판정과는 별개다.
- 구 APK의 접속마다 wgc 요청은 새 host에서 **그 세션 동안** 수용하므로 호환된다. 구 APK 자체의 저장 정책까지 host가 바꿨다고 주장하지 않는다. 구 host에 연결한 새 APK는 세션 범위 보장을 모르면 그렇게 안내한다.

필수 반례: 폰 WGC→종료→PC(backend 미요청)에서 기본DXGI, 격리 시 WGC 유지, 늦은 옛 종료/옛 backend 요청 뒤 새 세션 선택 보존, 같은 세션 control-resume/park에서는 설정 보존, 계정/host 변경 시 전파0. R2의 복수 parked entry에는 각자 요청을 보관하되 **현재 active owner만 실제 캡처 방식을 변경**할 수 있게 확장한다.

## 2. PS-A — 원본 보장, 기존 캐시의 거부 원인부터 수정

제안의 '원본 보존→필요할 때 재인코딩→없으면 duplication 재생성' 방향은 채택한다. 다만 현 코드를 직접 확인하면 **BootstrapFrameCache는 이미 별도 BGRA 원본을 보관하고 flush가 이를 직접 지우지 않는다.** 반면 `KickTryFill`은 streamGeneration/대상 등을 검사한 다음 **Default 데스크톱이 아니면 무조건 false**이고, PublishFrame이 보관하지 않는 NV12-only 경로도 있을 수 있다. 그러므로 PS 원인을 'flush가 모든 원본을 삭제했다' 하나로 확정하거나 캐시 하나를 더 만드는 것만으로 끝내지 않는다.

- 보존물은 앱이 소유한 immutable 원본/복사본이다. DXGI가 ReleaseFrame 뒤 재사용할 원본 텍스처, WGC frame pool의 재활용 surface, 이전 nv12 ring slot의 포인터/ComPtr만 붙잡은 것은 안전한 복사본이 아니다. CPU shared payload를 재사용하거나 GPU copy 완료와 참조 수명이 보장된 원본을 사용한다. 최신1개 중심의 유한 cache/소비 lease로 제한하고 주기마다 메모리가 증가하지 않게 한다.
- **미디어 큐 flush와 source cache 유효성은 분리**한다. 같은 source/desktop을 계속 관찰한 picker pause나 encoded queue 정리 때문에 valid 원본을 잃을 필요는 없다. 그러나 source/계정·권한/console session/desktop identity·generation/attachment/모니터·창/크기·crop·device가 바뀌거나 그 연속성을 확인하지 못하면 원본을 무효화한다. '모든 flush는 encoded만 버리고 원본은 항상 살린다'는 전역 규칙은 불가하다.
- 특히 **Default에서 캡처한 원본을 UAC/Winlogon에, 옛 사용자의 원본을 새 로그인/세션에 보내지 않는다.** prelogin 캐시를 허용하려면 해당 SYSTEM worker가 실제로 서비스 중인 Winlogon·console/권한·source 세대에 귀속된 프레임임을 재검증한다. 단순히 KickTryFill의 Default guard/streamGeneration 비교를 삭제하거나 옛 프레임에 현재 gen을 써서 통과시키지 않는다. 재인코딩의 새 media epoch와 원본 provenance를 별도로 유지한다.
- idle detach/desktop 전이 후에는 관찰하지 못한 동안 내용이 바뀔 수 있다. 같은 이름의 desktop이라는 이유만으로 오래된 snapshot이 현재라고 가정하지 말고 **재부착으로 새 전체 프레임을 확보**한다. cache가 없거나 안전한 source 여부가 불명확하면 invalid 상태를 유지하고 유한 횟수의 재획득/backoff를 사용한다. 내용이 없다고 2fps마다 duplication을 끝없이 재생성하지 않는다.
- stream 활성화/새 선택·새 세션 첫 화면/명시 keyframe 요청은 **아직 첫 AU가 없더라도** raw bootstrap을 시작할 수 있어야 한다. 현 `StaticRefreshDue`의 `lastEmittedAuCaptureUs != 0`에만 의존하면 첫 프레임이 계속 막힌다. 첫 화면은 실제 새 IDR을 인코딩·전달해야 하며 옛 encoded AU/P-frame을 재생하거나 key bit만 바꾸지 않는다.
- static refresh는 **stream active인 desktop 경로**에 적용한다. picker·비활성·KA parked의 영상/인코더0 계약은 유지한다. source를 재인코딩할 때 원래 source capture 시각/age는 진단에 보존하고, 인코더용 단조 timestamp와 새 전송 시각을 구분한다. 캐시 반복을 새 실내용 프레임/새 캡처 fps/건강한 모션 증거로 세지 않는다.

**계획의 '현2fps'는 정정한다.** main `host_startup_capture.cpp:343~345` 기본은1000ms=1Hz이고 stage_pop_frame 주석도1Hz다. 본 요청의 '최소주기 갱신'에 맞춰 **R1의 active 정지 화면 제품 목표를500ms(2fps) 갱신으로 채택**한다. 현재 이미2fps였다고 기록하지 않는다. 실제 출하 시작 경로의 설정·워커 환경과 wire/present 간격을 측정하고, 출하 경로를 시험용 refresh=0 env에 의존하게 하지 않는다. 움직이는 화면에 불필요하게2fps를 추가하거나 parked에서 계속 인코딩하지 않는다.

필수 행동: 실제 source가 정지한 로그인 전 화면에서 picker10초→desktop 선택→첫 정상 presented≤1초, 계속되는 정지 화면 갱신, 같은 viewer 재접속2회·폰 교차 접속·강제 keyframe, raw cache 없음/첫 AU 없음/NV12-only·CPU경로/경계 뒤 옛 snapshot 늦은 도착. **Default/Winlogon·session·target을 바꾸고 옛 cache가 보내지면 실패하는 부정 대조**를 포함한다. setter/Encode 호출 성공만으로 통과하지 않고 실제 새 IDR→수신→decode→표시까지 확인한다.

## 3. PS-B — prelogin 재부착 우선순위

**GO.** 실제 prelogin worker의 재부착은 일반 UAC의 'secure이면WGC' 분기보다 먼저 **DXGI + 현재 서비스 중인 logon desktop attach**로 결정한다. 이미 명시 backend 요청을 무시하는 규칙과 일치한다. 유효한 prelogin worker임을 로컬 mode/신뢰 경계로 판단하고 원격 flags만으로 이 권한을 얻지 않는다.

기존 requiredInputDesktop/console/worker 수명 검사, 다른 desktop을 보면 종료하는 r9/W2, 실제 DXGI 시작/재부착 실패의 r10/T2 종료·backoff는 유지한다. WGC/GDI나 Default의 캐시로 조용히 돌아가서는 안 된다. 정상적인 picker idle detach→active와 같은 session rollover는 그 자체가 rc15 사유가 아니다. PS-A와 함께 first-frame을 입증한다. 이 수정은 ordinary UAC의 WGC 복구를 없애라는 뜻이 아니다.

## 4. UA-1 — 같은 연결에서 UAC 진입·복귀

10-23 방향 그대로 GO. **active=requested=WGC인 경우에도** 입력 데스크톱 변화를 독립 감시한다. UAC는 실제 WTS lock/logon 이벤트와 같지 않다. 실제 lock state와 secure desktop 상태를 분리하고, 조회 실패/전환 중 Unknown을 무조건 '잠금'으로 확정하지 않는다.

전환 확인→옛 capture/attachment callback과 cache provenance 무효화→WGC item/pool/session 재구성→새 세대/IDR/최종 출력 확인을 기존 capture owner에서 직렬화한다. 중간 상태 debounce·epoch·bounded retry를 두어 각 loop마다 재시작하지 않는다. 옛 readback/encoded AU가 새 화면에 나가거나 PS-A의 반복 원본이 이 fence를 우회하지 못하게 한다. **PS-A의 캐시 검증과 UA-1 desktop generation은 같은 계약**으로 구현한다.

현재 관측은 fresh WGC로 UAC가 보이고 다시 fresh WGC로 Default가 복구됐으므로 전체 Host를 SYSTEM으로 바꾸거나 신규 범용 SYSTEM 캡처/입력 권한을 붙이는 변경은 이번 범위가 아니다. UAC 자동 승인·보안 설정 완화·LockCred 평문 guard 제거도 하지 않는다. 원격 연결/세션 cookie를 유지한 채 진입·예/아니오·복귀2회 이상과 이전 PID/세대 늦은 응답을 검증한다. 실제 UAC 승인/반려는 사용자 실기로 구분하며 자동 승인하지 않는다.

## 5. KA 폰 기본 — 전면 유지로 초기 구현/제품 기본 결정

**R2 기본은 앱 화면에 보이는 동안 유지(onStop 해제, 구성 변경 예외 보존·전면 복귀 즉시 preconnect)로 채택한다.** 이유는 현재 제품 수명을 유지하면서 새 Android background service/권한·상시 알림을 이 승인에 추정 추가하지 않는 설계 선택이다. **사용자 미답/시간 경과를 그 선택에 찬성한 것으로 해석하는 것은 아니다.** 이 기본값 결정은 기존 `codex_keepalive_r2.md` §2의 미결 중 구현 기본 정책을 본 범위에서 갱신한다.

정본·UI/릴리스 안내에 '휴대폰은 앱 화면에 보일 때만 유지, 홈/화면 꺼짐 시 해제, 복귀 시 준비'를 명시한다. **백그라운드 상시 연결까지 완료했다고 주장하지 않는다.** 사용자가 background 포함을 답하면 같은 task 정책을 수정하고 적합한 foreground service/알림/중지·OS제한 검증을 별도 수명 계약으로 반영한다. 기존 질문을 중복 발송하거나 이 미답 때문에 R1·host admission/PC 구현을 세우지 않는다.

PC는 셸 프로세스 수명 동안 유지한다. KA의 최근 성공 사용3대·자동 admission은 비탈취·active owner만 입력/미디어·다른 기기와 parked 공존·구 Host capability 협상·한 writer 계약은 그대로다. `328d616` spike/`6d85ce2` APK 준비/`5b1d2e0` LK는 존재와 parents를 읽었을 뿐 최종 구현 승인하지 않았다.

## 6. 배정·릴리스·완료 경계

- **작업용1** LK-UX 마무리→PS/UA shared native/Host→R1 통합·릴리스 후보→KA host spike 순서 수용. **작업용2** SH r2→UA-2 APK→KA APK 준비, NP는 설계 확정 뒤 소유 파일에 따라 배정. PC managed viewer/NP shared 파일은 한 writer 순서와 연결한다. SH와UA-2의 MainActivity 및 protocol/CMake 충돌은 고정 후보에서 취합하며 양쪽 부분 승인으로 합쳐진 코드를 자동 승인하지 않는다.
- **R1 PC0.2.163+APK0.2.32**: PS-A/B·UA-1/2·LK-UX + SH A(.60)를 목표로 한다. SH B는 기존 same-frame/기하·Host 터치/비용 기준을 통과한 후보를 Codex가 선택한 경우만 포함한다. B 실험 실패가 A와1순위 보수를 무기한 막지 않는다. A도 화질/구현·출하 gate가 통과해야 한다.
- **R2 PC0.2.164+APK0.2.33**: KA+NP 목표 수용. NP의 구현 목표는 사용자 새 배정/게시 지시에 포함된 것으로 보되, 현 집계/가설만으로 STUN/relay race·광고 포트 변경 설계를 확정하지 않는다. 기존 NP 문서에 맞춰 실제 socket 관찰·fresh connectivity check·relay fallback의 한 설계안을 먼저 취합해 Codex 확인 후 구현한다. 공유기 UPnP/NAT-PMP 설정 변경·광역 NAS 권한 완화·실험적 timeout 단축은 묵시 승인하지 않는다. R2 일정을 이유로 KA admission 격리/NP 실제 연결 검증을 생략하지 않는다.
- R1/R2 각각 **고정 build commit→검증용 독립 행동 검사→Codex SOURCE/EVIDENCE→서명·gnlink 배포→외부 signature/version/artifact hash→main 반영·관련 docs Git MCP commit/일반push·실remote hash→dist/최종 안내**로 연결한다. 개별 구현이 끝났다고 바로 빌드/게시하거나 과거0.2.162 hash를 새 버전 검증에 재사용하지 않는다. 키/서명자·APK appId/전체 signer 일치·4ABI·시험 define/byte gate 합집합을 새 산출물에 귀속한다.
- 실제 UAC/cold LogonUI·NVIDIA/폰 성능 같은 남은 실기는 자동 fixture의 권한/desktop/픽셀 경계와 구분한다. 자동 가능한 필수 UI·정지 첫 프레임·재연결을 생략하지 않는다. OS 사용자 단계만 남으면 기존 배포 완료/실기 대기 기준으로 정직하게 안내하되 실기 성공이라고 쓰지 않는다. 사용자 설치/실 서비스 교체/재부팅/UAC 승인을 에이전트가 임의 실행하지 않는다.
- 사용자10-26 승인 안의 다음 단계마다 반복 게시/일반push 승인을 묻지 않는다. force push·원격삭제·무관 branch/기능·원본 worktree/SDK·증거 삭제는 범위 밖이다. 실패/미검증과 다음 담당을 이어가고 기능별 history/계획checkbox/Git MCP commit을 해당 작업에 취합한다.

검증용은 이 판정으로 계획/실행 정본을 한 번에 갱신하고 실제 최초 task ID·다음 담당을 연결한다. 이미 시작한 LK/SH task는 재사용한다. PS/UA가 새 실행 단위면 최초 task를 발급하되 같은 기능 중복 지휘는 하지 않는다. 이번 Codex 산출물은 미추적 판정서/인계뿐이며 제품 수정·빌드·설치·삭제·게시·commit/push는 수행하지 않았다.
