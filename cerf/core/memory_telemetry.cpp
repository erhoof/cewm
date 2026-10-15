#define NOMINMAX
#define _CRT_SECURE_NO_WARNINGS
#include "memory_telemetry.h"
#include "cerf_emulator.h"
#include "log.h"

#include <psapi.h>
#include <tlhelp32.h>
#include <windows.h>

namespace {

struct ThreadStackStats {
    uint64_t count     = 0;
    uint64_t committed = 0;
    uint64_t reserved  = 0;
};

ThreadStackStats MeasureHostThreadStacks() {
    ThreadStackStats out;
    const DWORD our_pid = ::GetCurrentProcessId();

    HANDLE snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return out;

    THREADENTRY32 te;
    memset(&te, 0, sizeof(te));
    te.dwSize = sizeof(te);
    if (!::Thread32First(snap, &te)) {
        ::CloseHandle(snap);
        return out;
    }

    do {
        if (te.th32OwnerProcessID != our_pid) continue;
        out.count++;

        HANDLE ht = ::OpenThread(
            THREAD_QUERY_LIMITED_INFORMATION | THREAD_GET_CONTEXT,
            FALSE, te.th32ThreadID);
        if (!ht) continue;

        CONTEXT ctx;
        memset(&ctx, 0, sizeof(ctx));
        ctx.ContextFlags = CONTEXT_CONTROL;
        if (::GetThreadContext(ht, &ctx)) {
            MEMORY_BASIC_INFORMATION mbi;
            memset(&mbi, 0, sizeof(mbi));
            if (::VirtualQuery(reinterpret_cast<void*>(ctx.Esp), &mbi,
                               sizeof(mbi))) {
                uint8_t* alloc_base =
                    static_cast<uint8_t*>(mbi.AllocationBase);
                uint8_t* p = alloc_base;
                while (true) {
                    MEMORY_BASIC_INFORMATION m;
                    memset(&m, 0, sizeof(m));
                    if (::VirtualQuery(p, &m, sizeof(m)) == 0) break;
                    if (static_cast<uint8_t*>(m.AllocationBase) != alloc_base)
                        break;
                    if (m.State == MEM_COMMIT) out.committed += m.RegionSize;
                    if (m.State == MEM_COMMIT || m.State == MEM_RESERVE)
                        out.reserved += m.RegionSize;
                    p = static_cast<uint8_t*>(m.BaseAddress) + m.RegionSize;
                }
            }
        }
        ::CloseHandle(ht);
    } while (::Thread32Next(snap, &te));

    ::CloseHandle(snap);
    return out;
}

} /* namespace */

MemoryTelemetry::~MemoryTelemetry() {
    stop_.store(true, std::memory_order_release);
    if (stop_event_) ::SetEvent(stop_event_);
    if (thread_.joinable()) thread_.join();
    if (stop_event_) { ::CloseHandle(stop_event_); stop_event_ = NULL; }
    uint64_t peak_mb = peak_private_bytes_.load(std::memory_order_acquire) / (1024 * 1024);
    LOG(Mem, "peak_private_mb=%llu\n", (unsigned long long)peak_mb);
}

void MemoryTelemetry::OnReady() {
    stop_event_ = ::CreateEventW(NULL, TRUE, FALSE, NULL);
    thread_ = std::thread(ThreadMain, this);
    LOG(Mem, "telemetry started (poll=%ums)\n", POLL_INTERVAL_MS);
}

void MemoryTelemetry::ThreadMain(MemoryTelemetry* self) {
    self->Sample();
    while (!self->stop_.load(std::memory_order_relaxed)) {
        DWORD r = ::WaitForSingleObject(self->stop_event_, POLL_INTERVAL_MS);
        if (r == WAIT_OBJECT_0) break;
        self->Sample();
    }
}

void MemoryTelemetry::Sample() {
    PROCESS_MEMORY_COUNTERS_EX pmc;
    memset(&pmc, 0, sizeof(pmc));
    pmc.cb = sizeof(pmc);
    uint64_t private_bytes = 0;
    uint64_t working_set   = 0;
    if (::GetProcessMemoryInfo(::GetCurrentProcess(),
                               reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
                               sizeof(pmc))) {
        private_bytes = pmc.PrivateUsage;
        working_set   = pmc.WorkingSetSize;
    }
    uint64_t prev = peak_private_bytes_.load(std::memory_order_relaxed);
    while (private_bytes > prev &&
           !peak_private_bytes_.compare_exchange_weak(
               prev, private_bytes,
               std::memory_order_release,
               std::memory_order_relaxed)) {
    }

    ThreadStackStats stacks = MeasureHostThreadStacks();

    auto mb = [](uint64_t b) -> unsigned long long { return (unsigned long long)(b / (1024 * 1024)); };
    auto kb = [](uint64_t b) -> unsigned long long { return (unsigned long long)(b / 1024); };

    LOG(Mem,
        "private=%lluMB ws=%lluMB "
        "thread_stacks=%lluKB(%llu threads, reserve=%lluMB)\n",
        mb(private_bytes),
        mb(working_set),
        kb(stacks.committed), (unsigned long long)stacks.count,
        mb(stacks.reserved));
}

REGISTER_SERVICE(MemoryTelemetry);
