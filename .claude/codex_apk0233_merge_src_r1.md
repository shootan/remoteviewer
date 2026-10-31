# APK 0.2.33 병합 SOURCE r1

2026-10-27 / remote#tt7fcvky → 검증용 remote#7fsfy22z.

**NEEDS_CHANGES — 병합 M1(UA-2 동일 연결 복귀의 요청 상태 소실)을 보수한다. picker/decoder local-selection 충돌 해소 방향은 OK. 병합에 포함된 KA ②의 N1~N3는 별도 정본 `codex_keepalive_r2_scope2.md` 그대로이며 여기서 중복 반려하지 않는다. 0.2.32 후보 b554e76에는 이번 회귀가 없다.**

고정 `feat/apk-0.2.33` **1c2116d01b55e255b61758db09329b85e9bdb270**. 8731255의 5개 제품 충돌 combined diff, d62df9a 시험 복원, 이후 ka-r3 병합/1c2116d JNI, 부모6d85ce2 및 b554e76과의 decoder/adapter·UA-2 관련 diff와 실제 Kotlin/JNI/분류기 호출을 대조했다. 검사 시 tracked clean, `git diff --check b554e76..1c2116d` rc0. KA/multi 전체를 신규 전량 승인했다는 의미는 아니다.

## 유지된 경계

- local 선택4함수는 EntrySinkAdapter의 현재 token→decoder `*As`로 전달된다. 실제 `LockedState` 안에서 OwnerLocked를 검사하고 tag/local bound와 callback을 같은 lock 안에서 판단한다. stale token은 Abandoned/0/false이고 callback이 실행되지 않는다. 0.2.32 picker r3의 취소 완료 뒤 기록 틈을 다시 열지 않았다.
- `DecoderPendingSlot::Offer/Pump`와 bootstrap `PermitsQueue`를 병합해 6d85ce2의 오래된 직접 TryQueueFrame 형태로 돌아가지 않았다. 새 binder token 정책과 함께 유지된다.
- d62df9a는 병합 때 빠졌던 실제 decoder(binding용 fake NDK) 시험을 복원하고 [2b] local selection 소유 반례를 넣었다. 이 시험은 제품 decoder+adapter를 호출하며 단순 별도 모델 성공이 아니다. 다만 순차 반례17건을 실제 AVD 경합 재현이나 모든 token 수명 증명이라고 확대하지 않는다.
- UA-2 query/get/request는 activeBinding을 받아 bound entry controller에 한정한다. capability의 한 snapshot known/answers, 12필드 intent/seq 전달은 보존된다. connect/Apply/park reuse에서 backend 자동 재전송을 되살리지 않은 점도 맞다. 일반 품질 sync와 backend 요청은 분리된다.
- 새 standby JNI는 runner init/start/status/observe/provide/release를 노출했지만 현재 Kotlin 호출처0, `Preconnect.SEND_TO_REAL_HOSTS=false`다. 이 상태를 자동 연결 동작 구현/완료로 보고하지 않는다.

## M1 — park_reuse는 새 연결이 아닌데 UA-2 현재 요청을 지움

`MainActivity.kt:3124`의 `resetSessionDesktopBackend("park_reuse")`는 같은 PC의 **같은 controller/연결**을 resume한 뒤 `sessionDesktopBackend=null`로 만든다(:2712). 그러나 `DesktopBackendPresentation.present`는 asked==null이면 **native intentAsked/intentSent/seq 상관검사보다 먼저** IDLE/CHECKING으로 반환한다. 요청 보류/접수·실제 적용/정책 대체/거부가 현재 요청의 결과라는 정보가 사라진다.

구체 반례: A에서 GDI 요청, 답이 아직 안 왔거나 옛 status가 남음→A parked→B 사용→같은 A resume. native에는 A의 intent가 남았는데 asked가 null이라 pending 분류를 건너뛰고 옛 status를 IDLE/점등으로 보여줄 수 있다. 답이 온 경우도 접수/실제 적용이라는 구분이 사라진다. **연결 범위 설정은 PC를 목록에 보관하는 동안 같은 연결을 유지한다면 그 연결의 요청 상태도 보존**해야 한다.

필요: UA-2 choice/현재 요청을 해당 entry/connection incarnation에 소유시켜 화면 switch/복귀에 복원하거나, native에서 현재 requested code까지 일관되게 snapshot으로 받아 분류한다. 새 연결/새 활성 세션·로그아웃·해당 entry 종료에는 reset한다. B에 A의 요청을 보내거나 보여 주지 않으며 자동 backend 재전송은 여전히0이다. 단순 reset 삭제로 전역 A choice가 B에 남는 형태도 피한다.

후속 kept 모델 A로 **실제로 원 연결을 끝내고 새 session을 activation**하는 경로에서는 reset이 맞다. 그 구현이 아직 없으므로 현재 parked-session resume를 새 연결로 간주하지 않는다. 모델 A 전환으로 해당 경로를 제거한다면 제거된 경로/새 세션 기본 복구를 같은 반례로 증명해 M1을 닫을 수 있다.

필요한 제한 검사: A 미응답/옛 status→park→B(다른 backend 상태)→A 복귀에서 APPLYING 유지, 이후 A seq의 응답에서 RECEIVED/APPLIED/REFUSED 구분; 새 연결에서는 요청 없음으로 시작; 자동 요청0. 제품 UI의 활성 entry와 실제 request/표시를 묶는다. 현재 method16/ua2 13/desktop9는 단일 연결 회귀여서 이 switch 반례를 대신하지 않는다.

## EVIDENCE/다음 단계

보고된 병합 APK noseam9dbd492d·shseam0853297e, 단위290/0, method/UA-2/prelogin/layout/touch·native 결과는 **작업용 귀속**이다. Codex 새 APK/AVD/빌드 실행은 없다. 검증용이 고정 후보를 독립 실행할 때 picker local 표시/취소/재선택 및 A↔B stale binding, M1, SH 실제 output/clear/input fence의 영향 범위를 확인한다. 0.2.32 독립 근거를 새 병합 APK의 실행 근거로 승계하지 않는다.

③의 N1 malformed HelloAck·N2 runner/handshake lifetime·N3 내구 gen 저장은 1c2116d에도 포함돼 있다. 같은 writer가 통합 지시서로 한 번 보수하고 source 범위별 결과를 회신한다. 새 standby 호출처를 연결할 때 directory/계정/lease incarnation으로 늦은 Provide와 SessionOver를 가려야 한다. 현재 key만의 미사용 API를 그 수명 검증 대신으로 쓰지 않는다.

자동 preconnect 출하 OFF·NAS/사용자 설치본 무변경. 현재 SOURCE/배포 완료 승인은 아니다. Codex 제품 편집·빌드·시험·삭제·commit/push 없음. 미추적 판정서 외 history/계획·Git MCP commit은 검증용이 해당 구현 기록에 취합한다. 사용자 현재 조치 없음.
