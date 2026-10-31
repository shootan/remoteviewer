# UA-2 APK SOURCE r2

2026-10-27 / 검증용 `remote#yj8mrmrx` / 입력 `report_ua2_apk_r1.md` r2 전문.

**NEEDS_CHANGES — N1의 처리/실제 적용 의미, N3의 capability snapshot 두 잔여를 한 묶음으로 보수한다. N2 창/바탕화면 분리와 N1의 intent/seq 상관 방향은 수용한다.**

대상 제품 **cf8c021cf6f60e3c89b4ff80fb1ecf17330d3501**, 시험 **41de4cfaf3adb05235278bd4fa44e0fca514e6b7** 각각 전체 diff와 호출부. 통합 HEAD **05fe4b918110061b5f1bfcbf1e50c7541a9d486e**는 이 변경을 포함하지만 미완료 picker 보수를 포함하지 않으므로 출하 후보가 아니다. immutable ref로 검토했으며 SH 변경은 별도 판정이다. 제품 diff whitespace 검사 rc0.

## N1 잔여 (P2): handled를 실제 backend 적용 성공으로 표시

`DesktopBackendPresentation.present`는 current intent/seq·NotYetApplied를 확인한 뒤 requested==asked, WINDOW_TARGET 아님이면 곧바로 APPLIED로 간다. 문자열은 **“요청: X 적용됨 · 지금 바탕화면 캡처: Y”**다. 그러나 기존 Host가 답하는 handled는 요청을 처리/저장/거부한 의미이며 실제 재시작 성공이 아니다.

- `host_stage_backend.cpp`에서 desktop이 비활성/idleDetached이면 요청은 저장만 하고 active는 이전 값이다.
- 실제 restart가 실패해도 requested는 새 값이고 active만 prevActiveBackend로 복구한다. handled marker는 여전히 완료로 기록된다.
- 예: 현재 요청 GDI, 충분히 최신 seq, NotYetApplied 없음, requested/effective=GDI, active=DXGI, flags SessionScoped(창 아님). 현재 UI는 **“GDI 적용됨 · 지금 … DXGI”**다. 이는 source에서 실제 가능한 응답이며 backend 전환 성공 근거가 아니다.
- quarantine 요청 DXGI/effective WGC/active WGC도 요청 그대로 DXGI가 적용됐다는 문구보다 정책 대체가 적용됐음을 구분해야 한다.

보수는 **요청 처리 확인·대기/저장·정책 제한·실제 현재 방식**의 표현을 분리하는 것이다. effective도 이미 wire/JNI에 있으므로 활용할 수 있다. active가 effective에 도달하지 않았는데 “적용됨”을 쓰지 않는다. 현재 wire로 idle 저장과 재시작 실패의 정확한 원인을 구분할 수 없으면 “요청 접수, 현재 Y”처럼 사실만 설명하며 실패 원인을 만들어내지 않는다. Host wire/이미 게시한 PC163을 이 표시 보수 때문에 변경할 필요는 없다.

또 하나: asked==null 분기는 NotYetApplied를 보기 전에 IDLE로 돌아간다. 아직 현재 owner의 상태가 없어 Host가 0 필드+NotYetApplied로 답할 수 있으므로 이때 “지금 바탕화면 캡처: ?”를 확인된 IDLE처럼 내보내지 않고 CHECKING/상태 대기로 유지한다. 유효한 현재 상태가 있는 경우는 실제로 확인된 범위만 보여준다.

제한 사례: (a) 위 GDI requested/effective≠active, (b) quarantine 대체, (c) asked없음·seq있는0필드/NotYetApplied, (d) 정상 requested/effective/active 일치. 문구까지 기대를 명확히 하고, 하나의 처리 상태를 바꾼다고 terminal prelogin 거부를 다시 무한 APPLYING으로 만들지 않는다.

## N3 잔여 (P2): native JNI가 known과 지원 여부를 서로 다른 시점에 읽음

