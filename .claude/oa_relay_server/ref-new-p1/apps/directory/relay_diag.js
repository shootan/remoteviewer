'use strict';
// Bounded diagnostics for datagrams the relay refuses.
//
// The listener used to drop an ineligible punch or a mismatched Hello without a word, which is
// how the wire/advertised mix-up of 2026-10-11 stayed invisible: the log showed `eligible` and
// then nothing. One line per refusal would be the opposite mistake -- the relay port is open to
// the internet and reached before any authentication, so a stranger's flood would become our
// disk's problem. Three limits, none of which depends on a timer firing:
//
//   - per (reason, source ip): a few lines per window, then one count when the window rolls;
//   - entries: a hard cap on how many (reason, ip) pairs are tracked at once. Past it, a new
//     source allocates nothing and is only counted per reason;
//   - output: a global budget of lines per window, which the per-key lines, the per-key
//     summaries and the overflow summary all draw from. Everything that did not fit is one
//     number when the next window opens.
//
// Pure: the clock and the sink are injected, so the limits are testable without a socket and
// without waiting. Nothing here changes what the relay does with the packet -- a suppressed line
// is still a refused datagram.

function createRelayDiag(opts = {}) {
  const now = opts.now || Date.now;
  const log = opts.log || ((line) => console.log(line));
  const windowMs = opts.windowMs || 10 * 1000;
  const linesPerKey = opts.linesPerKey || 3;
  const maxKeys = opts.maxKeys || 256;
  const linesPerWindow = opts.linesPerWindow || 24;

  const keys = new Map();          // `${reason}:${ip}` -> { since, used, suppressed }
  const overflow = new Map();      // reason -> count of refusals from sources we could not track
  let win = { since: now(), used: 0, suppressed: 0 };

  // Emits through the global budget. Returns false when the line was dropped.
  function emit(line, at) {
    if (win.used >= linesPerWindow) { win.suppressed++; return false; }
    win.used++;
    log(line);
    return true;
  }

  // Rolls the global window when it is due, saying what the previous one left out.
  function rollWindow(at) {
    if (at - win.since < windowMs) return;
    const previous = win;
    win = { since: at, used: 0, suppressed: 0 };
    if (previous.suppressed > 0) {
      emit(`[relay] diag suppressed=${previous.suppressed} lines inLastMs=${at - previous.since}`, at);
    }
    if (overflow.size > 0) {
      const parts = [...overflow].map(([reason, n]) => `${reason}:${n}`).join(',');
      overflow.clear();
      emit(`[relay] diag untracked sources (over ${maxKeys} keys) refused=${parts}`, at);
    }
  }

  /**
   * One refused datagram. `reason` is a short fixed word (it is what overflow is counted by),
   * `ip` the source address, `line` the full message to print if the budgets allow it.
   */
  function note(reason, ip, line) {
    const at = now();
    rollWindow(at);
    const key = `${reason}:${ip}`;
    let b = keys.get(key);
    if (!b) {
      if (keys.size >= maxKeys) {
        overflow.set(reason, (overflow.get(reason) || 0) + 1);
        return;
      }
      b = { since: at, used: 0, suppressed: 0 };
      keys.set(key, b);
    } else if (at - b.since >= windowMs) {
      if (b.suppressed > 0) {
        emit(`[relay] ${key} suppressed=${b.suppressed} inLastMs=${at - b.since}`, at);
      }
      b.since = at; b.used = 0; b.suppressed = 0;
    }
    if (b.used >= linesPerKey) { b.suppressed++; return; }
    b.used++;
    emit(line, at);
  }

  /** Forgets keys that have been quiet for two windows, so the cap is spent on live sources. */
  function sweep() {
    const at = now();
    for (const [key, b] of keys) {
      if (at - b.since > 2 * windowMs) keys.delete(key);
    }
    rollWindow(at);
  }

  return { note, sweep, size: () => keys.size, limits: { windowMs, linesPerKey, maxKeys, linesPerWindow } };
}

module.exports = { createRelayDiag };
