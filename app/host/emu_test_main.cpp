// Headless smoke test: boot the ROM for a few seconds, dump the frame as PPM.
#include "emu_runner.h"
#include "host_state.h"
#include <chrono>
#include <cstdio>
#include <thread>
#include <unistd.h>
#include <vector>
namespace cerfengine { uint64_t InterpInsnCount(); uint64_t InterpRunCount(); uint64_t InterpIdleHits(); void InterpBlockStats(uint64_t*); }
int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s rom [secs]\n", argv[0]); return 2; }
    // CEWM_STATE=file: resume from it if present and save to it on exit (snapshot round-trip test).
    const char* st = getenv("CEWM_STATE");
    int gw = 240, gh = 400;
    if (const char* sz = getenv("CEWM_SIZE")) sscanf(sz, "%dx%d", &gw, &gh);
    if (const char* b = getenv("CEWM_BOARD")) cewm::Host().board_id = b;
    if (const char* d = getenv("CEWM_DPI")) cewm::Host().dpi = atoi(d);
    if (getenv("CEWM_GA")) { cewm::Host().guest_additions = true; if (const char* d = getenv("CEWM_SHARE")) cewm::Host().share_dir = d; }
    if (const char* b = getenv("CEWM_BATT")) cewm::Host().battery_percent = atoi(b);  // test hooks: battery level and charger
    if (const char* c = getenv("CEWM_CHG")) cewm::Host().on_battery = atoi(c) == 0;
    cewm::EmuStart(argv[1], gw, gh, st ? st : "", st != nullptr);
    // Wait up to `secs` for the first frame, then dump it and report how long it took.
    std::vector<uint32_t> px;
    uint32_t w = 0, h = 0;
    const auto t0 = std::chrono::steady_clock::now();
    const int secs = argc > 2 ? atoi(argv[2]) : 5;
    bool got = false;
    while (!got && std::chrono::steady_clock::now() - t0 < std::chrono::seconds(secs)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        cewm::PollWidgets();
        got = cewm::EmuGrabFrame(px, w, h);
    }
    if (got && getenv("CEWM_EXTRA")) {  // let the guest run on and report the frame every second (the first frame is only the splash)
        for (int i = 0, n = atoi(getenv("CEWM_EXTRA")); i < n; i++) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            cewm::PollWidgets();
            cewm::EmuGrabFrame(px, w, h);
            uint64_t sum = 0;
            for (uint32_t p : px) sum = sum * 31 + p;
            fprintf(stderr, "[TEST] t+%ds frame hash %016llx insns %.1fM\n", i + 1, (unsigned long long)sum, cerfengine::InterpInsnCount() / 1e6);
        }
    }
    if (got && getenv("CEWM_TEST_START")) {
        cewm::EmuKey(0x5b, false);
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        cewm::EmuKey(0x5b, true);
        std::this_thread::sleep_for(std::chrono::seconds(1));
        got = cewm::EmuGrabFrame(px, w, h);
    }
    if (got) if (const char* keys = getenv("CEWM_TEST_KEYS")) {
        // Comma-separated hex Windows virtual keys: 72 = WM Talk, 73 = End.
        for (const char* p = keys; *p;) {
            char* end = nullptr;
            const unsigned long vk = strtoul(p, &end, 16);
            if (end == p || vk > 255 || (*end && *end != ',')) { fprintf(stderr, "[TEST] invalid key list\n"); cewm::EmuStop(false); return 2; }
            cewm::EmuKey((uint8_t)vk, false);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            cewm::EmuKey((uint8_t)vk, true);
            std::this_thread::sleep_for(std::chrono::seconds(1));
            p = *end ? end + 1 : end;
        }
        got = cewm::EmuGrabFrame(px, w, h);
    }
    if (got) if (const char* taps = getenv("CEWM_TEST_TAPS")) {
        // Guest pixel coordinates x:y,x:y for exercising phone controls without a window.
        for (const char* p = taps; *p;) {
            char *endx = nullptr, *endy = nullptr;
            const long x = strtol(p, &endx, 10);
            const long y = *endx == ':' ? strtol(endx + 1, &endy, 10) : -1;
            if (endx == p || !endy || endy == endx + 1 || x < 0 || y < 0 || x >= w || y >= h || (*endy && *endy != ',')) {
                fprintf(stderr, "[TEST] invalid tap list\n"); cewm::EmuStop(false); return 2;
            }
            cewm::EmuPen(true, x, y, w, h);
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            cewm::EmuPen(false, x, y, w, h);
            std::this_thread::sleep_for(std::chrono::seconds(2));
            cewm::EmuGrabFrame(px, w, h);
            uint64_t sum = 0;
            for (uint32_t pixel : px) sum = sum * 31 + pixel;
            fprintf(stderr, "[TEST] tap %ld,%ld frame hash %016llx\n", x, y, (unsigned long long)sum);
            p = *endy ? endy + 1 : endy;
        }
        got = cewm::EmuGrabFrame(px, w, h);
    }
    if (getenv("CEWM_TEST_SOFT_RESET")) {
        if (!cewm::EmuSoftReset()) { cewm::EmuStop(false); return 3; }
        std::this_thread::sleep_for(std::chrono::seconds(15));
        cewm::PollWidgets();
        if (!cewm::EmuGrabFrame(px, w, h)) { cewm::EmuStop(false); return 4; }
        fprintf(stderr, "[TEST] soft reset: frame available %ux%u\n", w, h);
    }
    if (getenv("CEWM_LIVESAVE")) {  // save while the guest keeps running, then die without a clean stop
        cewm::EmuSnapshot();
        std::this_thread::sleep_for(std::chrono::seconds(2));
        fprintf(stderr, "[TEST] live snapshot done, insns %.1fM\n", cerfengine::InterpInsnCount() / 1e6);
        _exit(0);
    }
    fprintf(stderr, "[TEST] %.1f M guest instructions\n", cerfengine::InterpInsnCount() / 1e6);
    fprintf(stderr, "[TEST] idle-skip hits %llu\n", (unsigned long long)cerfengine::InterpIdleHits());
    fprintf(stderr, "[TEST] %.2f M Run() calls\n", cerfengine::InterpRunCount() / 1e6);
    { uint64_t o[5]; cerfengine::InterpBlockStats(o);
      fprintf(stderr, "[TEST] blocks run=%llu slow-lookups=%llu builds=%llu ops=%llu step-fallbacks=%llu avg-ops/block=%.2f\n", (unsigned long long)o[0], (unsigned long long)o[1], (unsigned long long)o[2], (unsigned long long)o[3], (unsigned long long)o[4], o[0] ? (double)o[3] / o[0] : 0.0); }
    fprintf(stderr, "[TEST] frame %s after %.1fs\n", got ? "ready" : "NOT ready",
            std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
    if (got) {
        FILE* f = fopen("frame.ppm", "wb");
        fprintf(f, "P6 %u %u 255\n", w, h);
        for (uint32_t p : px) { fputc(p >> 16, f); fputc(p >> 8, f); fputc(p, f); }
        fclose(f);
    }
    cewm::EmuStop(st && got && !getenv("CEWM_NOSAVE"));
    return got ? 0 : 1;
}
