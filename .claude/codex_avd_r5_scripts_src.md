# AVD r5 진단 스크립트 — SOURCE

2026-10-29 · **NEEDS_CHANGES 1건: adb 오류 본문의 민감값 처리.** stderr/종료 코드 보존과 나머지 진단 보수 방향은 수용한다. 아래 한정 보수 확인 뒤 동일 계획의 ke-r23 1회로 진행한다. 실제 AVD/adb는 실행하지 않았다.

## 고정과 수용 범위

- tracked source **9f9a91c**, artifact·APK는 기존 계획과 같은 범위다. 이번 검토는 제품 변경이 없는 **avd_guard_r5_snapshot**의 script 변경이다.
- report_avd_r5.md SHA-256 **e654d51260fec8d8708bd1296d664eeca5731c1777193259e058d6b9a8f99949**, report_avd_ke_r22.md **52f4e151708731038807e616b5853cc30dba0eedfd13cfaae495edbea38c46a1** 일치.
- r5 manifest의 **9파일 전체 hash를 직접 대조**했다. ke_slot 33f0c84b…·ke_avd 79396f58…·ka_avd e4c9fbf5…·orchestration 7675f773…·stub_avd aeb92415…와 나머지 r3 동일 파일이 맞다.

ke_slot은 AVD 호출 구간만 Continue로 낮추고 finally에서 원래 값을 복원한다. stdout/stderr 분리와 실제 LASTEXITCODE 기록, 바깥 fixture finally/job/최종 rc 결합은 유지한다. 이 방향을 수용한다.

고정 snapshot의 **실제 호출 block을 추출**해, 같은 string[] AvdCmd 형태로 자기 Python stub에 적용했다. Host·fixture·AVD·adb 없이 직접 확인한 결과:

```
stderr traceback: rc=1, launchCaught=False, preference=Stop
stdout=True, stderr=True, traceStart=True, traceEnd=True
missing executable: rc=99, launchCaught=True, preference=Stop
```

따라서 stderr를 정상적인 native 실패 자료로 보존하면서 종료 코드 1을 받는 수정은 재현됐다. 실행 파일 자체를 시작하지 못한 경우는 outer catch의 비성공으로 남는다. `.claude/codex_ka_e_review/avd_r5_script/native_error_probe.ps1`과 자기 새 출력 폴더들을 보존했다. 작업용의 전체 orchestration 19/19·이전 script 반례는 보고 근거이며 Codex가 전체 bundle을 다시 실행한 것은 아니다.

PowerShell의 native stderr 표현에는 ErrorRecord 형식이 붙을 수 있다. traceback 내용을 보존한 것과 byte 원본 그대로인 것을 구분한다. 대조의 `$stderrText.Length`는 문자열 길이이므로 보고의 “704 B”는 실제 파일 byte 길이와 구분해 적는다. 이 문구 정정은 별도 실행 관문이 아니다.

## N1 — 인자만 가리고 오류 본문은 그대로 전달함

ka_avd._shown은 input text 인자를 가리지만, adb의 RuntimeError는 **r.stderr.decode 원문을 그대로 덧붙인다.** native 오류가 입력값을 되풀이하면 그 값이 예외 본문에 남고, 새 ke_avd 진단이 그것을 avd_traceback.txt·steps·상위 stderr에 다시 기록할 수 있다. “비밀번호 비노출”은 `_shown` 단독 확인으로 성립하지 않는다.

실제 adb를 호출하지 않고 subprocess.run을 대역 처리했다. dummy 입력값을 stderr가 되풀이하도록 넣고 고정 ka_avd.adb를 부른 결과:

```
args_placeholder_present: true
stderr_still_contains_sentinel: true
```

즉 command 인자는 가려졌지만 **같은 RuntimeError 문자열의 오류 본문에는 dummy 값이 남았다.** 실제 fixture 비밀번호나 사용자 비밀번호를 읽거나 출력한 시험은 아니다. 자료는 `.claude/codex_ka_e_review/avd_r5_script/redaction_probe.json`이다.

필요한 보수:

- 민감 input text 값은 표시할 command뿐 아니라 **오류 본문에서도 제거**한 뒤 RuntimeError를 만든다. 원래 실행하는 argv는 바꾸지 않는다.
- traceback/예외 요약을 추가로 저장하는 경로에서도 같은 비노출 조건을 유지한다. 알려진 fixture credential을 그대로 보관하는 raw error/traceback 경로를 만들지 않는다.
- fake adb가 stderr에 입력값을 반복하는 대조로 **RuntimeError → format_exc → 저장할 traceback/step** 어디에도 그 값이 없는지 확인한다. 실제 adb는 필요 없다. non-secret offline/실패 코드 등 진단 정보가 보존되는 양성 대조도 둔다.

새 OS 동작이나 전체 테스트 재설계를 요구하는 보수는 아니다. 이 좁은 수정·대조와 새 script hash를 기존 task의 한 packet에 연결하면 된다. ke_slot/Host/job 안전 경계를 다시 작성하거나 이미 수용한 일반 8·record/fatal을 전량 반복할 필요는 없다.

## ke-r22와 다음 run의 해석

ke-r22는 **INVALID, 기능 시나리오 0**으로 유지한다. Host 3개의 OFF/출력 최종 판정과 job residual 0은 안전 실행·정리의 관측으로 보존하지만, E1 로그인 또는 KA 기능 성공으로 바꾸지 않는다.

40초 공백은 dump 재시도와 맞는 정황일 뿐 유일한 원인 증명은 아니다. auto_login REJECTED가 새 fixture에서 가능한 동작이라는 것과, pm clear가 실제 성공했는지도 별개다. pm clear의 stdout을 기록하는 보수는 유용하지만 현재 wrapper는 check=False에서 rc/stderr를 돌려주지 않으므로 빈 응답이면 **성공/실패 미확정**으로 남긴다. 필요하면 같은 한 번의 호출의 rc·정제된 stderr도 기록하되 원인 확인을 위해 command를 중복 실행하지 않는다.

N1 보수 후 **같은 고정 source/artifact·계획·slot 조건의 ke-r23 1회**로 이어간다. 실제 emulator 상태 변경·재시작/추가 입력·clipboard 시험을 이번 진단 변경에 넣지 않는다. E7f/E8/E12·old-host NOT RUN, clipboard (c), ke_r21 peer_lost의 미확정·E7h/E13/E13b 실제 행동 등 기존 상태도 유지한다. 한 번 통과한 사실만으로 간헐 peer_lost를 닫지 않는다.

이번 Codex는 source/hash 읽기와 자기 stub/오류 문자열 대역만 실행했다. 실제 Host/Viewer/AVD/adb·기기 조작·제품 변경·설치·운영·삭제·commit/push는 없다. 사용자 현재 조치 없음.
