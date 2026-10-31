import sys, re
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit
ROOT = r'D:\remote\remote'
BS = chr(92)
edit(ROOT + r'\apps\native_poc\src\viewer_frame_gate_test.cpp', [
("// P11 lifetime: a pending resume anchor ends on a keyframe wait, on a decoded keyframe, and on a\n// congestion state change; it never outlives the episode it started in.",
 "// P11 lifetime: a pending resume anchor ends on a keyframe wait, on a fresh keyframe (anchored at\n// admit), and on a congestion state change; it never outlives the episode it started in. Decoding a\n// frame, key or not, does not end it (S13, 2026-09-07)."),
('  std::printf("[T-P11d] pending ends on keyframe wait / decoded keyframe / state change' + BS + 'n");',
 '  std::printf("[T-P11d] pending ends on keyframe wait / fresh keyframe (anchored at admit) / state change' + BS + 'n");'),
("  // A decoded keyframe (resync) ends it.",
 "  // A fresh keyframe ends it at admit (the anchor moves to it); the decode afterwards changes nothing."),
])
# history #409 addendum (one line before the 미검증 line of #409), CRLF preserved
p = ROOT + r'\docs\history.md'
raw = open(p, 'rb').read()
t = raw.decode('utf-8')
i = t.index('### 409)')
m = re.search(r'\r?\n- 미검증: 사용자 실기\(UAC 2회·로그 업로드·원점 변경\)', t[i:])
assert m, 'anchor 409 미검증 not found'
nl = '\r\n' if '\r\n' in t[i:i+2000] else '\n'
add = nl + "- 보완(검증용 seq 1114 3번, 후속 test(viewer) 커밋): T-P11d printf·주석의 'decoded keyframe' 을 'fresh keyframe (anchored at admit)' 로 문구만 정정(동작 변경 없음 — 그 키프레임은 hold 1ms 의 fresh 프레임이라 admit 에서 anchor 되며 pending 이 끝난다; decode 는 pending 을 끝내지 않음). 우편함 지연으로 릴리스 커밋(#410) 뒤에 반영돼 검증용이 요청한 순서(테스트 커밋 → 릴리스 커밋)와 다름. 설치본은 1005ebe + 버전 bump 트리 빌드(제품 소스는 #410·이 커밋과 동일), 검사 요청 뒤라 재빌드는 검증용 회신 전 보류."
pos = i + m.start()
t = t[:pos] + add + t[pos:]
open(p, 'wb').write(t.encode('utf-8'))
print('history 409 addendum ok')
