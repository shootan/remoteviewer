import sys
p = sys.argv[1]
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')

def rep(old, new, count=1):
    global s
    assert s.count(old) == count, (s.count(old), old[:120])
    s = s.replace(old, new)

def block(name):
    return open(sys.argv[2] + '\\' + name, 'rb').read().decode('utf-8').replace('\r\n', '\n')

# includes for the receive chain
rep('#include "viewer_window_proc.hpp"\n',
    '#include "viewer_window_proc.hpp"\n'
    '#include "viewer_decoder_state.hpp"  // capture-lifecycle r1 (PS): the product receive chain\n'
    '#include "viewer_frame_gate_state.hpp"\n'
    '#include "viewer_video_receiver.hpp"\n'
    '#include <optional>\n')

# flags
rep('  bool foreignPeer = false;\n  for (int i = 1; i < argc; ++i) {',
    '  bool foreignPeer = false;\n'
    '  bool stillScreen = false;  // capture-lifecycle r1 (PS): a still logon screen picked after the picker\n'
    '  bool noAlias = false;      // ...served as "Default" (a host build without desktop_name_seam.hpp)\n'
    '  for (int i = 1; i < argc; ++i) {')
rep('    if (a == L"--foreign-peer") {',
    '    if (a == L"--still-screen") stillScreen = true;\n'
    '    if (a == L"--no-alias") noAlias = true;\n'
    '    if (a == L"--foreign-peer") {')

# skip the worker sub-runs (r6 V1/V7/V5, r9 W2) in the PS run
rep('  if (!notPrelogin && restricted && hostPort != 0) {',
    '  if (!notPrelogin && !stillScreen && restricted && hostPort != 0) {')

# worker env
rep('''                                  : captureFallback ? L"GNLINK_STREAM_TEST_FORCE_DXGI_FALLBACK_MS=8000"
                                  : notPrelogin ? L""''',
    '''                                  : captureFallback ? L"GNLINK_STREAM_TEST_FORCE_DXGI_FALLBACK_MS=8000"
                                  : (stillScreen && !noAlias) ? L"GNLINK_STREAM_TEST_LOGON_DESKTOP=Winlogon"
                                  : stillScreen ? L""
                                  : notPrelogin ? L""''')
rep('''                                  lockScreen ? L"GNLINK_STREAM_TEST_UNLOCK_PIPE=" + seam.unlockPipe() : std::wstring());''',
    '''                                  lockScreen ? L"GNLINK_STREAM_TEST_UNLOCK_PIPE=" + seam.unlockPipe()
                                  : (stillScreen && !noAlias) ? std::wstring(L"GNLINK_STREAM_TEST_DESKTOP_ALIAS=Winlogon")
                                                              : std::wstring());''')

# state for the receive chain
rep('''  std::thread worker, ingress;
  std::atomic<bool> ingressStop{false};''',
    '''  std::thread worker, ingress;
  std::atomic<bool> ingressStop{false};
  // capture-lifecycle r1 (PS): what the product receiver published, as the renderer would show it.
  struct Shown {
    uint64_t us = 0;
    bool synthetic = false;
    uint32_t lumaSpread = 0, w = 0, h = 0;
  };
  std::vector<Shown> shown;
  std::mutex shownMu;
  std::thread presentThread;
  std::optional<remote60::native_poc::viewer::VideoReceiver> receiver;''')

# receive chain instead of the counter
rep('''    ingress = std::thread([&] {
      std::vector<uint8_t> buf(2048);''',
    block('ps_rx_block.cpp.txt') + '''    ingress = std::thread([&] {
      std::vector<uint8_t> buf(2048);''')
rep('''    connected = wait_until([&] { return media.load() > 0; }, 20000);
    check("video is arriving", connected, std::to_string(media.load()) + " media datagrams");''',
    '''    connected = wait_until([&] { return media.load() > 0; }, 20000);
    check("video is arriving", connected, std::to_string(media.load()) + " media datagrams");
    }  // capture-lifecycle r1: !stillScreen''')

# the control client starts in the picker for PS
rep('    static remote60::native_poc::viewer::ControlClient control(ctx, args, /*startInPicker=*/false);',
    '    static remote60::native_poc::viewer::ControlClient control(ctx, args, /*startInPicker=*/stillScreen);')
rep('    if (connected && !notPrelogin && !sessionEnds && !captureFallback) {\n      {\n        std::lock_guard<std::mutex> lock(ctx.frameBuf.frame.mu);',
    '    if (connected && !notPrelogin && !sessionEnds && !captureFallback && !stillScreen) {\n      {\n        std::lock_guard<std::mutex> lock(ctx.frameBuf.frame.mu);')

# the scenario, right before the first big step block uses `bar`
anchor = '  const bool ready = connected && pump_until([&] { return ctx.control.lastRttUs.load() > 0; }, 25000);'
rep(anchor, block('ps_still_block.cpp.txt') + anchor)

# skip the main-mode steps in the PS run
rep('  if (ready && bar && !notPrelogin && !sessionEnds && !captureFallback) {',
    '  if (ready && bar && !notPrelogin && !sessionEnds && !captureFallback && !stillScreen) {')

# teardown: the present thread
rep('''  // ------------------------------------------------------------------------------- teardown
  ctx.session.running.store(false);
  if (worker.joinable()) worker.join();
  ingressStop.store(true);
  if (ingress.joinable()) ingress.join();''',
    '''  // ------------------------------------------------------------------------------- teardown
  ctx.session.running.store(false);
  if (worker.joinable()) worker.join();
  ingressStop.store(true);
  if (ingress.joinable()) ingress.join();
  if (presentThread.joinable()) presentThread.join();''')

if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok crlf=%s' % crlf)
