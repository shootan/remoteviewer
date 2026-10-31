import io

# ------------------------------------------------------------------ the wiring plan's W4a
p = 'docs/업데이트_배선_계획.md'
d = io.open(p, encoding='utf-8').read()

old = '''상태기계에는 이미 **`UpdatedButNotRelaunched`** 라는 별도 종착점이 있고
(`update_state_machine.cpp:167-178`), 재실행 실패는 롤백이 아니라 그것으로 끝난다. 그 부분은 이미
맞다. **없는 것은 그 뒤다**:'''
new = '''상태기계에는 이미 **`UpdatedButNotRelaunched`** 라는 별도 종착점이 있고, 재실행 실패는 롤백이 아니라
그것으로 끝난다. 그 부분은 이미 맞다. **없는 것은 그 뒤다**:

> **줄 번호로 가리키지 않는다.** 이 문단은 한때 `update_state_machine.cpp:167-178` 이라고 적었고,
> 그 사이 파일이 자라 **그 줄은 다른 것을 가리키게 됐다.** 자라는 파일을 줄로 가리키면 유통기한이
> 짧다 — 이름으로 가리킨다.'''
assert old in d, 'W4a line ref'
d = d.replace(old, new, 1)

old = '''- **복구 상태를 남긴다.** `RelaunchEffects::lastActions` 가 항목별 결과 문장을 이미 만들지만
  **아무도 읽지 않는다.** 업데이터(W1)가 로그에 남기고, 그 요약을 사용자 안내에 쓴다.'''
new = '''- **복구 상태를 남긴다.** ✅ 완료. 항목별 결과는 `RelaunchEffects::lastOutcomes` 로 나오고 업데이터가
  단계별로 로그에 적는다(`relaunch (required) ...` / `relaunch (optional) ...`). 여기 적혀 있던
  `lastActions` 는 **아무도 읽지 않아서 지웠다** — 읽히지 않는 출력은 기록이 아니라 미결이다.'''
assert old in d, 'lastActions bullet'
d = d.replace(old, new, 1)

old = '''- **무엇이 안 떴는지 구분한다.** 지금 `relaunch` 는 `bool` 하나를 돌려준다 — 그래서 **호스트가 못'''
new = '''- **무엇이 안 떴는지 구분한다.** ✅ 완료, 그리고 계획보다 멀리 갔다. `bool` → `RelaunchVerdict`
  (`AllBack`/`OptionalMissing`/`RequiredMissing`) 로 바뀌었고, 그 뒤 **단계 자체가 둘로 나뉘었다**:
  `RelaunchRequired()` → health → commit → `RelaunchOptional()`(설계 3.11.0, history #473). 아래 원문은
  당시 상태를 남긴 것이다 — 지금 `relaunch` 는 `bool` 하나를 돌려준다 — 그래서 **호스트가 못'''
assert old in d, 'bool bullet'
d = d.replace(old, new, 1)

# the E-list gains the ordering item
old = '''| E5b | **셸 폴백이 소유를 주장하지 않는가**'''
new = '''| E5c | **선택 이미지가 commit 뒤에만 뜨는가** | 정상 경로에서 **commit 전 optional 기동 0 / 후 1**, 롤백 경로에서 **롤백 전 0**. 순서를 되돌려 실패하는지까지 확인(반대 증거) | 없음 |
| E5b | **셸 폴백이 소유를 주장하지 않는가**'''
assert old in d, 'E5b'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)

# ------------------------------------------------------------------ the checklist: what a tester will now see
p = 'docs/수동확인_체크리스트.md'
d = io.open(p, encoding='utf-8').read()
old = '''**추가(2026-09-09)**: 무결성 수준은 두 경로가 같으므로 **어느 경로로 떴는지는 로그로 판정한다**'''
new = '''**추가(2026-09-09, 순서)**: 클라이언트는 이제 **commit 이 끝난 뒤에** 뜬다(설계 3.11.0). 호스트가 먼저 올라오고 health 가 확인된 다음이므로, **클라이언트 창이 몇 초 늦게 나타나는 것은 정상**이고 결함이 아니다. 반대로 **호스트보다 먼저 뜨면 그것이 결함**이다. **추가(2026-09-09)**: 무결성 수준은 두 경로가 같으므로 **어느 경로로 떴는지는 로그로 판정한다**'''
assert old in d, 'field-01 addition'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('wiring plan and checklist brought in line with the two phases')
