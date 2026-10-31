# 보조: 호스트 광고 주소(port)별 bound 관찰 비율 — 집(추정) 클라이언트 제외. 상관 확인용, 인과 아님.
import re,collections,os
p=os.path.join(os.path.dirname(os.path.abspath(__file__)),"server_relay_capability_lines.txt")
c={}
for l in open(p,encoding="utf-8",errors="replace"):
    m=re.search(r"connect=([0-9a-f]+)",l)
    if not m: continue
    e=c.setdefault(m.group(1),{})
    mm=re.search(r"minted host=(\w+)",l)
    if mm: e['id']=mm.group(1)[:8]
    mm=re.search(r"eligible client=([\d.]+):\d+ .*?host=[^@]+@([\d.]+):(\d+)",l)
    if mm: e['cip']=mm.group(1); e['port']=mm.group(3)
    if " bound client=" in l: e['bound']=1
g=collections.defaultdict(lambda:[0,0])
for e in c.values():
    if 'cip' not in e or e['cip'].startswith("175.207.45."): continue
    k=(e.get('id'),e['port']); g[k][0]+=1; g[k][1]+=e.get('bound',0)
for k in sorted(g): print(k, "connect",g[k][0],"bound",g[k][1])
