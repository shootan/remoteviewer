import io, re
NL = chr(92) + 'n'   # a literal backslash-n for C++ string literals (never type it directly here)
def read(p): return io.open(p, encoding='utf-8').read()
def write(p, s):
    crlf = (b'\r\n' in open(p, 'rb').read())
    io.open(p, 'w', encoding='utf-8', newline=('\r\n' if crlf else '')).write(s)
def rep(path, old, new, count=1):
    old = old.replace('@NL@', NL); new = new.replace('@NL@', NL)
    s = read(path); n = s.count(old)
    assert n == count, f"{path}: expected {count}, found {n} for: {old[:80]!r}"
    write(path, s.replace(old, new)); print("edited", path)

# ---------------------------------------------------------------- 1. DXGI backend: idle sleep outside the acquire
rep('libs/capture/src/capture_backend_dxgi.hpp',
"""  uint32_t acquireTimeoutMs = 100;
""", """  uint32_t acquireTimeoutMs = 100;
  // After DXGI_ERROR_WAIT_TIMEOUT, sleep this long OUTSIDE AcquireNextFrame (no frame held, no
  // device lock) before re-entering it. Desktop duplication holds an internal, unfair D3D11 device
  // lock for the whole call, so on a still desktop an immediate re-entry starves every other user
  // of the device (readback worker, MF encoder). 0 = re-enter immediately (legacy behaviour).
  uint32_t acquireIdleSleepUs = 0;
""")
rep('libs/capture/src/capture_backend_dxgi.cpp',
"""        ++stats.timeouts;
        continue;
      }
      if (hr == DXGI_ERROR_ACCESS_LOST || hr == DXGI_ERROR_ACCESS_DENIED) {""",
"""        ++stats.timeouts;
        // Leave a guaranteed lock-free window before re-entering AcquireNextFrame: the call holds
        // an internal, unfair D3D11 device lock for its whole duration (Sunshine display_base.cpp
        // documents the same starvation and sleeps after a timeout), and once WGC/MF have turned
        // multithread protection on for the shared device, an immediate re-entry starves the
        // readback worker and the encoder -- measured 100-500ms readback waits on a still desktop
        // right after a WGC round trip (GNLink 0.2.98). Success path untouched: a frame is never
        // held here and ReleaseFrame timing is unchanged.
        if (config.acquireIdleSleepUs > 0 && !stopRequested.load()) {
          std::this_thread::sleep_for(std::chrono::microseconds(config.acquireIdleSleepUs));
        }
        continue;
      }
      if (hr == DXGI_ERROR_ACCESS_LOST || hr == DXGI_ERROR_ACCESS_DENIED) {""")

