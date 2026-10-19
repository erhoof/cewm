// Frontend: imgui launcher (tabs like the nfsmw port) -> guest view. Guest size = display / N, integer nearest scale.
#include <SDL.h>
#include <SDL_opengles2.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl2.h>

#include <sys/stat.h>
#include <errno.h>
#include <ftw.h>
#include <dirent.h>
#include <unistd.h>
#include <cstring>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
// Any ARM build (aarch64 or the 32-bit armv7hl) runs on a phone/tablet: real full-screen window and display size; only the x86 laptop build fakes a phone.
#if defined(__aarch64__) || defined(__arm__)
#define CEWM_ON_PHONE 1
#endif
#include "emu_runner.h"
#include "p525_keypad.h"
#include "../host/p525_prepare.h"
#include "../host/haptic.h"
#include "host_state.h"
#include "device_stats.h"

#include "fonts/NotoSansBold.h"
#include "theme.h"
#include "romdb.h"
#include "rom_download.h"
#include "catalog.h"

#ifdef CEWM_WAYLAND
#include <SDL_syswm.h>
#include <wayland-client.h>
#endif

namespace {

#ifdef CEWM_ON_PHONE
constexpr int kVirtScale = 1;
#else
constexpr int kVirtScale = 3;
int kVirtW = 1080, kVirtH = 2400;  // laptop: virtual phone drawn at 360x800; --win WxH (window pixels) makes it any size, e.g. 900x700 for a landscape board
#endif

struct Settings {
    char rom[512] = "";
    int fixed_w = 480;  // original (native) ROM width: guest width is fixed to it and the scale follows from it; 0 only for --scale runs
    bool keep_w = true;  // keep the guest width at fixed_w; off: guest = display / n (some images dislike a changed width)
    int dev_w = 0, dev_h = 0;  // screen of the device these pixel settings were made for (0 = unknown); FitToDevice rescales them on another device
    int rot = 0;         // screen rotation in quarter turns (0, 90, 180, 270 degrees), done in a shader
    int orig_h = 800;   // original ROM height (shown in the editor; the guest height follows the screen unless fixed_h is set)
    float n = 3.f;  // --scale runs: guest = display / n
    bool borders = true;  // black side margins for the waterfall screen
    int border_px = 48;
    int top_px = 144;  // extra space above the guest for the camera cutout (default 3x the border)
    bool softkeys = true;
    bool p525_keypad = false;
    bool p525_keybg = false;  // dark rounded plates behind the T9 keys
    int bar_px = 140;
    char board[24] = "devemu_wvga";  // Device Emulator board variant for the ROM (its LCD class); 5.0 is a QVGA build
    int fixed_h = 0;  // >0: the ROM wants exactly this guest height (6.5: 800, CE: 480): the picture is fitted and centred unless fill is on; the editor warns, nothing is locked
    bool radio = true;   // fake GSM modem (CAIF/AT on UART0): signal bars, operator, Phone app; needs a fresh boot
    bool audio = false;  // guest audio out (off by default)
    bool proxy = false;  // host https-upgrading HTTP proxy at 10.0.2.2:18080 for the guest (needs Guest Additions)
    bool jit = false;  // dynarmic JIT instead of the interpreter (experimental)
    int dpi = 0;  // >0: compact mode, the guest sees this logical dpi (Guest Additions only)
    bool ga = false;  // Guest Additions: CE-side driver giving a shared folder (the VM's Storage dir) and a NAT NIC; changes the display path, so it has its own saved machine
    int btn_dy = 0;  // soft-button icons: vertical shift in px (+ down), for screens whose visible area differs from the window
    bool orig_res = false;  // run at the ROM's own resolution: no fill, no width change (off for VMs saved before the option existed)
    bool fill = false;  // fill the whole screen: full width, guest height as tall as the LCD allows, soft buttons in the leftover strip
    bool resume = true;  // save the machine when leaving the guest, restore it on start (like flash surviving a power cycle)
} g_s;  // the running VM (or the command-line run)

inline int FixedW() { return g_s.keep_w ? g_s.fixed_w : 0; }

// Downloaded ROMs wait here until a VM that uses one is saved.
constexpr const char* kStageDir = "/tmp/cewm-dl";

std::string Home() { const char* h = getenv("HOME"); return h ? h : "/tmp"; }

void MkdirP(const std::string& d) {
    for (size_t i = 1; i <= d.size(); i++)
        if (i == d.size() || d[i] == '/') mkdir(d.substr(0, i).c_str(), 0755);
}

std::string ConfDir() {  // command-line runs keep their saved machine here
    static const std::string d = Home() + "/.local/share/ru.erhoof/cewm";
    MkdirP(d);
    return d;
}

// Documents/CEWM holds vm-NAME.pref (settings) and NAME/ (ROM copy, saved machine, Storage/ = the guest's shared folder) per VM.
const std::string& VmRoot() {
    static const std::string d = [] { std::string r = Home() + "/Documents/CEWM"; MkdirP(r); return r; }();
    return d;
}
std::string VmDir(const std::string& n) { return VmRoot() + "/" + n; }
std::string PrefPath(const std::string& n) { return VmRoot() + "/vm-" + n + ".pref"; }

bool SetKey(Settings& s, const std::string& k, const std::string& v) {
    if (k == "rom") snprintf(s.rom, sizeof s.rom, "%s", v.c_str());
    else if (k == "fixed_w") s.fixed_w = std::clamp(atoi(v.c_str()), 0, 1000);
    else if (k == "keep_w") s.keep_w = atoi(v.c_str());
    else if (k == "dev_w") s.dev_w = std::clamp(atoi(v.c_str()), 0, 20000);
    else if (k == "dev_h") s.dev_h = std::clamp(atoi(v.c_str()), 0, 20000);
    else if (k == "rot") s.rot = std::clamp(atoi(v.c_str()), 0, 3);
    else if (k == "n") s.n = std::clamp((float)atof(v.c_str()), 1.f, 8.f);
    else if (k == "orig_h") s.orig_h = std::clamp(atoi(v.c_str()), 100, 1500);
    else if (k == "borders") s.borders = atoi(v.c_str());
    else if (k == "border_px") s.border_px = std::clamp(atoi(v.c_str()), 0, 160);
    else if (k == "softkeys") s.softkeys = atoi(v.c_str());
    else if (k == "p525_keypad") s.p525_keypad = atoi(v.c_str());
    else if (k == "p525_keybg") s.p525_keybg = atoi(v.c_str());
    else if (k == "top_px") s.top_px = std::clamp(atoi(v.c_str()), 0, 400);
    else if (k == "bar_px") s.bar_px = std::clamp(atoi(v.c_str()), 60, 300);
    else if (k == "board" && !v.empty()) snprintf(s.board, sizeof s.board, "%s", v.c_str());
    else if (k == "radio") s.radio = atoi(v.c_str());
    else if (k == "audio") s.audio = atoi(v.c_str());
    else if (k == "proxy") s.proxy = atoi(v.c_str());
    else if (k == "jit") s.jit = atoi(v.c_str());
    else if (k == "fixed_h") s.fixed_h = std::clamp(atoi(v.c_str()), 0, 1020);
    else if (k == "dpi") s.dpi = std::clamp(atoi(v.c_str()), 0, 400);
    else if (k == "ga") s.ga = atoi(v.c_str());
    else if (k == "btn_dy") s.btn_dy = std::clamp(atoi(v.c_str()), -150, 150);
    else if (k == "fill") s.fill = atoi(v.c_str());
    else if (k == "origres") s.orig_res = atoi(v.c_str());
    else if (k == "resume") s.resume = atoi(v.c_str());
    else return false;
    return true;
}

// `dir`: the VM's dir; the pref stores the ROM as a bare file name inside it.
bool ReadPref(const std::string& path, const std::string& dir, Settings& s) {
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return false;
    char line[600];
    while (fgets(line, sizeof line, f)) {
        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        std::string v = eq + 1;
        while (!v.empty() && (v.back() == '\n' || v.back() == '\r')) v.pop_back();
        SetKey(s, line, v);
    }
    fclose(f);
    if (s.rom[0] && !strchr(s.rom, '/')) { const std::string full = dir + "/" + s.rom; snprintf(s.rom, sizeof s.rom, "%s", full.c_str()); }
    return true;
}

bool WritePref(const std::string& path, const Settings& s) {
    FILE* f = fopen(path.c_str(), "w");
    if (!f) return false;
    const char* base = strrchr(s.rom, '/');
    fprintf(f, "p525_keypad=%d\n", s.p525_keypad);
    fprintf(f, "p525_keybg=%d\n", s.p525_keybg);
    fprintf(f, "rom=%s\ndev_w=%d\ndev_h=%d\nfixed_w=%d\nkeep_w=%d\nrot=%d\nn=%g\norig_h=%d\nborders=%d\nborder_px=%d\ntop_px=%d\nsoftkeys=%d\nbar_px=%d\nboard=%s\nfixed_h=%d\ndpi=%d\nga=%d\nbtn_dy=%d\nfill=%d\norigres=%d\nresume=%d\njit=%d\naudio=%d\nradio=%d\nproxy=%d\n",
            base ? base + 1 : s.rom, s.dev_w, s.dev_h, s.fixed_w, s.keep_w, s.rot, s.n, s.orig_h, s.borders, s.border_px, s.top_px, s.softkeys, s.bar_px, s.board, s.fixed_h, s.dpi, s.ga, s.btn_dy, s.fill, s.orig_res,
            s.resume, s.jit, s.audio, s.radio, s.proxy);
    return fclose(f) == 0;
}

enum class Mode { Launcher, Emu } g_mode = Mode::Launcher;

SDL_Window* g_win = nullptr;
bool g_win_forced = false;  // --win given: keep that size
float g_dispW = 0, g_dispH = 0;                    // set each frame from ImGui
std::atomic<bool> g_booting{false}, g_started{false};
std::thread g_boot_thread;
int g_gw = 0, g_gh = 0;                            // guest size the emulator was started with
Uint32 g_start_ticks = 0;

// Layout (all in display pixels). With the black borders on, the same inset is kept on the sides, above the guest, between the guest and
// the soft buttons, and below the buttons; the buttons are always separated by a small gap.
constexpr float kBtnGap = 12.f;
constexpr int kMaxGuestH = 1020;  // the guest LCD size fields are 10 bits wide
constexpr float kMinBar = 70.f;   // smallest soft-button strip in fill mode
// gapk/pad: the dark plate around the picture (T9 key backgrounds on): it runs between the key columns' outer edges, the picture sits pad inside it.
struct Layout { float inset, top, aw, ah, bar, y_btn; int fill_gh; float gapk = 0, pad = 0, dx = 0;
                float paw() const { return aw - gapk - 2 * pad; } float pah() const { return ah - gapk / 2 - 2 * pad; } };
bool Fill() { return g_s.fill; }
// Traced from aurora/icon_172.png: four pane outlines (star-shaped around their centroid); units of half the flag width, flag centre at 0,0.
static const float kFlagC[4][2] = {{-0.296f,-0.529f},{0.526f,-0.262f},{-0.527f,0.271f},{0.297f,0.529f}};
static const float kFlag[4][48][2] = {
  {{0.100f,-0.529f},{0.085f,-0.479f},{0.071f,-0.431f},{0.056f,-0.383f},{0.041f,-0.334f},{0.027f,-0.281f},{0.010f,-0.223f},{-0.017f,-0.166f},{-0.070f,-0.137f},{-0.140f,-0.151f},{-0.202f,-0.178f},{-0.253f,-0.197f},{-0.296f,-0.208f},{-0.338f,-0.216f},{-0.380f,-0.218f},{-0.425f,-0.219f},{-0.478f,-0.214f},{-0.548f,-0.201f},{-0.639f,-0.187f},{-0.725f,-0.200f},{-0.763f,-0.260f},{-0.753f,-0.340f},{-0.731f,-0.413f},{-0.713f,-0.474f},{-0.696f,-0.529f},{-0.681f,-0.580f},{-0.669f,-0.629f},{-0.654f,-0.677f},{-0.639f,-0.727f},{-0.619f,-0.777f},{-0.586f,-0.819f},{-0.539f,-0.845f},{-0.489f,-0.862f},{-0.441f,-0.877f},{-0.393f,-0.890f},{-0.345f,-0.900f},{-0.296f,-0.906f},{-0.247f,-0.908f},{-0.195f,-0.908f},{-0.141f,-0.905f},{-0.086f,-0.895f},{-0.030f,-0.877f},{0.028f,-0.854f},{0.087f,-0.824f},{0.133f,-0.777f},{0.146f,-0.713f},{0.133f,-0.644f},{0.116f,-0.584f}},
  {{0.923f,-0.262f},{0.908f,-0.211f},{0.894f,-0.163f},{0.880f,-0.115f},{0.867f,-0.065f},{0.849f,-0.014f},{0.817f,0.029f},{0.770f,0.057f},{0.720f,0.074f},{0.671f,0.088f},{0.623f,0.100f},{0.576f,0.112f},{0.526f,0.120f},{0.476f,0.122f},{0.424f,0.121f},{0.370f,0.115f},{0.315f,0.105f},{0.258f,0.088f},{0.199f,0.066f},{0.141f,0.034f},{0.096f,-0.013f},{0.081f,-0.077f},{0.094f,-0.146f},{0.113f,-0.207f},{0.129f,-0.262f},{0.143f,-0.312f},{0.157f,-0.361f},{0.171f,-0.409f},{0.185f,-0.459f},{0.200f,-0.512f},{0.218f,-0.570f},{0.246f,-0.626f},{0.299f,-0.656f},{0.368f,-0.644f},{0.431f,-0.616f},{0.482f,-0.598f},{0.526f,-0.588f},{0.568f,-0.581f},{0.611f,-0.576f},{0.656f,-0.575f},{0.710f,-0.580f},{0.781f,-0.593f},{0.871f,-0.606f},{0.955f,-0.591f},{0.992f,-0.530f},{0.980f,-0.450f},{0.957f,-0.377f},{0.939f,-0.316f}},
  {{-0.130f,0.271f},{-0.145f,0.321f},{-0.158f,0.370f},{-0.171f,0.419f},{-0.186f,0.468f},{-0.201f,0.521f},{-0.218f,0.580f},{-0.247f,0.636f},{-0.298f,0.667f},{-0.367f,0.657f},{-0.431f,0.628f},{-0.482f,0.608f},{-0.527f,0.597f},{-0.569f,0.591f},{-0.612f,0.588f},{-0.658f,0.589f},{-0.712f,0.592f},{-0.782f,0.603f},{-0.873f,0.617f},{-0.957f,0.601f},{-0.992f,0.539f},{-0.980f,0.459f},{-0.958f,0.387f},{-0.939f,0.325f},{-0.923f,0.271f},{-0.908f,0.221f},{-0.895f,0.172f},{-0.884f,0.123f},{-0.871f,0.073f},{-0.852f,0.022f},{-0.820f,-0.022f},{-0.774f,-0.051f},{-0.723f,-0.068f},{-0.673f,-0.083f},{-0.625f,-0.094f},{-0.576f,-0.101f},{-0.527f,-0.109f},{-0.476f,-0.114f},{-0.424f,-0.114f},{-0.369f,-0.109f},{-0.313f,-0.099f},{-0.256f,-0.082f},{-0.199f,-0.056f},{-0.143f,-0.024f},{-0.096f,0.023f},{-0.081f,0.087f},{-0.094f,0.155f},{-0.113f,0.217f}},
  {{0.697f,0.529f},{0.682f,0.580f},{0.669f,0.629f},{0.654f,0.677f},{0.638f,0.726f},{0.619f,0.776f},{0.586f,0.818f},{0.539f,0.845f},{0.489f,0.863f},{0.441f,0.877f},{0.393f,0.890f},{0.345f,0.899f},{0.297f,0.905f},{0.247f,0.907f},{0.195f,0.908f},{0.141f,0.904f},{0.087f,0.892f},{0.030f,0.876f},{-0.029f,0.854f},{-0.088f,0.824f},{-0.133f,0.777f},{-0.146f,0.712f},{-0.133f,0.644f},{-0.116f,0.583f},{-0.099f,0.529f},{-0.084f,0.479f},{-0.069f,0.431f},{-0.056f,0.383f},{-0.041f,0.334f},{-0.025f,0.282f},{-0.009f,0.223f},{0.018f,0.166f},{0.070f,0.137f},{0.139f,0.149f},{0.202f,0.175f},{0.253f,0.194f},{0.297f,0.206f},{0.338f,0.214f},{0.380f,0.217f},{0.426f,0.216f},{0.479f,0.213f},{0.548f,0.201f},{0.639f,0.187f},{0.725f,0.200f},{0.763f,0.259f},{0.753f,0.340f},{0.730f,0.413f},{0.712f,0.474f}},
};
static constexpr int kFlagN = 48;
bool Soft() { return g_s.softkeys || g_s.p525_keypad; }
float ButtonBarHeight(float width) {
    return g_s.p525_keypad ? width * 0.925f : (Soft() ? (float)g_s.bar_px : 0.f);
}
bool FillBar() { return Fill() && Soft(); }  // fill with a soft-button strip under the picture
int g_scaler_w = 0, g_scaler_h = 0;

Layout MakeLayout(float W, float H) {
    Layout l{};
    if (Fill()) {  // edge to edge; whatever height the LCD cannot take becomes the soft-button strip
        l.aw = W;
        l.top = g_s.top_px;  // camera notch: the picture starts below it
        const float n = FixedW() > 0 ? W / FixedW() : g_s.n, avail = H - l.top;
        int gh = std::min(kMaxGuestH, (int)(avail / n + 1e-3f));
        const float min_bar = g_s.p525_keypad ? ButtonBarHeight(W) : kMinBar;
        if (Soft() && avail - gh * n < min_bar) gh = (int)((avail - min_bar) / n + 1e-3f);
        l.fill_gh = std::max(1, gh);
        if (Soft()) { l.ah = l.fill_gh * n; l.bar = avail - l.ah; l.y_btn = l.top + l.ah; }
        else { l.ah = avail; l.y_btn = H; }  // no buttons: the picture takes the whole height, centred
        return l;
    }
    l.inset = g_s.borders ? g_s.border_px : 0;
    l.top = g_s.top_px;  // independent of the side borders: the camera cutout is there either way
    l.bar = ButtonBarHeight(W - 2 * l.inset);
    l.aw = W - 2 * l.inset;
    if (g_s.p525_keypad && FixedW() > 0 && g_s.fixed_h > 0) {  // too short for the whole device: shrink it (screen + keys) to fit, same aspect, centred
        const float fp = g_s.p525_keybg ? 0.955f : 1.f, fg = g_s.p525_keybg ? 0.0325f : 0.f;
        const float k = (float)g_s.fixed_h / FixedW() * fp + fg + 0.925f, need = l.top + 2 * l.inset + k * l.aw;
        if (need > H) { const float w = std::max(60.f, (H - l.top - 2 * l.inset) / k); l.dx = (l.aw - w) / 2; l.aw = w; l.bar = ButtonBarHeight(w); }
    }
    l.ah = H - l.top - (l.bar > 0 ? l.inset + l.bar : 0) - l.inset /*bottom*/;
    l.y_btn = l.top + l.ah + l.inset;
    if (g_s.p525_keypad && g_s.p525_keybg) {
        l.gapk = l.aw * 0.025f; l.pad = std::round(l.aw * 0.010f);
    }
    return l;
}

// Effective scale: from a fixed guest width when set, else the chosen 1/N.
float EffScale(const Layout& l) {
    if (!Fill() && FixedW() > 0 && g_s.fixed_h > 0) return std::min(l.paw() / FixedW(), l.pah() / g_s.fixed_h);  // native-size ROM: fit, never stretch
    return FixedW() > 0 ? l.paw() / FixedW() : g_s.n;
}

// Guest size: the fixed width (or area / N) by area height / scale.
void GuestSizeFor(float W, float H, int* gw, int* gh) {
    const Layout l = MakeLayout(W, H);
    const float n = EffScale(l);
    *gw = FixedW() > 0 ? FixedW() : std::max(1, (int)(l.paw() / n));
    // The guest's LCD size fields are 10 bits wide (1066 came back as a 42-line frame), so cap the height; the image is centred.
    *gh = Fill() ? l.fill_gh : g_s.fixed_h > 0 ? g_s.fixed_h : std::clamp((int)(l.pah() / n), 1, kMaxGuestH);
}

// One saved machine per VM: state-<W>x<H><tail>.img in the VM's dir. The guest size is part of the file name, and a start with a saved
// machine reuses that size, so changing the layout settings does not strand the saved machine. Command-line runs (--rom) have no VM
// and keep the old per-ROM naming in ConfDir().
std::string g_vm;  // name of the running VM; empty for a command-line run

std::string StateDir() { return g_vm.empty() ? ConfDir() : VmDir(g_vm); }

std::string StateKey(const std::string& rom) {
    if (!g_vm.empty()) return "state";
    uint32_t hash = 2166136261u;
    for (unsigned char c : rom) hash = (hash ^ c) * 16777619u;
    for (unsigned char c : std::string(g_s.board)) hash = (hash ^ c) * 16777619u;
    char key[24];
    snprintf(key, sizeof key, "state-%08x", hash);
    return key;
}

std::string StateTail(const Settings& s) { return std::string(s.radio ? "-rad" : "") + (s.ga ? "-ga" : "") + std::to_string(s.dpi) + ".img"; }

std::string StatePath(const std::string& key, int w, int h) {
    return StateDir() + "/" + key + "-" + std::to_string(w) + "x" + std::to_string(h) + StateTail(g_s);
}

// The newest saved machine in `dir` for this key and these settings, if any.
bool SavedSizeIn(const std::string& dir, const std::string& key, const Settings& s, int* w, int* h, bool prune = false) {
    bool found = false;
    time_t newest = 0;
    std::vector<std::pair<time_t, std::string>> all;
    const std::string prefix = key + "-", tail = StateTail(s);
    if (DIR* d = opendir(dir.c_str())) {
        while (dirent* e = readdir(d)) {
            int sw, sh, used = 0;
            if (strncmp(e->d_name, prefix.c_str(), prefix.size())) continue;
            // Cut the tail off first: with radio and GA off it is just "0.img", so "800x600" + "0.img" would read as height 6000.
            const std::string rest = e->d_name + prefix.size();
            if (rest.size() <= tail.size() || rest.compare(rest.size() - tail.size(), tail.size(), tail) != 0) continue;
            const std::string mid = rest.substr(0, rest.size() - tail.size());
            if (sscanf(mid.c_str(), "%dx%d%n", &sw, &sh, &used) != 2 || (size_t)used != mid.size()) continue;
            struct stat st;
            if (stat((dir + "/" + e->d_name).c_str(), &st) == 0) all.push_back({st.st_mtime, dir + "/" + e->d_name});
            if (stat((dir + "/" + e->d_name).c_str(), &st) == 0 && (!found || st.st_mtime > newest)) { found = true; newest = st.st_mtime; *w = sw; *h = sh; }
        }
        closedir(d);
    }
    if (prune)  // after a resolution change the machine is saved under the new size: the older saves of it are stale
        for (auto& f : all) if (f.first < newest) unlink(f.second.c_str());
    return found;
}
bool SavedSize(const std::string& key, int* w, int* h, bool prune = false) { return SavedSizeIn(StateDir(), key, g_s, w, h, prune); }

// Deletes every saved machine of a VM (whatever settings it was saved with).
void DiscardStatesIn(const std::string& dir) {
    if (DIR* d = opendir(dir.c_str())) {
        while (dirent* e = readdir(d)) {
            const std::string f = e->d_name;
            if (f.rfind("state-", 0) == 0 && f.size() > 4 && f.compare(f.size() - 4, 4, ".img") == 0) unlink((dir + "/" + f).c_str());
        }
        closedir(d);
    }
}

// A saved machine is restored at the size it was saved with. If the settings now ask for another size and Guest Additions are on, the guest is
// asked to switch to it a moment after it is back (see the main loop), so changing the resolution needs no reset.
int g_resize_w = 0, g_resize_h = 0;                    // resize still to be requested
int g_resize_watch_w = 0, g_resize_watch_h = 0;          // requested, the guest has not shown that size yet
Uint32 g_resize_watch_at = 0;
Uint32 g_resize_at = 0;

void StartEmu() {
#ifndef CEWM_ON_PHONE
    if (g_win && g_s.fixed_w > g_s.fixed_h && g_s.fixed_h > 0 && !g_win_forced) {  // laptop: a landscape board gets a landscape window, not the 360x800 phone
        const int ww = g_s.fixed_w * 6 / 5, wh = g_s.fixed_h * 5 / 4 + 80;
        kVirtW = ww * kVirtScale; kVirtH = wh * kVirtScale;
        SDL_SetWindowSize(g_win, ww, wh);
        SDL_SetWindowPosition(g_win, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    }
#endif
    const std::string key = StateKey(g_s.rom);
    int dw, dh, sw, sh;
    GuestSizeFor(g_s.rot & 1 ? g_dispH : g_dispW, g_s.rot & 1 ? g_dispW : g_dispH, &dw, &dh);
    g_resize_w = g_resize_h = 0;
    g_resize_at = 0;
    g_resize_watch_w = g_resize_watch_h = 0;
    if (g_s.resume && SavedSize(key, &sw, &sh, true)) {
        g_gw = sw; g_gh = sh;
        if (g_s.ga && (sw != dw || sh != dh)) { g_resize_w = dw; g_resize_h = dh; }
    } else {
        g_gw = dw; g_gh = dh;
    }
    g_booting = true;
    g_started = true;
    g_start_ticks = SDL_GetTicks();
    const std::string rom = g_s.rom;
    const int w = g_gw, h = g_gh;
    // Parsing the ROM and bringing up the board takes a moment; keep the UI alive meanwhile.
    const std::string state = StatePath(key, w, h);
    const bool resume = g_s.resume;
    if (g_s.radio) setenv("CEWM_FAKEMODEM", "1", 1); else unsetenv("CEWM_FAKEMODEM");
    setenv("CEWM_AUDIO", g_s.audio ? "1" : "0", 1);
    if (g_s.proxy && g_s.ga) setenv("CEWM_PROXY", "1", 1); else unsetenv("CEWM_PROXY");
    setenv("CEWM_JIT", g_s.jit ? "1" : "0", 1);
#ifdef CEWM_ON_PHONE
    setenv("CEWM_ASYNC", "1", 0);  // blocks are emitted on a worker thread, so they can be compiled much earlier
    setenv("CEWM_HOT", "100", 0);
#else
    setenv("CEWM_HOT", "2000", 0);
#endif
    cewm::Host().guest_additions = g_s.ga;
    cewm::Host().board_id = g_s.board;
    cewm::Host().dpi = g_s.dpi;
    cewm::Host().share_dir.clear();
    if (g_s.ga) {  // the shared folder lives in the user's Documents (UserDirs permission)
        const std::string d = g_vm.empty() ? Home() + "/Documents/CEWM/shared" : VmDir(g_vm) + "/Storage";
        MkdirP(d);
        cewm::Host().share_dir = d;
    }
    fprintf(stderr, "start: guest %dx%d display %.0fx%.0f rot %d board %s ga %d resume %d state %s\n", w, h, g_dispW, g_dispH, g_s.rot, g_s.board, (int)g_s.ga, (int)resume, state.c_str());
    g_boot_thread = std::thread([rom, w, h, state, resume] { cewm::EmuStart(rom, w, h, state, resume); g_booting = false; });
}

// `booted`: the guest has put up a frame, so its state is worth keeping.
void StopEmu(bool booted) {
    if (!g_started) return;
    if (g_boot_thread.joinable()) g_boot_thread.join();
    ReleaseP525Key();
    cewm::EmuStop(g_s.resume && booted);
    g_started = false;
}

bool g_quit = false;

float ES() { return ImGui::GetFontSize() * 0.8f; }

// ---- VM store -------------------------------------------------------------------------------------------------------------------

struct VmEntry { std::string name; Settings s; bool snap = false; };
std::vector<VmEntry> g_vms;

bool Exists(const std::string& p) { struct stat st; return stat(p.c_str(), &st) == 0; }
bool IsFile(const std::string& p) { struct stat st; return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode); }

std::string Base(const std::string& p) { const size_t i = p.rfind('/'); return i == std::string::npos ? p : p.substr(i + 1); }
std::string Stem(const std::string& p) { const std::string b = Base(p); const size_t i = b.rfind('.'); return i == std::string::npos || i == 0 ? b : b.substr(0, i); }

// A VM name is a file name: ASCII letters, digits, space, - and _ stay, so does any non-ASCII text (Cyrillic); everything else becomes a space.
std::string CleanName(const std::string& in) {
    std::string o;
    for (unsigned char c : in) {
        const char ch = (c >= 0x80 || isalnum(c) || c == '-' || c == '_') ? (char)c : ' ';
        if (ch == ' ' && (o.empty() || o.back() == ' ')) continue;
        o += ch;
    }
    if (o.size() > 48) { o.resize(48); while (!o.empty() && ((unsigned char)o.back() & 0xC0) == 0x80) o.pop_back(); if (!o.empty() && (unsigned char)o.back() >= 0xC0) o.pop_back(); }
    while (!o.empty() && o.back() == ' ') o.pop_back();
    return o.empty() ? "Generic VM" : o;
}

// `orig`: the VM being edited keeps its own name; any other taken name gets "_1", "_2", ...
std::string UniqueName(const std::string& base, const std::string& orig) {
    std::string c = base;
    for (int n = 1; c != orig && (Exists(PrefPath(c)) || Exists(VmDir(c))); n++) c = base + "_" + std::to_string(n);
    return c;
}

void ScanVms() {
    g_vms.clear();
    if (DIR* d = opendir(VmRoot().c_str())) {
        while (dirent* e = readdir(d)) {
            const std::string fn = e->d_name;
            if (fn.size() <= 8 || fn.compare(0, 3, "vm-") != 0 || fn.compare(fn.size() - 5, 5, ".pref") != 0) continue;
            VmEntry v;
            v.name = fn.substr(3, fn.size() - 8);
            ReadPref(VmRoot() + "/" + fn, VmDir(v.name), v.s);
            int w, h;
            v.snap = v.s.resume && SavedSizeIn(VmDir(v.name), "state", v.s, &w, &h);
            g_vms.push_back(v);
        }
        closedir(d);
    }
    std::sort(g_vms.begin(), g_vms.end(), [](const VmEntry& a, const VmEntry& b) { return a.name < b.name; });
}

// sparse: all-zero 64 KiB blocks become holes (a blank NAND image is almost all zeros).
bool CopyFile(const std::string& src, const std::string& dst, bool sparse = false) {
    FILE* in = fopen(src.c_str(), "rb");
    if (!in) return false;
    const std::string tmp = dst + ".part";
    FILE* out = fopen(tmp.c_str(), "wb");
    if (!out) { fclose(in); return false; }
    std::vector<char> buf(sparse ? 1 << 16 : 1 << 20);
    bool ok = true;
    long total = 0;
    for (size_t n; ok && (n = fread(buf.data(), 1, buf.size(), in)) > 0;) {
        total += (long)n;
        const bool zero = sparse && std::all_of(buf.begin(), buf.begin() + n, [](char c) { return c == 0; });
        ok = zero ? fseek(out, (long)n, SEEK_CUR) == 0 : fwrite(buf.data(), 1, n, out) == n;
    }
    if (ok && sparse) ok = fflush(out) == 0 && ftruncate(fileno(out), total) == 0;
    ok = ok && !ferror(in);
    ok = fclose(out) == 0 && ok;
    fclose(in);
    ok = ok && rename(tmp.c_str(), dst.c_str()) == 0;
    if (!ok) unlink(tmp.c_str());
    return ok;
}

// P525 "hard reset": the writable NAND volume goes back to the blank image kept next to it (registry, settings and files are lost).
bool ResetNand(const std::string& dir) {
    return IsFile(dir + "/p525-root.blank") && CopyFile(dir + "/p525-root.blank", dir + "/p525-root.img", true);
}

int RmRf(const std::string& p) {
    return nftw(p.c_str(), [](const char* f, const struct stat*, int, struct FTW*) { return remove(f); }, 16, FTW_DEPTH | FTW_PHYS);
}

// ---- editor state ---------------------------------------------------------------------------------------------------------------

enum class View { List, Edit, Browse, Catalog } g_view = View::List;
int g_tab = 0;
Settings g_ed;                 // the VM being created / edited
char g_ed_name[64] = "";
bool g_ed_orig_ga = false;       // Guest Additions setting the VM had when the editor opened
std::string g_ed_orig;         // name the VM had when the editor opened; empty for a new VM
std::string g_ed_prev_rom;     // the ROM copy it had then (replaced when another ROM is picked)
std::string g_ed_hashed;       // ROM path the screen size was last derived from
std::string g_ed_label;        // known-ROM name from the hash table; empty if unknown
bool g_ed_touched = false;     // the user typed a name: stop following the ROM's file name
int g_ed_tab = 0;
std::atomic<int> g_save{0};    // 0 idle, 1 copying/saving, 2 done, 3 failed
std::string g_save_err;

const char* BoardFor(int w, int h) {
    if (w >= 480) return h >= 720 ? "devemu_wvga" : "devemu_vga";
    return h >= 360 ? "devemu_wqvga" : "devemu";
}

// New VM on a tablet-shaped screen (phones are 1.78 and longer): a landscape guest (Jornada, SIMpad, CE 800x480) gets 270 degrees,
// no soft buttons and no camera offset; a portrait guest gets the plain defaults. Existing VMs are left alone.
void TabletDefaults() {
    if (!g_ed_orig.empty() || std::max(g_dispW, g_dispH) >= 1.7f * std::min(g_dispW, g_dispH)) return;
    const bool land = g_ed.fixed_w > g_ed.orig_h;
    g_ed.rot = land ? 3 : 0;
    g_ed.softkeys = !land;
    g_ed.top_px = land ? 0 : Settings().top_px;
}

void P525Defaults() {
    if (strcmp(g_ed.board, "asus_mypal_p525")) return;
    g_ed.ga = false;
    g_ed.orig_res = true;
    g_ed.fixed_w = 240;
    g_ed.orig_h = g_ed.fixed_h = 320;
    g_ed.fill = false;
    g_ed.keep_w = true;
    g_ed.p525_keypad = true;
    g_ed.p525_keybg = true;
}

// Picks a ROM: its hash tells which board and screen size it wants (unknown images get QVGA 240x320).
void SetRom(const std::string& path) {
    snprintf(g_ed.rom, sizeof g_ed.rom, "%s", path.c_str());
    g_ed_hashed = path;
    const std::string sha = romdb::Sha256File(path);
    static const romdb::Known kP525Bundle = {"", "Windows Mobile (ASUS P525)", "asus_mypal_p525", 240, 320, true};  // a bundle made by the preparer: any firmware
    const romdb::Known* k = romdb::Find(sha);
    if (!k && Base(path) == "p525-bridge.nb0" && IsFile(path.substr(0, path.rfind('/')) + "/p525-storage.bin")) k = &kP525Bundle;
    g_ed_label = k ? k->label : "";
    g_ed.fixed_w = k ? k->w : 240;  // unknown image: the most generic Windows Mobile screen, QVGA
    g_ed.orig_h = k ? k->h : 320;
    const bool real = k && strncmp(k->board, "devemu", 6) != 0;  // a real handheld: Guest Additions work there too, so any resolution and fill-screen are fine
    g_ed.fixed_h = k && k->exact ? k->h : 0;
    if (k && k->exact) { g_ed.fill = false; g_ed.keep_w = true; }  // safe defaults for an image that wants its native size; the user may change them
    snprintf(g_ed.board, sizeof g_ed.board, "%s", k ? k->board : "devemu");
    if (real) g_ed.ga = true;  // Guest Additions work on the real handhelds too
    P525Defaults();
    g_ed.dpi = 0;
    TabletDefaults();
    if (!g_ed_touched) snprintf(g_ed_name, sizeof g_ed_name, "%s", k == &kP525Bundle ? "ASUS P525" : CleanName(Stem(path)).c_str());
}

// The pixel settings (borders, camera-notch offset, button bar, icon shift) are tied to the screen they were made on. On another device
// (VM folder copied over) they are rescaled by the width ratio and the new screen is recorded. Returns true if anything changed.
bool FitToDevice(Settings& s) {
    const int w = (int)g_dispW, h = (int)g_dispH;
    if (w <= 0 || (s.dev_w == w && s.dev_h == h)) return false;
    if (s.dev_w > 0) {
        const float f = (float)w / s.dev_w;
        s.border_px = std::clamp((int)lroundf(s.border_px * f), 0, 160);
        s.top_px = std::clamp((int)lroundf(s.top_px * f), 0, 400);
        s.bar_px = std::clamp((int)lroundf(s.bar_px * f), 60, 300);
        s.btn_dy = std::clamp((int)lroundf(s.btn_dy * f), -150, 150);
    }
    s.dev_w = w;
    s.dev_h = h;
    return true;
}

void OpenEditor(int idx) {  // -1: a new VM
    g_ed_tab = 0;
    g_ed_label.clear();
    if (idx < 0) {
        g_ed = Settings();
        g_ed.ga = true;  // shared folder and network on by default for a new VM
        g_ed.orig_res = true;  // and the ROM's own resolution
        g_ed.fixed_w = 240; g_ed.orig_h = 320;
        snprintf(g_ed.board, sizeof g_ed.board, "devemu");
        snprintf(g_ed_name, sizeof g_ed_name, "Generic VM");
        g_ed_orig.clear();
        g_ed_prev_rom.clear();
        g_ed_hashed.clear();
        g_ed_touched = false;
    } else {
        g_ed = g_vms[idx].s;
        g_ed_orig_ga = g_ed.ga;
        FitToDevice(g_ed);  // saved with the VM when the editor is left
        snprintf(g_ed_name, sizeof g_ed_name, "%s", g_vms[idx].name.c_str());
        g_ed_orig = g_vms[idx].name;
        g_ed_prev_rom = g_ed.rom;
        g_ed_hashed = g_ed.rom;
        g_ed_touched = true;
    }
    g_save = 0;
    g_view = View::Edit;
}

// Runs off the UI thread (the ROM copy is ~100 MB): renames the VM's dir if the name changed, copies a newly picked ROM into it, writes the pref.
bool DoSave(Settings s, const std::string& name, const std::string& orig, std::string prev_rom, std::string* err) {
    const std::string dir = VmDir(name), old = orig.empty() ? "" : VmDir(orig);
    if (!orig.empty() && name != orig) {
        if (rename(old.c_str(), dir.c_str()) != 0 && errno != ENOENT) { *err = "Не удалось переименовать"; return false; }
        unlink(PrefPath(orig).c_str());
        for (std::string* p : {&prev_rom}) if (p->compare(0, old.size() + 1, old + "/") == 0) *p = dir + p->substr(old.size());
        const std::string r = s.rom;
        if (r.compare(0, old.size() + 1, old + "/") == 0) snprintf(s.rom, sizeof s.rom, "%s", (dir + r.substr(old.size())).c_str());
    }
    mkdir(dir.c_str(), 0755);
    const std::string rom = s.rom;
    if (!rom.empty() && rom.compare(0, dir.size() + 1, dir + "/") != 0) {  // a ROM from outside: keep our own copy
        const std::string src_dir = rom.substr(0, rom.rfind('/'));
        if (!strcmp(s.board, "asus_mypal_p525")) {
            for (const char* n : {"p525-storage.bin", "p525-disk.bin", "p525-root.img"}) {
                if (!IsFile(src_dir + "/" + n)) { *err = std::string("Нет файла ASUS P525: ") + n; return false; }
            }
            for (const char* n : {"p525-storage.bin", "p525-disk.bin", "p525-root.img"}) {
                if (!CopyFile(src_dir + "/" + n, dir + "/" + n, !strcmp(n, "p525-root.img"))) { *err = std::string("Не удалось скопировать: ") + n; return false; }
            }
            if (!CopyFile(src_dir + "/p525-root.img", dir + "/p525-root.blank", true)) { *err = "Не удалось скопировать: p525-root.blank"; return false; }  // kept for the NAND reset
        }
        const std::string dst = dir + "/" + Base(rom);
        if (!CopyFile(rom, dst)) { *err = "Не удалось скопировать ROM"; return false; }
        if (DIR* d = opendir(src_dir.c_str())) {  // the serial-config EEPROM next to the ROM (Jornada 720) belongs to the image
            while (dirent* e = readdir(d)) {
                const std::string n = e->d_name;
                if (n.find("eeprom") != std::string::npos && n.size() > 4 && n.compare(n.size() - 4, 4, ".bin") == 0 && !CopyFile(src_dir + "/" + n, dir + "/" + n)) {
                    closedir(d);
                    *err = "Не удалось скопировать EEPROM";
                    return false;
                }
            }
            closedir(d);
        }
        if (src_dir.compare(0, strlen(kStageDir) + 1, std::string(kStageDir) + "/") == 0 || src_dir == VmRoot() + "/.p525-stage") RmRf(src_dir);  // a downloaded or prepared image: the VM has its copy now
        if (!prev_rom.empty() && prev_rom != dst) unlink(prev_rom.c_str());
        DiscardStatesIn(dir);  // the saved machine belongs to the old image
        snprintf(s.rom, sizeof s.rom, "%s", dst.c_str());
    }
    if (!WritePref(PrefPath(name), s)) { *err = "Не удалось записать настройки"; return false; }
    return true;
}

void BeginSave() {
    g_save = 1;
    g_ed.dev_w = (int)g_dispW;  // remember which screen these pixel values belong to
    g_ed.dev_h = (int)g_dispH;
    const std::string name = UniqueName(CleanName(g_ed_name), g_ed_orig);
    std::thread([s = g_ed, name, orig = g_ed_orig, prev = g_ed_prev_rom] {
        std::string err;
        const bool ok = DoSave(s, name, orig, prev, &err);
        g_save_err = err;
        g_save = ok ? 2 : 3;
    }).detach();
}

// ---- widgets --------------------------------------------------------------------------------------------------------------------

// Touch scrolling: a mostly vertical drag in a scroll area moves it. Clicks that end such a drag must not fire (Tap), and a drag that
// is moving a slider or text field must not scroll (NoSwipe).
bool g_swipe = false, g_noswipe = false;
bool Tap(bool pressed) { return pressed && !g_swipe; }
void NoSwipe() { if (ImGui::IsItemActive()) g_noswipe = true; }
void SwipeScroll() {
    ImGuiIO& io = ImGui::GetIO();
    if (!g_noswipe && ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && ImGui::IsMouseDragging(0, 10.f) &&
        fabsf(io.MouseDelta.y) >= fabsf(io.MouseDelta.x)) {
        ImGui::SetScrollY(ImGui::GetScrollY() - io.MouseDelta.y);
        g_swipe = true;
    } else if (!io.MouseDown[0]) g_swipe = false;
    g_noswipe = false;
}
bool Slider(const char* id, int* v, int lo, int hi, const char* fmt) {
    ImGui::SetNextItemWidth(-1);
    const bool r = ImGui::SliderInt(id, v, lo, hi, fmt);
    NoSwipe();
    return r;
}

int NameFilter(ImGuiInputTextCallbackData* d) {  // 1 = reject the character
    const ImWchar c = d->EventChar;
    const bool ok = c < 128 ? (isalnum((int)c) || c == ' ' || c == '-' || c == '_') : ((c >= 0x400 && c <= 0x4FF) || (c >= 0xC0 && c <= 0xFF));
    return ok ? 0 : 1;
}

void DrawTabs(const char* const* names, int n, int* tab) {
    const float fs = ES(), tab_h = fs * 2.0f, gap = fs * 0.4f;
    const float tab_w = (ImGui::GetContentRegionAvail().x - gap * (n - 1)) / n;
    const ImVec4 accent = ImGui::GetStyleColorVec4(ImGuiCol_SliderGrab);
    const ImVec4 normal = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
    const ImVec4 hovered = ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (int i = 0; i < n; i++) {
        if (i > 0) ImGui::SameLine(0.f, gap);
        ImGui::PushID(i);
        ImGui::InvisibleButton("##tab", {tab_w, tab_h});
        if (ImGui::IsItemClicked()) *tab = i;
        const ImVec4 col = *tab == i ? accent : (ImGui::IsItemHovered() ? hovered : normal);
        const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        dl->AddRectFilled(a, b, ImGui::GetColorU32(col), fs * 0.8f, ImDrawFlags_RoundCornersTop);
        const ImVec2 ts = ImGui::CalcTextSize(names[i]);
        dl->AddText({a.x + (tab_w - ts.x) * 0.5f, a.y + (tab_h - ts.y) * 0.5f}, ImGui::GetColorU32(ImGuiCol_Text), names[i]);
        ImGui::PopID();
    }
}

// ---- screens --------------------------------------------------------------------------------------------------------------------

// ---- P525 firmware: a vendor .zip / .cap is turned into a bootable bundle in the stage dir --------------------------------------------

struct Prep { int phase = 0; int pct = 0; std::string text, rom; };  // phase: 0 idle, 1 busy, 2 done, 3 failed
std::mutex g_prep_mu;
Prep g_prep;

void StartPrepare(const std::string& src) {
    {
        std::lock_guard<std::mutex> lk(g_prep_mu);
        if (g_prep.phase == 1) return;
        g_prep = Prep{1, 0, "Подготовка..."};
    }
    std::thread([src] {
        const std::string out = VmRoot() + "/.p525-stage";  // the images are ~150 MB: not in /tmp, which may be RAM
        std::string err;
        const bool ok = cewm::p525::Prepare(src, out, [](int pct, const char* t) {
            std::lock_guard<std::mutex> lk(g_prep_mu);
            g_prep.pct = pct;
            g_prep.text = t;
        }, err);
        if (ok && !strncmp(src.c_str(), kStageDir, strlen(kStageDir))) unlink(src.c_str());  // a downloaded CAP is not needed once the bundle exists
        std::lock_guard<std::mutex> lk(g_prep_mu);
        g_prep = ok ? Prep{2, 100, "Готово", out + "/p525-bridge.nb0"} : Prep{3, 0, err, ""};
    }).detach();
}

Prep PrepGet() {
    std::lock_guard<std::mutex> lk(g_prep_mu);
    return g_prep;
}

// Called every frame by the views that start a preparation: hands a finished bundle to the editor. Returns true while busy.
bool PrepPoll() {
    const Prep p = PrepGet();
    if (p.phase == 2) {
        { std::lock_guard<std::mutex> lk(g_prep_mu); g_prep = Prep(); }
        SetRom(p.rom);
        g_view = View::Edit;
    }
    return p.phase == 1;
}

// ---- ROM file browser ---------------------------------------------------------------------------------------------------------

struct BrowseEntry { std::string name; bool dir; off_t size; };
std::string g_bdir;
std::vector<BrowseEntry> g_bent;
bool g_bscroll_top = false;

void ReadBrowseDir() {
    g_bent.clear();
    if (DIR* d = opendir(g_bdir.c_str())) {
        while (dirent* e = readdir(d)) {
            const std::string f = e->d_name;
            if (f[0] == '.') continue;
            struct stat st;
            if (stat((g_bdir + "/" + f).c_str(), &st) != 0) continue;
            if (S_ISDIR(st.st_mode)) g_bent.push_back({f, true, 0});
            else if (S_ISREG(st.st_mode)) {
                std::string lo = f;
                for (char& c : lo) c = tolower((unsigned char)c);
                const size_t dot = lo.rfind('.');
                const std::string ext = dot == std::string::npos ? "" : lo.substr(dot);
                if (ext == ".bin" || ext == ".nb0" || ext == ".img" || ext == ".rom" || ext == ".zip" || ext == ".cap") g_bent.push_back({f, false, st.st_size});
            }
        }
        closedir(d);
    }
    std::sort(g_bent.begin(), g_bent.end(), [](const BrowseEntry& a, const BrowseEntry& b) { return a.dir != b.dir ? a.dir : a.name < b.name; });
    g_bscroll_top = true;
}

void OpenBrowser() {
    if (g_bdir.empty() || !Exists(g_bdir)) {  // where a freshly downloaded image would be
        for (const std::string& d : {Home() + "/Downloads/CERF", Home() + "/Downloads", Home() + "/Documents", Home()})
            if (Exists(d)) { g_bdir = d; break; }
    }
    ReadBrowseDir();
    g_view = View::Browse;
}

// Full-screen folder browser: tap a folder to enter it, a ROM file to pick it (SetRom fills the editor), "Назад" to cancel.
void DrawBrowser(float w, float h) {
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize({w, h});
    ImGui::Begin("##browser", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);
    const float fs = ES();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {fs * 1.0f, fs * 0.6f});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {fs * 0.5f, fs * 0.6f});
    const char* title = "Выбор ROM";
    ImGui::SetCursorPos({(w - ImGui::CalcTextSize(title).x) * 0.5f, fs * 0.8f});
    ImGui::TextUnformatted(title);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", g_bdir.c_str());
    ImGui::PopStyleColor();
    const float btn_h = fs * 2.6f, y_btn = h - btn_h - fs * 1.5f;
    const bool prep_busy = PrepPoll();
    const Prep prep = PrepGet();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::BeginChild("##files", {0, y_btn - ImGui::GetCursorPosY() - ImGui::GetStyle().ItemSpacing.y - (prep.phase ? fs * 3.2f : 0)}, false);
    ImGui::BeginDisabled(prep_busy);
    if (g_bscroll_top) { ImGui::SetScrollY(0); g_bscroll_top = false; }
    std::string go;  // folder to enter next frame ("..": up)
    const float row = fs * 2.2f;
    if (g_bdir != "/" && Tap(ImGui::Selectable("[..]", false, 0, {0, row}))) go = "..";
    for (int i = 0; i < (int)g_bent.size(); i++) {
        const BrowseEntry& e = g_bent[i];
        char label[600];
        if (e.dir) snprintf(label, sizeof label, "[%s]", e.name.c_str());
        else snprintf(label, sizeof label, "%s  (%d МБ)", e.name.c_str(), (int)((e.size + (1 << 19)) >> 20));
        ImGui::PushID(i);
        if (Tap(ImGui::Selectable(label, false, 0, {0, row}))) {
            if (e.dir) go = e.name;
            else if (cewm::p525::IsFirmware(e.name)) StartPrepare(g_bdir + "/" + e.name);
            else { SetRom(g_bdir + "/" + e.name); g_view = View::Edit; }
        }
        ImGui::PopID();
    }
    if (g_bent.empty()) ImGui::TextDisabled("Нет образов (.bin .nb0 .img .rom .zip .cap) и папок");
    ImGui::EndDisabled();
    SwipeScroll();
    ImGui::EndChild();
    ImGui::PopStyleColor();
    if (prep_busy) {
        ImGui::TextUnformatted(prep.text.c_str());
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.25f, 0.25f, 0.28f, 1));
        ImGui::ProgressBar(prep.pct / 100.0f, {-1, fs * 0.8f}, "");
        ImGui::PopStyleColor();
    } else if (prep.phase == 3) {
        ImGui::TextColored({0.9f, 0.4f, 0.3f, 1}, "Ошибка: %s", prep.text.c_str());
    }
    ImGui::SetCursorPosY(y_btn);
    ImGui::BeginDisabled(prep_busy);
    if (ImGui::Button("Назад", {-1, btn_h})) { g_view = View::Edit; std::lock_guard<std::mutex> lk(g_prep_mu); g_prep = Prep(); }
    ImGui::EndDisabled();
    if (go == "..") { const size_t i = g_bdir.rfind('/'); g_bdir = i == 0 || i == std::string::npos ? "/" : g_bdir.substr(0, i); ReadBrowseDir(); }
    else if (!go.empty()) { g_bdir += (g_bdir == "/" ? "" : "/") + go; ReadBrowseDir(); }
    ImGui::PopStyleVar(2);
    ImGui::End();
}

