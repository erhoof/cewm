#pragma once

#define NOMINMAX

#include "wave_in_sink.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>

class WaveInFifo {
public:
    WaveInFifo() = default;
    WaveInFifo(const WaveInFifo&)            = delete;
    WaveInFifo& operator=(const WaveInFifo&) = delete;

    void Start(const char* log_tag, size_t cap_bytes);
    void Stop();

    void   Request(uint32_t sample_rate_hz, uint16_t channels, uint16_t bits);
    void   Clear();
    size_t Take(uint8_t* dst, size_t bytes);

private:
    static uint64_t FormatKey(uint32_t sample_rate_hz, uint16_t channels, uint16_t bits);

    void OnThreadMessage(const MSG& msg);
    void OpenOnThread();
    void OnRecordedData(const uint8_t* data, uint32_t bytes);

    WaveInSink            sink_;
    std::mutex            mtx_;
    std::deque<uint8_t>   fifo_;
    std::atomic<uint64_t> requested_{0};
    uint64_t              open_      = 0;
    size_t                cap_bytes_ = 0;
    const char*           log_tag_   = "WaveInFifo";
};
