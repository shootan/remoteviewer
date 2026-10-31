import io

p = 'docs/수동확인_체크리스트.md'
d = io.open(p, encoding='utf-8').read()

old = ('**추가(2026-09-09)**: 무결성 수준은 두 경로가 같으므로 **어느 경로로 떴는지는 로그로 판정한다** — '
       '`relaunch GNLinkClient.exe: started in the user context`(토큰 경로, 소유 성립) vs '
       '`... started by the shell, which returns no handle`(폴백, **소유 없음**). '
       '폴백으로 떴다면 **그 시도의 롤백은 불가능**하므로, 그 상태에서 일부러 실패를 만들어 결과가 '
       '**`RollbackFailed`** 로 보고되는지도 함께 본다. '
       '상승 권한 업데이터에서 **폴백이 상시로 쓰인다면 그 자체가 결함**이다(`SeImpersonate` 가 있어야 정상)')
new = ('**추가(2026-09-09)**: 무결성 수준은 두 경로가 같으므로 **어느 경로로 떴는지는 로그로 판정한다** — '
       '`relaunch (optional) GNLinkClient.exe: started in the user context`(토큰 경로, 소유 성립) vs '
       '`relaunch (optional) GNLinkClient.exe: started by the shell, which returns no handle`(폴백, **소유 없음**). '
       '상승 권한 업데이터에서 **폴백이 상시로 쓰인다면 그 자체가 결함**이다(`SeImpersonate` 가 있어야 정상). '
       '⚠️ **정정(2026-09-09, #473)**: 여기 한때 "폴백으로 떴으면 일부러 실패를 만들어 `RollbackFailed` 가 나오는지 보라" 고 적혀 있었다. '
       '**이제 일어나지 않는 결과다** — 선택 이미지는 commit 뒤에만 뜨므로 롤백이 가능한 구간에 소유 불가 프로세스가 존재하지 않는다. '
       '**시키는 대로 해도 답이 안 나오는 지시**여서 지운다. 그 성질은 실기가 아니라 `updater_scenarios_test` 시나리오 7 이 단정한다')
assert old in d, 'field-01 rollback instruction'
d = d.replace(old, new, 1)

old = ('| `a95e0df` (`update_relaunch.cpp` `launch_as_shell_user` → 실패 시 `launch_via_shell`), '
       '설계 3.5·3.11·**3.11.1**, history #448·**#471** | [ ] |')
new = ('| `a95e0df` (`update_relaunch.cpp` `launch_as_shell_user` → 실패 시 `launch_via_shell`), '
       '설계 3.5·3.11·**3.11.0·3.11.1**, history #448·**#471·#473** | [ ] |')
assert old in d, 'field-01 refs'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('UPD-FIELD-01 no longer asks for an outcome that cannot occur')
