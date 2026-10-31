# sh-strength r2 — SOURCE·근거·B 선택 옵션 결정

2026-10-26 / 검증용 `remote#yj8mrmrx` 회신 / 입력 `.claude/report_sh_strength_r2.md`, 기존 계약 `codex_field_0162_0231.md` §4·§4B.

**결정: APK 0.2.32에 B를 사용자 선택 옵션으로 구현하는 것 GO. 기본은 A를 유지한다. A 강0.60 SOURCE OK. 현재 B는 출하 SOURCE/EVIDENCE OK가 아니며, 아래 N1·N2 보수와 통합 자동 검증 뒤 최종 후보로 판단한다.**

고정 후보 **6d5c032d5df4bbc04882458e967c08807b9f7c6c**: A `3dc15c7e35a561488ee55442f4daf5c11bec4234`, B `2b5a718a767949c7500ee14cc2e018af510724d2`, 시험 `6d5c032`. `e8123e4..6d5c032` 전체 diff와 실제 renderer/정책/Activity/하네스 호출을 읽었다. picker N1·N2는 별도 미결이며 이 판정으로 해소하지 않는다.

## 1. 근거 판정

Codex 직접 확인:

- 저장된 `ab1/strength_ab.json`, `cmp3/compare_ab.json`, `lat2/latency_ab.json`과 시험 소스 대조. `ab_text.png`, `ab_lines.png`, `ab_checker.png` 직접 열람. B의 글씨·가는 선 대비가 더 강한 차이는 보인다.
- 같은 원본을 넣는 결정적 renderer fixture에서 .20/.35/.60 최종 픽셀 차이가 존재하고, B 확대와 A 출력 후 보간의 차이도 존재한다. 현재 시험의 **강도 전달이 무시되는 버그라는 설명은 지지되지 않는다**. 사용자 폰의 모든 draw까지 이 fixture 하나로 증명한 것은 아니다.
- 실제 APK의 ×1.333 고정 패턴 ROI에서 B.35/A.35 글씨 edge는 **+37.38%**. A.60/A.35는 **+4.47%**, A.60/A.20은 **+9.96%**로 JSON을 직접 재계산했다. 이 값은 edge 지표의 비율이며 “가독성 37% 향상”이 아니다. 저대비 글씨는 A 강도 변경에도 큰 차이가 있으므로 “A.60은 모든 글씨에서 무의미”도 아니다.
- 비용 JSON의 **12개 5초 window p95의 중앙값**은 off 87.160 / A.60 96.418 / B.60 108.099 ms, B−A **11.681 ms**. 마지막 draw wall 관측값들의 중앙값은 A 12.733 / B 17.657 ms. 전자는 전체 60초 샘플을 합친 p95가 아니며, 후자는 모든 GL draw의 분포가 아니다. 소프트웨어 AVD에서의 참고 비용으로만 사용한다. 각 segment 전체 분포는 이미 앱 로그에 있으므로 최종 보고에 함께 사용한다.
- 현재 보관 APK 해시: noseam **9c67f8d01c50151ec27c2d6ccf0cbd2f503f00145ae630d049db44f756859e37**, seam **95ae4996eecf213863e1a60d40543454c165906586c73c37a90c92e86fbe8a9d**. 현재 noseam의 B 스위치는 항상 0이므로 **일반 사용자가 B를 고를 UI는 아직 없다**.

실행 주체 구분: fixture 26/26, AVD compare 14/14, touch 36/36, 빌드·단위·gate는 작업용 보고다. Codex가 AVD를 독립 재실행한 것은 아니다. 현재 실제 APK 비교는 **같은 정적 패턴을 각 설정별로 다시 접속해 촬영한 것**(`scenario_compare_ab`); 측정 영역 밖 pulse도 있다. 동일 encoded AU/decoder timestamp의 same-frame 증거와 구분하며, 그 역할은 결정적 fixture가 맡는다.

검증용의 “위치 효과가 사용자 불만의 원인”은 **확정 대신 유력한 GNLink 내부 설명**으로 정정한다. 이 AVD 배율·ROI에서 B 효과가 A 강도 간격 효과보다 큰 근거는 충분하다. 실제 폰·OSLink의 해상도/배율/인코딩/색 변환까지 맞춘 원인 확정은 아니다. flat 한 색 영역의 일치도 전체 색 변환 동일성을 증명하지 않는다. `_clip`은 밝기≤2 또는 ≥253인 화소 비율로 흰 배경·원래 검정도 포함한다. 증가 자체를 새 clipping 오류 수나 화질 향상으로 해석하지 않는다.

## 2. 출하 옵션의 계약

