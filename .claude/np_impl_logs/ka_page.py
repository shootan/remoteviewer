import os
p = r'C:\work\gnlink-wt\ka-pc\apps\native_poc\ui\shell.html'
b = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in b
s = b.replace('\r\n', '\n')

def rep(a, c, cnt=1):
    global s
    assert s.count(a) == cnt, (s.count(a), a[:80])
    s = s.replace(a, c)

rep("""  // Remembered so the list can be redrawn when only the connecting target changes.
  let lastHosts = [];
  let connectingId = '';
""", """  // Remembered so the list can be redrawn when only the connecting target changes.
  let lastHosts = [];
  let connectingId = '';
  // ka-pc-impl r1 (KA ③): what the shell says about each kept PC -- only what a managed viewer reported
  // (hostKeep), never inferred here. "ready" means the PC's standby entry is confirmed and alive.
  const keepState = {};
  const keepText = {
    ready: '준비됨', preparing: '준비 중', starting: '준비 중', retry: '다시 준비 중',
    active: '연결됨', unsupported: '상시 유지 미지원 (PC 업데이트 필요)', failed: '상시 유지 안 됨',
  };
""")
rep("""      button.disabled = !host.online || busy;
      const state = connecting ? '연결하는 중…' : (host.online ? '연결 가능' : '오프라인');""",
    """      // Per card (ka-pc-impl r1): the PC being connected is the one that waits; another PC can still be
      // chosen -- the newest choice is what counts.
      button.disabled = !host.online || connecting;
      const kept = keepText[keepState[host.hostId]];
      const state = connecting ? '연결하는 중…'
                               : (host.online ? (kept ? kept : '연결 가능') : '오프라인');""")
rep("""      button.onclick = () => {
        if (busy) return;
        connectingId = host.hostId;
        setBusy(true);
        message($('hostsMsg'), (host.hostName || 'PC') + '에 연결하는 중');""",
    """      button.onclick = () => {
        if (host.hostId === connectingId) return;
        connectingId = host.hostId;
        message($('hostsMsg'), (host.hostName || 'PC') + '에 연결하는 중');""")
rep("""    if (msg.type === 'signedOut') {""", """    if (msg.type === 'hostKeep') {
      if (msg.state && msg.state !== 'none') keepState[msg.hostId] = msg.state;
      else delete keepState[msg.hostId];
      if (lastHosts.length) renderHosts(lastHosts);
      return;
    }
    if (msg.type === 'signedOut') {
      for (const k of Object.keys(keepState)) delete keepState[k];""")
out = s.replace('\n', '\r\n') if crlf else s
open(p, 'wb').write(out.encode('utf-8'))
print('ok')
