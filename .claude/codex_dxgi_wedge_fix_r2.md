# dxgi-wedge-fix r2 — Codex SOURCE/EVIDENCE 검토

2026-10-22 / task **t-dreqrszk** / 후보 **f0ee8886774231f026928a1fd619cdf2aa41e0b3** (d26c526 D1/D2, c6ae018 D3, fb454dd D4, f0ee888 history; base5063cd9).

**SOURCE: D1~D4 보수 확인 OK. EVIDENCE: NEEDS_CHANGES — 아래 E2/E3 두 프로세스 경로의 검증을 보완한다.** 새 제품 결함을 발견했다는 판정은 아니다. desktop의 재연결→WGC→클릭 없는 새 화면 present는 독립 근거를 확보했다. 명시 monitor 안전 전환과 present 실패 반례는 현재 순수 helper/변이 시험뿐이므로, 전체 구현 완료/통합·출하 승인은 아직 주지 않는다. 기존 task에서 영향 있는 두 경로만 연결하고 무관한 전량 재검사를 하지 않는다.

## 직접 확인한 대상과 증거

- 작업용 `.claude/report_dxgi_wedge_fix_impl_r2.md`, 검증용 `C:/work/gnlink-wt/verify-eop/.verify/verify_dxgi_wedge_r2.md`, 158aff3→f0ee888의 23파일 변경 목록·관련 제품/시험 diff·소스를 읽었다. 검증 worktree의 고정 HEAD 및 tracked clean을 확인했다.
- 독립 build-vs의 파일 hash를 직접 계산해 대조했다:
  - Stream `4ce6197a353b5076268b0cf491e443209839bfe32fda040fbaf6192c718d4d0e`
  - Host `14a06bee4d43e3143d20bf8e26fe2f6bfafb093cd91627ed2556a3526251e646`
  - Viewer `6fbc465b3d0ad9834a5578379e2e01292612ed41cae256987677ce3bf3af2ce2`
  - Client `de595ba0065c37b19b58552f9230482162433e15f74e39a5f731641307baa99c`
  - MtAb `a8b7057f8d737849fe810e0eaee9ac0615f5da6aac7560f95c91fb9ee7ebccf5`
  - shell.html `8fac49cd810e1445d36305bf5e62e9e539982fb0b671be86cb9008ee5ad62811` (r1과 같음).
- `.verify/dxgi_r2/` 원본 로그에서 store64/0×3, restore30/0, quarantine23/0, snapshot7/0, gate9/self18, 네 변이의 실패(3/1/6/1)를 확인했다. 최초 lock 생성 변이는 실제 20라운드 모두 bad, 정상은 bad0이다. 종료코드와 실행 환경은 검증용 기록을 인용한다. Codex는 빌드/제품 테스트를 재실행하지 않았다.
- UI recovery84/0 및 전체 사슬38/0 ALL PASS 원문을 확인했다. 사슬 fixture `.claude/test-tmp/dxgi-wedge-h392yx/shelldata/viewer.log`의 reveal→101ms 뒤 실제 present 보고, 새 viewer의 reveal→64ms 뒤 report/result=restored를 직접 대조했다. runner의 wedge→restored **19.8초**는 해당 격리 실행의 수치이며 현장 release-phase wedge 측정이 아니다.
- 검증용 `shots/3-reconnecting-0.png`와 `4-restored.png`를 직접 열어 재연결 안내와 복원 후 desktop 스트림을 확인했다. 사슬 실행은 검증용 귀속, Codex는 그 산출물의 독립 열람이다.

## D1~D4 판정

| 항목 | SOURCE 판정 / 근거 | EVIDENCE 한계 |
|---|---|---|
| D1 대상 의미 | **OK.** desktop=논리 desktop/current primary, monitor=명시 선택, window 분리. `DecideRestore`는 monitor/window를 즉시 picker로 남긴다. 기존 이름+rect 매칭과 목록 미지원/4초 primary 우회 제거. toolbar `RequestMonitorSelect`가 접수되면 monitor로 보수적 downgrade하여 desktop 잔존을 막음 | monitor를 실제 UI에서 고른 뒤 재연결하는 경로는 E2 보완 |
| D2 표시 후 확정 | **OK.** reveal은 generation 후보 Arm, `viewer_present.cpp`의 실제 `presented && !pickerVisible`에서 프레임 메타데이터 generation으로 commit/report. 새 pick은 후보 취소. shell은 새 보고 전까지 이전 값을 유지. toolbar의 조기 monitor downgrade는 자동복원 불가 값으로만 내려가는 안전 예외 | helper가 Arm 시 commit하는 변이는 잡지만, 실제 실패한 present가 report를 만들지 않는 wiring 반례는 E3 보완 |
| D3 future/lock | **OK.** 허용 skew 밖 future는 새 관측부터 재시작. lock 생성 share RW 및 create/open 경합을 동일3초 기한 내 재시도. owner/ACL/no-follow/총기한 유지 | 보호된 시험 폴더/별도 writer 근거 확보. 승격 ProgramData 제품 경로의 실기록은 미검증 |
| D4 MT 순서 | **시험 코드 SOURCE OK.** 새 context 직후 `mt_ab_apply(newCtx)`가 WinRT wrapper/MF/scaler 공개보다 앞섬. 초기 호출·출하 MT 무변경 유지 | 재생성 실행은 미관측. test-only A/B 현장 체크리스트에 유지, 제품 기본 MT 승인 근거로 쓰지 않음 |

