# SH strength r3 — 제품 옵션·SOURCE·검증 범위

2026-10-27 / 검증용 `remote#yj8mrmrx` / 입력 `report_sh_strength_r2.md` r3 전문.

**NEEDS_CHANGES — B의 N1 layout 대기/재개와 실제 표시/입력 소유 경계가 남았다. N2 중복 redraw 제거는 SOURCE OK. 기본 A·정상 제품 설정 B 채택은 유지하며 철회하지 않는다.**

고정 제품 **2f6905d18fa0e3d4ab97d8d21f87c7a14b9ef935**, 시험/통합 HEAD **05fe4b918110061b5f1bfcbf1e50c7541a9d486e** 각각 전체 diff·정책/renderer/Activity/시험을 읽었다. whitespace rc0. 이 HEAD APK는 picker 최종 보수 미포함이며 출하 후보가 아니다. UA2는 별도 판정서다.

## 수용한 부분

- `SharpenMethod`는 강도와 독립된 저장 키이며 미설정/알 수 없음은 DEFAULT A다. UI의 “기본 / 화면 크기 (실험)”·지연/발열 설명, OFF direct 의미·기존 강도 저장 보존이 계약에 맞는다. B의 설정은 제품 UI에서 고를 수 있고 시험 extra는 여전히 seam 전용이다.
- 새 renderer는 기존 release 확인 경로를 거친다. method 설정은 새 renderer에서 읽고, 초기 placement 전송 메모를 새 renderer마다 초기화한다.
- **N2 닫음**: Activity의 동등값 dedupe와 renderer의 최신 wanted snapshot/한 pending 작업, 적용값 동등 비교로 status poll마다 redraw하던 경로를 막았다. 저장 `b3_latency_ab/latency_ab.json`에서 glRedraws A0/B0을 확인했다. 이는 해당 변화 없는 placement 구간의 작업용 실행 근거이며 GL 지연 burst·정지 resize 전체 성공의 대용은 아니다.
- 첫 bind 때 path NONE인지가 아니라 **B가 의도됐는지**로 view buffer를 정하고, placement에 원래 view 치수를 담아 비율로 그리는 수정은 r2보다 개선됐다. 다만 아래 N1은 이 방식만으로 닫히지 않는다.

## N1 잔여 (P2) — layout 재시도 후 잘못된 크기 통과·정지 redraw 유실·입력 fence

### a. 크기가 계속 다르면 2회 뒤 영상 draw로 흘러감

`SharpenRenderer.draw`의 조건은 **`!matches && clearSwapsForLayout < 2`**다. 일치하지 않는 상태로 clear swap을2회 하면 다음 호출은 이 if를 벗어나 `frameWaitsForLayout=false`, updateTexImage, 영상 draw/swap을 수행한다. 즉 “view 크기 output에만 그린다”는 주석과 반대다. clear swap 반환 실패도 현재 이 분기에서 검사하지 않는다.

비율 quad는 배치를 일부 보존할 수 있어도, 잘못된 크기의 output에 그려 다시 확대된 프레임을 **B의 올바른 output/ready**로 승인할 근거가 아니다. source-size B 혼합을 재시도 상한으로 해결한 것은 아니다.

필요: mismatched layout은 영상 draw/qualified ready로 통과시키지 않는다. 제한된 retry/시간 뒤에는 안전한 실패·direct 복귀 등 terminal 처리를 사용한다. clear swap도 실패를 처리하고, counter는 일관된 layout episode에 귀속한다. 일치하게 된 뒤만 정상 프레임을 채택한다.

### b. 새 입력이 없는 redraw가 크기 불일치에 걸리면 clear 뒤 재개되지 않음

이미 latched frame이 있는 정지 화면에서 새 placement를 받으면 setter는 `draw(newFrame=false)`를 호출한다. output 크기가 아직 옛 값이면 clear swap하지만 **`if(newFrame)` 때만 frameWaitsForLayout=true**로 둔다. 예약 retry도 그 bool이 true일 때만 실행한다. 따라서 redraw에서는 false 그대로라 예약 작업이 아무것도 하지 않는다. 같은 placement의 다음 poll은 이제 dedupe되어, 새 영상 입력이 올 때까지 검은 clear 결과가 남을 수 있다.

필요: **새 frame을 latch할 대기와 기존 frame을 새 layout으로 redraw할 대기**를 구분한다. 새 decoder frame이 없어도 resize 완료 뒤 마지막 보관 frame을 새 placement로 실제 한 번 그려야 한다. 이 redraw에 updateTexImage를 무조건 호출하거나 새 decode/first-output/latency 샘플로 중복 계수하지 않는다. 최신 layout 하나로 병합·유한 retry를 유지한다.

