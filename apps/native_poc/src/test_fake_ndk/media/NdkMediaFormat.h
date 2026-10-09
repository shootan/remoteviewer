#pragma once
// TEST ONLY: see test_fake_ndk/fake_ndk.hpp.
#include <cstddef>
#include <cstdint>
struct AMediaFormat;
typedef int media_status_t;
enum { AMEDIA_OK = 0, AMEDIA_ERROR_UNKNOWN = -10000 };
extern const char* AMEDIAFORMAT_KEY_MIME;
extern const char* AMEDIAFORMAT_KEY_WIDTH;
extern const char* AMEDIAFORMAT_KEY_HEIGHT;
extern const char* AMEDIAFORMAT_KEY_CSD_0;
extern const char* AMEDIAFORMAT_KEY_CSD_1;
extern const char* AMEDIAFORMAT_KEY_PRIORITY;
AMediaFormat* AMediaFormat_new();
media_status_t AMediaFormat_delete(AMediaFormat* format);
void AMediaFormat_setString(AMediaFormat* format, const char* name, const char* value);
void AMediaFormat_setInt32(AMediaFormat* format, const char* name, int32_t value);
void AMediaFormat_setBuffer(AMediaFormat* format, const char* name, const void* data, size_t size);
bool AMediaFormat_getInt32(AMediaFormat* format, const char* name, int32_t* out);
