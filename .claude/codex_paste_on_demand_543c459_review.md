# paste-on-demand 사후 소스 검토 — Codex (2026-10-07)

대상: main-stability-integration `543c45932ddcfff39e045249c7d94b6ae39d32a8`(PC 0.2.150). 0.2.149의 64c042a/fd03819 및 r4 2435631..26c4f49가 반영된 소스를 읽었다. 사용자 선택 **“Codex 없이 게시”**에 따른 릴리스였다는 기록을 인정하며 과거 게시의 무승인 여부를 다시 문제 삼지 않는다.

**판정: NEEDS_CHANGES — P2 3건.** 아래는 제품 소스에서 도출한 실행 순서이며 **Codex가 실기/빌드로 재현한 결과가 아니다**. 작업용/검증용의 과거 PASS 수치를 내 직접 실행으로 인용하지 않는다. prelogin-service가 최우선이라는 사용자 순서는 유지한다. 이 보고서 한 묶음을 검증용이 기존 paste 작업의 후속 검증/수정에 연결하며, Codex가 별도로 작업용을 병렬 지휘하지 않는다.

## F1 [P2] 마지막으로 관찰한 hostCopyGen은 로컬/원격 복사의 실제 시간 순서가 아니다

위치: `viewer_window_proc.cpp:877–891`(로컬 복사에 마지막 관찰 gen 저장), `:632–639`(gen 크기만 비교), `:727–739`/`:792–815`(PassThrough 결정), `viewer_control_client.cpp:132–158,289–307`(새 poll). r4의 “원격에서 복사한 뒤 로컬에서 새로 복사하면 다시 로컬 것을 보낸다” 계약을 어긴다.

결정적 순서:

1. 뷰어는 호스트 gen=10까지 보았다.
2. 호스트에서 메뉴/물리 조작으로 이미지 R을 복사해 gen=11이 된다. 뷰어 poll은 아직 오지 않았다. 이미지라 R→P 텍스트 적용으로 로컬 내용이 바뀌는 부수 경로도 없다.
3. **그 뒤** 로컬에서 L을 새로 복사한다. WM_CLIPBOARDUPDATE는 Local로 표시하지만 gGenAtLocalCopy에는 여전히 10을 저장한다.
4. GNLink Ctrl+V의 강제 poll이 11을 반환한다(또는 gesture 전에 정기 poll이 11을 받는다).
5. `11 > 10`으로 Remote를 최신이라고 잘못 판정하여 L을 보내지 않고 R을 붙인다.

강제 poll은 호스트의 현재 값을 알려 줄 뿐, 2와 3의 선후를 복원하지 않는다. `viewer_paste_e2e_test.cpp:717–737`의 shared-station 하네스는 `copy_here`에서 **host gen 관찰을 기다린 뒤 로컬 WM_CLIPBOARDUPDATE를 재전달**한다. 이 추가 순서를 두 PC의 일반 순서라고 가정한 주석이 틀렸고, R4 시험도 그 helper를 써서 위 창을 덮지 못한다.

보수 기준: 로컬 복사의 관찰 시각과 원격 복사의 실제 선후를 구분하는 프로토콜/순서 계약을 먼저 정한다. 관찰되지 않은 교차 사건은 불확실함을 처리하는 규칙이 필요하다. 단순히 강제 poll을 하나 더 넣거나 현재 gen으로 baseline을 올리는 것으로 양쪽 순서를 모두 보장했다고 하지 않는다. 로컬 copy별 revision과 판단 근거를 ticket/snapshot에 고정하여, **다음 로컬 복사가 pending gesture의 비교 기준을 바꾸지 않게** 한다. 분리된 clipboard 또는 제어된 실제 poll 지연으로 `R→L→paste`와 `L→R→paste`를 각각 검사한다. clone 상태 모델만으로 끝내지 않는다.

## F2 [P2] 두 paste 사이에 입력한 키가 두 번째 paste 뒤로 이동한다

위치: `viewer_window_proc.cpp:753–766`(HasWaiting이면 held key를 전부 보류), `:825–850`(일반 키는 공통 held queue, 두 번째 paste는 별도 waiting slot), `viewer_paste_gate.cpp:71–82,106–113,126–145`.

순서: paste A의 응답을 지연 → 사용자가 Enter down/up → 두 번째 paste B → A Applied → B Applied. Enter는 held queue, B는 waiting slot에 든다. A의 응답 때 HasWaiting이므로 Enter를 flush하지 않고 B를 먼저 승격한다. 실제 송신은 **paste A → paste B → Enter**가 되어 사용자가 친 **paste A → Enter → paste B**와 다르다. 일반 문자도 같은 문제이며, Enter는 원격 앱에서 제출/줄바꿈 위치까지 바꾼다.

