// ASUS P525 firmware -> bootable bundle (see p525_prepare.h). Mirrors app/tools/prepare_p525.py step by step.
#include "p525_prepare.h"
#include "p525_stub.h"
#include "rom_download.h"
#include "../src/romdb.h"
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace cewm::p525 {
namespace {
using Bytes = std::vector<uint8_t>;

constexpr uint32_t kBase = 0x88588000u;
constexpr size_t kDiskStart = 0x80804;  // language word, IPL and the duplicated bootloader sectors come first
constexpr size_t kMinDisk = 64u << 20;

struct Cap {
    const char* sha;
    uint32_t power_off;   // OEMPowerOff entry (patched to `bx lr`)
    uint32_t truffs_load; // load address of the TrueFFS DLL inside the XIP
};
const Cap kCaps[] = {
    {"665cd9bdba96aa19aa2ebb3613c0d9622cef9c5926a7f6b9b847ef35a327176d", 0x8861E8FCu, 0x88814000u},  // V3.43 WWE
    {"64e0d19b8497fe318f7cf73494e8f278f04e5a7db8e7a70d56f95c21bf30b7ac", 0x8861E91Cu, 0x88814000u},  // V3.43 WWE4 Russian
    {"b5bacd72ff618ba7778805af31cee5dd89a933a9233bdb993cdebb17b1d3566b", 0x8861EB7Cu, 0x8883A000u},  // V4.010 Russian
};
// The stream-device exports of TrueFFS, as offsets in its image; the stub's symbols replace their first instructions.
struct Export { const char* name; uint32_t va; };
const Export kExports[] = {
    {"DSK_Close", 0x03DA6440}, {"DSK_Deinit", 0x03DA6520}, {"DSK_IOControl", 0x03DA6610}, {"DSK_Init", 0x03DA5DA8}, {"DSK_Open", 0x03DA639C},
    {"DSK_PowerDown", 0x03DA7874}, {"DSK_PowerUp", 0x03DA7834}, {"DSK_Read", 0x03DA78D0}, {"DSK_Seek", 0x03DA78E0}, {"DSK_Write", 0x03DA78D8}, {"DllEntry", 0x03DC2D34},
};

uint32_t Rd32(const Bytes& b, size_t o) { return b[o] | b[o + 1] << 8 | b[o + 2] << 16 | (uint32_t)b[o + 3] << 24; }
uint32_t Rd16(const Bytes& b, size_t o) { return b[o] | b[o + 1] << 8; }
void Wr32(Bytes& b, size_t o, uint32_t v) { for (int i = 0; i < 4; i++) b[o + i] = (uint8_t)(v >> (8 * i)); }

bool EndsWith(const std::string& s, const char* suf) {
    const size_t n = strlen(suf);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; i++)
        if (tolower((unsigned char)s[s.size() - n + i]) != suf[i]) return false;
    return true;
}

// IMGFS XPRESS: LZ77 with nibble-shared extended lengths (the port of winmob_extract.compress.xpress_decompress).
Bytes Xpress(const uint8_t* src, size_t slen, size_t out_size) {
    Bytes dst(out_size);
    size_t si = 0, di = 0;
    long nibble = -1;
    while (si < slen && di < out_size) {
        if (si + 4 > slen) break;
        const uint32_t flags = src[si] | src[si + 1] << 8 | src[si + 2] << 16 | (uint32_t)src[si + 3] << 24;
        si += 4;
        for (int bit = 31; bit >= 0; bit--) {
            if (si >= slen || di >= out_size) break;
            if (!(flags & (1u << bit))) { dst[di++] = src[si++]; continue; }
            if (si + 2 > slen) { dst.resize(di); return dst; }
            const uint32_t val = src[si] | src[si + 1] << 8;
            si += 2;
            const size_t off = (val >> 3) + 1;
            uint32_t len = val & 7;
            if (len == 7) {
                if (nibble < 0) {
                    if (si >= slen) { dst.resize(di); return dst; }
                    nibble = (long)si;
                    len = src[si++] & 0x0F;
                } else {
                    len = src[nibble] >> 4;
                    nibble = -1;
                }
                if (len == 15) {
                    if (si >= slen) { dst.resize(di); return dst; }
                    len = src[si++];
                    if (len == 255) {
                        if (si + 2 > slen) { dst.resize(di); return dst; }
                        len = src[si] | src[si + 1] << 8;
                        si += 2;
                        if (len == 0) {
                            if (si + 4 > slen) { dst.resize(di); return dst; }
                            len = src[si] | src[si + 1] << 8 | src[si + 2] << 16 | (uint32_t)src[si + 3] << 24;
                            si += 4;
                        }
                        if (len < 22) { dst.resize(di); return dst; }
                        len -= 22;
                    }
                    len += 15;
                }
                len += 7;
            }
            len += 3;
            long from = (long)di - (long)off;
            for (uint32_t k = 0; k < len && di < out_size; k++, from++) dst[di++] = (from >= 0 && (size_t)from < di) ? dst[from] : 0;
        }
    }
    if (di < out_size) dst.resize(di);
    return dst;
}

bool ReadFile(const std::string& path, Bytes& out) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    out.resize(n > 0 ? (size_t)n : 0);
    const bool ok = n > 0 && fread(out.data(), 1, out.size(), f) == out.size();
    fclose(f);
    return ok;
}
bool WriteFile(const std::string& path, const uint8_t* p, size_t n) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = fwrite(p, 1, n, f) == n;
    return fclose(f) == 0 && ok;
}

