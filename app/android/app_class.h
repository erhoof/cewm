// Looks up an app class (e.g. "ru.erhoof.cewm.Http") from a native thread, where plain FindClass only sees the system loader. Null if missing.
#pragma once
#include <jni.h>
jclass AppClass(JNIEnv* env, const char* dotted_name);
