import json
wt = 'C:\\work\\gnlink-wt\\rel-0163\\'

def edit(path, pairs):
    s = open(path, 'rb').read().decode('utf-8')
    crlf = '\r\n' in s
    s = s.replace('\r\n', '\n')
    for old, new in pairs:
        assert s.count(old) == 1, (path, s.count(old), old[:80])
        s = s.replace(old, new)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(path, 'wb').write(s.encode('utf-8'))

edit(wt + 'docs\\작업목록.md', [
    ('| **PS** | **PC 0.2.163 후보 dry-run(10-26, 게시 전) — capture-lifecycle r1~r4 구현·자동 검증·Codex SOURCE OK(adc9482), 실기 대기** · ',
     '| **PS** | **PC 0.2.163 최종 후보(10-26, 게시 전) — capture-lifecycle r1~r4(adc9482) + PC 뷰어 로그인 전 picker 선택 결함 수정(rel-0163 r3~r7, eefac09, Codex SOURCE OK; 실제 제품 renderer E1 로 발견·확인), 실기 대기** · '),
])

edit(wt + 'docs\\구현계획.md', [
    ('''- [x] R1 PC 0.2.163 통합·릴리스 후보 dry-run — rel/0.2.163 = main a1c551c + capture-lifecycle adc9482(LK-UX 포함), 버전 90192f4 (2026-10-26)''',
     '''- [x] R1 PC 0.2.163 통합·릴리스 후보 dry-run — rel/0.2.163 = main a1c551c + capture-lifecycle adc9482(LK-UX 포함), 버전 90192f4 (2026-10-26)
- [x] E1 실제 제품 renderer 시험(PS·UA, 화면 밖 창·WGC 창 캡처) — PS 에서 PC 뷰어 로그인 전 picker 선택 결함 발견 (2026-10-26)
- [x] 로그인 전 picker 바탕화면 선택 = 로컬 선택 transaction(ack 없음·op ticket·최종 채택 경계) — rel-0163 r3~r7, 최종 eefac09 Codex SOURCE OK (2026-10-26)
- [ ] 같은 ack 대기(공용 세션) — APK 0.2.32 단계에서 보수 (작업용2)
- [ ] R1 PC 0.2.163 최종 후보 재빌드·dry-run·검증용 독립 E1 (eefac09 위)'''),
])

h = wt + 'docs\\history.md'
s = open(h, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')
s += '''- 최종: 제품 **eefac09**(Codex `codex_rel_0163_src_r7.md` SOURCE OK) + 시험 0854ac4. 이전 후보 90192f4 와 그 dry-run 산출물(manifest 5ea1ac74…)은 이 결함을 포함하므로 쓰지 않는다. 최종 재빌드·dry-run 은 이 문서 commit 위에서(제품 diff 0).
'''
if crlf:
    s = s.replace('\n', '\r\n')
open(h, 'wb').write(s.encode('utf-8'))

R = 'C:/work/gnlink-wt/rel-0163'
d = 'D:\\remote\\remote\\.claude\\caplife\\'
json.dump({"repo_path": R, "files": ["docs/history.md", "docs/작업목록.md", "docs/구현계획.md"]}, open(d + 'r8d_add.json', 'w', encoding='utf-8'), ensure_ascii=False)
json.dump({"repo_path": R, "message": """docs(rel-0163 r8): the final 0.2.163 candidate -- the logon-screen picker defect and its fix, in the task list and the plan

eefac09 (SOURCE OK, codex_rel_0163_src_r7.md) + 0854ac4 is the final product/test; 90192f4 and its dry-run are not
used. Docs only: apps/libs unchanged.

Product / test / docs: product (none) / test (none) / docs (history, task list, plan).

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"""}, open(d + 'r8d_commit.json', 'w', encoding='utf-8'), ensure_ascii=False)
print('ok')