void RmDir(const std::string& d) {
    if (DIR* dir = opendir(d.c_str())) {
        while (dirent* e = readdir(dir))
            if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) unlink((d + "/" + e->d_name).c_str());
        closedir(dir);
    }
    rmdir(d.c_str());
}
}  // namespace

bool IsFirmware(const std::string& path) { return EndsWith(path, ".zip") || EndsWith(path, ".cap"); }

bool Prepare(const std::string& path, const std::string& out, const std::function<void(int, const char*)>& status, std::string& err) {
    mkdir(out.c_str(), 0755);
    std::string cap = path;
    const Cap* kind = nullptr;
    std::string tmp;
    if (EndsWith(path, ".zip")) {
        status(2, "Распаковка архива...");
        tmp = out + "/.src";
        mkdir(tmp.c_str(), 0755);
        if (!romdl::UnzipFile(path, tmp, ".cap", err)) { RmDir(tmp); return false; }
        cap.clear();
        if (DIR* d = opendir(tmp.c_str())) {
            while (dirent* e = readdir(d)) {
                const std::string n = e->d_name;
                if (!EndsWith(n, ".cap")) continue;
                const std::string sha = romdb::Sha256File(tmp + "/" + n);
                for (const Cap& c : kCaps)
                    if (sha == c.sha) { cap = tmp + "/" + n; kind = &c; }
            }
            closedir(d);
        }
        if (cap.empty()) { RmDir(tmp); err = "В архиве нет известной прошивки ASUS P525"; return false; }
    } else {
        const std::string sha = romdb::Sha256File(path);
        for (const Cap& c : kCaps)
            if (sha == c.sha) kind = &c;
        if (!kind) { err = "Неизвестная прошивка ASUS P525"; return false; }
    }
    auto fail = [&](const char* msg) { err = msg; if (!tmp.empty()) RmDir(tmp); return false; };

    status(15, "Чтение прошивки...");
    Bytes raw;
    if (!ReadFile(cap, raw) || raw.size() < kDiskStart + 512) return fail("Не удалось прочитать прошивку");
    if (!tmp.empty()) { RmDir(tmp); tmp.clear(); }
    static const uint8_t kKey[4] = {0xDF, 0xC0, 0x7B, 0xA8};
    for (size_t i = 0; i < raw.size(); i++) raw[i] ^= kKey[i & 3];
    // The CAP is a MBR disk image; its second partition holds the XPRESS-packed OS XIP.
    Bytes disk(raw.begin() + kDiskStart, raw.end());
    Bytes().swap(raw);
    if (disk[510] != 0x55 || disk[511] != 0xAA) return fail("Нет таблицы разделов прошивки");
    const size_t pstart = (size_t)Rd32(disk, 446 + 16 + 8) * 512, pcount = (size_t)Rd32(disk, 446 + 16 + 12) * 512;
    if (pstart + pcount > disk.size() || pcount < 0x44) return fail("Повреждена таблица разделов");
    const uint8_t* packed = &disk[pstart];
    size_t data_off = Rd32(disk, pstart + 0x34);
    const uint32_t blocks = Rd32(disk, pstart + 0x38), block_size = Rd32(disk, pstart + 0x3C), magic = Rd32(disk, pstart + 0x40);
    if (magic != 0x58505253u || block_size != 4096 || 0x44 + 2ull * blocks > pcount) return fail("Неожиданный заголовок XIP");
    status(25, "Распаковка системы...");
    Bytes image;
    image.reserve((size_t)blocks * block_size);
    for (uint32_t i = 0; i < blocks; i++) {
        const size_t sz = packed[0x44 + 2 * i] | packed[0x45 + 2 * i] << 8;
        if (data_off + sz > pcount) return fail("Обрезан блок XIP");
        Bytes dec;
        if (sz == block_size) dec.assign(packed + data_off, packed + data_off + sz);
        else dec = Xpress(packed + data_off, sz, block_size);
        if (dec.size() != block_size) return fail("Обрезан блок XIP");
        image.insert(image.end(), dec.begin(), dec.end());
        data_off += sz;
    }
    if (image.size() < 0x100 || memcmp(&image[0x40], "ECEC", 4)) return fail("Неверный образ XIP");

    status(55, "Подготовка образа...");
    // Redirect only the original TrueFFS DLL entry and stream-device API to our stub.
    for (const Export& e : kExports) {
        const p525stub::Sym* s = nullptr;
        for (const auto& y : p525stub::kSyms)
            if (!strcmp(y.name, e.name)) s = &y;
        const size_t off = (size_t)kind->truffs_load - kBase + e.va - 0x03DA1000u;
        if (!s || off + 8 > image.size()) return fail("Неожиданный образ TrueFFS");
        Wr32(image, off, 0xE51FF004u);
        Wr32(image, off + 4, s->addr);
    }
    const size_t romhdr = Rd32(image, 0x44) - kBase;
    if (romhdr + 84 + 3 * 32 > image.size() || Rd32(image, romhdr) != 0x01FA01FEu) return fail("Неожиданный образ XIP");
    Wr32(image, romhdr, 0x01FA01E1u);  // the bootloader reserves writable DLL slots for IMGFS as well as XIP
    // Reuse the unused OSAXS debugger module as an XIP display carrier (the board swaps in the native LCD driver or the Guest Additions one).
    const size_t toc = romhdr + 84 + 2 * 32;
    const size_t name_off = Rd32(image, toc + 16) - kBase, e32 = Rd32(image, toc + 20) - kBase, o32 = Rd32(image, toc + 24) - kBase;
    if (name_off + 12 > image.size() || e32 + 12 > image.size() || o32 + 20 > image.size() || memcmp(&image[name_off], "osaxst0.dll", 12)) return fail("Неожиданный носитель дисплея");
    if (Rd32(image, e32 + 8) != 0x88652000u) return fail("Неожиданный носитель дисплея");
    const size_t carrier = image.size() - 0x100;
    memset(&image[carrier], 0, 16);
    memcpy(&image[carrier], "pxa27x_lcd.dll", 15);
    Wr32(image, toc + 16, kBase + (uint32_t)carrier);
    Wr32(image, e32 + 8, 0x012A0000u);
    Wr32(image, o32 + 16, 0x012A1000u);
    Wr32(image, romhdr + 12, kBase + (uint32_t)image.size());
    // OEMPowerOff returns at once: CE still runs its power-up sequence without entering a PXA sleep.
    const size_t off = kind->power_off - kBase;
    if (off + 8 > image.size() || Rd32(image, off) != 0xE92D41F0u || Rd32(image, off + 4) != 0xE24DD014u) return fail("Неожиданный OEMPowerOff");
    Wr32(image, off, 0xE12FFF1Eu);  // bx lr
    status(70, "Запись образов...");
    if (!WriteFile(out + "/p525-bridge.nb0", image.data(), image.size())) return fail("Не удалось записать p525-bridge.nb0");
    if (!WriteFile(out + "/p525-storage.bin", p525stub::kStorage, sizeof p525stub::kStorage)) return fail("Не удалось записать p525-storage.bin");

    // Devices that are not modelled would only stall the desktop: their drivers' module names are made unloadable (an 'x' in front).
    for (const char* n : {"pcc_wlan.dll", "pxa27x_ipm.dll"}) {
        std::vector<uint8_t> enc;
        for (const char* c = n; *c; c++) { enc.push_back((uint8_t)*c); enc.push_back(0); }
        auto it = std::search(disk.begin(), disk.end(), enc.begin(), enc.end());
        if (it == disk.end()) return fail("Неожиданный IMGFS");
        *it = 'x';
    }
    disk.resize(std::max(kMinDisk, disk.size()));
    if (!WriteFile(out + "/p525-disk.bin", disk.data(), disk.size())) return fail("Не удалось записать p525-disk.bin");
    Bytes().swap(disk);

    status(92, "Создание тома...");
    const std::string root = out + "/p525-root.img";
    struct stat st;
    if (stat(root.c_str(), &st) != 0) {  // an existing volume keeps the user's data
        FILE* f = fopen(root.c_str(), "wb");
        if (!f) return fail("Не удалось создать p525-root.img");
        bool ok = ftruncate(fileno(f), (off_t)p525stub::kRootSize * 512) == 0;
        for (const auto& s : p525stub::kRootSectors) ok = ok && fseek(f, (long)s.index * 512, SEEK_SET) == 0 && fwrite(s.data, 1, 512, f) == 512;
        if (fclose(f) != 0 || !ok) return fail("Не удалось создать p525-root.img");
    }
    status(100, "Готово");
    return true;
}
}  // namespace cewm::p525