# ---------------------------------------------------------------- 2. device helper: multithread-protection state (read-only) + identity
rep('apps/native_poc/src/host_capture_device.hpp',
"""enum class DeviceAdapterOutputState { HasAttached, None, Unknown };
DeviceAdapterOutputState device_adapter_output_state(ID3D11Device* device);
""", """enum class DeviceAdapterOutputState { HasAttached, None, Unknown };
DeviceAdapterOutputState device_adapter_output_state(ID3D11Device* device);

// D3D11 multithread-protection state of the device's immediate context (ID3D11Multithread, QI on
// the context; a failed QI is Unknown, never Off). Read-only diagnostic (0.2.98): WGC / MF may switch
// it on behind our back, and with it on AcquireNextFrame's internal lock starves every other user of
// the shared device. Never Set it from here -- observe only.
enum class D3dMultithreadState { Off, On, Unknown };
D3dMultithreadState d3d_multithread_state(ID3D11DeviceContext* context);
const char* d3d_multithread_state_name(D3dMultithreadState state);
// One log line "[native-video-host] d3d-mt at=<where> state=<on|off|unknown> dev=<ptr> luid=<hi:lo>"
// (with trailing newline) so the protection flag can be followed across device / backend boundaries
// without mistaking a device swap for a state flip.
std::string d3d_multithread_log_line(const char* where, ID3D11Device* device, ID3D11DeviceContext* context);
""")
rep('apps/native_poc/src/host_capture_device.cpp',
"""#include <d3d11.h>
#include <dxgi1_2.h>
""", """#include <d3d11.h>
#include <d3d11_4.h>
#include <dxgi1_2.h>
""")
s = read('apps/native_poc/src/host_capture_device.cpp')
helper = '''
D3dMultithreadState d3d_multithread_state(ID3D11DeviceContext* context) {
  if (!context) return D3dMultithreadState::Unknown;
  Microsoft::WRL::ComPtr<ID3D11Multithread> mt;
  if (FAILED(context->QueryInterface(IID_PPV_ARGS(&mt))) || !mt) return D3dMultithreadState::Unknown;
  return mt->GetMultithreadProtected() ? D3dMultithreadState::On : D3dMultithreadState::Off;
}

const char* d3d_multithread_state_name(D3dMultithreadState state) {
  switch (state) {
    case D3dMultithreadState::Off: return "off";
    case D3dMultithreadState::On: return "on";
    default: return "unknown";
  }
}

std::string d3d_multithread_log_line(const char* where, ID3D11Device* device, ID3D11DeviceContext* context) {
  LUID luid{};
  bool haveLuid = false;
  if (device) {
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    DXGI_ADAPTER_DESC desc{};
    if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&dxgiDevice))) && dxgiDevice &&
        SUCCEEDED(dxgiDevice->GetAdapter(&adapter)) && adapter && SUCCEEDED(adapter->GetDesc(&desc))) {
      luid = desc.AdapterLuid;
      haveLuid = true;
    }
  }
  char buf[192];
  std::snprintf(buf, sizeof(buf), "[native-video-host] d3d-mt at=%s state=%s dev=%p luid=%ld:%lu@NL@",
                where ? where : "?", d3d_multithread_state_name(d3d_multithread_state(context)),
                static_cast<void*>(device), haveLuid ? luid.HighPart : 0L,
                haveLuid ? static_cast<unsigned long>(luid.LowPart) : 0UL);
  return std::string(buf);
}
'''.replace('@NL@', NL)
anchor = "HRESULT create_d3d11_device_for_primary_monitor("
i = s.index(anchor)
s = s[:i] + helper.lstrip('\n') + '\n' + s[i:]
if '#include <cstdio>' not in s:
    s = s.replace('#include <d3d11.h>\n', '#include <cstdio>\n#include <d3d11.h>\n', 1)
if '#include <string>' not in s:
    s = s.replace('#include <cstdio>\n', '#include <cstdio>\n#include <string>\n', 1)
write('apps/native_poc/src/host_capture_device.cpp', s); print('helper added')

# ---------------------------------------------------------------- 3. host: env knobs + logging at boundaries
S = 'apps/native_poc/src/host_capture_session.cpp'
rep(S, """      config.landscapeOnly = true;
      // Capture-thread side of the cursor forwarder: just stores the latest sample; the main""",
"""      config.landscapeOnly = true;
      // 0.2.98: short acquire timeout + idle sleep outside the call so the duplication's internal
      // device lock is released often on a still desktop (see capture_backend_dxgi.cpp). Both env
      // tunable for the A/B the field test needs: 100/0 = legacy, 8/0 = timeout only, 8/2000 = both.
      config.acquireTimeoutMs = env_u32_clamped("REMOTE60_NATIVE_DXGI_ACQUIRE_TIMEOUT_MS", 8, 1, 1000);
      config.acquireIdleSleepUs = env_u32_clamped("REMOTE60_NATIVE_DXGI_ACQUIRE_IDLE_SLEEP_US", 2000, 0, 100000);
      // Capture-thread side of the cursor forwarder: just stores the latest sample; the main""")
rep(S, """        std::cout << "[native-video-host] desktop_backend=dxgi capture-started=1@NL@";
        return true;""",
"""        std::cout << "[native-video-host] desktop_backend=dxgi capture-started=1 acquireTimeoutMs="
                  << config.acquireTimeoutMs << " acquireIdleSleepUs=" << config.acquireIdleSleepUs
                  << " d3dMt=" << d3d_multithread_state_name(d3d_multithread_state(res.ctx.Get())) << "@NL@";
        return true;""")
rep(S, """    pool = Direct3D11CaptureFramePool::CreateFreeThreaded(""",
"""    std::cout << d3d_multithread_log_line("wgc-pool-before", res.d3d.Get(), res.ctx.Get());
    pool = Direct3D11CaptureFramePool::CreateFreeThreaded(""")
