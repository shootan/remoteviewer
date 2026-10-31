# AVD r6 스크립트 — SOURCE

2026-10-29 · **SOURCE OK. r5 N1을 닫는다.** 기존 슬롯 큐에 따라 같은 계획의 **ke-r23 1회**를 실행할 수 있다. 실제 AVD 결과·EVIDENCE나 ke-r22 원인 확정은 아니다.

## 고정

- 제품/시험 tracked HEAD **9f9a91c852419a2d2848be73aab077d779b0550c**, tracked 변경 없음 확인. 기존 artifact·APK 계획을 유지한다.
- report_avd_r6.md SHA-256 **31e8319f95d15bb827ecf981c94ba2c8db2660e6e67896180bd67b7f36035b4e** 일치.
- avd_guard_r6_snapshot/SHA256SUMS.txt의 **10파일 hash 전부 일치**를 직접 확인했다. 변경된 ka_avd/ke_avd와 새 redaction 시험을 읽고 Python AST 검사를 통과했다.

| 핵심 파일 | SHA-256 |
|---|---|
| ka_avd.py | f228c00a03208c0294cf9c41da2804fc793edc5eac3c2b74b9f6766b55ad588e |
| ke_avd.py | ed8205663231ae928e15ddbb23d4ea699580ddc30b64c23f62bfbbc7f99b1d8c |
| ka_avd_redact_test.py | ae7fe0833a4019a11a84501eebd7ec9615d7fbd98906063bf617b72f550660c0 |
| ke_slot.ps1 | 33f0c84b5a90f1770765d3495d3ed93b7fecaaa6a169b97a60fe6c3634834bbe |

## N1 closure와 직접 대조

input text 호출의 표시 argv뿐 아니라 **그 호출의 값이 반복된 오류 본문도 정제**한다. RuntimeError가 정제된 문자열을 가지므로, 해당 오류에서 생성하는 format_exc와 저장 traceback·steps로 원래 값이 다시 전달되던 r5 반례가 닫혔다. 실제 subprocess에 넘기는 argv는 원래 값으로 유지한다.

별도 run helper는 같은 한 번의 adb 호출에서 rc와 정제된 stdout/stderr를 반환한다. pm clear는 이 helper를 한 번 호출한 결과를 기록한다. 기존 clear·login 동작을 반복하거나 새 재시도 정책을 넣은 변경은 아니다. rc는 명령의 보고 상태이고, 그것만으로 앱 내부 데이터가 실제 초기화됐거나 ke-r22의 원인이 확정됐다고 하지는 않는다.

고정 **ka_avd_redact_test.py를 직접 실행해 7 checks/0 failed, rc 0**을 확인했다. subprocess.run 대역과 dummy sentinel만 사용하며 adb/기기 호출은 0이다.

- native 호출 argv의 실제 값 유지.
- stderr가 값을 되풀이해도 RuntimeError/traceback에 sentinel 없음.
- 실패 코드·device busy/offline 등 비밀 아닌 진단 보존.
- run helper의 stdout/stderr 정제와 pm clear 빈 응답/실패 rc 구분.
- 민감값 없는 호출의 오류 내용 유지.

로그는 `.claude/codex_ka_e_review/avd_r6_script/redact_test.log`, 확인한 manifest는 같은 폴더 snapshot_hashes.json에 보존했다. Python -B로 snapshot bytecode 쓰기를 하지 않았다. 이 결과는 검수한 input text 오류·진단 경로에 대한 한정 검증이며, 임의 앱 화면/모든 외부 문자열의 전역 비노출 검증으로 확대하지 않는다.

## 유지되는 실행 계약

ke_slot은 r5와 같은 hash다. 이전 직접 대조의 stderr traceback 시작/끝 보존·rc 1·Stop 복원과 실행 파일 시작 실패의 비성공 경로를 그대로 유지한다. fixture/guard/job 및 Source OK된 안전 조건도 바뀌지 않았다. 전체 orchestration 19/19는 작업용의 보고 근거로 유지하며 Codex가 다시 실행한 것은 아니다.

검증용은 실행 사본과 r6 snapshot 10파일을 대조하고, 기존 고정 artifact/APK·계획·slot 조건으로 ke-r23을 연결하면 된다. 같은 범위의 별도 go나 일반 8/record/fatal 재실행은 필요 없다. 기존 선행 예약/실행과 SOURCE OK 순서를 존중한다.

ke-r22는 **기능 시나리오 0·INVALID**, Host 3개의 OFF 최종 판정과 residual=0/ended_confirmed=1만 해당 관측으로 보존한다. uiautomator dump/pm clear 두 가설은 미확정이다. String.Length 704 문자와 실제 UTF-16 파일 1410 bytes를 구분하는 정정도 수용한다.

old-host/E7f/E8/E12 NOT RUN, clipboard (c), ke_r21 peer_lost·E7h/E13/E13b 등 기존 미결은 유지한다. 새 run의 PASS/FAIL/INVALID·traceback·화면·pm clear 결과를 각각 원문으로 남기며, 한 번 통과했다고 간헐 문제를 닫지 않는다.

이번 Codex는 source/hash/AST 읽기와 대역 redaction 시험만 수행했다. 실제 Host/Viewer/AVD/adb·제품 변경·기기 조작·설치·운영·삭제·commit/push는 없다. 사용자 현재 조치 없음.
