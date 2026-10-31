# DXGI r3 — Codex 고정 후보 검토·범위 판정

2026-10-22 / task **t-dreqrszk** / 후보 **4db9f17214f8b27ca6be7b75c41e9ab8dc4419c5**.

**NEEDS_CHANGES.** D1~D4의 기존 SOURCE 판정은 유지한다. **E3의 present 실패→B 미보고/A 보존 및 성공 대조는 근거 확보로 닫는다.** E2는 제품 동등 시험 경로를 확인했지만 출하 뷰어의 모니터 선택이 막혀 있고 통합 변이도 충돌로 무효다. 아래 **① 변이 수정 + ③ 출하 목록 요청 보수**를 기존 task에 한 묶음으로 연결한다. **② hello 지연은 별도 재연결 결함 후보**로 기록·영향 분리한다. 현재 후보의 일반 구현 완료/통합·출하 승인은 아니다.

## 직접 확인한 근거

- 작업용 r3 보고, 검증용 `C:/work/gnlink-wt/verify-eop/.verify/verify_dxgi_wedge_r3.md`, f0ee888→4db9f17 제품/시험 diff 및 관련 목록·선택·hello 소스를 읽었다. 검증 worktree HEAD·tracked clean·CR-at-EOL 허용 diff-check 확인.
- 독립 출하 Viewer hash **2005ca23ba1c6b6c377da3b50eae9f78a49a4a501e244acf47a4cc9a88a898d5**, R3Test hash **c56f5a72001197342d2acb0aa3a75a279ad2d0596cfdee96f6323bbfeafaac68**를 직접 계산해 일치 확인했다. 나머지 r2 동일 산출물은 검증용 기록 귀속이다.
- 독립 원본 로그의 desktop38, E2 42, presentok36, presentfail 최초35/1 및 후속37/0 세 회, report_on_reveal33/3, monitor 변이 실패와 출하 monitor 실패를 직접 대조했다. gate9/self20·시험 Viewer 대체 rc1·출하 seam0은 검증용 원문 기록을 확인한 근거다. Codex가 제품 시험을 재실행한 것은 아니다.
- 실제 E2 picker 사유 부제 PNG, E3 rep2 restored-A PNG를 직접 열었다. E3의 주황 B가 desktop A 안에 포함된 것과 B 창 단독 캡처는 구분한다.
- `viewer_present.cpp` 실패 주입은 비디오 render를 건너뛰어 `presented=false`로 실제 제품 `note_present_result(false)`/exit43 분기에 들어가며, 성공 때만 호출되는 restore commit에는 도달하지 않는다. 제품 paint 경계의 유효한 실패 주입이다. 시험 플래그/환경 표식은 출하 경로에서 제외된다.

## ① monitor_as_desktop 변이 — 재작성 필요

**현재 FAIL을 유효 부정 대조로 세지 않는다.** `viewer_restore.cpp:58~59`의 non-Desktop guard는 유지되는데 변이된 `DecideRestore(monitor)`만 `{true,nullptr}`를 반환해, `fail`에 넘길 std::string을 nullptr로 구성하다 충돌한다. `0xC0000005`는 잘못된 자동 복원을 관측한 실패가 아니다. 출하 Monitor는 이유를 반환하므로 이것을 출하 충돌 결함으로 오인하지 않는다.

변이는 guard와 decision 양쪽을 일관되게 우회해 **명시 monitor를 실제 desktop 자동 선택으로 잘못 복원하는 경로**까지 실행하게 한다. 시험은 프로세스 생존·새 viewer의 연결/목록 처리 이후 잘못된 select/present 또는 desktop 보고 때문에 실패해야 한다. 단순 충돌·연결 불능·timeout은 INVALID/별도 실패로 구분하고 안전성 mutant가 잡혔다고 하지 않는다. worker 보고의 “변이 대조 포함 근거 확보”를 이 상태에 맞게 정정한다.

## ② hello 지연 — 별도 증상 조사, E3 대상 보존과 분리

**E3의 대상 계약은 확인됐다.** 네 회 모두 B 보고0·shell restore=desktop을 유지했다. 성공 세 회는 바로 A로 돌아갔고, 실패 회차도 두 번째 자동 재연결에서 A를 복원했다. presentok 대조와 report_on_reveal 유효 변이가 보고 시점의 차이를 실제로 구분했다. 따라서 E3의 B 미보고/A 유지 근거를 다시 전면 요구하지 않는다.

다만 최초 실행은 **35/1 FAIL로 그대로 기록**한다. 후속3PASS로 덮거나 “4회 전부 PASS”로 바꾸지 않는다. 직접 확인한 C1PKrA 원문은 hello11.252초·attempts181/badAcks25, host epoch2 종료 뒤 hello 성공, 목록 요청 무응답/15초 timeout, 첫 reconnect exit43, 다음 attempt의 A 복원이다. 원인/빈도/기준판 동일성은 아직 확정하지 않는다. r3 hello 코드 diff가 없다는 사실만으로 기준판 결함이라고 종결하지 않는다.

