import io

# 1) history.md -- append entry 428
frag = io.open('.claude/evidence_history_fragment.md', encoding='utf-8').read()
hist_path = 'docs/history.md'
hist = io.open(hist_path, encoding='utf-8-sig').read()
assert '### 428)' not in hist, 'entry 428 already present'
assert hist.endswith('\n'), 'history does not end with newline'
io.open(hist_path, 'w', encoding='utf-8-sig', newline='').write(hist + frag)
print('history.md appended, entry 428')

# 2) 구현계획.md -- update the 0.2.104 field-test checklist line
plan_path = 'docs/구현계획.md'
plan = io.open(plan_path, encoding='utf-8').read()
old = '- [ ] 양단 NIC 캡처로 최초 UDP 유입 중단 위치 확정'
new = (
    '- [~] 0.2.104 실기 17:15~17:22 증거 보전·문서화 완료 (history #428, `docs/field_test_2026-09-08_0.2.104.md`) — '
    'session-dead 자동 종료 6건(crash 아님, `viewer_session_watchdog.cpp:82-96`) · link-silent 7건 · terminal 11건(HQ 전환 전 3건 포함) · '
    'congestion 전이 0/373 이나 복구 요청 4회·실행 0회 · ABR 12→9→6.6Mbps · captureRestarts 1→14. **원인 미확정**\n'
    '- [ ] 양단 NIC 캡처로 최초 UDP 유입 중단 위치 확정'
)
assert old in plan, 'anchor line not found in 구현계획.md'
assert '#428' not in plan, 'entry already recorded'
plan = plan.replace(old, new, 1)
io.open(plan_path, 'w', encoding='utf-8', newline='').write(plan)
print('구현계획.md checklist updated')

# 3) 작업목록.md -- update item 1.8 status
tasks_path = 'docs/작업목록.md'
tasks = io.open(tasks_path, encoding='utf-8').read()
assert '#428' not in tasks, 'task line already updated'
lines = tasks.split('\n')
idx = next(i for i, l in enumerate(lines) if l.startswith('1.8 '))
lines[idx] = lines[idx].replace('1.8 [ ] ', '1.8 [~] ', 1) + (
    ' — 17:15~17:22 실기 증거 보전·문서화 완료 (history #428, `docs/field_test_2026-09-08_0.2.104.md`): '
    '뷰어 자동 종료 6회(크래시 아님)·링크 무수신 7회·terminal 11회·복구 요청 4/실행 0. **원인 미확정**, 양단 NIC 캡처 필요'
)
io.open(tasks_path, 'w', encoding='utf-8', newline='').write('\n'.join(lines))
print('작업목록.md item 1.8 updated')
