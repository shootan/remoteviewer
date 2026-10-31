import io
NL = chr(92) + 'n'
def read(p): return io.open(p, encoding='utf-8').read()
def write(p, s):
    crlf = (b'\r\n' in open(p, 'rb').read())
    io.open(p, 'w', encoding='utf-8', newline=('\r\n' if crlf else '')).write(s)
def rep(path, old, new, count=1):
    old = old.replace('@NL@', NL); new = new.replace('@NL@', NL)
    s = read(path); n = s.count(old)
    assert n == count, f"{path}: expected {count}, found {n} for: {old[:80]!r}"
    write(path, s.replace(old, new)); print("edited", path)

C = 'apps/native_poc/src/d3d_capture_readback.cpp'
# 1) idle: skip the context-lock sweep entirely and drop idle waits from the attribution (Codex P2)
rep(C, """      if (!running_.load(std::memory_order_acquire)) return;

      // Which pending copies has the GPU finished? Checked outside the slot loop so the
      // latest-wins pick sees a consistent snapshot.
      std::vector<uint64_t> seq(slots_.size(), 0);""",
"""      if (!running_.load(std::memory_order_acquire)) return;
      bool anyPending = false;
      for (const auto& s : slots_) {
        if (s.state == SlotState::GpuPending) { anyPending = true; break; }
      }
      if (!anyPending) {
        // Idle (the 1ms wait timed out with nothing submitted): do not take the context lock --
        // sweeping an empty ring only contends with the capture thread -- and do not let the
        // idle-period mutex waits leak into the NEXT frame's attribution (Codex a769ab4 review,
        // WARP probe: a 250ms idle lock hold was reported against a 3ms frame).
        workerCtxWaitAccUs = 0;
        workerD3dCallAccUs = 0;
        continue;  // slotMu_ released by scope exit
      }

      // Which pending copies has the GPU finished? Checked outside the slot loop so the
      // latest-wins pick sees a consistent snapshot.
      std::vector<uint64_t> seq(slots_.size(), 0);""")
# 2) Map: count the call time whether or not it succeeded
rep(C, """      if (SUCCEEDED(context_->Map(slotCopy.staging.Get(), 0, D3D11_MAP_READ, 0, &map))) {
        mapUs = qpc_us() - mapStartUs;
        workerD3dCallAccUs += mapUs;
        mapHeld = true;
      }""",
"""      const HRESULT mapHr = context_->Map(slotCopy.staging.Get(), 0, D3D11_MAP_READ, 0, &map);
      mapUs = qpc_us() - mapStartUs;
      workerD3dCallAccUs += mapUs;  // a failed Map still cost this time
      if (SUCCEEDED(mapHr)) mapHeld = true;""")
# 3) publish-site comment: what the two numbers are (and are not)
rep(C, """    if (handOff) {
      slotCopy.meta.workerCtxWaitUs = workerCtxWaitAccUs;  // `meta` aliases slotCopy.meta
      slotCopy.meta.workerD3dCallUs = workerD3dCallAccUs;""",
"""    if (handOff) {
      // Attribution window = the polls since the last publish (or since the ring went idle) while
      // at least one slot was GpuPending. With several slots in flight the numbers describe that
      // batch, not one frame; a hand-off that fails (Map error, superseded slot) is not reported.
      slotCopy.meta.workerCtxWaitUs = workerCtxWaitAccUs;  // `meta` aliases slotCopy.meta
      slotCopy.meta.workerD3dCallUs = workerD3dCallAccUs;""")

H = 'apps/native_poc/src/d3d_capture_readback.hpp'
rep(H, """  // Worker-side attribution for a slow readback (0.2.98): time spent waiting for the host's own
  // context mutex vs time spent INSIDE D3D calls (GetData / Map / Unmap) between the submit and
  // this publish. Large D3D call time with a small mutex wait means the runtime's internal device
  // lock (held by AcquireNextFrame) is what stalls the worker.""",
"""  // Worker-side attribution for a slow readback (0.2.98): time the worker spent waiting for the
  // host's own context mutex vs time spent INSIDE D3D calls (GetData / Map / Unmap) over the polls
  // that led to this publish, counted only while a slot was GpuPending (idle waits are dropped).
  // Large D3D call time with a small mutex wait is CONSISTENT with the runtime's internal device
  // lock (e.g. held by AcquireNextFrame) stalling the worker -- it narrows the suspect, it is not
  // proof; a wait-stack trace (WPA) is what confirms it.""")

# 4) LUID unknown -> "?" instead of 0:0
D = 'apps/native_poc/src/host_capture_device.cpp'
rep(D, """  char buf[192];
  std::snprintf(buf, sizeof(buf), "[native-video-host] d3d-mt at=%s state=%s dev=%p luid=%ld:%lu@NL@",
                where ? where : "?", d3d_multithread_state_name(d3d_multithread_state(context)),
                static_cast<void*>(device), haveLuid ? luid.HighPart : 0L,
                haveLuid ? static_cast<unsigned long>(luid.LowPart) : 0UL);
  return std::string(buf);""",
"""  char luidBuf[48];
  if (haveLuid) {
    std::snprintf(luidBuf, sizeof(luidBuf), "%ld:%lu", luid.HighPart, static_cast<unsigned long>(luid.LowPart));
  } else {
    std::snprintf(luidBuf, sizeof(luidBuf), "?");
  }
  char buf[192];
  std::snprintf(buf, sizeof(buf), "[native-video-host] d3d-mt at=%s state=%s dev=%p luid=%s@NL@",
                where ? where : "?", d3d_multithread_state_name(d3d_multithread_state(context)),
                static_cast<void*>(device), luidBuf);
  return std::string(buf);""")

# 5) version
rep('apps/native_poc/src/product_version.hpp', 'L"0.2.98"', 'L"0.2.99"')
print("ALL EDITS OK")