// ---- ROM catalog (downloads from the cerf-bundles repository) -------------------------------------------------------------------

int g_cat_group = -1;  // -1: the list of device types, else the index into kGroups
int g_cat_pick = -1;   // the catalog row being downloaded; names the new VM

// Full-screen list of known images: tap one to download it, unpack it next to the others and pick it in the editor.
void DrawCatalog(float w, float h) {
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize({w, h});
    ImGui::Begin("##catalog", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);
    const float fs = ES();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {fs * 1.0f, fs * 0.6f});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {fs * 0.5f, fs * 0.6f});
    const char* title = g_cat_group < 0 ? "Скачать ROM" : kGroups[g_cat_group];
    ImGui::SetCursorPos({(w - ImGui::CalcTextSize(title).x) * 0.5f, fs * 0.8f});
    ImGui::TextUnformatted(title);
    const cewm::romdl::State st = cewm::romdl::Get();
    if (st.phase == 2) {  // downloaded: hand the image to the editor
        cewm::romdl::Reset();
        if (cewm::p525::IsFirmware(st.rom)) { StartPrepare(st.rom); OpenBrowser(); }  // a P525 CAP: made bootable on the way (the browser view shows it, then the editor opens)
        else SetRom(st.rom);
        if (!cewm::p525::IsFirmware(st.rom) && g_cat_pick >= 0 && !g_ed_touched) snprintf(g_ed_name, sizeof g_ed_name, "%s", CleanName((kCatalog[g_cat_pick].group ? std::string(kCatalog[g_cat_pick].device) + " " : std::string()) + kCatalog[g_cat_pick].system).c_str());
        if (!cewm::p525::IsFirmware(st.rom) && g_ed_label.empty() && !st.board.empty()) {  // an image the hash table does not know: the manifest says which board it is for
            struct B { const char* id; int w, h; bool real; };
            static const B kBoards[] = {{"devemu", 240, 320, 0}, {"devemu_vga", 480, 640, 0}, {"devemu_wqvga", 240, 400, 0}, {"devemu_wvga", 480, 800, 0},
                                        {"simpad_sl4", 800, 600, 1}, {"ipaq_h31xx", 240, 320, 1}, {"ipaq_h36xx", 240, 320, 1}, {"ipaq_h3660_h3670", 240, 320, 1}, {"jornada_720", 640, 240, 1}};
            for (const B& b : kBoards) {
                if (st.board != b.id) continue;
                snprintf(g_ed.board, sizeof g_ed.board, "%s", b.id);
                g_ed.fixed_w = b.w;
                g_ed.orig_h = b.h;
                if (b.real) { g_ed.fixed_h = b.h; g_ed.fill = false; g_ed.keep_w = true; g_ed.ga = true; }
                g_ed_label = st.label;
                TabletDefaults();
            }
        }
        if (!cewm::p525::IsFirmware(st.rom)) g_view = View::Edit;
    }
    const float btn_h = fs * 2.6f, y_btn = h - btn_h - fs * 1.5f;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::BeginChild("##items", {0, y_btn - ImGui::GetCursorPosY() - ImGui::GetStyle().ItemSpacing.y - (st.phase ? fs * 3.2f : 0)}, false);
    ImGui::BeginDisabled(st.phase == 1);
    const float ww = ImGui::GetContentRegionAvail().x - fs;  // text wraps here, so a long name always shows in full
    const int n_items = (int)(sizeof kCatalog / sizeof *kCatalog), n_groups = (int)(sizeof kGroups / sizeof *kGroups);
    const int n_rows = g_cat_group < 0 ? n_groups : n_items;
    for (int i = 0; i < n_rows; i++) {
        char l1[96], l2[200];
        if (g_cat_group < 0) {  // device types
            int n = 0;
            for (const CatalogItem& c : kCatalog) n += c.group == i;
            snprintf(l1, sizeof l1, "%s", kGroups[i]);
            snprintf(l2, sizeof l2, "Образов: %d", n);
        } else {
            if (kCatalog[i].group != g_cat_group) continue;
            snprintf(l1, sizeof l1, "%s", kCatalog[i].device);
            snprintf(l2, sizeof l2, "%s  (%d МБ)", kCatalog[i].system, kCatalog[i].mb);
        }
        const float h1 = ImGui::CalcTextSize(l1, nullptr, false, ww).y, h2 = ImGui::CalcTextSize(l2, nullptr, false, ww).y;
        ImGui::PushID(i);
        const bool hit = Tap(ImGui::Selectable("##item", false, 0, {0, h1 + h2 + fs * 0.8f}));
        const ImVec2 a = ImGui::GetItemRectMin();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), {a.x + fs * 0.5f, a.y + fs * 0.25f}, ImGui::GetColorU32(ImGuiCol_Text), l1, nullptr, ww);
        dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), {a.x + fs * 0.5f, a.y + fs * 0.35f + h1}, ImGui::GetColorU32(ImGuiCol_TextDisabled), l2, nullptr, ww);
        if (hit && g_cat_group < 0) { g_cat_group = i; ImGui::SetScrollY(0); ImGui::PopID(); break; }
        else if (hit) { g_cat_pick = i; cewm::romdl::Start(kCatalog[i].id, kStageDir); }
        ImGui::PopID();
    }
    ImGui::EndDisabled();
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("Образы берутся из каталога cerf-bundles.dz3n.net и скачиваются во временную папку и копируются в машину при сохранении. Права на образы принадлежат их владельцам.");
    ImGui::PopTextWrapPos();
    SwipeScroll();
    ImGui::EndChild();
    ImGui::PopStyleColor();
    if (st.phase == 1) {  // below the list
        ImGui::TextUnformatted(st.text.c_str());
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.25f, 0.25f, 0.28f, 1));
        ImGui::ProgressBar(st.pct / 100.0f, {-1, fs * 0.8f}, "");
        ImGui::PopStyleColor();
    } else if (st.phase == 3) {
        ImGui::TextColored({0.9f, 0.4f, 0.3f, 1}, "Ошибка: %s", st.text.c_str());
    }
    ImGui::SetCursorPosY(y_btn);
    ImGui::BeginDisabled(st.phase == 1);
    if (ImGui::Button("Назад", {-1, btn_h})) {
        cewm::romdl::Reset();
        if (g_cat_group >= 0) g_cat_group = -1;  // back to the device types first
        else g_view = View::Edit;
    }
    ImGui::EndDisabled();
    ImGui::PopStyleVar(2);
    ImGui::End();
}

