// Just the slice of GLib that vendored libslirp uses, so the Android build needs no GLib. G_OS_UNIX stays undefined: slirp's process spawning is left out.
#pragma once
#include <assert.h>
#include <endian.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <android/log.h>

typedef char gchar;
typedef int gint;
typedef unsigned guint;
typedef int gboolean;
typedef void* gpointer;
typedef const void* gconstpointer;
typedef size_t gsize;
typedef ssize_t gssize;
#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif
#define G_BYTE_ORDER __BYTE_ORDER
#define G_LITTLE_ENDIAN __LITTLE_ENDIAN
#define G_BIG_ENDIAN __BIG_ENDIAN
#define G_UNLIKELY(x) __builtin_expect(!!(x), 0)
#define G_LIKELY(x) __builtin_expect(!!(x), 1)
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif
#define G_N_ELEMENTS(a) (sizeof(a) / sizeof((a)[0]))
#define G_STATIC_ASSERT(e) static_assert(e, #e)
#define G_GNUC_PRINTF(f, a) __attribute__((format(printf, f, a)))
#define G_GNUC_UNUSED __attribute__((unused))
#define G_SIZEOF_MEMBER(t, m) sizeof(((t*)0)->m)
#ifndef G_LOG_DOMAIN
#define G_LOG_DOMAIN "glib"
#endif

#define g_warning(...) __android_log_print(ANDROID_LOG_WARN, G_LOG_DOMAIN, __VA_ARGS__)
#define g_critical(...) __android_log_print(ANDROID_LOG_ERROR, G_LOG_DOMAIN, __VA_ARGS__)
#define g_debug(...) ((void)0)
#define g_error(...) (__android_log_print(ANDROID_LOG_FATAL, G_LOG_DOMAIN, __VA_ARGS__), abort())
#define g_assert(e) assert(e)
#define g_assert_not_reached() abort()
#define g_warn_if_reached() g_warning("reached %s:%d", __FILE__, __LINE__)
#define g_warn_if_fail(e) do { if (!(e)) g_warning("failed: %s", #e); } while (0)
#define g_return_if_fail(e) do { if (!(e)) { g_warning("failed: %s", #e); return; } } while (0)
#define g_return_val_if_fail(e, v) do { if (!(e)) { g_warning("failed: %s", #e); return (v); } } while (0)

#define g_malloc(n) malloc(n)
#define g_malloc0(n) calloc(1, (n))
#define g_realloc(p, n) realloc((p), (n))
#define g_free(p) free(p)
#define g_new(t, n) ((t*)malloc(sizeof(t) * (n)))
#define g_new0(t, n) ((t*)calloc((n), sizeof(t)))
#define g_strdup(s) ((s) ? strdup(s) : NULL)
#define g_strerror(e) strerror(e)
#define g_snprintf snprintf
#define g_vsnprintf vsnprintf
#define g_ascii_strcasecmp strcasecmp
#define g_str_has_prefix(s, p) (strncmp((s), (p), strlen(p)) == 0)
#define g_getenv(n) getenv(n)
static inline gsize g_strlcpy(gchar* d, const gchar* s, gsize n) {
    const gsize l = strlen(s);
    if (n) { const gsize c = l < n - 1 ? l : n - 1; memcpy(d, s, c); d[c] = 0; }
    return l;
}
static inline gchar* g_strstr_len(const gchar* h, gssize len, const gchar* n) {
    const gsize nl = strlen(n);
    if (len < 0) return (gchar*)strstr(h, n);
    for (gssize i = 0; i + (gssize)nl <= len; i++) if (!memcmp(h + i, n, nl)) return (gchar*)h + i;
    return NULL;
}

typedef gchar** GStrv;
static inline guint g_strv_length(gchar** v) { guint n = 0; while (v && v[n]) n++; return n; }

typedef struct { gchar* str; gsize len, alloc; } GString;
static inline GString* g_string_new(const gchar* s) {
    GString* g = (GString*)malloc(sizeof *g);
    g->len = s ? strlen(s) : 0;
    g->alloc = g->len + 1;
    g->str = (gchar*)malloc(g->alloc);
    memcpy(g->str, s ? s : "", g->len + 1);
    return g;
}
static inline void g_string_append_printf(GString* g, const gchar* f, ...) G_GNUC_PRINTF(2, 3);
static inline void g_string_append_printf(GString* g, const gchar* f, ...) {
    va_list a, b;
    va_start(a, f);
    va_copy(b, a);
    const int n = vsnprintf(NULL, 0, f, a);
    va_end(a);
    if (g->len + n + 1 > g->alloc) { g->alloc = (g->len + n + 1) * 2; g->str = (gchar*)realloc(g->str, g->alloc); }
    vsnprintf(g->str + g->len, n + 1, f, b);
    va_end(b);
    g->len += n;
}
static inline gchar* g_string_free(GString* g, gboolean free_seg) {
    gchar* s = g->str;
    free(g);
    if (free_seg) { free(s); return NULL; }
    return s;
}

typedef struct GRand { uint64_t s; } GRand;
static inline GRand* g_rand_new(void) {
    GRand* r = (GRand*)malloc(sizeof *r);
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    r->s = (uint64_t)t.tv_nsec * 6364136223846793005ull + (uint64_t)t.tv_sec + 1;
    return r;
}
static inline void g_rand_free(GRand* r) { free(r); }
static inline int g_rand_int_range(GRand* r, int b, int e) {  // splitmix64
    uint64_t z = (r->s += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return b + (int)((z ^ (z >> 31)) % (uint64_t)(e - b));
}
typedef struct { const gchar* key; guint value; } GDebugKey;
static inline guint g_parse_debug_string(const gchar* s, const GDebugKey* k, guint n) {
    guint r = 0;
    for (guint i = 0; s && i < n; i++) if (strstr(s, k[i].key)) r |= k[i].value;
    return r;
}

#define GLIB_CHECK_VERSION(a, b, c) 1
#define GUINT16_TO_BE(v) __builtin_bswap16((uint16_t)(v))
#define GUINT16_FROM_BE(v) __builtin_bswap16((uint16_t)(v))
#define GUINT32_TO_BE(v) __builtin_bswap32((uint32_t)(v))
#define GUINT32_FROM_BE(v) __builtin_bswap32((uint32_t)(v))
#define GINT16_TO_BE(v) ((int16_t)__builtin_bswap16((uint16_t)(v)))
#define GINT16_FROM_BE(v) ((int16_t)__builtin_bswap16((uint16_t)(v)))
#define GINT32_TO_BE(v) ((int32_t)__builtin_bswap32((uint32_t)(v)))
#define GINT32_FROM_BE(v) ((int32_t)__builtin_bswap32((uint32_t)(v)))

// Process spawning (slirp's guestfwd "exec:" rule) is not supported: the stubs always fail.
typedef struct { const gchar* message; } GError;
typedef int GPid;
typedef int GSpawnFlags;
typedef void (*GSpawnChildSetupFunc)(gpointer);
#define G_SPAWN_SEARCH_PATH 0
static inline void g_error_free(GError*) {}
static inline gboolean g_shell_parse_argv(const gchar*, gint*, gchar***, GError** e) {
    static GError err = {"unsupported"};
    if (e) *e = &err;
    return FALSE;
}
static inline gboolean g_spawn_async_with_fds(const gchar*, gchar**, gchar**, GSpawnFlags, GSpawnChildSetupFunc, gpointer, GPid*, gint, gint, gint, GError**) { return FALSE; }
static inline void g_strfreev(gchar** v) { if (!v) return; for (gchar** p = v; *p; p++) free(*p); free(v); }
