# keepalive-r2 e — AVD r5: ke-r22 INVALID 정리와 보수 (실행 0)

## ke-r22 기록 (그대로)
- **INVALID — 시나리오 0개.** bundle rc 3 (fixture 1, avd 99). run 폴더 `feat-apk-0233\.claude\e2e\runs\ke-r22-20261029173738-38772`. 상세는 `report_avd_ke_r22.md` (52f4e151…).
- Host 3개(a·b·c)의 OFF 판정은 **사실 그대로 PASS** 다.
  - 시작 전 gate `PASS the host said its input injection and its power management OFF before any client`
  - 최종 판정 `PASS input / power state: no contradiction during the run (… end, EOF and OFF lines confirmed, 4302/4303/4302 bytes …)`
- harness 의 exit 1 은 셋 다 `the host captured the window … never said` 때문이다. AVD 가 연결 전에 중단된 결과다.

## 고정 (이번 revision)
- tracked 제품·시험 코드 변경은 **없다**: feat-apk-0233 HEAD `9f9a91c` 그대로. artifact(job, pinned Host, 하네스, APK off/on)도 ke-r22 와 같다.
- 바뀐 것은 미추적 AVD 스크립트뿐이다. 불변 snapshot `.claude/caplife/avd_guard_r5_snapshot/` (읽기 전용 + `SHA256SUMS.txt`):

| 파일 | SHA-256 | r3 대비 |
|---|---|---|
| ke_slot.ps1 | 33f0c84b5a90f1770765d3495d3ed93b7fecaaa6a169b97a60fe6c3634834bbe | 변경 (1) |
| ke_avd.py | 79396f58e2ad4a7f1282ec92287673fc350bce509698cd8c9164845027a944d1 | 변경 (2) |
| ka_avd.py | e4c9fbf5dc5f751e0e8601d64c77937b33230fb3d9ff04f58d366f4e5dd9169d | 변경 (2) |
| ke_orchestration_test.ps1 | 7675f7737a7d6066a6ab72a2d7b5721e7572987ea0e21e96adcfc1f02090d719 | 대조 1개 추가 |
| stubs/stub_avd.py | aeb92415563c99246342c2e77b3f3031eb59e0b650346f86c2da7784703bd124 | stderr 모드 추가 |
| ke_fixture.ps1 · ka_delay_proxy.js · stub_harness.py · stub_node.py | (r3 와 같음) | 무변경 |

## (1) ke_slot: AVD 의 stderr 를 예외로 바꾸지 않음
- AVD 호출 구간만 `ErrorActionPreference=Continue` 로 감싼다(try/finally 로 원래 값 복원). stdout 은 `avd_stdout.txt`, **stderr 는 `avd_stderr.txt`** 에 그대로 받는다. exit code 는 AVD 의 것이다.
- `slot.txt` 에는 stderr 의 마지막 Traceback/Error 줄과 "in full: avd_stderr.txt" 를 남긴다.
- **대조**(stub, Host·adb 0). stand-in 이 ke-r22 처럼 stdout 에 run end 를 쓴 뒤 Python traceback 을 stderr 에 쓰고 exit 1 한다:
  - 보수 후: 오케스트레이션 **19/19, exit 0**, 잔여 0. 로그 sha256 `099bf1d0…`. 새 경우의 결과는 avd rc 1, `avd_stderr.txt` 에 traceback 전문(704 B), "AVD step error" 없음, bundle rc 1.
  - **보수 전**(r3 snapshot 의 ke_slot, 바깥 job 안): 그 경우만 **FAIL** — rc 99, stderr 0 바이트. ke-r22 를 그대로 재현했다. 나머지 18개는 PASS. 복원 후 해시 일치.

