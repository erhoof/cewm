// The Android build has no libcurl: android/curl_jni.cpp implements this slice on top of java.net.HttpURLConnection.
#pragma once
#include <stddef.h>
#include <stdint.h>
typedef void CURL;
typedef int CURLcode;
typedef int64_t curl_off_t;
struct curl_slist { char* data; curl_slist* next; };
enum { CURLE_OK = 0, CURLE_COULDNT_CONNECT = 7, CURLE_HTTP_RETURNED_ERROR = 22, CURLE_WRITE_ERROR = 23, CURLE_ABORTED_BY_CALLBACK = 42 };
enum { CURL_GLOBAL_DEFAULT = 3 };
enum { CURLINFO_EFFECTIVE_URL, CURLINFO_RESPONSE_CODE };
enum { CURLOPT_ACCEPT_ENCODING, CURLOPT_CONNECTTIMEOUT, CURLOPT_FAILONERROR, CURLOPT_FOLLOWLOCATION, CURLOPT_HEADERDATA, CURLOPT_HEADERFUNCTION,
       CURLOPT_HTTPHEADER, CURLOPT_LOW_SPEED_LIMIT, CURLOPT_LOW_SPEED_TIME, CURLOPT_MAXREDIRS, CURLOPT_NOBODY, CURLOPT_NOPROGRESS, CURLOPT_NOSIGNAL,
       CURLOPT_POST, CURLOPT_POSTFIELDS, CURLOPT_POSTFIELDSIZE_LARGE, CURLOPT_TIMEOUT, CURLOPT_URL, CURLOPT_WRITEDATA, CURLOPT_WRITEFUNCTION,
       CURLOPT_XFERINFOFUNCTION };
CURL* curl_easy_init();
void curl_easy_cleanup(CURL*);
int curl_easy_setopt(CURL*, int, ...);
int curl_easy_getinfo(CURL*, int, ...);
CURLcode curl_easy_perform(CURL*);
const char* curl_easy_strerror(CURLcode);
int curl_global_init(long);
curl_slist* curl_slist_append(curl_slist*, const char*);
void curl_slist_free_all(curl_slist*);
