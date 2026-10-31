# NP 구현 r1 SOURCE 검토

2026-10-27 / Codex remote#tt7fcvky → 검증용 remote#7fsfy22z / task np-impl.

**NEEDS_CHANGES — N1: connect가 교체된 뒤에도 옛 조기 응답 타이머가 송신한다. 1000ms 경합·구 서버 fallback·계측/relay 라벨 방향은 수용한다. N1 및 아래 근거 표현을 같은 작업에 취합해 제한 보수 후 SOURCE 재확인→검증용 독립 실행으로 잇는다. 제품/UI/실 NAT·APK·운영 게시 OK는 아니다.**

## 고정 대상/확인 범위

- `feat/np-early-relay` **5db95dd6436d84c1b1e23b478df955a3a41cb3fa**, 제품 **c803768**, 기준 **0a5e71d**. 검사 시 해당 worktree HEAD 일치·tracked clean, `git diff --check 0a5e71d..5db95dd` rc0.
- 전체 변경18파일(제품·시험·문서)을 검토했다. server connect/UDP dispatcher/예약 응답, rendezvous 경합·report·seam, bootstrap/client/enum/viewer wiring, CMake·시험/README·문서와 보고·proposal r2를 대조했다. c803768..5db95dd는 시험·문서뿐이고 제품0이다.
- 저장 `np_impl_logs/e2e_3.log`, `dir_test_3.log`의 핵심/최종 결과를 읽었다. 61/61×3·mutant 실패·832·26×3 및 빌드는 **작업용 실행 귀속**이다. Codex 새 제품 빌드/Viewer/Host/디렉터리 서버 실행은 없다.

## N1 (P2) — superseded connect의 조기 reply가 새 legacy 경합에 유입

`server.js:1778~1800`의 `relayScheduleReply`는 호출 시점의 eligible로 early 여부를 정하고, 0/120/240ms의 callback은 `relaySock` 존재만 검사해 송신한다. 그 사이 `/api/connect`가 `relayEligibleByWireIp`를 새 record로 교체하거나 유효기간이 끝나도 옛 callback은 살아 있다. **현재 connect/record·tuple·expiry를 송신 직전에 다시 결합하지 않는다.**

반례 순서:

1. A는 relayEarly accepted. tuple X에서 Punch를 받아 세 번의 즉시 계열 reply를 예약한다.
2. 50ms에 A를 포기하고 **같은 wire tuple X**를 사용하는 B가 무플래그 connect를 한다. 이 경우 B는 legacy 경합이며 새 record에는 early가 없다. socket/끝점의 재사용 또는 NAT mapping 재사용이므로 이 순서를 금지할 계약은 없다.
3. A의 남은120/240ms callback이 X에 여전히 유효49B Punch를 보낸다. B의 legacy first-answer-wins는 이 옛 reply를 B의 grace 이전 relay 응답으로 채택할 수 있다. direct가 예를 들어300ms에 가능해도 relay를 먼저 고른다.

즉 포트 일치 검사는 **예약 시점**의 다른 socket은 가르지만 **같은 tuple의 다음 connect**를 보호하지 못한다. README/정본의 무플래그 connect grace 보존 조건과 어긋난다. `relay_early_test`의 무플래그 후속 사례는 앞선 early 3회를400ms 이상 기다린 뒤 시작하므로 이 틈을 검사하지 않는다.

Codex 직접 제한 검증: 고정 `server.js`에서 **해당 함수 원문을 추출해 Node VM으로 실행**했다. 외부는 가상 timer·send recorder·eligible map과 endpointKey/buildPunchPacket 대역이며 실제 network/파일 생성·삭제는 없다. A를 예약하고0ms callback 실행→50ms에 같은tuple B legacy record로 map 교체→남은 callback 실행 결과:

```
sends = [0ms, 120ms, 240ms] to 127.0.0.1:42001
stale sends after B = 2
```

이는 **제품 함수의 타이머/record 수명 반례**이며 실제 구 Viewer/NAT를 실행해 현장 장애를 재현했다는 뜻은 아니다.

필요 보수: pending reply를 해당 eligibility/connect incarnation에 결합하고 callback 송신 직전에 현재 유효 record·expiry·대상 소유를 확인하거나, record 교체/만료 때 해당 timer를 취소한다. early만을 위해 일반 grace를 없애거나 새로운 무토큰 prflx를 허용하지 않는다. 이미 선로에 나간 datagram을 회수할 수 있다는 보장도 하지 않는다. 이번에는 **교체 이후 서버가 새로 보내는 옛 예약 응답**을 막으면 된다.

제한 반례는 A early 예약→3회 완료 전 같은tuple B legacy connect→A의 나머지 callback 송신0→B의 자기 Punch 응답은 자기 grace 이후, B direct@300이면 direct를 선택하는 순서다. eligible 만료·같은 IP 다른tuple 교체·정상 early3회/legacy grace도 함께 확인한다. 가상 timer로 정확히 겨냥한 뒤 필요한 실제 server 결합만 추가하며 기존832 전량을 매 수정마다 반복할 필요 없다.

## 수용한 구현