보수 기준: 보류 입력과 paste gesture가 같은 순서 축을 공유하게 하거나, waiting paste 앞/뒤의 key segment를 구분한다. 기존 e2e F(`viewer_paste_e2e_test.cpp:928–947`)는 **paste A → paste B → Ctrl+B** 순서만 시험해서 현재 로직도 통과한다. `paste A → Enter → paste B`와 `paste A → 문자 → paste B → 문자`를 실제 WndProc/송신 sink에서 검사하고 modifier release/실패·취소 정책을 함께 유지한다.

## F3 [P2] 가상 파일 형식만 보고 모든 로컬 복사를 원격 helper 게시로 간주한다

위치: `viewer_window_proc.cpp:880–886`. `FileGroupDescriptorW`가 있고 CF_HDROP이 없으면 owner·현재 helper instance·게시 revision을 확인하지 않고 `NoteRemoteCopy()`한다. 이것은 GNLink에만 전용인 출처 식별자가 아니다.

순서: 원격에는 예전 R이 남음 → 로컬 앱/격리 시험 소유 창이 FileGroupDescriptorW(+FileContents), CF_HDROP 없음으로 새 L을 게시 → 해당 WM_CLIPBOARDUPDATE → Remote 판정 → Ctrl+V가 PassThrough되어 **원격의 예전 R을 붙인다**. L이 현재 지원 범위 밖이어도 Q3의 “실패, 키 없음, 옛 원격 내용으로 대체 금지”가 적용돼야 한다. 로컬 앱이 텍스트 병행 형식을 제공해도 같은 무조건 분기가 먼저 실행된다.

보수 기준: 실제 R→P helper의 현재 게시 완료/owner/instance·clipboard revision에 결합한 출처 정보를 사용한다. 파일 형식만으로 원격 출처를 추정하지 않는다. 진짜 R→P 가상 파일은 PassThrough를 유지하고, **비 GNLink 소유자의 동일 형식**은 로컬 snapshot 읽기/지원 불가 정책으로 처리하는 짝 시험이 필요하다.

## 확인한 계약과 검토 범위

- 80/81의 현재 배치(`paste_apply_wire.hpp`)는 고정부 48/40 bytes, 길이 제한·hash 대조·seq/request id echo·enum 범위 검사가 있다. “header.size는 고정부, 텍스트가 뒤따름”은 기존 51 스타일과 맞는다. 실제 구현 필드 순서는 최초 설계 그림과 다르므로 **현재 소스를 wire 정본**으로 기록한다.
- 호스트 80은 `HostClipboardHub::ApplyPaste`의 실제 SetClipboardData 결과 뒤 81을 만들며, 같은 내용이어도 write를 생략하지 않는다(`host_control_session.cpp:1241–1281`, `clipboard_monitor.cpp:267–306`). 일반 receipt만 Applied라고 부르는 오류는 이 성공 경로에 없다. 이번 검토가 clipboard thread timeout/취소와 모든 image/helper 수명 교차의 전면 승인을 의미하지는 않는다.
- 뷰어는 capability 0x1000으로 새 경로를 고르고, 새 호스트는 기존 51을 유지한다. 요청 기반인 80/81에 Hello 광고를 추가하지 않은 방향에 이견 없다. hostCopyGen용 flags bit1/reserved는 구 poll 파서가 해당 의미를 사용하지 않는 방식이며, **F1은 이 확장값으로 시간 순서를 추론하는 의미의 문제**다.
- WM_CLIPBOARDUPDATE와 세션 초기 push 처리에서 즉시 push를 없애고, snapshot/새 요청마다 Applied를 기다리는 큰 방향은 합의와 맞는다. ACK id·connection/target 세대가 아닌 답의 paste key 주입은 Gate가 거절한다. 전체 사용자 행동 성공을 이 정적 사실만으로 선언하지 않는다.
- APK 0.2.26의 14ccb41은 키보드 버튼 순서 변경으로 분리해 diff를 읽었다. 이번 PC paste 사후 검토에 APK 전체 품질/폰 실기 승인을 포함하지 않는다.

다음 담당: 검증용이 위 3건의 반례를 기존 task에 취합하고 우선순위를 조정한다. prelogin A0 착수를 막는 항목은 아니다. 제품/시험/설치 변경·빌드·commit/push 없음: 요청된 미추적 검토 기록만 작성했으며 구현/릴리스 이력은 후속 고정 후보의 결과와 함께 기록한다.
