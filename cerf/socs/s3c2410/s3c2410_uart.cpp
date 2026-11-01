#include "../../peripherals/peripheral_base.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../boards/board_context.h"
#include "s3c2410_id.h"
#include "../../tracing/kernel_debug_sink.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"
#include "host_state.h"
#include "ril_modem.h"
#include "ril_android.h"
#include "../fake_at_modem.h"
#include "../../execution/guest_cycle_clock.h"
#include "../irq_controller.h"
#include "s3c2410_sub_source_levels.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <vector>
#include <string>

namespace {

using namespace fakeat;

constexpr int      kUartCount   = 3;
constexpr uint32_t kUartStride  = 0x4000u;
constexpr uint32_t kRegBlockEnd = 0x2Cu;       /* one past UBRDIV */
constexpr size_t   kRegsPerUart = 11;

constexpr uint32_t kSlotULCON   = 0;
constexpr uint32_t kSlotUCON    = 1;
constexpr uint32_t kSlotUFCON   = 2;
constexpr uint32_t kSlotUMCON   = 3;
constexpr uint32_t kSlotUTRSTAT = 4;
constexpr uint32_t kSlotUERSTAT = 5;
constexpr uint32_t kSlotUFSTAT  = 6;
constexpr uint32_t kSlotUMSTAT  = 7;
constexpr uint32_t kSlotUTXH    = 8;
constexpr uint32_t kSlotURXH    = 9;
constexpr uint32_t kSlotUBRDIV  = 10;

/* UTRSTAT: bit 0 = RX data ready (always 0; no input), bit 1 = TX
   buffer empty (always 1), bit 2 = transmitter empty (always 1). */
constexpr uint32_t kUtrstatTxIdle = 0x6u;

/* UART0/UART2 sub-source bits: RXD = 0/6, TXD = 1/7; main sources INT_UART0 = 28, INT_UART2 = 15. UART1 stays the debug console. */
constexpr int kMainUart[kUartCount] = {28, 23, 15};
constexpr int kSubRxd[kUartCount]   = {0, 3, 6};

class S3C2410Uart : public Peripheral, public S3C2410LevelSubSource {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::S3c2410;
    }
    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
        for (auto& ril : ril_) {
            ril.dial = [this](const std::string& number) { const bool ok = cewm::AndroidRil("dial:" + number) == "ok"; if (ok) last_call_.clear(); return ok; };
            ril.hangup = [] { return cewm::AndroidRil("hangup") == "ok"; };
            ril.answer = [] { return cewm::AndroidRil("answer") == "ok"; };
            ril.muted = [] { const auto value = cewm::AndroidRil("mute"); return value == "0" ? 0 : value == "1" ? 1 : -1; };
            ril.control = [](const std::string& cmd) { return cewm::AndroidRil(cmd) == "ok"; };
        }
        for (int u = 0; u < kUartCount; ++u) if (u != 1) emu_.Get<S3C2410SubSourceLevels>().Register(kMainUart[u], kSubRxd[u], this);
        if (modem_) {
            auto& clk = emu_.Get<GuestCycleClock>();
            radio_ev_ = clk.Add([this] { RadioPoll(); });
            clk.Arm(radio_ev_, clk.Cycles() + clk.NsToCycles(1000000000));
        }
    }
    bool SubSourceAsserted(int sub) override {
        for (int u = 0; u < kUartCount; ++u) if (u != 1 && sub == kSubRxd[u]) return !rx_[u].empty() && (storage_[u][kSlotUCON] & 3u) == 1u;
        return false;
    }

    uint32_t MmioBase() const override { return 0x50000000u; }
    uint32_t MmioSize() const override { return 0x0000C000u; }

    uint32_t ReadWord (uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    // Save link subscriptions, but reconcile live calls from Android rather than replaying guest actions.
    void SaveState(StateWriter& w) override {
        w.WriteBytes("storage", storage_, sizeof(storage_));
        w.Write("at_link", at_link_); w.Write("next_link", caif_next_link_);
        w.Write("link_uart", link_uart_); w.Write("numeric", at_v0_); w.Write("no_echo", at_e0_);
        unsigned sessions[256];
        for (int i = 0; i < 256; ++i) sessions[i] = ril_[i].Session();
        w.Write("ril_sessions", sessions);
    }
    void RestoreState(StateReader& r) override {
        r.ReadBytes("storage", storage_, sizeof(storage_));
        for (auto& ril : ril_) ril.Reset();
        last_call_.clear();
        for (auto& rx : rx_) rx.clear();
        for (auto& frame : caif_buf_) frame.clear();
        if (r.Remaining()) {
            r.Read("at_link", at_link_); r.Read("next_link", caif_next_link_);
            r.Read("link_uart", link_uart_); r.Read("numeric", at_v0_); r.Read("no_echo", at_e0_);
            unsigned sessions[256]; r.Read("ril_sessions", sessions);
            for (int i = 0; i < 256; ++i) {
                if (link_uart_[i] != 0 && link_uart_[i] != 2) r.Reject("Invalid modem UART");
                ril_[i].RestoreSession(sessions[i], i == at_link_);
            }
        }
    }

private:
    void     EmitTxByte(int uart_idx, uint8_t ch);
    void     Modem(int u, uint8_t ch);
    void     CaifPayload(int u, const uint8_t* p, size_t n);
    void     CaifSend(int u, const std::vector<uint8_t>& payload);

    uint32_t    storage_ [kUartCount][kRegsPerUart] = {};
    std::string tx_line_ [kUartCount];
    std::deque<uint8_t> rx_[kUartCount];  /* host -> guest bytes (fake modem replies) */
    cewm::RilModem     ril_[256];
    std::string last_call_;
    int link_uart_[256] = {};
    std::string         caif_buf_[kUartCount];
    uint8_t             caif_next_link_ = 0;
    bool                at_v0_[256] = {}, at_e0_[256] = {};
    uint8_t             at_link_ = 0;               /* the AT command link the guest opened (0 = none yet) */
    int                 last_bars_ = -2, last_stat_ = -2, last_act_ = -2;  /* radio state last reported to the guest */
    GuestCycleClock::Event* radio_ev_ = nullptr;
    void                RadioPoll();
    bool                modem_ = getenv("CEWM_FAKEMODEM") != nullptr;  /* experiment: fake GSM modem on UART0/2 (off by default) */
    bool                trace_ = getenv("CEWM_UARTLOG") != nullptr;
};