### c. 새 view 치수만 전달하는 것은 표시·입력 generation 확인이 아님

현재 `DisplayPlacement`에는 view 치수는 있으나, UI `ViewerViewport` 갱신→GL handler 적용→최종 TextureView latch에 대해 **현재 layout이 화면에 채택됐다는 반환 경계**가 없다. 입력은 UI의 최신 viewport를 사용한다. GL이 지연된 동안 resize/배치 변경을 끝낸 뒤 새 remote tap이 오면 화면은 옛 배치인데 좌표는 새 배치일 수 있다. 진행 중 gesture 취소만으로 그 다음 tap까지 보호되지는 않는다.

이는 r2 N1의 actual output/layout 채택·입력 fence 요구의 잔여다. 최신 layout/surface/renderer 소유 snapshot과 실제 표시 확인을 연결하고, 아직 그 layout이 표시되지 않은 동안에는 옛/새 좌표를 섞어 원격 입력을 보내지 않는다. UI와 GL을 장시간 서로 기다리게 하거나 네트워크 응답까지 잠글 필요는 없다. pan을 로컬 제스처로 처리하는 기존 규칙도 유지한다.

제한 반례를 한 묶음으로 둔다: (1) EGL 크기를 3회 이상 옛 값으로 고정해 영상 draw/ready0 및 bounded 복귀, (2) **decoder 새 frame0**에서 resize·정지 pan 후 실제 redraw와 timestamp 유지, (3) GL layout 적용/최종 latch를 잠시 보류한 뒤 새 remote tap → 잘못된 좌표 송신0, 새 layout 표시 뒤 정상 owned Host 좌표. 수정 전 실패를 남긴다. A/B 재접속 전체·기존 강도 행렬을 매번 반복할 필요는 없다.

## EVIDENCE 범위 — 보고된 시나리오가 자동 검증 전부를 대신하지 않음

직접 읽은 method_ui 시험은 기본→설정 B→다시 대상 선택→off/on→A→재시작 저장 복원이다. 실제 제품 버튼과 화면이 연결된 유용한 근거이며 `b3_method/method_2_display.png`의 원격 패턴 표시도 직접 열었다. 하지만 **회전/분할 resize·GL실패→fallback·정지 pan·스크롤 shimmer·모서리/letterbox 무입력**을 이 함수는 실행하지 않는다.

strength_ab의 B crop/rot90은 renderer 입력 변환 fixture로서 유효하지만 Activity의 회전/layout 수명 시험과 다르다. touch_ab의36 checks도 고정 라임 블록·전체/크게보기 중심이며 모서리/letterbox 및 stale layout tap을 포함하지 않는다. latency_ab의6 checks는 로그 존재/수집이고 성능 기준 통과가 아니다. 따라서 보고의 “이 명령들로 회전/resize·pan·모서리·shimmer 검증” 안내는 **아직 구현되지 않은 행동까지 포함한 것으로 수정**한다. 실제 남은 자동 행동은 통합 검사에서 수행하거나 실패/불가 근거를 명시한다. 실폰만의 항목으로 일괄 미루지 않는다.

작업용 인용: method16, strength30, compare26, touch36, latency6, 단위263/0·gate7/양성대조FAIL2. Codex 새 AVD/빌드/독립 시험 실행은 없다. 비용 JSON의 12개 window p95 중앙값과 전체 segment p95는 구분한다: 저장 sample의 전체 segment는 off88.070/A100.029/B110.595ms이다. 보고의 약85.1/98.9/110.6은 window p95 중앙값이며, S4 통과/실폰 성능이 아니다. B 실폰 화질·지연·발열은 기존 결정대로 게시 후 실기 대상이고 기본 A 유지다.

같은 SH 담당/기존 정본에 N1 a~c와 빠진 자동 행동을 취합해 제한 보수→변경 diff SOURCE→고정 통합 APK의 검증용 독립 EVIDENCE로 잇는다. 게시 승인·완료는 아직 아니다. picker 최종 보수와 UA2 잔여는 별도 정본으로 관리하되 writer를 늘리지 않는다. PC163 게시를 다시 열지 않는다. 사용자 현재 조치 없음. Codex 제품 편집·빌드·실행·commit/push 없음; 공개 history/계획·Git MCP commit은 같은 구현 작업에 취합한다.
