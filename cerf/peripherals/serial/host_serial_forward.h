#pragma once

#include "serial_endpoint.h"
#include "serial_line.h"

#define NOMINMAX
#include <windows.h>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class CerfEmulator;

/* SerialEndpoint that bridges the guest's UART to a real host COM port: raw
   bytes and control/status lines pass through both ways; the host port's baud +
   framing track the guest's SerialLine::LineConfig. No AT / no modem logic. */
class HostSerialForward : public SerialEndpoint {
public:
    HostSerialForward(std::wstring host_port, CerfEmulator& emu);
    ~HostSerialForward() override;

    /* Fired once, on the UI thread, when the host port dies (cannot open, or a read
       or write stops carrying bytes); the owner ejects. While an endpoint stays
       attached, guest TX routes to it and the widget shows a link, so a dead bridge
       left attached swallows guest bytes and reports a connection that is gone. */
    void SetOnBridgeDead(std::function<void()> cb) { on_dead_ = std::move(cb); }

    void OnGuestTx(const uint8_t* data, size_t n) override;
    void OnControlLines(bool dtr, bool rts) override;
    void OnOpen()  override;
    void OnClose() override;
    void ResendModemInputs() override;

private:
    void ReaderLoop();
    void WriterLoop();
    void ApplyLineConfig(const SerialLine::LineConfig& c);
    void StopThreads();
    void ReportDead(const std::wstring& detail);

    /* SerialCradle::PostRestore and PcmciaSlot::RestoreSlotState tear this endpoint
       down - joining the reader - from the hibernation thread while it holds the
       freeze write-lock, so the reader must never block acquiring the read side.
       Returns false when the endpoint is stopping and the guest touch is abandoned. */
    bool WithGuestAccess(const std::function<void()>& touch);

    CerfEmulator&     emu_;
    std::wstring      port_name_;        /* e.g. "COM3" */
    HANDLE            handle_          = INVALID_HANDLE_VALUE;
    HANDLE            read_ov_event_   = nullptr;
    HANDLE            write_ov_event_  = nullptr;
    HANDLE            stop_event_      = nullptr;
    HANDLE            pace_timer_      = nullptr;
    std::atomic<bool> running_{false};
    std::atomic<bool> dead_{false};      /* ReportDead fires at most once */

    std::function<void()> on_dead_;

    std::thread reader_;
    std::thread writer_;

    std::mutex              tx_mu_;
    std::condition_variable tx_cv_;
    std::vector<uint8_t>    tx_buf_;     /* guest TX awaiting WriteFile */

    /* SetCommState applies fDtrControl/fRtsControl, so a DCB re-apply re-drives the
       modem outputs: it must be serialized against the EscapeCommFunction that drives
       them, or a stale SetCommState lands after the drive and re-lowers the line. */
    std::mutex ctl_mu_;
    bool       dtr_ = false;
    bool       rts_ = false;

    std::atomic<uint8_t> last_ms_{0xFFu};   /* 0xFF = nothing pushed (reader masks 0xF0) */
};