**판단:** 복원 descriptor 결함과 구분되는 **재연결 handshake/세션 수명 결함 후보**로 ledger에 등재한다. 검증용이 기존 fixture와 같은 “Host는 살아 있고 Viewer만 재생성” 조건에서 기준판/후보를 제한된 횟수로 대조하고, capability 갱신·hello ack 수신·host peer/epoch 전환·control timeout의 타임라인을 분리한다. 시험 directory/세션 정리 문제인지 제품 경로인지 근거를 남긴다. 이는 범위가 제한된 조사 승인이지 인증 완화/timeout 증가/새 protocol·재시도 확대 승인으로 읽지 않는다. 실제 경계 변경이 필요하면 그 부분만 설계 delta로 취합한다.

사슬 acceptance는 **같은 operation/host의 기존 최대3회 자동 복구 정책 안에서 올바른 A가 돌아오는 사용자 행동**이다. 반드시 첫 새 PID만 성공해야 한다는 요구를 새로 추가하지 않는다. runner가 첫 PID에 고정돼 후속 자동 복구 성공을 못 보는 부분은 전체 관찰 기한을 늘리지 않고 operation/attempt별로 추적하도록 정정할 수 있다. 이 경우 첫 시도 실패·hello 지연도 별도 관찰로 반드시 남긴다. 기존 FAIL은 소급 PASS 처리하지 않고, 수정 하네스의 새 실행을 별도 기록한다. 최종 A 미복귀·사용자 개입·재시도 budget 초과는 여전히 실패다. hello 증상 자체가 해소됐다는 주장도 하지 않는다.

## ③ 출하 Viewer의 모니터 목록 요청 — 이번 DW 범위에 포함해 보수

**포함한다.** 명시 monitor를 picker에서 다시 고르라는 B6 최소안이 유효하려면 사용자가 실제 모니터 선택을 할 수 있어야 한다. 테스트 Viewer에만 목록 요청을 넣어 버튼을 만드는 상태로 제품 동작을 승인할 수 없다. main5063cd9에도 호출이 없다는 소스 정황은 이 결손을 허용할 이유가 아니며, 어느 출하 버전부터인지 이번 검토로 확정하지 않는다.

좁은 제품 보수 범위:

- 인증된 현재 control 세션에서 host가 monitor messages 지원을 광고하면 **초기 화면 목록을 실제로 요청**한다. picker 시작 여부와 분리하고 기존 요청 큐/응답 타입을 사용한다. 예컨대 최초 지원 전환 시 1회 요청은 허용한다. 구형/미지원 host에는 보내지 않는다.
- 목록 수신은 조회다. **MonitorSelect를 대신 보내거나** 사용자 pick 없이 capture/stream/generation을 바꾸지 않는다. 목록이 왔다는 이유로 마지막 logical desktop descriptor를 monitor로 바꾸지 않는다. 자동 복원도 계속 desktop만 가능하고 explicit monitor는 picker+이유다.
- 연결 교체 시 capability/목록 소유를 현재 세션에 맞추고 중복 무한 polling을 만들지 않는다. 필요한 제한된 갱신만 기존 흐름으로 한다. 응답의 toolbar 갱신도 기존 UI thread 경계를 따른다.
- E2의 `GNLINK_VIEWER_TEST_REQUEST_MONITORS` 보조가 없어도 **출하 GNLinkViewer**에서 버튼이 생기고 실제 toolbar callback→monitor 보고→shell→새 viewer picker→수동 선택으로 이어져야 한다. 시험 Host가 알리는 가상 두 번째 항목은 계속 허용하지만 실 두 번째 물리 출력의 동일성 성공이라고 부르지 않는다. 가능하면 중복 시험 helper는 제거해 제품 조회 누락을 숨길 길을 없앤다.

최소 독립 확인: 출하 Viewer+격리 Host의2개 목록→실제 버튼/메뉴 선택→E2 사슬, 미지원 host의 monitor 요청0 대조, 목록만 도착했을 때 자동 선택/desktop descriptor 변경0, 재작성한 유효 mutant가 잘못된 자동 선택으로 실패. 변경 후 desktop 정상 복원과 gate/hash를 영향 범위에서 확인한다. 이미 통과한 store/GPU 전량·E3 여러 회를 무조건 반복하지 않는다.

## 다음 담당/남은 실기

검증용은 ①/③을 **기존 t-dreqrszk/작업용2**의 한 유효 보수 지시로 연결하고, ②는 관찰·원인 조사·다음 담당을 분명히 한 별도 결함 후보로 관리한다. Codex가 작업용에 병렬 지시하지 않는다. 관련 보고/history·계획 상태·Git MCP commit은 실제 변경과 같은 task에 취합한다.

D3 영속 저장소 소스/격리 시험·D4 test-only 순서 판정은 유지한다. 회사 PC NVIDIA/release-phase wedge/승격 ProgramData 실쓰기/재부팅·D4 recreate 실행·실 두 번째 출력 identity는 미검증이다. minidump·MT 출하·Bⓑ2 보류, 버전/서명/게시/설치/push 범위 불변. Codex 변경은 미추적 판정서/인계뿐이며 제품 수정·빌드·실서비스/설치/UI 조작·commit·push 없음.
