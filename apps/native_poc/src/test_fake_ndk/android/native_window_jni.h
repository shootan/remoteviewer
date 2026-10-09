#pragma once
// TEST ONLY: see test_fake_ndk/fake_ndk.hpp. A "surface" is any non-null pointer; the window is it.
#include <jni.h>
struct ANativeWindow;
ANativeWindow* ANativeWindow_fromSurface(JNIEnv* env, jobject surface);
void ANativeWindow_release(ANativeWindow* window);
