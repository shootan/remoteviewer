# SH strength r4 — SOURCE 재검토

2026-10-27 / 검증용 remote#yj8mrmrx / 입력 report_sh_strength_r2.md r4, report_rel_apk_0232_r1.md r4 전문.

**NEEDS_CHANGES — N1-c1 실제 latch 상관·c2 터치 스트림 차단을 한 묶음으로 보수한다. 크기 불일치의 유한 retry/복귀(a), 새 frame 없는 redraw 대기(b), 기존 N2 dedupe는 수용한다. 한 버퍼 일찍 입력을 허용하는 한계는 채택하지 않는다.**

제품 9886c2298ac987728881a4da663fcacbb6ef1575, 시험 0c48c6f8a641fce189a42206024b28e78eb8a60a, 통합 HEAD 779528968481beeecfffdb8c1fa98a9144b33c77. 전체 diff·관련 Activity touch/latch·renderer·기존 FrameInfo/OutputGate를 읽었다. whitespace rc0, 검사 시 HEAD 일치·tracked clean.

APK .claude/rel-apk-0232/GNLink-0.2.32.apk의 SHA-256 **8782f38701e5b6ba154a068a7d15949574a8c9a56166e248871caa4807f1769f**를 직접 확인했다. 0c48c6f..7795289는 PC e2e 시험 파일뿐이며 APK 코드 diff0이다. 패키지/인증서·gate·단위·AVD 수치는 작업용 귀속이다. 현재 APK를 최종 출하 후보로 승인하지 않는다.

## N1-c1 (P2): latest swap generation은 이번 latch의 identity가 아님

onSurfaceTextureUpdated는 surface.timestamp를 읽은 다음 renderer.swappedPlacementGen의 **현재 값**을 shown으로 복사한다. renderer는 영상 swap 후 이 전역 값을 쓴다. 두 읽기가 같은 버퍼를 가리킨다는 상관이 없다.

가능한 순서:

1. view가 옛 layout A 버퍼를 latch하고 callback이 대기하거나, callback이 옛 timestamp를 읽은 뒤 멈춘다.
2. GL이 새 layout B 영상을 swap해 latest=B를 게시한다. B 버퍼는 아직 표시되지 않았다.
3. 앞선 callback이 latest=B를 읽어 shown=B로 만든다. viewport/보낸 generation도 B라 가드가 열리지만, 사용자는 A를 보고 있어 새 좌표가 잘못 전송될 수 있다.

이 한계 공개는 맞지만 r3에서 막아 달라고 한 경계 자체다. 버퍼/콜백 coalescing과 UI 지연 때문에 정확히 한 버퍼 이내라는 상한도 소스로 증명하지 않는다. holdplace:8000은 **swap 전** 지연을 검사하며, swap 뒤 새 버퍼 latch 전의 위 순서를 검사하지 않는다.

clear에도 같은 문제가 있다. shown generation을 CLEAR_SWAP_TIMESTAMP 검사보다 먼저 갱신한다. 이미 영상 swap이 있으면 clear는 마지막 영상 timestamp를 재사용한다. 그 영상 V가 실제 latch되지 않고 clear V만 먼저 latch되면, LatchClassifier는 이를 새 영상으로 볼 수 있다. native handoff가 남아 있으면 검은 clear가 first-output gate/영상 표시·latency 표본으로 인정될 수도 있다. 같은 timestamp이므로 redraw라는 전제는 이전 영상이 실제 latch된 경우에만 성립한다.

필요 보수는 **실제로 latch한 output 버퍼를 renderer/surface/layout generation 및 video/redraw/clear 종류와 연결**하는 것이다. latest scalar나 다음 콜백 수로 shown을 추정하지 않는다. redraw/clear는 decoder timestamp가 같을 수 있어 decoder timestamp 하나만의 map도 충분하지 않다. 예를 들어 고유 output token과 bounded metadata에 원 decoder frame/timestamp를 별도로 보존할 수 있다. 구체 구현은 작업용이 정하되 다음을 유지한다.

- metadata 없음·옛 renderer/surface/layout이면 새 입력을 열지 않는다. clear는 정상 영상/first-output/성능 표본의 근거가 아니다.
- metadata는 버퍼보다 먼저 확인 가능하고, 실패한 swap은 확인 근거에서 제외한다. 늦은 callback/ring entry가 새 layout을 승인하지 않는다.
- 원 decoder frame의 handoff→실제 latch 지연·codec/stream generation·source 크기를 보존한다. redraw/clear를 새 decode로 세지 않고 보관량을 제한한다.
- equal-placement에서 swap/latch 없이 새 이름의 generation을 credit하는 분기도 마지막으로 확인한 실제 표시 배치와 상관이 맞을 때만 허용한다.