명시 monitor 자동 복원을 보류하는 최소안은 r1에서 이미 허용한 범위다. 안정 output identity의 새 wire 설계를 이번 필수 보수로 추가하지 않는다. toolbar가 요청 접수 시 monitor로 먼저 내리는 것은 보수적 기능 제한이며, 아직 보지 않은 화면의 자동 공개를 허용하는 변경이 아니다. 문서/acceptance에는 **desktop만 자동 복원, 명시 monitor·window는 picker에서 재선택**을 명시한다.

## E2 — 명시 monitor → 재연결 picker의 실제 프로세스 근거 (필수 보완)

현재 monitor_as_desktop 변이는 `DecideRestore` 순수 함수만 검사한다. 새로 추가된 toolbar 접수→downgrade→stdout→shell 보관→새 viewer 파싱→picker 안내 경로의 동작은 증명하지 않는다. 이 부분을 코드 존재/30 PASS만으로 최종 승인하지 않는다.

- 격리 제품 UI에서 명시 monitor 선택을 발생시키고, shell이 `monitor` 보고를 보관한 뒤 재연결할 때 실제 새 viewer가 picker+사유를 표시하며 **자동 desktop 선택/새 stream 공개0**임을 확인한다. 실제 두 번째 물리 monitor의 안정 동일성을 검증하라는 뜻은 아니다. 가능한 명시 primary 선택도 이 의미를 갖는다.
- toolbar의 구형 ControlMonitorSelect 경로를 포함한다. 실제 UI 제약으로 외부 장비가 필요한 부분이 있다면 그 정확한 제약을 기록하고, 가능한 제품 동등 시험 구성의 실제 callback/제어/shell/새 viewer 경로를 검증한다. 별도 helper를 직접 호출해 항상 성공시키는 시험은 대체 근거가 아니다.
- 사용자 수동 선택 뒤 stream 표시가 가능함도 확인한다. desktop 정상 사슬은 이미 통과했으므로 이 반례를 위해 전체 desktop wedge 사슬을 반복할 필요는 없다. 안전 우회를 되살리는 변이가 통합 경로에서 실패하는지 확인한다.

## E3 — present 실패 시 B를 보고하지 않고 A를 유지하는 프로세스 반례 (필수 보완)

현재 commit_on_arm 변이는 순수 tracker를 검사한다. 실제 renderer의 실패 결과와 `restore_frame_presented` 호출 조건, stdout/shell 값 유지가 연결됐는지는 성공 사슬만으로 확정할 수 없다.

- 격리 제품 프로세스에서 A 실제 present/보고 → B 선택 ACK·decode/reveal → **B present 실패** → 새 B 보고0·result=restored0 → 실패 회복/exit43 → shell의 이전 A 유지 및 재연결 A 경로를 확인한다. B는 기존 시험 창 등으로 구분 가능하며 NVIDIA나 실제 장치 손상을 요구하지 않는다.
- 실제 제품 paint/render 결과 경계의 **시험 빌드 전용 실패 주입**은 허용한다. 실패가 아닌 성공 stub이나 tracker만 호출하는 시험으로 대체하지 않는다. B present 성공 대조에서 B로 갱신되는 것도 확인한다. 이미 있는 이전 대상을 새 viewer가 보고하기 전 유지하는 shell 동작을 이용한다.
- seam이 필요하면 출하 target과 분리하고 출하 부재/시험 양성 대조를 함께 확인한다. 제품 로직 변경이 필요 없다면 근거 보완만으로 닫는다. 검증용이 고정 후보에 대해 독립 실행하며, 수정이 필요하면 기존 작업용2 task로 취합한다.

## 계약 B1~B8 / 남은 범위

- **B1:** test-only A/B와 D4 소스 순서 보완 확인. MT 출하/NVIDIA 인과/재생성 실행 판정은 계속 보류.
- **B2/B3:** D3 소스·독립 파일/프로세스 반례 OK. 실제 승격 Host의 ProgramData SY/BA 쓰기는 미검증이며 출하 실기 근거와 구분한다.
- **B4/B5:** r1의 prelogin 제외·격리 우선 판정 유지. 이번 run exit44 뒤 WGC/격리 적용 사슬 확보. 영속 기록은 `folder-unresolved err=3`이라 이 사슬로 재부팅 영속화를 증명하지 않는다.
- **B6:** D1/D2 SOURCE OK, E2/E3의 동작 근거 보완 전 최종 완료 보류.
- **B7:** r1 E1 desktop 독립 UI 사슬 보완 **OK**. E2/E3만 자동 검증 잔여로 연결한다. 회사 PC 실기(release-phase wedge/NVIDIA/승격 ProgramData/재부팅)는 별도 미검증.
- **B8:** 기존 snapshot 판정 유지, minidump 보류 승인 범위 유지. Bⓑ2·MT 출하도 보류 유지.

staging 정리 WARN 및 runner pid=0 표시는 비차단 한계로 기록한다. 설치 제품/실서비스를 건드리거나 방화벽 규칙을 임의 삭제하지 않는다. 이번 실행에서 방화벽 창이 없었다는 것은 검증용 관찰이며 기존 시험 규칙 생성 기록을 지우지 않는다.

다음 담당은 검증용이다. E2/E3를 한 유효 근거 보완 요청으로 기존 **t-dreqrszk**에 연결한다. 이미 확보된 성공 사슬과 D3 전량을 매번 반복하지 않는다. SH와 별도 기능이며 worker2에게 Codex가 중복 병렬 지시하지 않는다. APK/S5·버전·서명·게시·설치·push 범위 변경 없음. Codex 변경은 미추적 판정서/인계 메모뿐이고 제품 편집·빌드·commit·push 없음.