// The original-resolution option pins the guest to the ROM's own size.
void ApplyOrigRes(Settings& s) {
    if (!s.orig_res) return;
    s.fixed_h = s.orig_h;
    s.keep_w = true;
    s.fill = false;
}

void RunVm(int i) {
    if (FitToDevice(g_vms[i].s)) WritePref(PrefPath(g_vms[i].name), g_vms[i].s);
    g_s = g_vms[i].s;
    ApplyOrigRes(g_s);
    g_vm = g_vms[i].name;
    StartEmu();
    g_mode = Mode::Emu;
}

// Word-wraps `text` to `width` and centres every line ("\n" forces a break).
void CenteredWrapped(const char* text, float width) {
    std::string line, word, all = text;
    auto flush = [&] {
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - ImGui::CalcTextSize(line.c_str()).x) * 0.5f);
        ImGui::TextUnformatted(line.c_str());
        line.clear();
    };
    all += ' ';
    for (char c : all) {
        if (c != ' ' && c != '\n') { word += c; continue; }
        if (!word.empty()) {
            const std::string t = line.empty() ? word : line + " " + word;
            if (!line.empty() && ImGui::CalcTextSize(t.c_str()).x > width) { flush(); line = word; }
            else line = t;
            word.clear();
        }
        if (c == '\n') flush();
    }
    if (!line.empty()) flush();
}

