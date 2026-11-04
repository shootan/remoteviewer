// lk-abcd-rel B2: the decoder's sample copies (mf_sample_copy.hpp) turn an allocation failure into a dropped frame.
//
// 2026-11-04 the e2e viewer process ended with fast-fail 7: std::bad_alloc from the vector the decoded sample is
// copied into, thrown through the video receiver's thread, std::terminate. Here the same copy code runs on a real
// IMFSample (MFCreateSample) holding a buffer that counts its Lock/Unlock, while this binary's global operator new
// throws std::bad_alloc on demand for a large request:
//   * a normal copy is byte for byte, the buffer unlocked;
//   * with the allocation failing: false, nothing thrown, the buffer unlocked, the output empty, counted;
//   * a buffer whose current length exceeds its maximum is refused before anything is read or allocated;
//   * the NV12 2D copy: the same three.
// Negative control: run with the try/catch removed (MUTANT) -- the process terminates (a non-zero exit).
// Tags: pure-logic (Media Foundation platform objects, no device, no window).

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>
#include <vector>

#include "mf_sample_copy.hpp"

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")

namespace {
std::atomic<bool> gFailLarge{false};
constexpr size_t kLarge = 4096;  // the test buffers' payloads are larger; nothing else here asks for this much
std::atomic<uint64_t> gLargeAsked{0};
}  // namespace

void* operator new(size_t n) {
  if (n >= kLarge) {
    gLargeAsked.fetch_add(1);
    if (gFailLarge.load()) throw std::bad_alloc();
  }
  if (void* p = std::malloc(n ? n : 1)) return p;
  throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }

namespace {
int gChecks = 0, gFailures = 0;
void check(const std::string& what, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.empty() ? "" : "  ", detail.c_str());
}

// A media buffer (contiguous and 2D) that counts its locks; payload `bytes`, reported current length `curLen`.
class CountingBuffer : public IMFMediaBuffer, public IMF2DBuffer {
 public:
  CountingBuffer(DWORD bytes, DWORD curLen, LONG pitch) : data_(bytes, 0), curLen_(curLen), pitch_(pitch) {
    for (DWORD i = 0; i < bytes; ++i) data_[i] = static_cast<BYTE>(i * 7 + 3);
  }
  int locks = 0, unlocks = 0, locks2d = 0, unlocks2d = 0;
  const std::vector<BYTE>& data() const { return data_; }
  // IUnknown
  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
    if (!ppv) return E_POINTER;
    if (riid == __uuidof(IUnknown) || riid == __uuidof(IMFMediaBuffer)) *ppv = static_cast<IMFMediaBuffer*>(this);
    else if (riid == __uuidof(IMF2DBuffer)) *ppv = static_cast<IMF2DBuffer*>(this);
    else {
      *ppv = nullptr;
      return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
  }
  STDMETHODIMP_(ULONG) AddRef() override { return ++refs_; }
  STDMETHODIMP_(ULONG) Release() override { return --refs_; }  // stack-owned by the test
  // IMFMediaBuffer
  STDMETHODIMP Lock(BYTE** p, DWORD* maxLen, DWORD* curLen) override {
    ++locks;
    *p = data_.data();
    if (maxLen) *maxLen = static_cast<DWORD>(data_.size());
    if (curLen) *curLen = curLen_;
    return S_OK;
  }
  STDMETHODIMP Unlock() override {
    ++unlocks;
    return S_OK;
  }
  STDMETHODIMP GetCurrentLength(DWORD* n) override {
    *n = curLen_;
    return S_OK;
  }
  STDMETHODIMP SetCurrentLength(DWORD n) override {
    curLen_ = n;
    return S_OK;
  }
  STDMETHODIMP GetMaxLength(DWORD* n) override {
    *n = static_cast<DWORD>(data_.size());
    return S_OK;
  }
  // IMF2DBuffer
  STDMETHODIMP Lock2D(BYTE** scan0, LONG* pitch) override {
    ++locks2d;
    *scan0 = data_.data();
    *pitch = pitch_;
    return S_OK;
  }
  STDMETHODIMP Unlock2D() override {
    ++unlocks2d;
    return S_OK;
  }
  STDMETHODIMP GetScanline0AndPitch(BYTE** scan0, LONG* pitch) override { return Lock2D(scan0, pitch); }
  STDMETHODIMP IsContiguousFormat(BOOL* c) override {
    *c = TRUE;
    return S_OK;
  }
  STDMETHODIMP GetContiguousLength(DWORD* n) override {
    *n = static_cast<DWORD>(data_.size());
    return S_OK;
  }
  STDMETHODIMP ContiguousCopyTo(BYTE*, DWORD) override { return E_NOTIMPL; }
  STDMETHODIMP ContiguousCopyFrom(const BYTE*, DWORD) override { return E_NOTIMPL; }

