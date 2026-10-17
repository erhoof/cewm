#include "emu_runner.h"
#include "host_state.h"

#include "core/cerf_emulator.h"
#include "core/log.h"
#include "execution/jit_runner.h"
#include "state/hibernation.h"
#include "host/panel_frame_renderer.h"
#include "host/guest_additions_frame_renderer.h"
#include "http_proxy.h"
#include "host/keyboard_router.h"
#include "host/touch_input.h"
#include "peripherals/cerf_virt/cerf_virt_resize.h"
#include "peripherals/cerf_virt/cerf_virt_pointer.h"
#include "peripherals/cerf_virt/cerf_guest_liveness.h"

#include "execution/engine_bridge.h"
#include "execution/guest_cycle_clock.h"
#include <cerfengine/guest_engine.h>
#include <dirent.h>
#include <algorithm>
#include <chrono>
#include <atomic>
#include <thread>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace cewm {
namespace {
std::unique_ptr<CerfEmulator> g_emu;
std::mutex g_save_mu;  // keeps a background snapshot from racing EmuStop
std::vector<std::string> g_args;
std::vector<char*> g_argv;
std::atomic<bool> g_pclog_stop{false};
}  // namespace

void EmuStart(const std::string& rom, int w, int h, const std::string& state_path, bool resume) {
    g_args = {"cewm", "--board-id=" + Host().board_id, "--rom-primary=" + rom,
              "--screen-width=" + std::to_string(w), "--screen-height=" + std::to_string(h),
              };
    // Guest Additions bring the shared folder and a virtual NIC behind libslirp NAT; without them the board has no network.
    if (Host().guest_additions) {
        g_args.push_back("--guest-additions");
        if (Host().dpi > 0) g_args.push_back("--screen-dpi=" + std::to_string(Host().dpi));
        if (!Host().share_dir.empty()) g_args.push_back("--ga-share-folder=" + Host().share_dir);
    } else {
        g_args.push_back("--disable-network");
    }
    if (const char* ar = getenv("CEWM_AUTORUN")) {  // test hook: guest command lines separated by '|'
        for (std::string all = ar, e; !all.empty();) {
            size_t b = all.find('|');
            g_args.push_back("--ga-autorun=" + all.substr(0, b));
            all = b == std::string::npos ? "" : all.substr(b + 1);
        }
    }
    if (const char* ex = getenv("CEWM_CERF_ARGS")) {  // test hook: extra CERF arguments separated by spaces (e.g. --log=ALL)
        for (std::string all = ex, e; !all.empty();) {
            size_t b = all.find(' ');
            if (b) g_args.push_back(all.substr(0, b));
            all = b == std::string::npos ? "" : all.substr(b + 1);
        }
    }
    {  // a serial-config EEPROM image next to the ROM (Jornada 720: jorn720_eeprom.bin); boards without one ignore it
        const size_t sl = rom.rfind('/');
        const std::string dir = sl == std::string::npos ? "." : rom.substr(0, sl);
        if (DIR* d = opendir(dir.c_str())) {
            while (dirent* e = readdir(d)) {
                const std::string n = e->d_name;
                if (n.find("eeprom") != std::string::npos && n.size() > 4 && n.compare(n.size() - 4, 4, ".bin") == 0) g_args.push_back("--rom-eeprom=" + dir + "/" + n);
            }
            closedir(d);
        }
    }
    if (getenv("CEWM_PROXY")) fprintf(stderr, "[PROXY] listening on %d\n", StartHttpProxy(18080));  // guest sees it as 10.0.2.2:18080
    for (auto& a : g_args) g_argv.push_back(a.data());
    g_argv.push_back(nullptr);
    Host().state_path = state_path;
    Host().resume = resume;
    Host().guest_w = w;
    Host().guest_h = h;
    CerfConfig cfg;
    ParseCerfArgs((int)g_args.size(), g_argv.data(), cfg);
    g_emu = std::make_unique<CerfEmulator>(cfg, (int)g_args.size(), g_argv.data());
    g_emu->Boot();
    if (getenv("CEWM_PCLOG")) {  // test hook: print the guest PC every CEWM_PCLOG ms to see whether it is stuck
        g_pclog_stop = false;
        std::thread([] {
            for (int i = 0; !g_pclog_stop; i++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(atoi(getenv("CEWM_PCLOG"))));
                if (g_pclog_stop || !g_emu) break;
                fprintf(stderr, "[PC] n=%d pc=0x%08X guest_ms=%lld\n", i + 1, g_emu->Get<EngineBridge>().Engine().Get<cerfengine::GuestEngine>().Pc(), (long long)(g_emu->Get<GuestCycleClock>().NowNs() / 1000000));
            }
        }).detach();
    }
}

