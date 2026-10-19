#pragma once

#include "core/cerf_emulator.h"
#include "execution/guest_cycle_clock.h"
#include "peripherals/serial/serial_endpoint.h"
#include "peripherals/serial/serial_line.h"
#include "socs/fake_at_modem.h"
#include "p525_hw_signals.h"
#include "socs/pxa27x/pxa27x_gpio.h"
#include "state/state_stream.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>

/* Plain-AT fake GSM modem on the P525 modem UART (FFUART): answers the RIL from the host radio state
   (fakeat::AtAnswer) and reports registration/signal changes as unsolicited codes. CEWM_MODEM_TRACE=1 logs the AT lines. */
class P525Modem : public SerialEndpoint {
public:
    void Attach(CerfEmulator& emu, SerialLine& line) {
        BindUart(line);
        gpio_ = emu.TryGet<Pxa27xGpio>();
        if (gpio_) gpio_->SetInputLevel(38, true);  // FFRI# idle high; a falling edge is the modem's ring/ready indication
        SetLines(line);  // CTS/DSR/DCD: the RIL waits for a modem to be present
        auto& clk = emu.Get<GuestCycleClock>();
        ev_ = clk.Add([this, &clk] { Tick(); clk.Arm(ev_, clk.Cycles() + clk.NsToCycles(1000000000)); });
        clk.Arm(ev_, clk.Cycles() + clk.NsToCycles(1000000000));
    }
    void OnOpen() override { if (uart_) SetLines(*uart_); }
    // Stored after the FFUART registers, so older UART frames remain readable.
    void SaveLinkState(StateWriter& w) {
        w.Write("modem_v0", v0_); w.Write("modem_e0", e0_);
        w.Write("modem_signal", last_sig_); w.Write("modem_report", cfun_);
        w.Write("modem_idle", idle_); w.Write("modem_stk", stk_pending_);
        w.Write<uint32_t>("modem_line_size", line_.size());
        w.WriteBytes("modem_line", line_.data(), line_.size());
        w.Write<uint32_t>("modem_reply_size", pend_.size());
        w.WriteBytes("modem_reply", pend_.data(), pend_.size());
    }
    void RestoreLinkState(StateReader& r) {
        if (!r.Remaining()) {
            // Legacy P525 snapshots were made after the OEM RIL selected E0/V0.
            v0_ = e0_ = true;
            return;
        }
        r.Read("modem_v0", v0_); r.Read("modem_e0", e0_);
        r.Read("modem_signal", last_sig_); r.Read("modem_report", cfun_);
        r.Read("modem_idle", idle_); r.Read("modem_stk", stk_pending_);
        uint32_t n = 0;
        r.Read("modem_line_size", n);
        if (n > 512) r.Reject("P525 modem command too long");
        line_.resize(n); r.ReadBytes("modem_line", line_.data(), n);
        r.Read("modem_reply_size", n);
        if (n > 512) r.Reject("P525 modem pending reply too long");
        pend_.resize(n); r.ReadBytes("modem_reply", pend_.data(), n);
    }
    void OnGuestTx(const uint8_t* data, size_t n) override {
        idle_ = 0;
        for (size_t i = 0; i < n; i++) {
            const char ch = (char)data[i];
            if (ch != '\r') { if (line_.size() < 512 && ch != '\n') line_.push_back(ch); continue; }
            const std::string a = fakeat::AtAnswer(line_, v0_, e0_);
            if (getenv("CEWM_MODEM_TRACE")) fprintf(stderr, "[MODEM] <- %s\n", line_.c_str());
            // In this modem dialect CFUN=6 enables SIM toolkit; it is not a reset.
            // rilgsm's capability query waits two seconds for its SET UP MENU indication.
            if (line_ == "AT+CFUN=6") stk_pending_ = true;
            if (line_ == "AT+CFUN=7" || line_ == "AT+CFUN=8") stk_pending_ = false;
            if (getenv("CEWM_RIGPIO") && line_.find(getenv("CEWM_RIGPIO")) != std::string::npos) ri_in_ = 2;
            if (line_.find("+CREG=1") != std::string::npos) { last_sig_ = -2; cfun_ = true; }  // registration reporting was just enabled: report the state
            // Automatic operator selection starts registration, even if the
            // host signal has not changed since the previous indication.
            if (line_ == "AT+COPS=0" || line_ == "AT+CFUN=1") cfun_ = true;
            line_.clear();
            if (getenv("CEWM_MODEM_TRACE")) { fprintf(stderr, "[MODEM] -> "); for (unsigned char ch : a) fprintf(stderr, "%02X ", ch); fprintf(stderr, "\n"); }
            if (getenv("CEWM_SPLIT") && a.size() > 2 && a.compare(a.size() - 2, 2, "0\r") == 0 && uart_) {
                uart_->PushRx((const uint8_t*)a.data(), a.size() - 2);
                pend_ = "0\r";
            } else if (!a.empty() && uart_) uart_->PushRx((const uint8_t*)a.data(), a.size());
        }
    }
private:
    template <class L> static void SetLines(L& l) {  // TEMP: CEWM_LINES = CTS DSR RI DCD as 0/1
        const char* e = getenv("CEWM_LINES");
        if (e && strlen(e) == 4) l.SetModemInputs(e[0] == '1', e[1] == '1', e[2] == '1', e[3] == '1');
        else l.SetModemInputs(true, true, false, true);
    }
    // Once a second: when signal or registration changed since the last report, send the codes the RIL enabled (+CREG/+CGREG=1).
    void Tick() {
        if (!uart_) return;
        if (ri_in_ > 0 && --ri_in_ == 0 && gpio_) { gpio_->SetInputLevel(38, false); ri_hold_ = 1; }
        else if (ri_hold_ > 0 && --ri_hold_ == 0 && gpio_) gpio_->SetInputLevel(38, true);
        if (g_p525_gprs_reset_release.load() != seen_reset_ && getenv("CEWM_RIRESET")) {  // module boot after GPRS_RESET: pulse RI#
            seen_reset_ = g_p525_gprs_reset_release.load();
            uart_->SetModemInputs(true, true, true, true);
            ri_ticks_ = 2;
        } else if (ri_ticks_ > 0 && --ri_ticks_ == 0) SetLines(*uart_);
        if (!pend_.empty()) { uart_->PushRx((const uint8_t*)pend_.data(), pend_.size()); pend_.clear(); return; }
        if (stk_pending_) {
            // An empty SET UP MENU clears SIM menu entries. The P525 RIL also
            // uses this indication to enable SIM toolkit capability results.
            static constexpr char menu[] = "\r\n+STKPRO: 37,0,\"00\",0,0,\"00\"\r\n";
            uart_->PushRx(reinterpret_cast<const uint8_t*>(menu), sizeof(menu) - 1);
            stk_pending_ = false;
            return;
        }
        const int sig = fakeat::RadioSignal();
        const int stat = sig < 0 ? 0 : 1;
        if (getenv("CEWM_NOURC") || ++idle_ < (getenv("CEWM_URCDELAY") ? atoi(getenv("CEWM_URCDELAY")) : 4)) return;  // unsolicited codes only on a quiet line, never between a command and its reply
        if (sig == last_sig_ && !cfun_) return;
        last_sig_ = sig;
        std::string t;
        cfun_ = false;
        const std::string loc = stat ? ",\"0001\",\"0001\"" : "";
        t += "\r\n+CREG: " + std::to_string(stat) + loc + "\r\n\r\n+CGREG: " + std::to_string(stat) + loc + "\r\n";
        if (getenv("CEWM_MODEM_TRACE")) fprintf(stderr, "[MODEM] URC %s\n", t.c_str());
        uart_->PushRx((const uint8_t*)t.data(), t.size());
    }
    std::string line_, pend_;
    int seen_reset_ = 0, ri_ticks_ = 0, ri_in_ = 0, ri_hold_ = 0;
    Pxa27xGpio* gpio_ = nullptr;
    bool v0_ = false, e0_ = false;
    int last_sig_ = -2;
    bool cfun_ = false;
    int idle_ = 0;
    bool stk_pending_ = false;
    GuestCycleClock::Event* ev_ = nullptr;
};