 private:
  std::vector<BYTE> data_;
  DWORD curLen_;
  LONG pitch_;
  ULONG refs_ = 1;
};

IMFSample* sample_with(CountingBuffer* b) {
  IMFSample* s = nullptr;
  if (FAILED(MFCreateSample(&s)) || FAILED(s->AddBuffer(b))) return nullptr;
  return s;
}
}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::printf("decoded-sample copies: an allocation failure is a dropped frame (lk-abcd-rel B2)\n\n");
  if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_LITE))) {
    std::printf("FAIL  MFStartup\n\nRESULT: FAILED  (1 checks, 1 failed)\n");
    return 1;
  }
  namespace mc = remote60::native_poc::mf_copy;
  constexpr DWORD kBytes = 64 * 1024;
  {
    std::printf("[contiguous]\n");
    CountingBuffer b(kBytes, kBytes, 256);
    IMFSample* s = sample_with(&b);
    std::vector<uint8_t> out;
    const bool ok = mc::SampleToBytes(s, &out);
    check("a normal copy: true, byte for byte, the buffer locked once and unlocked once",
          ok && out.size() == kBytes && std::equal(out.begin(), out.end(), b.data().begin()) && b.locks == 1 && b.unlocks == 1);
    const uint64_t before = mc::AllocationFailures().load();
    std::vector<uint8_t> fresh(3, 9);  // no capacity to reuse: the copy must allocate
    gFailLarge = true;
    bool threw = false, failed = false;
    try {
      failed = !mc::SampleToBytes(s, &fresh);
    } catch (...) {
      threw = true;
    }
    gFailLarge = false;
    check("THE ALLOCATION FAILS: false, nothing thrown", failed && !threw);
    check("...the buffer unlocked (locks == unlocks), the output empty, the failure counted",
          b.locks == 2 && b.unlocks == 2 && fresh.empty() && mc::AllocationFailures().load() == before + 1,
          "locks=" + std::to_string(b.locks) + " unlocks=" + std::to_string(b.unlocks));
    check("...and the next copy (memory back) works again", mc::SampleToBytes(s, &out) && out.size() == kBytes);
    s->Release();
  }
  {
    CountingBuffer b(kBytes, kBytes + 1, 256);  // current length past the maximum
    IMFSample* s = sample_with(&b);
    std::vector<uint8_t> out;
    const uint64_t asked = gLargeAsked.load();
    check("current length past the maximum: refused, nothing allocated for it, the buffer unlocked",
          !mc::SampleToBytes(s, &out) && out.empty() && gLargeAsked.load() == asked && b.unlocks == b.locks);
    s->Release();
  }
  {
    std::printf("\n[NV12 from the 2D buffer]\n");
    constexpr uint32_t w = 128, h = 64;
    CountingBuffer b(static_cast<DWORD>(256 * h * 3 / 2), 0, 256);  // pitch 256 > width
    IMFSample* s = sample_with(&b);
    std::vector<uint8_t> out;
    const bool ok = mc::SampleToNv12From2DBuffer(s, w, h, &out);
    bool rows = ok && out.size() == w * h * 3 / 2;
    for (uint32_t y = 0; rows && y < h; ++y) rows = std::memcmp(out.data() + y * w, b.data().data() + y * 256, w) == 0;
    check("a normal copy: tight NV12 (rows at the pitch), locked once and unlocked once",
          rows && b.locks2d == 1 && b.unlocks2d == 1);
    std::vector<uint8_t> fresh;  // no capacity to reuse
    gFailLarge = true;
    bool threw = false, failed = false;
    try {
      failed = !mc::SampleToNv12From2DBuffer(s, w, h, &fresh);
    } catch (...) {
      threw = true;
    }
    gFailLarge = false;
    check("THE ALLOCATION FAILS: false, nothing thrown, the 2D buffer unlocked, the output empty",
          failed && !threw && b.locks2d == 2 && b.unlocks2d == 2 && fresh.empty(),
          "locks2d=" + std::to_string(b.locks2d) + " unlocks2d=" + std::to_string(b.unlocks2d));
    s->Release();
  }
  MFShutdown();
  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", gFailures ? "FAILED" : "ALL PASS", gChecks, gFailures);
  return gFailures ? 1 : 0;
}
