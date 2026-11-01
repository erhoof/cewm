// SDL 2.30.0 calls ALooper_pollAll, which NDK r26+ marks unavailable; declare the NDK's own first, then alias.
#include <android/looper.h>
#define ALooper_pollAll ALooper_pollOnce
