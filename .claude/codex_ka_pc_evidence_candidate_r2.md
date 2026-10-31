# KA PC managed r3 — 후보 재검토

2026-10-29 · **protocol/lifecycle·crash-close 근거는 수용한다. picker 표시 근거는 미통과이므로 KA PC 작업용 시험 전체 관문은 아직 닫지 않는다.** 제출된 managed 60 PASS를 전부 무효로 하거나 제품 렌더 결함으로 확정하는 판정은 아니다. 보존한 실제 picker 이미지와 assertion 사이에 불일치가 있다.

## 고정과 유효한 관측

- source **5f0e7a3dddc84eeaecba8a46227320d2961ce6f4**, tracked 변경 없음. managed exe **7bb6b7902c5b48749b5885f4aadf6ed0175ad03a85fd5bc0dc2e2b5fbbe656aa**와 제품 3개 전체 hash가 기존 고정값에 일치함을 직접 확인했다.
- report_ka_pc_impl_r1.md SHA-256 **67a28c84f8a14fcabb1cf7b7f4547181f1338bb5e4b52b0750947136665a3434** 일치.
- 새 원문 log SHA-256 **bdfae981ffc0010da9c12ce87bdd768c6a5091fe00b033eb8b020f14161217d1**과 BOM/인코딩을 처리한 **PASS 60 / FAIL 0 / ALL PASS**를 확인했다. rc 0/slot 단일 호출·해제는 실행 보고 범위다.
- Host 1 launch의 종료·EOF·두 OFF 줄·무상충 final 판정, child 10개 종료/출력/로그 판정이 통과했다.
- viewer-C의 실제 trailer는 close 전 root + 1 system console host, kill-on-close/membership/close 성공 및 root 종료 signal을 기록하며 EOF 243 bytes/log kept다. 코드에서 승인한 한정 증거와 맞고, 닫힌 job aggregate 0이나 conhost별 종료를 보았다고 하지 않는다. **이전 crash-close 하네스 결함의 보수 관문은 닫는다.**
- 두 activation의 같은 lease/socket 재사용·capability 추가 없음, park/return, 다른 session을 12초 간섭하지 않음, owner epoch·Release/EndAll·즉시 재유지 등 기능 관측은 해당 범위의 후보로 보존한다.

## V1 — 두 picker 캡처는 blank인데 “picker 표시”로 PASS

Codex가 새 run의 **A-round1-picker.bmp와 A-round2-picker.bmp를 직접 열람**했다. 둘 다 GNLink 창 테두리/제목 외 **흰 client 영역**이고, 선택할 picker/card/label이 보이지 않는다. 두 파일의 byte hash도 같다.

- 두 picker BMP SHA-256: **f62e2dbaa017cf599ae7aa1227de546b3593b9f298af187fdb083c1395b0746d**
- 각 크기: **1586×893**
- 제목/테두리를 제외한 내부 `(20,60)-(1566,873)`의 **1,256,898 pixel 전부 RGB (255,255,255), distinct color 1**.
- 픽셀 검토 자료: `.claude/codex_ka_pc_review/managed_r3_visual_audit.json`. 읽기/메모리 분석만 했고 이미지를 수정하거나 새 사용자 화면을 캡처하지 않았다.

반면 로그는 두 round 모두 **“WGC capture ... shows the picker (not blank), variety=9”**로 PASS다. 고정 source의 variety는 **창 전체를 샘플링**하고 `>4`면 통과한다. 이 경우 title/icon/border의 색으로 통과하며, client에 picker가 실제 그려졌는지를 확인하지 못한다.

viewer-A 원문에는 `[picker] first paint after show, client=1584x860 desktopCard=24,78,320,293`도 있다. 그 layout/paint 진입 로그가 존재해도 제출된 픽셀에는 card가 보이지 않는다. 로그의 paint 진입과 실제 내용 렌더를 구분해야 한다.

