#pragma once

// Backend request matching shared by the host (encoder / capture backend) and the viewer (decoder).
//
// Role:    backend_request_is_any / backend_request_satisfied / backend_request_is_vendor_specific /
//          backend_fallback_reason -- pure string policy that names why the resolved backend differs
//          from the requested one ("none", "default_policy", "requested_backend_unavailable",
//          "fallback_to_software", "fallback_to_generic_hw", "requested_backend_mismatch").
// Thread:  none (pure).
// Input:   the requested backend string (env/CLI) and the resolved backend name.
// Output:  a fallback-reason token for the log line.
// Callers: host_capture_device.hpp (host encoder/capture backend logs), viewer_decoder_backend.hpp.
//
// The host (host_capture_device.cpp, host split Phase 0-6) and the viewer (viewer_decoder_backend.cpp,
// viewer split Phase 0-5) carried byte-identical copies; viewer split Phase 0-15 keeps one here,
// header-only inline. Bodies unchanged.

#include <cstddef>
#include <string>

#include "string_util.hpp"

namespace remote60::native_poc {

inline bool backend_request_is_any(const std::string& requestLower, const char* const* values,
                            size_t valueCount) {
  if (!values || valueCount == 0) return false;
  for (size_t i = 0; i < valueCount; ++i) {
    const char* v = values[i];
    if (v && requestLower == v) return true;
  }
  return false;
}

inline bool backend_request_satisfied(const std::string& requestLower, const std::string& resolvedLower) {
  if (requestLower.empty()) return true;
  if (requestLower == "auto" || requestLower == "mft_auto") return true;
  if (requestLower == "hw" || requestLower == "mft_hw") {
    return resolvedLower.find("mft_enum_hw") != std::string::npos;
  }
  if (requestLower == "sw" || requestLower == "mft_sw") {
    return resolvedLower.find("mft_enum_sw") != std::string::npos ||
           resolvedLower.find("clsid_cmsh264") != std::string::npos;
  }
  static const char* const kAmfAliases[] = {"amf_hw", "amf_mft", "amd_hw", "amd_mft", "amd"};
  static const char* const kNvencAliases[] = {
      "nvenc_hw", "nvenc_mft", "nvenc", "nvidia_hw", "nvidia_mft", "nvidia"};
  static const char* const kQsvAliases[] = {
      "qsv_hw", "qsv_mft", "qsv", "intel_hw", "intel_mft", "intel"};
  if (backend_request_is_any(requestLower, kAmfAliases, sizeof(kAmfAliases) / sizeof(kAmfAliases[0]))) {
    return resolvedLower.find("amf") != std::string::npos;
  }
  if (backend_request_is_any(requestLower, kNvencAliases,
                             sizeof(kNvencAliases) / sizeof(kNvencAliases[0]))) {
    return resolvedLower.find("nvenc") != std::string::npos ||
           resolvedLower.find("nvidia") != std::string::npos;
  }
  if (backend_request_is_any(requestLower, kQsvAliases, sizeof(kQsvAliases) / sizeof(kQsvAliases[0]))) {
    return resolvedLower.find("qsv") != std::string::npos ||
           resolvedLower.find("intel") != std::string::npos;
  }
  return resolvedLower.find(requestLower) != std::string::npos;
}

inline bool backend_request_is_vendor_specific(const std::string& requestLower) {
  static const char* const kAmfAliases[] = {"amf_hw", "amf_mft", "amd_hw", "amd_mft", "amd"};
  static const char* const kNvencAliases[] = {
      "nvenc_hw", "nvenc_mft", "nvenc", "nvidia_hw", "nvidia_mft", "nvidia"};
  static const char* const kQsvAliases[] = {
      "qsv_hw", "qsv_mft", "qsv", "intel_hw", "intel_mft", "intel"};
  return backend_request_is_any(requestLower, kAmfAliases, sizeof(kAmfAliases) / sizeof(kAmfAliases[0])) ||
         backend_request_is_any(requestLower, kNvencAliases,
                                sizeof(kNvencAliases) / sizeof(kNvencAliases[0])) ||
         backend_request_is_any(requestLower, kQsvAliases, sizeof(kQsvAliases) / sizeof(kQsvAliases[0]));
}

inline std::string backend_fallback_reason(const std::string& requestedRaw, const char* resolvedBackendRaw) {
  const std::string requestLower = ascii_lower(trim_ascii(requestedRaw));
  const std::string resolvedLower =
      ascii_lower(trim_ascii(resolvedBackendRaw ? std::string(resolvedBackendRaw) : std::string{}));
  if (requestLower.empty()) return "default_policy";
  if (backend_request_satisfied(requestLower, resolvedLower)) return "none";
  if (resolvedLower.find("_unavailable") != std::string::npos) {
    return "requested_backend_unavailable";
  }
  if (backend_request_is_vendor_specific(requestLower) &&
      (resolvedLower.find("mft_enum_hw") != std::string::npos ||
       resolvedLower.find("mft_enum_sw") != std::string::npos ||
       resolvedLower.find("clsid_cmsh264") != std::string::npos)) {
    return "requested_backend_unavailable";
  }
  if (resolvedLower.find("mft_enum_sw") != std::string::npos ||
      resolvedLower.find("clsid_cmsh264") != std::string::npos) {
    return "fallback_to_software";
  }
  if (resolvedLower.find("mft_enum_hw") != std::string::npos) {
    return "fallback_to_generic_hw";
  }
  return "requested_backend_mismatch";
}

}  // namespace remote60::native_poc
