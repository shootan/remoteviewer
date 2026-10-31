# keepalive-multi r1 — 2c22313

2026-10-16 · 기준 main eedd207 → feat/keepalive-multi 2c22313.
**NEEDS_CHANGES — M1 연결 성립 전 LRU, M2 전환 intent 누락, L1 종료 정책 정합 및 V1 핵심 다중 행동 검증.** 여러 PC 유지의 긍정 경로는 인정한다. 정책값 사용자 답 대기는 별도이며 아직 출하 GO가 아니다.

직접 검토: 보고/독립 검증 파일, session_registry/session_table, entry_sink_adapter와 decoder token 검사, native bridge 예약/commit/activate/close, MainActivity 전환·종료·directory 연결, ClientSessionController::Connect. 빌드/AVD는 직접 실행하지 않았다. 기존 작업 브랜치와 혼동하지 않고 고정 commit을 읽었다.

## 인정하는 구현·실행 근거

entry별 controller/rendezvous/error/gate와 shared_ptr 수명, 프로세스 유일 op 및 entry별 취소, 단일 decoder의 token 검사, map 분리 뒤 closer teardown은 방향이 맞다. sink 12종을 adapter로 통과시키고 decoder mutation과 token 검사를 같은 잠금에서 처리하는 개선을 확인했다. mutable token을 사용할 때 같은 entry의 재활성화도 구분하는 binding generation은 계속 유지해야 한다.

검증용 독립 2-host UI 20/20와 0.2.30 13/20 중 다중 유지 단계의 실패는 실제 요구에 맞는 음성 대조다. A/B 카운터 불변·개별 끊기·입력/화질 대상 분리·안정 park 영상0의 근거를 수용한다. 단위7종 및 Android213/0은 독립 실행 보고 인용이다. 아직 LRU·만료·지연 연결 취소 등 테이블 바깥 결합을 전부 입증한 것은 아니다.

## M1 — worker 시작 성공을 연결 성공으로 commit해 LRU를 먼저 종료

위치: MainActivity.kt 약 5035~5073(nativeDirectoryConnectAny→madeGeneration→nativeCommit), native bridge session_adopt_steps, native_video_client_session.cpp Connect 약65~120, SessionRegistry::Commit.

ClientSessionController::Connect는 snapshot을 Connecting으로 만들고 WorkerMain 스레드를 시작한 뒤 true를 반환한다. 실제 인증 Hello/제어 연결 성공은 비동기다. 그런데 madeGeneration!=0이면 UI callback이 즉시 nativeCommit을 호출하고, registry.Commit은 예약된 parked victim을 Closing으로 바꾼다. 이후 B의 Hello가 거절/timeout되어도 이미 종료한 A는 되살릴 수 없다. ‘실패한 연결은 기존 PC를 축출하지 않는다’ 계약 위반이다. Connecting을 Alive로 보는 보수는 조기 reap을 막는 데 필요하지만 이 commit 시점의 증명이 되지는 않는다. 수동 연결 경로 약2328도 같은 점검 대상이다.

반례: 한도 N의 기존 parked 연결들 → 새 D 예약/worker 시작 → D의 Hello ACK/인증 성립을 보류 → 현재 nativeCommit에서 LRU가 먼저 종료 → D 연결 실패. 기존 parked가 하나도 끊기지 않아야 한다.

요구: 동일 handle/incarnation·connection generation·op/lifecycle epoch의 **실제 Connected/인증 성립 확인 후** commit/eviction한다. UI만 늦추는 것뿐 아니라 native commit도 Connecting의 성급한 commit을 거절/보류하도록 경계를 닫는다. 확인 전에 sign-out/새 intent/실패면 예약 취소·피해 연결0. 실제 handshake 성립까지 무한 UI 대기나 테이블 락을 잡은 동기 wait를 넣지 않는다. 한도 중 임시 연결 예약/closing 자원 수와 최종 live 한도도 구분해 기록한다.