rep(S, """    capture.AttachFrameArrived(res, clientSession, stop, token);
    session.StartCapture();
""", """    capture.AttachFrameArrived(res, clientSession, stop, token);
    session.StartCapture();
    std::cout << d3d_multithread_log_line("wgc-started", res.d3d.Get(), res.ctx.Get());
""")
rep(S, """  session = nullptr;
  pool = nullptr;
}
""", """  const bool hadPool = static_cast<bool>(pool);
  session = nullptr;
  pool = nullptr;
  if (hadPool) std::cout << d3d_multithread_log_line("wgc-closed", res.d3d.Get(), res.ctx.Get());
}
""")
rep(S, """  if (useH264) (void)encoder.codec.set_d3d11_device(res.d3d.Get());
""", """  if (useH264) (void)encoder.codec.set_d3d11_device(res.d3d.Get());
  std::cout << d3d_multithread_log_line("device-recreated", res.d3d.Get(), res.ctx.Get());
""")
# PublishFrame: carry the worker timings
rep(S, """    res.frame.captureUnmapUs = workerMapUs;            // worker Map of the finished copy
""", """    res.frame.captureUnmapUs = workerMapUs;            // worker Map of the finished copy
    res.frame.captureWorkerCtxWaitUs = meta.workerCtxWaitUs;  // worker waits on res.d3dContextMu (0.2.98)
    res.frame.captureWorkerD3dCallUs = meta.workerD3dCallUs;  // worker GetData+Map+Unmap call time (0.2.98)
""")

G = 'apps/native_poc/src/host_startup_graphics.cpp'
rep(G, """  if (useH264) {
    (void)encoder.codec.set_d3d11_device(res.d3d.Get());
  }
  if (capture.gpuScalerRequested) {""",
"""  std::cout << d3d_multithread_log_line("device-created", res.d3d.Get(), res.ctx.Get());
  if (useH264) {
    (void)encoder.codec.set_d3d11_device(res.d3d.Get());
    std::cout << d3d_multithread_log_line("mf-device-set", res.d3d.Get(), res.ctx.Get());
  }
  if (capture.gpuScalerRequested) {""")
rep(G, """  res.d3dDevice = res.inspectable.as<IDirect3DDevice>();
  return 0;""", """  res.d3dDevice = res.inspectable.as<IDirect3DDevice>();
  std::cout << d3d_multithread_log_line("winrt-wrapper", res.d3d.Get(), res.ctx.Get());
  return 0;""")

# ---------------------------------------------------------------- 4. readback worker timing (Codex 13): our-mutex waits vs D3D call time
R = 'apps/native_poc/src/d3d_capture_readback.hpp'
rep(R, """  uint64_t submitCopyUs = 0;
  uint64_t submitUs = 0;
""", """  uint64_t submitCopyUs = 0;
  uint64_t submitUs = 0;
  // Worker-side attribution for a slow readback (0.2.98): time spent waiting for the host's own
  // context mutex vs time spent INSIDE D3D calls (GetData / Map / Unmap) between the submit and
  // this publish. Large D3D call time with a small mutex wait means the runtime's internal device
  // lock (held by AcquireNextFrame) is what stalls the worker.
  uint64_t workerCtxWaitUs = 0;
  uint64_t workerD3dCallUs = 0;
""")
C = 'apps/native_poc/src/d3d_capture_readback.cpp'
rep(C, """  auto pollBackoff = kPollMinUs;
  while (running_.load(std::memory_order_acquire)) {""",
"""  auto pollBackoff = kPollMinUs;
  // Per-publish attribution (0.2.98): accumulated across the polls that lead to one publish.
  uint64_t workerCtxWaitAccUs = 0;
  uint64_t workerD3dCallAccUs = 0;
  while (running_.load(std::memory_order_acquire)) {""")
rep(C, """      {
        std::lock_guard<std::mutex> d3dLock(*contextMu_);
        for (size_t i = 0; i < slots_.size(); ++i) {
          if (slots_[i].state != SlotState::GpuPending || !slots_[i].query) continue;""",
"""      {
        const uint64_t sweepLockStartUs = qpc_us();
        std::lock_guard<std::mutex> d3dLock(*contextMu_);
        workerCtxWaitAccUs += qpc_us() - sweepLockStartUs;
        for (size_t i = 0; i < slots_.size(); ++i) {
          if (slots_[i].state != SlotState::GpuPending || !slots_[i].query) continue;""")
