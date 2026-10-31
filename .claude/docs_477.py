import io

p = 'docs/history.md'
d = io.open(p, encoding='utf-8').read()
entry = '''
### 477) 2026-09-09 ⚠️ **handoff 가 끊어져 있었다** — Host 는 bootstrap 을 기다리고 있었다
- **결함**: Host 가 `WaitForMultipleObjects({readyEvent, process})` 의 `process` 로 **bootstrap** 핸들을 넘겼다. bootstrap 은 **자기가 실행 중인 파일이 교체 대상**이라 copy 를 띄우고 **즉시 종료**한다 — 그것이 정상 경로다. Host 는 그 종료를 **"업데이터가 죽었다"** 로 읽고 `readyEvent` 를 닫은 뒤 **"설치된 버전은 그대로이며 계속 사용할 수 있습니다"** 를 띄웠다. 그 사이 copy 는 멀쩡히 다운로드 중이었고, `signalReady` **반환값을 안 보고** 진행해 **방금 아무 일도 없을 거라고 말한 그 Host 를 종료**했다.
  → 원격 사용자에게는 **"업데이트 안 합니다" 를 듣고 그 기계를 잃는** 형태다. **이번 세션에서 사용자가 겪는 결과가 가장 나쁜 결함이다.**
- **부품은 전부 옳았다.** bootstrap 의 즉시 종료도, worker 의 늦은 signal 도, Host 의 대기도 각각 맞다. **셋을 붙였을 때만** 나타난다 — 어느 2프로세스 테스트로도 볼 수 없었다.
- **고친 것**
  - `HandoffStep{KeepWaiting, ExitNow, KeepRunning}` — **없던 세 번째 답이 결함이었다.** bootstrap 종료 + **exit code 0** 은 *계속 기다림*, **0 아님** 은 *지금 중단*. 종료 코드를 **읽는다**(못 읽으면 실패로 본다 — 알 수 없는 종료가 의도된 인계로 오인되면 안 된다).
  - **ack 이벤트 신설**(`make_ack_event_name`, ready 이름에서 파생 — 한 시도에 하나의 stem, 두 이벤트라 서로 다른 시도의 채널로 답할 수 없다). Host 는 **ExitNow 일 때만** ack 한다.
  - `signalReady` 가 **bool 을 돌려준다**(열리지 않으면 아무도 안 기다리는 것이다). `awaitAck` 신설. **둘은 함께 주입되어야 한다** — 하나만 있는 것이 정확히 이 결함의 모양이라 `validate()` 가 막는다.
  - `may_stop_the_product(delivered, acked)` 가 **PrepareForSwap 에서** 판정한다. 실패하면 **디스크를 하나도 건드리지 않은 채 abandon**.
  - Host 의 대기 루프를 **`await_handoff` 로 분리**했다. lambda 안에 있어서 **제품을 실제로 업데이트하지 않으면 돌릴 수 없었고, 그래서 한 번도 안 돌았고, 그래서 틀린 채로 있었다.**
- **회귀 — 실제 3프로세스**(`update_handoff_process_test`, 신설 fixture `remote60_handoff_fixture`): bootstrap·worker 를 **진짜 프로세스**로 띄우고, Host 쪽은 **제품이 부르는 그 `await_handoff`** 를 부른다(재현이 아니라 그 함수여야 한다 — 재현본을 시험한 것이 원래 결함이 남은 이유다).
  ① **즉시 bootstrap exit + 늦은 worker** → ExitNow, worker 진행 ② **bootstrap 실패(exit 3)** → 즉시 중단, 전체 timeout 을 앉아서 기다리지 않음 ③ **worker 무신호** → KeepRunning, worker 스스로 물러남 ④ **timeout 뒤 늦은 worker** → ack 없음 → **worker 가 제품을 종료하지 않음** ⑤ **종료 경합**: timeout 을 worker 지연 **양쪽으로 훑어** 두 결과가 실제로 다 나오는지까지 단정(한쪽만 나오면 레이스를 시험한 게 아니다).
- ⚠️ **반대 증거**: 옛 규칙(어떤 종료든 실패)으로 되돌리면 **정상 업데이트 케이스 ①이 `KeepRunning` 으로 실패**하고, ⑤의 훑기에서 **ExitNow 가 한 번도 나오지 않는다** — 즉 **그 코드로는 업데이트가 성립하지 않는다.** 단위 테스트도 2건 실패.
- **조합 회귀도 추가**(`updater_scenarios_test` 시나리오 10): **전달됐지만 응답 없음** → abandon, 디스크 무변경, 사유가 "never answered" / **전달 자체 실패** → 사유가 "could not be delivered" / **역대조**: 정상 응답이면 진행.
- **표현 정정**(지시): "생산 fallback 은 파일이 항상 존재한다" 를 **이번 시도 수명 안으로** 좁혔다. 이후의 다른 업데이트·제거는 그 파일을 바꾸거나 없앨 수 있다. **닫을 수 없는 창이며, 보장인 척하지 않는 편이 낫다.**
- **검증**(콘솔, 활성 RDP 없음): update 13종 전부 PASS — check 28 / effects 199 / **handoff 44** / **handoff_process 18** / http 36 / job_guard 19 / manifest 82 / relaunch 107 / release 80 / state_machine 115 / assembly 41 / options 48 / **scenarios 142**. 나머지 C++ 바이너리 전부 exit 0. 잔존 프로세스 0, `.claude/handoff-runs` 0(자기 회수).
- 변경 파일: `update_handoff.{hpp,cpp}` · `update_handoff_wait.cpp`(신설) · `handoff_fixture_main.cpp`(신설) · `update_handoff_process_test.cpp`(신설) · `update_handoff_test.cpp` · `host_app_main.cpp` · `updater_effects.{hpp,cpp}` · `updater_assembly_test.cpp` · `updater_scenarios_test.cpp` · `update_relaunch.cpp`(주석) · `CMakeLists.txt` · 문서 3종.
'''
assert '### 477)' not in d
d = d.rstrip('\n') + '\n' + entry
io.open(p, 'w', encoding='utf-8', newline='').write(d)

