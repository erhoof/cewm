// SDL audio output for the guest's I2S stream. The S3C2410 IIS model hands over 2 KB blocks of 16-bit stereo in guest time at the
// guest's current sample rate. They are resampled to the device rate (linear interpolation) with a ratio that follows the queue depth:
// a shallow queue stretches the sound a little (slower, up to -6%), a deep one squeezes it (faster, up to +6%), so a guest that runs
// slightly slow or jittery is smoothed instead of crackling. When the queue still runs dry, playback pauses and resumes once a cushion
// has built up (one clean gap instead of a stream of micro-gaps); when the guest runs far ahead the excess is dropped.
// CEWM_WAV=<file> also dumps the raw guest PCM (s16le stereo) for checking the data flow without a sound card.
// CEWM_AUDIOLOG=1 prints queue depth, adjustment and under/overruns once a second. CEWM_NOSTRETCH=1 turns the rate adaptation off.
#include "paced_wave_out_stub.h"

#include <SDL.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <vector>

struct PacedWaveOut::Impl {
    std::mutex mu;
    SDL_AudioDeviceID dev = 0;
    SDL_AudioSpec have{};
    uint32_t rate = 0;       // guest sample rate
    FILE* dump = nullptr;
    // resampler state: read position in input frames relative to the current block (-1 = the carried last frame of the previous block)
    double pos = -1.0;
    int16_t carry[2] = {0, 0};
    double adj = 0.0;        // smoothed rate adjustment, -kMaxAdj..+kMaxAdj
    bool primed = false;     // device unpaused (enough audio queued)
    bool stretch = getenv("CEWM_NOSTRETCH") == nullptr;
    uint32_t dropped = 0, underruns = 0;
    // CEWM_AUDIOLOG
    bool log = getenv("CEWM_AUDIOLOG") != nullptr;
    std::chrono::steady_clock::time_point last_print;
    uint32_t blocks = 0, minq = ~0u;
    uint64_t sumq = 0;

    static constexpr double kTargetMs = 180.0, kPrimeMs = 120.0, kMaxAdj = 0.06;
    static constexpr uint32_t kMaxQueueMs = 400;

    double QueuedMs() const { return have.freq ? SDL_GetQueuedAudioSize(dev) * 1000.0 / (4.0 * have.freq) : 0.0; }
    void Reset() {
        pos = -1.0;
        carry[0] = carry[1] = 0;
        adj = 0.0;
        primed = false;
        if (dev) { SDL_ClearQueuedAudio(dev); SDL_PauseAudioDevice(dev, 1); }
    }
};

PacedWaveOut::PacedWaveOut() : p_(new Impl) {}
PacedWaveOut::~PacedWaveOut() { Stop(); }

void PacedWaveOut::Start(const char*, uint32_t, uint16_t, uint16_t, bool) {
    std::lock_guard<std::mutex> lk(p_->mu);
    if (const char* f = getenv("CEWM_WAV")) p_->dump = fopen(f, "wb");
    if (const char* a = getenv("CEWM_AUDIO")) { if (a[0] == '0') return; }  // audio off: same state as "no device"
    if (!SDL_WasInit(SDL_INIT_AUDIO) && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "[AUDIO] SDL audio unavailable: %s\n", SDL_GetError());
        return;
    }
    SDL_AudioSpec want{};
    want.freq = 48000;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 512;
    p_->dev = SDL_OpenAudioDevice(nullptr, 0, &want, &p_->have, 0);
    if (!p_->dev) fprintf(stderr, "[AUDIO] cannot open an output device: %s\n", SDL_GetError());
}

void PacedWaveOut::Stop() {
    std::lock_guard<std::mutex> lk(p_->mu);
    if (p_->dev) { SDL_CloseAudioDevice(p_->dev); p_->dev = 0; }
    if (p_->dump) { fclose(p_->dump); p_->dump = nullptr; }
}

void PacedWaveOut::SetFormat(uint32_t rate_hz, uint16_t, uint16_t) {
    std::lock_guard<std::mutex> lk(p_->mu);
    if (rate_hz && rate_hz != p_->rate) { p_->rate = rate_hz; p_->pos = -1.0; }
}

// The device stays paused until QueueOutput has built a cushion.
void PacedWaveOut::BeginAudioOut(std::function<void()>) {}

bool PacedWaveOut::QueueOutput(const void* bytes, uint32_t length) {
    std::lock_guard<std::mutex> lk(p_->mu);
    Impl& s = *p_;
    if (s.dump) fwrite(bytes, 1, length, s.dump);
    if (!s.dev || !s.rate || length < 4) return false;
    double q = s.QueuedMs();
    if (s.primed && q <= 0.0) {  // the device ran dry: pause, and start again once a cushion has built up
        ++s.underruns;
        s.primed = false;
        SDL_PauseAudioDevice(s.dev, 1);
    }
    if (q > Impl::kMaxQueueMs) { ++s.dropped; return false; }  // guest far ahead: drop to keep latency bounded
    if (s.log) {
        ++s.blocks; s.sumq += (uint64_t)q; s.minq = std::min(s.minq, (uint32_t)q);
        const auto now = std::chrono::steady_clock::now();
        if (now - s.last_print > std::chrono::seconds(1)) {
            fprintf(stderr, "[AUDIO] rate=%u blocks/s=%u queue ms min=%u avg=%u adj=%+.1f%% underruns=%u dropped=%u\n", s.rate, s.blocks, s.minq,
                    s.blocks ? (uint32_t)(s.sumq / s.blocks) : 0u, s.adj * 100.0, s.underruns, s.dropped);
            s.last_print = now; s.blocks = 0; s.minq = ~0u; s.sumq = 0;
        }
    }
    if (s.stretch) {  // steer the queue to the target depth: shallow -> slow down, deep -> speed up (smoothed per block)
        const double want = std::max(-Impl::kMaxAdj, std::min(Impl::kMaxAdj, (q - Impl::kTargetMs) / Impl::kTargetMs * Impl::kMaxAdj));
        s.adj += (want - s.adj) * 0.1;
    }
    const int16_t* in = static_cast<const int16_t*>(bytes);
    const int n = (int)(length / 4);  // stereo frames
    const double step = (double)s.rate / s.have.freq * (1.0 + s.adj);  // input frames consumed per output frame
    std::vector<int16_t> out;
    out.reserve((size_t)((n + 2) / step * 2) + 8);
    double p = s.pos;
    for (; p < n - 1; p += step) {
        const int i = (int)p;  // -1 .. n-2
        const double f = p - i;
        for (int c = 0; c < 2; ++c) {
            const double a = i < 0 ? s.carry[c] : in[i * 2 + c];
            const double b = in[(i + 1) * 2 + c];
            out.push_back((int16_t)(a + (b - a) * f));
        }
    }
    s.pos = p - n;
    s.carry[0] = in[(n - 1) * 2];
    s.carry[1] = in[(n - 1) * 2 + 1];
    if (!out.empty()) SDL_QueueAudio(s.dev, out.data(), (uint32_t)(out.size() * sizeof(int16_t)));
    if (!s.primed && s.QueuedMs() >= Impl::kPrimeMs) { s.primed = true; SDL_PauseAudioDevice(s.dev, 0); }
    return true;
}

void PacedWaveOut::FinishAudioOut() {}

void PacedWaveOut::StopAudioOut() {
    std::lock_guard<std::mutex> lk(p_->mu);
    p_->Reset();
}
