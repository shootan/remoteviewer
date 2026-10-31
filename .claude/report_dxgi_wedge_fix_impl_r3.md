# report dxgi-wedge-fix-impl r3 — E2/E3 근거 보완 (작업용2 → 검증용 remote#wwml7h91, 2026-10-22)

task t-dreqrszk / rev r3 / kind=result. 근거 `.claude/codex_dxgi_wedge_fix_r2.md`, 지시 `task_dxgi_wedge_fix_impl_r1.md` r3 절.
**판정: E2/E3 프로세스 근거 확보(변이 대조 포함) · 제품 동작 변경 없음 · 아래 "발견" 1건은 Codex 판단 필요.** 버전·서명·게시·push 없음.

## 고정 후보
- `fix/dxgi-quarantine` HEAD **4db9f17**(f0ee888 위 1커밋).
  - 제품 소스 변경: 시험 빌드 `#ifdef` 와 viewer_present.cpp 의 지역 플래그 1개(`presentFailInjected=false`)뿐이다. 출하 경로 의미는 그대로다.
  - 테스트: 사슬 시나리오, runner, 시험·변이 뷰어 대상, gate 표식.
  - 문서: history.
- 빌드 `C:/work/gnlink-wt/fix-dxgi-quarantine-build`(Release) SHA256:
  - **GNLinkViewer `3ecabc05…c899`** — r2 의 `3eed1314…` 에서 바뀌었다(위 #ifdef 와 지역 플래그 때문).
  - Client `a6d845b1…cc09`, Host `b5f4936e…7dfb67`, Stream `4a0672a3…b711c` — r2 와 같다.
  - 시험 뷰어 GNLinkViewerR3Test `4a327070…af7e`.
- gate: `--dir` ALL PASS 9, `--self-test` 20(새 표식 `viewer-present-fail-seam`·`GNLINK_VIEWER_TEST_`).
  - 출하 GNLinkViewer 의 seam 문자열 0, 시험 뷰어 1(양성 대조).
- restore 순수 시험 30/30 유지.
- 시험 뷰어 `GNLinkViewerR3Test` = GNLinkViewer 와 같은 소스·옵션 + `REMOTE60_VIEWER_PRESENT_FAIL_TEST_SEAM`.
  - `GNLINK_VIEWER_TEST_PRESENT_FAIL_FILE` 이 있으면 video present 를 렌더하지 않고 실패를 보고한다. 그 뒤는 제품의 `note_present_result` 다(재시도 → 2 s 뒤 43).
  - `GNLINK_VIEWER_TEST_REQUEST_MONITORS=1` 이면 reveal 뒤 화면 목록을 요청한다.
- 실행: `node apps/directory/test/dxgi_wedge_reconnect_runner.js <Release> <scenario>`. GPU 슬롯 CreateNew 로 잡았고 해제했다. 원본은 `.claude/test-tmp/dxgi-wedge-*`.

## 발견 — 실제 제품에서는 명시 모니터를 고를 수 없다
- 시나리오 `monitor`(출하 GNLinkViewer, 시험 호스트가 화면 2개를 알림) 결과:
  - 툴바 sweep 으로 찾은 버튼은 1(대상 선택)·2(매크로)·4·5·6 이다. **화면 전환(3)이 없다.**
  - 이유: shell 이 띄우는 뷰어는 `--initial-view targets`(picker 모드)라서 화면 목록을 요청하지 않는다(`viewer_control_client.cpp` 의 `!startInPicker` 분기).
  - main 5063cd9 도 소스상 같다. r1 은 restore_on_reveal 의 부수 효과로 요청했고, r2 에서 그 요청을 없앴다.
- 결과: 현재 제품에서 PC 뷰어 사용자는 툴바로 모니터를 바꿀 수 없고, `monitor` descriptor 도 생기지 않는다.
- 다중 모니터 사용자에게 영향이 있는 기존 동작이다. 복원 계약(D1)과는 따로 판단할 일이라 **제품은 바꾸지 않았다.** Codex 판단이 필요하다:
  - picker 모드에서도 화면 목록을 요청할지
  - 그대로 둘지

## E2 — 툴바 명시 모니터 → monitor 보고 → shell → 새 뷰어 picker + 사유 → 수동 선택
- 이 PC 는 모니터가 1개다. 그래서 시험 호스트 seam 으로 화면을 2개 알리게 했다(`GNLINK_STREAM_TEST_MONITORS_FILE=extra`, 시험 빌드 전용 기존 seam).
- 제품 동등 구성(`monitor_test_viewer`): 시험 뷰어가 reveal 뒤 화면 목록만 요청한다(r1 이 하던 일). 그 뒤는 모두 제품 경로이고, 실제 툴바 창에 posted mouse 와 메뉴 키를 보냈다.
  1. 툴바에 화면 전환 버튼(3)이 생긴다.
  2. 메뉴(#32768)에서 두 번째 화면을 고른다 → `[viewer-restore] target=monitor`.
  3. DXGI wedge 44 → shell reconnect `restore=monitor`.
  4. 새 뷰어 `[restore] result=picker reason=monitor_not_restored`. 자동 restore 요청·select·restored 는 0 이다(4 s 관찰).
  5. 스크린샷이 picker 다(배경 47%).
  6. **수동으로 desktop 카드를 고르면** present·보고되고, 스트림 스크린샷이 나온다.
- 결과: **ALL PASS 42**(최종 회차).
- 변이 `GNLinkViewerR3Mutant_monitor_as_desktop`(명시 화면을 desktop 으로 복원)는 4번 "did NOT restore it" 에서 **FAIL** 했다.
- 한계: 실 두 번째 물리 출력, 출하 뷰어로 툴바 전환(위 발견 때문에 불가).

## E3 — A present → B ACK/decode → B present 실패 → 보고 0 · shell A 유지 → 재연결 A
- 시험 뷰어 `presentfail`:
  1. A(desktop) present → 보고 1회.
  2. 툴바 '대상 선택'으로 picker 를 연다.
  3. fixture 창 B(`!GNLINK-CHAIN-B`, 제목순 첫 항목)를 고른다. **목록 첫 항목이 B 인지 로그로 확인한 뒤에만 클릭**하고, 아니면 INVALID 로 중단한다. 이 PC 의 창은 고르지 않는다.
  4. 실패 파일을 만든 뒤 B 카드를 누른다 → `[restore] revealed gen=` (ACK + decode).
  5. present 실패 → 뷰어가 스스로 43 으로 종료한다. **B 보고는 0 이다(보고 1/1).**
  6. shell 로그: `viewer exit … code=43 … restore=desktop` → 새 뷰어 `result=restored target=desktop` → desktop 스트림 스크린샷.
- 결과: **ALL PASS 37**.
- 대조 `presentok`: 같은 경로에서 B present 성공 → `target=window` 보고 → (B 는 WGC 창이라 DXGI wedge 로는 끝나지 않으므로) present 실패로 세션 종료 → shell `restore=window` → 새 뷰어 `result=picker reason=window_not_restored`. **ALL PASS 36.**
- 변이 `GNLinkViewerR3Mutant_report_on_reveal`(r1 규칙: reveal 때 보고)는 **FAIL** 했다. 보고 2→3, `restore=desktop` 을 잃었고, 새 뷰어가 A 로 돌아가지 못했다.
- 참고: 순수 시험의 COMMIT_ON_ARM 변이는 통합 경로에서는 통과했다. 보고 시점이 아니라 내부 committed 값만 바꾸기 때문이다. 그래서 통합 대조로는 r1 규칙 변이를 썼다.

## 회귀
- 기본 `desktop` 사슬(출하 GNLinkViewer): **ALL PASS 38**(최종 회차).
- 전량 반복은 하지 않았다(지시대로).

## 미검증 / 다음
- 미검증 그대로: 회사 PC 실기(NVIDIA·release phase·승격 ProgramData·재부팅), D4 recreate 실행, 실 2번째 출력의 identity, MT 출하·minidump·Bⓑ2 보류.
- 다음: 검증용 독립 실행은 고정 후보 4db9f17 로 한다. 저는 SH r2 로 돌아간다. SH 는 남은 seam 시나리오 재실행과 보고가 남았다.
- 사용자 행동: 없음.