void DrawVmList() {
    const float fs = ES();
    if (g_vms.empty()) {
        const ImVec2 a = ImGui::GetContentRegionAvail();
        const float bw = a.x * 0.7f, bh = fs * 3.f;
        const float lh = ImGui::GetTextLineHeightWithSpacing(), text_h = lh * 9;  // room for the note above the button
        ImGui::SetCursorPosY((a.y - bh - text_h) / 2);
        CenteredWrapped("Виртуальных машин пока нет.\n \nСкачайте образ Windows Mobile или Windows CE "
                        "(файл .bin или .nb0, например из установщика Microsoft Device Emulator в формате .msi), "
                        "нажмите \"Создать ВМ\" и выберите его, или загрузите из онлайн-каталога.", a.x * 0.9f);
        ImGui::SetCursorPos({(a.x - bw) / 2, (a.y - bh) / 2 + text_h / 2});
        if (Tap(ImGui::Button("Создать ВМ", {bw, bh}))) OpenEditor(-1);
        return;
    }
    static int armed = -1;  // Reset asks twice, so a stray touch cannot throw a saved machine away
    static Uint32 armed_at = 0;
    if (armed >= 0 && SDL_GetTicks() - armed_at > 4000) armed = -1;
    int run = -1, edit = -1, reset = -1;
    const float bw = (ImGui::GetContentRegionAvail().x - 2 * ImGui::GetStyle().ItemSpacing.x) / 3;
    for (int i = 0; i < (int)g_vms.size(); i++) {
        const VmEntry& v = g_vms[i];
        ImGui::PushID(i);
        ImGui::TextUnformatted(v.name.c_str());
        if (Tap(ImGui::Button("Настройки", {bw, 0}))) edit = i;
        ImGui::SameLine();
        const bool p525 = !strcmp(v.s.board, "asus_mypal_p525");
        ImGui::BeginDisabled(!v.snap && !p525);
        if (Tap(ImGui::Button(armed == i ? "Точно?" : p525 ? "Сброс NAND" : "Сброс", {bw, 0}))) {
            if (armed == i) { reset = i; armed = -1; }
            else { armed = i; armed_at = SDL_GetTicks(); }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!IsFile(v.s.rom));
        if (Tap(ImGui::Button("Запуск", {bw, 0}))) run = i;
        ImGui::EndDisabled();
        ImGui::Separator();
        ImGui::PopID();
    }
    SwipeScroll();
    if (edit >= 0) OpenEditor(edit);
    else if (reset >= 0) {
        DiscardStatesIn(VmDir(g_vms[reset].name));
        if (!strcmp(g_vms[reset].s.board, "asus_mypal_p525")) ResetNand(VmDir(g_vms[reset].name));
        ScanVms();
    }
    else if (run >= 0) RunVm(run);
}

