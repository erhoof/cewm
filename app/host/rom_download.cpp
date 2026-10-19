// ROM bundle download: manifest.json -> roms/<name>/rom.zip -> SHA-256 check -> unzip. No zlib dependency: a small inflate is below.
#include "rom_download.h"
#include "../src/romdb.h"
#include <curl/curl.h>
#include <sys/stat.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <strings.h>
#include <mutex>
#include <thread>
#include <vector>

namespace cewm::romdl {
namespace {
const char kBase[] = "https://cerf-bundles.dz3n.net/cerf-bundles/";
constexpr size_t kMaxManifest = 4u << 20;
constexpr size_t kMaxUnpacked = 160u << 20;

std::mutex g_mu;
State g_st;
std::atomic<bool> g_busy{false};

void Set(int phase, int pct, const std::string& text) {
    std::lock_guard<std::mutex> lk(g_mu);
    g_st.phase = phase;
    g_st.pct = pct;
    g_st.text = text;
}

// ---- inflate (RFC 1951), after zlib's puff.c ------------------------------------------------------------------------------------
struct Bits { const uint8_t* in; size_t len, pos; uint32_t buf; int cnt; };
int GetBits(Bits& b, int need) {
    while (b.cnt < need) {
        if (b.pos >= b.len) return -1;
        b.buf |= (uint32_t)b.in[b.pos++] << b.cnt;
        b.cnt += 8;
    }
    const int v = (int)(b.buf & ((1u << need) - 1));
    b.buf >>= need;
    b.cnt -= need;
    return v;
}
struct Huff { uint16_t count[16], sym[288]; };
bool Build(Huff& h, const uint8_t* len, int n) {
    memset(h.count, 0, sizeof h.count);
    for (int i = 0; i < n; i++) h.count[len[i]]++;
    uint16_t offs[16] = {0};
    for (int l = 1; l < 15; l++) offs[l + 1] = offs[l] + h.count[l];
    for (int i = 0; i < n; i++)
        if (len[i]) h.sym[offs[len[i]]++] = (uint16_t)i;
    return true;
}
int Decode(Bits& b, const Huff& h) {
    int code = 0, first = 0, index = 0;
    for (int l = 1; l <= 15; l++) {
        const int bit = GetBits(b, 1);
        if (bit < 0) return -1;
        code |= bit;
        const int count = h.count[l];
        if (code - count < first) return h.sym[index + (code - first)];
        index += count;
        first = (first + count) << 1;
        code <<= 1;
    }
    return -1;
}
bool Codes(Bits& b, std::vector<uint8_t>& out, size_t& o, const Huff& lit, const Huff& dist) {
    static const uint16_t lb[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
    static const uint16_t le[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    static const uint16_t db[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
    static const uint16_t de[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
    for (;;) {
        int s = Decode(b, lit);
        if (s < 0) return false;
        if (s < 256) {
            if (o >= out.size()) return false;
            out[o++] = (uint8_t)s;
        } else if (s == 256) {
            return true;
        } else {
            s -= 257;
            if (s >= 29) return false;
            const int eb = GetBits(b, le[s]);
            if (eb < 0) return false;
            const size_t len = lb[s] + eb;
            const int ds = Decode(b, dist);
            if (ds < 0 || ds >= 30) return false;
            const int db_ = GetBits(b, de[ds]);
            if (db_ < 0) return false;
            const size_t d = db[ds] + db_;
            if (d > o || o + len > out.size()) return false;
            for (size_t i = 0; i < len; i++, o++) out[o] = out[o - d];
        }
    }
}
bool Inflate(const uint8_t* in, size_t n, std::vector<uint8_t>& out) {
    Bits b{in, n, 0, 0, 0};
    size_t o = 0;
    for (int last = 0; !last;) {
        last = GetBits(b, 1);
        const int type = GetBits(b, 2);
        if (last < 0 || type < 0) return false;
        if (type == 0) {
            b.buf = 0;
            b.cnt = 0;
            if (b.pos + 4 > n) return false;
            const size_t len = in[b.pos] | in[b.pos + 1] << 8;
            if ((len ^ 0xFFFF) != (size_t)(in[b.pos + 2] | in[b.pos + 3] << 8)) return false;
            b.pos += 4;
            if (b.pos + len > n || o + len > out.size()) return false;
            memcpy(&out[o], in + b.pos, len);
            b.pos += len;
            o += len;
        } else if (type == 1) {
            uint8_t l[320];
            int i = 0;
            for (; i < 144; i++) l[i] = 8;
            for (; i < 256; i++) l[i] = 9;
            for (; i < 280; i++) l[i] = 7;
            for (; i < 288; i++) l[i] = 8;
            for (i = 0; i < 30; i++) l[288 + i] = 5;
            Huff lit, dist;
            Build(lit, l, 288);
            Build(dist, l + 288, 30);
            if (!Codes(b, out, o, lit, dist)) return false;
        } else if (type == 2) {
            static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
            const int nlen = GetBits(b, 5) + 257, ndist = GetBits(b, 5) + 1, ncode = GetBits(b, 4) + 4;
            if (nlen < 257 || ndist < 1 || ncode < 4 || nlen > 286 || ndist > 30) return false;
            uint8_t l[320] = {0};
            for (int i = 0; i < ncode; i++) {
                const int v = GetBits(b, 3);
                if (v < 0) return false;
                l[order[i]] = (uint8_t)v;
            }
            Huff lc;
            Build(lc, l, 19);
            uint8_t ll[320] = {0};
            for (int i = 0; i < nlen + ndist;) {
                const int s = Decode(b, lc);
                if (s < 0) return false;
                if (s < 16) { ll[i++] = (uint8_t)s; continue; }
                int rep, v = 0;
                if (s == 16) {
                    if (i == 0) return false;
                    v = ll[i - 1];
                    rep = 3 + GetBits(b, 2);
                } else if (s == 17) rep = 3 + GetBits(b, 3);
                else rep = 11 + GetBits(b, 7);
                if (rep < 3 || i + rep > nlen + ndist) return false;
                while (rep--) ll[i++] = (uint8_t)v;
            }
            Huff lit, dist;
            Build(lit, ll, nlen);
            Build(dist, ll + nlen, ndist);
            if (!Codes(b, out, o, lit, dist)) return false;
        } else {
            return false;
        }
    }
    return o == out.size();
}

// ---- zip ------------------------------------------------------------------------------------------------------------------------
uint32_t R16(const uint8_t* p) { return p[0] | p[1] << 8; }
uint32_t R32(const uint8_t* p) { return R16(p) | R16(p + 2) << 16; }

// Unpacks every file of the archive into dir (flat: only the base name is used, so a hostile archive cannot write elsewhere).
bool Unzip(const std::vector<uint8_t>& z, const std::string& dir, std::string& err, const char* only_ext = nullptr) {
    if (z.size() < 22) { err = "архив повреждён"; return false; }
    size_t e = z.size() - 22;
    while (e > 0 && R32(&z[e]) != 0x06054b50) e--;
    if (R32(&z[e]) != 0x06054b50) { err = "архив повреждён"; return false; }
    const uint32_t count = R16(&z[e + 10]);
    size_t p = R32(&z[e + 16]);
    for (uint32_t i = 0; i < count; i++) {
        if (p + 46 > z.size() || R32(&z[p]) != 0x02014b50) { err = "архив повреждён"; return false; }
        const uint32_t method = R16(&z[p + 10]), csize = R32(&z[p + 20]), usize = R32(&z[p + 24]);
        const size_t nlen = R16(&z[p + 28]), elen = R16(&z[p + 30]), clen = R16(&z[p + 32]), loff = R32(&z[p + 42]);
        if (p + 46 + nlen > z.size()) { err = "архив повреждён"; return false; }
        std::string name((const char*)&z[p + 46], nlen);
        p += 46 + nlen + elen + clen;
        const size_t sl = name.find_last_of('/');
        if (sl != std::string::npos) name = name.substr(sl + 1);
        if (name.empty() || name == "." || name == "..") continue;  // a folder entry
        if (only_ext && (name.size() < strlen(only_ext) || strcasecmp(name.c_str() + name.size() - strlen(only_ext), only_ext))) continue;
        if (usize > kMaxUnpacked || loff + 30 > z.size() || R32(&z[loff]) != 0x04034b50) { err = "архив повреждён"; return false; }
        const size_t data = loff + 30 + R16(&z[loff + 26]) + R16(&z[loff + 28]);
        if (data + csize > z.size()) { err = "архив повреждён"; return false; }
        std::vector<uint8_t> out(usize);
        if (method == 0 && csize == usize) memcpy(out.data(), &z[data], usize);
        else if (method != 8 || !Inflate(&z[data], csize, out)) { err = "не удалось распаковать " + name; return false; }
        FILE* f = fopen((dir + "/" + name).c_str(), "wb");
        if (!f) { err = "не удалось записать " + name; return false; }
        const bool ok = fwrite(out.data(), 1, out.size(), f) == out.size();
        if (fclose(f) != 0 || !ok) { err = "нет места для " + name; return false; }
    }
    return true;
}

// ---- http -----------------------------------------------------------------------------------------------------------------------
struct Sink { std::string* mem; FILE* file; size_t got; };
size_t OnData(char* p, size_t s, size_t n, void* u) {
    auto* k = static_cast<Sink*>(u);
    const size_t len = s * n;
    k->got += len;
    if (k->mem) {
        if (k->got > kMaxManifest) return 0;
        k->mem->append(p, len);
        return len;
    }
    return fwrite(p, 1, len, k->file) == len ? len : 0;
}
int OnProgress(void*, curl_off_t total, curl_off_t now, curl_off_t, curl_off_t) {
    if (total > 0) {
        std::lock_guard<std::mutex> lk(g_mu);
        g_st.pct = (int)(now * 90 / total);  // the rest is the check and the unpacking
    }
    return 0;
}
bool Fetch(const std::string& url, Sink& sink, bool progress, std::string& err) {
    CURL* c = curl_easy_init();
    if (!c) { err = "curl"; return false; }
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 20L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1024L);  // stalled for 30 s = give up
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 30L);
    curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, OnData);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &sink);
    if (progress) {
        curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, OnProgress);
    }
    const CURLcode rc = curl_easy_perform(c);
    curl_easy_cleanup(c);
    if (rc != CURLE_OK) { err = std::string("нет связи: ") + curl_easy_strerror(rc); return false; }
    return true;
}

// The value of "key" (a string or a number) first met after `from` and before `to` in the manifest text.
std::string Field(const std::string& m, size_t from, size_t to, const char* key) {
    const size_t k = m.find(std::string("\"") + key + "\"", from);
    if (k == std::string::npos || k >= to) return "";
    size_t v = m.find(':', k) + 1;
    while (v < m.size() && (m[v] == ' ' || m[v] == '\n')) v++;
    if (m[v] == '"') { const size_t q = m.find('"', v + 1); return m.substr(v + 1, q - v - 1); }
    const size_t q = m.find_first_of(",\n}", v);
    return m.substr(v, q - v);
}

void Run(std::string name, std::string dir) {
    std::string err, man;
    Set(1, 0, "Подготовка...");
    Sink ms{&man, nullptr, 0};
    if (!Fetch(std::string(kBase) + "manifest.json", ms, false, err)) { Set(3, 0, err); g_busy = false; return; }
    const size_t b = man.find("\"name\": \"" + name + "\"");
    if (b == std::string::npos) { Set(3, 0, "образа нет в каталоге"); g_busy = false; return; }
    size_t end = man.find("\"updated_at\"", man.find("\"updated_at\"", b) + 1);  // the next bundle's (this one's own comes right after its name)
    if (end == std::string::npos) end = man.size();
    const std::string sha = Field(man, b, end, "archive_sha256"), path = Field(man, b, end, "archive_path"), primary = Field(man, b, end, "primary"), board = Field(man, b, end, "id"), label = Field(man, man.find("\"os\"", b), end, "name");
    const size_t size = strtoull(Field(man, b, end, "archive_size").c_str(), nullptr, 10);
    if (sha.size() != 64 || path.empty() || primary.empty() || path.find("..") != std::string::npos) { Set(3, 0, "каталог повреждён"); g_busy = false; return; }

    const std::string out = dir + "/" + name;
    mkdir(dir.c_str(), 0755);
    mkdir(out.c_str(), 0755);
    const std::string zip = out + "/.rom.zip";
    Set(1, 0, "Загрузка...");
    FILE* f = fopen(zip.c_str(), "wb");
    if (!f) { Set(3, 0, "нельзя записать в " + out); g_busy = false; return; }
    Sink fs{nullptr, f, 0};
    const bool got = Fetch(std::string(kBase) + path, fs, true, err);
    fclose(f);
    if (!got) { remove(zip.c_str()); Set(3, 0, err); g_busy = false; return; }
    Set(1, 90, "Проверка...");
    if (fs.got != size || romdb::Sha256File(zip) != sha) { remove(zip.c_str()); Set(3, 0, "контрольная сумма не совпала"); g_busy = false; return; }
    Set(1, 95, "Распаковка...");
    std::vector<uint8_t> z;
    if (FILE* r = fopen(zip.c_str(), "rb")) {
        z.resize(size);
        if (fread(z.data(), 1, size, r) != size) z.clear();
        fclose(r);
    }
    const bool ok = !z.empty() && Unzip(z, out, err);
    remove(zip.c_str());
    if (!ok) { Set(3, 0, err.empty() ? "не удалось прочитать архив" : err); g_busy = false; return; }
    {
        std::lock_guard<std::mutex> lk(g_mu);
        g_st = State{2, 100, "Готово", out + "/" + primary, board, label};
    }
    g_busy = false;
}
}  // namespace

bool UnzipFile(const std::string& zip, const std::string& dir, const char* only_ext, std::string& err) {
    std::vector<uint8_t> z;
    if (FILE* r = fopen(zip.c_str(), "rb")) {
        fseek(r, 0, SEEK_END);
        const long n = ftell(r);
        fseek(r, 0, SEEK_SET);
        if (n > 0) { z.resize(n); if (fread(z.data(), 1, z.size(), r) != z.size()) z.clear(); }
        fclose(r);
    }
    if (z.empty()) { err = "не удалось прочитать архив"; return false; }
    return Unzip(z, dir, err, only_ext);
}

void Start(const std::string& name, const std::string& dir) {
    if (g_busy.exchange(true)) return;
    Set(1, 0, "");
    std::thread(Run, name, dir).detach();
}

State Get() {
    std::lock_guard<std::mutex> lk(g_mu);
    return g_st;
}

void Reset() {
    if (g_busy) return;
    Set(0, 0, "");
}
}  // namespace cewm::romdl
