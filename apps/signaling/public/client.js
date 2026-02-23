(function () {
  'use strict';

  const statusEl = document.getElementById('status');
  const statsEl = document.getElementById('stats');
  const videoEl = document.getElementById('remoteVideo');
  const signalingUrlEl = document.getElementById('signalingUrl');
  const tokenEl = document.getElementById('token');
  const iceServersEl = document.getElementById('iceServers');
  const connectBtn = document.getElementById('connectBtn');
  const disconnectBtn = document.getElementById('disconnectBtn');
  const refreshWindowsBtn = document.getElementById('refreshWindowsBtn');
  const selectDesktopBtn = document.getElementById('selectDesktopBtn');
  const windowListEl = document.getElementById('windowList');
  const selectedWindowInfoEl = document.getElementById('selectedWindowInfo');

  const stats = {
    wsState: 'idle',
    registered: false,
    peerOnline: false,
    pcState: 'new',
    videoTracks: 0,
    audioTracks: 0,
    inputChannel: 'closed',
    inputSent: 0,
    inputAck: 0,
    inputNack: 0,
    videoBytesReceived: 0,
    videoPacketsReceived: 0,
    videoFramesDecoded: 0,
    videoResolution: '0x0',
    videoReadyState: 0,
  };

  let ws = null;
  let pc = null;
  let remoteStream = null;
  let inputDc = null;
  let offerStarted = false;
  let offerRetryTimer = null;
  let pendingLocalCandidates = [];
  let inputPingTimer = null;
  let statsPollTimer = null;
  let lastMouseMoveTs = 0;
  let disconnectTimer = null;
  let disconnectSeq = 0;
  let windowSwitchReconnectTimer = null;

  let windowTargets = [];
  let selectedWindowId = '0';
  let selectedWindowTitle = 'desktop';

  function debugLog(msg) {
    try {
      console.log(`[remote60-client][${new Date().toISOString()}] ${msg}`);
    } catch (_) {}
  }

  function asWindowId(v) {
    const s = String(v == null ? '' : v).trim();
    if (!s) return '0';
    return /^\d+$/.test(s) ? s : '0';
  }

  function setStatus(text, ok) {
    statusEl.textContent = text;
    statusEl.classList.toggle('ok', !!ok);
  }

  function updateStats() {
    statsEl.textContent =
      `wsState=${stats.wsState}\n` +
      `registered=${stats.registered}\n` +
      `peerOnline=${stats.peerOnline}\n` +
      `pcState=${stats.pcState}\n` +
      `videoTracks=${stats.videoTracks}\n` +
      `audioTracks=${stats.audioTracks}\n` +
      `inputChannel=${stats.inputChannel}\n` +
      `inputSent=${stats.inputSent}\n` +
      `inputAck=${stats.inputAck}\n` +
      `inputNack=${stats.inputNack}\n` +
      `videoBytesReceived=${stats.videoBytesReceived}\n` +
      `videoPacketsReceived=${stats.videoPacketsReceived}\n` +
      `videoFramesDecoded=${stats.videoFramesDecoded}\n` +
      `videoResolution=${stats.videoResolution}\n` +
      `videoReadyState=${stats.videoReadyState}\n` +
      `selectedWindowId=${selectedWindowId}\n`;
  }

  function defaultWsUrl() {
    const scheme = location.protocol === 'https:' ? 'wss' : 'ws';
    return `${scheme}://${location.host}`;
  }

  function parseIceServers(raw) {
    const parts = String(raw || '')
      .split(/[\n,;]+/g)
      .map((s) => s.trim())
      .filter((s) => s.length > 0);
    return parts.map((url) => ({ urls: url }));
  }

  function normalizeWsUrl(raw) {
    const t = String(raw || '').trim();
    if (!t) return defaultWsUrl();
    if (t.startsWith('http://')) return `ws://${t.slice('http://'.length)}`;
    if (t.startsWith('https://')) return `wss://${t.slice('https://'.length)}`;
    if (t.startsWith('ws://') || t.startsWith('wss://')) return t;
    return `ws://${t}`;
  }

  function savePrefs() {
    try {
      localStorage.setItem('remote60.token', tokenEl.value);
      localStorage.setItem('remote60.iceServers', iceServersEl.value);
    } catch (_) {}
  }

  function loadPrefs() {
    try {
      signalingUrlEl.value = normalizeWsUrl(defaultWsUrl());
      tokenEl.value = localStorage.getItem('remote60.token') || '';
      iceServersEl.value = localStorage.getItem('remote60.iceServers') || '';
    } catch (_) {
      signalingUrlEl.value = defaultWsUrl();
    }
  }

  function updateSelectedWindowInfo() {
    selectedWindowInfoEl.textContent = `Selected target: ${selectedWindowTitle} (${selectedWindowId})`;
  }

  function renderWindowTargets() {
    if (!windowListEl) return;
    windowListEl.innerHTML = '';
    if (!windowTargets || windowTargets.length === 0) {
      const empty = document.createElement('div');
      empty.className = 'help';
      empty.textContent = 'No windows yet. Connect first, then click Refresh.';
      windowListEl.appendChild(empty);
      return;
    }

    windowTargets.forEach((w) => {
      const id = asWindowId(w.id);
      const btn = document.createElement('button');
      btn.type = 'button';
      btn.className = `window-item${id === selectedWindowId ? ' active' : ''}`;

      const title = document.createElement('div');
      title.className = 'title';
      title.textContent = String(w.title || '(untitled)');
      btn.appendChild(title);

      const meta = document.createElement('div');
      meta.className = 'meta';
      const minTag = w.minimized ? 'minimized' : 'normal';
      meta.textContent = `id=${id} pid=${w.pid || 0} ${w.width || 0}x${w.height || 0} ${minTag}`;
      btn.appendChild(meta);

      btn.addEventListener('click', () => {
        selectWindow(id);
      });
      windowListEl.appendChild(btn);
    });
  }

  function resetState() {
    offerStarted = false;
    if (offerRetryTimer) {
      clearInterval(offerRetryTimer);
      offerRetryTimer = null;
    }
    pendingLocalCandidates = [];
    stats.wsState = 'idle';
    stats.registered = false;
    stats.peerOnline = false;
    stats.pcState = 'new';
    stats.videoTracks = 0;
    stats.audioTracks = 0;
    stats.inputChannel = 'closed';
    stats.inputSent = 0;
    stats.inputAck = 0;
    stats.inputNack = 0;
    stats.videoBytesReceived = 0;
    stats.videoPacketsReceived = 0;
    stats.videoFramesDecoded = 0;
    stats.videoResolution = '0x0';
    stats.videoReadyState = 0;
    updateStats();
  }

  function disconnectNow() {
    debugLog(`disconnectNow begin seq=${disconnectSeq} ws=${ws ? ws.readyState : 'null'} pc=${pc ? pc.connectionState : 'null'} dc=${inputDc ? inputDc.readyState : 'null'}`);
    if (disconnectTimer) {
      clearTimeout(disconnectTimer);
      disconnectTimer = null;
    }
    if (inputPingTimer) {
      clearInterval(inputPingTimer);
      inputPingTimer = null;
    }
    if (statsPollTimer) {
      clearInterval(statsPollTimer);
      statsPollTimer = null;
    }
    if (windowSwitchReconnectTimer) {
      clearTimeout(windowSwitchReconnectTimer);
      windowSwitchReconnectTimer = null;
    }
    if (inputDc) {
      try { inputDc.close(); } catch (_) {}
      inputDc = null;
    }
    if (pc) {
      try { pc.close(); } catch (_) {}
      pc = null;
    }
    if (ws) {
      try { ws.close(); } catch (_) {}
      ws = null;
    }
    remoteStream = null;
    videoEl.srcObject = null;
    resetState();
    setStatus('disconnected', false);
    debugLog(`disconnectNow end seq=${disconnectSeq}`);
  }

  function disconnect() {
    disconnectSeq += 1;
    debugLog(`disconnect click seq=${disconnectSeq}`);
    if (disconnectTimer) return;
    let sentSessionEnd = false;
    try {
      if (inputDc && inputDc.readyState === 'open') {
        inputDc.send('session_end');
        debugLog(`session_end sent via datachannel seq=${disconnectSeq}`);
        sentSessionEnd = true;
      }
    } catch (_) {}
    if (!sentSessionEnd) {
      debugLog(`session_end not sent (dc not open) seq=${disconnectSeq}, fallback=disconnectNow`);
      disconnectNow();
      return;
    }
    disconnectTimer = setTimeout(() => {
      debugLog(`disconnect timeout fired seq=${disconnectSeq}`);
      disconnectTimer = null;
      disconnectNow();
    }, 140);
  }

  function sendSignal(payload) {
    if (!ws || ws.readyState !== WebSocket.OPEN) return false;
    ws.send(JSON.stringify(payload));
    return true;
  }

  function canSignalMedia() {
    return !!ws && ws.readyState === WebSocket.OPEN && stats.registered;
  }

  function sendControlMessage(payload) {
    if (!inputDc || inputDc.readyState !== 'open') return false;
    try {
      inputDc.send(JSON.stringify(payload));
      return true;
    } catch (_) {
      return false;
    }
  }

  function requestWindowList() {
    if (!sendControlMessage({ type: 'window_list_request' })) {
      setStatus('waiting_input_channel_for_window_list', false);
      return;
    }
    debugLog('window_list_request sent');
  }

  function scheduleReconnectForWindowSwitch() {
    if (windowSwitchReconnectTimer) {
      clearTimeout(windowSwitchReconnectTimer);
      windowSwitchReconnectTimer = null;
    }
    windowSwitchReconnectTimer = setTimeout(() => {
      windowSwitchReconnectTimer = null;
      connect();
    }, 1000);
  }

  function selectWindow(windowId) {
    const id = asWindowId(windowId);
    if (!sendControlMessage({ type: 'window_select', windowId: id })) {
      setStatus('window_select_failed: input_channel_not_open', false);
      return;
    }
    setStatus(`window_select_requested id=${id}`, false);
    debugLog(`window_select sent id=${id}`);
  }

  function mapPointerToRemote(e) {
    const rect = videoEl.getBoundingClientRect();
    if (!rect || rect.width <= 1 || rect.height <= 1) return null;
    const rx = Math.max(0, Math.min(1, (e.clientX - rect.left) / rect.width));
    const ry = Math.max(0, Math.min(1, (e.clientY - rect.top) / rect.height));
    const vw = Math.max(1, videoEl.videoWidth || 1920);
    const vh = Math.max(1, videoEl.videoHeight || 1080);
    return {
      x: Math.round(rx * (vw - 1)),
      y: Math.round(ry * (vh - 1)),
    };
  }

  function sendInputEvent(ev) {
    if (!inputDc || inputDc.readyState !== 'open') return;
    inputDc.send(JSON.stringify({
      type: ev.type || 'unknown',
      keyCode: ev.keyCode || 0,
      x: ev.x || 0,
      y: ev.y || 0,
      button: ev.button || 0,
      wheelDelta: ev.wheelDelta || 0,
    }));
    stats.inputSent += 1;
    updateStats();
  }

  async function maybeStartOffer() {
    if (!pc || offerStarted || !stats.registered || !stats.peerOnline) return;
    offerStarted = true;
    try {
      const offer = await pc.createOffer();
      await pc.setLocalDescription(offer);
      const ok = sendSignal({ type: 'offer', sdp: offer.sdp });
      if (!ok) {
        offerStarted = false;
      }
    } catch (e) {
      offerStarted = false;
      setStatus(`offer_failed: ${String(e && e.message ? e.message : e)}`, false);
    }
  }

  async function pollWebRtcStats() {
    if (!pc) return;
    let bytes = 0;
    let packets = 0;
    let decoded = 0;
    try {
      const report = await pc.getStats();
      report.forEach((s) => {
        if (s && s.type === 'inbound-rtp' && s.kind === 'video') {
          bytes += Number(s.bytesReceived || 0);
          packets += Number(s.packetsReceived || 0);
          decoded += Number(s.framesDecoded || 0);
        }
      });
    } catch (_) {}
    stats.videoBytesReceived = bytes;
    stats.videoPacketsReceived = packets;
    stats.videoFramesDecoded = decoded;
    stats.videoResolution = `${videoEl.videoWidth || 0}x${videoEl.videoHeight || 0}`;
    stats.videoReadyState = Number(videoEl.readyState || 0);
    updateStats();
  }

  function setupInputHandlers() {
    const sendMoveFromEvent = (e) => {
      const now = Date.now();
      if (now - lastMouseMoveTs < 16) return;
      lastMouseMoveTs = now;
      const p = mapPointerToRemote(e);
      if (!p) return;
      sendInputEvent({ type: 'mouse_move', x: p.x, y: p.y });
    };
    const sendMoveFromTouch = (t) => {
      if (!t) return;
      sendMoveFromEvent({ clientX: t.clientX, clientY: t.clientY });
    };

    videoEl.addEventListener('contextmenu', (e) => e.preventDefault());
    videoEl.addEventListener('click', () => {
      videoEl.focus();
    });
    videoEl.addEventListener('mousemove', (e) => {
      sendMoveFromEvent(e);
    });
    videoEl.addEventListener('mousedown', (e) => {
      const p = mapPointerToRemote(e);
      if (p) sendInputEvent({ type: 'mouse_move', x: p.x, y: p.y });
      sendInputEvent({ type: 'mouse_button_down', button: e.button | 0 });
      e.preventDefault();
      videoEl.focus();
    });
    videoEl.addEventListener('mouseup', (e) => {
      const p = mapPointerToRemote(e);
      if (p) sendInputEvent({ type: 'mouse_move', x: p.x, y: p.y });
      sendInputEvent({ type: 'mouse_button_up', button: e.button | 0 });
      e.preventDefault();
    });
    videoEl.addEventListener('wheel', (e) => {
      const p = mapPointerToRemote(e);
      if (p) sendInputEvent({ type: 'mouse_move', x: p.x, y: p.y });
      const wheelDelta = (e.deltaY < 0) ? 120 : -120;
      sendInputEvent({ type: 'mouse_wheel', wheelDelta });
      e.preventDefault();
    }, { passive: false });
    videoEl.addEventListener('keydown', (e) => {
      const keyCode = e.keyCode || e.which || 0;
      sendInputEvent({ type: 'key_down', keyCode });
      e.preventDefault();
    });
    videoEl.addEventListener('keyup', (e) => {
      const keyCode = e.keyCode || e.which || 0;
      sendInputEvent({ type: 'key_up', keyCode });
      e.preventDefault();
    });

    videoEl.addEventListener('touchstart', (e) => {
      if (e.touches && e.touches.length > 0) {
        sendMoveFromTouch(e.touches[0]);
      }
      sendInputEvent({ type: 'mouse_button_down', button: 0 });
      videoEl.focus();
      e.preventDefault();
    }, { passive: false });
    videoEl.addEventListener('touchmove', (e) => {
      if (e.touches && e.touches.length > 0) {
        sendMoveFromTouch(e.touches[0]);
      }
      e.preventDefault();
    }, { passive: false });
    videoEl.addEventListener('touchend', (e) => {
      if (e.changedTouches && e.changedTouches.length > 0) {
        sendMoveFromTouch(e.changedTouches[0]);
      }
      sendInputEvent({ type: 'mouse_button_up', button: 0 });
      e.preventDefault();
    }, { passive: false });
  }

  function handleDataChannelJsonMessage(msg) {
    if (!msg || typeof msg !== 'object' || typeof msg.type !== 'string') return false;

    if (msg.type === 'input_ack') {
      stats.inputAck += 1;
      updateStats();
      return true;
    }
    if (msg.type === 'input_nack') {
      stats.inputNack += 1;
      updateStats();
      return true;
    }
    if (msg.type === 'window_list') {
      const list = Array.isArray(msg.windows) ? msg.windows : [];
      windowTargets = list.map((w) => ({
        id: asWindowId(w && w.id),
        title: String((w && w.title) || '(untitled)'),
        pid: Number((w && w.pid) || 0),
        width: Number((w && w.width) || 0),
        height: Number((w && w.height) || 0),
        minimized: !!(w && w.minimized),
      }));
      const sid = asWindowId(msg.selectedWindowId);
      selectedWindowId = sid;
      if (sid === '0') {
        selectedWindowTitle = 'desktop';
      } else {
        const hit = windowTargets.find((x) => x.id === sid);
        selectedWindowTitle = hit ? hit.title : `window-${sid}`;
      }
      updateSelectedWindowInfo();
      renderWindowTargets();
      setStatus(`window_list_received count=${windowTargets.length}`, true);
      return true;
    }
    if (msg.type === 'window_selected') {
      const ok = !!msg.ok;
      const id = asWindowId(msg.windowId);
      const reason = String(msg.reason || '');
      const title = String(msg.title || '');
      if (!ok) {
        setStatus(`window_select_failed: ${reason}`, false);
        return true;
      }
      selectedWindowId = id;
      selectedWindowTitle = id === '0' ? 'desktop' : (title || `window-${id}`);
      updateSelectedWindowInfo();
      renderWindowTargets();
      setStatus(`window_selected: ${selectedWindowTitle}`, true);
      if (msg.restartRequested) {
        setStatus(`window_selected: ${selectedWindowTitle}, reconnecting...`, false);
        scheduleReconnectForWindowSwitch();
      }
      return true;
    }

    return false;
  }

  function setupPeer() {
    remoteStream = new MediaStream();
    videoEl.srcObject = remoteStream;
    videoEl.muted = true;
    videoEl.autoplay = true;
    videoEl.playsInline = true;
    pc = new RTCPeerConnection({
      iceServers: parseIceServers(iceServersEl.value),
    });
    const videoTransceiver = pc.addTransceiver('video', { direction: 'recvonly' });
    pc.addTransceiver('audio', { direction: 'recvonly' });
    const tuneReceiverLowLatency = () => {
      try {
        const receivers = (pc && typeof pc.getReceivers === 'function') ? pc.getReceivers() : [];
        receivers.forEach((r) => {
          if (!r || !r.track || r.track.kind !== 'video') return;
          if ('playoutDelayHint' in r) {
            r.playoutDelayHint = 0.0;
          }
          if ('jitterBufferTarget' in r) {
            r.jitterBufferTarget = 0;
          }
        });
      } catch (_) {}
    };

    try {
      if (videoTransceiver && typeof videoTransceiver.setCodecPreferences === 'function' &&
          typeof RTCRtpReceiver !== 'undefined' &&
          typeof RTCRtpReceiver.getCapabilities === 'function') {
        const caps = RTCRtpReceiver.getCapabilities('video');
        const codecs = (caps && Array.isArray(caps.codecs)) ? caps.codecs : [];
        const h264 = codecs.filter((c) => String(c.mimeType || '').toLowerCase() === 'video/h264');
        if (h264.length > 0) {
          videoTransceiver.setCodecPreferences(h264);
        }
      }
    } catch (_) {}
    tuneReceiverLowLatency();

    inputDc = pc.createDataChannel('input');
    inputDc.onopen = () => {
      stats.inputChannel = 'open';
      updateStats();
      try { inputDc.send('input_ping'); } catch (_) {}
      requestWindowList();
      if (inputPingTimer) clearInterval(inputPingTimer);
      inputPingTimer = setInterval(() => {
        if (inputDc && inputDc.readyState === 'open') {
          try { inputDc.send('input_ping'); } catch (_) {}
        }
      }, 3000);
      if (statsPollTimer) clearInterval(statsPollTimer);
      statsPollTimer = setInterval(() => {
        pollWebRtcStats();
      }, 1000);
    };
    inputDc.onclose = () => {
      stats.inputChannel = 'closed';
      updateStats();
    };
    inputDc.onmessage = (ev) => {
      const text = String(ev && ev.data ? ev.data : '');
      let msg = null;
      if (text.startsWith('{') && text.endsWith('}')) {
        try {
          msg = JSON.parse(text);
        } catch (_) {
          msg = null;
        }
      }
      if (msg && handleDataChannelJsonMessage(msg)) return;
      if (text.indexOf('input_ack') >= 0) stats.inputAck += 1;
      if (text.indexOf('input_nack') >= 0) stats.inputNack += 1;
      updateStats();
    };

    pc.ontrack = (ev) => {
      if (ev.track.kind === 'video') {
        stats.videoTracks += 1;
        // Keep playback video-only to avoid A/V sync buffering side-effects.
        videoEl.srcObject = new MediaStream([ev.track]);
      } else if (ev.track.kind === 'audio') {
        stats.audioTracks += 1;
      } else if (remoteStream) {
        remoteStream.addTrack(ev.track);
      }
      const p = videoEl.play();
      if (p && typeof p.catch === 'function') p.catch(() => {});
      tuneReceiverLowLatency();
      updateStats();
    };

    pc.onconnectionstatechange = () => {
      stats.pcState = pc.connectionState;
      setStatus(`pc=${pc.connectionState}`, pc.connectionState === 'connected');
      updateStats();
    };

    pc.onicecandidate = (ev) => {
      if (!ev.candidate) return;
      const payload = {
        type: 'ice',
        candidate: ev.candidate.candidate || '',
      };
      if (ev.candidate.sdpMid != null) payload.candidateMid = ev.candidate.sdpMid;
      if (ev.candidate.sdpMLineIndex != null) payload.candidateMLineIndex = ev.candidate.sdpMLineIndex;
      if (canSignalMedia()) {
        sendSignal(payload);
      } else {
        pendingLocalCandidates.push(payload);
      }
    };
  }

  function flushPendingCandidates() {
    if (!canSignalMedia() || pendingLocalCandidates.length === 0) return;
    for (const c of pendingLocalCandidates) {
      sendSignal(c);
    }
    pendingLocalCandidates = [];
  }

  async function handleSignalMessage(msg) {
    if (!msg || typeof msg !== 'object') return;
    if (msg.type === 'hello') {
      const token = String(tokenEl.value || '').trim();
      const reg = { type: 'register', role: 'client' };
      if (token.length > 0) reg.authToken = token;
      sendSignal(reg);
      return;
    }
    if (msg.type === 'registered') {
      stats.registered = true;
      updateStats();
      flushPendingCandidates();
      await maybeStartOffer();
      return;
    }
    if (msg.type === 'peer_state') {
      stats.peerOnline = !!msg.peerOnline;
      updateStats();
      if (!stats.peerOnline) {
        offerStarted = false;
        setStatus('waiting_for_host', false);
      }
      await maybeStartOffer();
      return;
    }
    if (msg.type === 'answer' && pc && typeof msg.sdp === 'string' && msg.sdp.startsWith('v=0')) {
      try {
        await pc.setRemoteDescription({ type: 'answer', sdp: msg.sdp });
      } catch (e) {
        setStatus(`setRemoteDescription_failed: ${String(e && e.message ? e.message : e)}`, false);
      }
      return;
    }
    if (msg.type === 'ice' && pc && typeof msg.candidate === 'string' && msg.candidate.length > 0) {
      const cand = {
        candidate: msg.candidate,
        sdpMid: (typeof msg.candidateMid === 'string') ? msg.candidateMid : null,
      };
      if (Number.isInteger(msg.candidateMLineIndex)) cand.sdpMLineIndex = msg.candidateMLineIndex;
      try {
        await pc.addIceCandidate(cand);
      } catch (_) {}
      return;
    }
    if (msg.type === 'error') {
      if (msg.code === 'E_PEER_NOT_CONNECTED') {
        offerStarted = false;
        stats.peerOnline = false;
        updateStats();
        setStatus('waiting_for_host', false);
        return;
      }
      setStatus(`signal_error: ${msg.code || 'unknown'} ${msg.reason || ''}`, false);
      return;
    }
  }

  function connect() {
    debugLog('connect click');
    disconnectNow();
    savePrefs();
    setupPeer();

    const wsUrl = normalizeWsUrl(signalingUrlEl.value);
    signalingUrlEl.value = wsUrl;
    ws = new WebSocket(wsUrl);
    stats.wsState = 'connecting';
    updateStats();
    setStatus('ws=connecting', false);

    ws.onopen = () => {
      debugLog('ws onopen');
      stats.wsState = 'open';
      updateStats();
      setStatus('ws=open', false);
      if (offerRetryTimer) clearInterval(offerRetryTimer);
      offerRetryTimer = setInterval(() => {
        maybeStartOffer();
      }, 1000);
    };
    ws.onmessage = async (ev) => {
      let msg = null;
      try {
        msg = JSON.parse(String(ev.data || '{}'));
      } catch (_) {
        return;
      }
      await handleSignalMessage(msg);
    };
    ws.onclose = () => {
      debugLog('ws onclose');
      stats.wsState = 'closed';
      stats.registered = false;
      if (offerRetryTimer) {
        clearInterval(offerRetryTimer);
        offerRetryTimer = null;
      }
      updateStats();
      setStatus('ws=closed', false);
    };
    ws.onerror = () => {
      debugLog('ws onerror');
      setStatus('ws=error', false);
    };
  }

  connectBtn.addEventListener('click', () => connect());
  disconnectBtn.addEventListener('click', () => disconnect());
  refreshWindowsBtn.addEventListener('click', () => requestWindowList());
  selectDesktopBtn.addEventListener('click', () => selectWindow('0'));
  window.addEventListener('beforeunload', () => disconnect());

  loadPrefs();
  setupInputHandlers();
  updateSelectedWindowInfo();
  renderWindowTargets();
  updateStats();
})();
