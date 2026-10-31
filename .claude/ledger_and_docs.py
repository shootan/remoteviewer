import io

BS = chr(92)

# ---------------------------------------------------------------- ledger: three new items
p = 'docs/full_code_audit_2026-09-08.md'
d = io.open(p, encoding='utf-8').read()
anchor = '| I01 |'
start = d.find(anchor)
assert start != -1, 'I01'
end = d.find('\n', start) + 1
rows = (
 '| I02 | P1, 업데이트 해소됨 (2026-09-09, history #468) | `updater_effects.cpp` 조합이 주입받은 payload 목록을 무시하고 `product_image_names()` 를 하드코딩. 실제 제품 말고는 아무것도 상대로 돌릴 수 없었고, **그 계층의 시나리오 스위트가 전부 공허하게 통과**했다 | 주입값을 쓰도록 수정. 교훈: 테스트 가능성이 없으면 테스트가 없고, 테스트가 없으면 결함이 산다 |\n'
 '| I03 | P0, 업데이트 해소됨 (2026-09-09, history #468) | production payload 기본값이 `L"ui' + BS + 'shell.html"` — `' + BS + 's` 는 유효한 escape 가 아니라 값이 **`uishell.html`** 이 된다. `' + BS + 'm` 도 같다. **출시된 업데이터가 없는 파일 두 개를 찾아 모든 swap 이 실패**했을 것이고, 현장에서만 드러났을 것이다 | escape 수정. 빌드 산출물에서 `ui' + BS + 'shell.html` 1건·깨진 이름 0건 확인. **컴파일러는 조용했고 파이썬은 같은 문자열에 경고를 냈다** |\n'
 '| I04 | P2, 업데이트 해소됨 (2026-09-09, history #468) | swap 이 교체 대상을 `<name>.gnlink-old` 로 **이름을 바꾸면** 그 파일을 실행 중이던 프로세스는 바뀐 이름으로 계속 돈다. 이미지 이름이 불변이라 가정하고 **정확 일치로 훑던 정리 경로가 그것들을 못 봤다** | 접두사 일치로 수정. **swap 자체가 그 가정을 깬다**는 것이 요점 |\n'
)
d = d[:end] + rows + d[end:]
io.open(p, 'w', encoding='utf-8', newline='').write(d)

# ---------------------------------------------------------------- checklist: two corrections
p = 'docs/수동확인_체크리스트.md'
d = io.open(p, encoding='utf-8').read()

old = '''| **`run_from_copy` 의 실제 복사·실행** | **순서**는 `prepare_working_copy()` 로 덮였다(판단→생성→복사→실행). 실제 `CopyFileW`·`CreateProcessW` 는 관리자 권한과 실제 `%ProgramFiles%` 가 필요하다 | 05 |'''
new = '''| **`run_from_copy` 의 실제 복사·실행** | **순서**는 `prepare_working_copy()` 로 덮였다(판단→생성→복사→실행). 실제 `CopyFileW`·`CreateProcessW` 는 관리자 권한과 실제 `%ProgramFiles%` 가 필요하다 | 05 |
| **클라이언트 백업(`.gnlink-old`)의 수명** | 실제 조합에서 살아남는 경우가 있고 **원인 미상**이다. 클라이언트는 swap 이 *놓은* 파일에서 실행되므로 이름이 바뀐 백업을 붙잡을 이유가 없어 보이는데 그렇다. 시나리오 스위트의 단정은 **호스트 백업**으로 좁혀져 있다(history #468) | 04·06 |'''
assert old in d, 'unverified row'
d = d.replace(old, new, 1)

old = '''> ⚠️ **2026-09-08 재정정.**'''
new = '''> ⚠️ **2026-09-09 정정 — "실행 계층은 실기로만" 이 과했다.**
> 이 문서는 한때 복사·프로세스 시작·job·재실행을 실기 전용으로 적었다. **틀렸다**: 그것들은 dummy
> exe·임시 설치 root·주입 경로로 **격리 OS 테스트가 가능하고, 지금 실제로 그렇게 테스트된다**
> (`update_relaunch_test` E1~E12, `updater_scenarios_test` 여섯 시나리오, history #453·#467·#468).
> **진짜 실기 전용은 운영 서비스 등록·운영 인증서 체인·UAC 사용자 경험뿐**이다. 실행 계층을 실기로
> 미뤄 두면 그 코드는 출시 전까지 한 번도 돌지 않는데, 그것이 이 작업에서 결함이 살아남은 방식이다.

> ⚠️ **2026-09-08 재정정.**'''
assert old in d, 'correction anchor'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ledger I02-I04 and the two checklist corrections')
