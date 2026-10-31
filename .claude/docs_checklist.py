import io

p = 'docs/수동확인_체크리스트.md'
d = io.open(p, encoding='utf-8').read()

# ------------------------------------------------------------------ UPD-FIELD-01: two routes now
old = '| `09e6aee` (`update_relaunch.cpp` `launch_via_shell`), 설계 3.5·3.11, history #448 | [ ] |'
new = ('| `a95e0df` (`update_relaunch.cpp` `launch_as_shell_user` → 실패 시 `launch_via_shell`), 설계 3.5·3.11·**3.11.1**, '
       'history #448·**#471** | [ ] |')
assert old in d, 'field-01 ref'
d = d.replace(old, new, 1)

old = '''| UPD-FIELD-01 | P0 | **셸 경유 비승격 재실행**'''
new = '''| UPD-FIELD-01 | P0 | **비승격 재실행 (기본은 사용자 토큰, 셸은 폴백)**'''
assert old in d, 'field-01 title'
d = d.replace(old, new, 1)

old = '''ⓒ 는 지원 대상(그 사용자가 그 세션에 로그인해 있고 셸이 살아 있음 → **떠야 함**)과 불가능 조건(로그아웃·셸 없음 → 안전한 실패)을 **나눠서** 판정 |'''
new = '''ⓒ 는 지원 대상(그 사용자가 그 세션에 로그인해 있고 셸이 살아 있음 → **떠야 함**)과 불가능 조건(로그아웃·셸 없음 → 안전한 실패)을 **나눠서** 판정. **추가(2026-09-09)**: 무결성 수준은 두 경로가 같으므로 **어느 경로로 떴는지는 로그로 판정한다** — `relaunch GNLinkClient.exe: started in the user context`(토큰 경로, 소유 성립) vs `... started by the shell, which returns no handle`(폴백, **소유 없음**). 폴백으로 떴다면 **그 시도의 롤백은 불가능**하므로, 그 상태에서 일부러 실패를 만들어 결과가 **`RollbackFailed`** 로 보고되는지도 함께 본다. 상승 권한 업데이터에서 **폴백이 상시로 쓰인다면 그 자체가 결함**이다(`SeImpersonate` 가 있어야 정상) |'''
assert old in d, 'field-01 criteria'
d = d.replace(old, new, 1)

# ------------------------------------------------------------------ the client backup row, again
old_start = '| **클라이언트 백업(`.gnlink-old`)의 수명** |'
i = d.find(old_start)
assert i != -1, 'backup row'
end = d.find('\n', i) + 1
new_row = ('| **클라이언트 백업(`.gnlink-old`)의 수명** | **재현되지 않는다 (2026-09-09, history #471).** '
           '문제의 형태(셸 경유 클라이언트 + commit 하는 경로)를 **시나리오 8** 로 격리 재현했고 **고아가 생기지 않는다**. '
           '지우지 못한 백업은 이름·오류코드와 함께 기록되고, **Restart Manager(`RmGetList`) 로 홀더를 이름으로** 찍는다(읽기 전용 진단). '
           '지울 수 없으면 `.gnlink-old.N` 으로 옮겨 다음 업데이트를 막지 않되, **rename 도 막히는 경우가 있다** — '
           '공유 없이 열린 핸들. **미상으로 남은 것**: 스냅샷 상관관계 제거와 그것이 붙들던 handle 해제 중 **무엇이 없앴는지 특정 못 함**. '
           '다시 나타나면 홀더 이름이 함께 나온다 | 04·06 |\n')
d = d[:i] + new_row + d[end:]
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('checklist updated: two routes, and the backup row says what is now known')