uint32_t S3C2410Uart::ReadWord(uint32_t addr) {
    const uint32_t off       = addr - MmioBase();
    const uint32_t uart_idx  = off / kUartStride;
    const uint32_t reg_off   = off - uart_idx * kUartStride;
    if (uart_idx >= kUartCount || reg_off >= kRegBlockEnd) {
        HaltUnsupportedAccess("ReadWord", addr, 0);
    }
    const uint32_t slot = reg_off / 4u;

    uint32_t value;
    if (trace_ && uart_idx != 1) { static int n[3]; if (n[uart_idx]++ < 150) fprintf(stderr, "[UARTR] u%u s%u\n", uart_idx, slot); }
    if (slot == kSlotUTRSTAT) {
        value = kUtrstatTxIdle | (rx_[uart_idx].empty() ? 0u : 1u);
    } else if (slot == kSlotUFSTAT) {
        value = (uint32_t)(rx_[uart_idx].size() > 15 ? 15 : rx_[uart_idx].size());
    } else if (slot == kSlotUMSTAT) {
        value = (uart_idx == 1 || !modem_) ? 0u : 0x11u;  /* CTS asserted: a modem is attached */
    } else if (slot == kSlotURXH) {
        value = 0;
        if (!rx_[uart_idx].empty()) { value = rx_[uart_idx].front(); rx_[uart_idx].pop_front(); }
    } else {
        value = storage_[uart_idx][slot];
    }
    return value;
}

