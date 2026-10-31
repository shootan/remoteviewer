# report dxgi-wedge-fix-impl r2 — Codex 158aff3 NEEDS_CHANGES D1~D4 보수 (작업용2 → 검증용 remote#wwml7h91, 2026-10-22)

task t-dreqrszk / rev r2 / kind=result. 근거 `.claude/codex_dxgi_wedge_fix_r1.md`, 지시 `task_dxgi_wedge_fix_impl_r1.md` r2 절.
**판정: D1~D4 보수 + 자동 검증 완료 · E1 독립 UI 사슬은 검증용 · 회사 PC 실기 대기.** 버전·서명·게시·push 없음.

## 고정 후보
- worktree `C:/work/gnlink-wt/fix-dxgi-quarantine`, 브랜치 `fix/dxgi-quarantine`. 158aff3 위에 4커밋, HEAD **f0ee888**.

| commit | 내용 | 구분 |
|---|---|---|
| d26c526 | D1/D2 뷰어 복원·보고 | 제품(viewer) + 테스트 |
| c6ae018 | D3 저장소 future 기록·최초 lock 경합 | 제품(host store) + 테스트 |
| fb454dd | D4 MtAb 재생성 경로 MT 순서, `where=`·`mf-device-set` 로그 | 시험 빌드 경로(출하 MT 무변경) + 제품 로그 1줄 |
| f0ee888 | history 항목 | 문서 |

- 빌드 `C:/work/gnlink-wt/fix-dxgi-quarantine-build`(Release, 전체 빌드 exit 0). 빌드는 d26c526~fb454dd 와 같은 소스다. 차이는 커밋 직전 CMakeLists 끝 빈 줄 1줄 정리뿐이다. SHA256:
  - GNLinkStream `4a0672a3…b3b711c`
  - GNLinkHost `b5f4936e…b7dfb67`
  - GNLinkViewer `3eed1314…e1c98b`
  - GNLinkClient `a6d845b1…cc09`
  - GNLinkStreamMtAb `0b27aeb6…f6b9`
  - GNLinkStreamMigrationTest `135885f5…0e22`

## D1 — 명시 monitor 를 primary 로 바꾸지 않는다
- descriptor 는 `desktop` | `monitor` | `window` 이다.
  - **자동 복원은 `desktop`(논리 desktop) 만 한다.** desktop 카드와 같은 WindowSelect(0) 이고, 호스트가 현재 primary 를 잡는다(`host_loop_helpers.cpp` desktop 분기). 화면 목록이 없거나 늦거나 미지원이어도 의미가 같으므로 목록을 기다리지 않는다.
  - `monitor`(사용자가 고른 화면)는 **자동 복원하지 않고 picker + "직접 고른 모니터는 같은 화면인지 확인할 수 없어 자동으로 고르지 않습니다"** 를 보인다. 이유: GDI `\\.\DISPLAYn`+rect 는 안정 출력 identity 가 아니다. Codex 가 허용한 안전 최소안이다. 안정 identity 는 새 wire 가 필요하므로 별도 설계 delta 로 둔다(이번에 하지 않음).
  - e711f08 의 4 s/미지원 primary fallback 과 이름+rect 매칭(MatchRestoreMonitor)은 제거했다.
- **추가 발견**: PC 뷰어 툴바 화면 전환은 구형 ControlMonitorSelect 를 쓴다(`viewer_startup.cpp` onMonitor). 호스트가 적용 전에 목록으로 답하고 generation 을 올리지 않는다(`host_control_session.cpp:1440~`, `host_stage_selection.cpp:101~`). 그래서 r1 의 "새 generation decode 시 보고" 는 발화하지 않았고, 다른 화면으로 옮겨도 descriptor 가 `desktop` 으로 남았다. 이것이 D1 위험 그 자체다.
  - 이제 전환 요청이 받아들여지는 순간 `monitor` 로 낮춘다. picker 로만 이어지는 값이므로 present 전 확정이어도 낯선 화면을 공개하지 못한다.
- 순수 시험 `DecideRestore`:
  - desktop → select 0
  - monitor → picker `monitor_not_restored`
  - window / locked → picker
- 한계(acceptance 표기 필요): **명시 monitor·창의 재연결은 picker 다.** 현장 사례(한 화면 PC, desktop)는 그대로 자동 복원된다.

## D2 — 실제 present 성공 frame 에서만 commit
- reveal(확정 + 첫 decode)은 `RestoreCommitTracker::Arm(target, readyGeneration)` 후보일 뿐이다. `viewer_present.cpp` 의 `presented==true`(picker 숨김) 프레임 generation ≥ 후보 generation 일 때 commit 한다. 그때 `[viewer-restore] target=…` 보고와 `result=restored` 가 나간다.
- 새 pick 은 후보를 취소한다. decode 시점 보고(viewer_restore_frame.hpp, kMsgRestoreReport, restore_report_current)는 제거했다.
- shell 은 새 뷰어가 보고하기 전까지 이전 값을 유지한다(기존 `gRestoreByOperation`). 그래서 A present → B ack/decode 만 → present 실패·종료 → A 복원이다.
- 순수 시험:
  - 무 present 무 commit
  - stale generation 무 commit
  - A→B decode 만 → A 유지, B present → B
  - 취소된 후보는 늦게 present 돼도 무 commit
  - 툴바 `monitor` 이후 desktop 재선택은 present 전까지 `monitor`
