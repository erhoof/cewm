// SHA-256 of a ROM file and the table of known images: which Device Emulator board (LCD class) and screen size each one wants.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace romdb {

struct Known {
    const char* sha;
    const char* label;
    const char* board;
    int w, h;
    bool exact;  // the ROM only runs at exactly w x h (6.5); otherwise only the width is fixed and any height works
};

inline const Known kKnown[] = {
    {"7e5476179912538d9ce1bd4e03fadb966e4e4e3f5cf7213e98c69b487376d840", "Windows Mobile 5.0", "devemu_wqvga", 240, 400, false},
    {"a63fc18bc78ad2dcef33252eaf501e681214139b40cda5876dc77a7e754851f6", "Windows Mobile 6.0", "devemu_wvga", 480, 800, false},
    {"6c324827eeb03cba950a7468328c6e816d2d6ae6689bc737a17e53e008981d62", "Windows Mobile 6.1 (240x400)", "devemu_wqvga", 240, 400, false},
    {"6b25b8180928c2083fa02347642a0726198c1ee02859c1dadb126f5ef8a0e787", "Windows Mobile 6.1 (480x800)", "devemu_wvga", 480, 800, false},
    {"c48e280fe8bedd7d78454903de59f07c920cefe3da0418f16493aab14f024848", "Windows Mobile 6.5 (240x400)", "devemu_wqvga", 240, 400, true},
    {"9ec1cc0b7558d9ac5ea34012a2f3bd258f48a178085de29eb581fc6922465a5f", "Windows Mobile 6.5 (480x800)", "devemu_wvga", 480, 800, true},
    {"75d45b3166d4420f5f5b712e6771e90154d9c162843866b48d1c764fc65920a5", "Windows Mobile 6.1 (QVGA)", "devemu", 240, 320, false},
    {"6658d05c26a2f000fd37762f12633c078f033004e9e0ec1c8388b7278d7d9799", "Windows Mobile 6.1 (VGA)", "devemu_vga", 480, 640, false},
    {"6f5c20c5e41b79a7d95b817c476c9fad57932d8198b33f0cd116614dd31313dd", "Windows Mobile 6.0 (русский, QVGA)", "devemu", 240, 320, false},
    {"07ec007f4b642a36761269a87c6f984e5bf685bdd1aee252af2bf091ab593140", "Windows Mobile 6.0 (русский, VGA)", "devemu_vga", 480, 640, false},
    {"f31f1f9d359c39044543d047cf24fbae68a682fb4f95246c567e3b4a9041b67c", "Windows Mobile 6.0 Phone (русский)", "devemu", 240, 320, false},
    {"5f7f7a196c59777bfe22818c0bc247562d4a892cca193b98c4870d9d2dabbf98", "Windows Mobile 6.0 Pocket PC (русский)", "devemu", 240, 320, false},
    {"78e652786accc1c8919764d100ce81f22de789fe8731f71e265c2f6be13200eb", "Windows Mobile 6.0 (русский, 240x240)", "devemu", 240, 240, false},
    {"846331ca49968e0a152b461b5d6ca14f0b40ec87dbf85ad77fd7d80b29c62680", "Windows Mobile 6.0 (русский, 480x480)", "devemu_vga", 480, 480, false},
    {"e1e4abc97d507eb513adeebc88ce6a556dc346e48eb8de090587194fbc64f98d", "Windows Mobile 6.1 Phone (QVGA)", "devemu", 240, 320, false},
    {"58f4a7791267c5132779328483616880134ba29fd6128c4342c30925ffa0148b", "Windows Mobile 6.1 Pocket PC (QVGA)", "devemu", 240, 320, false},
    {"fe6a3b08ae4797121beb54a4381e98c59dad07f8ac334ee4373bc5bb2786ae60", "Windows Mobile 6.1 (240x240)", "devemu", 240, 240, false},
    {"aae0c07d7a94a629f58aef073cd7d2fa37cc04fd477be8347898a502de3d48f4", "Windows Mobile 6.1 (480x480)", "devemu_vga", 480, 480, false},
    {"4f78c4ad80b8534bf22d08dbd5d400129962d1a99b436895197a66a40ed7b429", "Windows CE 5.0 (русский, 800x480)", "devemu_wvga", 800, 480, true},
    {"9596da16dcf8ae77e0f957370c332f1ce2ecc3894c29bd2fddaa23e5a124d668", "Windows Embedded CE 6.0 (русский, 800x480)", "devemu_wvga", 800, 480, true},
    {"0385d9dca9f6faea1d4d5d3a19f9b4fd70098fcfd9612a969793735bda8e59b5", "Windows CE .NET 4.1 (SIMpad SL4)", "simpad_sl4", 800, 600, true},
    {"f9cda1f75576a0cd2696044a36d3eec1e32090fabbf369171f3a822093bf2046", "Handheld PC 2000 (SIMpad SL4)", "simpad_sl4", 800, 600, true},
    {"08921ed68730ee284f581668a07834ddf40606229627a75c496300a07a951063", "Pocket PC 2000 (iPAQ H3100)", "ipaq_h31xx", 240, 320, true},
    {"091dbde74c7678c32c8fd69c3004424c08be4068263e5e94690c7c9c039deebc", "Pocket PC 2002 (iPAQ H3100)", "ipaq_h31xx", 240, 320, true},
    {"f7cb7bef6d00098f5ff99d690e46a70d31994a3b285aa3453532218260cde5e4", "Pocket PC 2000 (iPAQ H3600)", "ipaq_h36xx", 240, 320, true},
    {"257c53a78c73def54b0a24c743d4bc3f0ff9a2fdb4d835250be2e71eb27ffc08", "Pocket PC 2002 (iPAQ H3600)", "ipaq_h36xx", 240, 320, true},
    {"bbad3911d7e5aa72d4fdb4f12fd074bf7223b638863719374762eb01f903c4bd", "Handheld PC 2000 (Jornada 720)", "jornada_720", 640, 240, true},
};