또한 desktopCard 좌표는 viewer log에서 가져와 PostMessage로 누른다. 그 뒤 실제 영상/Presented가 나온 것은 **선택 handler와 영상 경로가 작동한 근거**로 보존한다. 그러나 보이지 않는 UI의 내부 좌표를 눌렀다는 사실은 사용자가 보이는 카드를 골라 누를 수 있다는 근거가 아니다. PickerReady 통지/좌표 존재도 실제 picker 렌더와 구분한다.

선택 뒤 presented 이미지는 영상 내용이 표시되는 것을 확인했다. 그 내용의 관련 없는 사용자 desktop 정보는 여기 옮기지 않는다. 이 이미지를 선택 전 picker의 표시 증거로 대신하지 않는다.

## 필요한 한정 후속

작업용2의 같은 KA PC 시험 task에 **V1 하나의 묶음**으로 연결한다.

1. 기존 white BMP 두 장을 **부정 대조에서 반드시 실패**시키도록 시각 판정을 고친다. 전체 창 variety threshold만 임의로 올리는 수정은 충분하지 않다. title/border를 빼고 current op의 client/실제 card 영역·picker 내용과 연결한다.
2. capture가 너무 이르거나 off-screen/WGC 특성으로 실제 draw를 못 읽은 것인지, 제품이 실제로 아직 picker를 그리지 않은 것인지 **먼저 구분**한다. 현재 증거만으로 어느 쪽이라고 확정하지 않는다. render/present/paint 시각·generation과 캡처를 연결해 원문을 남긴다.
3. 두 activation에서 **실제 picker 내용이 보이는 근거를 확보한 뒤** 그 보이는 target을 선택하고 first fresh frame까지 이어지는 것을 확인한다. 백색 창+chrome를 picker라고 통과시키는 현재 false-positive를 닫는다.

사용자 화면에 창을 옮겨 덮거나 standby 중 window/decoder를 계속 유지하는 우회는 하지 않는다. 기존 off-screen/승인된 격리 창 및 decoder/window 0 계약을 유지한다. 새 product render seam/수정이 필요하면 고정 source와 영향 범위를 먼저 연결한다. 기존 캡처/판정의 좁은 진단은 같은 task 범위에서 진행한다.

이미 통과한 child/crash/guard 및 protocol 관측을 버리지 않는다. 제품·일반 수명 동작이 그대로이고 판정/캡처만 보수하면 **해당 activation/picker→선택→present의 최소 범위**를 다시 확인하면 된다. 정상 shell 148을 이 이유로 전량 반복할 필요도 없다. 제품 render를 바꾸게 되면 실제 영향을 받는 cold/return/선택 경로를 검증한다.

## 닫힘 여부

- **닫힘**: managed의 의도적 crash-close를 End에서 정당하게 판정하는 하네스 보수.
- **후보 수용**: 이번 protocol/lifecycle/종료 안전 관측과 기존 shell 148(실행한 옛 binary 9d0baa96와 연결).
- **아직 미닫힘**: picker 표시·사용자 선택을 포함한 KA PC 작업용 시험 전체 관문(V1).
- **별도 유지**: 최종 PC 독립 확인·R2 통합/출하, old-host NOT RUN 및 실 NAT·실 clipboard 등의 기존 미검증.

따라서 현재 두 실행을 합쳐 **“작업용 PC UI 시험 관문 완료”로 표시하지 않는다.** raw ALL PASS 자체는 실행 사실로 보존하되 시각 assertion의 한계를 함께 기록한다. 첫 managed FAIL 실행과 shell의 source/binary 귀속도 유지한다.

이번 Codex는 source/hash/log/trailer/image 읽기와 이미지 내부 픽셀 분석만 수행했다. Host/Viewer/AVD/시험 재실행·사용자 입력/clipboard·설치·운영·삭제·제품 편집·commit/push는 없다. 사용자 현재 조치 없음.