동일 제어 순서의 수정 전 FAIL→후보 PASS와 성공 시 LRU1회·피해 대상 정확, 실패 시0을 확인한다. native scripted Alive=true만으로 인증 연결을 대신하지 않는다.

## M2 — connecting 중 새 PC 탭의 최신 intent가 사라짐

MainActivity.connectToDirectoryHost 첫 줄 `if (directoryBusy) return`은 B의 연결/observe/punch가 느릴 때 A/C를 누르는 intent를 보관하지 않는다. 현재 heldTap은 park 확인 중만 처리한다는 보고와 일치한다. r1 §3의 ‘A/B/C 빠른 탭에서 최신 intent 하나’ 계약은 연결 준비 구간도 포함한다. 사용자 핵심인 A/B 오가기에 직접 관계한다.

기존 B는 자기 op만 취소·정리하고 최신 탭을 한 번 실행하거나, 작업 중임을 표시하면서 최신 선택을 보관해 이어가도록 한다. 최신 parked A로 복귀할 때 불필요하게 B 네트워크 timeout까지 기다리지 않게 한다. 단일 전환 소유자·이미 확보된 다른 parked 연결 보존·native 소유권 gate를 재사용한다. 연결 취소와 동시에 fallback이 다시 B를 시작하지 않아야 한다.

결정적 delayed observe/Hello의 실제 앱 경로에서 B 진행 중 A→C 탭, 최신 C1회·옛 B 채택0·관계없는 A park 보존을 확인한다. 빠른 탭을 시간 운에 맡기지 말고 시험 fixture 응답을 지연한다. sign-out/stop 뒤 지연 callback도 같은 묶음으로 확인한다.

## L1 — onStop·네트워크 정책 확정

이전 제안의 서문에서 ‘기존 경계 유지’라고 적은 것과 §6의 전부 종료가 충돌했고, 실제 0.2.30 active 세션은 onStop에서 유지됐다는 소스 사실을 확인했다. 이 부분을 명확히 고친다.

**이번 다중 keepalive는 앱 foreground 범위로 한정한다. onStop에서 active+parked+connecting 모두 종료하고 global lifecycle epoch를 올린다.** 현재 endParkedKept 대신 endAllKept 경로로 정합화한다. 이미 in-flight인 입력 release/gesture 정리·Surface/scene 복귀까지 함께 확인한다. configuration 변경이 onConfigurationChanged로 처리되는 기존 예외는 유지하며 회전만으로 전체 종료되었다고 오해하지 않게 한다.

이는 실제 active 세션의 백그라운드 유지 동작도 바꾸는 선택이다. 문서/릴리스에 ‘앱을 벗어나면 모든 PC 연결 종료’를 명시한다. 사용자 요구는 foreground PC 사이 왕복 유지이며 백그라운드 지속 서비스까지 요청한 것은 아니다. background 세션 유지 기능을 새로 섞지 않는다.

확인된 network path 변화도 현재 active 여부로 분기하지 말고 전부 종료·epoch 무효화로 통일한다. active만 남은 경우 network 감시 해제 여부도 점검해 문서와 실제 감시 범위가 일치하게 한다. 네트워크 callback의 최초 통지/중복 통지를 경로 변경으로 오인하지 않는 기존 기준은 유지한다.

명시적 ‘모두 끊기’ 버튼도 activeHandle 유무로 일부만 끊는 분기를 제거해 이름대로 전체 종료한다. UI에서 active 중 버튼이 도달 불가라면 그 점은 실행 경계로 적되 handler 의미는 전체로 일관되게 한다. PC별 카드 끊기/화면의 연결 해제는 여전히 자기 entry만 종료한다.

## V1 — 다음 revision에서 묶을 필수 행동

정상 2-host 왕복 전체를 매번 반복할 필요는 없다. 다음 자동화 가능 핵심 경계를 좁혀 확인한다.

