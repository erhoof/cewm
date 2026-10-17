// The Windows build answers guest pings through IcmpSendEcho; here the echo is left to libslirp's own ICMP handling.
#include "net/slirp_backend.h"

bool SlirpBackend::TryInterceptIcmpEcho(const uint8_t*, std::size_t) { return false; }
void SlirpBackend::JoinIcmpThreads() {}
