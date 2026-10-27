#pragma once

// keepalive-r2 ② (b, e): the install's standby device id + last generation, in one small file (the app's own files dir on
// Android; a profile dir on a PC). Written whole to "<path>.tmp" and moved over the old one -- a crash leaves the old or
// the new, never a half. Unreadable, short or long = nothing usable: GenerationAllocator then makes a new device id (the
// old entry at a host lapses on its lease). Never logged; nothing secret is in it (an id and a counter).

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#endif

#include "standby_entry_client.hpp"

namespace remote60::native_poc {

class FileGenerationStore : public StandbyGenerationStore {
 public:
  explicit FileGenerationStore(std::string path) : path_(std::move(path)) {}

  bool Load(std::array<uint8_t, 16>* id, uint32_t* last) override {
    FILE* f = std::fopen(path_.c_str(), "rb");
    if (!f) return false;
    uint8_t buf[21] = {};
    const size_t n = std::fread(buf, 1, sizeof(buf), f);
    std::fclose(f);
    if (n != 20) return false;  // a short or longer file is damaged
    std::memcpy(id->data(), buf, 16);
    std::memcpy(last, buf + 16, 4);
    return true;
  }

  bool Save(const std::array<uint8_t, 16>& id, uint32_t last) override {
    const std::string tmp = path_ + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return false;
    uint8_t buf[20];
    std::memcpy(buf, id.data(), 16);
    std::memcpy(buf + 16, &last, 4);
    const bool wrote = std::fwrite(buf, 1, sizeof(buf), f) == sizeof(buf) && std::fflush(f) == 0;
    std::fclose(f);
    if (!wrote) return false;
#if defined(_WIN32)
    // std::rename does not replace an existing file on Windows
    return MoveFileExA(tmp.c_str(), path_.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
#else
    return std::rename(tmp.c_str(), path_.c_str()) == 0;  // atomic replace on POSIX
#endif
  }

 private:
  std::string path_;
};

}  // namespace remote60::native_poc
