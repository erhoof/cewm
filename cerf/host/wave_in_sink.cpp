#include "wave_in_sink.h"

#include "../core/log.h"

WaveInSink::~WaveInSink() {
    Stop();
    CloseDevice();
    if (ready_event_) CloseHandle(ready_event_);
}

void WaveInSink::Start(ThreadCallback on_start, MessageHandler on_message,
                       const char* log_tag) {
    log_tag_     = log_tag;
    ready_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    thread_      = std::thread([this, on_start, on_message] {
        ThreadMain(on_start, on_message);
    });
    WaitForSingleObject(ready_event_, INFINITE);
}

void WaveInSink::Stop() {
    shutdown_.store(true, std::memory_order_release);
    if (thread_id_) PostThreadMessageW(thread_id_, WM_QUIT, 0, 0);
    if (thread_.joinable()) thread_.join();
}

void WaveInSink::Post(UINT message, WPARAM wparam, LPARAM lparam) const {
    if (thread_id_) PostThreadMessageW(thread_id_, message, wparam, lparam);
}

void WaveInSink::ThreadMain(ThreadCallback on_start, MessageHandler on_message) {
    thread_id_ = GetCurrentThreadId();

    /* Force the queue to exist before any PostThreadMessage races us. */
    MSG msg;
    PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
    SetEvent(ready_event_);

    if (on_start) on_start();

    while (!shutdown_.load(std::memory_order_acquire) &&
           GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (on_message) on_message(msg);
    }
}

void WaveInSink::CloseDevice() {
    if (!in_device_) return;
    waveInReset(in_device_);
    for (auto& b : bufs_) {
        if (b.hdr.dwFlags & WHDR_PREPARED)
            waveInUnprepareHeader(in_device_, &b.hdr, sizeof(WAVEHDR));
    }
    waveInClose(in_device_);
    in_device_ = nullptr;
}

bool WaveInSink::EnsureFormat(uint32_t sample_rate_hz, uint16_t channels,
                              uint16_t bits) {
    if (in_device_ && open_rate_ == sample_rate_hz &&
        open_channels_ == channels && open_bits_ == bits) {
        return true;
    }
    if (in_device_) CloseDevice();

    WAVEFORMATEX fmt{};
    fmt.wFormatTag      = WAVE_FORMAT_PCM;
    fmt.nChannels       = channels;
    fmt.nSamplesPerSec  = sample_rate_hz;
    fmt.wBitsPerSample  = bits;
    fmt.nBlockAlign     = static_cast<uint16_t>((bits / 8) * channels);
    fmt.nAvgBytesPerSec = sample_rate_hz * fmt.nBlockAlign;
    fmt.cbSize          = 0;

    MMRESULT r = waveInOpen(&in_device_, WAVE_MAPPER, &fmt,
                            thread_id_, 0, CALLBACK_THREAD);
    if (r != MMSYSERR_NOERROR) {
        LOG(Caution, "%s: waveInOpen(%u Hz x %u ch x %u bit) failed mmresult=%u "
                "- microphone capture unavailable\n",
                log_tag_, sample_rate_hz, channels, bits, r);
        in_device_ = nullptr;
        return false;
    }

    for (auto& b : bufs_) {
        b.bytes.assign(kRecordBufBytes, 0);
        b.hdr = WAVEHDR{};
        b.hdr.lpData         = reinterpret_cast<LPSTR>(b.bytes.data());
        b.hdr.dwBufferLength = kRecordBufBytes;
        if (waveInPrepareHeader(in_device_, &b.hdr, sizeof(WAVEHDR)) != MMSYSERR_NOERROR ||
            waveInAddBuffer(in_device_, &b.hdr, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
            LOG(Caution, "%s: record buffer prime failed\n", log_tag_);
            CloseDevice();
            return false;
        }
    }

    if (waveInStart(in_device_) != MMSYSERR_NOERROR) {
        LOG(Caution, "%s: waveInStart failed\n", log_tag_);
        CloseDevice();
        return false;
    }

    open_rate_     = sample_rate_hz;
    open_channels_ = channels;
    open_bits_     = bits;
    LOG(Periph, "[%s] waveIn opened %u Hz x %u ch x %u bit\n",
        log_tag_, sample_rate_hz, channels, bits);
    return true;
}

void WaveInSink::Requeue(WAVEHDR* hdr) {
    if (!in_device_ || !hdr) return;
    hdr->dwBytesRecorded = 0;
    hdr->dwFlags &= ~WHDR_DONE;
    waveInAddBuffer(in_device_, hdr, sizeof(WAVEHDR));
}
