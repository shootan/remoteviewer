// See capture_adapter_identity.hpp (dxgi-wedge-fix r1).

#include "capture_adapter_identity.hpp"

#include <wrl/client.h>

#include <cstdio>
#include <cstring>

#pragma comment(lib, "dxgi.lib")

namespace remote60::native_poc {

namespace {

constexpr char kLinePrefix[] = "[native-video-host][capture-adapter] ";

std::string utf8_of(const std::wstring& w) {
  if (w.empty()) return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
  if (n <= 0) return {};
  std::string s(static_cast<size_t>(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
  return s;
}

bool all_hex_lower(const std::string& s) {
  for (char c : s) {
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }
  return true;
}

// The value after "<name>=" up to the next space; empty when absent.
std::string field(const std::string& line, const char* name) {
  const std::string needle = std::string(" ") + name + "=";
  const std::string padded = " " + line;
  const size_t at = padded.find(needle);
  if (at == std::string::npos) return {};
  const size_t start = at + needle.size();
  const size_t end = padded.find_first_of(" \r\n", start);
  return padded.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

}  // namespace

bool CaptureAdapterIdentity::valid() const {
  return capture_adapter_key_shape_ok(key) && capture_adapter_driver_shape_ok(driver);
}

std::string capture_adapter_key(uint32_t vendorId, uint32_t deviceId, uint32_t subSysId, uint32_t revision,
                                const std::wstring& description) {
  char head[64];
  std::snprintf(head, sizeof(head), "v%04x d%04x s%08x r%02x|", vendorId, deviceId, subSysId, revision);
  const std::string material = std::string(head) + utf8_of(description);
  uint64_t h = 14695981039346656037ull;
  for (unsigned char c : material) {
    h ^= c;
    h *= 1099511628211ull;
  }
  char out[17];
  std::snprintf(out, sizeof(out), "%016llx", static_cast<unsigned long long>(h));
  return out;
}

std::string capture_adapter_driver_string(LARGE_INTEGER umd) {
  char out[32];
  std::snprintf(out, sizeof(out), "%u.%u.%u.%u", static_cast<unsigned>(HIWORD(umd.HighPart)),
                static_cast<unsigned>(LOWORD(umd.HighPart)), static_cast<unsigned>(HIWORD(umd.LowPart)),
                static_cast<unsigned>(LOWORD(umd.LowPart)));
  return out;
}

bool capture_adapter_key_shape_ok(const std::string& key) { return key.size() == 16 && all_hex_lower(key); }

bool capture_adapter_driver_shape_ok(const std::string& driver) {
  if (driver.empty() || driver.size() > 23) return false;
  int dots = 0;
  bool digitSinceDot = false;
  for (char c : driver) {
    if (c == '.') {
      if (!digitSinceDot) return false;
      ++dots;
      digitSinceDot = false;
    } else if (c >= '0' && c <= '9') {
      digitSinceDot = true;
    } else {
      return false;
    }
  }
  return dots == 3 && digitSinceDot;
}

CaptureAdapterIdentity identify_adapter(IDXGIAdapter1* adapter) {
  CaptureAdapterIdentity id;
  if (!adapter) return id;
  DXGI_ADAPTER_DESC1 desc{};
  if (FAILED(adapter->GetDesc1(&desc))) return id;
  if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) return id;  // WARP / basic render: not a capture GPU
  LARGE_INTEGER umd{};
  if (FAILED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &umd))) return id;
  id.vendorId = desc.VendorId;
  id.deviceId = desc.DeviceId;
  id.key = capture_adapter_key(desc.VendorId, desc.DeviceId, desc.SubSysId, desc.Revision, desc.Description);
  id.driver = capture_adapter_driver_string(umd);
  return id;
}

CaptureAdapterIdentity identify_device_adapter(ID3D11Device* device) {
  if (!device) return {};
  Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
  if (FAILED(device->QueryInterface(IID_PPV_ARGS(&dxgiDevice))) || !dxgiDevice) return {};
  Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
  if (FAILED(dxgiDevice->GetAdapter(&adapter)) || !adapter) return {};
  Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter1;
  if (FAILED(adapter.As(&adapter1)) || !adapter1) return {};
  return identify_adapter(adapter1.Get());
}

std::vector<CaptureAdapterIdentity> enumerate_present_adapters() {
  std::vector<CaptureAdapterIdentity> out;
  Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
  if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) || !factory) return out;
  for (UINT i = 0; i < 64; ++i) {
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    if (factory->EnumAdapters1(i, &adapter) == DXGI_ERROR_NOT_FOUND) break;
    CaptureAdapterIdentity id = identify_adapter(adapter.Get());
    if (id.valid()) out.push_back(std::move(id));
  }
  return out;
}

std::string capture_adapter_log_line(const CaptureAdapterIdentity& id) {
  if (!id.valid()) return std::string(kLinePrefix) + "key=unknown\n";
  char buf[160];
  std::snprintf(buf, sizeof(buf), "%skey=%s driver=%s vendor=0x%04x device=0x%04x\n", kLinePrefix, id.key.c_str(),
                id.driver.c_str(), id.vendorId, id.deviceId);
  return buf;
}

bool parse_capture_adapter_line(const std::string& line, CaptureAdapterIdentity* out) {
  // The child's stdout lines carry no timestamp of their own (the supervisor adds none either), but a
  // forwarded copy may: accept the prefix anywhere, then read only the two fields that matter.
  const size_t at = line.find(kLinePrefix);
  if (at == std::string::npos || line.size() > 512) return false;
  const std::string rest = line.substr(at + std::strlen(kLinePrefix));
  CaptureAdapterIdentity id;
  id.key = field(rest, "key");
  id.driver = field(rest, "driver");
  if (!id.valid()) return false;
  if (out) *out = std::move(id);
  return true;
}

}  // namespace remote60::native_poc