`nativeGetDesktopBackendStatus`는 `HostAnswersDesktopBackendStatus()`를 먼저 읽고, 다음 원소에서 `HostRoleKnown()`을 별도로 읽는다. 첫 pong 처리는 `desktopBackend_.SetHostAnswers(true)` 뒤 `hostRole_.Pong(...)`으로 known을 게시한다.

가능한 순서: JNI가 **answers=false**를 읽음 → 첫 pong이 answers=true/known=true를 게시 → JNI가 **known=true**를 읽음. 만들어진 raw 배열은 known=true+answers=false라 새 Host가 `NO_ANSWER_HOST`로 표시된다. pure 테스트가 올바른 조합을 넣는 것만으로 이 native 조합을 배제하지 못한다. 실제 AVD에서 이 짧은 경합을 재현했다는 주장은 아니다.

호스트 역할의 **한 Load에서 얻은 known+capture flags**로 capability 두 값을 함께 만들거나, 동등한 coherent snapshot을 사용한다. 이미 `HostRoleGate`가 그 pair를 한 atomic word로 제공하므로 새 전역 상태 기계는 필요 없다. 먼저 bool을 읽고 다시 known을 읽는 식의 순서 조정만으로 해결했다고 하지 않는다. 첫 pong 경계의 제한 native 반례/읽기 검증을 추가한다.

## 닫은 부분·근거

- native intent {asked/sent/sentSeq}는 Request/Consume에서 같은 mutex로 기록하고 Query는 마지막 change intent를 바꾸지 않는다. status seq가 현재 sentSeq보다 오래됐거나 아직 새 intent를 안 보냈으면 대기한다. backend 코드가 같다는 이유만으로 처리됐다고 추정하던 r1보다 맞는 구조다.
- handled prelogin 거부는 REFUSED terminal, WINDOW_TARGET은 STORED_FOR_DESKTOP, 현재 창은 WGC로 별도 표시한다. 저장 이미지 `ua2r2_2/ua2_3_after_pick.png`와 로그에서 GDI stored/현재창WGC·GDI 점등을 직접 확인했다. 이 점등은 **Host가 처리한 바탕화면 요청**이며 active actual radio라는 이전 의미와 다르다. 현 제목/상태 줄로 그 의미가 드러나는 방향을 수용한다.
- unconditional session-scope hint를 제거하고 응답 SESSION_SCOPED에 따라 범위를 안내한다. 구 Host는 요청 후에도 범위 미보장을 유지하며 새 조회를 보내지 않는 방향이 맞다. 남은 N3는 그 삼분류를 만드는 native snapshot 경계다.
- “약2초” 조회 정정, global 자동 재전송 제거·SH 저장 보존·새 연결 reset은 유지된다.

262/263 단위, 현 Host ua2 13/13·구 Host ua2_old 7/7은 **작업용 실행 보고**다. Codex 새 AVD/빌드 실행은 없다. A/B/A 순서 시험에서 r1 변이가 살아남았다는 한계를 기록한 것은 맞다. 그 사례는 late B의 requested≠asked라 r1도 APPLYING을 내기 때문이다. 다음 작은 단위 보강에는 **old A 응답(requested가 최신 A와 같음) seq3 < latest sentSeq5**를 넣어 seq 방어를 실제로 검출한다. 실 Host에서 전체 A/B/A를 억지로 재현하도록 새 시험 규모를 늘릴 필요는 없다.

기존 UA2 같은 담당에게 잔여 두 경계·제한 반례를 한 묶음으로 반영→해당 diff SOURCE 재확인한다. 닫힌 N2/기존 전량 시험을 다시 관문으로 만들지 않는다. picker 미결과 SH는 각 정본으로 유지하고 최종 통합 APK의 고정 해시/독립 실제 UI EVIDENCE 뒤에 게시로 잇는다. 사용자 현재 조치 없음. Codex 제품 수정·빌드·시험 실행·commit/push 없음; 공개 기록/계획·Git MCP commit은 같은 구현 작업에 취합한다.