- 실 프로세스 사슬 로그 순서(뷰어 2개 모두): `revealed gen=2 (reported once presented)` → (65 ms 뒤) `[viewer-restore] target=desktop` + `reported why=presented` → 재연결 뷰어 `result=restored target=desktop gen=2`.
- **present 실패 반례는 순수 시험으로만 했다.** 실 swapchain present 실패를 일으키는 프로세스 수준 반례는 만들지 않았다.

## D3 — 저장소
1. `NextRecord` 는 시작 시 받아들여질 기록(last ≤ now+300 s)만 이어 쓴다. 거부되는 future 기록 위의 실제 wedge 는 now 부터 새로 시작한다(wedges=1, first=last=now). 허용 skew 안의 약간 앞선 기록은 이어 쓴다(기존 단조 유지).
2. lock 파일을 FILE_SHARE_READ|WRITE 로 만든다. 열기·생성 중 sharing violation 이나 생성 경합(FILE_EXISTS)은 같은 3 s 기한 안에서 20 ms 간격으로 재시도한다. ACL/owner/no-follow 검사와 총 기한은 그대로다.

시험(`capture_recovery_store_test`, `REMOTE60_TEST_SCRATCH_STANDALONE=1` → 빌드 폴더 안 scratch):

| 실행 | 결과 |
|---|---|
| 정상 3회 | 64/64 ×3 |
| mutant CONTINUE_FUTURE | 3 FAIL: 순수 2, 실폴더 1(last=now+86700, wedges=2) |
| mutant UNSHARED_LOCK_CREATE | **20/20 round FAIL**(writer rc=80 lock, wedges 5/6) |

- 새 검사:
  - 순수: 거부 future → 새 시작 → 다음 시작 격리 / skew 안 이어 쓰기
  - 실폴더: future 기록 심음 → 시작 거부 확인 → wedge 기록 → 다음 시작 격리
  - **빈 보호 폴더(lock 없음)에 별도 writer 프로세스 2개를 이벤트로 동시에 풀기 ×20 round**: 모든 wedge 기록(6/6), 최악 62 ms

## D4 — MtAb(시험 빌드) 재생성 순서
- `RecreateCaptureDeviceOnPrimary`: 새 context 생성 직후(WinRT wrapper·MF·scaler 전)에 `[mt-ab] where=device-recreate(<reason>)` + `mt_ab_apply(newCtx)` 를 한다. MF set 뒤에 `d3d-mt at=mf-device-set` 를 남긴다. startup 에도 `where=startup` 를 남긴다.
- 실행 확인(MtAb, arm=on, loopback, 격리 LOCALAPPDATA, AVD 앱이 수동 연결로 client 역할):
  - `where=startup` → `arm=on before=0 after=1` → `d3d-mt at=device-created state=on` → `at=mf-device-set state=on` → `at=winrt-wrapper state=on`
- **재생성 경로 실행은 미관측**(어댑터 무출력/변경 조건이 필요하다). 소스 순서와 컴파일만 확인했다.
- 출하 MT 무변경. off 강제 없음.

## 회귀·gate
- 전체 사슬 `dxgi_wedge_reconnect_runner.js`(GPU 슬롯 CreateNew 05:20~05:24, 해제함) **ALL PASS**:
  - host_login_uia ALL PASS
  - client_reconnect_restore_e2e ALL PASS: picker 스크린샷 → desktop present 보고 → wedge exit 44 → 자동 재연결 → 클릭 없이 restored → 스트림 스크린샷 → 프레임 계속
  - watchdog phase=frame_handler, supervisor exit 44, 재기동 child WGC
  - `capture-quarantine not recorded reason=folder-unresolved err=3` 는 r1 과 같다(비승격 시험 호스트라 ProgramData 폴더 없음).
  - fixture `C:/work/gnlink-wt/fix-dxgi-quarantine/.claude/test-tmp/dxgi-wedge-pTIGiN`
- `client_recovery_ui_runner.js` **84 PASS / 0 FAIL**, `ALL PASS`(R1~R6 restore 포함). stand-in 보고값은 `desktop` 으로 갱신했다.
- `gnlink_check_fixed_server.py --dir <Release>` ALL PASS 9.
  - mutant 정의는 별도 시험 exe 에만 있고 출하 target 에는 없다.
  - **shseam/sh 표식은 이 브랜치 범위 밖이다**(SH r2 H4 에서 처리).
- 기존 영향 없는 GPU 전량 반복은 하지 않았다(Codex E1 허용).

## E1 / 남은 것
- 검증용 독립 UI 사슬: 고정 후보 f0ee888 빌드로 '재연결 중 → 클릭 없이 desktop 새 present'와 반례를 확인해야 한다(제 실행이 대신하지 않음).
- 미검증:
  - 실 present 실패 프로세스 반례
  - MtAb 재생성 경로 실행
  - 회사 PC 실기(NVIDIA·release phase·승격 ProgramData·재부팅)
  - 명시 monitor 안정 identity(설계 delta)
- 보류 유지: minidump · Bⓑ2 · MT 출하.
- 사용자 행동: 없음.

## 다음
- 작업용2 는 이제 SH r2(t-b7ss11ov, `codex_sh_filter_impl_r1.md` H1~H5)에 착수한다.