void S3C2410Uart::WriteWord(uint32_t addr, uint32_t value) {
    const uint32_t off       = addr - MmioBase();
    const uint32_t uart_idx  = off / kUartStride;
    const uint32_t reg_off   = off - uart_idx * kUartStride;
    if (uart_idx >= kUartCount || reg_off >= kRegBlockEnd) {
        HaltUnsupportedAccess("WriteWord", addr, value);
    }
    const uint32_t slot = reg_off / 4u;
    if (trace_ && uart_idx != 1 && slot != kSlotUTXH) { static int n[3]; if (n[uart_idx]++ < 150) fprintf(stderr, "[UARTW] u%u s%u=%X\n", uart_idx, slot, value); }

    if (slot == kSlotUTXH) {
        if (uart_idx != 1 && modem_) Modem((int)uart_idx, (uint8_t)(value & 0xFFu));
            EmitTxByte((int)uart_idx, (uint8_t)(value & 0xFFu));
        return;
    }
    storage_[uart_idx][slot] = value;
}

/* Fake ST-Ericsson modem speaking CAIF over the serial line (caifmux.dll in the ROM is the host side, rilgsm talks AT over its VEI links).
   Serial frame: STX(02) | len LE16 (= payload + 2) | payload | FCS LE16 (Linux crc_ccitt, init 0xFFFF).
   Payload: link id | data. Link 0 = control: ENUM (03) -> rsp 23; LINK_SETUP (00) -> rsp 20 + echoed fields + new link id;
   LINK_DESTROY (01) -> rsp 21. VEI data link: 00 (payload) | AT text; replies use the same framing. */
static uint16_t CaifCrc(const uint8_t* p, size_t n) {
    uint16_t c = 0xFFFF;
    for (size_t i = 0; i < n; i++) { c ^= p[i]; for (int b = 0; b < 8; b++) c = (c & 1) ? (uint16_t)((c >> 1) ^ 0x8408) : (uint16_t)(c >> 1); }
    return c;
}

/* Once a second of guest time: if signal or registration changed since the last report, push unsolicited result codes so the RIL
   refreshes without polling (also forces one refresh after a resume, since last_* start unknown). */
void S3C2410Uart::RadioPoll() {
    auto& clk = emu_.Get<GuestCycleClock>();
    clk.Arm(radio_ev_, clk.Cycles() + clk.NsToCycles(1000000000));
    if (!at_link_) return;
    const std::string call = cewm::AndroidRil("poll");
    if (!call.empty() && call != last_call_) {
        last_call_ = call;
        if (call.size() >= 4 && call[1] == ':' && call[3] == ':' && (call[0] == '0' || call[0] == '1' || call[0] == '2' || call[0] == '3' || call[0] == '4' || call[0] == '6') && (call[2] == '0' || call[2] == '1')) {
            for (int link = 1; link < 256; ++link) {
                const std::string report = ril_[link].CallEvent((cewm::RilModem::CallState)(call[0] - '0'), call.substr(4), call[2] - '0');
                if (report.empty()) continue;
                std::vector<uint8_t> frame = {(uint8_t)link, 0};
                frame.insert(frame.end(), report.begin(), report.end());
                CaifSend(link_uart_[link], frame);
            }
        }
    }
    const int sig = RadioSignal();
    const int bars = RadioBars(sig), stat = sig < 0 ? 0 : 1;
    const int act = RadioAct();
    if (bars == last_bars_ && stat == last_stat_ && act == last_act_) return;
    std::string t;
    const std::string loc = stat ? ",\"0001\",\"0001\"" : "";  /* +CREG/+CGREG keep the plain form (the RIL drops service when they carry AcT); only *EREG carries the network type */
    if (stat != last_stat_ || act != last_act_) t += "\r\n+CREG: " + std::to_string(stat) + loc + "\r\n\r\n*EREG: " + std::to_string(stat) + loc + (stat ? ActSuffix() : "") + "\r\n\r\n+CGREG: " + std::to_string(stat) + loc + "\r\n";
    if (bars != last_bars_) t += "\r\n+CIEV: 2," + std::to_string(bars) + "\r\n";
    last_bars_ = bars; last_stat_ = stat; last_act_ = act;
    std::vector<uint8_t> r = {at_link_, 0x00};
    r.insert(r.end(), t.begin(), t.end());
    CaifSend(0, r);
}

