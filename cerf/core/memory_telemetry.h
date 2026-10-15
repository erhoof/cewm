#pragma once
#include "service.h"
#include <atomic>
#include <cstdint>
#include <thread>
#include <windows.h>

class MemoryTelemetry : public Service {
public:
    explicit MemoryTelemetry(CerfEmulator& emu) : Service(emu) {}
    ~MemoryTelemetry() override;

    void OnReady() override;

private:
    static void ThreadMain(MemoryTelemetry* self);
    void Sample();

    std::thread       thread_;
    std::atomic<bool> stop_{false};
    HANDLE            stop_event_ = NULL;
    std::atomic<uint64_t> peak_private_bytes_{0};

    static constexpr DWORD POLL_INTERVAL_MS = 2000;
};