- M1의 인증 성립 전 실패→LRU0, 정상 Connected→LRU1. 최종 사용자 한도 N을 고정한 뒤 N+1번째 PC의 실제 fixture 흐름도 확인한다. 한도3이면 네 번째가 해당한다.
- 각 PC의 서로 다른 park 시각·독립 TTL: A를 쓰는 동안 B만 만료, B 복귀는 새 연결, A는 그대로. native expiry를 UI polling 없이 집행하는 실제 controller 경로에 연결한다. 시험에서 TTL을 단축하면 시험 전용 정책과 출하 기본값을 구분한다.
- delayed connect/park-confirm tap 및 onStop/sign-out/네트워크 종료 → 늦은 adopt/fallback0, 이미 active/parked 전체 종료, 재시작 시 새 연결. AVD의 실제 행동·callback/host 카운터로 확인한다.
- 구 host pause 미지원(해당PC 종료·업데이트 안내·다른 PC 유지), relay의 다른 기기 대체 후 자동 재탈취0을 최소 반례로 확인한다. direct는 기존 경로가 그대로여도 새 entry/handle 결합의 짧은 smoke1회는 필요하다.
- A/B 전환 뒤 기존 crop/크게보기 및 6M 저장/적용 대상, stale clipboard/preview가 다른 PC/로컬에 적용되지 않는 결합을 한 번 확인한다. 입력/화질 route 긍정은 이미 있으며 clipboard stale 경계와 구별한다.
- 최종 한도에서 폰 프로세스 스레드/메모리 및 양방향 control bytes를 측정한다. 현재 host→폰 0.56~1.27KB/s만으로 양방향 총량/폰 메모리/배터리까지 확인했다고 쓰지 않는다. 실폰 배터리는 미검증으로 남겨도 된다.

출하 gate는 실제 게시 방식 assembleDebug 기준으로 유지한다. hook 이름 리터럴과 실행 기능을 구분한 0.2.30 정정 기록을 계승한다. 제품 host를 APK 기능 검증의 시험 host 대신 띄우지 말고 fail-closed 격리 창을 계속 사용한다.

## 비차단 기록

native_bridge.run_detached는 `std::thread(std::move(f))` 생성 예외 뒤 f()를 실행한다. thread 생성 과정에서 인자가 이미 이동된 뒤 실패하면 fallback 함수가 비어 있을 수 있으므로 fallback을 보장한다고 단정하지 않는다. 별도 복사본/안전한 동기 fallback 등으로 정리할 수 있으나, 본 판정의 핵심 차단은 M1/M2/L1이다. table closer는 transitionMu_를 보유한 호출 문맥에서 dispatch되므로 동기 fallback이 실제 Close/join을 잠금 아래 실행하지 않는지도 함께 살핀다. 단순히 'map 락 밖'이라는 말로 모든 락 밖을 증명하지 않는다.

binding 조회로 shared_ptr를 얻은 뒤 controller 호출하기 전 재활성화될 수 있는 JNI 장기 작업에는 동일 binding의 승인 시점이 어디인지 명시한다. 현재 긍정 route 시험은 이 interleave를 검증한 것이 아니다. 장기 privileged action/비동기 clipboard/매크로는 시작 binding을 유지한다는 설계표와 실제 호출부를 최종 검토한다.

## 다음 연결

한도3대·5분은 아직 제안값이다. 사용자 답을 검증용이 기존 질문에서 수렴하고 정책 객체/UI/시험 기대값을 함께 고정한다. 답이 없는 상태를 승인으로 취급해 게시하지 않는다. M1/M2/L1과 V1은 같은 task·한 지시서에 취합해 작업용2로 진행한다. Codex가 작업용에 병렬 지시하지 않는다. 고정 보수 후보와 영향 범위의 독립 실행 근거를 받아 다음 소스/출하 판단을 한다.

제품·버전·설치본 변경, 빌드·AVD·commit/push는 하지 않았고 이 검토 파일만 작성했다.
