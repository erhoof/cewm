#include "host_serial_forward.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../host/host_window.h"
#include "../../state/emulation_freeze.h"

#include <utility>

namespace {
constexpr DWORD kReadTimeoutMs  = 50;     /* reader wakes to poll status + stop */
constexpr DWORD kWriteTimeoutMs = 2000;   /* a wedged device can't hang shutdown */
}  /* namespace */

HostSerialForward::HostSerialForward(std::wstring host_port, CerfEmulator& emu)
    : emu_(emu), port_name_(std::move(host_port)) {}

HostSerialForward::~HostSerialForward() { OnClose(); }

/* The bridge carries no bytes once the host port is gone, so the owner is told to
   eject. The job runs on the UI thread: the eject joins the reader and writer, and
   a thread cannot join itself. */
void HostSerialForward::ReportDead(const std::wstring& detail) {
    if (dead_.exchange(true)) return;
    std::wstring text = L"The serial port forwarder lost host " + port_name_ +
                        L".\n\n" + detail +
                        L"\n\nThe port has been ejected. Check that the port exists "
                        L"and is not in use by another application, then insert it "
                        L"again.";
    std::function<void()> on_dead = on_dead_;
    emu_.Get<HostWindow>().RunOnUiThread([text, on_dead] {
        MessageBoxW(nullptr, text.c_str(),
                    L"Serial port forwarder - CE Runtime Foundation",
                    MB_OK | MB_ICONWARNING);
        if (on_dead) on_dead();
    });
}

bool HostSerialForward::WithGuestAccess(const std::function<void()>& touch) {
    auto& freeze = emu_.Get<EmulationFreeze>();
    while (running_.load()) {
        auto frozen = freeze.TryWorkerSection();
        if (frozen.owns_lock()) {
            touch();
            return true;
        }
        if (WaitForSingleObject(stop_event_, 1) == WAIT_OBJECT_0) return false;
    }
    return false;
}

void HostSerialForward::OnOpen() {
    /* "\\.\" prefix is required for COM10+ and harmless for COM1..9. */
    const std::wstring path = L"\\\\.\\" + port_name_;
    handle_ = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                          OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (handle_ == INVALID_HANDLE_VALUE) {
        const DWORD err = GetLastError();
        LOG(Caution, "[SerialFwd] cannot open host %ls (err %lu)\n",
            port_name_.c_str(), err);
        if (uart_) uart_->SetModemInputs(false, false, false, false);
        ReportDead(L"The port could not be opened (Windows error " +
                   std::to_wstring(err) + L").");
        return;
    }

    ApplyLineConfig(uart_ ? uart_->GetLineConfig() : SerialLine::LineConfig{});

    last_ms_.store(0xFFu);

    COMMTIMEOUTS to{};
    to.ReadIntervalTimeout        = MAXDWORD;   /* return on first byte, or after */
    to.ReadTotalTimeoutMultiplier = MAXDWORD;   /* ReadTotalTimeoutConstant ms if  */
    to.ReadTotalTimeoutConstant   = kReadTimeoutMs;          /* the buffer is empty */
    to.WriteTotalTimeoutConstant  = kWriteTimeoutMs;
    SetCommTimeouts(handle_, &to);

    read_ov_event_  = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    write_ov_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    stop_event_     = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    pace_timer_     = CreateWaitableTimerExW(nullptr, nullptr,
                          CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!pace_timer_) pace_timer_ = CreateWaitableTimerW(nullptr, FALSE, nullptr);

    if (uart_)
        uart_->SetLineConfigCallback(
            [this](const SerialLine::LineConfig& c) { ApplyLineConfig(c); });

    running_ = true;
    reader_  = std::thread([this] { ReaderLoop(); });
    writer_  = std::thread([this] { WriterLoop(); });
    LOG(Periph, "[SerialFwd] bridging guest COM <-> host %ls\n", port_name_.c_str());
}