## (2) E1 로그인 중단 — 남은 자료로 좁힌 원인 (실행 없이)
남은 자료는 `avd/steps.txt`, `avd/_cur.log`(앱 로그), `avd/logcat.txt`, `result.json` 이다. 스크린샷은 없다.
- **시간 간격**: 17:39:03 `tapped loginIdInput … text='ketest'` 다음, 17:39:43 `run end`. 그 사이 40 s 동안 step 이 없다.
  - `login()` 다음 순서는 Ctrl+A → Del → `input text ketest` → `tap('loginPasswordInput')` 이다.
  - `tap` 은 `ka_avd.dump()` 를 부른다. dump 는 `uiautomator dump` 를 **최대 12회**(0.5 s 간격, 4·8회째에 남은 uiautomator 정리) 시도한 뒤 `RuntimeError('uiautomator dump failed')` 를 낸다.
  - 요소가 없을 때의 `not on screen` 은 dump 1회(수 초) 뒤에 나온다. **40 s 간격은 dump 12회 실패와 맞는다.** 가장 유력하다(원문이 없어 확정은 아니다).
- **에뮬레이터 상태 정황**:
  - 이번 adb 서버를 새로 띄운 직후 5 s 동안 `offline` 이었다.
  - install(17:37:56) 뒤 첫 `am start` 에서 앱이 앞에 오지 않아 재시작했다(17:38:39).
  - **logcat 캡처에 이번 실행의 줄이 하나도 없다**(마지막 줄은 에뮬레이터 시각 08:36:59, `logcat -c` 전의 버퍼). 에뮬레이터는 10-26 부터 계속 떠 있었다. 이 정황은 응답이 느렸거나 uiautomator·logcat 이 멈춘 상태와 맞는다(가능성).
- **저장 로그인 거부(`auto_login REJECTED`)**:
  - 앱이 저장된 로그인(이전 run 의 fixture 계정)으로 자동 로그인을 시도했고, 이번 fixture 디렉터리(새 비밀번호)가 거부해 지웠다(`sign_in_store … refused … erased`). 그 뒤 로그인 화면(`scene LOGIN`)이다.
  - 새 fixture 디렉터리에서는 **기대 동작**이다. 하네스는 이 경우를 정상 경로로 처리한다: `login()` 이 `loginIdInput` 을 기다리고, 미리 채워진 id 를 지운 뒤 새 계정으로 입력한다.
  - 다만 `start_app(clear=True)` 의 `pm clear` 가 성공했다면 저장 로그인이 남을 수 없다. 그래서 **`pm clear` 가 적용되지 않았을 가능성**이 있다. 그 결과는 지금까지 기록되지 않았다(`check=False`).
- 결론: 원인 후보는 ① 느린 에뮬레이터에서의 uiautomator dump 연속 실패(유력), ② `pm clear` 미적용(정황)이다. 둘 다 **확정하지 않는다**. 하네스 동작은 바꾸지 않고, 다음 실행에서 확정할 수 있게 **진단만** 보강했다:
  - `ke_avd.main`: 예외가 나면 traceback 전문을 `avd/avd_traceback.txt` 에 쓰고, `steps.txt` 에 마지막 줄을 남기고, `exception.png` 스크린샷을 찍은 뒤 다시 raise 한다(bundle 은 `avd_stderr.txt` 로도 받는다).
  - `start_app`: `pm clear` 의 응답(`Success` 등)을 step 에 기록한다.
  - `ka_avd.dump`: 12회 실패 시 마지막 uiautomator 응답(최대 160자)을 예외 문구에 넣는다(예: `could not get idle state`).
  - **비밀번호 비노출**: `ka_avd.adb` 의 오류 문구에서 `input text` 뒤의 인자를 `<redacted>` 로 바꾼다. 확인: `('shell','input','text','S3cr3t')` → `<redacted>`. traceback 의 소스 줄에는 변수 이름(`PW`)만 나온다.
  - 실 AVD 로는 검증하지 않았다. 구문(ast), redaction 함수, stand-in 대조만 확인했다.

## 다음
- Codex SOURCE 뒤 슬롯 차례에 **ke-r23 1회**. 계획은 `plan_avd_ke_r22.md` 와 같고, 스크립트 해시만 r5 snapshot 으로 바뀐다. 다시 dump 실패나 `pm clear` 실패가 나면 그 원문과 스크린샷으로 확정한다.
- 미결: 실 clipboard (c), old/E7f·E8·E12 NOT RUN, ke_r21 peer_lost, E7h·E13/E13b 실 행동.
