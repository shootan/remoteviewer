# old APK lane — pinned window 대체 범위

2026-10-30. **pinned window 선택을 이 lane의③~⑥ 공통 경로 근거로 수용한다. 이 lane 때문에 monitor/desktop 선택을 별도 필수로 추가하지 않는다.** 예정된 하네스 보수의 고정 SOURCE 확인 뒤 실행한다. 현재 작성 중인 r7 code를 이미 승인한 판정은 아니다.

## 근거와 범위

확인한 고정 source는 APK b554e76과 새 Host53fced4다.0.2.32의 Windows tab은 native window list를 요청하고, 그 visible card를 고르면 일반 WindowSelect/Rsp·세대 gate·codec/영상 경로로 진행한다. Host PinnedSelectTest는 nonzero pinned ID만 허용하고 **그 뒤 제품의 실제 selection transaction을 사용**한다. 테스트에서 복제한 성공 handler가 아니다.

0.2.32 `SessionPark.canPark`는 directoryHostId·manual 여부·계정/session·nativeConnected를 판정하며 monitor/window target 종류를 조건으로 삼지 않는다. 따라서:

- **S:** pinned window로 연결·실제 선택/첫 영상③, 해당 대상에 대한 수신 input④, 기존 directory session의 목록 park⑤·reuse/재선택 뒤 영상⑥을 검증할 수 있다.
- **P:** 게시 APK 원본의 수동 연결·pinned window③/④와 기존 P-5d 연결 해제/P-6r 새 연결을 검증한다. **P의⑤park/⑥resume는 계속 NOT RUN**이며 window를 고른다고 canPark가 true가 되지 않는다.⑦은P의PC standby 공존으로 유지한다.

이 합성 호환 lane의 목적은 새 Host/server와 구 APK의 연결·선택/영상/control·세션 수명/공존이다. **monitor id0의 분기, 모니터 열거/좌표·배율, 전체 desktop capture backend, prelogin desktop 선택까지 검증했다고 쓰지 않는다.** monitor 전용 동작이 실제 변경됐거나 그 자체가 필수 acceptance인 별도 항목은 다른 근거로 확인해야 하며 window 성공으로 대신하지 않는다. 이 lane을 살린다고 기존의 그런 미검증을 지우지는 않는다.

## 하네스 보수의 수용 조건

1. Windows tab에서 **실제 표시되고 활성화된 pinned window 카드**를 찾는다. Host 로그의 내부 좌표만 누르거나 JNI handler를 직접 불러 UI 선택을 대신하지 않는다.
2. 현재 run의 fixture target HWND/owner·Host window-list의 단일 nonzero target·카드 title을 연결하고, **tap 뒤 app/Host의 requestedId/appliedId가 그 target인지** 확인한다.80809746 같은 이전 run ID를 고정하지 않는다. 잘린 title만으로 다른 항목을 무조건 받아들이지 않는다.
3. 같은 선택 operation의 Host applied/answer·stream generation → 앱 select_ready/실제 표시·video 진행 → S의park/reuse 또는P의명시 재연결 순서를 남긴다. 캡처 target이 원래 pinned였다는 startup 줄만으로 선택 성공을 세지 않는다. 재선택에도 같은 검사를 한다.
4.④는 해당 선택 뒤 의도한 tap에 대응하는 event 종류/시각 등을 확인하고, 연결 때 지연돼 도착한 modifier release만으로 tap 성공을 세지 않는다. Host input none에서의 수신/기록 근거이며 실제 OS 입력 성공으로 부르지 않는다. 사용자 화면/clipboard는 조작하지 않는다.
5. id0/잘못된 target·title/Host list 불일치는 **tap 없이 거부/INVALID**되는 대조를 둔다. 기존 잘못된0 선택에 대한 `pinned_test_only` 거부는 정상 seam 동작으로 보존한다. seam을 풀거나0을 pinned window로 몰래 redirect해 desktop 성공처럼 만들지 않는다.

③~⑥ 문구·map·screenshot 이름도 window로 맞춘다. 실제 영상 이미지를 확인하며 사용자 desktop을 새로 캡처할 필요는 없다. monitor 경로를 위해 driver/디스플레이 설정·보안·사용자 설치본을 바꾸는 승인은 하지 않았다.

## 이번 실행 기록과 다음

검증용 통지상 S는①②PASS/P는②PASS 뒤③에서 실제로id0을 눌러 seam 거부를 받았다. **raw③FAIL과 후속 NOT RUN은 보존**하고, 전체 실행은 의도한 pinned target을 검증하지 못한 하네스 결함 INVALID로 분류한다.③FAIL을 제품 monitor 실패나③PASS로 바꾸지 않는다. 부분 성공①②도 전체 lane 완료와 구분한다. 정리/guard 근거는 제출 원문에 귀속한다.

앞 검토에서 desktop0 선택과 pinned seam의 불일치를 놓쳤다. 이번 scope 확인으로 그 잘못된 기대를 유지하지 않는다. 이미 위임한 **동일 하네스 보수/문구/부정 대조**를 한 task로 완료하고 고정 commit을 제출하면 그 diff와 대조만 Source 확인한다. 그 전에는 S/P 추가 실행을 보류한다. 제품 코드/seam/게시 APK 변경이나 기존 전체 검증 반복은 요구하지 않는다.

Source가 통과하면 기존 r6의 S(standby 없음)/P(WithStandby), SDK pin·단일 시험 package 초기화·Host OFF/end/EOF/job·자기 슬롯·원본 APK hash·P 무인증 update check 한정 허용을 유지한다. 실제 EVIDENCE 뒤 rel-0164 출하 관문을 판단한다. APK0.2.33의 별도 보류와 push 보류는 그대로다.

Codex는 고정 source·기존 하네스 일부를 읽고 범위만 판단했다. 현재 작성 중인 working copy의 고정되지 않은 r7 구현을 승인하거나 실제 Host/Viewer/AVD/adb를 실행하지 않았다. 제품 변경·삭제·설치·운영·commit/push도 없으며 미추적 검토 기록만 작성했으므로 제품 commit은 만들지 않았다. 사용자 현재 조치 없음.
