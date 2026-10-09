#pragma once
// TEST ONLY: the few JNI types android_video_decoder.hpp names, so the real decoder source can be
// compiled into a Windows test (android_decoder_owed_key_test). Never on an Android include path.
struct _JNIEnv {};
typedef _JNIEnv JNIEnv;
typedef void* jobject;
typedef int jint;
typedef long long jlong;
typedef unsigned char jboolean;
