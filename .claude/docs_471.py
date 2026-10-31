import io

p = 'docs/history.md'
d = io.open(p, encoding='utf-8').read()
entry = '''
### 471) 2026-09-09 안전 반려 4건 — **스위트가 저장소 밖을 지우고 이름으로 죽이고 있었다**, 그리고 셸 소유권은 증명이 아니었다
- ⚠️ **테스트가 저장소 root 밖을 삭제했다**(`updater_scenarios_test.cpp`, `CMakeLists.txt` / commit 3edd5cd):
  - `%TEMP%` 에 트리를 만들고 시작할 때마다 `gnlink-scn-*` 를 **열거해 `remove_tree`** 했다. **AGENTS.md 최상위 규칙 위반**이고, **테스트 실행 승인은 저장소 밖 파일 삭제 승인이 아니다.** 패턴이 마침 자기 산출물에만 맞았다는 건 근거가 아니다 — **자기 것이 아닌 디렉터리에 대해 그 판단을 내릴 자격이 없다**는 게 요점이다.
  - root 를 **빌드에서 주입**(`GNLINK_SCN_ROOT = ${CMAKE_SOURCE_DIR}/.claude/scenario-runs`). 런타임 추측 없음. 정리는 **자기 run 디렉터리 하나만**, `inside_run_root()` 통과할 때만. 남의 잔재는 **목록만** 찍는다.
- ⚠️ **테스트가 이름으로 남의 프로세스를 죽일 수 있었다**(같은 commit):
  - `running_under()` 가 경로를 못 읽으면 **`mine = true`** 로 두고 이름 prefix 만으로 포함한 뒤 Terminate 했다. **식별 못 한 것을 죽이는** 구조였다. 게다가 시작 시 **`stop_everything_under(L"")`** — 빈 scope 는 좁은 범위가 아니라 **범위의 부재**다. **같은 스위트의 두 번째 실행을 첫 번째가 죽였을 것이다.**
  - 이제 **경로를 반드시 읽고 scope 밑임이 확인될 때만** 후보. 못 읽으면 **보고만 하고 건드리지 않는다**(제품의 `Liveness::Unknown` 과 같은 규칙).
  - 반례 3종: **같은 이름 다른 디렉터리 생존** · **동시 run 모사 생존** · **`%TEMP%` 는 삭제 가능 범위 밖**.
  - **직전 라운드에서 `mine = true` 로 뒤집은 것이 나였다.** "못 여는 것이야말로 놓치던 대상" 이라는 관찰은 맞았지만, 거기서 **기본값을 안전한 쪽이 아니라 스위트가 초록이 되는 쪽**으로 돌렸다. 초록은 목적이 아니라 관측이다.
- **셸 소유권을 폐기하고 진짜 handle 을 받는다**(`update_relaunch.{hpp,cpp}`):
  - 실행 전/후 스냅샷 차분이 1개라는 것은 **자기 실행의 증거가 아니다.** 셸 handoff 가 늦는 동안 **사용자가 같은 프로그램을 켜면 차분도 1**이고, 그 위에서 연 handle 은 **남의 신원을 고정**한다. 스냅샷 비교(`pids_running_image`)는 **삭제**했다.
  - `RelaunchConfig::launchInUserContext` — 셸 창 토큰을 복제해 **`CreateProcessWithTokenW`** 로 실행하고 **`pi.hProcess` 를 받는다**. 관리자 프로세스의 `SeImpersonate` 로 충분하고, **업데이터가 상승 권한이라 이 일을 맡을 자리**다. 비면 실제 구현(`isStillRunning` 과 같은 형태 — 빈 seam 이 "건너뛰기" 가 되지 않게).
  - 실패하면 셸로 떨어지되 **소유를 주장하지 않는다**: `RelaunchOutcome::ownershipUnknown`, `unstoppable` 로 보고 → **강제 종료 없음, 롤백 진입 없음.**
  - **시나리오 7 신설**: 셸 경유 + 새 빌드 불건강 → **`RollbackFailed`**, 로그에 "no proof of ownership", 파일은 swap 이 남긴 그대로, 백업도 그대로(재시도 가능). **역대조**: 같은 상황에서 소유 가능한 경로면 정지되고 롤백이 실제로 돈다.
  - ⚠️ **남는 위험(판단 요청)**: 데스크톱이 있는데 토큰 경로만 실패하면 **셸로 뜬 클라이언트 때문에 롤백이 영구히 막힌다.** 대안은 (a) 소유 못 하는 optional 은 commit 이후에만 시작 (b) 현행 유지. 조용히 정하지 않고 올린다.
- **백업 홀더 — 추측을 그만두고 물었다**(`updater_scenarios_test.cpp`):
  - **Restart Manager(`RmStartSession`/`RmRegisterResources`/`RmGetList`) 를 읽기 전용으로** 붙였다. 고아 백업이 나오면 **누가 잡고 있는지 이름으로** 찍는다. 추측은 세 번 연속 틀렸다(실행 중 이미지 / 잔여 image section / 열린 프로세스 handle — 전부 검증하고 기각). **묻는 편이 셋 다보다 쌌다.**
  - **시나리오 8 신설**: 셸 경유 클라이언트 + commit 하는 경로 — 문제의 그 형태를 격리에서 재현. **현재는 고아가 발생하지 않는다**(백업이 깨끗이 삭제됨). 스냅샷 상관관계와 그것이 붙들던 handle 이 사라진 뒤로 재현되지 않는데, **둘 중 무엇이 없앴는지는 특정하지 못했다.**
- **`.gnlink-old.N` 은 `Commit` 이 아니라 `Swap()` 안이다**(검증용 정정 반영). 증거를 붙였다(`update_effects_test`):
  - **다음 시도 성공**: 지울 수 없는 백업(**실행 중 이미지**)이 이름을 차지해도 다음 업데이트가 **막히지 않는다**. 옮겨진 것은 `.1` 로 남고, 기록되고, **롤백은 원본 바이트를 정확히 복원**한다.
  - **상한**: `kMaxStaleBackups = 50`. 초과하면 swap 이 **멈추고 원인을 말한다** — "could not move aside" 만 남기면 운영자가 **엉뚱한 파일**을 본다. 원인 문자열이 증상과 함께 실린다.
  - ⚠️ **rename 이 만능이 아니다**: 공유 없이 열린 핸들이 잡은 파일은 **삭제도 rename 도 안 된다.** 이 경우 swap 은 실패하고 **아무것도 절반만 바뀌지 않는다.** rename 이 구제하는 건 **실행 중 이미지**뿐이라는 걸 단정으로 박았다.
- **"cleanup 이 죽여서 없어진 것" 과 production 복구를 분리**했다: 시나리오 단정이 읽는 값은 전부 **자기 정리 이전 시점**에 캡처된다. 캡처 지점에 그 이유를 적었다.
- **검증**(콘솔, 활성 RDP 없음): update 12종 전부 PASS — check 28 / effects **198** / handoff 33 / http 36 / job_guard 19 / manifest 82 / relaunch 107 / release 80 / state_machine 92 / assembly 40 / options 48 / scenarios **100**. 나머지 C++ 바이너리 전부 exit 0. 스위트 실행 후 **잔존 프로세스 0 · `.claude/scenario-runs/` 0개 · `%TEMP%\\gnlink-scn-*` 0개**.
- 변경 파일: `apps/native_poc/src/update_relaunch.{hpp,cpp}` · `update_effects.cpp` · `update_effects_test.cpp` · `updater_scenarios_test.cpp` · `apps/native_poc/CMakeLists.txt` · `docs/history.md` · `docs/full_code_audit_2026-09-08.md`.
- 라이브·설치·배포·버전 인상·릴리스 보류 그대로. 산출물 없음.
'''
assert '### 471)' not in d
d = d.rstrip('\n') + '\n' + entry
io.open(p, 'w', encoding='utf-8', newline='').write(d)

