# test-injection-finding — 출하 gate self-test 의 %TEMP% 사용 (Codex codex_avd_guard_src_r2.md T)

## 고정
- ka-r3 **8a84198157837370a47d4c87f782eb44c755844b** (부모 d665957).
  - 2파일: `automation/gnlink_check_fixed_server.py`, 새 `automation/gnlink_check_fixed_server_root_test.py`.
  - commit 메시지 본문의 anchor 경로 한 줄이 셸 escape 로 깨졌다(`D:emoteemote...`). 메시지 텍스트만의 문제이고, 코드의 경로는 `D:\remote\remote\.claude\test-tmp` 로 정확하다.
- feat 병합: feat-apk-0233 **3a3c77f** (merge 8a84198, 충돌 0).

## ① 보수
- `--self-test` 는 더 이상 `tempfile` 을 쓰지 않는다(import 없음, ast 로 확인).
- root 규칙:
  - `REMOTE60_TEST_TMP` 는 `D:\remote\remote\.claude\test-tmp` **바로 아래**, 일반 ASCII 이름이어야 한다.
  - 드라이브부터 그 폴더까지 link(reparse)가 없어야 한다.
  - 그 밖이면 **아무것도 만들기 전에** 거부한다(FAIL, 생성 0). 사용자 TEMP 로 fallback 하지 않는다.
- 유효한 root 에서는 자기 **새** `gate-selftest-<pid>-<ms>-<n>` 폴더를 만든다(기존 폴더는 채택하지 않는다).
- cleanup:
  - 그 run 폴더 안만 걷는다. 파일과 폴더만 지우고, link 는 따라가지도 지우지도 않는다(남기고 출력한다).
  - 결과는 self-test 의 check 하나다("left nothing").
- 정상 `--dir` / `--apk` 스캔은 그대로다(읽기만 한다).
- `gnlink_check_fixed_server_root_test.py` **15/15, exit 0**. ka-r3 와 feat 병합본 각각에서 실행했다.
  - 거부, 각각 생성 0:
    - unset
    - anchor 밖
    - anchor 자체
    - 두 단계 아래
    - 공백 이름
    - %TEMP% 경로
    - 일반 파일
    - 경로상 link (판정 함수에 link 를 묻는 방식. 실제 junction 은 만들지 않았다)
  - 유효 root: 새 run 폴더 → cleanup 은 그 폴더만 지우고 옆 파일은 보존한다. 내부 link 는 남기고 출력한다.
  - 실제 self-test: 자기 root 에서 53/53, run 폴더 제거, 잔여 0.
  - root 없이 self-test: FAIL, 생성 0.
  - 시험 뒤 anchor 는 시작 때와 같다.
- 보수 전 대조: d665957 의 gate 는 `tempfile` 을 import 한다. **정적 판정만** 했다. 보수 전 self-test 는 재실행하지 않았다.
- %TEMP% 에 대해서는 **제한된 조회만** 했다: 실행 전후 `gnlink-fixed-server-gate-*` 패턴의 개수만 세었고, 결과는 0 → 0 이었다. 그 밖의 탐색·정리·복구는 0 이다. 이 조회를 "%TEMP% 를 전혀 읽지 않았다" 로 쓰지 않는다. 또 현재 값으로 과거의 무영향을 판정하지 않는다.

## ② 과거 기록 (확인된 것 / 미확인)
- **이 세션(36e9662b, 작업용)에서 self-test 를 부른 도구 호출: 8건.** 이 문서 작성 중에 확인했다. 앞선 보고의 "1회" 는 57fa793 검증분 하나만 센 것이므로 **정정한다**.
  - 이 8 은 **호출 수**다. 한 호출 안에서 self-test 가 실제로 몇 번 돌았는지, 각 실행의 check 수, 만든 폴더의 경로, 삭제 결과는 **미확인**이다.
  - 2026-10-26 08:17Z / 10:52Z / 12:33Z / 16:35Z
  - 2026-10-28 00:08Z / 00:41Z / 01:18Z / 04:27Z (마지막이 57fa793 검증분)
  - 모두 `TemporaryDirectory` 판이라 %TEMP% 에 폴더를 만들고 지우는 실행이었다(소스 기준).
  - 실제 폴더 경로·파일 목록·개별 삭제 결과는 **미기록**이다. 당시 TMP 값은 참고일 뿐 증거가 아니다.