void S3C2410Uart::CaifSend(int u, const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> f;
    const uint16_t crc = CaifCrc(payload.data(), payload.size()), len = (uint16_t)(payload.size() + 2);
    f.push_back(0x02); f.push_back((uint8_t)len); f.push_back((uint8_t)(len >> 8));
    f.insert(f.end(), payload.begin(), payload.end());
    f.push_back((uint8_t)crc); f.push_back((uint8_t)(crc >> 8));
    if (trace_) { fprintf(stderr, "[CAIF%d] tx", u); for (uint8_t b : f) fprintf(stderr, " %02X", b); fprintf(stderr, "\n"); }
    for (uint8_t b : f) rx_[u].push_back(b);
    if ((storage_[u][kSlotUCON] & 3u) == 1u) emu_.Get<IrqController>().AssertSubIrq(kMainUart[u], kSubRxd[u]);
}

void S3C2410Uart::CaifPayload(int u, const uint8_t* p, size_t n) {
    if (trace_) { fprintf(stderr, "[CAIF%d] rx", u); for (size_t i = 0; i < n; i++) fprintf(stderr, " %02X", p[i]); fprintf(stderr, "\n"); }
    if (n < 2) return;
    if (p[0] == 0) {  /* control channel */
        const uint8_t cmd = p[1] & 0x0F;
        std::vector<uint8_t> r = {0x00, (uint8_t)(p[1] | 0x20)};
        if (cmd == 3) {                       /* ENUM */
            CaifSend(u, r);
        } else if (cmd == 0 && n >= 5) {      /* LINK_SETUP: echo type, prio/phy, endpoint, then the link id */
            r.insert(r.end(), p + 2, p + n);  /* VEI/DBG carry no extra parameters; others echo theirs */
            r.push_back(++caif_next_link_);
            last_call_.clear();
            CaifSend(u, r);
        } else if (cmd == 1 && n >= 3) {      /* LINK_DESTROY */
            r.push_back(p[2]);
            ril_[p[2]].Reset();
            last_call_.clear();
            at_v0_[p[2]] = at_e0_[p[2]] = false;
            CaifSend(u, r);
        }
        return;
    }
    if (p[1] != 0x00) return;  /* VEI control (flow, pins): ignore */
    at_link_ = p[0];
    link_uart_[p[0]] = u;
    for (size_t i = 2; i < n; i++) {
        const std::string a = ril_[p[0]].Feed(p[i], at_v0_[p[0]], at_e0_[p[0]], AtAnswer);
        if (a.empty()) continue;
        std::vector<uint8_t> r = {p[0], 0x00};
        r.insert(r.end(), a.begin(), a.end());
        CaifSend(u, r);
    }
}

/* Called for every byte the guest writes to a non-debug UART when the fake modem is on. */
void S3C2410Uart::Modem(int u, uint8_t ch) {
    std::string& b = caif_buf_[u];
    b.push_back((char)ch);
    for (;;) {
        while (!b.empty() && (uint8_t)b[0] != 0x02) b.erase(0, 1);
        if (b.size() < 3) return;
        const size_t len = (uint8_t)b[1] | ((uint8_t)b[2] << 8);
        if (len < 2 || len > 2048) { b.erase(0, 1); continue; }
        if (b.size() < 3 + len) return;
        const uint8_t* pl = (const uint8_t*)b.data() + 3;
        const size_t pn = len - 2;
        const uint16_t fcs = (uint16_t)(pl[pn] | (pl[pn + 1] << 8));
        if (fcs != CaifCrc(pl, pn)) { if (trace_) fprintf(stderr, "[CAIF%d] bad fcs\n", u); b.erase(0, 1); continue; }
        std::vector<uint8_t> copy(pl, pl + pn);
        b.erase(0, 3 + len);
        CaifPayload(u, copy.data(), copy.size());
    }
}

void S3C2410Uart::EmitTxByte(int uart_idx, uint8_t ch) {
    char tag[8];
    std::snprintf(tag, sizeof(tag), "UART%d", uart_idx);
    /* UART1 is the debug console - only it mirrors to HwScreen. */
    emu_.Get<KernelDebugSink>().EmitChar(static_cast<char>(ch), tx_line_[uart_idx],
                                         tag, true);
}

}  /* namespace */

REGISTER_SERVICE(S3C2410Uart);
