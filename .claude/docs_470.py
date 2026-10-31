import io

BS = chr(92)

# ---------------------------------------------------------------- history 470
p = 'docs/history.md'
d = io.open(p, encoding='utf-8').read()
entry = '''
### 470) 2026-09-09 Codex 반려 4건 반영 — **"주입은 테스트만 채우고 있었다" 를 구조로 막고, 쓸어담기가 눈이 멀어 있었다**
- **① 동일 identity 생존 검사를 생산에 연결**(`update_relaunch.*`, `update_process_identity.cpp` 신설):
  - `RelaunchConfig::isStillRunning` 이 비어 있으면 **"전부 나갔다" 로 가정**했다. 생산은 이걸 한 번도 채우지 않았으므로 **출시본에서 이 검사는 아예 꺼져 있었고**, 그 검사를 덮던 테스트는 **아무도 안 돌리는 구성**을 덮고 있었다.
  - 답을 **셋**으로 바꿨다(`Running / Exited / Unknown`). **조회 실패는 "종료됨" 이 아니다** — 나갔다고 넘겨짚으면 살아 있는 것을 또 띄워 두 개가 되고, 있다고 넘겨짚으면 내려간 기계를 성공으로 보고한다. `Unknown` 은 실패로 올려 필수 이미지면 롤백으로 간다.
  - 비어 있으면 **실제 검사(`real_liveness`)** 를 쓴다. `ACCESS_DENIED` 는 `Unknown` 이지 `Exited` 가 아니다.
  - identity 비교(`process_identity_matches`)를 **`update_process_identity.cpp` 로 분리**했다. relaunch 테스트가 **진짜 비교를 링크**한다 — 링크하기 싫어서 검사를 안 배선했던 것이 원래 순서였다.
- **① 일반화: 주입 지점 전수 점검을 구조로 바꿨다**(`update_effects.*`, `updater_effects.*`):
  - `UpdateEffectsConfig::unwired()` 가 **비어 있는 seam 이름을 열거**하고, `UpdaterEffects::run()` 은 **하나라도 비면 아무것도 하지 않고 그 이름들을 로그에 적고 중단**한다. manifest 를 안 가져오던 것도, 검증한 버전이 등록에 못 닿던 것도 **전부 이 형태**였다.
  - 감사 결과: `UpdateEffectsConfig` 의 std::function 10개 전부 생산이 채운다. `UpdaterDeps` 는 `validate()` 가 이미 강제. `RelaunchConfig` 에서 생산이 안 채우던 유일한 하나가 `isStillRunning` 이었고 위에서 해소했다.
- **② 정지 대상은 "받은 handle" 로만**(`update_relaunch.*`, `update_effects.*`):
  - `CreateProcessW` 가 준 handle 을 **닫지 않고 보유**한다. handle 이 살아 있는 동안 pid 는 재사용되지 않으므로 **handle 이 곧 신원**이다. 이름으로 pid 를 맞추는 것은 신원이 아니다.
  - **셸 경유는 launch 별 상관관계가 성립할 때만** 소유를 주장한다: 실행 직전 스냅샷과 직후 스냅샷을 비교해 **정확히 하나**가 나타났을 때만 handle 을 열어 보유하고, 그 handle 로 image 경로를 **다시** 확인한다. 0개거나 2개 이상이면 소유하지 않는다 — 이름으로 죽이면 남의 창을 죽인다.
  - `stopStarted` 가 `StopReport{stopped, unstoppable}` 를 돌려주고, **`unstoppable` 이 비지 않으면 파일 롤백에 들어가지 않는다.** 잡고 있을지 모르는 파일을 되돌리면 절반만 복구된 설치가 남고, 실패 원인은 롤백 탓으로 보인다.
  - 보유한 handle 은 시도가 끝날 때 전부 닫는다. **이미 종료된 프로세스라도 handle 이 열려 있으면 그 이미지 파일이 잠긴 채로 남는다.**
- **③ optional 실패여도 health 가 먼저**(`update_state_machine.cpp`):
  - optional 이미지 하나가 안 돌아왔다는 이유로 **health 를 묻기도 전에 commit** 하고 백업을 지웠다. `required started` 는 `CreateProcess 가 돌아왔다` 는 뜻이지 **호스트가 떴다** 는 뜻이 아니다.
  - 순서를 바꿨다: **health → (건강하면) optional 부분 commit, 건강하지 않으면 롤백.**
  - **회귀 추가**: "Host launched-but-unhealthy + Client launch fails" → `RolledBack`, `commitCount == 0`, health 가 결정보다 먼저. **역대조**: 같은 client 실패 + 건강한 host → 여전히 `UpdatedButNotRelaunched`(무조건 롤백이 아님을 증명).
  - **반대 증거**: 순서를 예전으로 되돌리면 이 스위트에서 **7건이 실패**하고, 호출 흔적이 `... Relaunch, Commit ...` 로 **HealthCheck 가 아예 없다.**
  - 예전 순서를 지키던 단정 `"relaunch fails -> health is not claimed"` 은 **결함을 보호하던 테스트**였다. 참이었고, 그게 버그였다.
- **④ `Commit()` 이 `DeleteFileW` 결과를 읽는다**(`update_effects.*`, `updater_effects.*`):
  - 지우지 못한 백업을 **이름과 오류 코드까지** 기록하고(`orphaned_backups()`), 로그에 남긴다. 예전엔 결과를 버리고 목록을 비워서, **살아남은 `.gnlink-old` 는 어디에도 흔적이 없었다.**
  - **격리에서 재현했고 실기로 미루지 않았다**: `update_effects_test` 에 백업을 열어 잡은 채 commit 하는 경우(고아 1건 기록) 와 **역대조**(정상 commit 은 고아 0건, 백업도 실제로 사라짐)를 넣었다.
  - 실제 조합에서 남는 클라이언트 백업의 **오류 코드는 5(ACCESS_DENIED), 속성 32(ARCHIVE)** 였다. 1초 재시도로는 안 풀렸고 재시도는 **얻는 것 없이 commit 만 느리게** 만들어 걷어냈다.
  - **더 나쁜 2차 피해를 막았다**: 지워지지 않는 백업이 이름을 차지하면 **다음 업데이트의 move-aside 가 막혀 설치 자체가 불가능**해진다. 이제 지울 수 없으면 `<name>.gnlink-old.N` 으로 **옮겨서 이름을 비우고** 그 사실을 기록한다. **실행 중인 파일도 rename 은 된다** — swap 이 처음부터 기대고 있는 그 비대칭이다.
- ⚠️ **시나리오 스위트의 쓸어담기가 눈이 멀어 있었다**(`updater_scenarios_test.cpp`):
  - `running_under()` 가 **image 경로를 읽으려고 `OpenProcess` 를 먼저 했고, 열 수 없는 프로세스는 건너뛰었다.** 건너뛴 것이 **정확히 이 함수가 멈추려던 프로세스들**이었다. 스냅샷에는 **이름으로 계속 보이고 있었다.**
  - 결과: 매 실행마다 `ScnClient.exe` 넷이 살아남아 자기 이미지를 잡고 있었고, **다음 시나리오는 시작 바이트를 쓰지 못한 채** 바이트에 대한 단정을 실패했다 — 화면에는 프로세스가 하나도 안 보이는 채로. 3·4·5·6 과 control 이 이 이유로 죽었다.
  - 고쳤다: **스냅샷 이름으로 식별**하고, 경로는 **확인되면** 디렉터리로 좁히되 **확인 못 하면 포함**한다. 못 여는 프로세스야말로 놓치던 대상이므로, "경로를 못 읽었다" 는 봐줄 이유가 아니다.
  - 시나리오 6 은 같은 이름의 stub 을 install 밖에서 돌리므로 이 범위 규칙으로 그대로 보존된다.
  - `seed()` 도 **쓰고 나서 읽어서 확인**한다. 덮어쓰기 실패를 조용히 넘겨서, 시나리오가 앞 시나리오의 바이트로 시작한 뒤 바이트 단정을 실패하고 있었다.
- **검증**(콘솔 세션, `qwinsta` 상 활성 RDP 없음):
  - update 계열 12종 **전부 PASS**: check_28 / effects_180 / handoff_33 / http_36 / job_guard_19 / manifest_82 / relaunch_107 / release_80 / state_machine_92 / assembly_40 / options_48 / scenarios_65.
  - 나머지 C++ 테스트 바이너리 전부 exit 0. 예외 2건은 **이번 변경 파일을 하나도 링크하지 않는** 환경 실패다: `remote60_gdi_capture_process_test`(부하 중 `GDI_DELIVERED_FPS=3.67`, copy avg 260ms), `remote60_udp_control_e2e_test`(`udp hello ack failed`). **판정 근거로 쓰지 않는다.**
  - JS/Kotlin 은 이번에 바뀐 것이 없어 재실행하지 않았다.
- 변경 파일: `apps/native_poc/src/update_state_machine.cpp` · `update_relaunch.{hpp,cpp}` · `update_effects.{hpp,cpp}` · `update_process_identity.cpp`(신설) · `updater_effects.{hpp,cpp}` · `update_state_machine_test.cpp` · `update_effects_test.cpp` · `update_relaunch_test.cpp` · `updater_scenarios_test.cpp` · `apps/native_poc/CMakeLists.txt` · `docs/history.md` · `docs/full_code_audit_2026-09-08.md` · `docs/수동확인_체크리스트.md`.
- 라이브·설치·배포·버전 인상·릴리스는 **보류 그대로**. 산출물 생성 없음.
'''
assert '### 470)' not in d
d = d.rstrip('\n') + '\n' + entry
io.open(p, 'w', encoding='utf-8', newline='').write(d)

