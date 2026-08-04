// Measures one number: how many times a second the desktop actually changes.
//
// A previous conclusion -- "the desktop only offers ~33 updates a second, so 60 fps is not
// reachable" -- was drawn from host statistics collected while RDP was connected, and RDP
// composes onto a Microsoft Remote Display Adapter that runs at 32 Hz. 33 and 32 are too close
// for comfort. This tool re-asks the question with nothing else in the way: no encoder, no
// client, no pacing gate, so whatever it reports is the source's own rate.
//
// Two things that corrupted earlier measurements are handled explicitly.
//
// Frames whose LastPresentTime is zero are cursor movement. They carry no desktop pixels and
// counting them as updates inflates the rate, which is exactly the mistake that made a black
// capture look like a successful one in the Winlogon probe.
//
// The output being measured is named in full, because this machine also has a Virtual Display
// Driver at 800x600 @ 30 Hz and a Parsec adapter. Duplicating one of those and reporting the
// result as "the desktop" would repeat the original error with a different ceiling.
//
// Diagnostic only: not product code, not staged into the installer.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX  // windows.h defines max() as a macro, which std::max cannot survive
#include <windows.h>

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

struct SecondBucket {
  uint32_t updates = 0;      // frames carrying new desktop pixels
  uint32_t cursorOnly = 0;   // pointer moved, desktop unchanged
  uint32_t timeouts = 0;     // nothing at all happened
  uint32_t accumulated = 0;  // updates the OS had to merge because we were not there yet
  uint32_t accumulatedMax = 0;
};

struct OutputChoice {
  ComPtr<IDXGIOutput1> output;
  std::wstring adapter;
  std::wstring device;
  uint32_t width = 0;
  uint32_t height = 0;
  bool attached = false;
};

// Prints every output on every adapter and returns the requested one. Seeing the whole list is
// the point: a 800x600 entry in it is the trap this tool exists to avoid falling into.
bool choose_output(int wantedIndex, OutputChoice* chosen, ComPtr<ID3D11Device>* deviceOut) {
  ComPtr<IDXGIFactory1> factory;
  if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
    wprintf(L"CreateDXGIFactory1 failed\n");
    return false;
  }

  int index = 0;
  wprintf(L"available outputs:\n");
  for (UINT a = 0;; ++a) {
    ComPtr<IDXGIAdapter1> adapter;
    if (factory->EnumAdapters1(a, &adapter) == DXGI_ERROR_NOT_FOUND) break;
    DXGI_ADAPTER_DESC1 adapterDesc{};
    adapter->GetDesc1(&adapterDesc);

    for (UINT o = 0;; ++o) {
      ComPtr<IDXGIOutput> output;
      if (adapter->EnumOutputs(o, &output) == DXGI_ERROR_NOT_FOUND) break;
      DXGI_OUTPUT_DESC desc{};
      if (FAILED(output->GetDesc(&desc))) continue;
      const uint32_t width =
          static_cast<uint32_t>(desc.DesktopCoordinates.right - desc.DesktopCoordinates.left);
      const uint32_t height =
          static_cast<uint32_t>(desc.DesktopCoordinates.bottom - desc.DesktopCoordinates.top);

      wprintf(L"  [%d] %-34s  %-18s  %ux%u  attached=%s\n", index, adapterDesc.Description,
              desc.DeviceName, width, height, desc.AttachedToDesktop ? L"yes" : L"no");

      if (index == wantedIndex) {
        // The D3D device must live on the adapter that owns the output, or DuplicateOutput
        // refuses -- the same coupling that breaks the product's recovery when RDP moves the
        // desktop to another adapter.
        const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
                                            D3D_FEATURE_LEVEL_10_1};
        HRESULT hr = D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0,
                                       levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
                                       deviceOut->GetAddressOf(), nullptr, nullptr);
        if (FAILED(hr)) {
          wprintf(L"  -> D3D11CreateDevice on that adapter failed hr=0x%08lX\n",
                  static_cast<unsigned long>(hr));
          return false;
        }
        if (FAILED(output.As(&chosen->output))) {
          wprintf(L"  -> IDXGIOutput1 unavailable\n");
          return false;
        }
        chosen->adapter = adapterDesc.Description;
        chosen->device = desc.DeviceName;
        chosen->width = width;
        chosen->height = height;
        chosen->attached = desc.AttachedToDesktop != 0;
      }
      ++index;
    }
  }
  if (!chosen->output) {
    wprintf(L"no output at index %d (there are %d)\n", wantedIndex, index);
    return false;
  }
  return true;
}

