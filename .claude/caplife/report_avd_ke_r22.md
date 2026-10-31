# keepalive-r2 e — AVD ke-r22 실행 결과 (2026-10-29 17:37–17:39, 계획 plan_avd_ke_r22.md 08824a43…)

## 판정: **INVALID (시나리오 0개 실행)** — bundle rc 3
- AVD 스크립트(`ke_avd.py`)는 첫 시나리오 E1 의 로그인 도중에 **Python 예외로 끝났다**. 마지막 step 은 `tapped loginIdInput … text='ketest'` 다. 결과는 `run end: 0/0 PASS, 0 NOT RUN` 이고, `result.json` 은 results/not_run/marks 가 모두 비어 있다.
- **예외 원문(traceback)이 남지 않았다**:
  - 원인은 `ke_slot.ps1` 이다. `$ErrorActionPreference='Stop'` 아래에서 `& python … > avd_stdout.txt 2>&1` 를 실행했다. PowerShell 5.1 은 native stderr 의 첫 줄(`Traceback …`)을 종료 오류로 바꿔 catch 로 보냈고, 그 결과 `slot.txt` 에는 `AVD step error: Traceback (most recent call last):` 한 줄만 남았다. avd rc 는 99(기본값)로 기록됐다.
  - 이것은 **r2 에서 slot 을 다시 쓸 때 생긴 진단 손실 결함**이다. stub 오케스트레이션 시험은 stderr 를 쓰지 않아서 이 경로를 검사하지 못했다.
- 예외의 위치는 미확정이다. `login()` 안에서 `tap('loginPasswordInput')` 이 step 을 남기지 못했다. 즉 `ka_avd.tap` 의 `not on screen` 이나 `uiautomator dump failed`, 또는 adb 오류일 가능성이 있다(**가능성**이며 원문은 없다).
  - 앱 로그(`avd/_cur.log`) 08:38:48Z: `sign_in_store … the directory refused the credential; it is erased`, `[auto_login] REJECTED`. 저장된 이전 로그인 정보(다른 fixture 계정)가 새 fixture 디렉터리에서 거부된 기록이다. 그 직후 로그인 화면의 상태가 원인일 수 있다(미확정).
- 결론: **이번 실행에서 E 시나리오의 PASS/FAIL 은 0** 이다. AVD EVIDENCE 가 아니다.

## 고정 (실행 직전 재확인, 계획과 같음)
- HEAD `9f9a91c852419a2d2848be73aab077d779b0550c`, tracked clean
- job `dc4c5049…`, pinned Host `d806e375…`, 하네스 `722bf981…`
- APK off `8c6422fe7104f8272242a9c51a64ac3d7a98989886a855f1d89cbee55f23828d`, on `64a021188c7b6a90492226b16f084deca4ccc78b8163f58cc3482b71cb3a8f37` (slot.txt 기록)
- 스크립트 9개는 r3 snapshot 과 불일치 0
- 에뮬레이터: adb 서버를 새로 띄운 직후 5 s 동안 offline 이었다가 `device`, `qemu=1`, `boot_completed=1` 이 됐다. 에뮬레이터는 10-26 부터 실행 중인 같은 인스턴스다.
- 슬롯: `sh_run.ps1` 이 `SLOT_REQUEST.txt` 를 CreateNew 로 잡았다. 실행 후 **자기 파일만 제거**(`slot removed=True`)했고, 지금 파일은 없다. **슬롯은 해제했다.**

## 실행된 것
- run 폴더: `feat-apk-0233\.claude\e2e\runs\ke-r22-20261029173738-38772`
- fixture 는 job 안에서 실행됐다: `job_run: child_exit=1 residual=0 forced=0 ended_confirmed=1`
- Host 3개는 모두 readiness 를 썼다(이 run id, 자기 harness pid, 정확한 host_log). 각 harness 의 결과:

| Host | 시작 전 gate | OFF 최종 판정 줄 | harness 결과 |
|---|---|---|---|
| a | PASS `the host said its input injection and its power management OFF before any client` | `PASS  input / power state: no contradiction during the run (1 launch(es): 1 judged on their complete output -- end, EOF and OFF lines confirmed, 4302 bytes --, 0 still running judged now)` | exit 1 |
| b | PASS (같음) | 같음, 4303 bytes | exit 1 |
| c | PASS (같음) | 같음, 4302 bytes | exit 1 |

- harness 가 exit 1 인 이유는 셋 모두 `FAIL  the host captured the window this test paints … the host never said what it captured` 하나뿐이다. AVD 가 연결·선택까지 가지 못해 Host 가 아무것도 capture 하지 않았기 때문이다. 이 FAIL 은 AVD 중단의 결과다.
- 정리: fixture 가 node 와 a/b/c 를 정상 종료했다(`fixture_result.txt`). 남은 GNLinkStream 은 사용자 제품(pid 27044)뿐이다.
  - 덧붙여, 10-28 에 내가 띄우고 남겨 둔 `tail -f` 모니터 18개(ke_r2–ke_r21 로그를 읽던 것)를 발견해 명령줄로 확인한 뒤 종료했다. 시험 프로세스가 아니다.

## 다음 (제안 — 실행하지 않음)
1. `ke_slot.ps1` 의 AVD 단계에서 native stderr 가 예외로 바뀌지 않게 한다(그 구간만 `ErrorActionPreference=Continue`, 또는 python stderr 를 별도 파일로 받기). traceback 원문이 남게 하고, stub 대조에 stderr 를 쓰는 AVD stand-in 을 추가한다. 스크립트 해시가 바뀌므로 SOURCE 재검토가 필요하다.
2. 그 뒤 ke-r23 으로 한 번 더 실행해 E 시나리오 결과를 얻는다. 로그인 화면에서 실패하면 그 원문으로 원인(저장 로그인 거부 뒤 화면 상태 등)을 확정한다.
- 미결: 실 clipboard (c), old/E7f·E8·E12 NOT RUN, ke_r21 peer_lost 원인, E7h·E13/E13b 실 행동.
