#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>

#include "slirp_poll_shim.h"

namespace {

/* Translate libslirp SLIRP_POLL_* event bits ↔ Windows WSAPOLLFD events.
   SLIRP_POLL_ERR / SLIRP_POLL_HUP are output-only (revents); WSAPoll
   reports POLLERR/POLLHUP unconditionally - no input bit needed. */
short SlirpEventsToWsa(int slirp_events) {
    short out = 0;
    if (slirp_events & SLIRP_POLL_IN)  out |= POLLRDNORM;
    if (slirp_events & SLIRP_POLL_OUT) out |= POLLWRNORM;
    if (slirp_events & SLIRP_POLL_PRI) out |= POLLRDBAND;
    return out;
}

int WsaEventsToSlirp(short wsa_revents) {
    int out = 0;
    if (wsa_revents & POLLRDNORM) out |= SLIRP_POLL_IN;
    if (wsa_revents & POLLWRNORM) out |= SLIRP_POLL_OUT;
    if (wsa_revents & POLLRDBAND) out |= SLIRP_POLL_PRI;
    if (wsa_revents & POLLERR)    out |= SLIRP_POLL_ERR;
    if (wsa_revents & POLLHUP)    out |= SLIRP_POLL_HUP;
    return out;
}

} /* namespace */

int cb_add_poll_socket(slirp_os_socket fd, int events, void* opaque) {
    auto* ctx = static_cast<PollFillCtx*>(opaque);
    WSAPOLLFD pfd{};
    pfd.fd      = fd;        /* slirp_os_socket == SOCKET on Windows; no truncation */
    pfd.events  = SlirpEventsToWsa(events);
    pfd.revents = 0;
    ctx->fds->push_back(pfd);
    return (int)ctx->fds->size() - 1;
}

int cb_get_revents(int idx, void* opaque) {
    auto* ctx = static_cast<PollFillCtx*>(opaque);
    if (idx < 0 || idx >= (int)ctx->fds->size()) return 0;
    return WsaEventsToSlirp((*ctx->fds)[idx].revents);
}
