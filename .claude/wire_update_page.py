# -*- coding: utf-8 -*-
"""Puts the install button on the page.

Before this the page could be told a new version existed and had nothing to offer but the
sentence. The native side has handled {"type":"update"} all along; nothing ever sent it.
"""
import io

p = 'apps/native_poc/ui/shell.html'
d = io.open(p, encoding='utf-8').read()
Q = chr(39)

# ---------------------------------------------------------------- style
old = '  @keyframes spin { to { transform: rotate(360deg); } }'
new = '''  /* Fixed to the top rather than placed in a card: the offer belongs to the program, not to the
     screen that happens to be showing, and it has to survive sign-in replacing one card with
     another. */
  #updateBar {
    position: fixed; top: 0; left: 0; right: 0; z-index: 10;
    display: flex; align-items: center; gap: 12px;
    padding: 10px 16px;
    background: #14304f; border-bottom: 1px solid var(--accent);
  }
  #updateBar .what { flex: 1; font-size: 13px; }
  #updateBar button {
    width: auto; margin: 0; padding: 7px 14px; font-size: 13px;
  }
  #updateLater { background: transparent; border: 1px solid var(--line); color: var(--muted); }
  #updateLater:hover { background: var(--panel-hover); }
  @keyframes spin { to { transform: rotate(360deg); } }'''
assert d.count(old) == 1, 'style anchor'
d = d.replace(old, new, 1)

# ---------------------------------------------------------------- markup
old = '<body>\n\n<div class="card" id="signInCard">'
new = '''<body>

<!-- Hidden until a check finds something. The version is filled in from the message's field, not
     read out of the sentence beside it. -->
<div id="updateBar" class="hidden">
  <div class="what" id="updateText"></div>
  <button id="updateNow">업데이트</button>
  <button id="updateLater">나중에</button>
</div>

<div class="card" id="signInCard">'''
assert d.count(old) == 1, 'markup anchor'
d = d.replace(old, new, 1)

# ---------------------------------------------------------------- behaviour
old = '''  window.chrome.webview.addEventListener(''' + Q + '''message''' + Q + ''', (event) => {'''
new = '''  // Cleared on sign-out, so an offer found for one account is never left standing for the next.
  let updateVersion = '';

  const showUpdateBar = (on) => show($('updateBar'), on);

  $('updateNow').onclick = () => {
    if (!updateVersion) return;
    // Disabled here as well as by the native side. This stops the ordinary double click; the
    // native flag stops the two messages that were already in flight when it happened.
    $('updateNow').disabled = true;
    send({ type: 'update' });
  };
  // A deferral and nothing else: it takes the bar down for this run and does not touch signing
  // in, the host list, or whether the next check offers it again.
  $('updateLater').onclick = () => showUpdateBar(false);

  window.chrome.webview.addEventListener('message', (event) => {'''
assert d.count(old) == 1, 'behaviour anchor'
d = d.replace(old, new, 1)

# ---------------------------------------------------------------- dispatch
old = '''    if (msg.type === ''' + Q + '''status''' + Q + ''') {'''
new = '''    if (msg.type === 'updateAvailable') {
      updateVersion = msg.version || '';
      $('updateText').textContent = msg.text || ('새 버전 ' + updateVersion + ' 이 있습니다.');
      $('updateNow').disabled = false;
      showUpdateBar(true);
      return;
    }
    if (msg.type === 'updateCleared') {
      updateVersion = '';
      $('updateNow').disabled = false;
      showUpdateBar(false);
      return;
    }
    if (msg.type === 'updateBusy') {
      // Re-enabled when the attempt ends, whichever way it ended. A button that stays dead after
      // a failure leaves the user with a notice and no way to act on it -- which is the state
      // this whole change exists to remove.
      $('updateNow').disabled = !!msg.busy;
      $('updateNow').textContent = msg.busy ? '준비 중…' : '업데이트';
      return;
    }
    if (msg.type === 'status') {'''
assert d.count(old) == 1, 'dispatch anchor'
d = d.replace(old, new, 1)

# ---------------------------------------------------------------- sign-out hides it
old = '''    if (msg.type === ''' + Q + '''signedOut''' + Q + ''') {
      setBusy(false);'''
new = '''    if (msg.type === 'signedOut') {
      setBusy(false);
      updateVersion = '';
      showUpdateBar(false);'''
assert d.count(old) == 1, 'signedOut anchor'
d = d.replace(old, new, 1)

io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('shell.html ok')