# ---------------------------------------------------------------- ledger I12-I15
p = 'docs/full_code_audit_2026-09-08.md'
d = io.open(p, encoding='utf-8').read()
anchor = '| I11 |'
start = d.find(anchor)
assert start != -1, 'I11'
end = d.find('\n', start) + 1
rows = (
 '| I12 | P1, 업데이트 해소됨 (2026-09-09, history #470) | `RelaunchConfig::isStillRunning` 이 비면 **"전부 나갔다"** 로 가정했고 **생산은 이 자리를 한 번도 채우지 않았다**. 출시본에서 생존 검사는 꺼져 있었고, 그것을 덮던 테스트는 **아무도 안 돌리는 구성**을 덮고 있었다 | 답을 셋(`Running/Exited/Unknown`)으로 바꾸고 비면 실제 검사를 쓴다. 일반화로 `UpdateEffectsConfig::unwired()` + `run()` 의 거부를 넣어 **비어 있는 seam 으로는 아무것도 실행되지 않는다** |\n'
 '| I13 | P1, 업데이트 해소됨 (2026-09-09, history #470) | `Commit()` 이 `DeleteFileW` 결과를 안 보고 `movedAside_` 를 비웠다. 지워지지 않은 백업은 **어디에도 흔적이 없었고**, 그 이름을 차지한 채 **다음 업데이트의 move-aside 를 막아 설치 자체를 불가능하게** 만든다 | 결과를 읽어 이름+오류코드로 기록. 지울 수 없으면 `<name>.gnlink-old.N` 으로 옮겨 이름을 비운다(**실행 중인 파일도 rename 은 된다**). 격리 재현 + 역대조 포함 |\n'
 '| I14 | P1, 업데이트 해소됨 (2026-09-09, history #470) | 롤백이 **자기가 띄운 프로세스를 못 멈춘 상태에서도** 파일을 되돌렸다. 절반만 복구된 설치가 남고, 실패 원인은 롤백 탓으로 읽힌다 | `stopStarted` 가 `unstoppable` 을 보고하고, 비지 않으면 **파일에 손대기 전에 거부**한다. 셸 경유는 launch 별 1:1 상관관계가 설 때만 소유를 주장한다 |\n'
 '| I15 | P2, 테스트 하네스 해소됨 (2026-09-09, history #470) | 시나리오 쓸어담기가 image 경로를 읽으려 `OpenProcess` 를 먼저 했고 **열 수 없는 프로세스를 건너뛰었다** — 건너뛴 것이 **정확히 멈추려던 프로세스들**이었다. 스냅샷에는 이름으로 계속 보였다. 매 실행 넷이 살아남아 다음 시나리오가 시작 바이트조차 쓰지 못했고, **프로세스는 하나도 안 보이는 채로** 바이트 단정이 실패했다 | 스냅샷 이름으로 식별하고, 경로는 **확인되면** 좁히되 **확인 못 하면 포함**한다. 못 여는 것이야말로 놓치던 대상이다 |\n'
)
d = d[:end] + rows + d[end:]
io.open(p, 'w', encoding='utf-8', newline='').write(d)

# ---------------------------------------------------------------- checklist row resolved
p = 'docs/수동확인_체크리스트.md'
d = io.open(p, encoding='utf-8').read()
old = '| **클라이언트 백업(`.gnlink-old`)의 수명** | 실제 조합에서 살아남는 경우가 있고 **원인 미상**이다.'
i = d.find(old)
assert i != -1, 'client backup row'
end = d.find('\n', i) + 1
new = ('| **클라이언트 백업(`.gnlink-old`)의 수명** | **미상 아님 (2026-09-09, history #470).** commit 이 지우지 못한 백업을 '
       '**이름과 오류 코드까지 기록**한다(실측 error 5 / attrs 32). 지울 수 없으면 `.gnlink-old.N` 으로 옮겨 '
       '**다음 업데이트를 막지 않는다.** 격리에서 재현되고 역대조가 있다. **남은 미상은 "누가 잡고 있는가" 하나뿐**이며, '
       '그 파일은 프로세스가 끝나는 즉시 풀린다 | 04·06 |\n')
d = d[:i] + new + d[end:]
io.open(p, 'w', encoding='utf-8', newline='').write(d)

print('history 470, ledger I12-I15, checklist row updated')
