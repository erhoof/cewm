// ASUS P525 OEMAddressTable recovered from galaxy.cap kernel (OS offset 0x34714).
#include "boards/board_context.h"
#include "boards/page_table_builder.h"
#include "core/cerf_emulator.h"
#include "core/log.h"
#include "cpu/emulated_memory.h"
#include "cpu/arm/arm_cp15_registers.h"
#include "host/touch_input.h"
#include "peripherals/wolfson_wm9713/wm9713_codec.h"
#include "socs/pxa27x/pxa27x_gpio.h"
#include "execution/guest_cycle_clock.h"
#include "peripherals/peripheral_dispatcher.h"
#include <atomic>
#include <mutex>
#include <vector>
#include <string>
#include <cstdio>
#include "../host/host_state.h"
#include <algorithm>
#include <atomic>
#include <deque>
#include <map>
#include <set>
#include "execution/engine_bridge.h"

namespace {
constexpr std::string_view kFamily = "asus_p525";
class AsusP525Context : public BoardContext {
public:
    using BoardContext::BoardContext;
    std::string_view GetBoardFamilyId() const override { return kFamily; }
};
struct Mapping { uint32_t va, pa, mb; };
constexpr Mapping kMap[] = {
    {0x80000000u, 0x00000000u, 64u},
    {0x84000000u, 0x04000000u, 64u},
    {0x88000000u, 0xA0000000u, 64u},
    {0x8C000000u, 0x0C000000u, 16u},  // disk bytes past 64 MiB (V4.010 firmware)
    {0x8F000000u, 0x40000000u, 32u},
    {0x91000000u, 0x2C000000u, 64u},
    {0x95000000u, 0x28000000u, 64u},
    {0x99000000u, 0x20000000u, 32u},
    {0x9B000000u, 0x4C000000u, 1u},
    {0x9B100000u, 0x48000000u, 1u},
    {0x9B200000u, 0x44000000u, 1u},
    {0x9B300000u, 0xE0000000u, 1u},
    {0x9B400000u, 0x14000000u, 1u},
    {0x9B500000u, 0x10000000u, 1u},
    {0x9B600000u, 0x0A000000u, 1u},
    {0x9B700000u, 0x08000000u, 1u},
    {0x9B800000u, 0x50000000u, 1u},
    {0x9B900000u, 0x58000000u, 1u},
    {0x9BA00000u, 0x5C000000u, 1u},
};
class AsusP525PageTableBuilder : public PageTableBuilder {
public:
    using PageTableBuilder::PageTableBuilder;
    bool ShouldRegister() override {
        auto* b = emu_.TryGet<BoardContext>();
        return b && b->GetBoardFamilyId() == kFamily;
    }
    uint32_t VaToPa(uint32_t va) const override {
        for (auto m : kMap) if (va >= m.va && va - m.va < (m.mb << 20)) return m.pa + va - m.va;
        LOG(Caution, "P525 VA outside OEMAddressTable: %08X\n", va);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    uint32_t InitStackTopPa() const override { return 0xA4000000u; }
    std::vector<DramRegion> CachedDramRegions() const override {
        return {{0x88000000u, 0xA0000000u, 0x04000000u}};
    }
    std::vector<BackedRegion> BackedMemoryRegions() const override {
        return {{0x88000000u, 0xA0000000u, 0x04000000u, PAGE_READWRITE},
                {0x80000000u, 0x00000000u, 0x00020000u, PAGE_READONLY},
                {0x80020000u, 0x00020000u, 0x03FE0000u, PAGE_READWRITE},
                {0x84000000u, 0x04000000u, 0x04000000u, PAGE_READWRITE},
                {0x8C000000u, 0x0C000000u, 0x01000000u, PAGE_READWRITE},
                {0x9BA00000u, 0x5C000000u, 0x00040000u, PAGE_READWRITE}};
    }
    std::vector<DramRegion> MappedVaSpans() const override {
        std::vector<DramRegion> r;
        for (auto m : kMap) r.push_back({m.va, m.pa, m.mb << 20});
        return r;
    }
};
}
REGISTER_SERVICE_AS(AsusP525Context, BoardContext);
REGISTER_SERVICE_AS(AsusP525PageTableBuilder, PageTableBuilder);

namespace {
// touch.dll 012712E0 clears GPDR3 bit 17: PENIRQ is GPIO113.
// The OAL enables GRER3 bit 17 (rising edge), so a pen press drives it high.
// Its polling path at 012716F0 reads WM9713 X/Y ADC results (register 7Ah).
class AsusP525TouchInput : public TouchInput {
public:
    using TouchInput::TouchInput;
    bool ShouldRegister() override { return emu_.Get<BoardContext>().GetBoardFamilyId() == kFamily; }
    void OnReady() override {
        codec_ = &emu_.Get<Wm9713Codec>();
        codec_->SetPenLineObserver([](bool) {});
        gpio_ = &emu_.Get<Pxa27xGpio>();
        gpio_->SetInputLevel(113u, false);
        auto& clock = emu_.Get<GuestCycleClock>();
        event_ = clock.Add([this] { Tick(); });
        Tick();
    }
    void OnPenDown(int x, int y) override {
        if (getenv("CEWM_INPUT_TRACE")) {
            auto& gpio = emu_.Get<Pxa27xGpio>();
            fprintf(stderr, "[P525 TOUCH] grer3=%08X gfer3=%08X gpdr3=%08X\n", gpio.ReadWord(0x40E00130), gpio.ReadWord(0x40E0013C), gpio.ReadWord(0x40E0010C));
        }
        SetPosition(x, y); codec_->QueuePenEdge(true); pen_down_ = true;
    }
    void OnPenMove(int x, int y) override { SetPosition(x, y); }
    void OnPenUp(int x, int y) override { SetPosition(x, y); codec_->QueuePenEdge(false); pen_down_ = false; }
    void OnCaptureLost() override { codec_->QueuePenEdge(false); pen_down_ = false; }
private:
    static uint16_t Scale(int pos, uint32_t span) {
        if (span < 2u) return 0u;
        // The OEM sampling code reserves 0xFFF as its invalid-sample sentinel.
        return static_cast<uint16_t>(static_cast<uint64_t>(std::clamp(pos, 0, (int)span - 1)) * 4094u / (span - 1u));
    }
    void SetPosition(int x, int y) {
        codec_->SetPenPosition(Scale(x, cewm::Host().guest_w.load()), Scale(y, cewm::Host().guest_h.load()));
    }
    void Tick() {
        // PENIRQ must wake the touch driver even while the AC97 link is gated.
        // Waiting for an AC97 frame to drain the codec's pen queue deadlocks
        // the first touch when the native display has not kept the link awake.
        bool down = pen_down_.load();
        gpio_->SetInputLevel(113u, down);
        auto& clock = emu_.Get<GuestCycleClock>();
        clock.Arm(event_, clock.Cycles() + clock.NsToCycles(5000000));
    }
    std::atomic<bool> pen_down_{false};
    Pxa27xGpio* gpio_ = nullptr;
    GuestCycleClock::Event* event_ = nullptr;
    Wm9713Codec* codec_ = nullptr;
};
}
REGISTER_SERVICE_AS(AsusP525TouchInput, TouchInput);

#include "boot/rom_parser_service.h"
#include "tracing/trace_manager.h"
#include "peripherals/peripheral_dispatcher.h"
#include "execution/guest_cycle_clock.h"
#include "execution/engine_bridge.h"
#include "tracing/kernel_debug_sink.h"
#include <cstdlib>
namespace {
class AsusP525Debug : public Service {
public:
    using Service::Service;
    bool ShouldRegister() override { return emu_.Get<BoardContext>().GetBoardFamilyId() == kFamily; }
    void OnReady() override {
        // rilgsm refuses ordinary AT commands (0x81000D60) until the first successful AT+CFUN=<state>, which the guest never sends at cold
        // boot: the phone UI already shows the radio as on. The fake modem is on from power-up, so let the gate pass until the guest switches
        // the radio off (flight mode) and keep it closed until it is switched on again.
        // The gate test's `cmp r3,#0` and the store of the new gate value, per firmware (V4.010 / V3.43).
        const bool v4010 = emu_.Get<RomParserService>().Primary().raw.size() == 3100672u;
        const uint32_t check = v4010 ? 0x016F5BF0u : 0x0151567Cu, store = v4010 ? 0x016FCF0Cu : 0x01539D58u;
        emu_.Get<TraceManager>().OnPc(check, [this](const TraceContext& c) { if (!radio_off_) const_cast<uint32_t*>(c.regs)[3] = 0; });
        emu_.Get<TraceManager>().OnPc(store, [this](const TraceContext& c) { radio_off_ = c.regs[2] != 0; });
        if (!getenv("CEWM_P525_DEBUG")) return;
        auto& clock = emu_.Get<GuestCycleClock>();
        ev_ = clock.Add([this] { Tick(); });
        clock.Arm(ev_, clock.Cycles() + clock.NsToCycles(500000000));
        emu_.Get<TraceManager>().OnPc(0x03F7A688u, [this](const TraceContext& c) {
            LOG(Nkdbg, "P525 LoadDriver caller=%08X\n", c.regs[14]);
            emu_.Get<KernelDebugSink>().EmitWideStringAt(c, c.regs[0], "P525 driver");
        });
        for (auto pc : {0x03F77A78u, 0x03F77A98u}) emu_.Get<TraceManager>().OnPc(pc, [](const TraceContext& c) {
            LOG(Nkdbg, "P525 DLL result pc=%08X result=%08X caller=%08X\n", c.pc, c.regs[0], c.regs[14]);
        });
        emu_.Get<TraceManager>().OnPc(0x03F3EDD0u, [](const TraceContext& c) {
            if (c.regs[3] > 0x1000u) return;
            auto vol=c.ReadVa32(c.regs[5]+0x10).value_or(0);
            auto fs=c.ReadVa32(vol+8).value_or(0);
            LOG(Nkdbg, "P525 fs callback=%08X handle=%08X vol=%08X fs=%08X\n",c.regs[3],c.regs[5],vol,fs);
            for (unsigned n=0;n<0x50;n+=4) LOG(Nkdbg, "P525 fs fields +%X=%08X/%08X stack=%08X\n",n,c.ReadVa32(vol+n).value_or(0),c.ReadVa32(fs+n).value_or(0),c.ReadVa32(c.regs[13]+n).value_or(0));
        });
        emu_.Get<TraceManager>().OnPc(0x03F7B128u, [this](const TraceContext& c) {
            emu_.Get<KernelDebugSink>().EmitWideStringAt(c, c.regs[0], "P525 launch");
        });
        emu_.Get<TraceManager>().OnPc(0x8860E334u, [this](const TraceContext& c) {
            emu_.Get<KernelDebugSink>().EmitWideStringAt(c, c.regs[0], "P525");
            if (c.regs[14] == 0x885E8B40u) {
                LOG(Nkdbg, "P525 exception pc=%08X ra=%08X bva=%08X fsr=%08X\n", c.regs[2], c.ReadVa32(c.regs[13]+4).value_or(0), c.ReadVa32(c.regs[13]+16).value_or(0), c.ReadVa32(c.regs[13]+20).value_or(0));
                emu_.Get<KernelDebugSink>().EmitWideStringAt(c, c.regs[3], "P525 exception module");
            }
            LOG(Nkdbg, "P525 args %08X %08X %08X lr=%08X\n", c.regs[1], c.regs[2], c.regs[3], c.regs[14]);
        });
    }
private:
    GuestCycleClock::Event* ev_ = nullptr;
    bool radio_off_ = false;
    void Tick() {
        auto& c = emu_.Get<GuestCycleClock>();
        auto& p = emu_.Get<PeripheralDispatcher>();
        LOG(Nkdbg, "P525 timer ms=%lld icmr=%08X icpr=%08X oscr=%08X osmr0=%08X ossr=%08X oier=%08X\n", (long long)(c.NowNs()/1000000),
            p.ReadWord(0x40D00004), p.ReadWord(0x40D00010), p.ReadWord(0x40A00010), p.ReadWord(0x40A00000), p.ReadWord(0x40A00014), p.ReadWord(0x40A0001C));
        c.Arm(ev_, c.Cycles() + c.NsToCycles(500000000));
    }
};
}
REGISTER_SERVICE(AsusP525Debug);

#include "boot/board_boot_placer.h"
#include "boot/rom_parser_service.h"
#include "boot/guest_cold_boot.h"
#include "core/host_file_bytes.h"
#include "core/device_config.h"
#include "cpu/emulated_memory.h"
void (*g_p525_persist_root)() = nullptr;  // set while the board runs; host calls it on stop
namespace {
class AsusP525Storage : public BoardBootPlacer {
public:
    using BoardBootPlacer::BoardBootPlacer;
    bool ShouldRegister() override { return emu_.Get<BoardContext>().GetBoardFamilyId() == kFamily; }
    ~AsusP525Storage() override { g_p525_persist_root = nullptr; s_self = nullptr; }
    // The root volume is the board's writable flash. Write changed 64 KiB chunks back to
    // the VM's p525-root.img so registry/user data survive restarts (also across a GA toggle).
    void StartPersist() {
        if (root_size_ == 0) return;
        g_p525_persist_root = [] { if (s_self) s_self->Persist(); };
        s_self = this;
        auto& clock = emu_.Get<GuestCycleClock>();
        persist_ev_ = clock.Add([this] {
            Persist();
            auto& c = emu_.Get<GuestCycleClock>();
            c.Arm(persist_ev_, c.Cycles() + c.NsToCycles(10000000000ull));
        });
        clock.Arm(persist_ev_, clock.Cycles() + clock.NsToCycles(10000000000ull));
    }
    void Persist() {
        std::lock_guard<std::mutex> lock(persist_mu_);
        if (root_size_ == 0) return;
        FILE* f = nullptr;
        // Hash the guest RAM in place when the volume is one host mapping; copy chunks out otherwise.
        const uint8_t* direct = emu_.Get<EmulatedMemory>().TryTranslateRange(0x00020000u, root_size_);
        std::vector<uint8_t> buf(direct ? 0 : kChunk);
        for (size_t off = 0; off < root_size_; off += kChunk) {
            const size_t n = std::min(kChunk, root_size_ - off);
            if (!direct) emu_.Get<EmulatedMemory>().CopyOut(0x00020000u + (uint32_t)off, buf.data(), n);
            const uint8_t* chunk = direct ? direct + off : buf.data();
            const uint64_t h = Hash(chunk, n);
            if (h == root_hash_[off / kChunk]) continue;
            if (!f) f = fopen(root_path_.c_str(), "r+b");
            if (!f) return;
            fseek(f, (long)off, SEEK_SET);
            if (fwrite(chunk, 1, n, f) != n) break;
            root_hash_[off / kChunk] = h;
        }
        if (f) fclose(f);
    }
    void PlaceAfterRom() override {
        const auto& path = emu_.Get<RomParserService>().Primary().path;
        const auto dir = path.substr(0, path.find_last_of("/\\") + 1);
        auto load = [&](const char* name, uint32_t pa, size_t max) {
            auto b = ReadHostFileBytes(dir + name);
            if (b.empty()) return;
            // IMGFS wins over the XIP carrier with the same name. With GA
            // enabled, hide only the native LCD module so the injected XIP
            // display stub is loaded. Keep the on-disk firmware unchanged.
            if (!strcmp(name, "p525-disk.bin") && emu_.Get<DeviceConfig>().guest_additions) {
                constexpr char16_t lcd[] = u"pxa27x_lcd.dll";
                auto it = std::search(b.begin(), b.end(), reinterpret_cast<const uint8_t*>(lcd), reinterpret_cast<const uint8_t*>(lcd) + sizeof(lcd));
                if (it != b.end()) { *it = 'x'; *(it + 2) = 'x'; }
            }
            if (b.size() > max) CerfFatalExit(CERF_FATAL_USER_ERROR);
            // The disk's tail past 64 MiB lives at PA 0C000000 (the 04000000 window is only 64 MiB).
            const size_t first = pa == 0x04000000u ? std::min<size_t>(b.size(), 0x4000000u) : b.size();
            emu_.Get<EmulatedMemory>().CopyIn(pa, b.data(), first);
            if (first < b.size()) emu_.Get<EmulatedMemory>().CopyIn(0x0C000000u, b.data() + first, b.size() - first);
            LOG(Boot, "P525 storage: %s at PA %08X (%zu bytes)\n", name, pa, b.size());
            if (!strcmp(name, "p525-root.img")) {
                root_path_ = dir + name;
                root_size_ = b.size();
                root_hash_.clear();
                for (size_t off = 0; off < b.size(); off += kChunk) root_hash_.push_back(Hash(b.data() + off, std::min(kChunk, b.size() - off)));
            }
        };
        load("p525-storage.bin", 0x00010000u, 0x10000u);
        load("p525-disk.bin", 0x04000000u, 0x5000000u);
        load("p525-root.img", 0x00020000u, 0x3FE0000u);
        StartPersist();  // root_size_ is only known here, after OnReady
    }
private:
    static constexpr size_t kChunk = 64 * 1024;
    static uint64_t Hash(const uint8_t* p, size_t n) {  // 4 interleaved FNV-style lanes over 64-bit words, enough to spot a changed chunk
        uint64_t h[4] = {1469598103934665603ull, 1099511628211ull, 14695981039346656037ull, 0x9E3779B97F4A7C15ull};
        size_t i = 0;
        for (; i + 32 <= n; i += 32)
            for (int k = 0; k < 4; ++k) { uint64_t w; memcpy(&w, p + i + 8 * k, 8); h[k] = (h[k] ^ w) * 1099511628211ull; h[k] ^= h[k] >> 29; }
        for (; i < n; ++i) { h[0] ^= p[i]; h[0] *= 1099511628211ull; }
        return h[0] ^ (h[1] * 3) ^ (h[2] * 5) ^ (h[3] * 7);
    }
    inline static AsusP525Storage* s_self = nullptr;
    std::string root_path_;
    size_t root_size_ = 0;
    std::vector<uint64_t> root_hash_;
    std::mutex persist_mu_;
    GuestCycleClock::Event* persist_ev_ = nullptr;
};
}
REGISTER_SERVICE_AS(AsusP525Storage, BoardBootPlacer);

#include "peripherals/wolfson_wm9713/wm9713_master_clocks.h"
namespace {
// Provisional board oscillator wiring; validate against the P525 driver setup.
class AsusP525CodecClocks : public Wm9713MasterClocks {
public:
    using Wm9713MasterClocks::Wm9713MasterClocks;
    bool ShouldRegister() override { return emu_.Get<BoardContext>().GetBoardFamilyId() == kFamily; }
    uint64_t MclkaHz() const override { return 24576000u; }
    uint64_t MclkbHz() const override { return 0u; }
};
}
REGISTER_SERVICE_AS(AsusP525CodecClocks, Wm9713MasterClocks);

#include "asus_p525_io.h"
#include "host/keyboard_input.h"
#include "host/keyboard_router.h"
#include "socs/pxa27x/pxa27x_keypad.h"
#include <mutex>
#include <vector>
namespace {
// pxa27x_keypad_Us.dll: 0125A81C scans six columns / three rows using
// the scan-code table at 0125C62C. BuiltInKeypad's VK table is 01251030.
// The joystick is separate: 012542AC reads active-low GPIO87/23/90/26/95;
// 01253428 translates its contact combinations into directional scan codes.
class AsusP525KeyboardInput : public KeyboardInput {
public:
    using KeyboardInput::KeyboardInput;
    bool ShouldRegister() override { return emu_.Get<BoardContext>().GetBoardFamilyId() == kFamily; }
    std::wstring SourceName() const override { return L"ASUS P525 keypad"; }
    int SourcePriority() const override { return 200; } // Keep the OEM IME path with GA enabled too.
    bool CanDeliverVk(uint8_t vk) const override {
        uint32_t row, col;
        return Matrix(vk, row, col) || Contacts(vk) != 0 || vk == 0x72 || vk == 0x73 || vk == 0x5B || vk == 0x08;
    }
    void OnReady() override {
        io_ = &emu_.Get<AsusP525Io>();
        keypad_ = &emu_.Get<Pxa27xKeypad>();
        gpio_ = &emu_.Get<Pxa27xGpio>();
        SetContacts(0);
        emu_.Get<KeyboardRouter>().Register(this);
        // Guest Additions blits spend their time in byte copy loops: cerf_guest.dll's stage-in (head 0x001B7B2C; r0=length, r2=source, r3=destination)
        // and the kernel memcpy byte path (head 0x80010328; r6=count, r0=source, r7=destination). Move all but the last byte here and let the guest finish it.
        if (emu_.Get<DeviceConfig>().guest_additions && !getenv("CEWM_NOFASTCOPY")) {
            struct Loop { uint32_t head; uint32_t code[4]; int n, src, dst; };
            static const Loop kLoops[] = {
                {0x001B7B2Cu, {0xE4D20001u, 0xE4C30001u, 0xE1530001u, 0x1AFFFFFBu}, 0, 2, 3},  // ldrb r0,[r2],#1 ; strb r0,[r3],#1 ; cmp r3,r1 ; bne
                {0x80010328u, {0xE4D04001u, 0xE2566001u, 0xE4C74001u, 0x1AFFFFFBu}, 6, 0, 7},  // ldrb r4,[r0],#1 ; subs r6,r6,#1 ; strb r4,[r7],#1 ; bne
            };
            for (const Loop& l : kLoops) emu_.Get<TraceManager>().OnPc(l.head - (l.n == 0 ? 4u : 0u), [&l](const TraceContext& c) {
                for (int i = 0; i < 4; i++) if (c.ReadVa32(l.head + i * 4) != l.code[i]) return;
                auto* r = const_cast<uint32_t*>(c.regs);
                if (r[l.n] < 64 || r[l.n] > 0x100000u) { static int d2 = 0; if (l.head > 0x80000000u && d2++ < 10) fprintf(stderr, "[FC] range n=%u\n", r[l.n]); return; }
                auto& guest = c.emu.Get<EngineBridge>().Guest();
                for (uint32_t left = r[l.n] - 1; left;) {
                    auto sp = guest.PeekGuestVa(r[l.src]), dp = guest.PeekGuestVa(r[l.dst]);
                    if (!sp || !dp) { static int dbg = 0; if (dbg++ < 20) fprintf(stderr, "[FC] head=%08X unmapped src=%08X(%d) dst=%08X(%d)\n", l.head, r[l.src], !!sp, r[l.dst], !!dp); return; }
                    uint32_t chunk = std::min({left, 4096u - (r[l.src] & 4095u), 4096u - (r[l.dst] & 4095u)});
                    memmove(*dp, *sp, chunk);
                    r[l.src] += chunk; r[l.dst] += chunk; r[l.n] -= chunk; left -= chunk;
                }
            });
        }
        // Guest Additions allow unsigned apps by writing two security-policy values; on this ROM the write sends the Today shell (shell32.exe) into a
        // permanent relayout loop (~14 full repaints/s, each rereading ROM resources). The GA callers are skipped; unsigned apps keep the ROM's own policy.
        // coredll RegSetValueExW: 0x03F802C8 on V3.43, 0x03F6EB00 on V4.010. A hook at the other layout's address is never reached by GA's callers.
        if (emu_.Get<DeviceConfig>().guest_additions && !getenv("CEWM_GA_TRUST")) for (uint32_t reg_set : {0x03F802C8u, 0x03F6EB00u}) emu_.Get<TraceManager>().OnPc(reg_set, [](const TraceContext& c) {
            auto* r = const_cast<uint32_t*>(c.regs);
            if (r[14] < 0x1A0000u || r[14] >= 0x1CC000u) return;
            std::string nm;
            for (uint32_t j = 0; j < 40 && r[1]; j++) { auto h = c.ReadVa32(r[1] + (j & ~1u) * 2); if (!h) break; uint16_t ch = (uint16_t)(*h >> (16 * (j & 1))); if (!ch) break; nm += (char)ch; }
            if (nm == "AllowUntrustedApps" || nm == "0000101a") { r[0] = 0; r[15] = r[14]; }
        });
        if (getenv("CEWM_INPUT_TRACE")) {
            emu_.Get<TraceManager>().OnPc(0x01254EECu, [](const TraceContext& c) {
                fprintf(stderr, "[KEYPAD] OEM scan=%02X up=%u\n", c.regs[9], c.regs[3]);
            });
        }
        // The OEM drivers' scan routines are the board's "I saw it": a release waits until the one that reads the key has run since its press.
        // pxa27x_keypad_Us.dll sits at 0x01250000 on V3.43 and 0x017B0000 on V4.010 (the 3100672-byte bridge); V4.010's matrix scan is 8 bytes earlier.
        const bool v4010 = emu_.Get<RomParserService>().Primary().raw.size() == 3100672u;
        emu_.Get<TraceManager>().OnPc(v4010 ? 0x017BA814u : 0x0125A81Cu, [this](const TraceContext&) { scans_[0]++; });
        emu_.Get<TraceManager>().OnPc(v4010 ? 0x017B42A4u : 0x012542ACu, [this](const TraceContext&) { scans_[1]++; });
        auto& clock = emu_.Get<GuestCycleClock>();
        event_ = clock.Add([this] { Drain(); });
        Rearm();
    }
    void OnHostKey(uint8_t vk, bool up) override {
        if (!CanDeliverVk(vk)) return;
        std::lock_guard<std::mutex> lock(mutex_);
        pending_.push_back({vk, up});
    }
private:
    struct Edge { uint8_t vk; bool up; };
    std::mutex mutex_;
    std::vector<Edge> pending_;
    std::deque<Edge> work_;
    std::atomic<uint32_t> scans_[2] = {};
    uint32_t seen_[256] = {}, down_tick_[256] = {}, tick_ = 0;
    AsusP525Io* io_ = nullptr;
    Pxa27xKeypad* keypad_ = nullptr;
    Pxa27xGpio* gpio_ = nullptr;
    GuestCycleClock::Event* event_ = nullptr;
    uint8_t contacts_[256] = {};
    static uint8_t Contacts(uint8_t vk) {
        switch (vk) {
        case 0x26: return 0x15; // up
        case 0x27: return 0x13; // right
        case 0x28: return 0x1A; // down
        case 0x25: return 0x1C; // left
        case 0x0D: return 0x10; // joystick centre
        default: return 0;
        }
    }
    static bool Matrix(uint8_t vk, uint32_t& row, uint32_t& col) {
        // Host VK stands for a matrix position (scan table at 0125C62C). The OEM
        // routine at 01254E80 turns scan 1F (host C1, UI Mode) into the task-switcher chord and plain scans 0C/0D
        // into F6/F7 (volume). Start and C are not in the matrix: they are PCA9535 lines (AsusP525Io::Key).
        static constexpr uint8_t keys[3][6] = {
            {0x33, 0x36, 0x39, 0x71, 0x70, 0x78},
            {0x32, 0x35, 0x38, 0x5C, 0xC1, 0x30},
            {0x31, 0x34, 0x37, 0x75, 0x76, 0x77},
        };
        for (row = 0; row < 3; ++row)
            for (col = 0; col < 6; ++col)
                if (keys[row][col] == vk) return true;
        return false;
    }
    void SetContacts(uint8_t mask) {
        static constexpr uint8_t pins[] = {87, 23, 90, 26, 95};
        for (unsigned i = 0; i < 5; ++i) gpio_->SetInputLevel(pins[i], !(mask & (1u << i)));
    }
    void Rearm() {
        auto& clock = emu_.Get<GuestCycleClock>();
        clock.Arm(event_, clock.Cycles() + clock.NsToCycles(5000000));
    }
    void Drain() {
        { std::lock_guard<std::mutex> lock(mutex_); work_.insert(work_.end(), pending_.begin(), pending_.end()); pending_.clear(); }
        tick_++;
        while (!work_.empty()) {
            const Edge e = work_.front();
            uint32_t row, col;
            const int kind = Matrix(e.vk, row, col) ? 0 : Contacts(e.vk) ? 1 : -1;
            if (kind >= 0 && !e.up) { seen_[e.vk] = scans_[kind]; down_tick_[e.vk] = tick_; }
            // shortcut: a driver that never scans again is let go after 100 ticks (0.5 s of guest time)
            if (kind >= 0 && e.up && scans_[kind] == seen_[e.vk] && tick_ - down_tick_[e.vk] < 100) break;
            work_.pop_front();
            if (kind >= 0 && e.up && getenv("CEWM_INPUT_TRACE")) fprintf(stderr, "[KEYPAD] %02X released after %u ticks, %u scans\n", e.vk, tick_ - down_tick_[e.vk], (uint32_t)scans_[kind] - seen_[e.vk]);
            if (e.vk == 0x72 || e.vk == 0x73 || e.vk == 0x5B || e.vk == 0x08) {
                io_->Key(e.vk, !e.up);
            } else if (Matrix(e.vk, row, col)) {
                keypad_->SetMatrixKey(row, col, !e.up);
                if (getenv("CEWM_INPUT_TRACE")) fprintf(stderr, "[KEYPAD] key %02X %s matrix row=%u col=%u\n", e.vk, e.up ? "up" : "down", row, col);
            } else {
                contacts_[e.vk] = e.up ? 0 : Contacts(e.vk);
                uint8_t mask = 0;
                for (auto c : contacts_) mask |= c;
                SetContacts(mask);
            }
        }
        Rearm();
    }
};
}
REGISTER_SERVICE(AsusP525KeyboardInput);
