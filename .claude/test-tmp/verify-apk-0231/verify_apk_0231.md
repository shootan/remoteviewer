# verify apk-0231 — 검증용 독립 AVD smoke (remote_claude, 2026-10-22 14:46~14:52)

대상 보고 `.claude/report_rel_apk_0231_r1.md`. 제품 소스·하네스 수정 없음. 사용자 폰·설치 제품(GNLinkStream pid 9000 등)·서비스·실 세션 조작 없음.

## 고정 대상
- APK `C:/work/gnlink-wt/rel-apk-0231-out/GNLink-0.2.31.apk` SHA256 **230A65E99A2A7EE30CE0AF978E51EB0FA189F457E9A07DFDDB4DC69484A9D5D4** (실행 전·후 동일, 지시 해시와 일치). AVD 설치 뒤 `dumpsys package`: versionCode=30, versionName=0.2.31.
- 하네스: worktree `C:/work/gnlink-wt/rel-apk-0231` HEAD **2b57bb8863eb…** (읽기 전용 사용).
  - `automation/sh_filter_avd_relsmoke.py`(미추적) SHA256 `9DB08259834B8DE3367C6924732A0A5D3EBE829169E238D80AE2F92A6A3B902F`
  - `automation/sh_filter_avd.py`(추적, 수정 없음) `295CEAC58589634363409C6EEA035A61F2257A282B7212AC81D075E59AFC6196`
  - `automation/sh_pattern_window.ps1`(추적, 수정 없음) `D95D15C72269F315B429C8A8C9179A81C3F40ECE2CBC94E7FD86C31710C58FC7`
  - `PYTHONDONTWRITEBYTECODE=1` 로 실행(워크트리에 pycache 추가 쓰기 없음).
- 시험 Host: `integ-0162-build/.../GNLinkStreamMigrationTest.exe` 를 `hostbin/` 으로 복사, SHA256 **B4089DBEAE30F8FB666AAE4846AD7C9F55BD76E1BA73C652D4ECEE63603806F5** (worker 와 같음; 세 실행 out/host 사본도 같은 해시). 로그상 localVersion=0.2.161 문자열(시험 빌드 표기).
- 환경: qwinsta **console Active**(RDP 아님). AVD `emulator-5554` = `gnlink_kbd_test`(x86_64, SwiftShader), 08:14 부터 떠 있던 인스턴스 재사용(다른 하네스·시험 Host 프로세스 없음 확인, 종료·재부팅 안 함).
- 태그: gpu(SwiftShader) · display-capture(Host DXGI 데스크톱+패턴 창) · network(loopback).
- 슬롯: 14:46:04 CreateNew → 마지막 실행 뒤 14:51:50 내 파일 확인 후 삭제.
- 출력: 이 디렉터리(`smoke_monitors/`, `compare/`, `smoke_one/`, 각 `.log`/`.rc`).

## 결과 (각 1회 유효 실행)
| 실행 | rc | RESULT | 요지 |
|---|---|---|---|
| smoke_monitors (`GNLINK_STREAM_TEST_MONITORS_FILE`=monitors.txt "extra") | **0** | `ALL PASS (13 checks, 0 failed)` | 목록 `['Desktop', '모니터 1  1920x1080 • 주 화면', '모니터 2  1920x1080']`. Host `monitor-list seq=1 count=2`, 모니터 선택 `window-select … applied=1 reason=monitor_selected`. session1 live meanDiff=12.96, `path=sharpen gen=1 s=0.2`; 연결 해제 → 같은 프로세스 재접속 live 12.96, `path=sharpen gen=2`. 충돌·fallback 0. |
| compare (`sh_filter_avd.py --scenario compare`) | **0** | `ALL PASS (25 checks, 0 failed)` | 끄기→0.20→0.35→끄기→0.20, 모든 단계 x0=275, 테두리 정상. A_text 18.197<22.999<24.686, 끄기 복귀 18.185(≈18.197). progress 8줄 src==out, mismatched 0. |
| smoke_one (화면 1개 Host) | **1** | `FAILED (13 checks, 1 failed)` | 유일한 FAIL: `a host with one screen: the target list shows Desktop  ['Desktop', 'Desktop']`. 나머지 12개(접속·live 12.96·0.20 gen=1·연결 해제·재접속·live·0.20 gen=2·disconnect 로그·충돌/fallback 0) PASS. Host `monitor-list count=1`. rc 는 그대로 기록, 재실행·하네스 수정 없음. |