rep(C, """          const HRESULT hr = context_->GetData(slots_[i].query.Get(), &done, sizeof(done),
                                               D3D11_ASYNC_GETDATA_DONOTFLUSH);""",
"""          const uint64_t getDataStartUs = qpc_us();
          const HRESULT hr = context_->GetData(slots_[i].query.Get(), &done, sizeof(done),
                                               D3D11_ASYNC_GETDATA_DONOTFLUSH);
          workerD3dCallAccUs += qpc_us() - getDataStartUs;""")
rep(C, """    {
      std::lock_guard<std::mutex> d3dLock(*contextMu_);
      const uint64_t mapStartUs = qpc_us();""",
"""    {
      const uint64_t mapLockStartUs = qpc_us();
      std::lock_guard<std::mutex> d3dLock(*contextMu_);
      const uint64_t mapStartUs = qpc_us();
      workerCtxWaitAccUs += mapStartUs - mapLockStartUs;""")
rep(C, """        mapUs = qpc_us() - mapStartUs;
        mapHeld = true;""", """        mapUs = qpc_us() - mapStartUs;
        workerD3dCallAccUs += mapUs;
        mapHeld = true;""")
rep(C, """      {
        std::lock_guard<std::mutex> d3dLock(*contextMu_);
        context_->Unmap(slotCopy.staging.Get(), 0);
      }""", """      {
        const uint64_t unmapLockStartUs = qpc_us();
        std::lock_guard<std::mutex> d3dLock(*contextMu_);
        const uint64_t unmapStartUs = qpc_us();
        workerCtxWaitAccUs += unmapStartUs - unmapLockStartUs;
        context_->Unmap(slotCopy.staging.Get(), 0);
        workerD3dCallAccUs += qpc_us() - unmapStartUs;
      }""")
rep(C, """    if (handOff) {
      publish_(std::move(payload), outW, outH, outStride, meta, gpuPendingUs, mapUs, memcpyUs);
    }""", """    if (handOff) {
      slotCopy.meta.workerCtxWaitUs = workerCtxWaitAccUs;  // `meta` aliases slotCopy.meta
      slotCopy.meta.workerD3dCallUs = workerD3dCallAccUs;
      publish_(std::move(payload), outW, outH, outStride, meta, gpuPendingUs, mapUs, memcpyUs);
    }
    workerCtxWaitAccUs = 0;
    workerD3dCallAccUs = 0;""")

# plumbing: frame state -> tick context -> stats -> logs
rep('apps/native_poc/src/host_frame_state.hpp', """  uint64_t captureUnmapUs = 0;
""", """  uint64_t captureUnmapUs = 0;
  uint64_t captureWorkerCtxWaitUs = 0;  // readback worker waits on d3dContextMu (0.2.98)
  uint64_t captureWorkerD3dCallUs = 0;  // readback worker time inside GetData/Map/Unmap (0.2.98)
""")
rep('apps/native_poc/src/host_main_loop.hpp', """  uint64_t captureUnmapUs = 0;
  uint64_t version = 0;""", """  uint64_t captureUnmapUs = 0;
  uint64_t captureWorkerCtxWaitUs = 0;
  uint64_t captureWorkerD3dCallUs = 0;
  uint64_t version = 0;""")
P = 'apps/native_poc/src/host_stage_pop_frame.cpp'
rep(P, """  auto& captureUnmapUs = tc.captureUnmapUs;
""", """  auto& captureUnmapUs = tc.captureUnmapUs;
  auto& captureWorkerCtxWaitUs = tc.captureWorkerCtxWaitUs;
  auto& captureWorkerD3dCallUs = tc.captureWorkerD3dCallUs;
""")
rep(P, """    captureUnmapUs = res.frame.captureUnmapUs;
""", """    captureUnmapUs = res.frame.captureUnmapUs;
    captureWorkerCtxWaitUs = res.frame.captureWorkerCtxWaitUs;
    captureWorkerD3dCallUs = res.frame.captureWorkerD3dCallUs;
""")
rep(P, """    stats.captureUnmapSumUs += captureUnmapUs;
    stats.captureUnmapMaxUs = std::max(stats.captureUnmapMaxUs, captureUnmapUs);
  }""", """    stats.captureUnmapSumUs += captureUnmapUs;
    stats.captureUnmapMaxUs = std::max(stats.captureUnmapMaxUs, captureUnmapUs);
    stats.captureWorkerCtxWaitSumUs += captureWorkerCtxWaitUs;
    stats.captureWorkerCtxWaitMaxUs = std::max(stats.captureWorkerCtxWaitMaxUs, captureWorkerCtxWaitUs);
    stats.captureWorkerD3dCallSumUs += captureWorkerD3dCallUs;
    stats.captureWorkerD3dCallMaxUs = std::max(stats.captureWorkerD3dCallMaxUs, captureWorkerD3dCallUs);
  }""")
