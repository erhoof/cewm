#pragma once

#include "host_state.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>

/* Fake GSM modem AT answers fed from the host's radio state (cewm::Host()); shared by every board's modem line. */
namespace fakeat {

/* Radio state from the phone (ofono via device_stats): signal 0..100, -1 = no network. CEWM_SIGNAL=<0..100> forces a value for laptop tests. */
inline int RadioSignal() {
    int s = cewm::Host().cell_signal.load();
    if (s < 0) if (const char* e = getenv("CEWM_SIGNAL")) s = atoi(e);
    if (s < 0) if (const char* f = getenv("CEWM_SIGNAL_FILE")) {  /* test hook: change the signal while the guest runs */
        if (FILE* fp = fopen(f, "r")) { int v = -1; if (fscanf(fp, "%d", &v) == 1) s = v; fclose(fp); }
    }
    return s;
}
/* Signal indicator (+CIND/+CIEV 0..5) for a host strength 0..100. The ROM draws indicator 0-2 as no bars, 3 as one, 4 as three, 5 as five
   (measured on the laptop), so any real signal is mapped to at least 3. */
inline int RadioBars(int sig) {
    if (sig < 0 || sig == 0) return 0;
    return sig <= 60 ? 3 : sig <= 85 ? 4 : 5;
}
/* Network type for the status bar (G / E / 3G / H) as the AcT field of +CREG/+CGREG; CEWM_ACT=<n> forces it for laptop tests. */
inline int RadioAct() {
    int a = cewm::Host().cell_act.load();
    if (a < 0) if (const char* e = getenv("CEWM_ACT")) a = atoi(e);
    return a;
}
inline std::string ActSuffix() { const int a = RadioAct(); return a < 0 ? "" : "," + std::to_string(a); }
inline std::string RadioPlmn() {
    std::lock_guard<std::mutex> lk(cewm::Host().info_mu);
    if (cewm::Host().operator_plmn.empty()) { const char* e = getenv("CEWM_PLMN"); return e ? e : "310150"; }
    return cewm::Host().operator_plmn;
}
inline std::string RadioName() {
    std::lock_guard<std::mutex> lk(cewm::Host().info_mu);
    const std::string& n = cewm::Host().operator_name;
    if (n.empty()) return "CE Mobile";
    /* The SIM/RIL name path is single-byte: transliterate Cyrillic (UTF-8) to Latin, drop what is left non-ASCII. */
    static const char* const kRu[64] = {"A","B","V","G","D","E","Zh","Z","I","I","K","L","M","N","O","P","R","S","T","U","F","H","C","Ch","Sh","Sh","","Y","","E","Yu","Ya",
        "a","b","v","g","d","e","zh","z","i","i","k","l","m","n","o","p","r","s","t","u","f","h","c","ch","sh","sh","","y","","e","yu","ya"};
    std::string o;
    for (size_t i = 0; i < n.size(); i++) {
        unsigned char c = n[i];
        if (c < 0x80) o += (char)c;
        else if (c == 0xD0 && i + 1 < n.size() && (unsigned char)n[i + 1] >= 0x90 && (unsigned char)n[i + 1] <= 0xBF) o += kRu[(unsigned char)n[++i] - 0x90];
        else if (c == 0xD1 && i + 1 < n.size() && (unsigned char)n[i + 1] >= 0x80 && (unsigned char)n[i + 1] <= 0x8F) o += kRu[(unsigned char)n[++i] - 0x80 + 48];
        else if (c >= 0xC0) while (i + 1 < n.size() && ((unsigned char)n[i + 1] & 0xC0) == 0x80) i++;
    }
    return o.empty() ? "CE Mobile" : o;
}

/* The RIL selects CSCS="UCS2", so alphanumeric operator names in +COPS come as UCS2 hex (the host's UTF-8 name, Cyrillic included). */
inline std::string RadioNameUcs2() {
    std::string n;
    { std::lock_guard<std::mutex> lk(cewm::Host().info_mu); n = cewm::Host().operator_name; }
    if (n.empty()) n = "CE Mobile";
    std::string hex;
    char b[8];
    for (size_t i = 0; i < n.size();) {
        unsigned char c = n[i];
        uint32_t cp = c;
        size_t len = 1;
        if (c >= 0xF0 && i + 3 < n.size()) { cp = (c & 7u) << 18 | (n[i + 1] & 0x3Fu) << 12 | (n[i + 2] & 0x3Fu) << 6 | (n[i + 3] & 0x3Fu); len = 4; }
        else if (c >= 0xE0 && i + 2 < n.size()) { cp = (c & 15u) << 12 | (n[i + 1] & 0x3Fu) << 6 | (n[i + 2] & 0x3Fu); len = 3; }
        else if (c >= 0xC0 && i + 1 < n.size()) { cp = (c & 31u) << 6 | (n[i + 1] & 0x3Fu); len = 2; }
        i += len;
        if (cp >= 0x10000) { cp -= 0x10000; snprintf(b, sizeof b, "%04X", 0xD800 + (cp >> 10)); hex += b; cp = 0xDC00 + (cp & 0x3FF); }
        snprintf(b, sizeof b, "%04X", cp);
        hex += b;
    }
    return hex;
}

/* One AT command (without the "AT" prefix) -> information text, or "" . Query/set forms follow 3GPP TS 27.007 as the RIL uses them. */
inline std::string AtInfo(const std::string& c) {
    auto is = [&](const char* p) { return c.rfind(p, 0) == 0; };
    const int sig = RadioSignal();
    const int bars = RadioBars(sig);
    const int rssi = sig < 0 ? 99 : sig * 31 / 100;      /* 0..31, 99 = unknown (the ROM does not draw bars from this) */
    const char* stat = sig < 0 ? "0" : "1";              /* registered / not registered */
    if (is("+CSQ"))   return "\r\n+CSQ: " + std::to_string(rssi) + ",99\r\n";
    if (is("+CPIN?")) return getenv("CEWM_CPIN") ? std::string("\r\n") + getenv("CEWM_CPIN") + "\r\n" : std::string("\r\n+CPIN: READY\r\n");
    // The SIM is unlocked and fixed dialing is disabled. Query mode needs an
    // information result; a bare OK makes the P525 RIL reject the lock status.
    if (is("+CLCK=\"FD\",2") || is("+CLCK=\"SC\",2")) return "\r\n+CLCK: 0\r\n";
    if (is("+CFUN?")) return "\r\n+CFUN: 1\r\n";
    if (is("+CREG?")) return std::string("\r\n+CREG: 2,") + stat + (sig < 0 ? "\r\n" : ",\"0001\",\"0001\"\r\n");
    if (is("+CGREG?")) return std::string("\r\n+CGREG: 1,") + stat + (sig < 0 ? "\r\n" : ",\"0001\",\"0001\"\r\n");
    static int cops_fmt = 2;  /* +COPS=3,<fmt> selects numeric (2) or long alphanumeric (0) operator names */
    if (is("+COPS=3,")) { cops_fmt = c.size() > 8 ? c[8] - '0' : 2; return ""; }
    if (is("+COPS?")) {
        if (sig < 0) return "\r\n+COPS: 0\r\n";
        return cops_fmt == 0 ? "\r\n+COPS: 0,0,\"" + RadioNameUcs2() + "\"\r\n" : "\r\n+COPS: 0,2,\"" + RadioPlmn() + "\"\r\n";
    }
    if (is("+CRSM=")) {  /* SIM file access: serve the CPHS operator name (EF 6F14 = 28436) from the host's registered operator, else "file not found" */
        int cmd = 0, file = 0;
        if (sscanf(c.c_str(), "+CRSM=%d,%d", &cmd, &file) == 2 && file == 28436) {
            if (cmd == 192) return "\r\n+CRSM: 144,0,\"000000146F14040011F0FF01020000\"\r\n";  /* GET RESPONSE: 20-byte transparent EF */
            if (cmd == 176) {                                                                       /* READ BINARY: name in GSM bytes, FF padded */
                std::string nm = RadioName(), hex;
                char b[4];
                for (size_t i = 0; i < 20; i++) { snprintf(b, sizeof b, "%02x", i < nm.size() ? (unsigned)(unsigned char)nm[i] : 0xFFu); hex += b; }
                return "\r\n+CRSM: 144,0,\"" + hex + "\"\r\n";
            }
        }
        if (cmd == 176 && file == 28486 && !getenv("CEWM_CRSMNF")) {  /* EF SPN: display-condition byte then 16 name bytes, FF padded */
            std::string nm = RadioName(), hex = "00";
            char b[4];
            bool ascii = true;
            { std::lock_guard<std::mutex> lk(cewm::Host().info_mu); for (unsigned char ch : cewm::Host().operator_name) ascii &= ch < 0x80; }
            if (!ascii) {  /* 0x80 flags UCS2 big-endian: up to seven characters fit the 16 data bytes */
                std::string u = RadioNameUcs2().substr(0, 28);
                hex = "0080" + u;
                for (size_t i = u.size() / 2; i < 15; i++) hex += "ff";
                return "\r\n+CRSM: 144,0," + hex + "\r\n";
            }
            for (size_t i = 0; i < 16; i++) { snprintf(b, sizeof b, "%02x", i < nm.size() ? (unsigned)(unsigned char)nm[i] : 0xFFu); hex += b; }
            return "\r\n+CRSM: 144,0," + hex + "\r\n";  /* the RIL reads this hex unquoted (a quote shifts every byte by a nibble) */
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
    if (is("+XGENDATA")) { const char* x = getenv("CEWM_XGEN"); if (x) return std::string("\r\n") + x + "\r\n"; }
    if (is("+CGSN") || is("+GSN")) return "\r\n356938035643809\r\n";
    if (is("+CIMI"))  return "\r\n310150123456789\r\n";
    return "";
}

/* A full command line ("AT..." possibly compound with ';') -> bytes for the guest. v0/e0 are the per-link ATV/ATE settings. */
inline std::string AtAnswer(const std::string& line, bool& v0, bool& e0) {
    std::string l;
    for (char ch : line) l.push_back((char)toupper((unsigned char)ch));
    if (l.rfind("AT", 0) != 0) return "";
    std::string out = e0 ? "" : line + "\r";
    std::string rest = l.substr(2);
    /* Basic-command settings E<n>/V<n> inside a compound line ("ATZS0=0E0Q0V0X4&C1&W+CRC=1;...") apply before the extended part. */
    const std::string basic = rest.substr(0, rest.find('+'));
    for (size_t i = 0; i + 1 < basic.size(); i++) {
        if (!isdigit((unsigned char)basic[i + 1]) || (i && basic[i - 1] == '=')) continue;
        if (basic[i] == 'E') e0 = basic[i + 1] == '0';
        if (basic[i] == 'V') v0 = basic[i + 1] == '0';
    }
    if (basic.size() > 2) out = e0 ? "" : line + "\r";
    if (rest.empty() || rest == "V0" || rest == "V1" || rest == "E0" || rest == "E1") {
        if (rest == "V0") v0 = true; else if (rest == "V1") v0 = false;
        else if (rest == "E0") { e0 = true; out.clear(); } else if (rest == "E1") e0 = false;
    } else {
        for (size_t pos = 0; pos <= rest.size();) {
            size_t end = rest.find(';', pos);
            if (end == std::string::npos) end = rest.size();
            std::string info = AtInfo(rest.substr(pos, end - pos));
            if (v0 && getenv("CEWM_V0STRIP") && info.rfind("\r\n", 0) == 0) info.erase(0, 2);  /* V.250: numeric mode has no leading CR LF */
            out += info;
            pos = end + 1;
        }
    }
    if (v0 && getenv("CEWM_V0NL")) { std::string r; for (char ch : out) if (ch != '\n') r += ch; out = r; }
    if (const char* ce = getenv("CEWM_CRSMERR")) if (rest.rfind("+CRSM=", 0) == 0) return std::string("\r\n") + ce + "\r\n";
    if (getenv("CEWM_FORCEV1")) v0 = false;
    if (v0 && getenv("CEWM_FINAL")) return out + getenv("CEWM_FINAL");
    return out + (v0 ? "0\r" : "\r\nOK\r\n");
}


}  /* namespace fakeat */