void HostSerialForward::OnClose() {
    if (uart_) uart_->SetLineConfigCallback(nullptr);
    StopThreads();
    if (uart_) uart_->SetModemInputs(false, false, false, false);
    if (handle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
    }
    if (read_ov_event_)  { CloseHandle(read_ov_event_);  read_ov_event_  = nullptr; }
    if (write_ov_event_) { CloseHandle(write_ov_event_); write_ov_event_ = nullptr; }
    if (stop_event_)     { CloseHandle(stop_event_);     stop_event_     = nullptr; }
    if (pace_timer_)     { CloseHandle(pace_timer_);     pace_timer_     = nullptr; }
}

void HostSerialForward::StopThreads() {
    running_.store(false);
    if (stop_event_) SetEvent(stop_event_);
    tx_cv_.notify_all();
    if (writer_.joinable()) writer_.join();
    if (reader_.joinable()) reader_.join();
}

void HostSerialForward::ApplyLineConfig(const SerialLine::LineConfig& c) {
    std::lock_guard<std::mutex> lk(ctl_mu_);
    if (handle_ == INVALID_HANDLE_VALUE) return;
    DCB dcb{};
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(handle_, &dcb)) return;

    dcb.BaudRate = c.baud;
    dcb.ByteSize = c.data_bits;
    switch (c.parity) {
        case SerialLine::LineConfig::Parity::None:  dcb.Parity = NOPARITY;    break;
        case SerialLine::LineConfig::Parity::Odd:   dcb.Parity = ODDPARITY;   break;
        case SerialLine::LineConfig::Parity::Even:  dcb.Parity = EVENPARITY;  break;
        case SerialLine::LineConfig::Parity::Mark:  dcb.Parity = MARKPARITY;  break;
        case SerialLine::LineConfig::Parity::Space: dcb.Parity = SPACEPARITY; break;
    }
    switch (c.stop) {
        case SerialLine::LineConfig::Stop::One:          dcb.StopBits = ONESTOPBIT;   break;
        case SerialLine::LineConfig::Stop::OnePointFive: dcb.StopBits = ONE5STOPBITS; break;
        case SerialLine::LineConfig::Stop::Two:          dcb.StopBits = TWOSTOPBITS;  break;
    }
    dcb.fBinary = TRUE;
    dcb.fParity = (c.parity != SerialLine::LineConfig::Parity::None);
    /* SetCommState applies fDtrControl/fRtsControl, and *_CONTROL_DISABLE lowers that
       line and leaves it lowered (Win32 DCB), so these fields carry the guest's live
       DTR/RTS state: every line-config re-apply also re-drives the modem outputs. */
    dcb.fOutxCtsFlow    = FALSE;
    dcb.fOutxDsrFlow    = FALSE;
    dcb.fDsrSensitivity = FALSE;
    dcb.fDtrControl     = dtr_ ? DTR_CONTROL_ENABLE : DTR_CONTROL_DISABLE;
    dcb.fRtsControl     = rts_ ? RTS_CONTROL_ENABLE : RTS_CONTROL_DISABLE;
    dcb.fInX            = FALSE;
    dcb.fOutX           = FALSE;
    dcb.fNull           = FALSE;
    dcb.fAbortOnError   = FALSE;
    /* The DCB carries the guest's DTR/RTS as well as its baud and framing, so a
       failed SetCommState leaves the peer with no carrier and every byte misframed. */
    if (!SetCommState(handle_, &dcb)) {
        LOG(Caution, "[SerialFwd] %ls SetCommState failed (err %lu): the port did not "
            "take baud=%u bits=%u parity=%u stop=%u DTR=%d RTS=%d\n",
            port_name_.c_str(), GetLastError(), c.baud, c.data_bits,
            (unsigned)c.parity, (unsigned)c.stop, (int)dtr_, (int)rts_);
        return;
    }
#if CERF_DEV_MODE
    LOG(Periph, "[SerialFwd] line cfg %ls baud=%u bits=%u parity=%u stop=%u "
        "DTR=%d RTS=%d\n", port_name_.c_str(), c.baud, c.data_bits,
        (unsigned)c.parity, (unsigned)c.stop, (int)dtr_, (int)rts_);
