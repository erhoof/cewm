#pragma once

#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <thread>
#include <vector>

/* Host waveIn capture sink (input twin of WaveOutSink). EnsureFormat MUST run on
   the audio thread - CALLBACK_THREAD delivers MM_WIM_DATA only there. The sink
   owns the record buffers; the owner reads each MM_WIM_DATA hdr then Requeue()s it. */
class WaveInSink {
public:
    using ThreadCallback = std::function<void()>;
    using MessageHandler = std::function<void(const MSG&)>;

    WaveInSink() = default;
    ~WaveInSink();
    WaveInSink(const WaveInSink&)            = delete;
    WaveInSink& operator=(const WaveInSink&) = delete;

    void Start(ThreadCallback on_start, MessageHandler on_message,
               const char* log_tag);
    void Stop();

    DWORD ThreadId() const { return thread_id_; }
    void Post(UINT message, WPARAM wparam = 0, LPARAM lparam = 0) const;

    /* Open the device, prepare+add the record buffers, and start recording; on
       the audio thread. Mapper conversion is allowed (no WAVE_FORMAT_DIRECT) so
       any host mic is resampled to the requested format. Returns IsOpen(). */
    bool EnsureFormat(uint32_t sample_rate_hz, uint16_t channels, uint16_t bits);
    bool IsOpen() const { return in_device_ != nullptr; }

    /* Re-arm a drained record buffer (waveInAddBuffer). On-thread. */
    void Requeue(WAVEHDR* hdr);

private:
    static constexpr uint32_t kRecordBuffers  = 8u;
    static constexpr uint32_t kRecordBufBytes = 4096u;

    void ThreadMain(ThreadCallback on_start, MessageHandler on_message);
    void CloseDevice();

    DWORD             thread_id_   = 0;
    HANDLE            ready_event_ = nullptr;
    std::thread       thread_;
    std::atomic<bool> shutdown_{false};

    HWAVEIN  in_device_     = nullptr;
    uint32_t open_rate_     = 0;
    uint16_t open_channels_ = 0;
    uint16_t open_bits_     = 0;

    struct RecBuf {
        WAVEHDR              hdr{};
        std::vector<uint8_t> bytes;
    };
    RecBuf      bufs_[kRecordBuffers];
    const char* log_tag_ = "WaveInSink";
};
