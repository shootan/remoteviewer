import io

p = 'docs/수동확인_체크리스트.md'
d = io.open(p, encoding='utf-8').read()

old = '''| **운영 배포·서명키** | 별도 승인 사항(설계 4.5). 운영키는 fail-closed 유지 | — |'''
new = '''| **운영 배포·서명키** | 별도 승인 사항(설계 4.5). 운영키는 fail-closed 유지 | — |
| **`production_updater_deps()` 의 동작** | 주입 경계 **너머**다. 테스트는 경계 이쪽만 돈다 — **모든 배선이 존재한다는 것**은 회귀로 고정했지만(assembly 40 checks), 진짜 WinHTTP·진짜 SCM·진짜 프로세스 열거기가 **제 일을 하는지**는 실기다 | 00·03·04·05 |
| **`run_from_copy` 의 실제 복사·실행** | **순서**는 `prepare_working_copy()` 로 덮였다(판단→생성→복사→실행). 실제 `CopyFileW`·`CreateProcessW` 는 관리자 권한과 실제 `%ProgramFiles%` 가 필요하다 | 05 |'''
assert old in d, 'table'
d = d.replace(old, new, 1)

old = '''**선행 조건**: 두 플랫폼 모두 경로가 불리고(`updater_main.cpp` → `run_update`, `MainActivity` →
확인·다운로드·설치), **그 경로에 실제 데이터가 흐른다**(manifest fetch → 검증 → 버전 → 등록·health,
history #462). 이제 막는 것은 **설치본이 없다는 것 하나**다.'''
new = '''**선행 조건**: 두 플랫폼 모두 경로가 불리고(`updater_main.cpp` → `run_update`, `MainActivity` →
확인·다운로드·설치), **그 경로에 실제 데이터가 흐르며**(manifest fetch → 검증 → 버전 → 등록·health,
history #462), **업데이터가 그것들을 어떻게 조립하는지에도 테스트가 있다**(history #464).
이제 막는 것은 **설치본이 없다는 것 하나**다.

**세 층을 구분해 둔다** — 이 작업에서 세 번 다 한 번씩 틀렸기 때문이다:

| 층 | 무엇을 보장하나 | 어떻게 확인했나 | 여기서 놓치면 |
|---|---|---|---|
| **부품** | 각 조각이 옳게 동작 | 918 checks | 아무것도 — 부품은 다 옳았다 |
| **조합** | 업데이터가 그것들을 제대로 **배선**했는가 | assembly 40 checks + 대조군 | **890 이 초록인 채 다섯 결함**(#462·#463) |
| **실행** | 진짜 OS·진짜 권한에서 도는가 | **아직 안 함** — 아래 UPD-FIELD | 지금 남아 있는 것 전부 |

부품이 검증된 것과 기능이 동작하는 것이 다르고(#452), 호출자가 있는 것과 데이터가 흐르는 것이
다르며(#462), 데이터가 흐르는 것과 **그 배선을 아무도 안 보는 것**이 또 다르다(#464).'''
assert old in d, 'precondition'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('remaining gaps recorded')