- 사용자에게 의미 있는 선택이므로 **B 옵션 구현을 채택**한다. 메뉴 이름은 예를 들어 **선명화 방식: 기본 / 화면 크기(실험)**. “OSLink 방식”은 사용하지 않는다. 독립 구현이며 OSLink와 동일한 전체 처리·품질을 검증한 이름이 아니다.
- **미설정/기존 사용자 기본은 A**. 기존 끄기·약 .20·강 저장값 유지, strong만 이미 승인한 .60으로 이동. 처리 방식 저장값은 강도와 분리한다. 끄기는 A/B 설정과 무관하게 원래 direct 경로이며 GL s=0 대체가 아니다. 강도와 방식은 한 설정 화면에서 비교할 수 있게 한다.
- 사용자가 B를 켜기 전에 “글씨를 더 또렷하게 표시 · 기기에 따라 지연·발열 증가” 정도의 짧은 설명을 제공한다. 이미 측정된 이득과 비용을 숨기지 않되 구현 용어·시험 seam을 제품 UI에 노출하지 않는다.
- **B의 실폰 비용·발열·체감/S4는 게시 후 실기 항목으로 남기는 것을 수용**한다. 이는 기본 A 보존·명시 선택인 B의 실험적 출하에 한정한다. 기존 S4 목표와 AVD 미달을 삭제하거나 성능 PASS로 바꾸지 않는다. 기본 B 전환이나 일반 화질/성능 완료 선언은 이번 승인에 포함되지 않는다.
- 정상 설정으로 고를 수 있는 제품 경로를 구현하고 시험 intent와 분리한다. `shTestDisplayPass`/Double·강도 강제·fault·metadata override는 계속 시험 전용이고 출하 gate의 양성/음성 대조를 유지한다. 번호·버전은 APK0.2.32 정본에서 한 번 확정한다.
- 모드 전환은 새 renderer/layout 소유 경계로 처리한다. 이전 renderer release 확인 전 다른 producer를 붙이지 않으며 RELEASING/BLOCKED·늦은 release 규칙을 유지한다. 끄기/실패 direct 복귀 시 source buffer와 TextureView transform이 함께 원래 상태로 돌아와야 한다. 네트워크 접속·비트레이트·remote source/터치 해상도를 바꾸지 않는다.

## 3. 현재 B의 SOURCE 보수

### N1 — view 픽셀 placement와 실제 output buffer를 같은 layout으로 결합

`MainActivity.kt:bindVideoSurface/resolveSurfaceBufferWidth/Height/sharpenDisplayPassActive`에서 첫 bind는 path NONE라 source 크기를 정한다. `bind()`가 STARTING을 만든 뒤 `applyVideoTransform()`은 B를 활성화해 **view 픽셀 placement + TextureView identity**를 보내지만, 그 순간 output buffer는 아직 source 크기일 수 있다. 다음 sync가 view 크기로 바꾸기 전 decoder가 붙어 frame이 오면, renderer는 `place.width / eglQuerySurface.width`로 서로 다른 layout을 함께 그린다. resize/회전에서도 UI 최신 placement와 실제 EGL 크기가 따로 바뀔 수 있다.

이것은 “실제로 잘못된 첫 화면을 재현했다”는 주장이 아니라 소스상 허용된 순서다. 제공 로그에는 source buffer bind 뒤 view buffer resize가 분리된 흔적도 있으며, 6초 뒤 compare 캡처는 이 경계를 증명하지 못한다.

**필요**: 예정된 B mode·실제 view/output 크기·placement·surface/layout generation을 coherent snapshot으로 전달한다. 유효 크기를 확보하고 renderer가 해당 output/layout을 채택하기 전 첫 출력·입력 준비로 인정하지 않는다. 일반적인 pan이 비동기 redraw인 것만으로 결함이라고 하지 않되, 이전 resize의 placement가 새 크기에 적용되거나 새 입력 좌표가 아직 표시되지 않은 layout에 쓰이는 경계는 막는다. 늦은 옛 resize/callback, A↔B/direct 전환도 같은 소유 규칙에 포함한다.

제한 반례: 첫 bind의 NONE→STARTING, renderer ready와 첫 frame을 다음 UI sync보다 앞당김, resize 사이 draw latch. 잘못된 layout의 frame이 표시/ready가 되지 않고, 새 layout의 실제 출력 후에만 좌표 입력 가능해야 한다. 정상 경로의 화면 표시를 무기한 보류하는 방식도 피한다.

### N2 — 같은 placement의 주기적 redraw와 무제한 post를 제거

