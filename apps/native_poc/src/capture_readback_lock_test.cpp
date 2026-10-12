// encoder-rc-budget r12: the lock-order counterexamples the fourth Home run's dumps demanded.
//
// What the dumps showed (23 of 23): the WGC FrameArrived thread owns the D3D11 device's critical
// section and waits for one of the readback ring's std::mutexes inside Submit; the readback worker
// holds those mutexes and waits for the device CS inside GetData. This test builds that shape on
// purpose with a real D3D11 device (WARP, multithread-protected, so ID3D11Multithread::Enter IS the
// device CS the runtime's own calls take) and checks the three r12 contracts:
//   [1]  a thread that owns the device CS and calls the callback entry (FrameSignalCollector::Signal)
//        returns at once while another thread is blocked inside Submit's D3D work on that CS;
//   [1n] negative control, in a child process with a bounded wait: the pre-r12 shape -- the CS owner
//        calls Submit directly -- does deadlock (child never exits; the parent kills it);
//   [2]  while the worker's GetData waits for the device CS, slotMu_ is free (SetOutputSize returns);
//        pre-r12 the worker held slotMu_ across the sweep and this blocked;
//   [3]  the diagnostic readers return without waiting while the worker holds slotMu_, and they
//        report the pending slot as pending with a growing age (never 0 while frozen);
//   [4]  ownership: a reconfigure or a superseding Submit between the worker's probe and its commit
//        leaves the old result unapplied -- the old frame is never published, the new one once;
//   [5]  FrameSignalCollector: lost-wake (a signal during work is followed by another drain), late
//        signals after Close are ignored, Stop is bounded while work is running, Signal after Stop is
//        safe through the shared_ptr the delegate would hold;
//   [6]  (r13) the whole handoff end to end: a producer fills a fake pool and signals the collector
//        while the collector's worker drains latest-wins and Submits into the real ring -- the LAST
//        pixel value is what the ring publishes, exactly once, and no later signal arrives on its own;
//   [7]  (r13, Codex C2) the diagnostic view: a writer stuck mid-update reads as Inconsistent (values
//        kept, not zeroed), a generation change is visible in the same view, a failed slot rebuild
//        (texture create failure) refreshes the snapshot instead of leaving the old generation's
//        pending count behind, and the legacy getters agree with the view;
//   [8]  (r13, Codex C3) collector start failure: event or thread failure -> Start() false, nothing
//        runs, Signal is ignored, Stop/destruction are clean.
// Every wait in this test is bounded; a check that cannot complete is a FAIL, not a hang.

#include <windows.h>
#include <d3d11.h>
#include <d3d11_4.h>
#include <wrl/client.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "d3d_capture_readback.hpp"
#include "frame_signal_collector.hpp"

using namespace remote60::native_poc;
using Microsoft::WRL::ComPtr;

namespace {

int gPass = 0, gFail = 0;
void check(bool ok, const char* what, const std::string& detail = "") {
  std::printf("%s %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : " -- ", detail.c_str());
  (ok ? gPass : gFail)++;
}

struct Device {
  ComPtr<ID3D11Device> dev;
  ComPtr<ID3D11DeviceContext> ctx;
  ComPtr<ID3D11Multithread> mt;
  bool create() {
    D3D_FEATURE_LEVEL level{};
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &dev, &level, &ctx)))
      return false;
    if (FAILED(ctx.As(&mt)) || !mt) return false;
    mt->SetMultithreadProtected(TRUE);  // the product runs d3d-mt on; Enter() is then the device CS
    return true;
  }
  ComPtr<ID3D11Texture2D> source(uint32_t w, uint32_t h, uint32_t pixel) {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = w; desc.Height = h; desc.MipLevels = 1; desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count = 1; desc.Usage = D3D11_USAGE_DEFAULT;
    std::vector<uint32_t> px(static_cast<size_t>(w) * h, pixel);
    D3D11_SUBRESOURCE_DATA init{px.data(), w * 4, 0};
    ComPtr<ID3D11Texture2D> t;
    if (FAILED(dev->CreateTexture2D(&desc, &init, &t))) return nullptr;
    return t;
  }
};

