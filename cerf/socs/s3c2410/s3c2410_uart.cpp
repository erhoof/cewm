#include "../../peripherals/peripheral_base.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../boards/board_context.h"
#include "s3c2410_id.h"
#include "../../tracing/kernel_debug_sink.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"
#include "host_state.h"
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

    /* Only the register file is machine state - tx_line_ is a host-side
       console line accumulator, rebuilt as the guest writes. */
    void SaveState(StateWriter& w) override    { w.WriteBytes("storage", storage_, sizeof(storage_)); }
    void RestoreState(StateReader& r) override { r.ReadBytes("storage", storage_, sizeof(storage_)); }

private:
    void     EmitTxByte(int uart_idx, uint8_t ch);
    void     Modem(int u, uint8_t ch);
    void     CaifPayload(int u, const uint8_t* p, size_t n);
    void     CaifSend(int u, const std::vector<uint8_t>& payload);

    uint32_t    storage_ [kUartCount][kRegsPerUart] = {};
    std::string tx_line_ [kUartCount];
    std::deque<uint8_t> rx_[kUartCount];  /* host -> guest bytes (fake modem replies) */
    std::string         at_line_[kUartCount];
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

/* Radio state from the phone (ofono via device_stats): signal 0..100, -1 = no network. CEWM_SIGNAL=<0..100> forces a value for laptop tests. */
static int RadioSignal() {
    int s = cewm::Host().cell_signal.load();
    if (s < 0) if (const char* e = getenv("CEWM_SIGNAL")) s = atoi(e);
    if (s < 0) if (const char* f = getenv("CEWM_SIGNAL_FILE")) {  /* test hook: change the signal while the guest runs */
        if (FILE* fp = fopen(f, "r")) { int v = -1; if (fscanf(fp, "%d", &v) == 1) s = v; fclose(fp); }
    }
    return s;
}
/* Signal indicator (+CIND/+CIEV 0..5) for a host strength 0..100. The ROM draws indicator 0-2 as no bars, 3 as one, 4 as three, 5 as five
   (measured on the laptop), so any real signal is mapped to at least 3. */
static int RadioBars(int sig) {
    if (sig < 0 || sig == 0) return 0;
    return sig <= 60 ? 3 : sig <= 85 ? 4 : 5;
}
/* Network type for the status bar (G / E / 3G / H) as the AcT field of +CREG/+CGREG; CEWM_ACT=<n> forces it for laptop tests. */
static int RadioAct() {
    int a = cewm::Host().cell_act.load();
    if (a < 0) if (const char* e = getenv("CEWM_ACT")) a = atoi(e);
    return a;
}
static std::string ActSuffix() { const int a = RadioAct(); return a < 0 ? "" : "," + std::to_string(a); }
static std::string RadioPlmn() {
    std::lock_guard<std::mutex> lk(cewm::Host().info_mu);
    if (cewm::Host().operator_plmn.empty()) { const char* e = getenv("CEWM_PLMN"); return e ? e : "310150"; }
    return cewm::Host().operator_plmn;
}
static std::string RadioName() {
    std::lock_guard<std::mutex> lk(cewm::Host().info_mu);
    return cewm::Host().operator_name.empty() ? "CE Mobile" : cewm::Host().operator_name;
}

