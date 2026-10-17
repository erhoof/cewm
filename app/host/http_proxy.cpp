// sslstrip-style HTTP proxy for the guest: it asks for plain http://, the host fetches https:// (falls back to http) and hands back
// plain http with https links rewritten. Reached from the guest as 10.0.2.2:<port> (slirp maps that to host loopback).
#include "http_proxy.h"
#include <arpa/inet.h>
#include <curl/curl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace {
constexpr size_t kMaxBody = 16u << 20;
constexpr size_t kMaxHead = 64u << 10;

std::mutex g_mu;
std::set<std::string> g_plain;  // hosts that failed https; go straight to http next time

struct Reply {
    std::string head, body;
};

size_t OnBody(char* p, size_t s, size_t n, void* u) {
    auto* r = static_cast<Reply*>(u);
    if (r->body.size() + s * n > kMaxBody) return 0;
    r->body.append(p, s * n);
    return s * n;
}
size_t OnHead(char* p, size_t s, size_t n, void* u) {
    auto* r = static_cast<Reply*>(u);
    const std::string line(p, s * n);
    if (line.rfind("HTTP/", 0) == 0) r->head.clear();  // a redirect hop: keep only the last response's headers
    r->head += line;
    return s * n;
}

bool IEq(const std::string& a, const char* b) {
    size_t n = strlen(b);
    if (a.size() < n) return false;
    for (size_t i = 0; i < n; i++) if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return false;
    return true;
}

void ReplaceAll(std::string& s, const std::string& from, const std::string& to) {
    for (size_t p = 0; (p = s.find(from, p)) != std::string::npos; p += to.size()) s.replace(p, from.size(), to);
}

// scheme-less "host/path?q" of a URL
std::string Bare(const std::string& u) {
    size_t p = u.find("://");
    return p == std::string::npos ? u : u.substr(p + 3);
}

void Send(int fd, const std::string& s) {
    for (size_t o = 0; o < s.size();) {
        ssize_t n = send(fd, s.data() + o, s.size() - o, MSG_NOSIGNAL);
        if (n <= 0) return;
        o += n;
    }
}

void Fail(int fd, int code, const char* why) {
    std::string b = std::string("<html><body><h3>cewm proxy: ") + why + "</h3></body></html>";
    Send(fd, "HTTP/1.1 " + std::to_string(code) + " Error\r\nContent-Type: text/html\r\nContent-Length: " + std::to_string(b.size()) +
                 "\r\nConnection: close\r\n\r\n" + b);
}

// Fetches `url`; returns CURLE_OK with the final response in `out` and the post-redirect URL in `eff`.
CURLcode Fetch(const std::string& url, const std::string& method, const std::vector<std::string>& hdrs, const std::string& body, Reply& out,
               std::string& eff, long& code) {
    CURL* c = curl_easy_init();
    curl_slist* l = nullptr;
    for (auto& h : hdrs) l = curl_slist_append(l, h.c_str());
    out = Reply();
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, l);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 10L);
    curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");  // curl decodes gzip/br so the body can be rewritten
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, OnBody);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &out);
    curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, OnHead);
    curl_easy_setopt(c, CURLOPT_HEADERDATA, &out);
    if (method == "HEAD") curl_easy_setopt(c, CURLOPT_NOBODY, 1L);
    else if (method == "POST") {
        curl_easy_setopt(c, CURLOPT_POST, 1L);
        curl_easy_setopt(c, CURLOPT_POSTFIELDS, body.data());
        curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)body.size());
    }
    CURLcode rc = curl_easy_perform(c);
    char* e = nullptr;
    curl_easy_getinfo(c, CURLINFO_EFFECTIVE_URL, &e);
    eff = e ? e : url;
    code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    curl_slist_free_all(l);
    curl_easy_cleanup(c);
    return rc;
}