// Runs fn on a thread and reports whether it returned within `ms`. The thread is detached when it
// did not: the caller then releases whatever blocked it before the process exits.
template <class F>
bool returns_within(F fn, DWORD ms, std::thread* keep = nullptr) {
  auto done = std::make_shared<std::atomic<bool>>(false);
  std::thread t([fn, done] { fn(); done->store(true, std::memory_order_release); });
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
  while (!done->load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  const bool ok = done->load(std::memory_order_acquire);
  if (keep) *keep = std::move(t); else if (ok) t.join(); else t.detach();
  return ok;
}

struct Published {
  std::mutex mu;
  std::condition_variable cv;
  std::vector<uint64_t> captureUs;
  std::vector<uint8_t> firstByte;
  D3dCaptureReadbackPipeline::PublishFn fn() {
    return [this](std::shared_ptr<std::vector<uint8_t>> bytes, uint32_t, uint32_t, uint32_t, const CaptureFrameMeta& meta, uint64_t, uint64_t, uint64_t) {
      std::lock_guard<std::mutex> lk(mu);
      captureUs.push_back(meta.captureUs);
      firstByte.push_back(bytes->empty() ? 0 : bytes->at(0));
      cv.notify_all();
    };
  }
  bool wait_count(size_t n, int ms) {
    std::unique_lock<std::mutex> lk(mu);
    return cv.wait_for(lk, std::chrono::milliseconds(ms), [&] { return captureUs.size() >= n; });
  }
  size_t count() { std::lock_guard<std::mutex> lk(mu); return captureUs.size(); }
};

CaptureFrameMeta meta_of(uint32_t w, uint32_t h, uint64_t captureUs) {
  CaptureFrameMeta m{}; m.width = w; m.height = h; m.captureUs = captureUs; return m;
}

// A gate the worker parks at: the test waits until the worker is there, does something, releases it.
struct Park {
  HANDLE arrived = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  HANDLE go = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  std::atomic<bool> armed{false};
  ~Park() { CloseHandle(arrived); CloseHandle(go); }
  std::function<void()> hook() {
    return [this] {
      if (!armed.exchange(false)) return;  // one parking per arm
      SetEvent(arrived);
      WaitForSingleObject(go, 5000);
    };
  }
  bool arm_and_wait(DWORD ms) { ResetEvent(arrived); ResetEvent(go); armed.store(true); return WaitForSingleObject(arrived, ms) == WAIT_OBJECT_0; }
  void release() { SetEvent(go); }
};

// ---------------------------------------------------------------------------------------------
// [1n] negative control, run as a child: the device-CS owner calls Submit directly (pre-r12 shape).
int negative_direct_submit() {
  Device d;
  if (!d.create()) return 2;
  const uint32_t W = 64, H = 64;
  auto src = d.source(W, H, 0xff000001);
  std::mutex contextMu;
  Published pub;
  D3dCaptureReadbackPipeline pipe;
  if (!pipe.Initialize(d.dev.Get(), d.ctx.Get(), &contextMu, W, H, 2, pub.fn())) return 3;
  // T owns the device CS. S enters Submit: contextMu_ then CopyResource -> waits for the CS.
  HANDLE sInside = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  d.mt->Enter();
  std::thread s([&] { SetEvent(sInside); pipe.Submit(src.Get(), meta_of(W, H, 1)); });
  WaitForSingleObject(sInside, 2000);
  Sleep(100);  // S is now inside CopyResource on the CS, holding contextMu_
  // The pre-r12 callback: Submit from the CS owner. It waits for contextMu_ (held by S, which waits
  // for the CS held by this thread). This never returns; the parent measures that.
  pipe.Submit(src.Get(), meta_of(W, H, 2));
  d.mt->Leave();
  s.join();
  std::printf("negative control: the CS owner's direct Submit RETURNED (no deadlock)\n");
  return 0;  // reaching here means the negative control did NOT reproduce the cycle
}

bool run_negative_child(const wchar_t* exe) {
  std::wstring cmd = std::wstring(L"\"") + exe + L"\" --negative-direct-submit";
  STARTUPINFOW si{}; si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  std::vector<wchar_t> buf(cmd.begin(), cmd.end()); buf.push_back(0);
  if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
    check(false, "[1n] negative control child could not be started");
    return false;
  }
  const DWORD w = WaitForSingleObject(pi.hProcess, 4000);
  DWORD code = 0;
  GetExitCodeProcess(pi.hProcess, &code);
  const bool deadlocked = (w == WAIT_TIMEOUT);
  if (deadlocked) TerminateProcess(pi.hProcess, 99);
  CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
  check(deadlocked, "[1n] negative control: the device-CS owner calling Submit directly (pre-r12 callback shape) deadlocks -- child did not exit in 4 s",
        deadlocked ? "killed after the bound" : "child exited with code " + std::to_string(code));
  return deadlocked;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  if (argc > 1 && std::wcscmp(argv[1], L"--negative-direct-submit") == 0) return negative_direct_submit();
  setvbuf(stdout, nullptr, _IONBF, 0);  // a check that deadlocks must not take the earlier lines with it
  wchar_t self[MAX_PATH]{};
  GetModuleFileNameW(nullptr, self, MAX_PATH);

  Device d;
  if (!d.create()) { std::printf("SKIP capture_readback_lock_test: no WARP device / ID3D11Multithread\n"); return 77; }
  const uint32_t W = 64, H = 64;
  auto src1 = d.source(W, H, 0xff000001);
  auto src2 = d.source(W, H, 0xff000002);
  std::mutex contextMu;

  // ---------------------------------------------------------------------------------------------
  // [1] the callback entry returns while the device CS is owned by the calling thread and another
  //     thread is blocked inside Submit's D3D work on that CS.
  {
    Published pub;
    D3dCaptureReadbackPipeline pipe;
    check(pipe.Initialize(d.dev.Get(), d.ctx.Get(), &contextMu, W, H, 2, pub.fn()), "[1] pipeline initialised (WARP)");
    std::atomic<int> drains{0};
    FrameSignalCollector collector([&] { drains.fetch_add(1); pipe.Submit(src2.Get(), meta_of(W, H, 2)); });
    check(collector.Start(), "[1] collector started");
    HANDLE sInside = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    d.mt->Enter();  // this thread = the WGC thread of the dumps: owns the device CS
    std::thread s([&] { SetEvent(sInside); pipe.Submit(src1.Get(), meta_of(W, H, 1)); });
    WaitForSingleObject(sInside, 2000);
    Sleep(100);  // S is inside CopyResource waiting for the CS, holding contextMu_
    const auto t0 = std::chrono::steady_clock::now();
    const bool signalled = collector.Signal();
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    check(signalled && ms < 50.0, "[1] Signal() from the device-CS owner returned at once while Submit was blocked on that CS", "took " + std::to_string(ms) + " ms");
    // the collector's worker is now blocked in Submit (contextMu_ held by S) -- as it may be: it holds no framework lock
    Sleep(50);
    check(drains.load() == 1, "[1] the collector's worker started the drain (blocked behind the CS owner, not the callback)");
    d.mt->Leave();
    s.join();
    const bool both = pub.wait_count(2, 3000);
    check(both, "[1] both frames published once the CS was released", "published=" + std::to_string(pub.count()));
    check(collector.StopWithin(2000), "[1] collector stopped within the bound");
    CloseHandle(sInside);
    pipe.Shutdown();
  }

  // [1n] negative control in a child process
  run_negative_child(self);

  // ---------------------------------------------------------------------------------------------
  // [2] worker's GetData waits for the device CS -> slotMu_ must be free (pre-r12: held across the sweep)
  {
    Published pub;
    D3dCaptureReadbackPipeline pipe;
    Park park;
    pipe.beforeGpuPollForTest = park.hook();
    check(pipe.Initialize(d.dev.Get(), d.ctx.Get(), &contextMu, W, H, 2, pub.fn()), "[2] pipeline initialised");
    park.armed.store(true);
    ResetEvent(park.arrived); ResetEvent(park.go);
    check(pipe.Submit(src1.Get(), meta_of(W, H, 1)), "[2] frame 1 submitted");
    check(WaitForSingleObject(park.arrived, 3000) == WAIT_OBJECT_0, "[2] worker parked before its GetData sweep (contextMu_ held, slotMu_ not)");
    d.mt->Enter();      // now the worker's GetData will wait for the device CS
    park.release();
    Sleep(100);         // worker is inside GetData on the CS
    const bool slotFree = returns_within([&] { pipe.SetOutputSize(0, 0); }, 300);
    check(slotFree, "[2] slotMu_ is free while the worker waits for the device CS inside GetData (SetOutputSize returned)");
    const bool statsFree = returns_within([&] { (void)pipe.GpuPendingCount(); (void)pipe.OldestGpuPendingAgeUs(); }, 300);
    check(statsFree, "[2] the diagnostic readers return while the worker waits for the device CS");
    uint32_t stuckCount = 0;
    const bool countRead = returns_within([&] { stuckCount = pipe.GpuPendingCount(); }, 300);
    check(countRead && stuckCount == 1, "[2] the pending frame is reported as pending (not 0) while the worker is stuck", "count=" + std::to_string(stuckCount) + (countRead ? "" : " (reader blocked)"));
    d.mt->Leave();
    check(pub.wait_count(1, 3000), "[2] frame 1 published after the CS was released");
    pipe.Shutdown();
  }

  // ---------------------------------------------------------------------------------------------
  // [3] stats readers are wait-free while the worker holds slotMu_ (pre-r12: they took slotMu_)
  {
    Published pub;
    D3dCaptureReadbackPipeline pipe;
    Park park;
    pipe.whileSlotLockedForTest = park.hook();
    check(pipe.Initialize(d.dev.Get(), d.ctx.Get(), &contextMu, W, H, 2, pub.fn()), "[3] pipeline initialised");
    park.armed.store(true);
    ResetEvent(park.arrived); ResetEvent(park.go);
    check(pipe.Submit(src1.Get(), meta_of(W, H, 1)), "[3] frame 1 submitted");
    check(WaitForSingleObject(park.arrived, 3000) == WAIT_OBJECT_0, "[3] worker parked with slotMu_ held (slot pinned Reading)");
    uint64_t age1 = 0, age2 = 0; uint32_t cnt = 0; uint64_t subm = 0;
    const bool ok = returns_within([&] { age1 = pipe.OldestGpuPendingAgeUs(); cnt = pipe.GpuPendingCount(); subm = pipe.OldestSubmittingAgeUs(); }, 100);
    check(ok, "[3] OldestGpuPendingAgeUs/GpuPendingCount/OldestSubmittingAgeUs returned within 100 ms while slotMu_ was held");
    Sleep(30);
    returns_within([&] { age2 = pipe.OldestGpuPendingAgeUs(); }, 100);
    check(cnt == 1, "[3] the pinned slot counts as pending (Reading), not as idle", "count=" + std::to_string(cnt));
    check(age1 > 0 && age2 > age1 + 20000, "[3] the pending age keeps growing while the worker is frozen (not disguised as 0)", "age1=" + std::to_string(age1) + " age2=" + std::to_string(age2));
    check(subm == 0, "[3] no slot is inside Submit, so the Submitting age is 0 (true idle, not a stale read)");
    D3dCaptureReadbackPipeline::DiagSnapshot diag{};
    uint64_t diagAge = 0;
    const bool diagRead = returns_within([&] { diag = pipe.ReadDiag(); diagAge = pipe.DiagAgeUs(); }, 100);
    check(diagRead && diag.consistent && diag.gpuPendingCount == 1 && diag.generation >= 1, "[3] ReadDiag snapshot is consistent and carries count/generation", "gen=" + std::to_string(diag.generation) + (diagRead ? "" : " (reader blocked)"));
    check(diagRead && diagAge > 20000, "[3] DiagAgeUs reports the snapshot's staleness while nothing moves", std::to_string(diagAge) + " us");
    park.release();
    check(pub.wait_count(1, 3000), "[3] frame 1 published after release");
    pipe.Shutdown();
  }

  // ---------------------------------------------------------------------------------------------
  // [4a] ownership: reconfigure between probe and commit -> old result unapplied, old frame never published
  {
    Published pub;
    D3dCaptureReadbackPipeline pipe;
    Park park;
    pipe.beforeGpuPollForTest = park.hook();
    check(pipe.Initialize(d.dev.Get(), d.ctx.Get(), &contextMu, W, H, 2, pub.fn()), "[4a] pipeline initialised");
    park.armed.store(true); ResetEvent(park.arrived); ResetEvent(park.go);
    check(pipe.Submit(src1.Get(), meta_of(W, H, 1)), "[4a] frame 1 submitted");
    check(WaitForSingleObject(park.arrived, 3000) == WAIT_OBJECT_0, "[4a] worker parked before its sweep");
    check(returns_within([&] { pipe.Reconfigure(W, H); }, 500), "[4a] Reconfigure (generation bump) completed while the worker was parked (slotMu_ free)");
    park.release();
    Sleep(150);
    check(pub.count() == 0, "[4a] the probed frame of the old generation was NOT published", "published=" + std::to_string(pub.count()));
    check(pipe.GpuPendingCount() == 0, "[4a] the new generation's ring is idle (old pending slot not carried over)");
    check(pipe.Submit(src2.Get(), meta_of(W, H, 2)), "[4a] frame 2 submitted into the new generation");
    const bool got2 = pub.wait_count(1, 3000);
    Sleep(100);
    check(got2 && pub.count() == 1 && pub.captureUs.back() == 2 && pub.firstByte.back() == 2, "[4a] frame 2 published once with its own pixels", "published=" + std::to_string(pub.count()));
    pipe.Shutdown();
  }
  // [4b] ownership: a superseding Submit between probe and commit (ring of 1) -> only the newer frame publishes, once
  {
    Published pub;
    D3dCaptureReadbackPipeline pipe;
    Park park;
    pipe.beforeGpuPollForTest = park.hook();
    check(pipe.Initialize(d.dev.Get(), d.ctx.Get(), &contextMu, W, H, 1, pub.fn()), "[4b] pipeline initialised (1 slot)");
    park.armed.store(true); ResetEvent(park.arrived); ResetEvent(park.go);
    check(pipe.Submit(src1.Get(), meta_of(W, H, 1)), "[4b] frame 1 submitted");
    check(WaitForSingleObject(park.arrived, 3000) == WAIT_OBJECT_0, "[4b] worker parked before its sweep");
    // Submit 2 takes the slot over (new submitSeq) under slotMu_ -- free -- then waits for contextMu_,
    // which the parked worker holds. Run it on a helper so the test can release the park.
    std::thread submitter([&] { pipe.Submit(src2.Get(), meta_of(W, H, 2)); });
    Sleep(80);  // the reservation (Submitting, new seq) has happened; the D3D work waits for contextMu_
    park.release();
    submitter.join();
    check(pub.wait_count(1, 3000), "[4b] a frame was published");
    Sleep(150);
    std::set<uint64_t> seen(pub.captureUs.begin(), pub.captureUs.end());
    check(pub.count() == 1 && seen.count(2) == 1 && seen.count(1) == 0, "[4b] only frame 2 published, exactly once; the superseded frame 1's poll result was not applied",
          "published=" + std::to_string(pub.count()));
    pipe.Shutdown();
  }

  // ---------------------------------------------------------------------------------------------
  // [5] FrameSignalCollector contract
  {
    std::atomic<int> drains{0};
    std::atomic<bool> inWork{false};
    auto c = std::make_shared<FrameSignalCollector>([&] { inWork.store(true); drains.fetch_add(1); Sleep(20); inWork.store(false); });
    check(c->Start() && c->ok(), "[5] collector started");
    // lost wake: signal, wait until the worker is inside work, signal again -> a second drain must follow
    c->Signal();
    for (int i = 0; i < 200 && !inWork.load(); ++i) Sleep(1);
    check(inWork.load(), "[5] first signal started a drain");
    c->Signal();  // arrives DURING work: must not be lost
    Sleep(120);
    check(drains.load() == 2, "[5] a signal that arrived during a drain produced one more drain (no lost wake, no extra)", "drains=" + std::to_string(drains.load()));
    // many signals from several threads coalesce into bounded drains
    std::vector<std::thread> ts;
    for (int t = 0; t < 4; ++t) ts.emplace_back([&] { for (int i = 0; i < 50; ++i) c->Signal(); });
    for (auto& t : ts) t.join();
    Sleep(200);
    const int after = drains.load();
    check(after >= 3 && after <= 2 + 200, "[5] 200 signals from 4 threads coalesced into bounded drains", "drains=" + std::to_string(after));
    check(c->Signals() == 202, "[5] signal counter", std::to_string(c->Signals()));
    // stop while work runs is bounded; signals after close are ignored and do not drain
    c->Signal();
    for (int i = 0; i < 200 && !inWork.load(); ++i) Sleep(1);
    const auto t0 = std::chrono::steady_clock::now();
    const bool stopped = c->StopWithin(2000);
    const double stopMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    check(stopped && stopMs < 1000, "[5] StopWithin joined the worker while it was inside work", std::to_string(stopMs) + " ms");
    const int atStop = drains.load();
    std::weak_ptr<FrameSignalCollector> weak = c;
    auto delegateCopy = c;  // what the WGC delegate holds
    c.reset();
    check(!weak.expired(), "[5] the delegate's reference keeps the collector alive after the owner dropped it");
    check(!delegateCopy->Signal() && delegateCopy->IgnoredAfterClose() == 1, "[5] a late Signal after Stop is ignored and counted");
    Sleep(50);
    check(drains.load() == atStop, "[5] no drain after Stop");
    delegateCopy.reset();
    check(weak.expired(), "[5] collector destroyed when the last (delegate) reference went");
  }


  // ---------------------------------------------------------------------------------------------
  // [6] (r13) end to end: fake pool -> collector drain (latest-wins) -> real Submit -> ring publishes the
  //     LAST value once, with no signal after the producer stopped. This is the WGC path's shape with the
  //     pool replaced by a queue of textures; the collector and the ring are the product's.
  {
    Published pub;
    D3dCaptureReadbackPipeline pipe;
    check(pipe.Initialize(d.dev.Get(), d.ctx.Get(), &contextMu, W, H, 2, pub.fn()), "[6] pipeline initialised");
    pipe.SetPublishIntervalUs(60000);  // paced like the product (submit cadence) so the ring, not luck, keeps the last frame
    struct FakePool {
      std::mutex mu;
      std::vector<std::pair<ComPtr<ID3D11Texture2D>, uint64_t>> frames;
      void push(ComPtr<ID3D11Texture2D> t, uint64_t id) { std::lock_guard<std::mutex> lk(mu); frames.emplace_back(std::move(t), id); }
      bool try_get_next(ComPtr<ID3D11Texture2D>* t, uint64_t* id) {
        std::lock_guard<std::mutex> lk(mu);
        if (frames.empty()) return false;
        *t = std::move(frames.front().first); *id = frames.front().second; frames.erase(frames.begin());
        return true;
      }
    } pool;
    std::atomic<int> drains{0}, submitted{0};
    auto collector = std::make_shared<FrameSignalCollector>([&] {
      drains.fetch_add(1);
      ComPtr<ID3D11Texture2D> latest; uint64_t latestId = 0;
      ComPtr<ID3D11Texture2D> t; uint64_t id = 0;
      while (pool.try_get_next(&t, &id)) { latest = t; latestId = id; }  // latest-wins drain, as the WGC worker does
      if (!latest) return;
      Sleep(3);  // the worker is "slow": several signals land while it works
      if (pipe.Submit(latest.Get(), meta_of(W, H, latestId))) submitted.fetch_add(1);
    });
    check(collector->Start(), "[6] collector started");
    const int N = 40;
    std::vector<ComPtr<ID3D11Texture2D>> keep;
    for (int i = 1; i <= N; ++i) {
      keep.push_back(d.source(W, H, 0xff000000u | static_cast<uint32_t>(i)));
      pool.push(keep.back(), static_cast<uint64_t>(i));
      collector->Signal();
      if (i % 7 == 0) Sleep(2);
    }
    const uint64_t signalsAtStop = collector->Signals();
    // the final value must arrive without any further signal
    bool gotLast = false;
    for (int i = 0; i < 300 && !gotLast; ++i) {
      Sleep(10);
      std::lock_guard<std::mutex> lk(pub.mu);
      gotLast = !pub.captureUs.empty() && pub.captureUs.back() == static_cast<uint64_t>(N) && pub.firstByte.back() == static_cast<uint8_t>(N);
    }
    check(gotLast, "[6] the LAST produced frame (id 40, pixel 40) was published after the producer stopped, with no further signal",
          "signalsAtStop=" + std::to_string(signalsAtStop) + " signalsNow=" + std::to_string(collector->Signals()) + " drains=" + std::to_string(drains.load()) +
              " submitted=" + std::to_string(submitted.load()) + " published=" + std::to_string(pub.count()));
    Sleep(250);
    size_t lastCount = 0;
    {
      std::lock_guard<std::mutex> lk(pub.mu);
      for (uint64_t v : pub.captureUs) if (v == static_cast<uint64_t>(N)) ++lastCount;
    }
    check(lastCount == 1, "[6] the last frame was published exactly once", "count=" + std::to_string(lastCount));
    check(collector->Signals() == signalsAtStop, "[6] no signal arrived after the producer stopped (the delivery came from the drain/ring, not a late callback)");
    check(drains.load() <= N && submitted.load() <= drains.load(), "[6] drains coalesced (<= signals) and every drain submitted at most once",
          "drains=" + std::to_string(drains.load()) + " submitted=" + std::to_string(submitted.load()));
    // ordering: published ids are non-decreasing (latest-wins never goes backwards)
    bool monotone = true;
    { std::lock_guard<std::mutex> lk(pub.mu); for (size_t i = 1; i < pub.captureUs.size(); ++i) if (pub.captureUs[i] < pub.captureUs[i - 1]) monotone = false; }
    check(monotone, "[6] published frame ids never went backwards");
    check(collector->StopWithin(2000), "[6] collector stopped");
    pipe.Shutdown();
  }

  // ---------------------------------------------------------------------------------------------
  // [7] (r13, Codex C2) the diagnostic view keeps Unknown as Unknown
  {
    Published pub;
    D3dCaptureReadbackPipeline pipe;
    // uninitialised: no ring yet
    const auto v0 = pipe.ViewDiag();
    check(v0.validity == D3dCaptureReadbackPipeline::DiagValidity::Uninitialized && v0.gpuPendingCount == 0,
          "[7] before Initialize the view is Uninitialized (not an idle 'ok')");
    Park park;
    pipe.midDiagWriteForTest = park.hook();
    check(pipe.Initialize(d.dev.Get(), d.ctx.Get(), &contextMu, W, H, 2, pub.fn()), "[7] pipeline initialised");
    const auto v1 = pipe.ViewDiag();
    check(v1.ok() && v1.generation == 1 && v1.gpuPendingCount == 0, "[7] after Initialize: ok, generation 1, idle", "gen=" + std::to_string(v1.generation));
    // a writer stuck mid-update (odd sequence): park the Submit thread's RefreshDiagLocked between the
    // two sequence increments; the reader must return quickly with Inconsistent and keep the values.
    park.armed.store(true); ResetEvent(park.arrived); ResetEvent(park.go);
    std::thread submitter([&] { pipe.Submit(src1.Get(), meta_of(W, H, 1)); });
    check(WaitForSingleObject(park.arrived, 3000) == WAIT_OBJECT_0, "[7] writer parked mid-update (sequence odd)");
    D3dCaptureReadbackPipeline::DiagView torn{};
    const bool fast = returns_within([&] { torn = pipe.ViewDiag(); }, 200);
    check(fast, "[7] ViewDiag returned within 200 ms while the writer was stuck");
    check(torn.validity == D3dCaptureReadbackPipeline::DiagValidity::Inconsistent, "[7] a stuck writer reads as Inconsistent, not as data",
          std::string("validity=") + D3dCaptureReadbackPipeline::diag_validity_name(torn.validity));
    check(torn.generation == 1, "[7] the torn view still carries the last generation (values kept, not zeroed)", "gen=" + std::to_string(torn.generation));
    uint32_t legacyCount = 99; uint64_t legacyAge = 99;
    returns_within([&] { legacyCount = pipe.GpuPendingCount(); legacyAge = pipe.OldestGpuPendingAgeUs(); }, 200);
    check(true, "[7] (information) legacy getters while torn -- the product no longer consumes them; stats/watchdog read ViewDiag",
          "count=" + std::to_string(legacyCount) + " age=" + std::to_string(legacyAge));
    park.release();
    submitter.join();
    pipe.midDiagWriteForTest = nullptr;
    check(pub.wait_count(1, 3000), "[7] the frame published after the writer resumed");
    const auto v2 = pipe.ViewDiag();
    check(v2.ok() && v2.generation == 1 && v2.gpuPendingCount == 0, "[7] after publish: ok, same generation, idle again");
    // generation change is visible in the same view, and a FAILED rebuild refreshes the snapshot too
    check(pipe.Submit(src2.Get(), meta_of(W, H, 2)), "[7] a frame submitted (pending for a moment)");
    // 40000 x 40000 exceeds D3D11's texture limit (16384): CreateTexture2D fails after the generation moved
    const bool rebuilt = pipe.Reconfigure(40000, 40000);
    check(!rebuilt, "[7] Reconfigure to an impossible size failed (texture create failure path)");
    const auto v3 = pipe.ViewDiag();
    check(v3.ok() && v3.generation == 2 && v3.gpuPendingCount == 0,
          "[7] the failed rebuild refreshed the snapshot: generation 2, nothing pending (pre-r13: the old generation's pending slot stayed in the snapshot)",
          "validity=" + std::string(D3dCaptureReadbackPipeline::diag_validity_name(v3.validity)) + " gen=" + std::to_string(v3.generation) + " count=" + std::to_string(v3.gpuPendingCount));
    check(pipe.Reconfigure(W, H), "[7] a normal Reconfigure succeeds afterwards");
    const auto v4 = pipe.ViewDiag();
    check(v4.ok() && v4.generation == 3, "[7] generation advanced again in the same view", "gen=" + std::to_string(v4.generation));
    pipe.Shutdown();
    const auto v5 = pipe.ViewDiag();
    check(v5.ok() && v5.gpuPendingCount == 0, "[7] after Shutdown the view is a refreshed idle ring, not stale");
  }

  // ---------------------------------------------------------------------------------------------
  // [8] (r13, Codex C3) collector start failure leaves nothing running and nothing leaked
  {
    std::atomic<int> drains{0};
    {
      FrameSignalCollector::StartSeams seams; seams.failEvent = true;
      FrameSignalCollector c([&] { drains.fetch_add(1); }, seams);
      check(!c.Start() && !c.ok(), "[8] event creation failure -> Start() false, ok() false", c.start_error());
      check(!c.Signal(), "[8] Signal on a collector that did not start is ignored");
      Sleep(30);
      check(drains.load() == 0, "[8] no worker ran");
      check(c.StopWithin(100), "[8] StopWithin on a never-started collector returns at once");
    }
    {
      FrameSignalCollector::StartSeams seams; seams.failThread = true;
      FrameSignalCollector c([&] { drains.fetch_add(1); }, seams);
      check(!c.Start() && !c.ok(), "[8] thread start failure -> Start() false; the event handle was closed again", c.start_error());
      check(!c.Signal(), "[8] Signal ignored after a thread start failure");
      check(c.StopWithin(100), "[8] StopWithin clean after a thread start failure");
    }
    {
      FrameSignalCollector c([&] { drains.fetch_add(1); });
      check(c.Start() && c.Start(), "[8] a normal Start succeeds and is idempotent");
      c.Signal();
      Sleep(30);
      check(drains.load() == 1, "[8] the started collector drains");
      check(c.StopWithin(1000), "[8] stopped");
    }
  }

  std::printf("capture_readback_lock_test: %d PASS, %d FAIL\n", gPass, gFail);
  return gFail == 0 ? 0 : 1;
}