rep('apps/native_poc/src/host_stats.hpp', """  uint64_t captureUnmapSumUs = 0;
  uint64_t captureUnmapMaxUs = 0;
""", """  uint64_t captureUnmapSumUs = 0;
  uint64_t captureUnmapMaxUs = 0;
  uint64_t captureWorkerCtxWaitSumUs = 0;  // 0.2.98 readback attribution
  uint64_t captureWorkerCtxWaitMaxUs = 0;
  uint64_t captureWorkerD3dCallSumUs = 0;
  uint64_t captureWorkerD3dCallMaxUs = 0;
""")
rep('apps/native_poc/src/host_stage_stats.cpp', """    stats.captureUnmapSumUs = 0;
    stats.captureUnmapMaxUs = 0;
""", """    stats.captureUnmapSumUs = 0;
    stats.captureUnmapMaxUs = 0;
    stats.captureWorkerCtxWaitSumUs = 0;
    stats.captureWorkerCtxWaitMaxUs = 0;
    stats.captureWorkerD3dCallSumUs = 0;
    stats.captureWorkerD3dCallMaxUs = 0;
""")
T = 'apps/native_poc/src/host_stage_stats_h264.cpp'
rep(T, """  const uint64_t captureUnmapAvgUs =
      (stats.captureReadbackSamples > 0) ? (stats.captureUnmapSumUs / stats.captureReadbackSamples) : 0;
""", """  const uint64_t captureUnmapAvgUs =
      (stats.captureReadbackSamples > 0) ? (stats.captureUnmapSumUs / stats.captureReadbackSamples) : 0;
  const uint64_t captureWorkerCtxWaitAvgUs =
      (stats.captureReadbackSamples > 0) ? (stats.captureWorkerCtxWaitSumUs / stats.captureReadbackSamples) : 0;
  const uint64_t captureWorkerD3dCallAvgUs =
      (stats.captureReadbackSamples > 0) ? (stats.captureWorkerD3dCallSumUs / stats.captureReadbackSamples) : 0;
""")
rep(T, """            << " captureUnmapAvgUs=" << captureUnmapAvgUs
            << " captureUnmapMaxUs=" << stats.captureUnmapMaxUs
""", """            << " captureUnmapAvgUs=" << captureUnmapAvgUs
            << " captureUnmapMaxUs=" << stats.captureUnmapMaxUs
            << " workerCtxWaitAvgUs=" << captureWorkerCtxWaitAvgUs
            << " workerCtxWaitMaxUs=" << stats.captureWorkerCtxWaitMaxUs
            << " workerD3dCallAvgUs=" << captureWorkerD3dCallAvgUs
            << " workerD3dCallMaxUs=" << stats.captureWorkerD3dCallMaxUs
            << " d3dMt=" << d3d_multithread_state_name(d3d_multithread_state(res.ctx.Get()))
""")
A = 'apps/native_poc/src/host_stage_encode_send_h264_au.cpp'
s = read(A)
old = '            << " captureUnmapUs=" << captureUnmapUs\n'
n = s.count(old); assert n == 2, n
s = s.replace(old, old + '            << " workerCtxWaitUs=" << tc.captureWorkerCtxWaitUs\n            << " workerD3dCallUs=" << tc.captureWorkerD3dCallUs\n')
write(A, s); print('feedback worker fields added')

# ---------------------------------------------------------------- 5. version
rep('apps/native_poc/src/product_version.hpp', 'L"0.2.97"', 'L"0.2.98"')
print("ALL EDITS OK")
