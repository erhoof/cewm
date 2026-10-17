// POSIX replacement for cerf/cerf/core/log.cpp + log_emergency.cpp (MIT upstream, ring-buffer/Win32 logger). Same `Log::` API,
// plain mutex-guarded stdio. Shortcuts: no flood suppression, no ring writer thread, no native stack dumps, no symbolizer.
#include "core/log.h"

#include <unistd.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

std::atomic<uint64_t> Log::detail::enabled_mask{Log::MASK_PRODUCTION_DEFAULT};

namespace {
std::mutex g_mu;
FILE* g_file = nullptr;
bool g_flush = false;
bool g_emergency = false;
char g_recent[2048];  // last caution lines, for CopyRecentCautionsPostFreeze

void Emit(const char* line, size_t len) {
    fwrite(line, 1, len, stderr);
    if (g_file) {
        fwrite(line, 1, len, g_file);
        if (g_flush) fflush(g_file);
    }
}
}  // namespace

void Log::SetEnabled(uint64_t mask) { detail::enabled_mask.store(mask, std::memory_order_relaxed); }
void Log::SetFlush(bool enabled) { g_flush = enabled; }
void Log::SetAllowFlood(bool) {}
void Log::InitDefaultLogFile() {}
void Log::SetFile(const char* path) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_file) fclose(g_file);
    g_file = path && *path ? fopen(path, "w") : nullptr;
}
void Log::Close() {
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_file) fclose(g_file);
    g_file = nullptr;
}

void Log::Print(Cat cat, const char* fmt, ...) {
    char body[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(body, sizeof body, fmt, ap);
    va_end(ap);
    char line[2200];
    const int n = snprintf(line, sizeof line, "[%s] %s", kCategories[(int)cat].slug, body);
    std::lock_guard<std::mutex> lk(g_mu);
    Emit(line, n < 0 ? 0 : (size_t)n < sizeof line ? (size_t)n : sizeof line - 1);
    if (cat == Cat::Caution) snprintf(g_recent, sizeof g_recent, "%s", line);
}

void Log::CopyRecentCautionsPostFreeze(char* out, size_t out_size) {
    if (out_size) snprintf(out, out_size, "%s", g_recent);
}
bool Log::SymbolizeAddress(const void*, char*, size_t) { return false; }

void Log::EmergencyStart() { g_emergency = true; }
bool Log::IsEmergencyActive() { return g_emergency; }
void Log::Emergency(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}
void Log::EmergencyWriteBytes(const char* buf, size_t len) { fwrite(buf, 1, len, stderr); }
void Log::EmergencyPrintNativeStack(const char*) {}
void Log::EmergencyDumpAllThreadStacks() {}
void Log::InstallCrashHandler() {}

[[noreturn]] void CerfFatalExit(int code) {
    fflush(stderr);
    if (code != CERF_FATAL_NORMAL_EXIT) _exit(code);
    exit(code);
}
