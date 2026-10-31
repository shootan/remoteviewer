# keepalive-r2 e — AVD r6 (Codex codex_avd_r5_scripts_src.md N1 + 작은 항목 3), 실행 0

## 고정
- tracked 변경 없음: feat-apk-0233 HEAD `9f9a91c`. artifact 와 APK 는 ke-r22 와 같다.
- 스크립트 불변 snapshot: `.claude/caplife/avd_guard_r6_snapshot/` (10파일, 읽기 전용 + `SHA256SUMS.txt`)

| 파일 | SHA-256 | r5 대비 |
|---|---|---|
| ka_avd.py | f228c00a03208c0294cf9c41da2804fc793edc5eac3c2b74b9f6766b55ad588e | N1 정제, `run()` 추가 |
| ke_avd.py | ed8205663231ae928e15ddbb23d4ea699580ddc30b64c23f62bfbbc7f99b1d8c | pm clear 기록 |
| ka_avd_redact_test.py (새) | ae7fe0833a4019a11a84501eebd7ec9615d7fbd98906063bf617b72f550660c0 | 대조 |
| ke_slot.ps1 | 33f0c84b5a90f1770765d3495d3ed93b7fecaaa6a169b97a60fe6c3634834bbe | 같음 |
| ke_fixture.ps1 · ke_orchestration_test.ps1 · ka_delay_proxy.js · stubs 3 | r5 와 같음 | — |

## N1 — 오류 본문에서도 비밀 제거
- `ka_avd._secrets(args)`: `input text` 뒤의 값들을 모은다.
- `_clean(text, args)`: 그 값이 텍스트에 나오면 모두 `<redacted>` 로 바꾼다.
- `adb()` 의 RuntimeError 는 `adb <argv 가림> (exit N): <정제된 stderr>` 형식이다. 그래서 `format_exc`, `avd_traceback.txt`, steps 의 EXCEPTION 줄 어디에도 그 값이 남지 않는다.
- **실행 argv 는 그대로**다(실제 값으로 호출한다).
- 대조 `ka_avd_redact_test.py`:
  - subprocess.run 대역을 쓴다(adb·기기 0). dummy sentinel 을 쓰고, 실제 비밀번호는 쓰지 않는다.
  - 대역은 실패하는 adb 처럼 stderr 에 입력값을 되풀이한다.
  - 보수 후(r6) **7/7, exit 0**:
    - argv 에는 실제 값이 유지된다.
    - 오류와 traceback 에 sentinel 이 0 이다.
    - 비밀이 아닌 진단(`Failure calling service input`, `device busy`, `exit 1`)은 남는다.
    - `run()` 은 stdout 과 stderr 를 모두 정제한다.
    - 비밀이 없는 호출(`tap`)의 오류는 전문 그대로다.
  - **보수 전**(r5 snapshot 의 ka_avd.py) **5 중 2 FAIL, exit 1**: traceback 에 sentinel 이 그대로 남는다(Codex 재현과 같다). 그리고 `run()` 이 없다.

## 작은 항목
- **pm clear**: 같은 한 번의 호출에서 `ka_avd.run()` 으로 rc, stdout, 정제된 stderr 를 함께 기록한다: `pm clear: rc=<n> stdout=… stderr=…`. 중복 실행은 없다. 빈 응답도 rc 로 성공과 실패를 구분한다(대조: rc=0 빈 응답, rc=1 SecurityException 전문).
- **보고 문구 정정**: r5 의 "traceback 704B" 는 PowerShell `String.Length` 로 잰 **704 문자**다. 같은 파일(`orch-stderr-20261029174514-34528\avd_stderr.txt`)의 크기는 **1410 바이트**다(PowerShell 5.1 의 `2>` 리다이렉트가 UTF-16 으로 쓴다).
- **ke-r22**:
  - 기능 결과 0, **INVALID** 다.
  - 유효한 관측은 **Host 3개의 OFF 판정 PASS** 와 **job `residual=0 ended_confirmed=1`** 뿐이다.
  - E1 중단 원인 두 가설(① uiautomator dump 연속 실패 ② pm clear 미적용)은 **미확정으로 유지**한다.

## 다음
- 확인되면 같은 계획으로 ke-r23 을 1회 돌린다. 스크립트 해시는 r6 snapshot 이고, artifact 와 APK 는 그대로다.
