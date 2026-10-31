# SH r5 + evidence A — SOURCE 재검토

2026-10-27 / 검증용 remote#yj8mrmrx / 입력 report_sh_strength_r2.md r5, report_rel_apk_0232_r1.md r5 전문.

**SH SOURCE: NEEDS_CHANGES — 출력 identity의 renderer 간 중복과 REDRAW 첫 표시 두 잔여를 한 묶음으로 보수한다. c2 제스처 전체 차단 및 evidence A 시험 Host 구성은 아래 범위로 수용한다.**

고정 제품 **8d6771075f96c4782b186f9d11eb12119c72b3c8**, 시험 **a6635a2cd19b476241d2f2a04df37da4d51c1770**, A seam/통합 **ed7f4ad0ab55fcc55c1cb434e965e11519fc9c67**. 관련 전체 diff·새 OutputLog/시험·Activity/renderer/router·A 하네스/Host를 읽었다. 326e0c3..ed7f4ad whitespace rc0. 검사 시 HEAD 일치·tracked clean.

후보 APK의 SHA-256 **1d4a05f4f318496a895ac748be5705365598c3de677c6f4652988034bd3deebd**를 직접 확인했다. 이전8782f387은 최종 후보 근거로 쓰지 않는다. 새 후보도 아래 소스 잔여 때문에 최종 출하/EVIDENCE OK는 아니다.

## N1-a (P2): output timestamp가 renderer마다 BASE+1부터 재사용됨

SharpenOutputLog의 seq는 **인스턴스 필드0**이고 reserve는 BASE+seq다. 각 SharpenRenderer가 새 log를 만든다. 따라서 renderer A와 B가 각각 첫 output을 reserve하면 둘 다 **2^62+1**이다. renderer/surface incarnation을 식별하는 namespace가 없다.

Activity는 callback의 surface.timestamp를 **현재 renderer의 log**로 조회한다. A의 마지막 버퍼/늦은 callback timestamp가 B의 현재 log key와 같으면, 옛 A를 B의 VIDEO/REDRAW·새 placement generation·B decoderTs로 해석할 수 있다. current log에 조회했다는 사실만으로 다른 renderer가 배제되지 않는다. 특히 같은 TextureView를 유지하는 A/B renderer 교체에 해당한다.

unknownOutputsSayNothing 시험은 other log를 비워 둔 상태에서만 old key를 조회해 실제 충돌을 놓친다. 두 log가 모두 output을 reserve하는 대조가 필요하다.

필요: renderer/surface 수명 전체에서 출력 token이 재사용되지 않도록 namespace/전역 유일 할당 등으로 묶고, 실제 callback surface/renderer 소유와도 맞춘다. 단순 layout generation은 이미 key에 없고, map의 값에 generation을 붙이는 것만으로 key alias를 해결하지 못한다. overflow/미등록/옛 source는 fail-closed로 유지한다. 고정64개 보관·swap 전 게시/실패 withdraw는 유지한다.

제한 반례: A와 B가 **모두 같은 순번의 output을 보유**한 상태에서 A의 늦은 latch를 B current log로 조회→B shown/first-output0. 실제 B 버퍼의 latch만 새 layout을 승인한다. A VIDEO→B CLEAR 및 A CLEAR→B VIDEO처럼 종류가 다를 때도 잘못된 종류로 해석하지 않아야 한다. 새 seq allocator는 동시 renderer/release 대기에도 중복0이어야 한다.

## N1-b (P2): REDRAW가 그 decoder frame의 첫 실제 표시이면 영구 대기/오계수 가능

Activity는 **latched.frame && latchClassifier.isNewFrame(...)**일 때만 실제 첫 표시/first-output gate/지연을 처리한다. REDRAW decision은 frame=false, redraw=true라 classifier를 호출하지 않고 redraw 통계 후 반환한다.

가능한 순서:

1. 새 renderer가 decoder frame F의 VIDEO를 swap한다. view는 아직 그 버퍼를 latch하지 않았다.
2. source/layout/강도 변경으로 같은 F의 REDRAW를 swap한다. view가 VIDEO를 건너뛰고 REDRAW를 처음 latch한다.
3. F의 올바른 그림은 실제 보이지만 code는 REDRAW라는 이유로 first-output/visible-frame을 확인하지 않는다. 더 새 decoder frame이 없는 정지 화면에서는 gate/선택 대기 또는 원치 않는 direct fallback으로 이어질 수 있다.

**새 decode가 아니라는 사실과, 그 decode frame이 이번에 처음 화면에 표시됐다는 사실은 별개**다. 같은 F를 이미 본 뒤의 redraw는 중복 계수0이어야 하지만, F를 처음 보여준 REDRAW는 정상 영상으로 qualification할 수 있어야 한다.

필요: VIDEO/REDRAW 모두의 실제 영상 buffer에 대해 원 decoder frame identity가 이미 표시됐는지를 판정한다. source decode 수는 늘리지 않고, F 첫 visible/latch/first-output·원 handoff→latch 지연은 한 번만 기록한다. CLEAR/unknown/실패 buffer는 계속 제외한다. 현재 newFrame 변수의 의미를 실제 새 decoder 생성과 혼동하지 않는다.

