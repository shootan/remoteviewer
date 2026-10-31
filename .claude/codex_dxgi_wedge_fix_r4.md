# DXGI r4 — Codex SOURCE/EVIDENCE 판정

2026-10-22 / task **t-dreqrszk** / 후보 **fc4cd57dfba81a70d37b0361ab6200baab84337f**.

**SOURCE / EVIDENCE OK — DW 보수 검토 범위의 잔여①/③ 닫음.** 이전 D1~D4 및 E3 판정은 유지한다. 상태는 **DW 테스트·통합 검토 입력 후보 / 회사 PC 실기 미검증**이다. 별도 hello 지연 수정과 통합 후보 검증은 남아 있으므로 전체 재연결 문제 해결·제품 구현 완료·일반 릴리스/게시 승인으로 읽지 않는다. 현재 DW 고정 후보에 대한 추가 보수 요청은 없다.

## 닫는 항목

- **③ 출하 Viewer 목록 요청 OK.** host가 monitor messages 지원을 처음 광고하면 기존 큐로 조회한다. Run 시작에 capability를 초기화하고 반복 WindowList에서는 재요청하지 않는다. 기존 비-picker `--monitor N` 선택 분기를 보존하며 나머지 지원 경로는 picker 여부와 무관하게 조회한다. 미지원은 요청 생성 및 큐 소비 양쪽에서 차단한다. ApplyMonitorList는 목록/revision/selectedMonitorId만 바꾸고 capture 선택·stream generation·restore descriptor를 변경하지 않는다. 시험 Viewer의 목록 요청 보조도 제거돼 제품 누락을 가릴 수 없다.
- **① monitor 변이 OK.** guard와 decision을 함께 우회해 새 viewer가 실제 `[restore] requested desktop`→Host의 desktop select applied에 도달한다. 독립 두 번째 실행은 “WRONG: it restored the desktop…”로 실패했고 충돌이 아니다. 첫 실행은 hello 지연으로 그 단계에 못 간 **INVALID**로 유지한다. r3의 충돌 FAIL은 유효 부정 대조가 아니었다는 기록 정정을 수용한다.

## 직접 대조한 증거

- 작업용 `.claude/report_dxgi_wedge_fix_impl_r4.md`, 검증용 `C:/work/gnlink-wt/verify-eop/.verify/verify_dxgi_wedge_r4.md`, 4db9f17→fc4cd57 관련 제품/시험 diff·목록/복원 경로를 읽었다. 양쪽 HEAD·tracked clean·CR-at-EOL 허용 diff-check를 확인했다.
- 검증용 build-vs 산출물 hash 직접 일치:
  - Viewer **57c4e0c33eaee1fd0d2e0efc2295aab241bfcbaec6d6af667330108981c15fbe**
  - Stream **673e5f8caeda16d5bc348365c33d3bae27eacdf108dc41d166e45a0b539b7ac8**.
- 독립 원본 `monitor`44/0, `nomonitors`29/0, `desktop`38/0, 유효 monitor 변이38/1의 결과와 fixture 로그를 확인했다. 실행rc/환경은 검증용 귀속이며 Codex 재실행이 아니다.
- 출하 GNLinkViewer가 pid별 목록1회, WindowList2회에도 조회 추가0, 목록 도착만으로 monitor 보고/자동 선택0 → toolbar 사용자 선택1회 → monitor 보고 → exit44/새 connect→restore=monitor→picker 사유·자동 선택0 → 수동 desktop present로 이어진다. 비지원 대조는 요청/Host 목록 응답/모니터 버튼0이다.
- `shots_monitor/E2-2-after-manual-pick.png`를 직접 열어 실제 출하 툴바의 **모니터1 ▾**와 수동 선택 후 스트림을 확인했다. 가상 두 번째 항목을 제공한 격리 Host 시험이며 실 물리 출력2개의 동일성 증명은 아니다.
- gate9/self20 원문 및 출하 Viewer/Stream seam0·시험 양성 자료를 대조했다. 옛 `GNLINK_VIEWER_TEST_REQUEST_MONITORS`는 시험 Viewer에서도 제거됐다.

## Stream 변경·미검증의 취급

Stream의 변경은 `REMOTE60_STREAM_TEST_SEAM` 전용 함수/조건과 플래그 설정의 중괄호 블록이다. 출하 전처리 경로에서는 같은 두 monitor flags를 무조건 설정한다. CMake define은 MigrationTest 시험 target에 한정됨을 확인했다. **출하 동작 변경이 없다는 소스 판정**을 수용하되 Stream의 바이트 동일성/재현 빌드를 입증했다고 쓰지 않는다. 새 hash를 산출물 기준으로 사용한다. 이 확인을 위해 별도 전처리/바이트 재현 작업을 추가 요구하지 않는다.

비-picker `--monitor N` 실행 및 control-resume 후 요청 수는 이번 독립 UI 사슬이 직접 검증한 항목은 아니다. 해당 선택 분기 보존과 같은 Run 안에서 capability가 재초기화되지 않는 소스를 확인한 한계로 남긴다. 이를 확인하려고 이미 통과한 DW 사슬을 전량 다시 돌릴 필요는 없다. 통합 때 실제 공유 경로 변경이 있으면 그 영향만 확인한다.

hello 지연은 첫 연결에서도12.835초로 발생한 새 관찰을 **별도 reconnect-hello-delay 과제**에 연결한다. 첫 monitor 변이 INVALID를 안전성 PASS나 실제 변이 검출로 합치지 않는다. 수정 설계 정본은 `.claude/codex_reconnect_hello_delay_r1.md`이며, 그 최소 수정과 독립 검증을 반영하기 전 전체 연결 문제가 해결됐다고 하지 않는다.

회사 PC NVIDIA/release-phase wedge/승격 ProgramData 실기록/재부팅·D4 MT 재생성 실행·실 두 번째 물리 출력 identity는 계속 미검증이다. minidump·MT 출하·Bⓑ2 보류를 유지한다. 자동복원은 logical desktop만, 명시 monitor/window는 picker 재선택이라는 승인된 제한도 유지한다.

## 다음 담당

검증용은 **DW의 현재 자동 보수 검토를 닫고 fc4cd57를 통합 검토 입력으로 고정**한다. LK e728485와 별도 hello 수정·SH의 현재 미결을 각각 고정 commit으로 관리하고, 통합 후 공통 protocol/gate 합집합·영향 있는 UI/재접속·새 artifact hash를 독립 확인해 Codex 최종 통합 검토로 보낸다. 이 판정만으로 버전 인상·서명·NAS 게시·사용자 설치·push를 진행하지 않는다.

사용자가 지금 할 일: 없음. Codex 변경은 미추적 판정서/인계뿐이며 제품 수정·빌드·설치/실서비스/UI 조작·commit·push 없음. 구현 기록/커밋은 기존 fc4cd57 기능 task에 귀속한다.
