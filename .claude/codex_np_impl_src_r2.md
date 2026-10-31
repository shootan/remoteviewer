# NP 구현 r2 — SOURCE 재확인

2026-10-27 / Codex remote#tt7fcvky → 검증용 remote#7fsfy22z / 기존 task np-impl.

**NP 범위 SOURCE OK — r1 N1 닫음.** 고정 **e16ad0d95605abb874be3ce682c22ea7cd2109a5**의 해당 독립 검사로 진행한다. 이는 PC/server의 ⓪+①b 구현 SOURCE 판정이며 제품 UI EVIDENCE·실 NAT·APK NP 배선·운영 배포/전체 KA SOURCE 승인과 구분한다.

검토 diff **5db95dd..e16ad0d**, 전체8파일. 제품 변경은 `server.js` 예약 응답 callback의 fence이며 다른 실행 변경은 시험이다. rendezvous 헤더는 정책 설명, punch_any는 replay label/측정 집합 정정이다. 검사 시 HEAD 일치·tracked clean, diff whitespace rc0. 보고 `report_np_impl_r1.md` 최신r2와 README/history/관련 시험·저장 로그를 대조했다.

## N1: 닫음

각 예약 callback은 실제 send 직전에 **`relayEligibleByWireIp.get(clientIp) === eligible`** 및 `Date.now() <= eligible.expiresAt`를 검사한다. 다른 record로 교체/삭제/sweep되거나 만료됐으면 send 없이 반환한다. 이 검사와 send 사이에 await가 없어 같은 callback 안에서 새 connect handler가 끼어드는 틈을 추가하지 않았다. early뿐 아니라 legacy grace 예약도 같은 소유 fence를 적용한다.

동일tuple B가 A를 교체하면 A의 남은120/240ms 예약은 송신0이다. 새 record에는 별도 replied 집합이 있고 B의 자기 Punch가 그 record 아래서 자기 grace를 따른다. 같은 IP의 다른tuple A도 계속 Punch하면 현 record 아래에서 grace 응답을 받을 수 있으므로, 옛 예약을 살리는 방식으로 경로를 열 필요 없다. 이미 send한 datagram이 선로에 남을 수 있다는 한계는 유지한다.

## 직접 실행/저장 근거 구분

Codex 직접 실행:

- `node apps/directory/test/relay_reply_fence_test.js`: **9 PASS/0 FAIL**. 고정 `server.js`의 함수 원문을 가상 timer/Date/send recorder로 실행한다. 정상 early0/120/240, 같은tuple50ms 교체 후 송신0·B grace, 만료·다른tuple·legacy교체·sweep, 정상 legacy2500/2620/2740을 검사한다. 실제 network/서버 실행이나 파일 생성·삭제는 없다.
- `node --check apps/directory/server.js`: rc0.
- 고정 commit·tracked clean·diff --check 확인. 새 Viewer/native 빌드/실 서버/AVD 실행은 하지 않았다.

Codex가 직접 읽은 저장 결과(실행은 **작업용 귀속**):

- `np_impl_logs/re_fixed_3.log`: 같은tuple 첫reply29ms→B connect완료30ms→afterB=0/total=1, B 자기reply405ms(grace400), 기존 활성 lease 양방향 보존 및 전체21/21.
- `np_impl_logs/re_mutant.log`: 보수 전 c803768에서 같은 사례 **afterB=2/total=3 FAIL**, 다른 정상 사례 유지. 단순 무응답으로 시험을 통과시킨 보수가 아니다.
- `np_impl_logs/e2e_r2.log`: 전체 ALL PASS, 구 요청 모드/구 시험 서버의 legacy grace 및 relay label/direct 구분을 보존한다.

61/61×3·21/21×3·26/26 및 c803768 가상 반례7 FAIL은 작업용 보고 귀속을 유지한다. 이번 보수는 서버callback fence이므로 관련 target/서버 사례 중심으로 검사하고832 전체를 다시 돌리지 않은 것은 범위에 맞다.

## 정정/유지 계약

PC9건은 같은T0 측정, Host482·APK554/626은 다른 기준점의 참고, relay9회는 direct를 생략한 합성 입력으로 시험label/history/최신 보고가 정정됐다. 시험은 정책 선택을 검증할 뿐 현장 원인/직접비율 불변을 증명하지 않는다. `version>=2`는 현재 수용정책이며 미래 호환/인증 보장이 아니라는 주석도 수용한다. flag=false는 구 요청모드이고 실제 구 바이너리 실행으로 확대하지 않는다. 과거 보고의 잘못된 집계는 최신r2가 명시적으로 대체한다.

r1에서 수용한1000ms hold·구 서버 fallback·cancel/cadence·bounded stranger 계측·relay label·①a 미구현/무토큰 prflx 금지는 유지한다. 이번 보수로 별도 소스 범위를 늘릴 필요 없다.

## 다음 담당/판정 경계

검증용 remote#7fsfy22z가 **e16ad0d 고정 후보**의 독립 실행으로 연결한다. 자체 build-verify/고정 산출물hash와 안전한 내부 tmp runner를 사용하고 같은tuple회수·신/구 요청/서버·1000ms 경합/유실/cancel 및 relay 표시 영향 범위를 확인한다. 직접/relay의 **실 제품 창 표시**와 실제 연결/첫화면 시각은 headless probe/enum 검사가 대신하지 않는다. 실 NAT KPI는 후속으로 남기며 정책시간·UI시간·first presented를 분리한다.

APK는0.2.33 위 JNI options/report와 Kotlin 플래그/응답parsing을 함께 적용하는 기존 순서를 유지한다. 플래그만 먼저 보내지 않는다. 기준0a5e71d의 별도 KA N1~N3 SOURCE 상태는 해당 고정 보수 판정에 따르며 **NP SOURCE OK로 기반 전체를 main에 승인한 것이 아니다.** 검증용이 순차 통합/관련 회귀·검수 commit을 고정한다. 이미 게시된APK0.2.32는 영향 없다.

Codex 제품 편집·빌드·실 서버/프로세스 실행·삭제·commit/push 없음. 직접 실행은 위 비변경 함수검사/문법검사/Git 확인뿐이다. 미추적 운영 판정서 외 history/계획·Git MCP commit은 같은 구현 작업에 취합한다. 사용자 현재 조치 없음.
