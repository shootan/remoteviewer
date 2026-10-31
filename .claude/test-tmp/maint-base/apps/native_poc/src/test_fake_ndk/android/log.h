#pragma once
// TEST ONLY: see test_fake_ndk/fake_ndk.hpp.
enum { ANDROID_LOG_INFO = 4, ANDROID_LOG_ERROR = 6 };
int __android_log_write(int prio, const char* tag, const char* text);
