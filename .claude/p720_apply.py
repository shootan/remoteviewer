import io

# history #431
frag = io.open('.claude/p720_history.md', encoding='utf-8').read()
p = 'docs/history.md'
h = io.open(p, encoding='utf-8-sig').read()
assert '### 431)' not in h
io.open(p, 'w', encoding='utf-8-sig', newline='').write(h + frag)
print('history #431 appended')

# 구현계획.md -- add under the 0.2.104 field-test area
p = 'docs/구현계획.md'
d = io.open(p, encoding='utf-8').read()
anchor = '- [ ] 양단 NIC 캡처로 최초 UDP 유입 중단 위치 확정'
assert anchor in d and '#431' not in d
d = d.replace(
    anchor,
    '- [~] 0.2.104 실기 17:50~ **720p 고착** 증거 보전·코드 교차확인 완료 (history #431, '
    '`docs/field_test_2026-09-08_0.2.104_720p.md`, 원장 **HN13**) — 17:56:08 에 profile=high 12Mbps 로 복귀했으나 '
    'encode=1280x720 유지, 뷰어는 17:55:21.763~18:03:40 내내 720p. `encodeSource` 오염(payload 무조건 대입) + '
    'aspectClose 교정 스킵 + fit 의 upscale 불가가 폐곡선. **런타임 실측 없음(정합이지 증명 아님)**, 수정 미위임\n'
    '- [ ] HN13 수정 — `encodeSource` 를 원본 geometry 의 진실로 유지 + readback 출력 재설정 복구. '
    '**무조건 upscale 허용은 해법 아님.** 별도 위임 대기\n'
    + anchor, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('구현계획.md updated')

# 작업목록.md -- extend item 1.8
p = 'docs/작업목록.md'
d = io.open(p, encoding='utf-8').read()
assert '#431' not in d
lines = d.split('\n')
i = next(k for k, l in enumerate(lines) if l.startswith('1.8 '))
lines[i] = lines[i] + (
    ' · **17:50~ 720p 고착 별건** 증거·코드 교차확인 완료 (history #431, 원장 HN13): '
    'profile 은 high 12Mbps 로 복귀했는데 encode 는 720p 고정 — `encodeSource` 오염 + aspectClose 교정 스킵. 수정 미위임')
io.open(p, 'w', encoding='utf-8', newline='').write('\n'.join(lines))
print('작업목록.md updated')