void print_summary(const std::vector<SecondBucket>& seconds) {
  if (seconds.empty()) return;
  std::vector<uint32_t> rates;
  uint64_t totalUpdates = 0;
  uint64_t totalCursor = 0;
  uint64_t totalCoalesced = 0;
  uint32_t worstAccumulated = 0;
  for (const SecondBucket& bucket : seconds) {
    rates.push_back(bucket.updates);
    totalUpdates += bucket.updates;
    totalCursor += bucket.cursorOnly;
    if (bucket.accumulated > bucket.updates) totalCoalesced += bucket.accumulated - bucket.updates;
    worstAccumulated = std::max(worstAccumulated, bucket.accumulatedMax);
  }
  std::sort(rates.begin(), rates.end());
  const uint32_t p50 = rates[rates.size() / 2];
  const uint32_t p95 = rates[std::min(rates.size() - 1, rates.size() * 95 / 100)];
  const uint32_t peak = rates.back();

  wprintf(L"\n================ result over %zu seconds ================\n", seconds.size());
  wprintf(L"desktop updates per second:  median %u   p95 %u   peak %u   mean %.1f\n", p50, p95,
          peak, static_cast<double>(totalUpdates) / static_cast<double>(seconds.size()));
  wprintf(L"cursor-only frames:          %llu total (not counted as updates)\n",
          static_cast<unsigned long long>(totalCursor));
  wprintf(L"updates merged by the OS:    %llu (max %u in one acquire)\n",
          static_cast<unsigned long long>(totalCoalesced), worstAccumulated);
  wprintf(L"\nreading it:\n");
  // The peak is the number that answers the question. A median dragged down by seconds when
  // nothing was happening says something about the content, not about the ceiling.
  if (peak >= 50) {
    wprintf(L"  peak %u/s -- the desktop CAN offer far more than 33 a second here.\n", peak);
    wprintf(L"  The earlier \"~33 updates a second\" figure was the RDP virtual display's\n");
    wprintf(L"  32 Hz ceiling, not the content. 60 fps was never source-limited.\n");
  } else if (peak <= 40) {
    wprintf(L"  peak %u/s -- the source really does top out near this rate.\n", peak);
    wprintf(L"  The earlier conclusion stands: 60 fps has nothing to carry.\n");
  } else {
    wprintf(L"  peak %u/s -- between the two. Re-run while playing a 60 fps video full\n", peak);
    wprintf(L"  screen; if it does not climb, the ceiling is real.\n");
  }
  wprintf(L"  (If the peak looks low, check that something was actually moving on the\n");
  wprintf(L"   measured output during the run.)\n");
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  const int seconds = (argc >= 2) ? _wtoi(argv[1]) : 60;
  const int outputIndex = (argc >= 3) ? _wtoi(argv[2]) : 0;

  OutputChoice chosen;
  ComPtr<ID3D11Device> device;
  if (!choose_output(outputIndex, &chosen, &device)) return 1;

  wprintf(L"\nmeasuring [%d] %s on %s  %ux%u  attached=%s  for %d seconds\n", outputIndex,
          chosen.device.c_str(), chosen.adapter.c_str(), chosen.width, chosen.height,
          chosen.attached ? L"yes" : L"no", seconds);
  wprintf(L"move windows, scroll, play a video on THAT display now.\n\n");

  ComPtr<IDXGIOutputDuplication> duplication;
  if (FAILED(chosen.output->DuplicateOutput(device.Get(), &duplication)) || !duplication) {
    wprintf(L"DuplicateOutput failed -- if this session is over RDP the desktop is on a "
            L"different adapter.\n");
    return 2;
  }

  std::vector<SecondBucket> buckets;
  SecondBucket current;
  ULONGLONG secondStart = GetTickCount64();
  const ULONGLONG deadline = secondStart + static_cast<ULONGLONG>(seconds) * 1000ull;

  while (GetTickCount64() < deadline) {
    DXGI_OUTDUPL_FRAME_INFO info{};
    ComPtr<IDXGIResource> resource;
    const HRESULT hr = duplication->AcquireNextFrame(100, &info, &resource);
    if (SUCCEEDED(hr)) {
      if (info.LastPresentTime.QuadPart != 0) {
        ++current.updates;
        current.accumulated += info.AccumulatedFrames;
        current.accumulatedMax = std::max(current.accumulatedMax, info.AccumulatedFrames);
      } else {
        ++current.cursorOnly;
      }
      // Released immediately and with nothing in between: duplication reports no further change
      // while a frame is held, so any work done here would be measured as the desktop being
      // slower than it is.
      duplication->ReleaseFrame();
    } else if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
      ++current.timeouts;
    } else if (hr == DXGI_ERROR_ACCESS_LOST) {
      wprintf(L"  (desktop switched; rebuilding duplication)\n");
      duplication.Reset();
      if (FAILED(chosen.output->DuplicateOutput(device.Get(), &duplication)) || !duplication) {
        wprintf(L"  could not rebuild duplication, stopping\n");
        break;
      }
    } else {
      wprintf(L"AcquireNextFrame failed hr=0x%08lX\n", static_cast<unsigned long>(hr));
      break;
    }

    const ULONGLONG now = GetTickCount64();
    if (now - secondStart >= 1000) {
      wprintf(L"  %2zus  updates=%-3u cursorOnly=%-3u timeouts=%-3u merged=%u max=%u\n",
              buckets.size() + 1, current.updates, current.cursorOnly, current.timeouts,
              current.accumulated > current.updates ? current.accumulated - current.updates : 0u,
              current.accumulatedMax);
      buckets.push_back(current);
      current = SecondBucket{};
      secondStart = now;
    }
  }

  print_summary(buckets);
  return 0;
}
