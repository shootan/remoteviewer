import json,sys
p=r'C:\Users\shota\.claude\projects\D--remote-remote\c8a294c9-a630-46fb-9049-714f0642cff2.jsonl'
out=[]
for line in open(p,encoding='utf-8'):
    if sys.argv[1] not in line: continue
    try: o=json.loads(line)
    except: continue
    for c in (o.get('message',{}).get('content') or []):
        if isinstance(c,dict) and c.get('type')=='tool_use':
            cmd=str(c.get('input',{}).get('command',''))
            if sys.argv[1] in cmd and 'find_cmd' not in cmd and 'grep' not in cmd and 'json' not in cmd: out.append(cmd)
print(out[-1] if out else None)
