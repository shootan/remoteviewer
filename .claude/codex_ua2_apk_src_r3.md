# UA-2 APK SOURCE r3

2026-10-27 / 검증용 `remote#yj8mrmrx` / 입력 `report_ua2_apk_r1.md` r3 전문.

**제품 SOURCE OK — 남았던 N1 처리/실제 적용 구분과 N3 capability snapshot 경계 닫음. 기존 intent/seq·창/바탕화면 분리·legacy 안내도 유지한다. 통합 EVIDENCE·APK 게시 승인은 별도다.**

제품 **482a9014c6cdc8079d0e1c76a7af4e28a0b59f2d**, 시험 **3e4d567d0ecc557fa5bb50063655507d57aa4810** 각각 전체 diff와 JNI·HostRoleGate·Host backend 상태 응답/관련 UI 호출을 읽었다. 두 diff whitespace rc0. 검사 시 worktree HEAD=3e4d567·tracked clean. picker 제품9afcec7의 SOURCE OK는 유지하며 SH r4 WIP는 검토하지 않았다.

## N1: 닫음

- 현재 intent가 전송됐는지, 해당 sentSeq 또는 후속 상태인지, NotYetApplied가 없는지 먼저 확인한다. 처리 전·옛 응답은 여전히 APPLYING이며, prelogin의 처리된 거부는 REFUSED terminal이다.
- requested==현재 요청이라도 active가 다르면 **RECEIVED**로 “접수·아직 이 방식으로 캡처하지 않음”을 표시한다. idle 저장인지 restart 실패인지 wire에 없는 원인을 추측하지 않는다. terminal이라 새 선택도 가능하며, 후속 조회에서 active가 요청에 도달하면 APPLIED로 바뀐다.
- requested와 다른 effective가 실제 active인 경우 **POLICY_REPLACED**로 정책 대체를 표시한다. 현재 요청과 active가 일치할 때만 “요청 X 적용됨”이다. 창 대상은 그보다 먼저 STORED_FOR_DESKTOP으로 분리해 현재창WGC와 바탕화면 요청 저장을 유지한다.
- 요청이 없고 현재 owner 상태도 아직 없는 0필드/NotYetApplied 또는 유효한 desktop active가 없는 경우 CHECKING이다. WINDOW_TARGET이 확인된 경우의 현재창WGC는 별도로 표시할 수 있다. “지금 ?”를 확정 IDLE로 만드는 r2 문제는 닫혔다.
- 버튼 점등은 **Host가 처리한 바탕화면 요청**이라는 기존 r2 의미다. 점등만으로 현재 캡처 성공·영상 표시 성공이라고 보고하지 않는다. 실제 backend 상태와 최종 정상 영상은 여전히 별개다.

## N3: 닫음

`DesktopBackendCapability()`는 HostRoleGate의 **한 Load로 얻은 role 값**에서 known과 해당 flags의 backend-status 지원 여부를 만든다. JNI도 이 snapshot 두 필드만 사용한다. 기존의 answers 먼저/known 나중 두 읽기는 없다. 첫 pong이 그 사이에 처리돼도 snapshot은 과거의 unknown 또는 이후의 known+answers 중 하나이며, 서로 다른 시점의 조합을 만들지 않는다.

생산 JNI 호출은 stopPoint를 전달하지 않는다. 읽기 직후 첫 pong을 통과시키는 P10은 이 getter를 직접 시험한다. 새 네트워크 필드·권한·Host 수명 변경은 없다. 최종 출하 매크로/시험 seam 검사도 기존 통합 gate에서 유지한다.

## 확인한 근거와 남은 EVIDENCE

Codex가 직접 읽은 저장 근거:

- `psel_ua2r3.txt`의60/0과 `psel_ua2r3_mut.txt`의 P10 **known=1 answers=0 FAIL**. 보수 snapshot은 읽기 후 첫 pong이 처리돼도 known=0/answers=0이고 다음 읽기는 known/answers 모두 true다.
- Kotlin 변이 로그의 **266 tests, 3 failed**: handled/actual·quarantine·요청 없는 미확정 상태 세 사례가 r2 규칙을 검출한다. 추가 old A 응답(seq3<sentSeq5, 요청 코드 동일)도 단위 코드에서 APPLYING/점등 없음으로 기대한다.
- 저장 `ua2r3/ua2_tap_lines.txt`의 CHECKING→IDLE(window=true)→GDI 요청→APPLYING→STORED_FOR_DESKTOP(seq7/sentSeq7) 흐름.

위 실행 및 단위266/0·AVD ua2 13/13은 **작업용 귀속**이다. Codex가 새 빌드/AVD/테스트를 실행한 것은 아니다. 현재 AVD는 창 대상 경로이며 **바탕화면 대상 IDLE 실제 UI나 backend 전환 성공을 실행 증명하지 않는다**. `host_capture_lifecycle_ua_e2e`도 이번 실행에 포함되지 않았다는 구분은 타당하다. 기본 active=DXGI 소스값 하나만으로 UI 동작/전환 EVIDENCE를 대신하지 않는다.

최종 고정 APK의 검증용 독립 검사에, 안전한 통제 fixture/기존 prelogin 시험과 묶을 수 있는 범위에서 **창이 아닌 대상의 상태 없음→CHECKING→확인된 IDLE 및 접수/실제 상태 표시**를 실제 제품 UI로 확인한다. 필요하면 backend 응답 경계의 product-equivalent fixture를 쓰되 무엇을 대체했는지 명시한다. 사용 중인 PC 화면을 임의 캡처하거나 실제 UAC/설치/재시작을 하는 요청은 아니다. 실제 Host desktop 전환·UAC 실기는 해당 실행 근거와 따로 남긴다.

구 Host/new Host·재연결 전파0·저장값 제거/SH 보존과 요청/현재창 분리도 최종 통합 후보의 기존 독립 체크에 묶는다. 이미 닫힌 소스를 다시 고치거나 266건 전량을 매 단계 반복할 필요는 없다. 별도 미결인 picker 시험 helper E1은 기존 seq14189 판정대로 시험 배선만 보완하고 EVIDENCE에 확인한다. 새 제품 SOURCE 왕복은 필요 없다.

같은 담당은 SH r4 잔여 보수 후 최종 제품/시험 commit·APK 해시를 새로 고정해 검증용 독립 행동/gate/버전·ABI·서명자 검사→Codex EVIDENCE→기존 승인된 게시 절차로 이어간다. APK0.2.32가 게시됐거나 전체 task가 완료됐다는 판정은 아니다. PC163 게시 재검토·새 병렬 writer는 없다.

사용자 현재 조치 없음. Codex 제품 편집·빌드·시험 실행·commit/push 없음. 이 미추적 판정서/인계 외 공개 history/계획·Git MCP commit은 같은 구현 작업에 취합한다.