void Serve(int fd) {
    std::string in;
    char buf[4096];
    size_t hend;
    while ((hend = in.find("\r\n\r\n")) == std::string::npos) {
        ssize_t n = recv(fd, buf, sizeof buf, 0);
        if (n <= 0 || in.size() > kMaxHead) { close(fd); return; }
        in.append(buf, n);
    }
    fprintf(stderr, "[PROXY] request: %.80s\n", in.c_str());
    const std::string head = in.substr(0, hend);
    std::string body = in.substr(hend + 4);
    const size_t le = head.find("\r\n");
    const std::string rl = head.substr(0, le);
    const size_t s1 = rl.find(' '), s2 = rl.rfind(' ');
    if (s1 == std::string::npos || s2 <= s1) { Fail(fd, 400, "bad request"); close(fd); return; }
    const std::string method = rl.substr(0, s1), target = rl.substr(s1 + 1, s2 - s1 - 1);

    if (method == "CONNECT") { Fail(fd, 403, "https tunnels are not supported, use http:// and the proxy upgrades it"); close(fd); return; }
    if (target.rfind("http://", 0) != 0 && target.rfind("https://", 0) != 0) { Fail(fd, 400, "proxy requests only"); close(fd); return; }

    std::vector<std::string> hdrs;
    size_t clen = 0;
    for (size_t p = le == std::string::npos ? head.size() : le + 2; p < head.size();) {
        size_t e = head.find("\r\n", p);
        if (e == std::string::npos) e = head.size();
        std::string h = head.substr(p, e - p);
        p = e + 2;
        if (IEq(h, "content-length:")) clen = strtoul(h.c_str() + 15, nullptr, 10);
        // Hop-by-hop and encoding headers are dropped; Origin/Referer are upgraded to https like the target.
        if (IEq(h, "proxy-") || IEq(h, "connection:") || IEq(h, "accept-encoding:") || IEq(h, "content-length:") || IEq(h, "host:") ||
            IEq(h, "keep-alive:") || IEq(h, "if-none-match:") || IEq(h, "if-modified-since:"))
            continue;
        if (IEq(h, "referer:") || IEq(h, "origin:")) ReplaceAll(h, "http://", "https://");
        hdrs.push_back(h);
    }
    if (clen > kMaxBody) { Fail(fd, 413, "body too large"); close(fd); return; }
    while (body.size() < clen) {
        ssize_t n = recv(fd, buf, sizeof buf, 0);
        if (n <= 0) { close(fd); return; }
        body.append(buf, n);
    }
    body.resize(clen);

    const std::string bare = Bare(target);
    const std::string host = bare.substr(0, bare.find_first_of("/?:"));
    bool plain;
    { std::lock_guard<std::mutex> g(g_mu); plain = g_plain.count(host) != 0; }
    Reply r;
    std::string eff;
    long code = 0;
    CURLcode rc = plain ? CURLE_COULDNT_CONNECT : Fetch("https://" + bare, method, hdrs, body, r, eff, code);
    if (rc != CURLE_OK && code == 0) {  // no response over https at all: try plain http and remember it
        rc = Fetch("http://" + bare, method, hdrs, body, r, eff, code);
        if (rc == CURLE_OK) { std::lock_guard<std::mutex> g(g_mu); g_plain.insert(host); }
    }
    if (rc != CURLE_OK) { Fail(fd, 502, curl_easy_strerror(rc)); close(fd); return; }
    fprintf(stderr, "[PROXY] %s %s -> %ld %zu bytes\n", method.c_str(), target.c_str(), code, r.body.size());

    // Redirects were followed upstream (a plain "redirect to https" would loop through us); if the page moved, tell the browser so
    // its relative links resolve against the right URL.
    if (Bare(eff) != bare) {
        Send(fd, "HTTP/1.1 302 Found\r\nLocation: http://" + Bare(eff) + "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        close(fd);
        return;
    }

    std::string ctype, out;
    for (size_t p = r.head.find("\r\n"); p != std::string::npos && p + 2 < r.head.size();) {
        p += 2;
        size_t e = r.head.find("\r\n", p);
        if (e == std::string::npos) break;
        std::string h = r.head.substr(p, e - p);
        p = e;
        if (h.empty()) continue;
        if (IEq(h, "content-type:")) ctype = h;
        if (IEq(h, "content-length:") || IEq(h, "content-encoding:") || IEq(h, "transfer-encoding:") || IEq(h, "connection:") ||
            IEq(h, "strict-transport-security:") || IEq(h, "content-security-policy") || IEq(h, "alt-svc:") || IEq(h, "upgrade-insecure"))
            continue;
        if (IEq(h, "set-cookie:")) { ReplaceAll(h, "; Secure", ""); ReplaceAll(h, "; secure", ""); }
        ReplaceAll(h, "https://", "http://");
        out += h + "\r\n";
    }
    std::string lo = ctype;
    for (auto& ch : lo) ch = tolower((unsigned char)ch);
    if (lo.find("text/html") != std::string::npos || lo.find("text/css") != std::string::npos || lo.find("javascript") != std::string::npos ||
        lo.find("xml") != std::string::npos)
        ReplaceAll(r.body, "https://", "http://");
    Send(fd, "HTTP/1.1 " + std::to_string(code) + " OK\r\n" + out + "Content-Length: " + std::to_string(r.body.size()) + "\r\nConnection: close\r\n\r\n" +
                 (method == "HEAD" ? "" : r.body));
    close(fd);
}
}  // namespace

namespace cewm {
int StartHttpProxy(int port) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return -1;
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);  // slirp's 10.0.2.2 lands on loopback; nothing else on the LAN may use it
    a.sin_port = htons(port);
    if (bind(s, (sockaddr*)&a, sizeof a) < 0 || listen(s, 16) < 0) { close(s); return -1; }
    socklen_t len = sizeof a;
    getsockname(s, (sockaddr*)&a, &len);
    curl_global_init(CURL_GLOBAL_DEFAULT);
    std::thread([s] {
        for (;;) {
            int c = accept(s, nullptr, nullptr);
            if (c < 0) continue;
            std::thread(Serve, c).detach();  // shortcut: thread per connection, no cap; fine for one guest
        }
    }).detach();
    return ntohs(a.sin_port);
}
}  // namespace cewm
