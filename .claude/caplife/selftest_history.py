"""Read-only: every tool call in this project's Claude transcripts that ran the release gate's --self-test, with its
time, session, the command (trimmed) and what its result said. Nothing is executed, created outside this script's own
output file, or deleted."""
import glob
import json
import os
import re

DIR = r'C:\Users\shota\.claude\projects\D--remote-remote'
rows = []
for path in sorted(glob.glob(os.path.join(DIR, '*.jsonl'))):
    sid = os.path.basename(path)[:8]
    calls = {}
    try:
        lines = open(path, encoding='utf-8', errors='replace').read().splitlines()
    except Exception:
        continue
    for line in lines:
        try:
            o = json.loads(line)
        except Exception:
            continue
        msg = o.get('message') or {}
        content = msg.get('content')
        if not isinstance(content, list):
            continue
        for c in content:
            if not isinstance(c, dict):
                continue
            if c.get('type') == 'tool_use':
                cmd = json.dumps(c.get('input', {}), ensure_ascii=False)
                flat = cmd.replace(chr(92), '/')
                if re.search(r'python[^|;&]{0,60}gnlink_check_fixed_server\.py[^|;&]{0,60}--self-test', flat):
                    calls[c.get('id')] = (o.get('timestamp', '?'), c.get('name'), cmd)
            elif c.get('type') == 'tool_result' and c.get('tool_use_id') in calls:
                ts, name, cmd = calls.pop(c.get('tool_use_id'))
                body = c.get('content')
                text = body if isinstance(body, str) else json.dumps(body, ensure_ascii=False)
                m = re.findall(r'ALL PASS \((\d+) checks\)|FAIL \((\d+) failed, (\d+) passed\)', text)
                note = re.search(r'NOTE  self-test run folder: [^\\n"]*', text)
                rows.append((ts, sid, name, re.sub(r'\s+', ' ', cmd)[:160], str(m)[:60], note.group(0) if note else ''))
    for cid, (ts, name, cmd) in calls.items():
        rows.append((ts, sid, name, re.sub(r'\s+', ' ', cmd)[:160], 'no result recorded', ''))
rows.sort()
out = r'D:\remote\remote\.claude\caplife\selftest_history.txt'
with open(out, 'w', encoding='utf-8') as f:
    for r in rows:
        f.write(' | '.join(r) + '\n')
print(len(rows), 'calls')

