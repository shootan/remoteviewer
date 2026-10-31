# report dxgi-wedge-fix-impl r4 — 출하 뷰어 화면 목록 요청 · monitor 변이 재작성 (작업용2 → 검증용 remote#wwml7h91, 2026-10-22)

task t-dreqrszk / rev r4 / kind=result. 근거 `.claude/codex_dxgi_wedge_fix_r3.md` ①·③, 지시 `task_dxgi_wedge_fix_impl_r1.md` r4 절. ②(hello 지연)는 별도 task 라 손대지 않았다.
**판정: ③ 제품 보수와 ① 유효 변이 확인 완료 · 회사 PC 실기 대기.** 버전·서명·게시·push 는 없다.

## 고정 후보
- `fix/dxgi-quarantine` HEAD **fc4cd57**(4db9f17 위 1커밋).
  - 제품 변경: `viewer_control_client.cpp`.
  - 시험 seam: `host_window_enum.*` / `host_control_session.cpp` 의 `#ifdef REMOTE60_STREAM_TEST_SEAM`.
  - 변이 정의: `viewer_restore.cpp` 의 `#ifndef` 1곳.
  - 테스트: 사슬 e2e·runner. 문서: history.
- 빌드 `C:/work/gnlink-wt/fix-dxgi-quarantine-build`(Release, 전체 빌드 exit 0) SHA256:

| 파일 | 해시 | r3 대비 |
|---|---|---|
| GNLinkViewer | `85ffb7ad…6d47d65` | 바뀜 |
| GNLinkStream | `61488567…0c0e8e` | 바뀜 |
| GNLinkClient | `a6d845b1…cc09` | 같음 |
| GNLinkHost | `b5f4936e…7dfb67` | 같음 |
| GNLinkStreamMigrationTest | `0771e768…2f56` | — |

  - GNLinkStream 의 소스 변경은 시험 빌드 전용 `#ifdef` 와 중괄호 블록뿐이다. 그래도 해시가 바뀌었는데, 이 빌드는 바이트 재현이 보장되지 않기 때문이다. 동작 변화라고 읽지 않는다.
- gate: `--dir` ALL PASS 9, `--self-test` 20. 출하 GNLinkViewer 의 seam 문자열 0.
- restore 순수 시험 30/30.

## ③ 출하 뷰어의 화면 목록 요청 (제품)
- `handle_window_list`: window list 의 monitor 플래그가 **이 연결에서 처음** 켜지면 `RequestMonitorList()` 를 1회 호출한다(기존 큐). picker 모드인지와 관계없다.
  - 비-picker `--monitor N` 의 MonitorSelect 분기는 그대로다.
  - 로그: `[control] monitor-list requested`.
- `ControlClient::Run()` 시작에 `SetHostSupportsMonitors(false)` → capability 와 목록이 그 연결 소유가 되고, 요청은 연결당 1회다.
- 조회만 한다. select·stream·generation·복원 descriptor 는 바뀌지 않는다(목록 적용은 snapshot 의 목록·selectedMonitorId 갱신뿐).
- 시험 뷰어의 보조 `GNLINK_VIEWER_TEST_REQUEST_MONITORS` 는 제거했다. 제품 누락을 가릴 길을 없앤 것이다.
- 시험 호스트 seam: monitors 파일 `nomonitors` 면 monitor messages 플래그를 알리지 않는다(구형 호스트 대조).

## 사슬 결과 (출하 GNLinkViewer, 격리 Host·loopback)
- **monitor (E2) 44/0**:
  - 뷰어가 목록을 1회 요청했다. 목록만으로 `target=monitor` 보고는 0 이다.
  - 툴바 sweep 에서 화면 전환(3)이 나타났다.
  - 메뉴에서 2번째 화면 → `monitor` 보고 → wedge 44 → shell `restore=monitor` → 새 뷰어 `result=picker reason=monitor_not_restored`. restore 요청·select·restored 는 0 이다.
  - picker 스크린샷 → 수동 desktop 선택 present·보고 → 스트림 스크린샷.
- **nomonitors 29/0**: 목록 요청 0, 툴바 전환 버튼 없음(버튼 1·2·4·5·6).
- **desktop 38/0**: 기본 복원 회귀.
- 시험 호스트가 알리는 두 번째 화면은 가상 항목이다. 실 두 번째 물리 출력의 동일성은 미검증 그대로다.

## ① monitor_as_desktop 변이 재작성 · r3 보고 정정
- **정정**: r3 의 이 변이 FAIL 은 유효한 부정 대조가 아니었다.
  - guard(`restore_step` 의 non-Desktop → fail)는 그대로 두고 `DecideRestore` 만 `{true, nullptr}` 로 바꿨다. 그래서 `fail(nullptr)` 에서 **뷰어가 충돌**(0xC0000005)했다.
  - r3 보고의 "변이 FAIL(통합 경로)" 서술은 이 충돌을 잘못된 복원의 검출로 센 것이다.
- **재작성**: 변이 정의 아래에서 guard 와 결정을 함께 우회한다 → 실제로 desktop 을 요청·present·보고한다.
- E2 판정은 두 결과를 기다려 이름을 붙인다:
  - picker+사유 = 정상
  - `[restore] requested`/`result=restored` = **WRONG**
  - 둘 다 없음(종료·정지) = INVALID(exit 3)
- 결과 `GNLinkViewerR3Mutant_monitor_as_desktop`: 38 PASS 뒤 **"WRONG: it restored the desktop on its own for an explicitly chosen screen"** 로 FAIL. 뷰어 충돌 없음.

## E3 / 기록
- E3 은 Codex 판정대로 닫혔다.
- r3 첫 presentfail 실행 35/1 FAIL(첫 재연결 hello 지연 → exit43 → 두 번째 시도에서 A 복원)과 이후 3 PASS 는 그대로 둔다. 소급 PASS 로 바꾸지 않는다.
- hello 지연(②)은 작업용1 의 별도 조사 task 다.

## 미검증 · 다음
- 미검증: 회사 PC 실기(NVIDIA·release phase·승격 ProgramData·재부팅), D4 recreate 실행, 실 두 번째 출력 identity.
- 보류: minidump·MT 출하·Bⓑ2.
- 검증용: 고정 후보 fc4cd57 로 독립 사슬(monitor·nomonitors·변이·desktop)을 실행한다.
- 저는 SH r3 로 돌아간다. b6ac421 에서 멈춰 둔 상태이고, race 반례 seam 보수와 touch_host 크게보기 1건이 남았다.
- 사용자 행동: 없음.