`syncVideoSurface()`는 크기 변화가 없어도 `applyVideoTransform()`을 부르고, B는 매번 `setDisplayPlacement()`를 호출한다. renderer setter는 값이 같아도 handler 작업과 별도 redraw를 예약한다. `placementRedrawPending`은 잠깐 겹친 redraw만 합류하므로 정지 화면에서 status poll마다 반복 draw한다. 저장 B 비용 로그에도 새 frame redraw가 아닌 `glRedraws=18`인 5초 구간이 있다. 따라서 현재 비용은 순수 출력 픽셀 증가 비용만 분리한 결과가 아니다.

**필요**: 같은 placement/layout이면 아무 작업도 하지 않고, 변경 burst도 최신 snapshot 하나/예약 작업 하나로 합류한다. 새 metadata·source frame·강도 변경으로 필요한 redraw는 유지한다. 새 영상 입력 없는 pan/zoom/resize 뒤에는 최신 layout으로 실제 한 번 redraw하고, 새 decode/첫 출력/지연 샘플로 중복 세지 않는다. GL 대기 중 UI 이벤트 수만큼 작업이 무한히 쌓이지 않아야 한다.

제한 반례: 정지 화면에서 여러 status poll → draw 증가0; GL을 잠시 지연한 동안 pan/layout 변경 burst → 최신 배치 채택·작업 수 제한; 실제 pan 후 마지막 정지 redraw가 보이며 터치 일치. 이 보수 뒤 비용 비교를 한 번 다시 수집하고 기존 비용 기록도 보존한다.

## 4. 게시 전 자동 행동과 최종 판정 경계

기존 SH 전량을 매 수정마다 반복하지 않고 **고정 통합 APK 한 번**에서 검증용이 다음 핵심을 독립 실행한다. worker 자체 검사와 독립 실행을 각각 기록한다.

1. **출하와 같은 일반 설정 UI**에서 A↔B, off/.20/.60 선택 → 실제 새 표시, 설정 저장/재실행 복원, 기존 미설정/default/off 보존. seam extra 없이 B가 켜져야 한다. 같은 연결의 모드 전환·B→off·B GL실패→direct 안내/실화면·다시 선택도 포함한다.
2. 회전/resize/컨트롤 공간 변화, 전체·크게·pan·정지 redraw에서 실제 output/layout/화면 경계와 owned Host 창 좌표를 대조한다. 중앙 한 블록 통과만으로 모든 위치≤1px라고 하지 않는다. 모서리·화면 가장자리·letterbox 무입력, 진행 중 gesture 취소 및 옛 callback 반례가 필요하다.
3. B의 source tap·crop/flip/rot90 fixture를 확대/비정수 placement와 함께 돌린다. 현재 AB fixture는 B crop/rotation을 아직 증명하지 않는다. source metadata 없음은 output 크기로 대체하지 않으며, 시작/선택뿐 아니라 이미 LIVE 중에도 계속 못 그리는 상태가 되면 bounded 진단·복귀해야 한다. 실 Codec2 1088은 폰 항목으로 남기되 padding/crop 모델의 자동 반례는 생략하지 않는다.
4. 스크롤 글씨·1px 선·체커·사진 경계/컬러/반전 배경에서 shimmer·halo·세부 소실을 최종 출력으로 확인한다. 움직이는 입력은 대응 시각/프레임 및 누락을 기록한다. edge 값 상승만으로 PASS를 만들지 않는다. double transform/output-pixel tap/옛 layout 부정 대조는 실제 실패해야 한다.
5. 정지 redraw 보수 뒤 matched handoff→latch 전체 segment 분포와 매칭 n/unmatched/stale/miss·draw/메모리 경계를 확인한다. SwiftShader 결과는 실폰 GPU/발열 증거가 아니다. GL/해제 실패·back/reconnect에서 producer/스레드 누적이 없어야 한다.

그 뒤 picker 보수·SH·UA-2를 합친 제품 commit, APK 해시, 정상/시험 APK gate·서명 인증서·버전/ABI를 고정해 SOURCE 잔여 및 EVIDENCE로 회신한다. 독립 검사가 끝난 같은 후보에 기존 게시 승인을 적용하며 추상 GO를 반복 요청할 필요는 없다. B가 자동 검증에서 기하/복귀를 못 지키면 B만 출하 제외하고 A 후보와 실패를 분리 보고한다. 실폰만 남았다는 이유로 자동 가능한 항목을 생략하지 않는다.

정본은 기존 SH 작업/0.2.32 지시서에 취합하고 같은 담당·writer가 처리한다. 새 병렬 제품 task나 KA 재개를 만들지 않는다. 현재 사용자 조치 없음. Codex는 제품 수정·빌드·AVD 실행·서명/배포·commit/push를 하지 않았다. 이 판정서는 미추적 운영 자료이며 공개 history/계획·Git MCP commit은 같은 구현 작업에 취합한다.
