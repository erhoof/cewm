#pragma once

#include <winsock2.h>
#include <vector>

#include <slirp/libslirp.h>

/* Opaque context passed to libslirp's poll-fill / get-revents callbacks.
   We own the WSAPOLLFD vector; libslirp identifies entries by returned
   index. */
struct PollFillCtx {
    std::vector<WSAPOLLFD>* fds;
};

/* libslirp v6 poll-fill callback - called once per watched socket. */
int cb_add_poll_socket(slirp_os_socket fd, int events, void* opaque);

/* libslirp v6 get-revents callback - libslirp asks us what events fired
   on the entry it previously registered at `idx`. */
int cb_get_revents(int idx, void* opaque);