- 프로젝트 전체 Claude 기록(`C:\Users\shota\.claude\projects\D--remote-remote\*.jsonl`)을 읽기만 해서 집계했다. 명령줄에 `python … gnlink_check_fixed_server.py … --self-test` 가 든 Bash/PowerShell 호출(Write/Edit 제외)은 **43건, 8개 세션**이고, 기간은 2026-09-29 ~ 2026-10-28 이다. 이것도 **도구 호출 수**다. 실제 self-test 실행 횟수·check 수·경로·삭제 결과와 같은 값이 아니다.
  - 세션별: a019f5e8 15 · 36e9662b 8 · a23358d2 7 · 3a9098b5 6 · 72cee16d 2 · ac7da4d1 2 · e8706c29 2 · 0840bc58 1
  - 한 호출에 self-test 가 몇 번 돌았는지, 같은 실행이 중복으로 잡혔는지, 각 호출의 check 수(52/57 등)는 **확정하지 않는다**. 결과 문자열에 다른 시험의 PASS 개수가 섞여 있다.
  - 집계 스크립트 `.claude/caplife/selftest_history.py` (sha256 `32e424cf…`), 결과 `selftest_history.txt` (sha256 `7f8bc41a…`).
- 사용자 TEMP 의 재탐색·삭제·복구·재현은 **0** 이다. 지금 TEMP 에 남은 것이 없다는 사실로 과거 삭제의 성공이나 무영향을 판정하지 않는다.
- 과거 self-test 의 기능 assertion 결과(PASS 수)는 그 한계와 함께 보존한다. **경계 준수 증거로는 쓰지 않는다.** 정상 read-only scan 과 서명·게시 사실은 이 사안으로 무효가 되지 않는다.

## r2 추가 — T1: cleanup 이 junction 에 들어가지 않음 (Codex codex_avd_guard_src_r3.md)
- ka-r3 **af49c26ff8fff967003433d07126f58384f93cc3** (부모 8a84198). feat 병합 **9f9a91c** (충돌 0).
- 순회 방식: top-down 으로 걷되, 각 하위 폴더는 **내려가기 전에** 판정한다. link(symlink·junction)는 순회에서 제외(prune)하고, 그 자리에 남기고, 출력한다.
- 삭제 순서: 남은 파일을 지운 뒤, 폴더를 깊은 순서부터 지운다. cleanup root 자체가 일반 폴더가 아니면 아무것도 건드리지 않는다.
- 대조(root_test **20/20**, ka-r3 와 feat 병합본 각각):
  - 실제 directory junction 을 이 시험 소유의 sibling 폴더(sentinel 있음)로 만들었다.
  - **보수 전**(git 의 8a84198 cleanup) 을 삭제 기록 대역으로 돌렸다(실제 삭제 0). junction **아래의 sentinel 을 선택했다** = 보수 전 FAIL 재현.
  - **보수 후** 를 같은 기록 대역으로 돌렸다. junction 아래 선택 0, run 자신의 파일만 선택했다.
  - 실제 cleanup: sentinel 과 junction 이 남고, junction 을 출력했고, run 을 깨끗하다고 하지 않았다(비성공).
  - 정리: junction 은 **link 자체만** 제거했다(`os.rmdir(junction)`, 재귀 삭제 없음). sentinel 은 무변경이었다.
- 이 root_test 안에서 보수된(T1) self-test 가 자기 root 에서 53/53, 잔여 0 으로 실행됐다. 보수 전 self-test 는 실행하지 않았다.
- **commit 메시지 정정**: 8a84198 메시지의 anchor 경로 한 줄(`D:emoteemote...`)은 셸 escape 로 깨진 것이다. 뜻한 경로는 `D:\remote\remote\.claude\test-tmp` 다. af49c26 메시지에 정정을 기록했고, amend 는 하지 않았다.
