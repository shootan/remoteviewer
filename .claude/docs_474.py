import io

p = 'docs/history.md'
d = io.open(p, encoding='utf-8').read()
entry = '''
### 474) 2026-09-09 ⚠️ **테스트가 사용자 화면에 오류창을 띄웠다** — 발사한 뒤 정리하면 늦는다
- **무슨 일**: 사용자 화면에 `...\\Temp\\gnlink-scn-1052\\install\\ScnClient.exe 를 찾을 수 없습니다` 대화상자가 떴다. `%TEMP%` 경로이므로 **`3edd5cd` 이전 실행**이 낸 셸 요청이, **디렉터리가 정리된 뒤에** 탐색기에서 뒤늦게 실행된 것이다.
- **`GetFileAttributes` 선확인은 방어가 아니다.** 확인 시점엔 파일이 있었고, 정리가 지웠고, 셸은 **나중에** 실행했다. 확인과 실행 사이가 곧 삭제가 일어난 구간이다 — **TOCTOU**. 주석이 "이걸로 모달을 막는다" 고 적고 있었고, 그 문장부터 고쳤다. **막는 것은 선확인이 아니라 "요청이 도착할 때 파일이 아직 거기 있는 것"** 이고, 그건 **누가 언제 지워도 되는가**의 문제다.
- **fixture 를 자기 종결형으로 교체**(`scn_dummy_main.cpp` 신설, `remote60_scn_dummy`): 실행되면 **자기 이름과 pid 를 `witness.txt` 에 적고 즉시 종료**하는 진짜 PE. 예전 fixture 는 **명령 해석기 사본**이라 ⓐ 절대 종료하지 않아 매 시나리오가 하나씩 남겼고(그래서 **이름으로 죽이는 쓸어담기**가 필요했다), ⓑ **셸 요청이 실제로 도착했는지 알 방법이 없었다.**
- **수명을 launch 종결까지 보존**: 셸로 발사한 시나리오는 **그 시나리오 안에서** witness 줄을 기다린다(다음 `seed()` 가 witness 를 지우므로, 정리 시점에 묻는 것은 **다른 실행에 대해 묻는 것**이다 — 첫 구현이 그래서 안 지워도 될 디렉터리를 남겼다). 끝내 안 오면 **디렉터리를 지우지 않고** 사유를 찍는다. **저장소 안 쓰레기는 싼 값이고, 남의 화면에 뜬 대화상자는 그것과 바꿀 것이 아니다.**
- **`update_relaunch_test` 도 같은 모양이었다**: `%TEMP%` 에 fixture 를 만들고 셸로 발사한 뒤 트리를 지웠다. root 를 저장소 안(`.claude/relaunch-runs`)으로 옮기고, **클라이언트 witness 를 기다린 뒤에만** 지운다.
- **거절 사유를 분리**: 첫 구현이 "보류 중" 과 "root 밖" 을 같은 `else` 로 묶어, **root 안에 있는 디렉터리를 두고 "root 밖이라 못 지운다"** 고 찍었다. **틀린 이유를 대는 메시지는 없느니만 못하다** — 읽는 사람을 엉뚱한 데로 보낸다.
- **생산 경로 검토**(지시 5): 설치 디렉터리는 삭제되지 않고, **선택 이미지는 commit 뒤에만 기동**되므로(#473) 뒤이어 롤백이 파일을 바꿔치지 않는다. 늦게 도착한 요청은 **제자리에 있는 새 클라이언트**를 띄운다. 근거를 `launch_via_shell` 주석에 적었다.
- **하지 않은 것**: `%TEMP%` 삭제·광역 이름 kill·대화상자 닫기 **전부 안 했다.** `%TEMP%\\gnlink-exec-*` 3건은 **목록만** 남긴다(모두 실제 `.cmd` 를 담고 있어 늦은 요청이 와도 대화상자가 아니라 dummy 가 뜬다). `.claude/scenario-runs/scn-27308` 1건은 **위 규칙이 스스로 남긴 것**이라 그대로 둔다.
- **검증**(콘솔, 활성 RDP 없음): update 12종 전부 PASS — check 28 / effects 199 / handoff 33 / http 36 / job_guard 19 / manifest 82 / **relaunch 107** / release 80 / state_machine 115 / assembly 40 / options 48 / **scenarios 115**. 실행 후 **잔존 프로세스 0**, 새 `%TEMP%` 디렉터리 0건.
- 변경 파일: `apps/native_poc/src/scn_dummy_main.cpp`(신설) · `updater_scenarios_test.cpp` · `update_relaunch_test.cpp` · `update_relaunch.cpp`(주석) · `apps/native_poc/CMakeLists.txt` · `docs/history.md` · `docs/full_code_audit_2026-09-08.md`.
'''
assert '### 474)' not in d
d = d.rstrip('\n') + '\n' + entry
io.open(p, 'w', encoding='utf-8', newline='').write(d)

p = 'docs/full_code_audit_2026-09-08.md'
d = io.open(p, encoding='utf-8').read()
anchor = '| I22 |'
start = d.find(anchor)
assert start != -1, 'I22'
end = d.find('\n', start) + 1
rows = (
 '| I23 | **P0, 테스트 하네스 해소됨** (2026-09-09, history #474) | 시나리오 스위트가 셸로 launch 를 발사한 뒤 **디렉터리를 지웠고**, 탐색기가 뒤늦게 실행하면서 **사용자 화면에 "찾을 수 없습니다" 대화상자**가 떴다. `GetFileAttributes` 선확인은 **TOCTOU 라 방어가 아니다** — 확인·삭제·실행이 이 순서로 일어난다 | 자기 종결 fixture(`remote60_scn_dummy`, witness 기록 후 종료) + **발사한 시나리오 안에서 witness 대기** + 끝내 안 오면 **디렉터리 보존**. `update_relaunch_test` 도 같은 처치 |\n'
 '| I24 | P2, 테스트 하네스 해소됨 (2026-09-09, history #474) | 정리 거절 메시지가 "보류 중" 과 "root 밖" 을 같은 `else` 로 묶어, **root 안 디렉터리를 두고 "root 밖이라 못 지운다"** 고 찍었다 | 사유 분리. **틀린 이유를 대는 메시지는 없느니만 못하다** |\n'
)
d = d[:end] + rows + d[end:]
io.open(d and p, 'w', encoding='utf-8', newline='').write(d)
print('history 474, ledger I23-I24')
