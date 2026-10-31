# NP 집계 재현 스크립트 (검증용 2026-10-22). 입력: 같은 폴더 server_relay_capability_lines.txt
#  = journalctl --user -u remote60-directory --since 2026-10-09 | grep -a "\[relay\] connect=\|\[capability\] connect=" (KST)
# 단위: 고유 connect id. 그룹: eligible 줄의 host 이름 + client= (서버 광고 관찰값) IP 대역 추정.
# 열: bound 관찰(중계 바인딩 시도, 성공 아님) / active 관찰(HelloAck 뒤) / bound 없음(직접 성공·실패·취소·누락 구분 불가).
import re,collections,os,sys
p=os.path.join(os.path.dirname(os.path.abspath(__file__)),"server_relay_capability_lines.txt")
c={}
grace=collections.Counter()
for l in open(p,encoding="utf-8",errors="replace"):
    m=re.search(r"connect=([0-9a-f]+)",l)
    if not m: continue
    k=m.group(1); e=c.setdefault(k,{})
    if 'first' not in e: e['first']=l[:15]
    mm=re.search(r"minted host=(\w+)",l)
    if mm: e['hostid']=mm.group(1)[:8]
    mm=re.search(r"eligible client=([\d.]+):\d+ (?:wire=([\d.]+) )?host=([^@]+)@",l)
    if mm: e['cip']=mm.group(1); e['wire']=mm.group(2); e['hname']=mm.group(3)
    if " bound client=" in l: e['bound']=1
    if "active; helloAck" in l: e['active']=1
    mm=re.search(r"answering in (\d+)ms",l)
    if mm: grace[mm.group(1)]+=1
def net(ip):
    if ip is None: return "eligible 줄 없음"
    if ip.startswith("175.207.45."): return "집(추정)"
    if ip.startswith("211.218.222."): return "회사(추정)"
    return "기타 "+".".join(ip.split(".")[:2])+".x"
g=collections.defaultdict(lambda:[0,0,0,0])
for k,e in c.items():
    key=(e.get('hname',e.get('hostid','?')),net(e.get('cip')))
    r=g[key]; r[0]+=1; r[1]+=e.get('bound',0); r[2]+=e.get('active',0); r[3]+=0 if e.get('bound') else 1
tot=[0,0,0,0]
print("host | client 그룹 | 고유 connect | bound 관찰 | active 관찰 | bound 없음")
for key in sorted(g):
    r=g[key]; tot=[a+b for a,b in zip(tot,r)]
    print(f"{key[0]} | {key[1]} | {r[0]} | {r[1]} | {r[2]} | {r[3]}")
print(f"합계 | | {tot[0]} | {tot[1]} | {tot[2]} | {tot[3]}")
print("relay 응답 지연 로그값:",dict(grace))
print("범위:",min(e['first'] for e in c.values()),"~",max(e['first'] for e in c.values()),"(KST, journal 표기 월 일 시각)")