void EmuStop(bool save) {
    std::lock_guard<std::mutex> lk(g_save_mu);
    if (!g_emu) return;
    g_pclog_stop = true;
    g_emu->Get<JitRunner>().RequestStop();
    g_emu->WaitForExit();
    if (save && !Host().state_path.empty() && !Host().powered_down) g_emu->Get<Hibernation>().Save(L"");
    g_emu.reset();
}

void EmuSnapshot() {  // live save: Hibernation::Save freezes the guest itself
    std::lock_guard<std::mutex> lk(g_save_mu);
    if (g_emu && !Host().state_path.empty() && !Host().powered_down) g_emu->Get<Hibernation>().Save(L"");
}

bool EmuGrabFrame(std::vector<uint32_t>& bgra, uint32_t& w, uint32_t& h) {
    if (!g_emu) return false;
    // The LCD renderer is the board's PanelFrameRenderer; the Win32 PresentedFrameRenderer on top of it is not ported.
    FrameRenderer* fr = g_emu->TryGet<GuestAdditionsFrameRenderer>();  // the Guest Additions framebuffer when those are on, else the LCD panel
    if (!fr) fr = g_emu->TryGet<PanelFrameRenderer>();
    if (!fr || !fr->HasFrame()) return false;
    fr->PresentedSize(w, h);
    if (w == 0 || h == 0) return false;
    bgra.resize(static_cast<size_t>(w) * h);
    fr->RenderInto(bgra.data(), w, h);
    return true;
}

bool EmuRunning() { return g_emu != nullptr; }

bool EmuResize(int w, int h) {
    if (!g_emu || w <= 0 || h <= 0) return false;
    auto* r = g_emu->TryGet<CerfVirtResize>();
    if (!r) return false;
    r->RequestResize((uint32_t)w, (uint32_t)h);
    return true;
}

void EmuPen(bool down, int x, int y, int w, int h) {
    if (!g_emu) return;
    // Guest Additions pointer: the guest driver places the cursor from 0..65535 fractions of its own screen, so it stays right at any resolution
    // (the stock panel's calibration is only good at the native size). It needs the Guest Additions driver to be up.
    auto* gp = g_emu->TryGet<CerfVirtPointer>();
    auto* live = g_emu->TryGet<CerfGuestLiveness>();
    if (gp && live && live->IsAlive() && w > 1 && h > 1) {
        static bool was_down_ga = false;
        const uint32_t nx = (uint32_t)((uint64_t)std::clamp(x, 0, w - 1) * 65535u / (uint32_t)(w - 1));
        const uint32_t ny = (uint32_t)((uint64_t)std::clamp(y, 0, h - 1) * 65535u / (uint32_t)(h - 1));
        if (down || was_down_ga) gp->SetPointer(nx, ny, down ? 1u : 0u);
        was_down_ga = down;
        return;
    }
    auto* t = g_emu->TryGet<TouchInput>();
    if (!t) return;
    static bool was_down = false;
    if (down && !was_down) t->OnPenDown(x, y);
    else if (down) t->OnPenMove(x, y);
    else if (was_down) t->OnPenUp(x, y);
    was_down = down;
}

void EmuKey(uint8_t vk, bool up) {
    if (!g_emu) return;
    if (auto* k = g_emu->TryGet<KeyboardRouter>()) k->OnHostKey(vk, up);
}
}  // namespace cewm