void DrawAbout() {
    ImGui::TextWrapped(
        "CE/WM - эмулятор Windows Mobile и Windows CE для ОС Аврора.\n\n"
        "Автор: Павел Бибиченко (@erhoof)\n"
        "Telegram: t.me/erhoof\n"
        "Версия 1.2\n\n"
        "Основа: CERF / libCerfEngine (MIT), dynarmic, libslirp, Dear ImGui (MIT) и SDL2 (zlib).\n\n"
        "Спасибо @sashikknox за шаблон интерфейса на ImGui.\n\n"
        "Образы ROM в комплект не входят, используйте свои. Виртуальные машины лежат в Documents/CEWM.");
}

// Dropdowns: the popup is exactly as wide as the box and its rows span it edge to edge.
bool Dropdown(const char* id, const char* preview) {
    ImGui::SetNextWindowSizeConstraints({ImGui::GetContentRegionAvail().x, 0}, {ImGui::GetContentRegionAvail().x, FLT_MAX});  // exactly as wide as the box
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    const bool open = ImGui::BeginCombo(id, preview);
    ImGui::PopStyleVar();
    return open;
}
bool DropItem(const char* label, bool sel, const char* tag = nullptr) {  // tag: a green note after the label
    const float row = ImGui::GetTextLineHeight() * 1.15f;  // a little taller than the text, like the highlighted row of a padded popup
    // BeginCombo pads the popup by FramePadding.x, and Selectable's highlight spills half an ItemSpacing past its box: undo both.
    const float sp = ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetCursorPosX(sp * 0.5f);
    const bool r = ImGui::Selectable((std::string("##") + label).c_str(), sel, 0, {ImGui::GetWindowWidth() - sp, row});
    const ImVec2 a = ImGui::GetItemRectMin();
    ImGui::GetWindowDrawList()->AddText({a.x + ES() * 0.8f, a.y + (row - ImGui::GetTextLineHeight()) * 0.5f}, ImGui::GetColorU32(ImGuiCol_Text), label);
    if (tag) ImGui::GetWindowDrawList()->AddText({a.x + ES() * 0.8f + ImGui::CalcTextSize(label).x + ES() * 0.4f, a.y + (row - ImGui::GetTextLineHeight()) * 0.5f}, IM_COL32(90, 210, 110, 255), tag);
    return r;
}

void Hint(const char* text) {  // dimmed, wrapped description under a setting
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

void DrawEditGeneral() {
    const float fs = ES();
    ImGui::TextUnformatted("Название");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##name", g_ed_name, sizeof g_ed_name, ImGuiInputTextFlags_CallbackCharFilter, NameFilter)) g_ed_touched = true;
    NoSwipe();
    ImGui::Dummy({0, fs * 0.3f});
    ImGui::TextUnformatted("Образ ROM");
    if (Tap(ImGui::Button(g_ed.rom[0] ? Base(g_ed.rom).c_str() : "Выбрать ROM...", {-1, 0}))) OpenBrowser();
    if (Tap(ImGui::Button("Скачать ROM...", {-1, 0}))) g_view = View::Catalog;
    const std::string path = g_ed.rom;
    const bool ok = IsFile(path);
    if (!path.empty()) Hint(path.c_str());
    if (!path.empty() && !ok) ImGui::TextColored({0.9f, 0.7f, 0.2f, 1}, "Файл не найден");
    else if (path.empty()) ImGui::TextColored({0.9f, 0.7f, 0.2f, 1}, "Выберите образ ROM");
    else if (!g_ed_label.empty()) ImGui::TextColored({0.4f, 0.9f, 0.4f, 1}, "%s", g_ed_label.c_str());
    else if (path != g_ed_prev_rom) ImGui::TextColored({0.9f, 0.7f, 0.2f, 1}, "Неизвестный образ: разрешение по умолчанию");
    ImGui::Dummy({0, fs * 0.3f});
    ImGui::TextUnformatted("Модель устройства");
    ImGui::SetNextItemWidth(-1);
    struct Model { const char* board; const char* label; int w, h; };  // w x h: the fixed screen of a real device (0: set by the ROM)
    static const Model kModels[] = {
        {"devemu", "Windows Mobile / CE (Device Emulator)", 0, 0},
        {"simpad_sl4", "Siemens SIMpad SL4", 800, 600},
        {"ipaq_h31xx", "Compaq iPAQ H3100", 240, 320},
        {"ipaq_h36xx", "Compaq iPAQ H3600", 240, 320},
        {"ipaq_h3660_h3670", "Compaq iPAQ H3660/H3670", 240, 320},
        {"jornada_720", "HP Jornada 720", 640, 240},
        {"asus_mypal_p525", "ASUS P525 (Windows Mobile)", 240, 320},
    };
    const bool de = strncmp(g_ed.board, "devemu", 6) == 0;
    const char* cur = kModels[0].label;
    for (const Model& m : kModels)
        if (!de && !strcmp(m.board, g_ed.board)) cur = m.label;
    ImGui::SetNextItemWidth(-1);
    if (Dropdown("##model", cur)) {
        for (const Model& m : kModels) {
            const bool sel = m.label == cur;
            if (!DropItem(m.label, sel, m.w ? nullptr : "Рекомендуется") || sel) continue;
            if (m.w) {  // a real device: its own board and screen, Guest Additions on, native size
                snprintf(g_ed.board, sizeof g_ed.board, "%s", m.board);
                g_ed.fixed_w = m.w;
                g_ed.orig_h = m.h;
                g_ed.fixed_h = m.h;
                g_ed.fill = false;
                g_ed.keep_w = true;
                g_ed.ga = true;
                g_ed.p525_keypad = false;
                P525Defaults();
                g_ed_label.clear();
                TabletDefaults();
            } else if (g_ed.rom[0]) {
                SetRom(g_ed.rom);  // back to the board the ROM asks for
            } else {
                snprintf(g_ed.board, sizeof g_ed.board, "devemu");
            }
        }
        ImGui::EndCombo();
    }
}

void DrawEditFeatures() {
    const bool was_ga = g_ed.ga;
    ImGui::Checkbox("Гостевые дополнения (сеть, общая папка)", &g_ed.ga);
    if (strncmp(g_ed.board, "devemu", 6) != 0) {  // a real handheld: with the additions any resolution and fill-screen work, without them the LCD size is fixed
        if (g_ed.ga != was_ga) {
            g_ed.fixed_h = g_ed.ga ? 0 : g_ed.orig_h;
            if (!g_ed.ga) g_ed.fill = false;
        }
    }
    if (!g_ed_orig.empty() && g_ed.ga != g_ed_orig_ga) Hint("Смена гостевых дополнений требует новой загрузки: сохранённое состояние нельзя перенести. Оно останется для прежнего значения.");
    if (g_ed.ga) Hint(("Общая папка: Documents/CEWM/" + UniqueName(CleanName(g_ed_name), g_ed_orig) + "/Storage").c_str());
    ImGui::BeginDisabled(!g_ed.ga);
    ImGui::Checkbox("Веб-прокси (https)", &g_ed.proxy);
    ImGui::EndDisabled();
    Hint("Хост сам открывает https-страницы и отдаёт их гостю по http. Укажите прокси 10.0.2.2, порт 18080 в Internet Explorer или в настройках соединения для сетевого профиля. Нужны гостевые дополнения.");
    ImGui::Checkbox("Радиомодем (сигнал, оператор, телефон)", &g_ed.radio);
    ImGui::Checkbox("Звук", &g_ed.audio);
#ifdef CEWM_HAVE_DYNARMIC  // the 32-bit ARM build has no JIT
    ImGui::Checkbox("JIT (экспериментально)", &g_ed.jit);
#endif
    ImGui::Checkbox("Сохранять состояние между запусками", &g_ed.resume);
}

// What the editor's settings add up to, computed with the layout code the guest view uses (shown above the bottom buttons on every tab).
std::string GuestInfo() {
    ApplyOrigRes(g_ed);
    std::swap(g_s, g_ed);
    const float dw = g_s.rot & 1 ? g_dispH : g_dispW, dh = g_s.rot & 1 ? g_dispW : g_dispH;
    const float n = EffScale(MakeLayout(dw, dh));
    int gw, gh;
    GuestSizeFor(dw, dh, &gw, &gh);
    std::swap(g_s, g_ed);
    char t[96];
    snprintf(t, sizeof t, "Экран гостя: %d x %d, масштаб %.3g", gw, gh, n);
    return t;
}

void DrawEditScreen() {
    const float fs = ES();
    ApplyOrigRes(g_ed);
    ImGui::TextUnformatted("Исходное разрешение ROM");
    const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
    // Common sizes (the four LCD classes of the Device Emulator board); the fields below take any other size.
    static const struct { int w, h; const char* name; } kRes[] = {
        {240, 320, "240 x 320 (QVGA)"}, {240, 400, "240 x 400 (WQVGA)"}, {480, 640, "480 x 640 (VGA)"}, {480, 800, "480 x 800 (WVGA)"}, {800, 480, "800 x 480 (CE, альбомный)"}};
    const char* cur = "Другое";
    for (auto& r : kRes) if (r.w == g_ed.fixed_w && r.h == g_ed.orig_h) cur = r.name;
    bool ch = false;
    int w = g_ed.fixed_w, h = g_ed.orig_h;
    ImGui::SetNextItemWidth(-1);
    if (Dropdown("##res", cur)) {
        for (auto& r : kRes)
            if (DropItem(r.name, r.w == g_ed.fixed_w && r.h == g_ed.orig_h)) { w = r.w; h = r.h; ch = true; }
        ImGui::EndCombo();
    }
    ImGui::SetNextItemWidth(half);
    ch |= ImGui::InputInt("##ow", &w, 0, 0);
    NoSwipe();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(half);
    ch |= ImGui::InputInt("##oh", &h, 0, 0);
    NoSwipe();
    if (ch) {
        g_ed.fixed_w = std::clamp(w, 160, 1000);
        g_ed.orig_h = std::clamp(h, 160, 1020);
        if (g_ed.fixed_h > 0) g_ed.fixed_h = g_ed.orig_h;
        else if (g_ed_label.empty()) snprintf(g_ed.board, sizeof g_ed.board, "%s", BoardFor(g_ed.fixed_w, g_ed.orig_h));
    }
    Hint("Артефакты при нестандартном разрешении исправляются своими обоями. Разрешение применяется при запуске (с гостевыми дополнениями) или после сброса ВМ.");
    ImGui::Dummy({0, fs * 0.3f});
    ImGui::TextUnformatted("Поворот экрана");
    ImGui::SetNextItemWidth(-1);
    static const char* const kRot[] = {"Нет", "90°", "180°", "270°"};
    if (Dropdown("##rot", kRot[g_ed.rot & 3])) {
        for (int i = 0; i < 4; i++)
            if (DropItem(kRot[i], g_ed.rot == i)) g_ed.rot = i;
        ImGui::EndCombo();
    }
    ImGui::Dummy({0, fs * 0.3f});
    if (ImGui::Checkbox("Исходное разрешение", &g_ed.orig_res) && !g_ed.orig_res) g_ed.fixed_h = 0;  // off: the guest height follows the screen again
    Hint("Гость работает в разрешении самого образа. Выключите, чтобы менять ширину, масштаб и заполнение экрана.");
    ImGui::BeginDisabled(g_ed.orig_res);
    ImGui::Checkbox("Не менять ширину", &g_ed.keep_w);
    Hint("Некоторые образы Windows Mobile не любят смену ширины.");
    if (!g_ed.keep_w) {
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderFloat("##n", &g_ed.n, 1.5f, 6.f, "Масштаб: экран / %.2f")) g_ed.n = roundf(g_ed.n * 4.f) / 4.f;
        NoSwipe();
    }
    ImGui::Dummy({0, fs * 0.3f});
    ImGui::Checkbox("На весь экран", &g_ed.fill);
    ImGui::EndDisabled();
    if (g_ed.fixed_h > 0 && !g_ed.orig_res) {  // a warning, not a lock: the user may still try
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.7f, 0.2f, 1));
        ImGui::TextWrapped("Внимание: этот образ рассчитан на разрешение %d x %d. Другая ширина, масштаб или заполнение экрана могут не работать.", g_ed.fixed_w, g_ed.fixed_h);
        ImGui::PopStyleColor();
    }
    ImGui::BeginDisabled(g_ed.fill);
    ImGui::Checkbox("Чёрные поля по бокам", &g_ed.borders);
    ImGui::BeginDisabled(!g_ed.borders);
    Slider("##bpx", &g_ed.border_px, 0, 160, "Поля %d px");
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    Slider("##top", &g_ed.top_px, 0, 400, "Отступ сверху %d px");
    ImGui::Dummy({0, fs * 0.3f});
    ImGui::Checkbox("Экранные кнопки", &g_ed.softkeys);
    ImGui::Checkbox("Клавиатура ASUS P525 (T9)", &g_ed.p525_keypad);
    ImGui::BeginDisabled(!g_ed.p525_keypad);
    ImGui::Checkbox("Тёмный фон клавиш T9", &g_ed.p525_keybg);
    ImGui::EndDisabled();
    Hint("Клавиши 0–9, *, #, две программные кнопки, вызов, отбой и пятипозиционный джойстик. Ввод текста обрабатывает сама Windows Mobile.");
    ImGui::BeginDisabled(!g_ed.softkeys || g_ed.fill || g_ed.p525_keypad);
    Slider("##bar", &g_ed.bar_px, 60, 300, "Высота панели %d px");
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!g_ed.softkeys || g_ed.p525_keypad);
    Slider("##bdy", &g_ed.btn_dy, -150, 150, "Сдвиг значков %d px");
    ImGui::EndDisabled();
}

