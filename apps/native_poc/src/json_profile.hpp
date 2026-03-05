#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <regex>
#include <string>

namespace remote60::native_poc::json_profile {

inline bool load_json_text_file(const std::string& path, std::string* outText, std::string* outError) {
  if (!outText) return false;
  std::ifstream ifs(path, std::ios::binary);
  if (!ifs.is_open()) {
    if (outError) *outError = "failed to open file";
    return false;
  }
  std::string text((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
  *outText = std::move(text);
  if (outError) outError->clear();
  return true;
}

inline std::string regex_escape(const std::string& s) {
  static const std::regex kMeta(R"([-[\]{}()*+?.,\^$|#\s])");
  return std::regex_replace(s, kMeta, R"(\$&)");
}

inline std::string json_unescape(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    const char c = s[i];
    if (c != '\\' || i + 1 >= s.size()) {
      out.push_back(c);
      continue;
    }
    const char n = s[++i];
    switch (n) {
      case '"':
        out.push_back('"');
        break;
      case '\\':
        out.push_back('\\');
        break;
      case '/':
        out.push_back('/');
        break;
      case 'b':
        out.push_back('\b');
        break;
      case 'f':
        out.push_back('\f');
        break;
      case 'n':
        out.push_back('\n');
        break;
      case 'r':
        out.push_back('\r');
        break;
      case 't':
        out.push_back('\t');
        break;
      default:
        out.push_back(n);
        break;
    }
  }
  return out;
}

inline bool json_get_string(const std::string& json, const std::string& key, std::string* out) {
  if (!out) return false;
  const std::regex re("\"" + regex_escape(key) + "\"\\s*:\\s*\"((?:\\\\.|[^\"\\\\])*)\"");
  std::smatch m;
  if (!std::regex_search(json, m, re) || m.size() < 2) return false;
  *out = json_unescape(m[1].str());
  return true;
}

inline bool json_get_u32(const std::string& json, const std::string& key, uint32_t* out) {
  if (!out) return false;
  const std::regex re("\"" + regex_escape(key) + "\"\\s*:\\s*([0-9]+)");
  std::smatch m;
  if (!std::regex_search(json, m, re) || m.size() < 2) return false;
  const unsigned long long v = std::strtoull(m[1].str().c_str(), nullptr, 10);
  *out = static_cast<uint32_t>(std::min<unsigned long long>(v, 0xFFFFFFFFull));
  return true;
}

inline bool json_get_bool(const std::string& json, const std::string& key, bool* out) {
  if (!out) return false;
  const std::regex re("\"" + regex_escape(key) + "\"\\s*:\\s*(true|false)");
  std::smatch m;
  if (!std::regex_search(json, m, re) || m.size() < 2) return false;
  *out = (m[1].str() == "true");
  return true;
}

inline void set_env_or_unset(const char* key, const std::string& value, bool set) {
  if (!key) return;
  if (set) {
    _putenv_s(key, value.c_str());
  } else {
    _putenv_s(key, "");
  }
}

inline void clear_runtime_env_overrides() {
  static const char* kKeys[] = {
      "REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE",
      "REMOTE60_NATIVE_ENCODER_BACKEND",
      "REMOTE60_NATIVE_DECODER_BACKEND",
      "REMOTE60_NATIVE_H264_NO_PACING",
      "REMOTE60_NATIVE_GUARD_STALE_PREENCODE",
      "REMOTE60_NATIVE_CAPTURE_POOL_BUFFERS",
      "REMOTE60_NATIVE_GUARD_STALE_ENCODED",
      "REMOTE60_NATIVE_ABR_DISABLE",
      "REMOTE60_NATIVE_FRAME_GATING_DISABLE",
      "REMOTE60_NATIVE_STATIC_SCENE_FPS",
      "REMOTE60_NATIVE_FRAME_GATING_STATIC_THRESHOLD_PM",
      "REMOTE60_NATIVE_FRAME_GATING_MOTION_THRESHOLD_PM",
      "REMOTE60_NATIVE_KEYREQ_MIN_INTERVAL_US",
      "REMOTE60_NATIVE_KEYREQ_TOKEN_REFILL_US",
      "REMOTE60_NATIVE_KEYREQ_TOKEN_CAPACITY",
      "REMOTE60_NATIVE_KEYFRAME_REQ_MIN_INTERVAL_US",
      "REMOTE60_NATIVE_KEYFRAME_REQ_TOKEN_REFILL_US",
      "REMOTE60_NATIVE_KEYFRAME_REQ_TOKEN_CAPACITY",
      "REMOTE60_NATIVE_CATCHUP_REENTER_MIN_INTERVAL_US",
      "REMOTE60_NATIVE_STALE_CAPTURE_DROP_US",
      "REMOTE60_NATIVE_CONGEST_RECOVER_MIN_US",
      "REMOTE60_NATIVE_CONGEST_RECOVERY_TIMEOUT_US",
      "REMOTE60_NATIVE_M9_ENABLE",
      "REMOTE60_NATIVE_M9_APPLY",
      "REMOTE60_NATIVE_M9_COOLDOWN_SEC",
      "REMOTE60_NATIVE_M9_DOWN_REQUIRE_SEC",
      "REMOTE60_NATIVE_M9_UP_REQUIRE_SEC",
  };
  for (const char* key : kKeys) {
    _putenv_s(key, "");
  }
}

inline void apply_runtime_env_overrides_from_json(const std::string& json) {
  clear_runtime_env_overrides();

  auto set_bool_flag = [&](const char* jsonKey, const char* envKey) {
    bool v = false;
    if (json_get_bool(json, jsonKey, &v) && v) {
      set_env_or_unset(envKey, "1", true);
    }
  };
  auto set_u32_if_pos = [&](const char* jsonKey, const char* envKey) {
    uint32_t v = 0;
    if (json_get_u32(json, jsonKey, &v) && v > 0) {
      set_env_or_unset(envKey, std::to_string(v), true);
    }
  };
  auto set_str_if_nonempty = [&](const char* jsonKey, const char* envKey) {
    std::string v;
    if (json_get_string(json, jsonKey, &v) && !v.empty()) {
      set_env_or_unset(envKey, v, true);
    }
  };

  set_bool_flag("forceEncodedExperiment", "REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE");
  set_str_if_nonempty("encoderBackend", "REMOTE60_NATIVE_ENCODER_BACKEND");
  set_str_if_nonempty("decoderBackend", "REMOTE60_NATIVE_DECODER_BACKEND");
  set_bool_flag("h264NoPacing", "REMOTE60_NATIVE_H264_NO_PACING");
  set_bool_flag("guardStalePreEncode", "REMOTE60_NATIVE_GUARD_STALE_PREENCODE");
  set_u32_if_pos("capturePoolBuffers", "REMOTE60_NATIVE_CAPTURE_POOL_BUFFERS");
  set_bool_flag("guardStaleEncoded", "REMOTE60_NATIVE_GUARD_STALE_ENCODED");
  set_bool_flag("abrDisable", "REMOTE60_NATIVE_ABR_DISABLE");
  set_bool_flag("frameGatingDisable", "REMOTE60_NATIVE_FRAME_GATING_DISABLE");
  set_u32_if_pos("staticSceneFps", "REMOTE60_NATIVE_STATIC_SCENE_FPS");
  set_u32_if_pos("frameGatingStaticThresholdPm", "REMOTE60_NATIVE_FRAME_GATING_STATIC_THRESHOLD_PM");
  set_u32_if_pos("frameGatingMotionThresholdPm", "REMOTE60_NATIVE_FRAME_GATING_MOTION_THRESHOLD_PM");
  set_u32_if_pos("catchupReenterMinIntervalUs", "REMOTE60_NATIVE_CATCHUP_REENTER_MIN_INTERVAL_US");
  set_u32_if_pos("staleCaptureDropUs", "REMOTE60_NATIVE_STALE_CAPTURE_DROP_US");
  set_u32_if_pos("congestRecoverMinUs", "REMOTE60_NATIVE_CONGEST_RECOVER_MIN_US");
  set_u32_if_pos("congestRecoveryTimeoutUs", "REMOTE60_NATIVE_CONGEST_RECOVERY_TIMEOUT_US");
  set_bool_flag("m9Enable", "REMOTE60_NATIVE_M9_ENABLE");
  set_bool_flag("m9Apply", "REMOTE60_NATIVE_M9_APPLY");
  set_u32_if_pos("m9CooldownSec", "REMOTE60_NATIVE_M9_COOLDOWN_SEC");
  set_u32_if_pos("m9DownRequireSec", "REMOTE60_NATIVE_M9_DOWN_REQUIRE_SEC");
  set_u32_if_pos("m9UpRequireSec", "REMOTE60_NATIVE_M9_UP_REQUIRE_SEC");

  uint32_t keyMin = 0;
  if (json_get_u32(json, "keyframeReqMinIntervalUs", &keyMin) && keyMin > 0) {
    const std::string s = std::to_string(keyMin);
    set_env_or_unset("REMOTE60_NATIVE_KEYREQ_MIN_INTERVAL_US", s, true);
    set_env_or_unset("REMOTE60_NATIVE_KEYFRAME_REQ_MIN_INTERVAL_US", s, true);
  }
  uint32_t keyRefill = 0;
  if (json_get_u32(json, "keyframeReqTokenRefillUs", &keyRefill) && keyRefill > 0) {
    const std::string s = std::to_string(keyRefill);
    set_env_or_unset("REMOTE60_NATIVE_KEYREQ_TOKEN_REFILL_US", s, true);
    set_env_or_unset("REMOTE60_NATIVE_KEYFRAME_REQ_TOKEN_REFILL_US", s, true);
  }
  uint32_t keyCap = 0;
  if (json_get_u32(json, "keyframeReqTokenCapacity", &keyCap) && keyCap > 0) {
    const std::string s = std::to_string(keyCap);
    set_env_or_unset("REMOTE60_NATIVE_KEYREQ_TOKEN_CAPACITY", s, true);
    set_env_or_unset("REMOTE60_NATIVE_KEYFRAME_REQ_TOKEN_CAPACITY", s, true);
  }
}

}  // namespace remote60::native_poc::json_profile