- authenticated connect + 유효 observation/legacy relay 대상에서만 `relayEarly:"accepted"`. 요청 없음/미수락/구 서버는 legacy 경합. standby 클래스에 early를 적용하지 않는다. 조기 Punch 자체가 Hello bind를 하지 않고 기존 활성 lease를 직접 선점하지 않는 소스 구조를 유지한다.
- viewer가 요청한 뒤 **서버 accepted를 확인한 경우에만** hold를 사용한다. 409 재관찰 retry도 플래그를 같이 전달한다. T0는 DNS 후보 해석 뒤 첫 full batch 송신 직전 monotonic 시각이다.
- held relay가 있으면 strict T0+1000 경계에서 commit하며 receive 반환 직후에도 같은 경계를 검사한다. 그때 relay가 없으면 기존4초 예산 내 계속 기다린다. 늦은 relay/그 전에 도착한 direct·private, cancel 우선, 한 번의 choose 지점을 유지한다. 기존 send cadence/read budget/일시적 UDP 오류 분류를 다시 바꾸지 않았다.
- 비일치 tuple8개 bounded 집계·count/time/첫길이만 남기며 token/패킷 원문0. CandidateKind::Relay→bootstrap/session.relay→viewer relayPath 전달이 보존된다. 도달성 관찰은 인증 성공/첫 화면 성공과 별개다.
- clock/receive seam은 기존 시험 target에만 정의된다. 이번 probe는 제품 bootstrap을 호출하는 비출하 도구이며, 가짜 Host의 UDP 응답 성공을 실제 Host→Viewer 영상 E2E로 보고하지 않는다.

## 근거 표현/후속 검증

1. **replay 기준점 보정:** 5db95dd는 APK554/626ms를 참고로 분리했지만 Host482ms(wake→Host punch)를 PC9건의 viewer Punch→answer와 여전히 한 measured set으로 합쳤다. Host482도 별도 참고/synthetic 지연 사례로 표시한다. 실제 같은 T0의 PC direct 표본은9건이다. relay-won9건도 원래 direct 수신이 전혀 없었다고 관찰한 것이 아니라, **시험에서 direct를 생략한 합성 입력**이다. 이 시험은 정책 결과를 보여줄 뿐 현장 직접 비율 불변/원인 해명을 증명하지 않는다. 관련 시험 label·report/history를 같은 수정에서 정정하면 된다.
2. `version>=2`는 현재 구현의 명시적 수용 범위이지 임의 미래 버전과의 호환을 증명한 것은 아니다. 현재 Host/relay의49B v2 응답 호환과 구/새 경합의 실제 정상 형식을 독립 검사에 포함하고, 미래 호환 보장 표현은 피한다. unknown version이 현재 지원 범위에 포함되는 이유/정책은 정본에 명시한다. 이것을 상대 Host 인증으로 승격시키지 않는다.
3. 새 바이너리의 flag=false 시험은 **구 요청 모드** 검증이다. 구 바이너리 자체 실행이나 운영 서버와 완전 동일한 검증으로 부르지 않는다. 기준0a5e71d의 구 server 역시 KA 포함 시험 서버다. 보고가 이 차이를 공개한 점은 유지한다.
4. APK 순서 조정은 수용: 현재 scope는 PC/server 공용 변경과 legacy overload 호환까지다. 0.2.33 위에서 JNI의 options/report와 Kotlin 요청/응답 parsing을 함께 적용해야 하며 **플래그만 먼저 보내면 안 된다**. 아직 APK NP 동작 구현/검증 완료가 아니다. 공용 형식/enum 변경의 Android 빌드·경합 회귀는 통합 때 남는다.
5. SOURCE 보수 후 검증용이 고정후보에서 독립 실행한다. 선택 ms·전체 open ms·실제 Hello/control/첫 화면은 각각 구분한다. relayPath 사용자 노출 변화는 실제 격리 제품 창의 direct/relay 표시로 확인한다. DOM/enum grep/Headless probe는 그 화면 증거가 아니다. 실 NAT KPI·배포는 기존 후속으로 남는다.
6. 기반0a5e71d에 포함된 **KA SOURCE N1~N3**(`codex_keepalive_r2_scope2.md`)는 별도 미결이다. NP 범위 검토가 이를 승인하거나 NP branch 전체를 그대로 main에 넣을 권한이 아니다. 승인된 변경 통합/충돌 검토와 KA 보수 상태를 검증용이 고정한다. 이미 게시된APK0.2.32와 묶지 않는다.

검증용 remote#7fsfy22z가 N1+근거 정정을 **같은 np-impl task의 한 유효 지시서**에 취합해 기존 작업용2에 전달한다. 논쟁할 구현 조건이 있으면 이 파일을 참조해 반론/근거를 한 번에 합의한다. 새 병렬 writer·운영 설정·공유기·설치본 조작은 없다.

Codex 제품 수정·빌드·서버 실행·삭제·commit/push 없음. 직접 실행은 위 비변경 Node 함수 검증과 Git diff 검사다. 이 미추적 판정서 외 history/계획·Git MCP commit은 해당 구현 작업에 취합한다. 사용자 현재 조치 없음.
