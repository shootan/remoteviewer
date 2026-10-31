// The configurations here are the ones that broke duplication on the development machine and
// that cannot be reproduced without physically changing the display setup: an RDP session moving
// the desktop onto a virtual adapter, and a machine carrying Parsec plus a Virtual Display
// Driver whose outputs report a zero extent.

#include "dxgi_output_selection.hpp"

#include <cstdio>
#include <string>
#include <vector>

using remote60::host::DxgiOutputInfo;
using remote60::host::DxgiSelectionReason;
using remote60::host::dxgi_selection_reason_name;
using remote60::host::select_dxgi_output;

namespace {

int gFailures = 0;

void fail(const std::string& what) {
  std::printf("  FAIL %s\n", what.c_str());
  ++gFailures;
}

DxgiOutputInfo make(uint32_t adapter, const char* adapterName, const char* device, int32_t width,
                    int32_t height, uint64_t monitorId, bool attached = true) {
  DxgiOutputInfo info;
  info.adapterIndex = adapter;
  info.adapterDescription = adapterName;
  info.deviceName = device;
  info.right = width;
  info.bottom = height;
  info.monitorId = monitorId;
  info.attachedToDesktop = attached;
  return info;
}

// The RDP case. The desktop is on the Microsoft Remote Display Adapter while the D3D device was
// built on the physical GPU, so recreating duplication with the existing device could never
// succeed. The selection has to say which adapter to rebuild on.
void TestRdpMovesTheDesktopToAnotherAdapter() {
  std::printf("under RDP the desktop lives on a different adapter than the D3D device\n");
  const std::vector<DxgiOutputInfo> outputs = {
      make(0, "AMD Radeon(TM) Graphics", "\\\\.\\DISPLAY1", 1920, 1080, 0x1111),
      make(1, "Microsoft Remote Display Adapter", "\\\\.\\DISPLAY5", 2246, 1232, 0x2222),
  };
  const auto selection = select_dxgi_output(outputs, 0x2222, true);
  if (!selection.found || selection.output.adapterIndex != 1) {
    fail("should select the remote display adapter's output");
    return;
  }
  if (!selection.needsDeviceOnAdapter(0)) {
    fail("a device on adapter 0 must be told to move");
    return;
  }
  if (selection.needsDeviceOnAdapter(1)) {
    fail("a device already on adapter 1 must not be rebuilt");
    return;
  }
  std::printf("  ok   picked adapter %u (%s), device rebuild required\n",
              selection.output.adapterIndex, selection.output.adapterDescription.c_str());
}

// The phantom-output case. Previously the code fell back to the first output it saw and only then
// checked the size, so it selected a zero-sized virtual display and failed with
// dxgi_output_size_invalid instead of using the real monitor sitting next to it.
void TestZeroSizedVirtualOutputsAreNeverChosen() {
  std::printf("a zero-sized virtual output is not a candidate, even as a fallback\n");
  const std::vector<DxgiOutputInfo> outputs = {
      make(0, "Parsec Virtual Display Adapter", "\\\\.\\DISPLAY9", 0, 0, 0x3333),
      make(1, "AMD Radeon(TM) Graphics", "\\\\.\\DISPLAY1", 1920, 1080, 0x1111),
  };
  const auto selection = select_dxgi_output(outputs, 0x9999 /* a monitor that is gone */, true);
  if (!selection.found) {
    fail("should have fallen back to the real monitor");
    return;
  }
  if (selection.output.width() <= 0) {
    fail("fell back to a zero-sized output, which is the original bug");
    return;
  }
  if (selection.reason != DxgiSelectionReason::PrimaryFallback) {
    fail(std::string("expected primary_fallback, got ") +
         dxgi_selection_reason_name(selection.reason));
    return;
  }
  std::printf("  ok   skipped the virtual adapter, chose %s %dx%d\n",
              selection.output.deviceName.c_str(), selection.output.width(),
              selection.output.height());
}

void TestDetachedOutputsAreSkipped() {
  std::printf("an output not attached to the desktop is not a candidate\n");
  const std::vector<DxgiOutputInfo> outputs = {
      make(0, "Virtual Display Driver", "\\\\.\\DISPLAY24", 800, 600, 0x4444, /*attached=*/false),
      make(0, "AMD Radeon(TM) Graphics", "\\\\.\\DISPLAY2", 1920, 1080, 0x1111),
  };
  const auto selection = select_dxgi_output(outputs, 0, true);
  if (!selection.found || selection.output.monitorId != 0x1111) {
    fail("should have chosen the attached output");
    return;
  }
  std::printf("  ok   chose the attached output\n");
}

void TestExactMonitorMatchWins() {
  std::printf("the requested monitor is preferred over enumeration order\n");
  const std::vector<DxgiOutputInfo> outputs = {
      make(0, "AMD Radeon(TM) Graphics", "\\\\.\\DISPLAY1", 1920, 1080, 0x1111),
      make(0, "AMD Radeon(TM) Graphics", "\\\\.\\DISPLAY2", 2560, 1440, 0x2222),
  };
  const auto selection = select_dxgi_output(outputs, 0x2222, true);
  if (!selection.found || selection.output.monitorId != 0x2222 ||
      selection.reason != DxgiSelectionReason::MonitorMatch) {
    fail("should match the requested monitor exactly");
    return;
  }
  std::printf("  ok   matched the requested monitor\n");
}

void TestLandscapeOnlyRejectsPortrait() {
  std::printf("a portrait-only machine is reported, not silently accepted\n");
  const std::vector<DxgiOutputInfo> outputs = {
      make(0, "AMD Radeon(TM) Graphics", "\\\\.\\DISPLAY1", 1080, 1920, 0x1111),
  };
  const auto selection = select_dxgi_output(outputs, 0, true);
  if (selection.found || selection.reason != DxgiSelectionReason::RotationRejected) {
    fail(std::string("expected rotation_rejected, got ") +
         dxgi_selection_reason_name(selection.reason));
    return;
  }
  // The same machine is fine when the caller does not require landscape.
  const auto relaxed = select_dxgi_output(outputs, 0, false);
  if (!relaxed.found) {
    fail("portrait should be selectable when landscape is not required");
    return;
  }
  std::printf("  ok   rejected for rotation, accepted when landscape is not required\n");
}

void TestNothingUsableIsDistinctFromNothingPresent() {
  std::printf("no outputs and no usable outputs are different answers\n");
  const auto empty = select_dxgi_output({}, 0, true);
  if (empty.found || empty.reason != DxgiSelectionReason::NoOutputs) {
    fail("an empty list should report no_outputs");
    return;
  }
  const std::vector<DxgiOutputInfo> allPhantom = {
      make(0, "Parsec Virtual Display Adapter", "\\\\.\\DISPLAY9", 0, 0, 0x3333),
  };
  const auto phantom = select_dxgi_output(allPhantom, 0, true);
  if (phantom.found || phantom.reason != DxgiSelectionReason::NoUsableOutput) {
    fail(std::string("expected no_usable_output, got ") +
         dxgi_selection_reason_name(phantom.reason));
    return;
  }
  std::printf("  ok   no_outputs and no_usable_output are reported separately\n");
}

void TestSingleAdapterNeedsNoDeviceRebuild() {
  std::printf("the ordinary single-GPU case does not rebuild the device\n");
  const std::vector<DxgiOutputInfo> outputs = {
      make(0, "AMD Radeon(TM) Graphics", "\\\\.\\DISPLAY1", 1920, 1080, 0x1111),
  };
  const auto selection = select_dxgi_output(outputs, 0x1111, true);
  if (!selection.found || selection.needsDeviceOnAdapter(0)) {
    fail("a device already on the right adapter must be kept");
    return;
  }
  std::printf("  ok   device kept\n");
}

}  // namespace

int main() {
  TestRdpMovesTheDesktopToAnotherAdapter();
  TestZeroSizedVirtualOutputsAreNeverChosen();
  TestDetachedOutputsAreSkipped();
  TestExactMonitorMatchWins();
  TestLandscapeOnlyRejectsPortrait();
  TestNothingUsableIsDistinctFromNothingPresent();
  TestSingleAdapterNeedsNoDeviceRebuild();

  if (gFailures != 0) {
    std::printf("dxgi_output_selection_test: FAIL (%d)\n", gFailures);
    return 1;
  }
  std::printf("dxgi_output_selection_test: PASS\n");
  return 0;
}