수치(13/0, 25/0, 12/1)와 FAIL 문구는 worker 보고와 같다.

### 무효 시도 1건 (환경, 제품 무관)
- 14:46:17 smoke_monitors 1차: rc 1. 첫 PASS 뒤 하네스의 `print(target list)` 가 `UnicodeEncodeError: 'cp949' … '\u2022'` 로 중단(stdout 을 파일로 돌릴 때 Python 기본 인코딩 cp949, '•' 출력 불가). 목록 판정 전에 끊겼으므로 **INVALID**(하네스 I/O, 검증용 실행 환경 문제). 산출물은 `smoke_monitors_attempt1_invalid_cp949*` 로 보존.
- `PYTHONIOENCODING=utf-8` 만 더해 다시 실행한 것이 위 표의 유효 결과다. 이후 두 실행도 같은 설정.

## smoke_one FAIL = 하네스 기대값 오류 주장 확인 (소스 근거, 정적)
- 하네스(`sh_filter_avd_relsmoke.py` look_list)는 dump 의 **모든 node text** 중 'Desktop' 또는 '모니터' 로 시작하는 것을 모은다. resource-id 로 카드만 고르지 않는다.
- 탭 버튼 `listDevicesButton` 의 text 는 `@string/target_desktop_button` = **"Desktop"**(values/strings.xml:12; values-ko 는 "바탕화면", AVD 는 영어 로케일).
- `MainActivity.kt` DESKTOP 탭(4441-4462): `monitors.size > 1` 이면 '모니터 N  WxH[ • 주 화면]' 카드들, 아니면 `desktop_mode_button` = **"Desktop"** 카드 1개(values/strings.xml:14).
- 대조: smoke_monitors 목록 첫 항목 'Desktop' 은 모니터가 2개라 카드일 수 없다(그 분기에서는 Desktop 카드가 생기지 않음) → 탭 버튼 text 다. 따라서 smoke_one 의 `['Desktop','Desktop']` = 탭 1 + 카드 1 이고, Host `monitor-list count=1` 과 제품 규칙(화면 1개면 Desktop 하나)에 맞는다.
- 한계: 하네스가 dump XML 을 저장하지 않아 이 실행의 resource-id 를 직접 보지 못했다(실행 중 별도 dump 는 하네스 dump 와 충돌할 수 있어 하지 않음). 판정은 소스 + smoke_monitors 대조에 의한 것이며, **worker 주장과 일치**한다. 기록상 rc 1 은 유지한다.

## 관찰 (판정 무관)
- live meanDiff 가 네 번 모두 12.96 (worker 와 같음). 패턴이 시간 결정적이라는 worker 설명과 맞는 모양이나 확인하지 않음.
- smoke_monitors Host 14:47:14.493 `capture-pipeline-flushed reason=stream-inactive` (연결 직후) — 기존 ledger 의 수동 IP ~10 s control 정지와 같은 계열로 보이며, 하네스 11.5 s 대기 우회 그대로. 판정 영향 없음.
- 모든 로그 첫 줄 cp949 PowerShell 경고 1줄(패턴 창) — 기존과 같음.
- rel 워크트리에 `.claude/rel/android-0.2.31/`(manifest·payload APK, 14:45:24 생성)이 새로 보였다. 내 실행 시작(14:46) 전 다른 세션의 게시 준비물이며 검증용이 만들지 않았다.

## 미검증
- 실 폰(SM-S948N): 화질·지연·발열, Codec2 crop, 실 LAN/relay 재연결, 회전 뒤 터치.
- 하네스 dump 원문(resource-id) 직접 열람.
- 서명 manifest·게시·설치는 범위 밖.