제한 반례: F VIDEO는 미latch, F REDRAW만 첫latch→첫 정상 출력·선택 완료/표시1, 이후 같은F의 VIDEO/REDRAW latch→추가표시·지연0, CLEAR만latch→0. 새 MediaCodec frame을 계속 흘려서 다음 VIDEO로 우연히 회복시키지 않는다. OutputLog만이 아니라 실제 Activity가 쓰는 qualification/classifier/gate 연결을 검사한다.

CLEAR/unknown을 실제 현재 출력으로 latch했을 때는 이전 shown 값을 남겨 그것을 새 표시의 증거로 사용하지 않는지도 같은 경계에서 확인한다. 지금 null shownGen은 shown 값을 유지하므로, clear 자체를 정상 그림으로 보거나 미등록 current buffer에 이전 proof를 자동 재사용해서는 안 된다.

## 수용한 보수

- 같은 renderer 안의 출력별 종류/layout/원 decoderTs를 swap 전에 기록하고, 실패 시 삭제·latch의 timestamp로 조회하는 방향은 맞다. r4 latest-scalar를 없앴고 same-layout/new-name도 실제 redraw/latch를 거치도록 바뀌었다. 다만 위 수명 namespace와 첫 actual display를 마저 닫는다.
- **c2 SOURCE OK**: 제품 ViewerTouchRouter가 DOWN에서 PC/LOCAL/SWALLOW를 결정하며 POINTER_DOWN·MOVE·부분UP·중간layout표시도 owner를 바꾸지 않는다. layout 전송 시 기존 PC stream은 interrupt/release once·나머지swallow, local pan은 보존한다. 예전 DOWN-only 가드를 제품 routing에 합친 것을 확인했다.
- 늦은callback/두번째손가락을 단위로 검증하고 AVD 재현으로 주장하지 않은 구분은 맞다. 275/0·변이3·AVD35/26/36/6/47은 작업용 귀속이다. Codex 새 빌드/AVD/테스트 실행은 없다.

## evidence A 시험 Host·하네스: 이 구성으로 진행 가능

- seam은 REMOTE60_STREAM_TEST_SEAM 안의 GNLINK_STREAM_TEST_PRELOGIN_ROLE로 기본 off, ordinary/manual trial Host의 역할 광고와 기존 allowlist tripwire를 제공한다. 금지 control·credential/reboot/SAS는 링크 종료하며 가짜 WindowSelected를 주입하지 않는다.
- A 하네스는 loopback·시험 소유 pattern을 startup capture로 고정하고 input injection none으로 실행하는 구성이다. 실제 사용자 입력/서비스/재부팅을 검사하지 않는다. 이 구성과 달리 input injection을 켜서 OS 입력 검증으로 확대하지 않는다.
- =1의 역할/기능 mask와 =2의 잘못된 feature mask 대조, ordinary Host의 no-window-list 실패 대조는 목적에 맞는다. 실제 --prelogin directory 인증을 수동 허용으로 바꾼 코드가 없으며, 이 trial WGC/pattern을 실 worker DXGI/SYSTEM 증거로 쓰지 않는다.
- 출하 env 문자열/define 제외와 시험 Host 양성, gate self-test54는 보고 귀속이다. 최종 컴파일/바이트 gate 근거에 정확한 두 exe 해시를 기록한다.
- A 하네스의 취소/재선택은 pause-publish 동안 실제 첫 프레임이 대기했는지와, 같은 연결/op·새 IDR·실제 표시가 이어졌는지 로그/화면으로 판정한다. 메뉴 컨트롤이 존재한다는 것만으로 표시 전 취소를 통과시키지 않는다.

## 독립 배치와 다음 담당

현재 시작한 독립 배치를 중단하거나 실행 중 프로세스를 임의 조작하라는 요청은 아니다. 검증용은 이 APK/Host 해시에서 실제 확인한 결과를 그대로 보존하고 부분 근거로 활용할 수 있다. 다만 위 N1-a/b의 출력/latch/first-output·renderer 교체 및 최종 관련 사용자 행동은 **보수된 새 고정 APK**로 확인해야 한다. 현재 배치 PASS를 새 제품에 자동 승계해 출하하지 않는다.

같은 담당에게 N1-a/b 및 제한 반례를 한 묶음으로 취합→해당 diff SOURCE→최종고정APK 독립 EVIDENCE로 잇는다. c2·a/b layout복구·기존 picker/UA2 SOURCE OK를 다시 열지 않는다. 여전히 기본A·제품선택B 결정과 실폰 S4/발열/체감 후속 경계는 유지한다.

삭제/권한 질문을 새로 만들지 않는다. 자기 CreateNew slot 인스턴스 예외와 중앙 deletion_candidates 목록 정책을 따른다. 제품 정책 문서 task t-94tjfghd는 별도이며 APK 검토를 막지 않는다.

사용자 현재 조치 없음. Codex 산출물은 미추적 판정서/인계이고 제품 편집·빌드·테스트 실행·서명/게시·commit/push 없음.
