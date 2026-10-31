# -*- coding: utf-8 -*-
"""P1: the fixed banner covered the page. P2: a connecting card claimed it was idle."""
import io

p = 'apps/native_poc/ui/shell.html'
d = io.open(p, encoding='utf-8').read()
Q = chr(39)

# ---------------------------------------------------------------- P2 styles
old = '''  .host .state { font-size: 11.5px; color: var(--muted); margin-top: 2px; }'''
new = '''  .host .state { font-size: 11.5px; color: var(--muted); margin-top: 2px; }
  /* The card that is connecting says so on itself. The status line under the list was the only
     place it appeared, so the card kept its green dot and "연결 가능" while the connection was
     being made -- not missing information, wrong information. */
  .host[data-connecting="1"] { border-color: var(--accent); opacity: 1; }
  .host[data-connecting="1"] .state { color: var(--accent); }
  .host[data-connecting="1"] .dot { background: var(--accent); box-shadow: none; }'''
assert d.count(old) == 1, 'host state css'
d = d.replace(old, new, 1)

# ---------------------------------------------------------------- P2 render
old = '''  function renderHosts(hosts) {
    const list = $('hostList');'''
new = '''  // Remembered so the list can be redrawn when only the connecting target changes.
  let lastHosts = [];
  let connectingId = '';

  function renderHosts(hosts) {
    lastHosts = hosts;
    const list = $('hostList');'''
assert d.count(old) == 1, 'render head'
d = d.replace(old, new, 1)

old = '''      button.className = 'host';
      button.dataset.online = host.online ? '1' : '0';
      button.disabled = !host.online;
      button.innerHTML =
        '<span class="dot' + (host.online ? ' on' : '') + '"></span>' +
        '<span><span class="name"></span><div class="state">' +
        (host.online ? '연결 가능' : '오프라인') + '</div></span>';'''
new = '''      const connecting = host.hostId === connectingId;
      button.className = 'host';
      button.dataset.online = host.online ? '1' : '0';
      if (connecting) button.dataset.connecting = '1';
      button.disabled = !host.online || busy;
      const state = connecting ? '연결하는 중…' : (host.online ? '연결 가능' : '오프라인');
      button.innerHTML =
        (connecting ? '<span class="spinner"></span>'
                    : '<span class="dot' + (host.online ? ' on' : '') + '"></span>') +
        '<span><span class="name"></span><div class="state">' + state + '</div></span>';'''
assert d.count(old) == 1, 'card markup'
d = d.replace(old, new, 1)

old = '''      button.onclick = () => {
        if (busy) return;
        setBusy(true);
        message($('hostsMsg'), (host.hostName || 'PC') + '에 연결하는 중');
        send({ type: 'connect', hostId: host.hostId, hostName: host.hostName });
      };'''
new = '''      button.onclick = () => {
        if (busy) return;
        connectingId = host.hostId;
        setBusy(true);
        message($('hostsMsg'), (host.hostName || 'PC') + '에 연결하는 중');
        renderHosts(lastHosts);
        send({ type: 'connect', hostId: host.hostId, hostName: host.hostName });
      };'''
assert d.count(old) == 1, 'onclick'
d = d.replace(old, new, 1)

# ---------------------------------------------------------------- P2 clear on settle
old = '''  function setBusy(on, text) {
    busy = on;'''
new = '''  function setBusy(on, text) {
    busy = on;
    // Whatever ended the wait -- connected, failed, signed out -- the card stops claiming it.
    // The mark disappearing is how "back to normal" is observed.
    if (!on && connectingId) {
      connectingId = '';
      if (lastHosts.length) renderHosts(lastHosts);
    }'''
assert d.count(old) == 1, 'setBusy'
d = d.replace(old, new, 1)

# ---------------------------------------------------------------- P1 make room for the banner
old = '''  const showUpdateBar = (on) => show($('updateBar'), on);'''
new = '''  // The bar is position:fixed, so it sits over the page rather than in it. A fixed offset would
  // be wrong at every width except the one it was measured at: at 360px the text wraps to four
  // lines and covered the GNLink wordmark entirely. The page is pushed down by whatever the bar
  // actually measures, after it has been laid out.
  const showUpdateBar = (on) => {
    const bar = $('updateBar');
    show(bar, on);
    requestAnimationFrame(() => {
      document.body.style.paddingTop = on ? bar.offsetHeight + 'px' : '';
    });
  };'''
assert d.count(old) == 1, 'showUpdateBar'
d = d.replace(old, new, 1)

# The text can rewrap when the version string changes, so re-measure after setting it.
old = '''      $('updateText').textContent = msg.text || ('새 버전 ' + updateVersion + ' 이 있습니다.');
      $('updateNow').disabled = false;
      showUpdateBar(true);'''
new = '''      $('updateText').textContent = msg.text || ('새 버전 ' + updateVersion + ' 이 있습니다.');
      $('updateNow').disabled = false;
      showUpdateBar(true);'''
assert d.count(old) == 1, 'updateAvailable'

# ---------------------------------------------------------------- P1 keep it right on resize
old = '''  send({ type: 'ready' });'''
new = '''  // A window that is resized while the offer is up rewraps the bar; the offset has to follow.
  window.addEventListener('resize', () => {
    const bar = $('updateBar');
    if (!bar.classList.contains('hidden')) {
      document.body.style.paddingTop = bar.offsetHeight + 'px';
    }
  });

  send({ type: 'ready' });'''
assert d.count(old) == 1, 'ready'
d = d.replace(old, new, 1)

io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('shell.html ok')