// Header, tabs, content and the bottom button(s). Everything but the guest view lives in this one full-screen window.
void DrawLauncher(float w, float h) {
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize({w, h});
    ImGui::Begin("##launcher", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_NoSavedSettings);
    const float fs = ES();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {fs * 1.0f, fs * 0.6f});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {fs * 0.5f, fs * 0.6f});
    const bool edit = g_view == View::Edit;
    const char* title = edit ? (g_ed_orig.empty() ? "Создание ВМ" : "Изменение ВМ") : "CE/WM";
    ImGui::SetCursorPos({(w - ImGui::CalcTextSize(title).x) * 0.5f, fs * 0.8f});
    ImGui::TextUnformatted(title);
    static const char* const kListTabs[] = {"ВМ", "О программе"};
    static const char* const kEditTabs[] = {"Общие", "Функции", "Экран"};
    if (edit) DrawTabs(kEditTabs, 3, &g_ed_tab);
    else DrawTabs(kListTabs, 2, &g_tab);
    ImGui::Spacing();
    const float btn_h = fs * 2.6f;
    const bool list_btn = !edit && g_tab == 0 && !g_vms.empty();
    const bool bottom = edit || g_tab == 1 || list_btn;
    const float avail_h = h - ImGui::GetCursorPosY() - (bottom ? btn_h + fs * 1.5f + ImGui::GetStyle().ItemSpacing.y : fs) - (edit ? ImGui::GetTextLineHeightWithSpacing() : 0.f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));  // the theme paints children darker than the window; keep one colour
    ImGui::BeginChild("##content", {0, avail_h}, false);
    if (edit) {
        ImGui::BeginDisabled(g_save == 1);
        switch (g_ed_tab) {
            case 0: DrawEditGeneral(); break;
            case 1: DrawEditFeatures(); break;
            default: DrawEditScreen(); break;
        }
        ImGui::EndDisabled();
        SwipeScroll();
    } else if (g_tab == 0) DrawVmList();
    else DrawAbout();
    ImGui::EndChild();
    ImGui::PopStyleColor();
    const float y_btn = h - btn_h - fs * 1.5f;
    if (edit) {  // status line just above the buttons: copy progress or error, else the resulting guest screen
        ImGui::SetCursorPosY(y_btn - ImGui::GetTextLineHeightWithSpacing());
        if (g_save == 1) ImGui::TextUnformatted("Копирование ROM...");
        else if (g_save == 3) ImGui::TextColored({0.95f, 0.4f, 0.3f, 1}, "%s", g_save_err.c_str());
        else {
            const std::string info = GuestInfo();
            ImGui::SetCursorPosX((w - ImGui::CalcTextSize(info.c_str()).x) * 0.5f);
            ImGui::TextUnformatted(info.c_str());
        }
    }
    if (bottom) ImGui::SetCursorPosY(y_btn);
    if (edit) {
        const float bw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
        ImGui::BeginDisabled(g_save == 1);
        if (ImGui::Button("Назад", {bw, btn_h})) {
            g_save = 0;
            if (!g_ed_orig.empty() && IsFile(g_ed.rom)) BeginSave();  // an edited VM is saved on the way out
            else { g_view = View::List; ScanVms(); }
        }
        ImGui::SameLine();
        static Uint32 del_at = 0;
        const bool is_new = g_ed_orig.empty();
        const bool armed = del_at && SDL_GetTicks() - del_at < 4000;
        ImGui::BeginDisabled(is_new && !IsFile(g_ed.rom));
        if (ImGui::Button(is_new ? "Создать" : armed ? "Точно?" : "Удалить", {bw, btn_h})) {
            if (is_new) BeginSave();
            else if (armed) { RmRf(VmDir(g_ed_orig)); unlink(PrefPath(g_ed_orig).c_str()); del_at = 0; g_view = View::List; ScanVms(); }
            else del_at = SDL_GetTicks();
        }
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        if (g_save == 2) { g_save = 0; g_view = View::List; ScanVms(); }
    } else if (list_btn) {
        if (ImGui::Button("Создать ВМ", {-1, btn_h})) OpenEditor(-1);
    } else if (g_tab == 1) {
        if (ImGui::Button("Выход", {-1, btn_h})) g_quit = true;
    }
    ImGui::PopStyleVar(2);
    ImGui::End();
}


// Guest view: black borders + soft buttons per settings, guest integer-scaled and centered in the free area.
struct Guest {
    int w = 0, h = 0;
    GLuint tex = 0;
    std::vector<uint32_t> fb;
    Uint32 last_grab = 0;
    bool have_frame = false;
    void Alloc(int nw, int nh) {
        w = nw; h = nh;
        fb.assign(w * h, 0xff000000);
        if (!tex) {
            glGenTextures(1, &tex);
            glBindTexture(GL_TEXTURE_2D, tex);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);  // sampled by the sharp-bilinear shader
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, fb.data());
    }
    // Pull the guest's current frame (about 30 Hz) and upload it; the guest hands out BGRA, GLES2 wants RGBA.
    uint64_t last_hash = 0;
    Uint32 last_change = 0;
    void Update() {
        const Uint32 now = SDL_GetTicks();
        // 60 Hz while the picture is changing, 15 Hz once it has been still for half a second.
        if (now - last_grab < (now - last_change < 500 ? 16u : 66u) || g_booting) return;
        last_grab = now;
        std::vector<uint32_t> px;
        uint32_t gw = 0, gh = 0;
        if (!cewm::EmuGrabFrame(px, gw, gh)) return;
        if ((int)gw != w || (int)gh != h) { Alloc((int)gw, (int)gh); last_hash = 0; }
        uint64_t hsh = 1469598103934665603ull;  // unchanged frame: skip the swizzle and the GL upload
        for (uint32_t p : px) hsh = (hsh ^ p) * 1099511628211ull;
        if (have_frame && hsh == last_hash) return;
        last_change = now;
        last_hash = hsh;
        for (size_t i = 0; i < px.size(); ++i) {
            const uint32_t p = px[i];
            fb[i] = 0xff000000u | (p & 0xff00ff00u) | ((p & 0xffu) << 16) | ((p >> 16) & 0xffu);
        }
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, fb.data());
        have_frame = true;
    }
} g_guest;

// Sharp-bilinear scaler: draws the guest texture into an offscreen texture at its exact on-screen size. Texels stay crisp and only a
// 1-pixel band at each texel edge is blended, so a non-integer scale (2.5) is even instead of alternating 2/3-pixel columns.
struct Scaler {
    GLuint prog = 0, vbo = 0, fbo = 0, tex = 0;
    int w = 0, h = 0;
    GLint uSrc = -1, uTexSize = -1, uScale = -1;
    // pending job, set by DrawEmu each frame and executed right before ImGui renders
    bool job = false;
    GLuint src = 0;
    int gw = 0, gh = 0;

    static GLuint Compile(GLenum type, const char* s) {
        GLuint sh = glCreateShader(type);
        glShaderSource(sh, 1, &s, nullptr);
        glCompileShader(sh);
        return sh;
    }
    void Init() {
        const char* vs = "attribute vec2 aPos; attribute vec2 aUv; varying vec2 vUv; void main(){ vUv = aUv; gl_Position = vec4(aPos, 0.0, 1.0); }";
        const char* fs =
            "precision highp float; varying vec2 vUv; uniform sampler2D uSrc; uniform vec2 uTexSize; uniform vec2 uScale;\n"
            "void main(){\n"
            "  vec2 px = vUv * uTexSize;\n"
            "  vec2 range = 0.5 - 0.5 / uScale;\n"
            "  vec2 f = fract(px) - 0.5;\n"
            "  vec2 g = (f - clamp(f, -range, range)) * uScale + 0.5;\n"
            "  gl_FragColor = texture2D(uSrc, (floor(px) + g) / uTexSize);\n"
            "}\n";
        prog = glCreateProgram();
        glAttachShader(prog, Compile(GL_VERTEX_SHADER, vs));
        glAttachShader(prog, Compile(GL_FRAGMENT_SHADER, fs));
        glBindAttribLocation(prog, 0, "aPos");
        glBindAttribLocation(prog, 1, "aUv");
        glLinkProgram(prog);
        uSrc = glGetUniformLocation(prog, "uSrc");
        uTexSize = glGetUniformLocation(prog, "uTexSize");
        uScale = glGetUniformLocation(prog, "uScale");
        // Triangle strip; guest row 0 goes to framebuffer row 0 so ImGui::Image shows it upright.
        const float quad[] = {-1, -1, 0, 0, 1, -1, 1, 0, -1, 1, 0, 1, 1, 1, 1, 1};
        glGenBuffers(1, &vbo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
        glGenFramebuffers(1, &fbo);
        glGenTextures(1, &tex);
    }
    void Resize(int nw, int nh) {
        if (nw == w && nh == h) return;
        w = nw; h = nh;
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    }
    // Render the pending job (if any) at w x h.
    void Run(int out_w, int out_h) {
        if (!job) return;
        job = false;
        if (!prog) Init();
        GLint prev_fbo = 0, vp[4];
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
        glGetIntegerv(GL_VIEWPORT, vp);
        Resize(out_w, out_h);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, w, h);
        glDisable(GL_BLEND); glDisable(GL_SCISSOR_TEST); glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
        glUseProgram(prog);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, src);
        glUniform1i(uSrc, 0);
        glUniform2f(uTexSize, (float)gw, (float)gh);
        glUniform2f(uScale, (float)w / gw, (float)h / gh);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, (void*)0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 16, (void*)8);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glDisableVertexAttribArray(0);
        glDisableVertexAttribArray(1);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, prev_fbo);
        glViewport(vp[0], vp[1], vp[2], vp[3]);
    }
} g_scaler;

// Rotates the finished frame by 90/180/270 degrees on its way to the window (Aurora never rotates client buffers): the UI is drawn into an
// offscreen texture of the swapped size, then a quad with a rotation matrix puts it on the screen. Same matrices as the PPSSPP port.
struct Rotator {
    GLuint prog = 0, vbo = 0, fbo = 0, tex = 0;
    int w = 0, h = 0;
    GLint uRot = -1, uSrc = -1;
    void Init() {
        const char* vs = "attribute vec2 aPos; attribute vec2 aUv; uniform mat2 uRot; varying vec2 vUv; void main(){ vUv = aUv; gl_Position = vec4(uRot * aPos, 0.0, 1.0); }";
        const char* fs = "precision mediump float; varying vec2 vUv; uniform sampler2D uSrc; void main(){ gl_FragColor = texture2D(uSrc, vUv); }";
        prog = glCreateProgram();
        glAttachShader(prog, Scaler::Compile(GL_VERTEX_SHADER, vs));
        glAttachShader(prog, Scaler::Compile(GL_FRAGMENT_SHADER, fs));
        glBindAttribLocation(prog, 0, "aPos");
        glBindAttribLocation(prog, 1, "aUv");
        glLinkProgram(prog);
        uRot = glGetUniformLocation(prog, "uRot");
        uSrc = glGetUniformLocation(prog, "uSrc");
        const float quad[] = {-1, -1, 0, 0, 1, -1, 1, 0, -1, 1, 0, 1, 1, 1, 1, 1};
        glGenBuffers(1, &vbo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
        glGenFramebuffers(1, &fbo);
        glGenTextures(1, &tex);
    }
    // Binds the offscreen target at nw x nh; everything drawn until End() lands in it.
    void Begin(int nw, int nh) {
        if (!prog) Init();
        if (nw != w || nh != h) {
            w = nw; h = nh;
            glBindTexture(GL_TEXTURE_2D, tex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
            fprintf(stderr, "rotator target %dx%d status 0x%x gl error 0x%x\n", w, h, glCheckFramebufferStatus(GL_FRAMEBUFFER), glGetError());
        }
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, w, h);
    }
    // Draws the offscreen frame rotated by `rot` quarter turns (counter-clockwise, like wl_output_transform) onto the window of dw x dh.
    void End(int rot, int dw, int dh) {
        static const float kMat[4][4] = {{1, 0, 0, 1}, {0, 1, -1, 0}, {-1, 0, 0, -1}, {0, -1, 1, 0}};  // column-major
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, dw, dh);
        glDisable(GL_BLEND); glDisable(GL_SCISSOR_TEST); glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
        glUseProgram(prog);
        glUniformMatrix2fv(uRot, 1, GL_FALSE, kMat[rot & 3]);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex);
        glUniform1i(uSrc, 0);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, (void*)0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 16, (void*)8);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glDisableVertexAttribArray(0);
        glDisableVertexAttribArray(1);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }
} g_rotator;

// Tells the compositor how the buffer is turned (same enum as wl_output_transform), so its edge gestures (close the app) follow the picture.
void ApplyBufferTransform(SDL_Window* win, int rot) {
#ifdef CEWM_WAYLAND
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(win, &info) || info.subsystem != SDL_SYSWM_WAYLAND || !info.info.wl.surface) return;
    fprintf(stderr, "buffer transform %d (wl_surface version %u)\n", rot & 3, wl_proxy_get_version((wl_proxy*)info.info.wl.surface));
    if (wl_proxy_get_version((wl_proxy*)info.info.wl.surface) >= 2) wl_surface_set_buffer_transform(info.info.wl.surface, rot & 3);
#else
    (void)win; (void)rot;
#endif
}

// Pointer in unrotated window space -> the rotated space the UI is laid out in (inverse of the Rotator's matrix).
void RotatePoint(Sint32& x, Sint32& y, int rot) {
    const float W = g_dispW, H = g_dispH, u = x / W, v = y / H;
    switch (rot & 3) {
        case 1: x = (Sint32)((1 - v) * H); y = (Sint32)(u * W); break;
        case 2: x = (Sint32)((1 - u) * W); y = (Sint32)((1 - v) * H); break;
        case 3: x = (Sint32)(v * H); y = (Sint32)((1 - u) * W); break;
        default: break;
    }
}