inline const Known* Find(const std::string& sha) {
    for (const Known& k : kKnown)
        if (sha == k.sha) return &k;
    return nullptr;
}

namespace detail {
inline uint32_t Ror(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
inline void Block(uint32_t h[8], const uint8_t* p) {
    static const uint32_t K[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be,
        0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa,
        0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85,
        0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
        0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f,
        0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    uint32_t w[64];
    for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[4 * i] << 24 | p[4 * i + 1] << 16 | p[4 * i + 2] << 8 | p[4 * i + 3];
    for (int i = 16; i < 64; i++) {
        const uint32_t s0 = Ror(w[i - 15], 7) ^ Ror(w[i - 15], 18) ^ (w[i - 15] >> 3), s1 = Ror(w[i - 2], 17) ^ Ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; i++) {
        const uint32_t t1 = hh + (Ror(e, 6) ^ Ror(e, 11) ^ Ror(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
        const uint32_t t2 = (Ror(a, 2) ^ Ror(a, 13) ^ Ror(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}
}  // namespace detail

// Hex digest of `n` bytes read from `f`; empty on a read error.
inline std::string Sha256Stream(FILE* f) {
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::vector<uint8_t> chunk(1 << 16);  // heap, not a big thread_local: libhybris keeps the GPU driver's TLS slots inside glibc's TLS, and our extra TLS block crashed it
    uint8_t* const buf = chunk.data();
    const size_t kChunk = chunk.size();
    uint64_t total = 0;
    size_t have = 0;
    for (;;) {
        const size_t n = fread(buf + have, 1, kChunk - have, f);
        if (n == 0 && ferror(f)) return "";
        have += n;
        total += n;
        size_t used = 0;
        for (; have - used >= 64; used += 64) detail::Block(h, buf + used);
        memmove(buf, buf + used, have - used);
        have -= used;
        if (n == 0) break;
    }
    uint8_t tail[128] = {0};
    memcpy(tail, buf, have);
    tail[have] = 0x80;
    const size_t len = have < 56 ? 64 : 128;
    const uint64_t bits = total * 8;
    for (int i = 0; i < 8; i++) tail[len - 1 - i] = (uint8_t)(bits >> (8 * i));
    for (size_t o = 0; o < len; o += 64) detail::Block(h, tail + o);
    char out[65];
    for (int i = 0; i < 8; i++) snprintf(out + 8 * i, 9, "%08x", h[i]);
    return out;
}

inline std::string Sha256File(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return "";
    const std::string r = Sha256Stream(f);
    fclose(f);
    return r;
}

}  // namespace romdb