#endif
}

void HostSerialForward::OnGuestTx(const uint8_t* data, size_t n) {
    if (handle_ == INVALID_HANDLE_VALUE || n == 0) return;
#if CERF_DEV_MODE
    LOG(Periph, "[SerialFwd] TX guest->host %ls n=%zu b0=0x%02X\n",
        port_name_.c_str(), n, data[0]);
#endif
    {
        std::lock_guard<std::mutex> lk(tx_mu_);
        tx_buf_.insert(tx_buf_.end(), data, data + n);
    }
    tx_cv_.notify_one();
}

void HostSerialForward::ResendModemInputs() { last_ms_.store(0xFFu); }

void HostSerialForward::OnControlLines(bool dtr, bool rts) {
    std::lock_guard<std::mutex> lk(ctl_mu_);
    dtr_ = dtr;
    rts_ = rts;
    if (handle_ == INVALID_HANDLE_VALUE) return;
#if CERF_DEV_MODE
    LOG(Periph, "[SerialFwd] drive host %ls DTR=%d RTS=%d\n",
        port_name_.c_str(), dtr, rts);
#endif
    EscapeCommFunction(handle_, dtr ? SETDTR : CLRDTR);
    EscapeCommFunction(handle_, rts ? SETRTS : CLRRTS);
}

void HostSerialForward::ReaderLoop() {
    OVERLAPPED ov{};
    ov.hEvent = read_ov_event_;
    uint8_t buf[512];
    while (running_.load()) {
        DWORD ms = 0;
        if (GetCommModemStatus(handle_, &ms)) {
            const uint8_t cur = (uint8_t)(ms & 0xF0u);   /* CTS/DSR/RI/DCD levels */
            if (cur != last_ms_.load()) {
#if CERF_DEV_MODE
                LOG(Periph, "[SerialFwd] host modem CTS=%d DSR=%d RI=%d DCD=%d\n",
                    (ms & MS_CTS_ON) != 0, (ms & MS_DSR_ON) != 0,
                    (ms & MS_RING_ON) != 0, (ms & MS_RLSD_ON) != 0);
#endif
                if (uart_) {
                    const bool cts = (ms & MS_CTS_ON)  != 0;
                    const bool dsr = (ms & MS_DSR_ON)  != 0;
                    const bool ri  = (ms & MS_RING_ON) != 0;
                    const bool dcd = (ms & MS_RLSD_ON) != 0;
                    if (!WithGuestAccess([&] {
                            uart_->SetModemInputs(cts, dsr, ri, dcd);
                        }))
                        break;
                }
                last_ms_.store(cur);
            }
        }

        DWORD got = 0;
        ResetEvent(read_ov_event_);
        if (!ReadFile(handle_, buf, sizeof buf, &got, &ov)) {
            const DWORD err = GetLastError();
            if (err != ERROR_IO_PENDING) {
                LOG(Caution, "[SerialFwd] %ls reader ReadFile err=%lu; host->guest "
                    "bridge is dead\n", port_name_.c_str(), err);
                ReportDead(L"Reading from the port failed (Windows error " +
                           std::to_wstring(err) + L").");
                break;
            }
            const HANDLE waits[2] = { read_ov_event_, stop_event_ };
            if (WaitForMultipleObjects(2, waits, FALSE, INFINITE) != WAIT_OBJECT_0) {
                CancelIoEx(handle_, &ov);
                break;
            }
            if (!GetOverlappedResult(handle_, &ov, &got, FALSE)) got = 0;
        }
        if (got > 0 && uart_) {
#if CERF_DEV_MODE
            LOG(Periph, "[SerialFwd] RX host->guest %ls n=%lu b0=0x%02X\n",
                port_name_.c_str(), got, buf[0]);
#endif
            if (!WithGuestAccess([&] { uart_->PushRx(buf, got); })) break;
        }
    }
}