# ---------------------------------------------------------------- ledger
p = 'docs/full_code_audit_2026-09-08.md'
d = io.open(p, encoding='utf-8').read()
anchor = '| I15 |'
start = d.find(anchor)
assert start != -1, 'I15'
end = d.find('\n', start) + 1
rows = (
 '| I16 | **P0, 테스트 하네스 해소됨** (2026-09-09, history #471) | 시나리오 스위트가 `%TEMP%` 의 `gnlink-scn-*` 를 **열거해 `remove_tree`** 했다 — **저장소 root 밖 삭제**, 최상위 규칙 위반. 테스트 실행 승인에 포함되지 않는 행위다 | root 를 빌드에서 주입(`${CMAKE_SOURCE_DIR}/.claude/scenario-runs`), **자기 run 디렉터리만** 가드 통과 시 삭제, 남의 잔재는 목록만 |\n'
 '| I17 | **P0, 테스트 하네스 해소됨** (2026-09-09, history #471) | 쓸어담기가 경로를 못 읽으면 **`mine = true`** 로 두고 **이름만으로 Terminate** 했고, 시작 시 **빈 scope** 로 호출됐다. **같은 스위트의 동시 실행을 죽였을 것**이고 PID 재사용 위험도 그대로 | 경로 확인 필수, 미확인은 **보고만**. 반례 3종(같은 이름 다른 경로 / 동시 run / `%TEMP%` 제외) |\n'
 '| I18 | P1, 업데이트 해소됨 (2026-09-09, history #471) | 셸 경유 실행의 **스냅샷 차분 1개** 를 소유 근거로 썼다. 사용자가 같은 프로그램을 그 순간 켜면 차분도 1이고, **그 위에서 연 handle 은 남의 신원을 고정**한다 — 롤백이 그걸 죽인다 | 스냅샷 비교 삭제. **`CreateProcessWithTokenW`** 로 실제 handle 확보, 실패 시 **소유 주장 안 함**(`ownershipUnknown`) → 강제 종료·롤백 진입 없음. 시나리오 7 + 역대조 |\n'
 '| I19 | P2, 업데이트 (2026-09-09, history #471) | `.gnlink-old` 를 옮겨 이름을 비우는 것이 **만능이 아니다**: 공유 없이 열린 핸들이 잡은 파일은 **삭제도 rename 도 안 된다**. rename 이 구제하는 건 **실행 중 이미지**뿐 | 상한 50 + 초과 시 **원인을 증상과 함께** 보고. 세 경우(실행 중 이미지 / 상한 초과 / 배타 핸들)를 각각 단정으로 고정 |\n'
)
d = d[:end] + rows + d[end:]
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('history 471 and ledger I16-I19')