void DrawEmu(float W, float H) {
    const float n = EffScale(MakeLayout(W, H));
    const Layout L = MakeLayout(W, H);
    const float ax = L.inset + L.dx, aw = L.aw, ah = L.ah;
    float bar = L.bar, y_btn = L.y_btn;
    if (g_guest.w != g_gw || g_guest.h != g_gh) {
        if (!g_guest.tex || !g_guest.have_frame) g_guest.Alloc(g_gw, g_gh);
    }
    g_guest.Update();
    const float iw = std::round(g_guest.w * n), ih = std::round(g_guest.h * n);
    g_scaler.job = g_guest.tex != 0; g_scaler.src = g_guest.tex; g_scaler.gw = g_guest.w; g_scaler.gh = g_guest.h;
    g_scaler_w = (int)iw; g_scaler_h = (int)ih;

    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize({W, H});
    // ImGui clips a window's content to its rect inset by half the padding (12 px here), which cut the guest picture's edges.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::Begin("##emu", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings);
    float kx = ax, kw = aw;  // where the T9 keys go: under the plate when there is one
    ImVec2 img_pos = {ax + (aw - iw) / 2, L.top};  // pinned to the top margin; spare height ends up above the keys
    // Fill mode: the buttons take whatever is left below the picture as it is actually drawn, and sit in the middle of that strip.
    if (FillBar()) { y_btn = img_pos.y + ih; bar = std::max(0.f, H - y_btn); }
    if (L.pad > 0 && !Fill()) {  // plate hugs the picture, which sits one pad below the top margin
        img_pos.y = L.top + L.pad;
        const float bottom = img_pos.y + ih + L.pad;
        kx = img_pos.x - L.pad - L.gapk / 2; kw = iw + 2 * L.pad + L.gapk;
        ImGui::GetWindowDrawList()->AddRectFilled({img_pos.x - L.pad, img_pos.y - L.pad}, {img_pos.x + iw + L.pad, bottom}, IM_COL32(48, 50, 54, 255), 5.f);
    }
    ImGui::SetCursorPos(img_pos);
    ImGui::Image((ImTextureID)(intptr_t)(g_scaler.tex ? g_scaler.tex : g_guest.tex), {iw, ih});

    // Pointer -> stylus, in guest pixels. A press that starts on the image keeps tracking until release.
    {
        ImGuiIO& io = ImGui::GetIO();
        const ImVec2 o = ImGui::GetItemRectMin();
        static bool tracking = false;
        if (io.MouseClicked[0] && ImGui::IsItemHovered()) tracking = true;
        if (tracking) {
            const int gx = std::clamp((int)((io.MousePos.x - o.x) / n), 0, g_guest.w - 1);
            const int gy = std::clamp((int)((io.MousePos.y - o.y) / n), 0, g_guest.h - 1);
            cewm::EmuPen(io.MouseDown[0], gx, gy, g_guest.w, g_guest.h);
            if (!io.MouseDown[0]) tracking = false;
        }
    }

    // The guest is switching to another resolution (the saved machine was made at a different size): say so until the new size shows up.
    if (g_resize_watch_w > 0 && ((g_guest.w == g_resize_watch_w && g_guest.h == g_resize_watch_h) || SDL_GetTicks() - g_resize_watch_at > 30000)) g_resize_watch_w = 0;
    const bool resizing = g_resize_w > 0 || g_resize_watch_w > 0;
    if (g_booting || !g_guest.have_frame || resizing) {
        // Centred in the picture: title, the resolution note if any, an empty line, then the elapsed time.
        char t[24];
        snprintf(t, sizeof t, "%uс", (SDL_GetTicks() - g_start_ticks) / 1000);
        std::vector<const char*> lines = {g_booting ? "Запуск..." : g_guest.have_frame ? "Запуск..." : "Загрузка Windows..."};
        if (resizing) lines.push_back("Смена разрешения займет некоторое время");
        lines.push_back("");
        lines.push_back(t);
        const float lh = ImGui::GetTextLineHeightWithSpacing();
        const float cx = ax + aw / 2, top = img_pos.y + (FillBar() ? ih : ah) / 2 - lh * lines.size() * 0.5f;
        if (resizing && g_guest.have_frame && !g_booting)  // the old picture is still there: dim it so the text reads
        {
            const ImVec2 wp = ImGui::GetWindowPos(), p0 = {wp.x + img_pos.x, wp.y + img_pos.y};
            ImGui::GetWindowDrawList()->AddRectFilled(p0, {p0.x + iw, p0.y + ih}, IM_COL32(0, 0, 0, 170));
        }
        for (size_t k = 0; k < lines.size(); k++) {
            ImGui::SetCursorPos({cx - ImGui::CalcTextSize(lines[k]).x / 2, top + lh * k});
            ImGui::TextColored({0.9f, 0.9f, 0.9f, 1}, "%s", lines[k]);
        }
    }

    if (g_s.p525_keypad) {
        DrawP525Keypad(kx, y_btn, kw, bar, g_s.p525_keybg);
    } else if (Soft()) {
        // HTC HD2 order. Windows virtual keys of the devemu keyboard: F3 (Call), RWIN (Home, untested), LWIN (Start), ESC (Back), F4 (End).
        enum Icon { Call, Home, Start, Back, End, Esc, Enter };
        struct Key { const char* label; uint8_t vk; Icon icon; } keys[] = {
            {"Call", 0x72, Call}, {"Home", 0x5C, Home}, {"Start", 0x5B, Start}, {"Back", 0x1B, Back}, {"End", 0x73, End}};
        int kN = 5;
        if (!strcmp(g_s.board, "jornada_720") || !strcmp(g_s.board, "simpad_sl4")) {  // devices with no phone keys: Start, Esc and Enter (the calibration screen needs them)
            keys[0] = {"Start", 0x5B, Start}; keys[1] = {"Esc", 0x1B, Esc}; keys[2] = {"Enter", 0x0D, Enter};
            kN = 3;
        }
        const float bw = (aw - (kN - 1) * kBtnGap) / kN;
        static int held = -1;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        for (int i = 0; i < kN; i++) {
            ImGui::SetCursorPos({ax + (bw + kBtnGap) * i, y_btn});
            const ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton(keys[i].label, {bw, bar});
            const bool act = ImGui::IsItemActive();
            if (act && held != i) { held = i; cewm::EmuKey(keys[i].vk, false); cewm::HapticTick(); }
            else if (!act && held == i) {
                held = -1; cewm::EmuKey(keys[i].vk, true); }
            // Bare icons on the black: no button chrome, a thin line between neighbours.
            const ImVec2 c = {p.x + bw / 2, p.y + bar / 2 + g_s.btn_dy};
            const float r = std::min(bw, bar) * 0.21f, t = std::max(2.f, r * 0.22f);
            const int a = act ? 255 : 200;
            const ImU32 white = IM_COL32(255, 255, 255, a);
            if (i > 0) dl->AddLine({p.x - kBtnGap / 2, p.y + bar * 0.2f + g_s.btn_dy}, {p.x - kBtnGap / 2, p.y + bar * 0.8f + g_s.btn_dy}, IM_COL32(255, 255, 255, 70), 2.f);
            switch (keys[i].icon) {
            case Start: {  // Windows flag traced from the app icon: four rounded, wavy panes (red, green, blue, yellow)
                const ImU32 cols[4] = {IM_COL32(240, 90, 40, a), IM_COL32(120, 190, 50, a), IM_COL32(50, 150, 230, a), IM_COL32(250, 200, 40, a)};
                const float w = r * 1.2f;
                // The fan triangles are drawn without anti-aliasing (AA fringes on every triangle showed as noise); the outline smooths the pane edge.
                const ImDrawListFlags fl = dl->Flags;
                dl->Flags &= ~ImDrawListFlags_AntiAliasedFill;
                for (int k = 0; k < 4; k++) {
                    const ImVec2 o = {c.x + kFlagC[k][0] * w, c.y + kFlagC[k][1] * w};
                    ImVec2 ring[kFlagN];
                    for (int m = 0; m < kFlagN; m++) {
                        const float* p0 = kFlag[k][m]; const float* p1 = kFlag[k][(m + 1) % kFlagN];
                        ring[m] = {c.x + p0[0] * w, c.y + p0[1] * w};
                        dl->AddTriangleFilled(o, ring[m], {c.x + p1[0] * w, c.y + p1[1] * w}, cols[k]);
                    }
                    dl->Flags = fl;
                    dl->AddPolyline(ring, kFlagN, cols[k], ImDrawFlags_Closed, 1.f);
                    dl->Flags &= ~ImDrawListFlags_AntiAliasedFill;
                }
                dl->Flags = fl;
                break;
            }
            case Home:  // house: roof triangle over a body with a door gap
                dl->AddTriangleFilled({c.x - r * 1.1f, c.y - r * 0.1f}, {c.x + r * 1.1f, c.y - r * 0.1f}, {c.x, c.y - r * 1.0f}, white);
                dl->AddRectFilled({c.x - r * 0.75f, c.y - r * 0.1f}, {c.x + r * 0.75f, c.y + r * 0.9f}, white);
                dl->AddRectFilled({c.x - r * 0.2f, c.y + r * 0.35f}, {c.x + r * 0.2f, c.y + r * 0.9f}, IM_COL32(0, 0, 0, 255));
                break;
            case Esc: case Enter: {
                const char* tx = keys[i].icon == Esc ? "Esc" : "Enter";
                const float fsz = r * 1.3f;
                const ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(fsz, FLT_MAX, 0.f, tx);
                dl->AddText(ImGui::GetFont(), fsz, {c.x - ts.x / 2, c.y - ts.y / 2}, white, tx);
                break;
            }
            case Back:  // left arrow
                dl->AddLine({c.x + r, c.y}, {c.x - r, c.y}, white, t);
                dl->AddLine({c.x - r, c.y}, {c.x - r * 0.1f, c.y - r * 0.9f}, white, t);
                dl->AddLine({c.x - r, c.y}, {c.x - r * 0.1f, c.y + r * 0.9f}, white, t);
                break;
            default: {  // handset: a thick arc with round ends; green to answer, red (flatter) to hang up
                const bool call = keys[i].icon == Call;
                const ImU32 col = call ? IM_COL32(60, 200, 80, a) : IM_COL32(230, 60, 50, a);
                const float a0 = 3.14159f * (call ? 0.95f : 1.2f), a1 = a0 + 3.14159f * 0.6f;
                const ImVec2 cc = {c.x, c.y + (call ? r * 0.7f : r * 0.85f)};  // the arc's own box sits above its centre, so push it down to centre the glyph
                dl->PathArcTo(cc, r * 1.2f, a0, a1, 24);
                dl->PathStroke(col, 0, r * 0.5f);
                for (float ang : {a0, a1}) dl->AddCircleFilled({cc.x + cosf(ang) * r * 1.2f, cc.y + sinf(ang) * r * 1.2f}, r * 0.3f, col);
            }
            }
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
}

}  // namespace

// Command line (overrides saved settings; nothing here is saved):
//   --rom PATH        ROM image; implies starting straight into the guest, skipping the launcher
//   --scale N         guest = display / N (3 or 4)
//   --borders PX | --no-borders      --softkeys | --no-softkeys
//   --shot FILE.ppm   write the guest frame to FILE.ppm once it is up, then exit (debugging)
//   --shot-ui FILE.ppm  same, but the composed screen (guest + soft buttons), for layout debugging
//   --shot-after SEC  wait this long after the first frame before --shot (default 0)
//   --new | --edit NAME | --tab N   open the VM editor / a tab (for --shot-ui of the launcher)
//   --create ROM | --run NAME   create a VM from a ROM file / start a VM straight away (tests)
//   --hires   with --shot-ui on the laptop: write the full 1080x2400 picture instead of the 360x800 window
//   --board B | --fixed-h N | --ga | --rot DEG   board variant, exact guest height, Guest Additions for a --rom run
struct Cli { bool hires = false; bool autostart = false; const char* shot = nullptr; const char* uishot = nullptr; int shot_after = 0; bool open_new = false; const char* open_edit = nullptr; int tab = -1; const char* create = nullptr; const char* run = nullptr; const char* rename = nullptr; const char* setrom = nullptr; const char* browse = nullptr; int catalog = -2; int click_x = -1, click_y = -1; std::vector<std::pair<int, int>> clicks; } g_cli;

void ParseCli(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        auto val = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--rom") { snprintf(g_s.rom, sizeof g_s.rom, "%s", val()); g_cli.autostart = true; }
        else if (a == "--scale") { g_s.n = std::clamp((float)atof(val()), 1.f, 8.f); g_s.fixed_w = 0; }
        else if (a == "--borders") { g_s.borders = true; g_s.border_px = atoi(val()); }
        else if (a == "--no-borders") g_s.borders = false;
        else if (a == "--width") g_s.fixed_w = std::clamp(atoi(val()), 0, 1000);
        else if (a == "--top") g_s.top_px = std::clamp(atoi(val()), 0, 400);
        else if (a == "--softkeys") g_s.softkeys = true;
        else if (a == "--p525-keypad") g_s.p525_keypad = true;
        else if (a == "--p525-keybg") g_s.p525_keybg = true;
        else if (a == "--no-softkeys") g_s.softkeys = false;
        else if (a == "--board") snprintf(g_s.board, sizeof g_s.board, "%s", val());
        else if (a == "--win") {
#ifndef CEWM_ON_PHONE
            int ww = 0, wh = 0;
            if (sscanf(val(), "%dx%d", &ww, &wh) == 2 && ww >= 200 && wh >= 200) { kVirtW = ww * kVirtScale; kVirtH = wh * kVirtScale; g_win_forced = true; }
#else
            (void)val();
#endif
        }
        else if (a == "--fixed-h") g_s.fixed_h = std::clamp(atoi(val()), 0, 1020);
        else if (a == "--ga") g_s.ga = true;
        else if (a == "--jit") g_s.jit = true;
        else if (a == "--no-resume") g_s.resume = false;
        else if (a == "--rot") g_s.rot = std::clamp(atoi(val()) / 90, 0, 3);
        else if (a == "--new") g_cli.open_new = true;
        else if (a == "--edit") g_cli.open_edit = val();
        else if (a == "--tab") g_cli.tab = atoi(val());
        else if (a == "--hires") g_cli.hires = true;
        else if (a == "--browse") g_cli.browse = val();
        else if (a == "--catalog") g_cli.catalog = atoi(val());  // test hook: open the download catalog, -1 = the device types, n = that type
        else if (a == "--click") sscanf(val(), "%d,%d", &g_cli.click_x, &g_cli.click_y);
        else if (a == "--clicks") {  // "x,y;x,y;..." taps in order, 20 frames apart
            for (std::string all = val(); !all.empty();) {
                int x, y;
                if (sscanf(all.c_str(), "%d,%d", &x, &y) == 2) g_cli.clicks.push_back({x, y});
                const size_t p = all.find(';');
                all = p == std::string::npos ? "" : all.substr(p + 1);
            }
        }
        else if (a == "--create") g_cli.create = val();
        else if (a == "--run") g_cli.run = val();
        else if (a == "--rename") g_cli.rename = val();
        else if (a == "--setrom") g_cli.setrom = val();
        else if (a == "--shot") g_cli.shot = val();
        else if (a == "--shot-ui") g_cli.uishot = val();
        else if (a == "--shot-after") g_cli.shot_after = atoi(val());
        else fprintf(stderr, "cewm: unknown argument %s\n", a.c_str());
    }
}