void HostSerialForward::WriterLoop() {
    OVERLAPPED ov{};
    ov.hEvent = write_ov_event_;
    LARGE_INTEGER qpf{};
    QueryPerformanceFrequency(&qpf);
    LARGE_INTEGER next{};
    QueryPerformanceCounter(&next);
    std::vector<uint8_t> local;
    while (true) {
        {
            std::unique_lock<std::mutex> lk(tx_mu_);
            tx_cv_.wait(lk, [this] { return !tx_buf_.empty() || !running_.load(); });
            if (!running_.load() && tx_buf_.empty()) return;
            local.swap(tx_buf_);
        }

        /* Emit at the guest's programmed line rate. */
        const SerialLine::LineConfig cfg =
            uart_ ? uart_->GetLineConfig() : SerialLine::LineConfig{};
        const LONGLONG byte_ticks =
            cfg.baud ? (LONGLONG)(cfg.BitsPerChar() * (double)qpf.QuadPart / cfg.baud)
                     : 0;

        for (size_t i = 0; i < local.size() && running_.load(); ++i) {
            LARGE_INTEGER now{};
            QueryPerformanceCounter(&now);
            if (next.QuadPart < now.QuadPart) next.QuadPart = now.QuadPart;
            if (next.QuadPart > now.QuadPart && pace_timer_) {
                LARGE_INTEGER due{};
                due.QuadPart = -((next.QuadPart - now.QuadPart) * 10000000 / qpf.QuadPart);
                SetWaitableTimer(pace_timer_, &due, 0, nullptr, nullptr, FALSE);
                const HANDLE waits[2] = { pace_timer_, stop_event_ };
                if (WaitForMultipleObjects(2, waits, FALSE, INFINITE) != WAIT_OBJECT_0) {
#if CERF_DEV_MODE
                    LOG(Periph, "[SerialFwd] writer pace-wait aborted at byte %zu/%zu\n",
                        i, local.size());
#endif
                    break;
                }
            }

            DWORD wrote = 0;
            ResetEvent(write_ov_event_);
            if (!WriteFile(handle_, &local[i], 1, &wrote, &ov)) {
                const DWORD err = GetLastError();
                if (err != ERROR_IO_PENDING) {
                    LOG(Caution, "[SerialFwd] %ls writer WriteFile err=%lu at byte "
                        "%zu/%zu; guest->host bytes dropped\n", port_name_.c_str(),
                        err, i, local.size());
                    ReportDead(L"Writing to the port failed (Windows error " +
                               std::to_wstring(err) + L").");
                    break;
                }
                const HANDLE waits[2] = { write_ov_event_, stop_event_ };
                if (WaitForMultipleObjects(2, waits, FALSE, INFINITE) != WAIT_OBJECT_0) {
                    CancelIoEx(handle_, &ov);
#if CERF_DEV_MODE
                    LOG(Periph, "[SerialFwd] writer wait aborted at byte %zu/%zu; "
                        "thread exits\n", i, local.size());
#endif
                    return;
                }
                if (!GetOverlappedResult(handle_, &ov, &wrote, FALSE)) {
                    const DWORD ov_err = GetLastError();
                    LOG(Caution, "[SerialFwd] %ls writer GetOverlappedResult err=%lu "
                        "at byte %zu/%zu; guest->host bytes dropped\n",
                        port_name_.c_str(), ov_err, i, local.size());
                    ReportDead(L"Writing to the port failed (Windows error " +
                               std::to_wstring(ov_err) + L").");
                    break;
                }
            }
            if (wrote == 0) {
                LOG(Caution, "[SerialFwd] %ls writer timed out at byte %zu/%zu; "
                    "guest->host bytes dropped\n", port_name_.c_str(), i,
                    local.size());
                ReportDead(L"The port stopped accepting data (write timed out).");
                break;
            }
            next.QuadPart += byte_ticks;
        }
        local.clear();
    }
}
