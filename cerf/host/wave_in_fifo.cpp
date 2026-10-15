#define NOMINMAX

#include "wave_in_fifo.h"

#include "../core/log.h"

#include <algorithm>

namespace {
constexpr UINT kMsgOpen = WM_USER + 0x10u;
}

uint64_t WaveInFifo::FormatKey(uint32_t sample_rate_hz, uint16_t channels, uint16_t bits) {
    return static_cast<uint64_t>(sample_rate_hz) | (static_cast<uint64_t>(channels) << 32) |
           (static_cast<uint64_t>(bits) << 48);
}

void WaveInFifo::Start(const char* log_tag, size_t cap_bytes) {
    log_tag_   = log_tag;
    cap_bytes_ = cap_bytes;
    sink_.Start(nullptr, [this](const MSG& msg) { OnThreadMessage(msg); }, log_tag);
}

void WaveInFifo::Stop() { sink_.Stop(); }

void WaveInFifo::Request(uint32_t sample_rate_hz, uint16_t channels, uint16_t bits) {
    const uint64_t key = FormatKey(sample_rate_hz, channels, bits);
    if (requested_.exchange(key) != key) sink_.Post(kMsgOpen);
}

void WaveInFifo::Clear() {
    std::lock_guard<std::mutex> lk(mtx_);
    fifo_.clear();
}

size_t WaveInFifo::Take(uint8_t* dst, size_t bytes) {
    size_t taken = 0;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        taken = std::min(bytes, fifo_.size());
        std::copy(fifo_.begin(), fifo_.begin() + taken, dst);
        fifo_.erase(fifo_.begin(), fifo_.begin() + taken);
    }
    std::fill(dst + taken, dst + bytes, uint8_t{0});
    return taken;
}

void WaveInFifo::OnThreadMessage(const MSG& msg) {
    if (msg.message == kMsgOpen) {
        OpenOnThread();
    } else if (msg.message == MM_WIM_DATA) {
        auto* hdr = reinterpret_cast<LPWAVEHDR>(msg.lParam);
        if (!hdr) return;
        OnRecordedData(reinterpret_cast<const uint8_t*>(hdr->lpData), hdr->dwBytesRecorded);
        sink_.Requeue(hdr);
    }
}

void WaveInFifo::OpenOnThread() {
    const uint64_t key = requested_.load();
    if (key == open_) return;
    open_ = key;
    Clear();
    const uint32_t rate     = static_cast<uint32_t>(key);
    const uint16_t channels = static_cast<uint16_t>(key >> 32);
    const uint16_t bits     = static_cast<uint16_t>(key >> 48);
    if (!sink_.EnsureFormat(rate, channels, bits)) {
        LOG(Caution, "[%s] microphone capture unavailable at %u Hz (host waveIn open failed); "
                     "the guest records silence\n", log_tag_, rate);
    }
}

void WaveInFifo::OnRecordedData(const uint8_t* data, uint32_t bytes) {
    if (bytes == 0u) return;
    std::lock_guard<std::mutex> lk(mtx_);
    fifo_.insert(fifo_.end(), data, data + bytes);
    if (fifo_.size() > cap_bytes_) fifo_.erase(fifo_.begin(), fifo_.end() - cap_bytes_);
}