// Saves the running machine (off the UI thread), at most every 60 s; called when the app goes to the background.
void AutoSave(bool now) {
    static Uint32 last = 0;
    static std::atomic<bool> busy{false};
    if (g_mode != Mode::Emu || !g_s.resume || !g_guest.have_frame || busy) return;
    const Uint32 t = SDL_GetTicks();
    if (!now && t - last < 60000) return;
    last = t;
    busy = true;
    std::thread([] { cewm::EmuSnapshot(); busy = false; }).detach();
}
int main(int argc, char** argv) {
#ifdef CEWM_ON_PHONE
    setenv("CEWM_DATA_DIR", "/usr/share/ru.erhoof.cewm/runtime", 0);  // 0: do not override a developer's own dir
#endif
    if (!getenv("CEWM_STDERR")) {  // no terminal on the phone: the log of the last run goes next to the settings (cewm.log), unbuffered so a crash keeps it
        char lp[600];
        snprintf(lp, sizeof lp, "%s/cewm.log", ConfDir().c_str());
        rename(lp, (std::string(lp) + ".prev").c_str());  // a crash is followed by a relaunch: keep the crashed run's log
        if (freopen(lp, "w", stderr)) setvbuf(stderr, nullptr, _IONBF, 0);
    }
    {  // debug switch for a device without a terminal: an empty file "debug" next to the settings adds the guest PC (every 2 s) to cewm.log
        const std::string dbg = ConfDir() + "/debug";
        if (access(dbg.c_str(), F_OK) == 0) setenv("CEWM_PCLOG", "2000", 0);
    }
    SDL_Init(SDL_INIT_VIDEO);
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) fprintf(stderr, "audio unavailable: %s\n", SDL_GetError());  // the guest runs silent without it
    cewm::StartDeviceStats();
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_DisplayMode dm;
    SDL_GetDesktopDisplayMode(0, &dm);
    // --win sets both the SDL window and virtual display dimensions.
    ParseCli(argc, argv);
    // Phone: fullscreen at the real display size. Laptop: a "virtual phone", i.e. the Note 17's 1080x2400 shown at 1/kVirtScale in a window,
    // so guest size, borders and soft-button bar come out exactly as on the device.
#ifdef CEWM_ON_PHONE
    SDL_Window* win = SDL_CreateWindow("CE/WM", 0, 0, dm.w, dm.h, SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN);
#else
    SDL_Window* win = SDL_CreateWindow("CE/WM", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, kVirtW / kVirtScale,
                                       kVirtH / kVirtScale, SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI);
#endif
    g_win = win;
    if (!win) { SDL_Log("window: %s", SDL_GetError()); return 1; }
#ifndef CEWM_ON_PHONE
    int window_w = 0, window_h = 0;
    SDL_GetWindowSize(win, &window_w, &window_h);
    fprintf(stderr, "window: %dx%d, virtual display %dx%d\n", window_w, window_h, kVirtW, kVirtH);
#endif
    SDL_GLContext gl = SDL_GL_CreateContext(win);
    if (!gl) { SDL_Log("GL context: %s", SDL_GetError()); return 1; }
    SDL_GL_SetSwapInterval(1);

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // sandbox: no imgui.ini
    io.LogFilename = nullptr;
    // Font ~4.5 mm tall from the physical DPI, like the nfsmw launcher.
    float vdpi = 96.f;
    SDL_GetDisplayDPI(SDL_GetWindowDisplayIndex(win), nullptr, nullptr, &vdpi);
    if (vdpi <= 1.f) vdpi = 96.f;
#ifndef CEWM_ON_PHONE
    vdpi = 400.f;  // virtual phone: the Note 17 is ~400 dpi, the laptop panel is not
#endif
    const float font_px = std::clamp(4.5f * (vdpi / 25.4f), 14.f, 96.f);
    AuroraLauncher_ApplyTheme(font_px);
    ImGui::GetStyle().ScaleAllSizes(0.8f);
    static const ImWchar ranges[] = {0x0020, 0x00FF, 0x0400, 0x04FF, 0x2010, 0x205E, 0};
    if (!io.Fonts->AddFontFromMemoryCompressedTTF(NotoSansBold_compressed_data, NotoSansBold_compressed_size, font_px,
                                                  nullptr, ranges))
        io.FontGlobalScale = font_px / 13.f;
    ImGui_ImplSDL2_InitForOpenGL(win, gl);
    ImGui_ImplOpenGL3_Init("#version 100");
    ScanVms();
    if (g_cli.create) {
        OpenEditor(-1);
        SetRom(g_cli.create);
        BeginSave();
        while (g_save == 1) SDL_Delay(20);
        fprintf(stderr, "create: %s\n", g_save == 2 ? "ok" : g_save_err.c_str());
        g_save = 0;
        g_view = View::List;
        ScanVms();
    }
    if (g_cli.open_new) OpenEditor(-1);
    for (int i = 0; g_cli.open_edit && i < (int)g_vms.size(); i++) if (g_vms[i].name == g_cli.open_edit) OpenEditor(i);
    if (g_cli.open_edit && g_view == View::Edit && (g_cli.rename || g_cli.setrom)) {  // test hook: edit and save without clicking
        if (g_cli.rename) snprintf(g_ed_name, sizeof g_ed_name, "%s", g_cli.rename);
        if (g_cli.setrom) SetRom(g_cli.setrom);
        BeginSave();
        while (g_save == 1) SDL_Delay(20);
        fprintf(stderr, "save: %s\n", g_save == 2 ? "ok" : g_save_err.c_str());
        g_save = 0;
        g_view = View::List;
        ScanVms();
    }
    if (g_cli.catalog > -2) { OpenEditor(-1); g_cat_group = g_cli.catalog; g_view = View::Catalog; }
    if (g_cli.browse) { OpenEditor(-1); g_bdir = g_cli.browse; ReadBrowseDir(); g_view = View::Browse; }  // test hook
    if (g_cli.tab >= 0) g_tab = g_ed_tab = g_cli.tab;
    io.FontGlobalScale = 0.75f;  // the whole UI font runs at 3/4 of the physical-size default

    while (!g_quit) {
        for (SDL_Event e; SDL_PollEvent(&e);) {
#ifndef CEWM_ON_PHONE
            if (e.type == SDL_MOUSEMOTION) { e.motion.x *= kVirtScale; e.motion.y *= kVirtScale; }
            if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) { e.button.x *= kVirtScale; e.button.y *= kVirtScale; }
#endif
            if (g_mode == Mode::Emu && g_s.rot && g_dispW > 0) {
                if (e.type == SDL_MOUSEBUTTONDOWN) {  // which space do the compositor's touches come in? (the tablets disagree with the phone)
                    int ww = 0, wh = 0, dw = 0, dh = 0;
                    SDL_GetWindowSize(win, &ww, &wh);
                    SDL_GL_GetDrawableSize(win, &dw, &dh);
                    fprintf(stderr, "touch raw %d,%d which=%u window %dx%d drawable %dx%d display %.0fx%.0f rot %d\n", e.button.x, e.button.y, e.button.which, ww, wh, dw, dh, g_dispW, g_dispH, g_s.rot);
                }
                if (e.type == SDL_MOUSEMOTION) RotatePoint(e.motion.x, e.motion.y, g_s.rot);
                else if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) RotatePoint(e.button.x, e.button.y, g_s.rot);
            }
            if (e.type == SDL_FINGERDOWN || e.type == SDL_FINGERMOTION || e.type == SDL_FINGERUP) {  // multi-touch for the T9 keys (the mouse only follows one finger)
                Sint32 fx = (Sint32)(e.tfinger.x * g_dispW), fy = (Sint32)(e.tfinger.y * g_dispH);
                if (g_mode == Mode::Emu && g_s.rot && g_dispW > 0) RotatePoint(fx, fy, g_s.rot);
                const long long id = (long long)e.tfinger.fingerId;
                auto it = std::find_if(g_p525_fingers.begin(), g_p525_fingers.end(), [&](auto& f) { return f.first == id; });
                if (e.type == SDL_FINGERUP) { if (it != g_p525_fingers.end()) g_p525_fingers.erase(it); }
                else if (it != g_p525_fingers.end()) it->second = {(float)fx, (float)fy};
                else g_p525_fingers.push_back({id, {(float)fx, (float)fy}});
            }
            ImGui_ImplSDL2_ProcessEvent(&e);
            if (e.type == SDL_QUIT) g_quit = true;
            // Aurora kills a closed app outright, so save whenever it leaves the foreground.
            if (e.type == SDL_APP_WILLENTERBACKGROUND || (e.type == SDL_WINDOWEVENT && (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST || e.window.event == SDL_WINDOWEVENT_MINIMIZED))) AutoSave(true);
            // No on-screen back button: Esc on a keyboard returns to the launcher.
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE && g_mode == Mode::Emu) { StopEmu(g_guest.have_frame); g_guest.have_frame = false; g_mode = Mode::Launcher; g_vm.clear(); ScanVms(); }
        }
        if (!g_cli.clicks.empty()) {  // test hook: scripted taps
            static int fr = 0;
            static Uint32 t0 = 0;
            const char* da = getenv("CEWM_CLICKS_AFTER");  // seconds after the guest's first frame (in the guest view); the launcher taps start at once
            if (g_mode == Mode::Emu && !(g_guest.have_frame && (t0 || (t0 = SDL_GetTicks()))) ) goto no_scripted_tap;
            if (g_mode == Mode::Emu && SDL_GetTicks() - t0 < (Uint32)(da ? atoi(da) : 0) * 1000u) goto no_scripted_tap;
            fr++;
            const int i = (fr - 8) / 20, ph = (fr - 8) % 20;
            if (fr >= 8 && i < (int)g_cli.clicks.size()) {
                io.AddMousePosEvent((float)g_cli.clicks[i].first, (float)g_cli.clicks[i].second);
                if (ph == 3) io.AddMouseButtonEvent(0, true);
                if (ph == 6) io.AddMouseButtonEvent(0, false);
            }
        }
        no_scripted_tap:;
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
#ifndef CEWM_ON_PHONE
        io.DisplaySize = {(float)kVirtW, (float)kVirtH};
        const float fbs = g_cli.hires ? 1.0f : 1.0f / kVirtScale;  // --hires: render at the real phone resolution
        io.DisplayFramebufferScale = {fbs, fbs};
#endif
        g_dispW = io.DisplaySize.x; g_dispH = io.DisplaySize.y;  // the physical screen
        const int rot = g_mode == Mode::Emu ? g_s.rot : 0;
        if (rot & 1) std::swap(io.DisplaySize.x, io.DisplaySize.y);  // the UI is laid out in the rotated space
        if (g_cli.click_x >= 0) {  // test hook: one tap at (x,y) of the UI space, on frames 8-10
            static int fr = 0;
            fr++;
            if (fr >= 8 && fr <= 16) io.AddMousePosEvent((float)g_cli.click_x, (float)g_cli.click_y);  // keep it there: the backend may reset it
            if (fr == 10) io.AddMouseButtonEvent(0, true);
            if (fr == 12) io.AddMouseButtonEvent(0, false);
        }
        ImGui::NewFrame();
        cewm::PollWidgets();
        if (g_resize_w > 0 && g_mode == Mode::Emu && g_guest.have_frame && !g_booting) {  // restored machine at an old size: switch it to the current one
            if (!g_resize_at) g_resize_at = SDL_GetTicks();
            else if (SDL_GetTicks() - g_resize_at > 2500) {
                if (cewm::EmuResize(g_resize_w, g_resize_h)) {
                    g_gw = g_resize_w; g_gh = g_resize_h;
                    g_resize_watch_w = g_resize_w; g_resize_watch_h = g_resize_h; g_resize_watch_at = SDL_GetTicks();
                    cewm::Host().state_path = StatePath(StateKey(g_s.rom), g_gw, g_gh);  // the next save is named after the new size
                }
                g_resize_w = g_resize_h = 0;
            }
        }
        if (g_cli.run) {
            for (int i = 0; i < (int)g_vms.size(); i++) if (g_vms[i].name == g_cli.run) RunVm(i);
            g_cli.run = nullptr;
        }
        if (g_cli.autostart) { g_cli.autostart = false; StartEmu(); g_mode = Mode::Emu; }
        if (const char* rs = getenv("CEWM_RESIZE")) {  // test hook: "secs,w,h" asks the guest to resize that long after its first frame
            static Uint32 t0 = 0;
            static bool sent = false;
            int secs = 0, rw = 0, rh = 0;
            if (g_guest.have_frame && !t0) t0 = SDL_GetTicks();
            if (t0 && !sent && sscanf(rs, "%d,%d,%d", &secs, &rw, &rh) == 3 && SDL_GetTicks() - t0 >= (Uint32)secs * 1000u) { sent = true; fprintf(stderr, "resize %dx%d: %d\n", rw, rh, (int)cewm::EmuResize(rw, rh)); }
        }
        auto write_guest_shot = [&] {
            if (FILE* f = fopen(g_cli.shot, "wb")) {
                fprintf(f, "P6 %d %d 255\n", g_guest.w, g_guest.h);
                for (uint32_t p : g_guest.fb) { fputc(p & 0xff, f); fputc((p >> 8) & 0xff, f); fputc((p >> 16) & 0xff, f); }
                fclose(f);
            }
            g_cli.shot = nullptr;
        };
        if (g_cli.shot && g_guest.have_frame && !g_cli.uishot) {  // (with --shot-ui too, that block writes this one as well)
            static Uint32 first = SDL_GetTicks();
            if (SDL_GetTicks() - first >= (Uint32)g_cli.shot_after * 1000u) { write_guest_shot(); g_quit = true; }
        }
        const Mode before = g_mode;
        if (g_mode == Mode::Launcher && g_view == View::Browse) DrawBrowser(io.DisplaySize.x, io.DisplaySize.y);
        else if (g_mode == Mode::Launcher && g_view == View::Catalog) DrawCatalog(io.DisplaySize.x, io.DisplaySize.y);
        else if (g_mode == Mode::Launcher) DrawLauncher(io.DisplaySize.x, io.DisplaySize.y);
        else DrawEmu(io.DisplaySize.x, io.DisplaySize.y);
        ImGui::Render();
        int w, h;
        SDL_GL_GetDrawableSize(win, &w, &h);
#ifndef CEWM_ON_PHONE
        const bool hires = g_cli.hires && !rot;  // --hires --shot-ui: draw into an offscreen frame of the real 1080x2400 and read that
        if (hires) { w = kVirtW; h = kVirtH; }
#else
        const bool hires = false;
#endif
        const int fw = rot & 1 ? h : w, fh = rot & 1 ? w : h;  // the offscreen frame has the swapped size for 90/270
        if (rot || hires) g_rotator.Begin(fw, fh);
        else glViewport(0, 0, w, h);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        g_scaler.Run(g_scaler_w, g_scaler_h);
        glViewport(0, 0, fw, fh);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        if (rot) g_rotator.End(rot, w, h);
        static int applied_rot = -1;
        if (rot != applied_rot) { ApplyBufferTransform(win, rot); applied_rot = rot; }
        static int launcher_frames = 0;
        if (g_cli.uishot && (g_guest.have_frame || (g_mode == Mode::Launcher && ++launcher_frames > 20 + 20 * (int)g_cli.clicks.size()))) {
            static Uint32 first = SDL_GetTicks();
            if (SDL_GetTicks() - first >= (Uint32)g_cli.shot_after * 1000u) {
                std::vector<uint8_t> px((size_t)w * h * 4);
                glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
                if (g_cli.shot) write_guest_shot();
                if (FILE* f = fopen(g_cli.uishot, "wb")) {
                    fprintf(f, "P6 %d %d 255\n", w, h);
                    for (int y = h - 1; y >= 0; y--)
                        for (int x = 0; x < w; x++) fwrite(&px[((size_t)y * w + x) * 4], 1, 3, f);
                    fclose(f);
                }
                g_quit = true;
            }
        }
        SDL_GL_SwapWindow(win);
    }
    StopEmu(g_guest.have_frame);
    return 0;
}
