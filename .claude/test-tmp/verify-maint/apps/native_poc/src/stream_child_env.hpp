#pragma once

// The environment the product gives every GNLinkStream it starts (prelogin-ui r6, V1).
//
// GNLinkStream reads a few switches from its environment, and one of them decides whether it can run at
// all: H.264 is still behind a build-time experiment flag, and a build without it refuses --codec h264
// and exits (rc 11) unless REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE=1. GNLinkHost has always set these
// for its child; the service's --prelogin worker inherited the service's environment instead, had none
// of them, and exited at once -- and the tests hid it by setting the switch themselves.
//
// Both starters now take the values from HERE, so they cannot drift apart again: GNLinkHost sets them in
// its own environment before CreateProcess; the service builds an explicit environment block for the
// worker (its own environment, with these stated on top). Pure, so the block is testable.

#include <cstddef>
#include <cwchar>
#include <string>
#include <utility>
#include <vector>

namespace remote60::native_poc {

using EnvPairs = std::vector<std::pair<std::wstring, std::wstring>>;

// The product's switches for a streaming child. nv12Surface: GPU surface encode (GNLinkHost decides it
// per launch; the pre-login worker runs with it off).
inline EnvPairs ProductStreamChildEnv(bool nv12Surface) {
  return {
      // H.264 is the product's only codec; the switch is stated, not left to how the binary was built.
      {L"REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE", L"1"},
      // The product encoder preset (A/B 2026-07-31), stated instead of inherited.
      {L"REMOTE60_NATIVE_ENCODER_TUNE_MODE", L"low_latency"},
      {L"REMOTE60_NATIVE_NV12_SURFACE", nv12Surface ? L"1" : L"0"},
  };
}

// An environment block (NAME=value\0 ... \0\0) made of `base` (another block, e.g. the starter's own
// GetEnvironmentStringsW) with every name in `stated` replaced by its stated value (names compared
// case-insensitively, as Windows does). The result is what CreateProcess* takes with
// CREATE_UNICODE_ENVIRONMENT.
inline std::vector<wchar_t> BuildEnvironmentBlock(const wchar_t* base, const EnvPairs& stated) {
  auto same_name = [](const std::wstring& entry, const std::wstring& name) {
    if (entry.size() <= name.size() || entry[name.size()] != L'=') return false;
    for (size_t i = 0; i < name.size(); ++i) {
      wchar_t a = entry[i], b = name[i];
      if (a >= L'a' && a <= L'z') a = static_cast<wchar_t>(a - 32);
      if (b >= L'a' && b <= L'z') b = static_cast<wchar_t>(b - 32);
      if (a != b) return false;
    }
    return true;
  };
  std::vector<wchar_t> out;
  for (const wchar_t* p = base; p && *p; p += wcslen(p) + 1) {
    const std::wstring entry = p;
    bool replaced = false;
    for (const auto& kv : stated) replaced = replaced || same_name(entry, kv.first);
    if (replaced) continue;
    out.insert(out.end(), entry.begin(), entry.end());
    out.push_back(L'\0');
  }
  for (const auto& kv : stated) {
    const std::wstring entry = kv.first + L"=" + kv.second;
    out.insert(out.end(), entry.begin(), entry.end());
    out.push_back(L'\0');
  }
  if (out.empty()) out.push_back(L'\0');
  out.push_back(L'\0');
  return out;
}

}  // namespace remote60::native_poc
