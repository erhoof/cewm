// Audio sink for S3C2410Iis (upstream PacedWaveOut's interface, backed by SDL audio; see paced_wave_out_sdl.cpp).
#pragma once
#include <cstdint>
#include <functional>
#include <memory>

class PacedWaveOut {
public:
    PacedWaveOut();
    ~PacedWaveOut();
    void Start(const char* tag, uint32_t rate, uint16_t channels, uint16_t bits, bool allow_resampler);
    void Stop();
    void SetFormat(uint32_t rate_hz, uint16_t channels, uint16_t bits);
    void BeginAudioOut(std::function<void()> on_block_done);
    bool QueueOutput(const void* bytes, uint32_t length);
    void FinishAudioOut();
    void StopAudioOut();

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};
