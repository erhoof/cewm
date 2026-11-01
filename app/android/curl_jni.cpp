// The slice of libcurl that host/http_proxy.cpp and host/rom_download.cpp use, on top of ru.erhoof.cewm.Http (HttpURLConnection) over JNI.
#include <SDL.h>
#include <jni.h>
#include <curl/curl.h>
#include "app_class.h"
#include <cstdarg>
#include <string>
#include <vector>

struct Easy {
    std::string url, eff;
    long follow = 0, connect_s = 300, nobody = 0, post = 0, failonerror = 0, noprogress = 1, code = 0;
    curl_slist* hdrs = nullptr;
    const char* postdata = nullptr;
    curl_off_t postsize = -1;
    size_t (*write)(char*, size_t, size_t, void*) = nullptr;
    void* writedata = nullptr;
    size_t (*head)(char*, size_t, size_t, void*) = nullptr;
    void* headdata = nullptr;
    int (*xfer)(void*, curl_off_t, curl_off_t, curl_off_t, curl_off_t) = nullptr;
    long low_speed_time = 0;
};

CURL* curl_easy_init() { return new Easy(); }
void curl_easy_cleanup(CURL* c) { delete (Easy*)c; }
int curl_global_init(long) { return 0; }
curl_slist* curl_slist_append(curl_slist* l, const char* s) {
    curl_slist* n = new curl_slist{strdup(s), nullptr};
    if (!l) return n;
    curl_slist* t = l;
    while (t->next) t = t->next;
    t->next = n;
    return l;
}
void curl_slist_free_all(curl_slist* l) {
    while (l) { curl_slist* n = l->next; free(l->data); delete l; l = n; }
}
const char* curl_easy_strerror(CURLcode c) {
    switch (c) {
        case CURLE_OK: return "no error";
        case CURLE_COULDNT_CONNECT: return "couldn't connect";
        case CURLE_HTTP_RETURNED_ERROR: return "HTTP error";
        case CURLE_WRITE_ERROR: return "write error";
        case CURLE_ABORTED_BY_CALLBACK: return "aborted";
        default: return "transfer failed";
    }
}

int curl_easy_setopt(CURL* h, int opt, ...) {
    Easy* e = (Easy*)h;
    va_list a;
    va_start(a, opt);
    switch (opt) {
        case CURLOPT_URL: e->url = va_arg(a, const char*); break;
        case CURLOPT_HTTPHEADER: e->hdrs = va_arg(a, curl_slist*); break;
        case CURLOPT_FOLLOWLOCATION: e->follow = va_arg(a, long); break;
        case CURLOPT_CONNECTTIMEOUT: e->connect_s = va_arg(a, long); break;
        case CURLOPT_TIMEOUT: va_arg(a, long); break;
        case CURLOPT_NOBODY: e->nobody = va_arg(a, long); break;
        case CURLOPT_POST: e->post = va_arg(a, long); break;
        case CURLOPT_POSTFIELDS: e->postdata = va_arg(a, const char*); break;
        case CURLOPT_POSTFIELDSIZE_LARGE: e->postsize = va_arg(a, curl_off_t); break;
        case CURLOPT_FAILONERROR: e->failonerror = va_arg(a, long); break;
        case CURLOPT_NOPROGRESS: e->noprogress = va_arg(a, long); break;
        case CURLOPT_LOW_SPEED_TIME: e->low_speed_time = va_arg(a, long); break;
        case CURLOPT_WRITEFUNCTION: e->write = va_arg(a, size_t (*)(char*, size_t, size_t, void*)); break;
        case CURLOPT_WRITEDATA: e->writedata = va_arg(a, void*); break;
        case CURLOPT_HEADERFUNCTION: e->head = va_arg(a, size_t (*)(char*, size_t, size_t, void*)); break;
        case CURLOPT_HEADERDATA: e->headdata = va_arg(a, void*); break;
        case CURLOPT_XFERINFOFUNCTION: e->xfer = va_arg(a, int (*)(void*, curl_off_t, curl_off_t, curl_off_t, curl_off_t)); break;
        default: break;  // MAXREDIRS, NOSIGNAL, ACCEPT_ENCODING, LOW_SPEED_LIMIT: the Java side handles or ignores them
    }
    va_end(a);
    return 0;
}

int curl_easy_getinfo(CURL* h, int info, ...) {
    Easy* e = (Easy*)h;
    va_list a;
    va_start(a, info);
    if (info == CURLINFO_EFFECTIVE_URL) *va_arg(a, const char**) = e->eff.c_str();
    else if (info == CURLINFO_RESPONSE_CODE) *va_arg(a, long*) = e->code;
    va_end(a);
    return 0;
}

