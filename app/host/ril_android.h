#pragma once
#include <string>
#ifdef __ANDROID__
#include <SDL.h>
#include "../android/app_class.h"
#endif

namespace cewm {
// All JNI calls originate on the emulator thread; Java serializes Telecom operations on its main thread.
inline std::string AndroidRil(const std::string& command) {
#ifdef __ANDROID__
    auto* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    jclass cls = AppClass(env, "ru.erhoof.cewm.PhoneService");
    if (!cls) return "";
    jmethodID method = env->GetStaticMethodID(cls, "request", "(Ljava/lang/String;)Ljava/lang/String;");
    jstring arg = env->NewStringUTF(command.c_str());
    jstring result = method ? (jstring)env->CallStaticObjectMethod(cls, method, arg) : nullptr;
    std::string out;
    if (env->ExceptionCheck()) env->ExceptionClear();
    else if (result) {
        const char* chars = env->GetStringUTFChars(result, nullptr);
        if (chars) { out = chars; env->ReleaseStringUTFChars(result, chars); }
    }
    if (result) env->DeleteLocalRef(result);
    env->DeleteLocalRef(arg);
    env->DeleteLocalRef(cls);
    return out;
#else
    (void)command;
    return "";
#endif
}
}
