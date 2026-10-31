p = r'C:\work\gnlink-wt\ka-pc\apps\native_poc\ui\shell.html'
b = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in b
s = b.replace('\r\n', '\n')

def rep(a, c):
    global s
    assert s.count(a) == 1, (s.count(a), a[:90])
    s = s.replace(a, c)

rep('''  .empty { color: var(--muted); font-size: 12.5px; text-align: center; padding: 26px 0; }
''', '''  .empty { color: var(--muted); font-size: 12.5px; text-align: center; padding: 26px 0; }
  /* ka-pc-impl r2 (Codex N3): a kept PC's own "연결 해제" -- a button BESIDE the card (a button inside a button is
     not a button), over the card's right edge; the card leaves room for it. */
  .hostRow { position: relative; }
  .hostRow[data-kept="1"] .host { padding-right: 96px; }
  .keepRelease {
    position: absolute; right: 10px; top: 50%; transform: translateY(-50%);
    width: auto; margin: 0; padding: 6px 10px; font-size: 12px; font-weight: normal;
    background: none; border: 1px solid var(--line); border-radius: 7px; color: var(--muted); cursor: pointer;
  }
  .keepRelease:hover:not(:disabled) { color: var(--text); border-color: #33404f; background: none; }
  .keepRelease:disabled { opacity: .45; cursor: default; }
''')

rep('''  // ka-pc-impl r1 (KA ③): what the shell says about each kept PC -- only what a managed viewer reported
  // (hostKeep), never inferred here. "ready" means the PC's standby entry is confirmed and alive.
  const keepState = {};
  const keepText = {
    ready: '준비됨', returning: '돌아오는 중', preparing: '준비 중', starting: '준비 중', retry: '다시 준비 중',
    active: '연결됨', unsupported: '상시 유지 미지원 (PC 업데이트 필요)', failed: '상시 유지 안 됨',
  };
''', '''  // ka-pc-impl r1 (KA ③): what the shell says about each kept PC -- only what a managed viewer reported
  // (hostKeep), never inferred here.
  // ka-pc-impl r2 (Codex D1/D2): "대기 중" (ready) = this PC handed its session back here and its standby entry is
  // alive -- NOT a statement that the PC has finished cleaning up the previous connection; for some seconds after a
  // session that may still be going on there ("returning", said as such). An unanswered standby is "not confirmed",
  // never a diagnosis ("PC update needed" was one): connecting to that PC again tries again.
  const keepState = {};
  const keepText = {
    ready: '대기 중', returning: '대기 중 · 이전 연결 정리 중일 수 있음', preparing: '대기 준비 중', starting: '대기 준비 중',
    retry: '대기 다시 준비 중', connecting: '여는 중', active: '연결됨',
    unsupported: '대기 확인 안 됨 (다시 연결하면 재시도)', failed: '대기 유지 안 됨',
  };
  // The states in which a kept PC can be released by the user (its standby viewer exists).
  const keepReleasable = { ready: 1, returning: 1, preparing: 1, starting: 1, retry: 1, connecting: 1, active: 1 };
''')

rep('''    for (const host of hosts) {
      const button = document.createElement('button');''', '''    for (const host of hosts) {
      const row = document.createElement('div');
      row.className = 'hostRow';
      const button = document.createElement('button');''')

rep('''      button.onclick = () => {
        if (host.hostId === connectingId) return;
        connectingId = host.hostId;
        message($('hostsMsg'), (host.hostName || 'PC') + '에 연결하는 중');
        renderHosts(lastHosts);
        send({ type: 'connect', hostId: host.hostId, hostName: host.hostName });
      };
      list.appendChild(button);
    }''', '''      button.onclick = () => {
        if (host.hostId === connectingId) return;
        connectingId = host.hostId;
        message($('hostsMsg'), (host.hostName || 'PC') + '에 연결하는 중');
        renderHosts(lastHosts);
        send({ type: 'connect', hostId: host.hostId, hostName: host.hostName });
      };
      row.appendChild(button);
      // ka-pc-impl r2 (Codex N3): this PC's standby (and its open session, if any) ends, and it is not kept again
      // on its own -- until the user connects to it.
      if (keepReleasable[keepState[host.hostId]]) {
        row.dataset.kept = '1';
        const release = document.createElement('button');
        release.className = 'keepRelease';
        release.textContent = '연결 해제';
        release.dataset.hostId = host.hostId;
        release.onclick = (e) => {
          e.stopPropagation();
          release.disabled = true;
          message($('hostsMsg'), (host.hostName || 'PC') + ' 연결을 해제하는 중');
          send({ type: 'releaseKept', hostId: host.hostId });
        };
        row.appendChild(release);
      }
      list.appendChild(row);
    }''')
open(p, 'wb').write((s.replace('\n', '\r\n') if crlf else s).encode('utf-8'))
print('ok')