제한 반례: A latch/callback 정지→B swap 완료/아직 B latch 없음→옛 callback 처리+tap은 송신0, B 자신의 실제 latch 뒤 정상 좌표. clear가 미표시 영상과 같은 decoder timestamp를 가진 경우 output-kind로 걸러 first-output/새 영상 계수0. 현재 방식의 변이가 실제로 실패해야 한다.

## N1-c2 (P2): 보류 DOWN 뒤 두 번째 손가락이 가드를 우회

handleViewerTouch는 먼저 ViewerTouchRouter.route로 ACTION_DOWN의 owner를 PC로 정한다. layout 미표시라 return true해도 owner는 PC이고 activeTouchPointerId는 INVALID다. 이어진 **ACTION_POINTER_DOWN**은 새 가드(ACTION_DOWN만 검사)를 거치지 않고 PC route의 DOWN/POINTER_DOWN 공통 분기에 들어간다. active pointer가 INVALID이므로 두 번째 손가락의 MOUSE_DOWN을 실제 queue할 수 있다.

표시되지 않은 layout에서 시작한 **그 제스처 전체**를 최종 UP/CANCEL까지 SWALLOW/보류로 고정한다. 두 번째 손가락·move/pinch/scroll·부분 up이 전송을 시작시키면 안 된다. 중간에 layout이 표시돼도 그 스트림을 재개하지 않고 다음 새 DOWN부터 정상 처리한다. 이미 시작된 정상 PC 스트림 중 geometry generation이 바뀌면 필요한 release를 한 번 처리하고 이후 이벤트를 삼킨다. 로컬 pan은 계속 로컬이다.

제한 반례: 미표시 layout DOWN→POINTER_DOWN/MOVE/POINTER_UP→표시 완료→남은 MOVE/UP까지 remote 입력0, 다음 새 DOWN만 정상 owned Host 좌표. 실제 Activity 또는 동일 제품 routing 경계를 사용해 DOWN만 검사하는 현재 모양도 검출한다.

## 수용한 보수·검증 범위

- mismatch 영상 fall-through를 없앤 16ms retry·동일 layout 1초 상한·실패 direct 복귀·clear swap 반환 검사는 맞다. 새 입력과 보관 frame redraw 대기 분리도 수용한다. clear output의 정상 영상 여부는 위 c1에서 닫는다.
- layoutB133_never의 영상 draw0/bounded failure, panB133_redraw의 새 decoder frame0·실제 위치 이동·timestamp 유지는 유효한 제한 반례다. Activity pan에는200ms pulse가 있었다는 구분도 유지한다.
- layout_b는 이제 회전/resize·모서리·letterbox·pan·fallback을 실제 실행한다. stable 좌표와 GL 적용 전 holdplace 대조는 수용하지만47 PASS가 c1/c2를 검사한 것은 아니다.
- **picker E1 닫음**: 7795289의 실 Host --android-select GateSink가 자체 mutex 안에서 CommitLocalSelectionFor를 구현해 승인한 경계와 맞는다. 37/37은 작업용 귀속이다. picker9afcec7·UA2 482a901 SOURCE OK는 유지한다.

## 다음 단계

현재 source 전체 OK가 아니므로 8782f387 APK로 최종 EVIDENCE/게시를 닫지 않는다. 검증용은 a/b·기존 설정/회귀의 독립 확인을 진행할 수 있지만, **입력/실제 latch 승인과 최종 출하 행동은 보수된 새 고정 APK에서** 확인한다. c1/c2 제한 반례→해당 diff SOURCE→새 최종 후보 독립 EVIDENCE로 잇고 닫힌 소스/전체 행렬을 매 commit 반복하지 않는다.

shimmer 수치 oracle 부재 공개는 타당하다. 독립 검사에서 짧은 통제 스크롤/연속 frame 자료를 직접 열람해 미세선 깜빡임·halo·세부 소실을 정성 관찰하고 수치 판정/폰 화질 완료와 구분한다. 새 자동 화질 점수 프로젝트를 만들 요구는 아니다. 실폰 S4·발열·최종 체감은 기존대로 게시 후 실기, AVD 지연/기존 S4 미달은 보존한다.

같은 SH/통합 담당이 **N1-c1/c2 한 묶음**을 기존 정본에 반영한다. 기본 A·선택형 B 결정은 유지하며 실험 옵션이라는 이유로 입력 정확성 조건을 낮추지 않는다. KA/PC163 재검토·새 병렬 writer는 없다.

사용자 현재 조치 없음. Codex 제품 수정·빌드·AVD/새 테스트 실행·서명/게시·commit/push 없음. 직접 확인은 소스·기존 자료와 APK SHA이며 작업용34/26/36/6/47·method16·ua2 13·단위266/gate·실Host37을 독립 실행으로 확대하지 않는다. 미추적 판정서/인계 외 기록·계획·Git MCP commit은 같은 작업에 취합한다.