/* One AT command (without the "AT" prefix) -> information text, or "" . Query/set forms follow 3GPP TS 27.007 as the RIL uses them. */
static std::string AtInfo(const std::string& c) {
    auto is = [&](const char* p) { return c.rfind(p, 0) == 0; };
    const int sig = RadioSignal();
    const int bars = RadioBars(sig);
    const int rssi = sig < 0 ? 99 : sig * 31 / 100;      /* 0..31, 99 = unknown (the ROM does not draw bars from this) */
    const char* stat = sig < 0 ? "0" : "1";              /* registered / not registered */
    if (is("+CSQ"))   return "\r\n+CSQ: " + std::to_string(rssi) + ",99\r\n";
    if (is("+CPIN?")) return "\r\n+CPIN: READY\r\n";
    if (is("+CFUN?")) return "\r\n+CFUN: 1\r\n";
    if (is("+CREG?")) return std::string("\r\n+CREG: 2,") + stat + (sig < 0 ? "\r\n" : ",\"0001\",\"0001\"\r\n");
    if (is("+CGREG?")) return std::string("\r\n+CGREG: 1,") + stat + (sig < 0 ? "\r\n" : ",\"0001\",\"0001\"\r\n");
    static int cops_fmt = 2;  /* +COPS=3,<fmt> selects numeric (2) or long alphanumeric (0) operator names */
    if (is("+COPS=3,")) { cops_fmt = c.size() > 8 ? c[8] - '0' : 2; return ""; }
    if (is("+COPS?")) {
        if (sig < 0) return "\r\n+COPS: 0\r\n";
        return cops_fmt == 0 ? "\r\n+COPS: 0,0,\"" + RadioName() + "\"\r\n" : "\r\n+COPS: 0,2,\"" + RadioPlmn() + "\"\r\n";
    }
    if (is("+CRSM=")) {  /* SIM file access: serve the CPHS operator name (EF 6F14 = 28436) from the host's registered operator, else "file not found" */
        int cmd = 0, file = 0;
        if (sscanf(c.c_str(), "+CRSM=%d,%d", &cmd, &file) == 2 && file == 28436) {
            if (cmd == 192) return "\r\n+CRSM: 144,0,\"000000146F14040011F0FF01020000\"\r\n";  /* GET RESPONSE: 20-byte transparent EF */
            if (cmd == 176) {                                                                       /* READ BINARY: name in GSM bytes, FF padded */
                std::string nm = RadioName(), hex;
                char b[4];
                for (size_t i = 0; i < 20; i++) { snprintf(b, sizeof b, "%02X", i < nm.size() ? (unsigned)(unsigned char)nm[i] : 0xFFu); hex += b; }
                return "\r\n+CRSM: 144,0,\"" + hex + "\"\r\n";
            }
        }
        return "\r\n+CRSM: 148,4\r\n";
    }
    if (is("+CIND?")) return "\r\n+CIND: 5," + std::to_string(bars) + "," + (sig < 0 ? "0" : "1") + ",0,0,0,0,0\r\n";
    if (is("+CNUM"))  return "\r\n+CNUM: \"\",\"+15550100\",145\r\n";
    if (is("+CPBS?")) return "\r\n+CPBS: \"SM\",0,250\r\n";
    if (is("+CSMS?")) return "\r\n+CSMS: 0,1,1,1\r\n";
    if (is("+CPMS?")) return "\r\n+CPMS: \"SM\",0,10,\"SM\",0,10,\"SM\",0,10\r\n";
    if (is("+CMUT?")) return "\r\n+CMUT: 0\r\n";
    if (is("+CLIP?")) return "\r\n+CLIP: 1,2\r\n";
    if (is("+CLIR?")) return "\r\n+CLIR: 0,2\r\n";
    if (is("+CGMI") || is("+GMI")) return "\r\nCERF\r\n";
    if (is("+CGMM") || is("+GMM")) return "\r\nFake GSM\r\n";
    if (is("+CGMR") || is("+GMR")) return "\r\n1.0\r\n";
    if (is("+CGSN") || is("+GSN")) return "\r\n356938035643809\r\n";
    if (is("+CIMI"))  return "\r\n310150123456789\r\n";
    return "";
}

/* A full command line ("AT..." possibly compound with ';') -> bytes for the guest. v0/e0 are the per-link ATV/ATE settings. */
static std::string AtAnswer(const std::string& line, bool& v0, bool& e0) {
    std::string l;
    for (char ch : line) l.push_back((char)toupper((unsigned char)ch));
    if (l.rfind("AT", 0) != 0) return "";
    std::string out = e0 ? "" : line + "\r";
    std::string rest = l.substr(2);
    if (rest.empty() || rest == "V0" || rest == "V1" || rest == "E0" || rest == "E1") {
        if (rest == "V0") v0 = true; else if (rest == "V1") v0 = false;
        else if (rest == "E0") { e0 = true; out.clear(); } else if (rest == "E1") e0 = false;
    } else {
        for (size_t pos = 0; pos <= rest.size();) {
            size_t end = rest.find(';', pos);
            if (end == std::string::npos) end = rest.size();
            out += AtInfo(rest.substr(pos, end - pos));
            pos = end + 1;
        }
    }
    return out + (v0 ? "0\r" : "\r\nOK\r\n");
}

/* Once a second of guest time: if signal or registration changed since the last report, push unsolicited result codes so the RIL
   refreshes without polling (also forces one refresh after a resume, since last_* start unknown). */
void S3C2410Uart::RadioPoll() {
    auto& clk = emu_.Get<GuestCycleClock>();
    clk.Arm(radio_ev_, clk.Cycles() + clk.NsToCycles(1000000000));
    if (!at_link_) return;
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
            CaifSend(u, r);
        } else if (cmd == 1 && n >= 3) {      /* LINK_DESTROY */
            r.push_back(p[2]);
            CaifSend(u, r);
        }
        return;
    }
    if (p[1] != 0x00) return;  /* VEI control (flow, pins): ignore */
    at_link_ = p[0];
    std::string& line = at_line_[u];
    for (size_t i = 2; i < n; i++) {
        if (p[i] != '\r') { if (line.size() < 256) line.push_back((char)p[i]); continue; }
        if (trace_) fprintf(stderr, "[AT%d] link%u %s\n", u, p[0], line.c_str());
        const std::string a = AtAnswer(line, at_v0_[p[0]], at_e0_[p[0]]);
        line.clear();
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
