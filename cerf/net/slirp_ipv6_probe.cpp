#define NOMINMAX
#define _CRT_SECURE_NO_WARNINGS

#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstring>

#include "../core/log.h"

bool ProbeHostIpv6Reachable() {
    LOG(Net, "IPv6 probe: testing public v6 reachability via TCP connect "
             "to 2606:4700:4700::1111:53 (Cloudflare DNS anycast, 1.5s timeout); "
             "result controls whether libslirp advertises itself as the guest's "
             "IPv6 default router and whether AAAA DNS records are passed through "
             "to the guest...\n");
    SOCKET s = ::socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        LOG(Net, "IPv6 probe: socket(AF_INET6) failed (err=%d) -> no v6\n",
            WSAGetLastError());
        return false;
    }
    u_long nonblock = 1;
    ioctlsocket(s, FIONBIO, &nonblock);
    sockaddr_in6 dst{};
    dst.sin6_family = AF_INET6;
    dst.sin6_port   = htons(53);
    static const uint8_t cloudflare_v6[16] = {
        0x26, 0x06, 0x47, 0x00, 0x47, 0x00, 0, 0,
        0, 0, 0, 0, 0, 0, 0x11, 0x11
    };
    std::memcpy(&dst.sin6_addr, cloudflare_v6, 16);
    int rc  = ::connect(s, (sockaddr*)&dst, sizeof(dst));
    int err = (rc == 0) ? 0 : WSAGetLastError();
    bool reachable = false;
    if (rc == 0) {
        reachable = true;
    } else if (err == WSAEWOULDBLOCK) {
        WSAPOLLFD pfd{};
        pfd.fd = s;
        pfd.events = POLLWRNORM;
        int pr = WSAPoll(&pfd, 1, 1500 /* ms */);
        if (pr > 0 && (pfd.revents & POLLWRNORM) &&
            !(pfd.revents & (POLLERR | POLLHUP))) {
            int so_err = 0;
            int len = (int)sizeof(so_err);
            if (getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&so_err, &len) == 0
                && so_err == 0) {
                reachable = true;
            } else {
                err = so_err;
            }
        }
    }
    closesocket(s);
    LOG(Net, "IPv6 probe -> 2606:4700:4700::1111:53 = %s (err=%d)\n",
        reachable ? "REACHABLE" : "UNREACHABLE", reachable ? 0 : err);
    return reachable;
}