p = 'docs/full_code_audit_2026-09-08.md'
d = io.open(p, encoding='utf-8').read()
i = d.find('| I26 |')
assert i != -1
end = d.find('\n', i) + 1
rows = (
 '| I27 | **P0, 업데이트 해소됨** (2026-09-09, history #477) | Host 가 handoff 대기에서 **bootstrap 핸들**을 지켜봤다. bootstrap 은 자기 실행 파일이 교체 대상이라 **즉시 종료하는 것이 정상**인데, Host 는 그것을 죽음으로 읽고 **"설치된 버전 그대로"** 를 알린 뒤 ready 를 닫았다. copy 는 `signalReady` **반환값을 안 보고** 진행해 **그 Host 를 종료**했다 — 사용자는 취소를 듣고 기계를 잃는다 | `HandoffStep` 에 **KeepWaiting** 추가 + **exit code 판독**, **ack 이벤트**로 양방향 handshake, `signalReady` 가 bool, `awaitAck` 신설(**둘은 함께**), `PrepareForSwap` 에서 판정. **실제 3프로세스 회귀** 5종 + 반대 증거 |\n'
 '| I28 | P1, 업데이트 해소됨 (2026-09-09, history #477) | Host 의 대기 루프가 **lambda 안에** 있어 **제품을 실제로 업데이트하지 않으면 돌릴 수 없었다.** 그래서 한 번도 안 돌았고, 그래서 틀린 채로 있었다 | `await_handoff` 로 분리. 테스트가 **그 함수를** 부른다 — 재현본을 시험한 것이 결함이 남은 이유였다 |\n'
)
d = d[:end] + rows + d[end:]
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('history 477, ledger I27-I28')