jclass AppClass(JNIEnv* env, const char* name) {
    jobject act = (jobject)SDL_AndroidGetActivity();
    if (!act) return nullptr;
    jclass activity = env->FindClass("android/app/Activity");
    jclass loader = env->FindClass("java/lang/ClassLoader");
    jobject ldr = env->CallObjectMethod(act, env->GetMethodID(activity, "getClassLoader", "()Ljava/lang/ClassLoader;"));
    jstring n = env->NewStringUTF(name);
    jclass c = ldr ? (jclass)env->CallObjectMethod(ldr, env->GetMethodID(loader, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;"), n) : nullptr;
    if (env->ExceptionCheck()) { env->ExceptionClear(); c = nullptr; }
    env->DeleteLocalRef(n);
    if (ldr) env->DeleteLocalRef(ldr);
    env->DeleteLocalRef(loader);
    env->DeleteLocalRef(activity);
    env->DeleteLocalRef(act);
    return c;
}

namespace {
jclass HttpClass(JNIEnv* env) {
    static jclass cls = nullptr;
    if (!cls) if (jclass c = AppClass(env, "ru.erhoof.cewm.Http")) cls = (jclass)env->NewGlobalRef(c);
    return cls;
}
}  // namespace

CURLcode curl_easy_perform(CURL* h) {
    Easy* e = (Easy*)h;
    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    jclass cls = HttpClass(env);
    if (!cls) return CURLE_COULDNT_CONNECT;
    std::vector<std::string> hv;
    for (curl_slist* l = e->hdrs; l; l = l->next) hv.push_back(l->data);
    jobjectArray harr = env->NewObjectArray((jsize)hv.size(), env->FindClass("java/lang/String"), nullptr);
    for (size_t i = 0; i < hv.size(); i++) env->SetObjectArrayElement(harr, (jsize)i, env->NewStringUTF(hv[i].c_str()));
    jbyteArray body = nullptr;
    if (e->post && e->postdata) {
        const jsize n = e->postsize >= 0 ? (jsize)e->postsize : (jsize)strlen(e->postdata);
        body = env->NewByteArray(n);
        env->SetByteArrayRegion(body, 0, n, (const jbyte*)e->postdata);
    }
    jmethodID open = env->GetStaticMethodID(cls, "open", "(Ljava/lang/String;Ljava/lang/String;[Ljava/lang/String;[BZII)Lru/erhoof/cewm/Http;");
    jobject http = env->CallStaticObjectMethod(cls, open, env->NewStringUTF(e->url.c_str()), env->NewStringUTF(e->nobody ? "HEAD" : e->post ? "POST" : "GET"), harr, body,
                                               (jboolean)(e->follow != 0), (jint)(e->connect_s * 1000), (jint)((e->low_speed_time ? e->low_speed_time : 60) * 1000));
    if (env->ExceptionCheck() || !http) { env->ExceptionClear(); return CURLE_COULDNT_CONNECT; }

    jclass hc = env->GetObjectClass(http);
    e->code = env->GetIntField(http, env->GetFieldID(hc, "code", "I"));
    jstring js = (jstring)env->GetObjectField(http, env->GetFieldID(hc, "url", "Ljava/lang/String;"));
    const char* u = env->GetStringUTFChars(js, nullptr);
    e->eff = u;
    env->ReleaseStringUTFChars(js, u);
    const jlong total = env->GetLongField(http, env->GetFieldID(hc, "length", "J"));
    jmethodID close = env->GetMethodID(hc, "close", "()V");
    CURLcode rc = CURLE_OK;
    if (e->head) {
        js = (jstring)env->GetObjectField(http, env->GetFieldID(hc, "headers", "Ljava/lang/String;"));
        const char* hs = env->GetStringUTFChars(js, nullptr);
        std::string all = hs;
        env->ReleaseStringUTFChars(js, hs);
        for (size_t p = 0; p < all.size();) {  // line by line, CRLF kept, like libcurl; the blank line ends each hop
            size_t q = all.find("\r\n", p);
            if (q == std::string::npos) break;
            e->head(&all[p], 1, q + 2 - p, e->headdata);
            p = q + 2;
        }
    }
    if (e->failonerror && e->code >= 400) rc = CURLE_HTTP_RETURNED_ERROR;
    if (rc == CURLE_OK) {
        jmethodID rd = env->GetMethodID(hc, "read", "([B)I");
        jbyteArray buf = env->NewByteArray(65536);
        std::vector<char> tmp(65536);
        curl_off_t got = 0;
        for (;;) {
            const jint n = env->CallIntMethod(http, rd, buf);
            if (n == -1) break;
            if (n < 0) { rc = (CURLcode)56; break; }
            env->GetByteArrayRegion(buf, 0, n, (jbyte*)tmp.data());
            if (e->write && e->write(tmp.data(), 1, n, e->writedata) != (size_t)n) { rc = CURLE_WRITE_ERROR; break; }
            got += n;
            if (!e->noprogress && e->xfer && e->xfer(nullptr, total > 0 ? total : 0, got, 0, 0)) { rc = CURLE_ABORTED_BY_CALLBACK; break; }
        }
    }
    env->CallVoidMethod(http, close);
    return rc;
}
